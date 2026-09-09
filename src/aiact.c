/* aiact.c -- the map AI's action layer: the routines that carry out the choice
 * src/aiscore.c has already scored, once the behaviour dispatcher in
 * src/mapai.c has decided that choice is worth taking.
 *
 * Nothing here decides anything.  The tile, the target, the spell and the bag
 * slot all arrive in the globals the matching search published (gamedata.h),
 * so an action run without its search first is carrying out a stale decision.
 *
 * See aiact.h for what each entry point promises.
 */
#include <i86.h>
#include "gamedata.h"
#include "aitarget.h"
#include "cmbspell.h"
#include "combat.h"
#include "death.h"
#include "gauge.h"
#include "item.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "movegrid.h"
#include "palette.h"
#include "spell.h"
#include "table.h"
#include "unit.h"
#include "unitatk.h"
#include "unitstat.h"
#include "aiact.h"

/* Two of the four values this file stores into data_fdps_map_cursor_draw_mode
   (gamedata.h): 0 at 00012e5c, 00012ed9, 00013d80 and 0002746f so nothing is
   painted over the walk or over the frame that opens the fight, and 1 at
   00012e8e and 00012ee8 for the plain cursor box.  Both stores of 1 are plain
   stores and not restores -- MOV dword ptr [0x00069cd0],0x1 with the entry
   value never read -- so a caller that was in any other mode comes back in the
   box mode.  The other two values are the blast diamond CURSOR_BLAST_MODE_BIAS
   names and the beam trail CURSOR_TRAIL_MODE names, both below. */
#define CURSOR_DRAW_MODE_HIDDEN 0
#define CURSOR_DRAW_MODE_BOX 1

/* PUSH 0x64 at 00012eb8: the tenth of a second the actor is held facing its
   target before the fight starts. */
#define FACE_TARGET_HOLD_MS 100

/* CMP EAX,0x1 at 00012f04 and 00012f6c.  fdps_check_can_counter_attack answers
   1 or -1 and never 0 (aitarget.h), so this is an equality against 1 and not a
   truth test: every defender that may NOT strike back would pass a truth
   test. */
#define COUNTER_ATTACK_ALLOWED 1

/* ADD EAX,0x8 at 00012fb0 on the int pointer the gauge call returned, which is
   two ints along: pair 0 of data_fdps_battle_combat_gauge_pos_pairs is where
   the actor's blow paints its bar and pair 1 where the counterblow paints
   its (gauge.h). */
#define COUNTER_GAUGE_PAIR_AT 2

/* The stack buffer fdps_collect_death_scripts fills, 0x0c bytes at [EBP-0x18]:
   four 3-byte records.  That collector is told no capacity and checks none, so
   the size is a contract with it and not a local choice (death.h). */
#define DEATH_SCRIPT_BUFFER_BYTES 12

/* IMUL ...,0xf then IDIV by 10 at 00013004, both signed. */
#define XP_CREDIT_NUMERATOR 15
#define XP_CREDIT_DENOMINATOR 10

/* CMP dword ptr [0x00063f88],0x6 / JL at 00013cd9: the tier the spell search's
   score has to reach before the cast is carried out.  Signed, and the same
   threshold the three searches are compared against elsewhere (gamedata.h). */
#define SPELL_ACTION_SCORE_FLOOR 6

/* The stack array fdps_collect_targets_in_range fills, 0x20 bytes at
   [EBP-0x40]: one unit index per byte.  That collector is told no capacity and
   checks none, so the size is a contract with it (aitarget.h). */
#define SPELL_TARGET_BUFFER_BYTES 32

/* PUSH 0x0 at 00013cf6: the min_dist argument.  Nothing inside the blast is
   excluded, so the whole diamond the reach byte spreads is collected. */
#define SPELL_TARGET_MIN_DISTANCE 0

/* The stack buffer fdps_collect_death_script_events fills, 0x0c bytes at
   [EBP-0x20]: four 3-byte records, and the same uncapped contract as the
   attack path's buffer above (death.h). */
#define DEATH_EVENT_BUFFER_BYTES 12

/* ADD EAX,0x2 at 00013d2f on the spell record's reach byte, and the same
   ADD EAX,0x2 at 000272c0 on the ITEM.DAT use_radius byte.  Reaches 1, 2 and
   3 land on fdps_draw_map_cursor's radius-1, radius-2 and radius-3 diamonds --
   overlay modes 3, 4 and 5 -- so the area of effect is shown while the cursor
   travels to the tile (mapcur.h).  MAGICDAT.DAT's forty records carry reaches
   0, 1, 2, 3, 6 and 8 and no other (assets/spells.md), so the modes this store
   can produce are 2, 3, 4, 5, 8 and 10: reach 0 shows the plain cursor and the
   two ground shocks show nothing at all, and the mode-6 arm -- the one that
   clears the movement grid's marker byte under the cursor as a side effect of
   drawing -- is out of reach from here, because no record has a reach of 4. */
#define CURSOR_BLAST_MODE_BIAS 2

/* IMUL by 0x18 at 00013d37 and 00013d3f, and again at 000272d2, 000272da,
   00027457 and 0002745f: a map tile is 24 pixels square, so the two tile
   coordinates are scaled into the pixel coordinates fdps_map_cursor_move_to
   scrolls to.  The item action also divides the two cursor pixel globals by it
   the other way, to get the tile the actor is standing on. */
#define MAP_TILE_SIZE 0x18

/* PUSH 0xc8 at 00013d4f: the fifth of a second the blast outline is held over
   the target tile before the spell is played. */
#define CAST_AIM_HOLD_MS 200

/* CMP EAX,0x1 at 00013d72, on the animation flag widened out of its byte.
   This is an equality against 1 and not a truth test: any other value takes
   the on-map route, so a byte of 2 would go to the full-screen presentation
   under `if (flag)` and does not here (gamedata.h). */
#define BATTLE_ANIMATION_ON 1

/* CMP byte ptr [EAX + 0x6],0x0 at 00013cb6: the authored target side the
   enemy phase inverts against. */
#define SPELL_TARGET_SIDE_PLAYER 0

/* Walks the actor onto the tile the attack search picked and fights the unit
   it picked there.  aiact.h carries what the whole sequence promises; what is
   worth saying beside the code is why three of these lines are spelled the way
   they are.

   THE TWO COUNTER-ATTACK TESTS ARE EQUALITIES AGAINST 1.  See
   COUNTER_ATTACK_ALLOWED above: the natural truth test turns every defender
   that may not strike back into one that does.

   THE SECOND TEST IS A BITWISE AND AND NOT A SHORT-CIRCUIT.  Both operands are
   evaluated -- the swing's answer at 00012f46 lands in [EBP-0x20] and the
   second fdps_check_can_counter_attack is called at 00012f64 whatever that
   answer was, its own 0/1 landing in [EBP-0x1c], and only then are the two
   ANDed at 00012f84.  Writing && would skip the second call on a dead target.

   THE EXPERIENCE GOES TO THE TARGET.  PUSH dword ptr [0x00063f74] at 0001301c,
   the unit that was attacked, not the actor at [EBP+0x14]: this is how a player
   unit earns experience for surviving an enemy turn. */
int fdps_map_actor_move_and_attack(int unit_index, int side_select)
{
    /* Where the two HP bars were put, as the gauge call's own array of two
       {x, y} pairs: pair 0 is the bar the actor's blow drains and pair 1 the
       bar the counterblow drains (gauge.h). */
    int *gauge_positions;
    /* What the actor's swing left the target on, 0 when the target died. */
    int target_hp_left;
    /* The 3-byte on-death records of every unit the exchange killed. */
    unsigned char death_scripts[DEATH_SCRIPT_BUFFER_BYTES];
    /* How many of those records were collected. */
    int death_script_count;

    data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
    fdps_map_cursor_move_to_unit(unit_index);
    fdps_battle_move_unit_toward(data_fdps_battle_ai_best_physical_target_x,
                                 data_fdps_battle_ai_best_attack_tile_y,
                                 unit_index, side_select);

    data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_BOX;
    fdps_map_cursor_move_to_unit(data_fdps_battle_ai_best_physical_target_idx);
    fdps_unit_face_target(unit_index,
                          data_fdps_battle_ai_best_physical_target_idx);
    delay((unsigned int) FACE_TARGET_HOLD_MS);

    data_fdps_battle_pending_xp_credit = 0;

    if (data_fdps_ui_battle_animation_enabled == 0) {
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_render_view_frame();
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_BOX;

        if (fdps_check_can_counter_attack(
                unit_index, data_fdps_battle_ai_best_physical_target_idx)
                == COUNTER_ATTACK_ALLOWED) {
            fdps_unit_face_target(data_fdps_battle_ai_best_physical_target_idx,
                                  unit_index);
        }

        gauge_positions = fdps_battle_show_combat_gauges(
            unit_index, data_fdps_battle_ai_best_physical_target_idx);
        target_hp_left = fdps_unit_attack_target(
            unit_index, data_fdps_battle_ai_best_physical_target_idx,
            gauge_positions);

        if ((target_hp_left != 0)
                & (fdps_check_can_counter_attack(
                       unit_index,
                       data_fdps_battle_ai_best_physical_target_idx)
                   == COUNTER_ATTACK_ALLOWED)) {
            fdps_unit_face_target(
                unit_index, data_fdps_battle_ai_best_physical_target_idx);
            fdps_unit_face_target(data_fdps_battle_ai_best_physical_target_idx,
                                  unit_index);
            fdps_unit_attack_target(
                data_fdps_battle_ai_best_physical_target_idx, unit_index,
                gauge_positions + COUNTER_GAUGE_PAIR_AT);
        }
    } else {
        fdps_combat_play_attack_exchange(
            unit_index, data_fdps_battle_ai_best_physical_target_idx);
    }

    death_script_count = fdps_collect_death_scripts(death_scripts);
    fdps_play_death_animation_and_mark_dead();
    fdps_run_death_scripts(data_fdps_battle_ai_best_physical_target_idx,
                           death_script_count, death_scripts);

    data_fdps_battle_pending_xp_credit =
        data_fdps_battle_pending_xp_credit * XP_CREDIT_NUMERATOR
        / XP_CREDIT_DENOMINATOR;
    fdps_unit_award_exp_and_level_up(
        data_fdps_battle_ai_best_physical_target_idx);

    return 1;
}

/* Casts the spell the map AI's spell search chose, at the tile it chose.
   aiact.h carries what the whole sequence promises; what is worth saying
   beside the code is why four of these lines are spelled the way they are.

   THE AUTHORED TARGET SIDE IS INVERTED ON THE ENEMY PHASE.  With side_select 0
   the filter handed to fdps_collect_targets_in_range is `record byte +6 == 0`
   and not the byte itself, so a spell authored to hit side 0 becomes one that
   hits every non-zero side and every other authored value collapses to 0: an
   enemy caster aims a player-facing spell at the player and a friendly-facing
   one at its own side.  With side_select non-zero -- the NPC phase, whose
   units already stand on the player's side -- the byte is used unchanged.
   Writing the byte through on both arms aims the enemy phase's offensive
   spells at the enemy's own ranks.

   THE ANIMATION FLAG IS TESTED FOR EQUALITY WITH 1.  See BATTLE_ANIMATION_ON
   above: the natural truth test sends every value but 0 to the full-screen
   presentation, where the original sends every value but 1 to the map.

   THE GRID IS RESET AFTER THE COLLECT AND NOT BEFORE.
   fdps_collect_targets_in_range wants the grid arriving at the 0xff sentinel
   and leaves it dirty, and putting it back is the caller's half of that
   contract (aitarget.h, movegrid.h).

   THE DEATH SCRIPTS ARE RUN FOR data_fdps_battle_ai_best_physical_target_idx.
   That global is the melee target the ATTACK search picked (gamedata.h) --
   neither the caster nor any unit this spell killed, and on the
   fdps_map_actor_behavior_step path a leftover from an earlier actor, because
   the attack search has not run this turn.  It reaches the chapter event
   handler table as the unit index of an opcode-2 record, so the obvious
   reading -- the caster, which is what fdps_battle_spell_command passes at its
   own matching call at 000280b1 -- hands those handlers a different unit.
   PUSH dword ptr [0x00063f74] at 00013dce is what the original does. */
int fdps_map_actor_cast_chosen_spell(int unit_index, int side_select)
{
    /* The chosen spell's MAGICDAT.DAT record, read for the blast radius at
       +0x04 and the authored target side at +0x06. */
    struct fdps_spell_effect *spell;
    /* The unit indices the blast covers, one per byte, as the collector
       appends them. */
    unsigned char targets[SPELL_TARGET_BUFFER_BYTES];
    /* How many of those slots it filled. */
    int target_count;
    /* The 3-byte on-death event records of every unit the spell killed. */
    unsigned char death_events[DEATH_EVENT_BUFFER_BYTES];
    /* How many of those records were collected. */
    int death_event_count;
    /* The select_mode the collector is asked for: the record's target side,
       inverted on the enemy phase. */
    int target_side_filter;
    /* 1 once the spell has been cast, 0 when the score gate refused it. */
    int cast_done;

    spell = fdps_get_spell_record(data_fdps_map_ai_best_spell_id);
    if (side_select == 0) {
        target_side_filter = (spell->target_side == SPELL_TARGET_SIDE_PLAYER);
    } else {
        target_side_filter = (int) spell->target_side;
    }

    if (data_fdps_battle_ai_best_spell_score < SPELL_ACTION_SCORE_FLOOR) {
        cast_done = 0;
    } else {
        fdps_map_cursor_move_to_unit(unit_index);

        target_count = fdps_collect_targets_in_range(
            (int) data_fdps_battle_ai_best_spell_target_x,
            (int) data_fdps_battle_ai_best_spell_target_y, targets,
            (int) spell->area, SPELL_TARGET_MIN_DISTANCE, target_side_filter);
        fdps_map_grid_reset();

        data_fdps_map_cursor_draw_mode =
            (int) spell->area + CURSOR_BLAST_MODE_BIAS;
        fdps_map_cursor_move_to(
            (int) (data_fdps_battle_ai_best_spell_target_x * MAP_TILE_SIZE),
            (int) (data_fdps_battle_ai_best_spell_target_y * MAP_TILE_SIZE));
        delay((unsigned int) CAST_AIM_HOLD_MS);
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_render_view_frame();

        if (data_fdps_ui_battle_animation_enabled == BATTLE_ANIMATION_ON) {
            fdps_combat_play_spell_on_targets(unit_index,
                                              data_fdps_map_ai_best_spell_id,
                                              target_count, targets);
        } else {
            fdps_cast_spell_on_targets(unit_index,
                                       data_fdps_map_ai_best_spell_id,
                                       target_count, targets);
        }

        death_event_count = fdps_collect_death_script_events(death_events);
        fdps_render_view_frame();
        fdps_play_death_animation_and_mark_dead();
        fdps_run_death_scripts(data_fdps_battle_ai_best_physical_target_idx,
                               death_event_count, death_events);

        data_fdps_battle_pending_xp_credit = 0;
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        cast_done = 1;
    }

    return cast_done;
}

/* The 0x20-byte stack array at [EBP-0x48] that whichever collector runs fills
   with one unit index per byte.  Neither collector is told a capacity and
   neither checks one, so the size is a contract with them (aitarget.h). */
#define ITEM_TARGET_BUFFER_BYTES 32

/* Bit 0x10 of the ITEM.DAT use_distance byte at +0x10 is the shape: below it
   the item covers an area and the reach is the separate use_radius byte at
   +0x12, from it up the item covers a straight line whose length in tiles is
   the low nibble.  CMP EAX,0xf / JLE at 00027233 picks the collector and
   CMP EAX,0x10 / JGE at 000272d0 picks the presentation; the two spellings
   partition the byte the same way. */
#define ITEM_LINE_SHAPE_BASE 0x10

/* PUSH 0x0 at 0002727a: the min_dist of the area collect, so nothing inside
   the diamond is excluded. */
#define ITEM_TARGET_MIN_DISTANCE 0

/* CMP byte ptr [EAX + 0x11],0x0 at 000271f3: the authored target side the
   enemy phase inverts against. */
#define ITEM_TARGET_SIDE_PLAYER 0

/* PUSH 0xc8 at 000272a8 and 00027301: the fifth of a second the aim is held,
   once after the collect and once more with the actor turned to face the first
   unit the beam found. */
#define ITEM_AIM_HOLD_MS 200

/* MOV dword ptr [0x00069cd0],0x6 at 00027356.  Mode 6 is the one arm of
   fdps_draw_map_cursor that draws nothing at all: it clears the movement
   grid's marker byte under the cursor instead, so a cursor walked across the
   map in this mode leaves the tiles it crossed marked behind it (mapcur.h). */
#define CURSOR_TRAIL_MODE 6

/* The white flash the beam opens with: MOV dword ptr [EBP-0x24],0x40 at
   0002730e counted down to 0, PUSH 0x0 and PUSH 0xff at 00027336 and 00027331
   for the whole DAC, and PUSH 0x4 at 00027346 between steps.  The bias is
   applied to all three channels at once, so 0x40 clamps every component to the
   6-bit maximum -- a white screen -- and 0 puts the palette back exactly
   (palette.h). */
#define FLASH_BIAS_MAX 0x40
#define FLASH_FIRST_DAC_ENTRY 0
#define FLASH_LAST_DAC_ENTRY 0xff
#define FLASH_STEP_MS 4

/* MOV dword ptr [0x0006015c],0x14 at 0002744d, ahead of the sweep: the
   movement-range highlight's blend ramp is put back to a fixed phase so the
   trail the sweep leaves always starts from the same shade (gamedata.h). */
#define TRAIL_BLEND_PHASE_START 0x14

/* MOV dword ptr [EBP-0x24],0x1 / CMP dword ptr [EBP-0x24],0x9 at 00027479 and
   00027480: the trail is held on screen for eight composed frames before the
   grid is wiped. */
#define TRAIL_HOLD_FIRST_FRAME 1
#define TRAIL_HOLD_FRAME_LIMIT 9

/* Uses the bag entry the item search chose, on the tile it chose.  aiact.h
   carries what the whole sequence promises; what is worth saying beside the
   code is why six of these lines are spelled the way they are.

   THE AUTHORED TARGET SIDE IS INVERTED ON THE ENEMY PHASE, the same way the
   cast above inverts the spell record's.  With side_select 0 the filter is
   `use_target == 0` and not the byte itself, so an item authored to reach the
   player's side reaches every non-zero side when a player unit holds it and
   side 0 -- the enemy's own ranks -- when an enemy holds it.  That inversion
   is what lets one authored healing item serve both sides.

   THE RECORD IS RESOLVED TWICE.  fdps_get_unit_record is called at 00027190
   for the tile the beam starts from and again at 000271b8 for the bag, with
   the same argument; the second is the argument-slot-and-result-slot shape of
   an inline expansion rather than a second decision
   (rebuild_info/build_flags.md).  It is a pure index-into-the-array lookup, so
   both land on the same record.

   THE GRID IS RESET AFTER THE COLLECT AND NOT BEFORE, which is the caller's
   half of the collectors' contract (aitarget.h, movegrid.h).  The line arm
   resets it a second time, after the sweep, because mode 6 has just cleared
   the marker of every cell the cursor crossed.

   THE AREA ARM LEAVES THE BLAST DIAMOND UP.  Nothing on that path puts
   data_fdps_map_cursor_draw_mode back, and only one of the two callers does it
   for us.  fdps_map_actor_take_best_action stores 0 at 00012e26, the shared
   exit both of its call sites jump to.  fdps_map_actor_behavior_step does not:
   its four stores of 0 sit at 000101ee, 00010239, 0001029e and 000103f1, every
   one of them on an earlier behaviour branch than the call at 000105fa, and the
   path from there to its single RET at 00010754 has none.  The diamond survives
   that return and is put back by whichever action runs next --
   fdps_map_actor_move_and_attack at 00012e5c,
   fdps_map_actor_move_toward_nearest_reachable_opponent at 0001276d,
   fdps_unit_rest at 0001212e -- or by the phase loop,
   fdps_battle_enemy_turn_phase at 0001298b and fdps_battle_npc_turn_phase at
   00012b2c and 00012b55.  The line arm does end at 0.

   THE SWEPT-TO TILE IS CLAMPED AGAINST THE WRONG WORDS, and that is the
   original's behaviour and not a defect to fix.  The bounds are the signed
   words at +0 and +2 of layer 0's .MPL blob, but an .MPL carries its
   dimensions at +7 and +9 and opens with a four-byte magic -- so the bounds
   are 0x504d and 0x004c whatever map is loaded.  On x that upper bound of
   20557 is out of reach -- the longest beam the shipped ITEM.DAT carries is 14
   tiles (records 0x63 and 0xc7) and no shipped .MPL is larger than 64 by 64,
   which caps the extrapolated x near 900 -- so only the clamp to 0 ever fires
   there; the y bound of 76 IS reachable, from six rows of aim difference up,
   and pins the sweep at row 75.  Either way a beam aimed off the right or
   bottom edge sweeps the cursor past the map, where mode 6 marks cells of the
   following row.  Clamping against the working grid's real extents -- which is
   what fdps_collect_targets_in_line itself uses -- would stop the sweep at the
   map edge and would NOT reproduce the original (rebuild_info/pitfalls.md).

   THE FAR END OF THE BEAM IS EXTRAPOLATED FROM THE CURSOR AND NOT FROM THE
   RECORD.  data_fdps_map_cursor_world_x / _y are divided by the tile size to
   get the tile the actor stands on, which is the same tile the record carries
   only because fdps_map_cursor_move_to_unit has just put the cursor there, and
   the collector restores both globals before it returns. */
int fdps_map_actor_use_item(int unit_index, int side_select)
{
    /* The acting unit's record, read for the tile the beam is drawn from. */
    struct fdps_unit_record *actor;
    /* The ITEM.DAT record of the bag entry the search picked, read for its
       shape, its authored target side and its blast radius. */
    struct fdps_item_effect *item;
    /* That entry's item id, out of the actor's own bag. */
    int item_id;
    /* What the collector is asked to keep: the item's authored target side,
       inverted on the enemy phase. */
    int target_side_filter;
    /* The unit indices the shape covers, one per byte, as the collector
       appends them. */
    unsigned char targets[ITEM_TARGET_BUFFER_BYTES];
    /* How many of those slots it filled. */
    int target_count;
    /* The item's shape-and-reach byte, cut down to the beam's length in tiles
       once the line arm has been taken. */
    unsigned char use_distance;
    /* The two tile bounds the swept-to tile is clamped against -- read off
       layer 0's blob, which is the mistake the note above is about. */
    int clamp_bound_x;
    int clamp_bound_y;
    /* The white-flash bias, walked from saturation back down to none. */
    int flash_bias;
    /* Which of the eight frames the beam's trail is held for. */
    int hold_frame;

    actor = fdps_get_unit_record(unit_index);
    item_id = (int) fdps_get_unit_record(unit_index)->inventory_slots
                  [data_fdps_map_ai_best_item_bag_slot * 2 + 1];
    item = fdps_get_item_record(item_id);

    if (side_select == 0) {
        target_side_filter = (item->use_target == ITEM_TARGET_SIDE_PLAYER);
    } else {
        target_side_filter = (int) item->use_target;
    }

    fdps_map_cursor_move_to_unit(unit_index);

    use_distance = item->use_distance;
    if (use_distance >= ITEM_LINE_SHAPE_BASE) {
        target_count = fdps_collect_targets_in_line(
            data_fdps_map_ai_best_item_target_x,
            data_fdps_battle_ai_best_item_target_y, targets,
            (int) actor->pos_x, (int) actor->pos_y,
            (int) use_distance - ITEM_LINE_SHAPE_BASE, target_side_filter);
    } else {
        target_count = fdps_collect_targets_in_range(
            data_fdps_map_ai_best_item_target_x,
            data_fdps_battle_ai_best_item_target_y, targets,
            (int) item->use_radius, ITEM_TARGET_MIN_DISTANCE,
            target_side_filter);
    }

    fdps_map_grid_reset();
    delay((unsigned int) ITEM_AIM_HOLD_MS);
    data_fdps_map_cursor_draw_mode = (int) item->use_radius
                                   + CURSOR_BLAST_MODE_BIAS;

    if (use_distance < ITEM_LINE_SHAPE_BASE) {
        fdps_map_cursor_move_to(
            data_fdps_map_ai_best_item_target_x * MAP_TILE_SIZE,
            data_fdps_battle_ai_best_item_target_y * MAP_TILE_SIZE);
    } else {
        fdps_unit_face_target(unit_index, (int) targets[0]);
        delay((unsigned int) ITEM_AIM_HOLD_MS);
        for (flash_bias = FLASH_BIAS_MAX; flash_bias >= 0; flash_bias--) {
            fdps_set_palette_range((struct fdps_palette_entry *)
                                   data_fdps_vga_main_palette_ptr,
                                   FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                                   flash_bias, flash_bias, flash_bias);
            delay((unsigned int) FLASH_STEP_MS);
        }

        use_distance = (unsigned char) (use_distance - ITEM_LINE_SHAPE_BASE);
        data_fdps_map_cursor_draw_mode = CURSOR_TRAIL_MODE;
        clamp_bound_x = (int) *(short *) data_fdps_scene_layer_tile_map_ptrs[0];
        clamp_bound_y = (int) *(short *)
                        (data_fdps_scene_layer_tile_map_ptrs[0] + 2);

        data_fdps_map_ai_best_item_target_x =
            data_fdps_map_cursor_world_x / MAP_TILE_SIZE
            + (int) use_distance
              * (data_fdps_map_ai_best_item_target_x
                 - data_fdps_map_cursor_world_x / MAP_TILE_SIZE);
        data_fdps_battle_ai_best_item_target_y =
            data_fdps_map_cursor_world_y / MAP_TILE_SIZE
            + (data_fdps_battle_ai_best_item_target_y
               - data_fdps_map_cursor_world_y / MAP_TILE_SIZE)
              * (int) use_distance;

        if (data_fdps_map_ai_best_item_target_x < clamp_bound_x) {
            if (data_fdps_map_ai_best_item_target_x < 0) {
                data_fdps_map_ai_best_item_target_x = 0;
            }
        } else {
            data_fdps_map_ai_best_item_target_x = clamp_bound_x - 1;
        }
        if (data_fdps_battle_ai_best_item_target_y < clamp_bound_y) {
            if (data_fdps_battle_ai_best_item_target_y < 0) {
                data_fdps_battle_ai_best_item_target_y = 0;
            }
        } else {
            data_fdps_battle_ai_best_item_target_y = clamp_bound_y - 1;
        }

        data_fdps_marked_tile_blend_phase = TRAIL_BLEND_PHASE_START;
        fdps_map_cursor_move_to(
            data_fdps_map_ai_best_item_target_x * MAP_TILE_SIZE,
            data_fdps_battle_ai_best_item_target_y * MAP_TILE_SIZE);
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        for (hold_frame = TRAIL_HOLD_FIRST_FRAME;
             hold_frame < TRAIL_HOLD_FRAME_LIMIT; hold_frame++) {
            fdps_render_view_frame();
        }
        fdps_map_grid_reset();
        fdps_map_cursor_move_to_unit((int) targets[0]);
    }

    fdps_apply_item_effect_to_targets(unit_index,
                                      data_fdps_map_ai_best_item_bag_slot,
                                      target_count, targets);
    data_fdps_battle_pending_xp_credit = 0;
    return 0;
}
