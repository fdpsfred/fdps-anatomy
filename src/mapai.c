/* mapai.c -- the map AI: one computer-controlled actor's behaviour on its
 * turn, and the map queries that decision needs.
 *
 * See mapai.h for what each entry point promises.  Nothing here owns state:
 * the map layers, the movement grid and the unit records all belong to other
 * files, and everything below works on what those pointers hold.
 */
#include <stdlib.h>
#include "gamedata.h"
#include "fdpstype.h"
#include "aiact.h"
#include "aiscore.h"
#include "anim.h"
#include "audio.h"
#include "btlturn.h"
#include "chevt6.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "maptile.h"
#include "movegrid.h"
#include "table.h"
#include "unit.h"
#include "unitatk.h"
#include "unititem.h"
#include "mapai.h"

/* The two values this handler stores into data_fdps_map_cursor_draw_mode
   (gamedata.h): 0 at 0001276d so nothing is painted while the actor walks, and
   1 at 000127a6 afterwards.  THE SECOND IS A PLAIN STORE AND NOT A RESTORE --
   MOV dword ptr [0x00069cd0],0x1, with the entry value never read -- so a
   caller that was in any other mode comes back in the ordinary box mode. */
#define CURSOR_DRAW_MODE_HIDDEN 0
#define CURSOR_DRAW_MODE_BOX 1

/* The score a search has to reach before the behaviour dispatcher spends the
   actor's turn on it: CMP dword ptr [0x00063f8c],0x6 at 000105e9 and the same
   compare against the physical score at 00010612 and 000106bc and against the
   spell score at 00010693.  All four are JL, the signed compare, and the three
   scores are tiers on one scale (gamedata.h). */
#define MAP_AI_ACTION_SCORE_THRESHOLD 6

/* The occasion fdps_map_set_pending_tile_event (maptile.h) is told about at
   the end of a turn, PUSH 0x1 at 00010706: the actor has ENDED ITS TURN on the
   tile, not stepped onto it. */
#define TILE_EVENT_ON_TURN_END 1

/* Where the map block's per-event-code result table sits and how wide an entry
   is: base + slot * 3 + 0x53 for the opcode byte and + 0x54 for the operand
   word, from MOV EDX,[0x0006013c] / LEA EAX,[EAX+EAX*2] / MOV AL,byte ptr
   [EDX+0x53] at 0001032e and the MOVSX word ptr [EAX+0x54] at 00010352.  The
   player's own search action reads the same three bytes of the same entry at
   000185f2..0001861a. */
#define CHEST_RESULT_TABLE_BASE 0x53
#define CHEST_RESULT_ENTRY_STRIDE 3

/* The two opcodes the chest result table carries that the actor records, and
   the one of those that also hands the item over on the spot.  CMP dword ptr
   [EBP-0x18],0x2 / JGE at 00010359 skips both stores for anything above 1, and
   CMP ...,0x0 / JNZ at 00010372 gates the give. */
#define CHEST_OPCODE_ITEM 0
#define CHEST_OPCODE_LIMIT 2

/* The behaviour the actor is switched to once it has opened its chest, MOV
   byte ptr [EAX+0x34],0x7 at 0001039a: walk to the stored destination and
   retire on arrival.  The write is a whole byte, so the high nibble of 0x34
   goes to zero with it. */
#define AI_BEHAVIOR_WALK_THEN_RETIRE 7

/* The portrait ids that make an arriving behaviour-7 actor play "Posion.saf"
   before it retires: CMP EAX,0x24 / JL and CMP EAX,0x27 / JLE at 00010470 and
   00010480, on the zero-extended record byte 7. */
#define POISON_ANIM_PORTRAIT_FIRST 0x24
#define POISON_ANIM_PORTRAIT_LAST 0x27

/* The portrait ids that make a behaviour-11 actor run chapter 30's undead
   revival first: CMP EAX,0x3c / JL and CMP EAX,0x3e / JLE at 00010667 and
   00010677, on the same zero-extended record byte 7. */
#define UNDEAD_REVIVE_PORTRAIT_FIRST 0x3c
#define UNDEAD_REVIVE_PORTRAIT_LAST 0x3e

/* 00010010.  One computer-controlled actor's whole turn.
 *
 * Three things about it are behaviour rather than shape, and all three are
 * playtest contracts (rebuild_info/pitfalls.md).
 *
 * BEHAVIOUR 8 IS THE ONLY LIVE ARM THAT SKIPS THE TAIL.  CMP dword ptr
 * [EBP-0x14],0x8 / JZ 0x0001074e at 000104c1 jumps to the EPILOGUE, past the
 * turn-end tile event, past the redraw mark and past the frame.  Every other
 * value reaches the tail, INCLUDING the five -- 6, 12, 13, 14 and 15 -- that
 * match no arm at all and fall off the end of the chain having done nothing.
 * Folding 8 in with those five as "another value that does nothing" gives an
 * idle actor a tile event and a redrawn frame it never had.
 *
 * THE BEHAVIOUR IS THE LOW NIBBLE AND NOTHING ELSE.  AND AL,0xf at 00010065.
 * The high nibble of record byte 0x34 rides along untouched and is not part of
 * any comparison here, so an actor whose byte reads 0xf8 is behaviour 8.
 *
 * THE STORES TO data_fdps_map_cursor_draw_mode ARE ZERO AND NEVER A RESTORE.
 * Behaviours 3, 4, 5 and 7 write 0 (00010239, 0001029e, 000103f1) or, in
 * behaviour 3's chase arm, write it AFTER the walk at 000101ee.  None of the
 * four reads the entry value and none of them puts anything back, so an actor
 * that took one of those arms leaves the cursor overlay switched off for
 * whatever runs next.
 *
 * The destination pair and the event slot are latched from the record ONCE, at
 * 0001006f..0001008d, before any handler runs.  Behaviour 7's arrival test at
 * 0001043e compares the RE-READ record's tile against those latched values, so
 * a handler that moved the actor is measured against where it was told to go
 * and not against a destination it might have rewritten.
 *
 * Three stretches of this body are inline expansions and are emitted as the
 * calls they expand, which is what the rest of this file already does with
 * fdps_unit_is_retired: the retired gate at 00010031..00010052 replays
 * fdps_unit_is_retired (000109b0) instruction for instruction, the store at
 * 0001049b..000104b8 replays fdps_unit_mark_retired (000138f0), and the tail's
 * 00010727..00010745 replays fdps_battle_mark_unit_done (000119b0), which is
 * why the redraw bit is set through the unit array base rather than through
 * the record this function already holds.
 *
 * The actor record is fetched afresh after every handler that could have moved
 * the actor, because a handler may walk it and the array itself may move
 * (fdps_relocate_unit_array, unit.h). */
void fdps_map_actor_behavior_step(int actor_index, int side_select)
{
    /* The acting unit's record.  Re-fetched after each arm that walks it. */
    struct fdps_unit_record *actor;
    /* The record of the character behaviours 3 and 9 chase. */
    struct fdps_unit_record *chase_target;
    /* Where fdps_battle_find_unit_by_character_id publishes the record it
       matched.  It must not be null -- the callee writes it before anything
       else (unit.h) -- but nothing here ever reads it back. */
    struct fdps_unit_record *matched_record;
    /* The low nibble of record byte 0x34: which arm below runs. */
    int behavior_mode;
    /* The tile behaviours 4 and 7 walk to and the character id behaviours 3
       and 9 chase, latched from record bytes 0x35 and 0x36 on entry. */
    int dest_x;
    int dest_y;
    /* Record byte 0x3d: which map cell event this actor belongs to, and so
       which entry of the trigger table and of the chest result table is its
       own. */
    int event_slot;
    /* The unit index the chase found, or -1 for no such character. */
    int chase_index;
    /* Column then row of the chest behaviour 5 walks to, the two bytes
       fdps_map_find_chest_cell writes. */
    unsigned char chest_xy[2];
    /* The one-entry unit list the poison animation is played over. */
    unsigned char anim_unit_ids[1];
    /* The chest result entry: what the opened chest does, and the value it
       does it with.  Both are copied into the actor's death-script fields and
       the value is the item id when the opcode is 0. */
    int chest_script_opcode;
    int chest_script_operand;

    actor = fdps_get_unit_record(actor_index);
    if (fdps_unit_is_retired(actor_index) != 0) {
        return;
    }

    behavior_mode = (int) (actor->ai_behavior & 0x0f);
    dest_x = (int) actor->ai_dest_x;
    dest_y = (int) actor->ai_dest_y;
    event_slot = (int) actor->event_slot;

    if (behavior_mode == 0) {
        if (fdps_map_actor_take_best_action(actor_index, side_select) == 0 &&
            fdps_map_actor_move_toward_nearest_reachable_opponent(
                actor_index, side_select) == 0 &&
            fdps_map_actor_move_toward_nearest_opponent(actor_index,
                                                        side_select) == 0) {
            fdps_unit_rest(actor_index);
        }
    } else if (behavior_mode == 1) {
        if (fdps_map_actor_take_best_action(actor_index, side_select) == 0 &&
            fdps_map_actor_move_toward_nearest_reachable_opponent(
                actor_index, side_select) == 0) {
            fdps_unit_rest(actor_index);
        }
    } else if (behavior_mode == 2) {
        if (fdps_map_actor_take_best_action(actor_index, side_select) == 0 &&
            fdps_map_actor_score_best_attack(actor_index, side_select) == 0) {
            fdps_unit_rest(actor_index);
        }
    } else if (behavior_mode == 3) {
        if (fdps_map_actor_take_best_action(actor_index, side_select) == 0) {
            chase_index = fdps_battle_find_unit_by_character_id(
                              dest_x, &matched_record);
            if (chase_index == -1) {
                if (fdps_map_actor_move_toward_nearest_reachable_opponent(
                        actor_index, side_select) == 0 &&
                    fdps_map_actor_move_toward_nearest_opponent(
                        actor_index, side_select) == 0) {
                    fdps_unit_rest(actor_index);
                }
            } else {
                chase_target = fdps_get_unit_record(chase_index);
                fdps_map_cursor_move_to_unit(actor_index);
                if (fdps_battle_move_unit_toward((int) chase_target->pos_x,
                                                 (int) chase_target->pos_y,
                                                 actor_index,
                                                 side_select) == 0) {
                    fdps_unit_rest(actor_index);
                }
                data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
            }
        }
    } else if (behavior_mode == 4) {
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_map_cursor_move_to_unit(actor_index);
        if (fdps_battle_move_unit_toward(dest_x, dest_y, actor_index,
                                         side_select) == 0) {
            fdps_unit_rest(actor_index);
        }
    } else if (behavior_mode == 5) {
        if (fdps_map_actor_take_best_action(actor_index, side_select) == 0) {
            data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
            fdps_map_cursor_move_to_unit(actor_index);

            if (data_fdps_map_cell_event_triggered_flags[event_slot] == 0 &&
                fdps_map_find_chest_cell(event_slot, chest_xy) == 0) {
                if (fdps_battle_move_unit_toward((int) chest_xy[0],
                                                 (int) chest_xy[1],
                                                 actor_index,
                                                 side_select) == 0) {
                    fdps_unit_rest(actor_index);
                }

                actor = fdps_get_unit_record(actor_index);
                if (actor->pos_x == chest_xy[0] &&
                    actor->pos_y == chest_xy[1]) {
                    chest_script_opcode = (int)
                        data_fdps_tile_event_data_table_ptr[
                            event_slot * CHEST_RESULT_ENTRY_STRIDE +
                            CHEST_RESULT_TABLE_BASE];
                    chest_script_operand = (int) *(short *)
                        (data_fdps_tile_event_data_table_ptr +
                         event_slot * CHEST_RESULT_ENTRY_STRIDE +
                         CHEST_RESULT_TABLE_BASE + 1);

                    if (chest_script_opcode < CHEST_OPCODE_LIMIT) {
                        actor->death_script_opcode =
                            (unsigned char) chest_script_opcode;
                        actor->death_script_operand =
                            (short) chest_script_operand;
                        if (chest_script_opcode == CHEST_OPCODE_ITEM) {
                            fdps_unit_add_item(actor_index,
                                               chest_script_operand);
                        }
                    }

                    data_fdps_map_cell_event_triggered_flags[event_slot] = 1;
                    fdps_map_apply_triggered_cell_changes();
                    actor->ai_behavior = AI_BEHAVIOR_WALK_THEN_RETIRE;
                    fdps_play_sfx("Chess.wav");
                }
            } else {
                if (fdps_map_actor_move_toward_nearest_reachable_opponent(
                        actor_index, side_select) == 0 &&
                    fdps_map_actor_move_toward_nearest_opponent(
                        actor_index, side_select) == 0) {
                    fdps_unit_rest(actor_index);
                }
            }
        }
    } else if (behavior_mode == 7) {
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_map_cursor_move_to_unit(actor_index);
        if (fdps_battle_move_unit_toward(dest_x, dest_y, actor_index,
                                         side_select) == 0) {
            fdps_unit_rest(actor_index);
        }

        actor = fdps_get_unit_record(actor_index);
        if ((int) actor->pos_x == dest_x && (int) actor->pos_y == dest_y) {
            anim_unit_ids[0] = (unsigned char) actor_index;
            if ((int) actor->portrait_id >= POISON_ANIM_PORTRAIT_FIRST &&
                (int) actor->portrait_id <= POISON_ANIM_PORTRAIT_LAST) {
                fdps_play_vfs_animation_over_units(1, anim_unit_ids,
                                                   "Posion.saf");
            }
            fdps_unit_mark_retired(actor_index);
        }
    } else if (behavior_mode == 8) {
        return;
    } else if (behavior_mode == 9) {
        chase_index = fdps_battle_find_unit_by_character_id(dest_x,
                                                            &matched_record);
        if (chase_index == -1) {
            if (fdps_map_actor_take_best_action(actor_index,
                                                side_select) == 0 &&
                fdps_map_actor_move_toward_nearest_reachable_opponent(
                    actor_index, side_select) == 0 &&
                fdps_map_actor_move_toward_nearest_opponent(
                    actor_index, side_select) == 0) {
                fdps_unit_rest(actor_index);
            }
        } else {
            chase_target = fdps_get_unit_record(chase_index);
            fdps_map_cursor_move_to_unit(actor_index);
            if (fdps_battle_move_unit_toward((int) chase_target->pos_x,
                                             (int) chase_target->pos_y,
                                             actor_index, side_select) == 0 &&
                fdps_map_actor_take_best_action(actor_index,
                                                side_select) == 0 &&
                fdps_map_actor_move_toward_nearest_reachable_opponent(
                    actor_index, side_select) == 0 &&
                fdps_map_actor_move_toward_nearest_opponent(
                    actor_index, side_select) == 0) {
                fdps_unit_rest(actor_index);
            }
        }
    } else if (behavior_mode == 10) {
        fdps_map_actor_score_best_item(actor_index, side_select);
        if (data_fdps_battle_ai_best_item_score >=
                MAP_AI_ACTION_SCORE_THRESHOLD) {
            fdps_map_actor_use_item(actor_index, side_select);
        }
        fdps_map_actor_score_best_attack(actor_index, side_select);
        if (data_fdps_battle_ai_best_physical_score >=
                MAP_AI_ACTION_SCORE_THRESHOLD) {
            fdps_map_actor_move_and_attack(actor_index, side_select);
        } else if (fdps_map_actor_move_toward_nearest_reachable_opponent(
                       actor_index, side_select) == 0) {
            fdps_unit_rest(actor_index);
        }
    } else if (behavior_mode == 11) {
        if ((int) actor->portrait_id >= UNDEAD_REVIVE_PORTRAIT_FIRST &&
            (int) actor->portrait_id <= UNDEAD_REVIVE_PORTRAIT_LAST) {
            fdps_chapter_30_revive_wave_4_undead();
        }
        fdps_map_actor_score_best_spell(actor_index, side_select);
        if (data_fdps_battle_ai_best_spell_score >=
                MAP_AI_ACTION_SCORE_THRESHOLD) {
            fdps_map_actor_cast_chosen_spell(actor_index, side_select);
        }
        fdps_map_actor_score_best_attack(actor_index, side_select);
        if (data_fdps_battle_ai_best_physical_score >=
                MAP_AI_ACTION_SCORE_THRESHOLD) {
            fdps_map_actor_move_and_attack(actor_index, side_select);
        } else if (fdps_map_actor_move_toward_nearest_reachable_opponent(
                       actor_index, side_select) == 0) {
            fdps_unit_rest(actor_index);
        }
    }

    actor = fdps_get_unit_record(actor_index);
    fdps_map_set_pending_tile_event((int) actor->pos_x, (int) actor->pos_y,
                                    TILE_EVENT_ON_TURN_END);
    fdps_battle_mark_unit_done(actor_index);
    fdps_render_view_frame();
}

/* The allowance handed to the flood fill, PUSH 0x64 at 00012701.  It is not a
   unit's real movement allowance: it is large enough to cover any map in the
   game, so the fill spreads as far as the terrain lets the actor walk. */
#define MAP_AI_UNLIMITED_ALLOWANCE 100

/* fdps_move_path_trace's mode 2, the nearest-unit search (movegrid.h), pushed
   as a full dword at 00012717 and read by the callee as one byte. */
#define TRACE_MODE_NEAREST_UNIT 2

/* The side filter that search is given, PUSH 0x0 at 0001271d.  In mode 2 the
   tracer's start_x argument is a truth value and 0 selects the units whose
   side byte is NON-ZERO.  It is a hard-coded literal and NOT side_select: see
   the note on the function below. */
#define MAP_AI_TRACE_SIDE_FILTER 0

/* 000126b0.  Walks one actor toward the opposing unit nearest to it over
   walkable terrain rather than nearest in a straight line.

   THE CLASS RECORD IS FETCHED WITHOUT THE +1 EVERY NEIGHBOUR APPLIES.  MOV
   AL,byte ptr [EDX+0x20] / MOV [EBP-8],EAX / PUSH EAX at 000126e7..000126f5,
   with no INC anywhere between the load and the push, while
   fdps_battle_move_unit_toward at 00011a27 and fdps_map_cursor_select_tile at
   0002b7f8 both push the same record byte incremented.  Row 0 of PROMAP.DAT is
   the default row, so the raw code names the row belonging to the class one
   below the actor's and this search floods the map with the neighbouring
   class's terrain costs.  Writing the obvious +1 changes which unit the map AI
   walks at (rebuild_info/pitfalls.md).

   THE SIDE FILTER IS A HARD-CODED 0 AND NOT side_select.  PUSH 0x0 at
   0001271d, with side_select at [EBP+0x18] untouched until the move call at
   00012783.  On the NPC phase, where side_select is 1, the acting unit's own
   record therefore passes the filter, wins the search at cost 0 on its own
   tile, and the handler returns 0 having moved nothing.

   THE GRID IS TAKEN TO BE BLANK ON ENTRY.  No fdps_map_grid_reset runs before
   the fill and no zone-of-control pass runs at all, so what the fill relaxes
   is whatever the caller left behind; the reset comes afterwards, on both arms
   (00012748 and 0001273a), so nothing this function computed survives it.

   BOTH TILE BYTES WIDEN THROUGH XOR EAX,EAX / MOV AL, at 0001274d and 00012755,
   so a coordinate of 0x80 or above is 128 and never -128; the two compares that
   follow are equalities, so neither is a signedness test.

   The tile handed to fdps_battle_move_unit_toward is the target unit's OWN
   occupied tile, which no walk can end on: that function is what retargets the
   request to the reachable tile nearest it (movegrid.h), so a non-zero result
   means the actor moved, not that it arrived.

   The answer is 1 only when the move reported steps.  fdps_map_actor_behavior_step
   reads a 0 as "this handler did not claim the actor" and moves on to the next
   handler in that behaviour nibble's chain. */
int fdps_map_actor_move_toward_nearest_reachable_opponent(int unit_index,
                                                          int side_select)
{
    /* The acting unit's record inside the map unit array. */
    struct fdps_unit_record *actor;
    /* The tile the actor is standing on, record bytes +0 and +1. */
    int actor_x;
    int actor_y;
    /* The actor's class code, record byte +0x20, raw and not biased. */
    int actor_class;
    /* The PROMAP.DAT row the fill takes its per-terrain costs from. */
    struct fdps_class_record *class_move_cost;
    /* Where the search writes the winning unit's tile: column then row, two
       bytes, which is all fdps_move_path_trace's mode 2 stores. */
    unsigned char target_xy[2];
    /* The flooded cost of that unit's tile, or -1 when nothing matched. */
    int target_cost;
    /* The same tile widened out of the two bytes above. */
    int target_x;
    int target_y;
    /* The answer: 1 once the move has reported that it played a walk. */
    int moved;

    moved = 0;

    actor = fdps_get_unit_record(unit_index);
    actor_x = (int) actor->pos_x;
    actor_y = (int) actor->pos_y;
    actor_class = (int) actor->clazz;
    class_move_cost = fdps_get_class_record(actor_class);

    fdps_move_grid_flood_fill_range(class_move_cost, actor_x, actor_y,
                                    MAP_AI_UNLIMITED_ALLOWANCE);
    target_cost = fdps_move_path_trace(actor_x, actor_y, target_xy,
                                       MAP_AI_TRACE_SIDE_FILTER, 0,
                                       TRACE_MODE_NEAREST_UNIT);

    if (target_cost == -1) {
        fdps_map_grid_reset();
        return 0;
    }

    fdps_map_grid_reset();

    target_x = (int) target_xy[0];
    target_y = (int) target_xy[1];

    if (target_x != actor_x || target_y != actor_y) {
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_map_cursor_move_to_unit(unit_index);
        if (fdps_battle_move_unit_toward(target_x, target_y, unit_index,
                                         side_select) != 0) {
            moved = 1;
        }
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_BOX;
    }

    return moved;
}

/* The distance the straight-line search starts from, MOV dword ptr
   [EBP-0x24],0xffff at 000127cc.  It is 65535 and not -1: the compare that
   guards the replacement is JGE, the signed one, and the largest sum two byte
   coordinates can be apart is 510, so the first candidate always beats it. */
#define MAP_AI_NO_DISTANCE 0xffff

/* The tile that search starts from, MOV dword ptr [EBP-0x14],0xffffffff at
   000127d3, and the value CMP dword ptr [EBP-0x14],-0x1 at 000128df tests for.
   A candidate's column is widened out of a record byte through XOR EAX,EAX /
   MOV AL, so it is 0..255 and can never collide with this. */
#define MAP_AI_NO_TILE (-1)

/* 000127c0.  Walks one actor toward the opposing unit nearest to it in a
   straight line, ignoring terrain entirely.  This is the fallback
   fdps_map_actor_behavior_step reaches for at all five of its call sites once
   the path-cost sibling above has declined the actor.

   THE SIDE FILTER COMPARES TRUTH VALUES AND NEVER SIDE NUMBERS.  CMP dword ptr
   [EBP+0x18],0x0 then CMP byte ptr [EAX+0x6],0x0 on each arm at 00012833..
   0001284f: side_select 0 keeps every unit whose side byte is non-zero, and any
   non-zero side_select keeps only the units whose side byte is 0.  Side byte 6
   is 0 for the enemy side, 1 for neutral and 2 for the player, so the obvious
   record->side != own_side is wrong in both directions -- it would make neutral
   units targets for a player-driven actor and player units targets for a
   neutral one (rebuild_info/pitfalls.md).

   THE ACTOR SCORES ITSELF WHENEVER ITS OWN SIDE BYTE PASSES THAT FILTER.  The
   sweep runs over every index from 0 and nothing excludes unit_index, so an
   actor of side 1 driven with side_select 0 wins its own tile at distance 0,
   the equality test below then holds and the handler returns 0 having moved
   nothing.  This is the same shape as the sibling's hard-coded filter and is
   equally load-bearing.

   THE RUNNING BEST IS REPLACED ONLY ON A STRICTLY SMALLER DISTANCE.  CMP EAX,
   [EBP-0x24] / JGE at 000128c3, so a tie keeps the candidate already held and
   the lowest unit index wins it.

   The distance is the SUM of the two axes, abs on each through the CRT call at
   0003d364 twice per candidate (000128a2 and 000128b3) with ADD EBX,EAX
   between: the diagonal metric would pick a different unit wherever the two
   sums tie and the larger axes do not.

   The retired test is bit 0 of the flags byte alone -- AND AL,0x1 at 00012876
   inside an inline expansion of fdps_unit_is_retired, whose out-of-line body at
   000109b0 this replays instruction for instruction (rebuild_info/build_flags.md
   on inline expansion under -od).  Bit 7, the acted-this-turn flag, is not part
   of the answer.

   The tile handed to fdps_battle_move_unit_toward is the winning unit's OWN
   occupied tile, which no walk can end on; that function is what retargets the
   request to the reachable tile nearest it (movegrid.h), so a non-zero result
   means the actor moved, not that it arrived.

   The cursor draw mode is written 0 and then 1 around the walk, at 000128fe and
   00012937.  THE SECOND IS A PLAIN STORE AND NOT A RESTORE, and neither runs at
   all when the winning tile is the one the actor already stands on.

   The answer is 1 only when the move reported steps; 0 covers "no candidate",
   "the winner is standing where I am" and "the walk played nothing", and
   fdps_map_actor_behavior_step reads all three as "this handler did not claim
   the actor". */
int fdps_map_actor_move_toward_nearest_opponent(int unit_index, int side_select)
{
    /* The acting unit's record inside the map unit array. */
    struct fdps_unit_record *actor;
    /* The record the sweep is looking at this iteration. */
    struct fdps_unit_record *candidate;
    /* The tile the actor is standing on, record bytes +0 and +1, both widened
       as unsigned so a coordinate of 0x80 or above is 128 and never -128. */
    int actor_x;
    int actor_y;
    /* The unit index the sweep is at. */
    int index;
    /* The tile this candidate is standing on. */
    int candidate_x;
    int candidate_y;
    /* This candidate's straight-line distance from the actor. */
    int distance;
    /* The smallest distance seen so far. */
    int best_distance;
    /* The tile that distance belongs to -- the winning unit's own tile, which
       is what gets handed to the move.  target_x carries the "nothing found"
       sentinel; target_y is only ever read once target_x has left it, which is
       the same iteration that writes it. */
    int target_x;
    int target_y;
    /* The answer: 1 once the move has reported that it played a walk. */
    int moved;

    best_distance = MAP_AI_NO_DISTANCE;
    target_x = MAP_AI_NO_TILE;
    moved = 0;

    actor = fdps_get_unit_record(unit_index);
    actor_x = (int) actor->pos_x;
    actor_y = (int) actor->pos_y;

    for (index = 0; index < data_fdps_map_unit_count; index++) {
        candidate = fdps_get_unit_record(index);

        if (!((side_select == 0 && candidate->side != 0) ||
              (side_select != 0 && candidate->side == 0))) {
            continue;
        }
        if (fdps_unit_is_retired(index) != 0) {
            continue;
        }

        candidate_x = (int) candidate->pos_x;
        candidate_y = (int) candidate->pos_y;
        distance = abs(actor_x - candidate_x) + abs(actor_y - candidate_y);

        if (distance < best_distance) {
            target_x = candidate_x;
            target_y = candidate_y;
            best_distance = distance;
        }
    }

    if (target_x == MAP_AI_NO_TILE) {
        return 0;
    }

    if (target_x != actor_x || target_y != actor_y) {
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_map_cursor_move_to_unit(unit_index);
        if (fdps_battle_move_unit_toward(target_x, target_y, unit_index,
                                         side_select) != 0) {
            moved = 1;
        }
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_BOX;
    }

    return moved;
}

/* Bit 0x40 of the actor's behaviour byte, AND AL,0x40 at 00012c72.  It is not
   part of the behaviour nibble the dispatcher above reads; here it is the flag
   that decides the two ties below in the physical attack's favour.  Nothing in
   the shipped game can raise it -- every write to record byte 0x34 anywhere in
   the image either stores an immediate with the bit clear or merges a low
   nibble into (old & 0xf0), and across all 63 MAP*.DAT files the deployment
   byte it is seeded from never exceeds the low nibble -- so both ties always
   take their clear-bit arm.  It is still read here because the original reads
   it. */
#define AI_BEHAVIOR_PHYSICAL_TIE_BIT 0x40

/* CMP dword ptr [0x00063f90],0x12 / JGE at 00012d1b: below this spell id the
   attack-versus-spell tie is settled by comparing the spell's power word with
   the physical damage estimate, and at it and above by the flag bit instead.
   It is not a boundary in the spell table -- plain HP powers sit on both sides
   of it -- but a literal the author chose, and its effect is that above it the
   tie is decided by a flag that is always clear and the physical attack never
   wins it. */
#define MAP_AI_SPELL_POWER_TIE_LIMIT 0x12

/* 00012c10.  Runs all three action searches for one AI actor and carries out
   the one the five comparisons below pick, or reports that none was worth
   taking.
 *
 * THE DISPATCH IS FIVE EXPLICIT COMPARISONS AND NOT A SELECTION OF THE
 * MAXIMUM.  When all three scores are equal and at least one of them reaches
 * the threshold, every one of the five conditions is false and NO action
 * routine runs at all -- yet the cursor overlay is still cleared and 1 is
 * still returned, so fdps_map_actor_behavior_step believes the actor acted,
 * skips its fallbacks, and the unit stands still for the turn.  Writing the
 * obvious "run whichever option scored highest" gives that case an action and
 * changes enemy behaviour (rebuild_info/pitfalls.md).
 *
 * BOTH UNIT RECORDS, THE FLAG BIT AND THE DAMAGE ESTIMATE ARE COMPUTED BEFORE
 * ANY BRANCH.  The second fdps_get_unit_record is handed
 * data_fdps_battle_ai_best_physical_target_idx, which no scorer zeroes -- it is
 * written only when an attack was actually found -- so on a turn where the
 * attack search found nobody this resolves whatever the PREVIOUS actor left
 * there and reads its defence word.  That is harmless because the estimate is
 * used only in the attack-versus-spell tie, which cannot be reached unless the
 * attack score is at least 6 and so the index is fresh; moving the fetch inside
 * that arm would still be a different program, and it is left where the
 * original put it.
 *
 * The threshold compares are CMP ...,0x6 with JGE and JL, the signed ones, and
 * the three scores are tiers on one scale: the attack search publishes 0, 8 or
 * 0x12 and the spell and item searches publish sums of their own per-target
 * tiers (aiscore.h).
 *
 * fdps_get_spell_record is called on the tie arm BEFORE the 0x12 test and
 * whatever that test then decides, so the record is resolved even on the arm
 * that never dereferences it.
 *
 * The damage estimate is the actor's attack word less the attack target's
 * defence word, both MOVSX at 00012c7f and 00012c86, so a stat that has gone
 * negative stays negative; the compare against the spell's power word at
 * 00012d30 is JLE, so an estimate that merely equals the power leaves the tie
 * with the spell. */
int fdps_map_actor_take_best_action(int unit_index, int side_select)
{
    /* The acting unit's record. */
    struct fdps_unit_record *actor;
    /* The record of the unit the attack search picked out.  Stale whenever
       that search published nothing this turn -- see the note above. */
    struct fdps_unit_record *attack_target;
    /* The record of the spell the spell search picked out.  Only the power
       word of it is ever read. */
    struct fdps_spell_effect *chosen_spell;
    /* Behaviour-byte bit 0x40: when set, both of the ties below go to the
       physical attack instead of to the spell or the item. */
    int physical_wins_tie_flag;
    /* What the physical attack is expected to take off the attack target:
       the actor's attack stat less that target's defence stat. */
    int physical_damage_estimate;
    /* The chosen spell's power word, what the estimate is weighed against. */
    int chosen_spell_power;
    /* The answer: 1 once the dispatch has been entered. */
    int acted;

    fdps_map_actor_score_best_attack(unit_index, side_select);
    fdps_map_actor_score_best_spell(unit_index, side_select);
    fdps_map_actor_score_best_item(unit_index, side_select);

    actor = fdps_get_unit_record(unit_index);
    attack_target =
        fdps_get_unit_record(data_fdps_battle_ai_best_physical_target_idx);
    physical_wins_tie_flag =
        (int) (actor->ai_behavior & AI_BEHAVIOR_PHYSICAL_TIE_BIT);
    physical_damage_estimate = (int) actor->ap - (int) attack_target->dp;

    if (data_fdps_battle_ai_best_physical_score <
            MAP_AI_ACTION_SCORE_THRESHOLD &&
        data_fdps_battle_ai_best_spell_score <
            MAP_AI_ACTION_SCORE_THRESHOLD &&
        data_fdps_battle_ai_best_item_score <
            MAP_AI_ACTION_SCORE_THRESHOLD) {
        acted = 0;
    } else {
        if (data_fdps_battle_ai_best_physical_score >
                data_fdps_battle_ai_best_spell_score &&
            data_fdps_battle_ai_best_physical_score >
                data_fdps_battle_ai_best_item_score) {
            fdps_map_actor_move_and_attack(unit_index, side_select);
        } else if (data_fdps_battle_ai_best_physical_score ==
                       data_fdps_battle_ai_best_spell_score &&
                   data_fdps_battle_ai_best_spell_score >
                       data_fdps_battle_ai_best_item_score) {
            chosen_spell =
                fdps_get_spell_record(data_fdps_map_ai_best_spell_id);
            if (data_fdps_map_ai_best_spell_id <
                    MAP_AI_SPELL_POWER_TIE_LIMIT) {
                chosen_spell_power = (int) chosen_spell->power;
                if (physical_damage_estimate > chosen_spell_power) {
                    fdps_map_actor_move_and_attack(unit_index, side_select);
                } else {
                    fdps_map_actor_cast_chosen_spell(unit_index, side_select);
                }
            } else if (physical_wins_tie_flag == 0) {
                fdps_map_actor_cast_chosen_spell(unit_index, side_select);
            } else {
                fdps_map_actor_move_and_attack(unit_index, side_select);
            }
        } else if (data_fdps_battle_ai_best_physical_score ==
                       data_fdps_battle_ai_best_item_score &&
                   data_fdps_battle_ai_best_item_score >
                       data_fdps_battle_ai_best_spell_score) {
            if (physical_wins_tie_flag == 0) {
                fdps_map_actor_use_item(unit_index, side_select);
            } else {
                fdps_map_actor_move_and_attack(unit_index, side_select);
            }
        } else if (data_fdps_battle_ai_best_spell_score >
                       data_fdps_battle_ai_best_physical_score &&
                   data_fdps_battle_ai_best_spell_score >=
                       data_fdps_battle_ai_best_item_score) {
            fdps_map_actor_cast_chosen_spell(unit_index, side_select);
        } else if (data_fdps_battle_ai_best_item_score >
                       data_fdps_battle_ai_best_physical_score &&
                   data_fdps_battle_ai_best_item_score >
                       data_fdps_battle_ai_best_spell_score) {
            fdps_map_actor_use_item(unit_index, side_select);
        }

        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        acted = 1;
    }

    return acted;
}

/* 00013e10.  The map dimensions come from the MOVEMENT GRID's header -- MOVSX
   word ptr [EAX] and MOVSX word ptr [EAX+2] on data_fdps_battle_move_grid_ptr
   at 00013e21 and 00013e2c -- and not from the terrain layer's header at +7,
   which is the width fdps_map_load_tile_info goes on to index all three layers
   with.  The two agree in practice because the layers cover the same map; the
   scan simply trusts that, and there is no bounds check and no null test
   anywhere in the function, on the grid pointer or on anything else.

   Both header words are read with MOVSX, signed, and the loop tests are JL,
   the signed compare.  A header word of 0xffff has to come out as -1 so the
   loop body never runs; read unsigned it would walk 65535 rows.

   Both dimensions are latched into their own stack slots before the loops
   ([EBP-0xc] and [EBP-8]) and nothing in the body writes either, so they are
   two ordinary locals and not a per-iteration reload.

   The kind test is CMP EAX,0x20 after AND AL,0x60 -- an equality on the
   two-bit field, not a bit test.  The obvious (attr & 0x20) also accepts kind
   0x60, and widening it to the 0x20/0x40 pair that the player-side cursor
   search accepts adds buried treasure, which the original never sends an AI
   actor to (rebuild_info/pitfalls.md).

   The event code is compared as a full int: MOVSX EAX,word ptr [0x00069d06]
   then CMP EAX,[EBP+0x14].  The global is signed and only ever receives a
   zero-extended cell byte, so the comparison is against 0..255.

   The coordinates go out as byte stores, MOV byte ptr [EDX] and MOV byte ptr
   [EDX+1]: the caller's buffer is two bytes and nothing past them is written.

   The return sense is 0 for found and -1 for not found, which is what the
   caller's TEST EAX,EAX / JZ at 000102d0 gates on. */
int fdps_map_find_chest_cell(int cell_code, unsigned char *out_xy)
{
    int grid_width;
    int grid_height;
    int tile_x;
    int tile_y;

    grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
    grid_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2);

    for (tile_y = 0; tile_y < grid_height; tile_y++) {
        for (tile_x = 0; tile_x < grid_width; tile_x++) {
            fdps_map_load_tile_info(tile_x, tile_y);

            if ((data_fdps_map_current_tile_attr_flags & 0x60) == 0x20 &&
                (int) data_fdps_map_current_cell_event_code == cell_code) {
                out_xy[0] = (unsigned char) tile_x;
                out_xy[1] = (unsigned char) tile_y;
                return 0;
            }
        }
    }

    return -1;
}
