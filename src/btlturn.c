/* btlturn.c -- the battle's turn and phase engine.
 *
 * See btlturn.h for what the status byte at record +5 means and who reads it.
 * Everything here reaches the unit records through
 * data_fdps_map_unit_array_ptr and works in place on what it finds.
 */
#include <stdlib.h>
#include <conio.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "aiscore.h"
#include "anim.h"
#include "audio.h"
#include "blit.h"
#include "btlact.h"
#include "btlmenu.h"
#include "cdaudio.h"
#include "chapter.h"
#include "indicat.h"
#include "keybd.h"
#include "mapai.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "maptile.h"
#include "movegrid.h"
#include "overview.h"
#include "sprite.h"
#include "statwin.h"
#include "table.h"
#include "unit.h"
#include "unititem.h"
#include "unitstat.h"
#include "walk.h"
#include "btlturn.h"

/* The chapter script's turn-event table, in the resident MAP%02d.DAT block
   reached through data_fdps_tile_event_data_table_ptr (gamedata.h).  Sixteen
   fixed entries of three bytes each start at offset 3 of the block, right
   behind its three header bytes, of which +1 and +2 are the two counts
   fdps_field_load_chapter_resources publishes (rsrc.c).  MOV dword ptr
   [EBP-0x4],0x0 / CMP dword ptr [EBP-0x4],0x10 for the count,
   LEA EAX,[EAX+EAX*0x2] for the stride, and the +0x3, +0x4, +0x5
   displacements for the three fields.

   Nothing in the block says how many of the sixteen are in use.  The shipped
   maps fill the unused ones with turn 0xff, handler 0xff, side 0x00, which no
   turn counter ever matches -- the walk runs all sixteen regardless and the
   filler simply never fires. */
#define TURN_EVENT_TABLE_AT 3
#define TURN_EVENT_ENTRY_BYTES 3
#define TURN_EVENT_ENTRY_COUNT 0x10
#define TURN_EVENT_TURN_AT 0
#define TURN_EVENT_HANDLER_AT 1
#define TURN_EVENT_SIDE_AT 2

/* PUSH 0x0 in front of CALL dword ptr [EAX+0x601c4]: of the eight sites that
   call through that table (chapter.h) this is the only one with no unit to
   name, and it pushes a literal.  The slot's argument is a unit index and 0 is
   a valid one, not a "none" marker, so a handler that does read its argument
   reads unit 0 here. */
#define TURN_EVENT_HANDLER_UNIT_INDEX 0

/* 000119b0.  IMUL EAX,dword ptr [EBP+0x14],0x50 / MOV EDX,dword ptr
   [0x00069cd8] / ADD EDX,EAX / OR byte ptr [EAX+0x5],0x80.  The whole function
   is that: one record address formed inline and one bit ORed into the status
   byte.  There is no CALL, no branch and no return value.

   The multiply is IMUL, the signed one, and the index arrives as a full dword
   argument that nothing compares against data_fdps_map_unit_count first.  Both
   halves are reproduced deliberately: a caller that passes an out-of-range or
   negative index gets the byte outside the array written, which is what the
   original does, and adding the bound check the original does not have would
   change behaviour rather than protect anything.  All nine call sites reach
   here holding an index the phase engine has just been iterating over, so
   nothing in the image is known to depend on that -- see the open issue.

   OR, not MOV: bit 0 of the same byte is the retired flag, and a unit that
   died on its own turn is marked done afterwards.  Storing 0x80 would clear
   that flag and resurrect it. */
void fdps_battle_mark_unit_done(int unit_index)
{
    struct fdps_unit_record *unit;

    unit = (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * 0x50);
    unit->flags = (unsigned char) (unit->flags | 0x80);
}

/* 0002e0c0.  A counted walk over the whole sixteen-entry table -- there is no
   early exit anywhere in the body, the only backward branch is the loop's own
   JMP 0x0002e0de -- so every entry carrying this turn and this side fires, in
   table order, and a handler that schedules another event for the same turn
   and a later slot has it fired on the same pass.

   Both tests are equality, JNZ and JZ, on a byte widened with AND EAX,0xff.
   The turn byte is therefore unsigned: 0xff matches a turn counter of 255, not
   of -1 (contract C).  The counter itself is the int at data_fdps_battle_turn_counter,
   which starts at 1 and is bumped once per turn cycle, so entries scheduled
   for turn 0 are unreachable.

   MOV EDX,dword ptr [0x0006013c] is inside the loop body, once per field read:
   the block pointer is re-read on every iteration and a handler that reloaded
   the chapter -- fdps_field_load_chapter_resources frees this block and
   replaces it -- would have the rest of the walk read the new one.  Written
   that way here, with the entry address formed fresh each pass.

   The handler slot is the raw byte, scaled by four straight into the call,
   LEA EAX,[EAX*0x4 + 0x0] / CALL dword ptr [EAX+0x601c4] with no comparison
   against the table's fifty entries.  A map naming slot 50 or higher would
   call whatever lies past the table; the shipped maps use slots 0 to 44. */
void fdps_battle_run_turn_events(int side)
{
    int entry_index;
    unsigned char *entry;

    for (entry_index = 0; entry_index < TURN_EVENT_ENTRY_COUNT; entry_index++) {
        entry = data_fdps_tile_event_data_table_ptr + TURN_EVENT_TABLE_AT +
                entry_index * TURN_EVENT_ENTRY_BYTES;
        if ((int) entry[TURN_EVENT_TURN_AT] == data_fdps_battle_turn_counter &&
            (int) entry[TURN_EVENT_SIDE_AT] == side) {
            data_fdps_chapter_event_handler_table[entry[TURN_EVENT_HANDLER_AT]](
                TURN_EVENT_HANDLER_UNIT_INDEX);
        }
    }
}

/* Values the phase engine's own tests compare against.  The side byte is
   record +6, 0 enemy / 1 friendly NPC / 2 player, and 0 is both the side this
   phase sweeps and the side_select it hands the scorers and the behaviour
   step.  The mask is AND AL,0x81: bit 0 retired, bit 7 acted this turn, tested
   together and in one instruction.  The immobilising byte is record +0x26,
   struct field status_timers[4], the paralysis counter (aitarget.h).  Six is
   the score a spell or an item has to reach before the phase is willing to
   spend the unit's turn on it -- the same threshold, and the same tier scale,
   as src/mapai.c's MAP_AI_ACTION_SCORE_THRESHOLD, which is what lets the two
   be compared at all. */
#define ENEMY_PHASE_SIDE 0
#define UNIT_BUSY_FLAGS_MASK 0x81
#define STATUS_TIMER_PARALYSIS 4
#define PHASE_ACTION_SCORE_THRESHOLD 6

/* MOV dword ptr [0x00069cd0],0x0: the map-cursor overlay is put away before
   each enemy acts, the same value src/mapai.c calls CURSOR_DRAW_MODE_HIDDEN. */
#define CURSOR_DRAW_MODE_HIDDEN 0

/* The "no event is pending" value the four turn drivers seed
   data_fdps_chapter_pending_event_idx with and test it back against
   (gamedata.h).  CMP dword ptr [0x00069d90],0xff at 00012a16 and 00012acd. */
#define NO_PENDING_EVENT 0xff

/* 00012960.  The enemy side's whole phase of one battle turn, as TWO sweeps
   over the unit array rather than one.

   FIRST SWEEP.  Every eligible enemy has its best spell and its best item
   scored, and acts only if one of the two scores reaches
   PHASE_ACTION_SCORE_THRESHOLD.  SECOND SWEEP.  The same index range and the
   same eligibility test, no scoring: whoever is still eligible acts.  Since
   fdps_map_actor_behavior_step ends by raising the acted-this-turn bit
   (00010745, which UNIT_BUSY_FLAGS_MASK then catches), the second sweep picks
   up exactly the enemies the score gate turned away in the first, and every
   enemy ends the phase having had one behaviour step.  What the two sweeps
   really buy is ORDER: the enemies with something worth casting or drinking
   all move before the ones with nothing to do.

   Folding them into one loop would lose that, and it would also move three
   things the SECOND sweep deliberately does not do -- the cursor-mode clear,
   fdps_relocate_unit_array, and the two scorers.

   The bound is re-read from data_fdps_map_unit_count at the top of every
   iteration (CMP EAX,dword ptr [0x00060150] inside the loop, not hoisted) and
   the record pointer is re-fetched from fdps_get_unit_record after the
   relocation, never cached across it.  Both are load-bearing: an event handler
   may deploy more enemies part-way through, which fdps_deploy_unit appends at
   the end of the array so the growing bound reaches them, and
   fdps_relocate_unit_array zeroes and frees the block every pointer into the
   array was aimed at (unit.h).

   The tail after a unit has been dealt with runs whether or not it acted, and
   is the same in both sweeps.  If the pending-event slot is no longer
   NO_PENDING_EVENT -- fdps_map_set_pending_tile_event (maptile.h) fills it
   when the tile the unit finished on carries an event -- the handler it names
   is called with this unit's index.  The current chapter's post-action handler
   is then called with no argument.  A non-zero battle-end code in
   data_fdps_chapter_event_or_battle_end_code returns at once, from whichever
   sweep is running; fdps_battle_advance_turn tests the same global on the
   instruction after the call (0001e5f0).

   The physical-attack scorer is not called here.  The gate this phase applies
   is on the spell and item scores only -- the decision to close on somebody
   and hit them stays inside fdps_map_actor_behavior_step, which runs its own
   attack search.  So an enemy whose only good move is to walk up and swing
   fails the gate in the first sweep and gets its turn in the second. */
void fdps_battle_enemy_turn_phase(void)
{
    int unit_index;
    struct fdps_unit_record *unit;

    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_relocate_unit_array();
        unit = fdps_get_unit_record(unit_index);
        data_fdps_chapter_pending_event_idx = NO_PENDING_EVENT;

        if ((int) unit->side == ENEMY_PHASE_SIDE &&
            (unit->flags & UNIT_BUSY_FLAGS_MASK) == 0 &&
            unit->status_timers[STATUS_TIMER_PARALYSIS] == 0) {
            fdps_map_actor_score_best_spell(unit_index, ENEMY_PHASE_SIDE);
            fdps_map_actor_score_best_item(unit_index, ENEMY_PHASE_SIDE);
            if (data_fdps_battle_ai_best_spell_score >=
                    PHASE_ACTION_SCORE_THRESHOLD ||
                data_fdps_battle_ai_best_item_score >=
                    PHASE_ACTION_SCORE_THRESHOLD) {
                fdps_map_actor_behavior_step(unit_index, ENEMY_PHASE_SIDE);
            }
        }

        if (data_fdps_chapter_pending_event_idx != NO_PENDING_EVENT) {
            data_fdps_chapter_event_handler_table
                [data_fdps_chapter_pending_event_idx](unit_index);
        }
        data_fdps_chapter_post_action_handler_table
            [data_fdps_chapter_current_chapter_id]();
        if (data_fdps_chapter_event_or_battle_end_code != 0) {
            return;
        }
    }

    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        data_fdps_chapter_pending_event_idx = NO_PENDING_EVENT;

        if ((int) unit->side == ENEMY_PHASE_SIDE &&
            (unit->flags & UNIT_BUSY_FLAGS_MASK) == 0 &&
            unit->status_timers[STATUS_TIMER_PARALYSIS] == 0) {
            fdps_map_actor_behavior_step(unit_index, ENEMY_PHASE_SIDE);
        }

        if (data_fdps_chapter_pending_event_idx != NO_PENDING_EVENT) {
            data_fdps_chapter_event_handler_table
                [data_fdps_chapter_pending_event_idx](unit_index);
        }
        data_fdps_chapter_post_action_handler_table
            [data_fdps_chapter_current_chapter_id]();
        if (data_fdps_chapter_event_or_battle_end_code != 0) {
            return;
        }
    }
}

/* Side 1 in the record +6 encoding: the friendly NPCs, the side this phase
   sweeps.  It is also the side_select literal handed to the behaviour step --
   PUSH 0x1 at 00012bab, against the PUSH 0x0 the enemy phase makes at
   00012a08 -- so the one constant does both jobs here exactly as
   ENEMY_PHASE_SIDE does in the enemy phase above. */
#define NPC_PHASE_SIDE 1

/* 00012b20.  The NPC side's whole phase of one battle turn: ONE sweep over the
   unit array, giving every eligible side-1 unit a behaviour step.

   It is the enemy phase's first sweep with the scoring taken out.  There is no
   second sweep -- the last instruction of the loop body is the battle-end test
   and its JZ goes back to the increment at 00012b4d, and the fall-through at
   00012bfe is the epilogue -- and there is no score gate: an eligible NPC is
   handed straight to fdps_map_actor_behavior_step with nothing in front of it.
   No call to either scorer appears in the body at all, so the two score
   globals the enemy phase leans on are not read or written anywhere in this
   phase.  Ordering the NPCs by what they have worth casting, which is what the
   enemy phase's two sweeps buy, is simply not done for this side: the NPCs act
   in index order.

   Eligibility is the same three tests as the enemy phase, with 1 for the side:
   side byte +6 equal to NPC_PHASE_SIDE, neither bit of UNIT_BUSY_FLAGS_MASK
   set in the status byte at +5, and the paralysis counter at status_timers[4]
   zero.

   The cursor-mode clear happens ONCE BEFORE the loop as well as once per
   iteration -- MOV dword ptr [0x00069cd0],0x0 at 00012b2c, ahead of the index
   being zeroed at 00012b36, and again at 00012b55 inside the body.  The
   pre-loop one is the only write this function makes when the battle has no
   units at all, and it is not redundant with the in-loop one: the player phase
   that ran before this leaves the cursor overlay up, and a battle whose NPC
   side is empty would otherwise keep painting it.

   As in the enemy phase the bound is re-read from data_fdps_map_unit_count
   every iteration (CMP EAX,dword ptr [0x00060150] at 00012b40, inside the
   loop), and the record pointer is re-fetched from fdps_get_unit_record after
   fdps_relocate_unit_array, never carried across it -- the relocation frees
   and zeroes the block every pointer into the array was aimed at, and an
   event handler may deploy units that grow the count part-way through
   (unit.h).

   The tail after a unit has been dealt with runs whether or not it acted and
   is the enemy phase's tail exactly: a pending event fires its handler with
   THIS unit's index (PUSH EAX at 00012bd5, the unit index, then CALL dword ptr
   [EDX+0x601c4] and ADD ESP,0x4), the chapter's post-action handler is then
   called with no argument at all (no PUSH, no ADD ESP around CALL dword ptr
   [EAX+0x6028c] at 00012beb), and a non-zero battle-end code returns at once.
   fdps_battle_advance_turn, the only caller, tests that same global on the
   instruction after the call (0001e59a). */
void fdps_battle_npc_turn_phase(void)
{
    int unit_index;
    struct fdps_unit_record *unit;

    data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;

    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_relocate_unit_array();
        unit = fdps_get_unit_record(unit_index);
        data_fdps_chapter_pending_event_idx = NO_PENDING_EVENT;

        if ((int) unit->side == NPC_PHASE_SIDE &&
            (unit->flags & UNIT_BUSY_FLAGS_MASK) == 0 &&
            unit->status_timers[STATUS_TIMER_PARALYSIS] == 0) {
            fdps_map_actor_behavior_step(unit_index, NPC_PHASE_SIDE);
        }

        if (data_fdps_chapter_pending_event_idx != NO_PENDING_EVENT) {
            data_fdps_chapter_event_handler_table
                [data_fdps_chapter_pending_event_idx](unit_index);
        }
        data_fdps_chapter_post_action_handler_table
            [data_fdps_chapter_current_chapter_id]();
        if (data_fdps_chapter_event_or_battle_end_code != 0) {
            return;
        }
    }
}

/* The tile pitch the cursor's world pixels are divided by to name a tile, and
   the tile index is multiplied by to name a pixel again: MOV EBX,0x18 / IDIV
   EBX at 00015518 and 00015530, IMUL EAX,...,0x18 at 0001558a and 00015623.
   The divide is IDIV behind SAR EDX,0x1f, the signed one, which is what plain
   int division is. */
#define UNIT_TURN_TILE_PIXELS 0x18

/* PUSH 0x0 / PUSH 0x0 / PUSH 0x4 at 0001556e: fdps_map_cursor_select_loop in
   mode 4, "any tile the movement grid has marked" (mapcur.h), with no
   candidate list. */
#define UNIT_TURN_SELECT_MODE_MARKED_TILE 4

/* The -1 that fdps_map_cursor_select_loop, fdps_move_path_trace and
   fdps_battle_action_menu all answer for "backed out" / "no path": CMP dword
   ptr [EBP-0x1c],-0x1 at 00015584, 000156a8 and 0001579e, CMP dword ptr
   [EBP-0x10],-0x1 at 0001563b. */
#define UNIT_TURN_CANCELLED (-1)

/* The truth value both grid sweeps are handed, PUSH 0x1 at 0001550e, 00015560
   and 000155ac: the player's own side, so the zones of control marked are the
   enemies' and the tiles blocked are those of the non-enemy units
   (movegrid.h). */
#define UNIT_TURN_PLAYER_SIDE_SELECT 1

/* XOR EAX,EAX / PUSH EAX at 000155ce: fdps_move_path_trace mode 0, the
   strict-descent walk (movegrid.h). */
#define UNIT_TURN_PATH_MODE_STRICT 0

/* MOV dword ptr [0x0006015c],0x14 at 000154cc: the phase the marked-tile pulse
   is restarted at, once, before the range is first shown (gamedata.h). */
#define UNIT_TURN_BLEND_PHASE_START 0x14

/* The two values this function stores into data_fdps_map_cursor_draw_mode:
   0 while the path is traced and walked (00015619), 1 -- the plain cursor
   overlay -- whenever the map cursor is handed back (000156da, 000157a4,
   00015806). */
#define UNIT_TURN_CURSOR_HIDDEN 0
#define UNIT_TURN_CURSOR_NORMAL 1

/* The trigger_kind handed to fdps_map_set_pending_tile_event, PUSH 0x1 at
   00015704, 00015737 and 000157c3: "a unit ends its turn on this cell"
   (maptile.h). */
#define UNIT_TURN_TILE_EVENT_TURN_END 1

/* The unit_has_moved argument of fdps_battle_action_menu: PUSH 0x1 at
   0001568f after a walk, PUSH 0x0 at 00015785 when the unit stood still. */
#define UNIT_TURN_MENU_MOVED 1
#define UNIT_TURN_MENU_STAYED 0

/* The slot of the action ring's disabled array this function writes, MOV
   dword ptr [EBP-0x34] -- four bytes into the array at EBP-0x38 -- at
   00015668 and 00015772: slot 1, the spell command (btlact.h). */
#define UNIT_TURN_SPELL_SLOT 1

/* The two char_id values that keep the spell command after a move: CMP EAX,0x7
   at 00015651 and CMP EAX,0x3 at 00015661, both on record byte +0x08 widened
   with AND EAX,0xff.  They are 琴琴 (07) and 裘娜 (03) of the twelve playable
   characters (assets/characters.md). */
#define UNIT_TURN_CHAR_CAST_AFTER_MOVE_A 7
#define UNIT_TURN_CHAR_CAST_AFTER_MOVE_B 3

/* The unit record stride the take-back forms its address with, IMUL
   EDX,dword ptr [EBP+0x14],0x50 at 000156bb. */
#define UNIT_TURN_RECORD_BYTES 0x50

/* 00015470.  One player-controlled unit's whole turn: show its movement range,
   let the player pick a destination with the map cursor, walk it there and
   open the action menu -- round and round until the turn is spent or the
   player backs out of the range cursor.

   THE LOOP is a while over the byte-wide flag at EBP-0x4 (CMP byte ptr
   [EBP-0x4],0x0 / JNZ at 000154d6), which every exit arm sets to 1.  Each pass
   re-publishes the unit array (fdps_relocate_unit_array) and re-fetches the
   record, re-seeds data_fdps_chapter_pending_event_idx to 0xff, derives the
   start tile from the map CURSOR -- not from the record: the cursor is on the
   unit when the turn opens and is walked back onto the start tile before every
   later pass -- floods the range and hands the cursor to the player.

     select -1         the cursor is walked back onto the start tile and the
                       turn ends with nothing spent: no bit 7, so the unit can
                       be picked again.
     anything else     the range is flooded again, fdps_move_path_trace walks
                       from the cursor's tile back to the start tile, and the
                       step count it answers decides:
       0                 the unit stands still; the menu opens with
                         unit_has_moved 0.
       -1                nothing at all -- the loop runs again.
       otherwise         the unit walks the path and the menu opens with
                         unit_has_moved 1.

   The menu's answer and data_fdps_battle_action_cancel_ends_turn_flag between
   them end the turn or loop again, and the two branches are NOT symmetrical:

     moved, menu -1, flag 0      TAKE-BACK: pos_x/pos_y are put back to the
                                 start tile, the cursor walked there, loop.
     moved, anything else        mark done, report the tile for occasion 1,
                                 end.
     stayed, menu -1, flag 0     loop (there is nothing to put back).
     stayed, menu -1, flag != 0  mark done, report the tile, end.
     stayed, menu answered       mark done, end -- with NO tile report.

   The last row is the original's own shape (000157ea goes straight to
   fdps_battle_mark_unit_done and on to 000157fa, past no report) and is kept:
   a unit that acts without moving does not re-fire its cell's turn-end event.

   THE SPELL SLOT.  The two four-int ring arrays are initialised ONCE, before
   the loop, from the read-only copies the compiler parks at 00014720 and
   00014730, and they live across passes.  After a move every character except
   琴琴 and 裘娜 has slot 1 greyed; a unit that stood still has it cleared
   again.  Nothing else here resets them, and fdps_battle_action_menu only ever
   sets slots 1..3 (btlact.h), so a slot the menu greyed on one pass stays
   greyed after a take-back.

   THE RECORD POINTER IS NOT RE-FETCHED after fdps_battle_action_menu.  The
   tile report reads pos_x/pos_y through the pointer taken at the top of the
   pass (EBP-0x20, reloaded at 00015706 and 00015739), exactly as the assembly
   does.  Only the take-back forms the address afresh, inline off
   data_fdps_map_unit_array_ptr rather than through fdps_get_unit_record.

   THE EXIT.  The cursor overlay is put back to 1, the path buffer freed, a
   pending event fires its handler with unit_index (PUSH EAX / CALL dword ptr
   [EDX+0x601c4] / ADD ESP,0x4 at 00015838), and the chapter's post-action
   handler is called with nothing (CALL dword ptr [EAX+0x6028c], no push).

   The path buffer is malloc(move) with move the record's byte +0x3b read
   unsigned; it is neither checked for NULL nor bounded against the step count
   fdps_move_path_trace writes into it. */
void fdps_battle_unit_turn(int unit_index)
{
    /* The ring's icon and disabled arrays, copied from 00014720 and 00014730
       at entry (four MOVSD each) and passed to every menu call of the turn. */
    int action_icons[4] = { 3, 2, 1, 0 };
    int action_disabled[4] = { 0, 0, 0, 0 };
    /* EBP-0x4: set once the turn has ended one way or another. */
    unsigned char turn_over;
    /* EBP-0x20: the acting unit's record for the current pass. */
    struct fdps_unit_record *unit;
    /* EBP-0xc: record byte +0x3b, the unit's movement allowance; the path
       buffer's size and the flood fill's budget. */
    int move_points;
    /* EBP-0x24: the buffer fdps_move_path_trace writes the direction codes
       into and fdps_animate_move_path replays. */
    unsigned char *path_buffer;
    /* EBP-0x8: record byte +0x20, the class code. */
    int class_code;
    /* EBP-0x28: the unit's PROMAP.DAT row, class code plus one. */
    struct fdps_class_record *class_record;
    /* EBP-0x18 / EBP-0x14: the tile the turn started from, taken off the
       cursor at the top of each pass. */
    int start_tile_x;
    int start_tile_y;
    /* EBP-0x1c: what fdps_map_cursor_select_loop and then
       fdps_battle_action_menu answered. */
    int answer;
    /* EBP-0x10: fdps_move_path_trace's step count, 0 for "the cursor is on
       the start tile", -1 for "no path". */
    int step_count;

    turn_over = 0;
    unit = fdps_get_unit_record(unit_index);
    move_points = (int) unit->move;
    path_buffer = (unsigned char *) malloc(move_points);
    class_code = (int) unit->clazz;
    data_fdps_marked_tile_blend_phase = UNIT_TURN_BLEND_PHASE_START;

    while (turn_over == 0) {
        fdps_relocate_unit_array();
        unit = fdps_get_unit_record(unit_index);
        data_fdps_chapter_pending_event_idx = NO_PENDING_EVENT;
        class_record = fdps_get_class_record(class_code + 1);
        fdps_move_grid_mark_opposing_zones_of_control(
            UNIT_TURN_PLAYER_SIDE_SELECT);
        start_tile_x = data_fdps_map_cursor_world_x / UNIT_TURN_TILE_PIXELS;
        start_tile_y = data_fdps_map_cursor_world_y / UNIT_TURN_TILE_PIXELS;
        fdps_move_grid_flood_fill_range(class_record, start_tile_x,
                                        start_tile_y, move_points);
        fdps_move_grid_block_occupied_tiles(unit_index,
                                            UNIT_TURN_PLAYER_SIDE_SELECT);
        answer = fdps_map_cursor_select_loop(
            UNIT_TURN_SELECT_MODE_MARKED_TILE, 0, NULL);
        fdps_map_grid_reset();

        if (answer == UNIT_TURN_CANCELLED) {
            fdps_map_cursor_move_to(start_tile_x * UNIT_TURN_TILE_PIXELS,
                                    start_tile_y * UNIT_TURN_TILE_PIXELS);
            turn_over = 1;
            continue;
        }

        data_fdps_ui_play_active_flag = 0;
        fdps_move_grid_mark_opposing_zones_of_control(
            UNIT_TURN_PLAYER_SIDE_SELECT);
        fdps_move_grid_flood_fill_range(class_record, start_tile_x,
                                        start_tile_y, move_points);
        step_count = fdps_move_path_trace(
            start_tile_x, start_tile_y, path_buffer,
            data_fdps_map_cursor_world_x / UNIT_TURN_TILE_PIXELS,
            data_fdps_map_cursor_world_y / UNIT_TURN_TILE_PIXELS,
            UNIT_TURN_PATH_MODE_STRICT);
        fdps_map_grid_reset();
        data_fdps_map_cursor_draw_mode = UNIT_TURN_CURSOR_HIDDEN;
        fdps_map_cursor_move_to(start_tile_x * UNIT_TURN_TILE_PIXELS,
                                start_tile_y * UNIT_TURN_TILE_PIXELS);

        if (step_count != 0 && step_count != UNIT_TURN_CANCELLED) {
            if ((int) unit->char_id != UNIT_TURN_CHAR_CAST_AFTER_MOVE_A &&
                (int) unit->char_id != UNIT_TURN_CHAR_CAST_AFTER_MOVE_B) {
                action_disabled[UNIT_TURN_SPELL_SLOT] = 1;
            }
            fdps_flush_keyboard_queue();
            fdps_animate_move_path(unit_index, path_buffer, step_count);
            data_fdps_ui_play_active_flag = 1;
            answer = fdps_battle_action_menu(unit_index, action_icons,
                                             action_disabled,
                                             UNIT_TURN_MENU_MOVED);
            if (answer == UNIT_TURN_CANCELLED &&
                data_fdps_battle_action_cancel_ends_turn_flag == 0) {
                unit = (struct fdps_unit_record *)
                       (data_fdps_map_unit_array_ptr +
                        unit_index * UNIT_TURN_RECORD_BYTES);
                unit->pos_x = (unsigned char) start_tile_x;
                unit->pos_y = (unsigned char) start_tile_y;
                data_fdps_map_cursor_draw_mode = UNIT_TURN_CURSOR_NORMAL;
                fdps_map_cursor_move_to(start_tile_x * UNIT_TURN_TILE_PIXELS,
                                        start_tile_y * UNIT_TURN_TILE_PIXELS);
            } else {
                fdps_battle_mark_unit_done(unit_index);
                fdps_map_set_pending_tile_event((int) unit->pos_x,
                                                (int) unit->pos_y,
                                                UNIT_TURN_TILE_EVENT_TURN_END);
                turn_over = 1;
            }
            data_fdps_ui_play_active_flag = 1;
        } else if (step_count == 0) {
            action_disabled[UNIT_TURN_SPELL_SLOT] = 0;
            fdps_flush_keyboard_queue();
            data_fdps_ui_play_active_flag = 1;
            answer = fdps_battle_action_menu(unit_index, action_icons,
                                             action_disabled,
                                             UNIT_TURN_MENU_STAYED);
            if (answer == UNIT_TURN_CANCELLED) {
                data_fdps_map_cursor_draw_mode = UNIT_TURN_CURSOR_NORMAL;
                if (data_fdps_battle_action_cancel_ends_turn_flag != 0) {
                    fdps_battle_mark_unit_done(unit_index);
                    fdps_map_set_pending_tile_event(
                        (int) unit->pos_x, (int) unit->pos_y,
                        UNIT_TURN_TILE_EVENT_TURN_END);
                    turn_over = 1;
                }
            } else {
                fdps_battle_mark_unit_done(unit_index);
                turn_over = 1;
            }
            data_fdps_ui_play_active_flag = 1;
        }
    }

    data_fdps_map_cursor_draw_mode = UNIT_TURN_CURSOR_NORMAL;
    free(path_buffer);
    if (data_fdps_chapter_pending_event_idx != NO_PENDING_EVENT) {
        data_fdps_chapter_event_handler_table
            [data_fdps_chapter_pending_event_idx](unit_index);
    }
    data_fdps_chapter_post_action_handler_table
        [data_fdps_chapter_current_chapter_id]();
}

/* ---- fdps_battle_advance_turn, 0001e3f0 -------------------------------- */

/* The side the end-of-turn rest applies to, CMP EAX,0x2 at 0001e477 on record
   byte +6: the player's units.  It is also the side argument of the two calls
   that open the next player phase, PUSH 0x2 at 0001e630 and 0001e63a. */
#define PLAYER_PHASE_SIDE 2

/* The rest's two status-timer tests, CMP byte ptr [EAX+0x25],0x0 at 0001e492
   and CMP byte ptr [EAX+0x26],0x0 at 0001e49d: status_timers[3] is the poison
   counter fdps_battle_tick_status_effects bleeds HP from (unitstat.h) and
   status_timers[4] the paralysis counter STATUS_TIMER_PARALYSIS above names. */
#define STATUS_TIMER_POISON 3

/* MOV EBX,0x5 / IDIV EBX at 0001e4af: a resting unit gets back a fifth of its
   maximum, the same fraction fdps_unit_rest (unitatk.h) gives. */
#define ADVANCE_REST_DIVISOR 5

/* The page the rest flashes are composed on and how it is presented: the
   0x15180 pushed to malloc at 0001e40e, a whole 360 by 240 page at a 0x168
   pitch, of which 312 by 192 from page byte 0x21d8 -- page pixel (24,24) -- is
   put down at screen byte 0x504 -- screen pixel (4,4) -- of the 320-pitch
   mode 13h page; the six pushes at 0001e51b through 0001e53c.  The screen base
   stays a literal: 0xa0000 is where the display adapter answers, not the
   address of anything the linker places (contract E). */
#define ADVANCE_SCENE_BYTES 0x15180
#define ADVANCE_SCENE_PITCH 0x168
#define ADVANCE_SCENE_WINDOW_AT 0x21d8
#define ADVANCE_VGA_SCREEN_BASE 0x000a0000
#define ADVANCE_SCREEN_WINDOW_AT 0x504
#define ADVANCE_VGA_SCREEN_PITCH 0x140
#define ADVANCE_VIEW_WIDTH 0x138
#define ADVANCE_VIEW_ROWS 0xc0

/* VGA input status register 1; bit 3 is set while the vertical retrace is in
   progress, which the one present straddles (0001e4f9 and 0001e50a). */
#define ADVANCE_VGA_INPUT_STATUS_1 0x3da
#define ADVANCE_VGA_STATUS_VERTICAL_RETRACE 0x08

/* fdps_blit_unit_sprite's kernel selector and mode operand for the rest flash:
   mode 3, the recolour kernel, with 0xff00 -- offset 0, base 0xff, mask 0 --
   which paints the whole sprite in the one palette index 0xff, a flat white
   silhouette (rlecolor.h).  The same pair fdps_unit_rest flashes with. */
#define ADVANCE_REST_FLASH_BLIT_MODE 3
#define ADVANCE_REST_FLASH_RECOLOR 0xff00

/* MOV EAX,0x61784 / CALL fdps_play_sfx at 0001e54b.  A plain writable literal,
   already upper case as the original's copy is: the lookup upper-cases the
   caller's own storage in place (audio.h, rebuild_info/pitfalls.md). */
#define ADVANCE_REST_SOUND "REST.WAV"

/* PUSH 0x28 / CALL delay at 0001e559: how long the flashed frame is held. */
#define ADVANCE_REST_HOLD_MS 0x28

/* The two phase banners, MOV EAX,0x61790 at 0001e5a7 and MOV EAX,0x617a0 at
   0001e603.  They stay in the original's MIXED case: fdps_play_vfs_animation
   only recognises the player-phase clip after its loader has upper-cased the
   caller's buffer in place, and it needs a writable buffer to do that
   (anim.h). */
#define ADVANCE_ENEMY_PHASE_BANNER "EnyPhase.saf"
#define ADVANCE_PLAYER_PHASE_BANNER "PlyPhase.saf"

/* The track_slot of fdps_cd_verify_disc_and_play_track: PUSH 0x1 at 0001e5db,
   just before the enemy phase runs, and PUSH 0x0 at 0001e620 as the player
   phase opens (cdaudio.h). */
#define ADVANCE_CD_TRACK_ENEMY_PHASE 1
#define ADVANCE_CD_TRACK_PLAYER_PHASE 0

/* The two values stored into data_fdps_map_cursor_draw_mode: 0 at 0001e611,
   the overlay put away while the player phase is being opened, and 1 at
   0001e807, the plain cursor box handed back to the player. */
#define ADVANCE_CURSOR_HIDDEN 0
#define ADVANCE_CURSOR_NORMAL 1

/* The per-turn MP regeneration the player phase opens with.  Four ITEM.DAT
   ids -- 妖刀村正 0xa6 and 妖刀正宗 0xa7 looked for on unit index 4 only
   (PUSH 0xa6 / PUSH 0x4 at 0001e644, PUSH 0xa7 / PUSH 0x4 at 0001e65c), 形見指環
   0xb1 on unit index 8 only (0001e6e6), and 魔精石碎片 0xb3 on every unit
   (0001e79e) -- assets/items.md lists all four as the items whose per-turn
   recovery is not in the item table. */
#define REGEN_ITEM_MURAMASA 0xa6
#define REGEN_ITEM_MASAMUNE 0xa7
#define REGEN_ITEM_KATAMI_RING 0xb1
#define REGEN_ITEM_MANA_SHARD 0xb3
#define REGEN_SWORD_UNIT_INDEX 4
#define REGEN_RING_UNIT_INDEX 8

/* fdps_unit_find_item_slot's "not held" answer, CMP ...,-0x1 at 0001e656,
   0001e66e, 0001e6f8 and 0001e7af. */
#define REGEN_ITEM_NOT_HELD (-1)

/* The equipped bit of an inventory entry's flag byte, AND AL,0x40 at 0001e690
   and 0001e71a (unititem.h).  The two single-unit items need it; the shard
   does not -- the per-unit sweep has no such test. */
#define REGEN_ITEM_EQUIPPED 0x40

/* PUSH 0xf at 0001e6b8, 0001e742 and 0001e7d1: the restore requested of
   fdps_unit_restore_mp, which rolls 13 or 14 of it (unitstat.h). */
#define REGEN_MP_AMOUNT 0x0f

/* PUSH 0xd at 0001e6d3, 0001e75d and 0001e7f2: the Number.cel digit set the
   restored figure floats in, the MP set (indicat.h). */
#define REGEN_MP_GLYPH_BASE 0x0d

/* PUSH 0x0 at 0001e811 and 0001e81f: the unit the view is brought back to
   once the regeneration is done, unless fdps_unit_is_retired says it has
   left the field. */
#define ADVANCE_FOCUS_UNIT_INDEX 0

/* 0001e3f0.  Closes the player phase and runs one whole battle turn round to
   the next player phase.  The plain -4s frame: PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP, SUB ESP,0x1c, no argument read above [EBP], one epilogue at
   0001e835 with a bare RET, and all three call sites -- 00014d42 and 00014e3d
   in fdps_battle_system_menu, 0002ea9d in
   fdps_battle_end_phase_if_all_units_done -- a bare CALL whose EAX nobody
   reads afterwards.

   THE REST.  Every unit on side 2 that is neither retired nor already acted
   (flags & 0x81), neither poisoned nor paralysed, and whose HP is not EQUAL
   to its maximum (CMP / JNZ at 0001e4a8, so a unit above its maximum rests
   too and is clamped down), gets a fifth of its maximum back and is flashed
   white into the page.  Both HP words are MOVSX-widened and the ceiling test
   is JLE, so the sums are signed (contract C).  The page is presented ONCE,
   after the sweep, straddling one retrace; the sound plays only if somebody
   rested; the frame is held, the page freed and the view repainted whether or
   not anybody did.

   THE PHASES.  NPC events and status ticks, the NPC phase, the enemy banner,
   the bit-7 clear, enemy events and ticks, the enemy-phase music, the enemy
   phase, then the turn counter bump, the player banner, the cursor put away,
   a second bit-7 clear, the player-phase music and player events and ticks.
   The battle-end code in data_fdps_chapter_event_or_battle_end_code is tested
   four times -- after the NPC ticks, after the NPC phase, after the enemy
   ticks and after the enemy phase -- and a non-zero one returns at once,
   leaving the play-active flag at the 0 this function put there and the
   counter where it was.  After the player ticks it is NOT tested.

   THE REGENERATION.  Unit 4 is asked for 妖刀村正 and, failing that, 妖刀正宗;
   unit 8 for 形見指環; both need the found entry equipped.  Then every unit is
   asked for 魔精石碎片, merely held.  Each also needs its whole status byte
   zero -- CMP byte ptr [EAX+0x5],0x0, not the 0x81 mask -- and its MP not
   equal to its maximum (a 16-bit compare, equality only).  A unit that
   qualifies has MP restored, the view scrolled to it, the rolled figure
   floated in the MP digits and the popup played; a unit carrying both an
   equipped sword or ring and a shard regenerates twice.

   THE CALLS' ANSWERS.  malloc's page (0001e41b) is composed into and freed
   without a NULL test.  fdps_get_unit_record's pointer (0001e455, 0001e682,
   0001e70c, 0001e79b) is where every field comes from and the healed HP goes
   back to; in the per-unit sweep it is fetched BEFORE the shard lookup, in the
   two single-unit blocks AFTER it, and nothing between moves the array.  inp's
   byte is the retrace bit.  fdps_unit_find_item_slot's slot (0001e653,
   0001e66b, 0001e6f5) indexes the inventory, and in the sweep (0001e7af) is
   only compared with -1.  fdps_unit_restore_mp's roll (0001e6c4, 0001e74e,
   0001e7df) is the figure floated -- the roll, not the MP gained.
   fdps_unit_is_retired's answer (0001e81b) decides the last scroll.  Nothing
   else called returns anything the original reads. */
void fdps_battle_advance_turn(void)
{
    /* EBP-0x4: set once any unit has rested, which is what decides the
       sound. */
    unsigned char any_unit_rested;
    /* EBP-0x8: fdps_blit_unit_sprite's mode operand, parked in a stack slot
       on entry (MOV dword ptr [EBP-0x8],0xff00 at 0001e400) rather than
       pushed as a literal. */
    unsigned int rest_flash_recolor;
    /* EBP-0xc: the page the rest flashes are composed on. */
    unsigned char *scene;
    /* EBP-0x14: the unit the rest sweep and the shard sweep are on. */
    int unit_index;
    /* EBP-0x1c: that unit's record, or unit 4's or unit 8's. */
    struct fdps_unit_record *unit;
    /* EBP-0x10: the unit's HP as found, and then the rested figure written
       back. */
    int current_hp;
    /* EBP-0x18: its maximum, the figure the fifth is taken of and the
       ceiling. */
    int hp_max;
    /* EBP-0x14 again in the original: the inventory entry
       fdps_unit_find_item_slot found the sword or ring in. */
    int item_slot;
    /* EBP-0x14 / EBP-0x10 in the original: fdps_unit_restore_mp's roll, the
       figure floated over the unit. */
    int mp_restored;

    any_unit_rested = 0;
    rest_flash_recolor = ADVANCE_REST_FLASH_RECOLOR;
    data_fdps_ui_play_active_flag = 0;
    scene = (unsigned char *) malloc((size_t) ADVANCE_SCENE_BYTES);
    fdps_draw_scene_layers(scene);

    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        current_hp = (int) unit->hp_current;
        hp_max = (int) unit->hp_max;
        if ((int) unit->side == PLAYER_PHASE_SIDE &&
            (unit->flags & UNIT_BUSY_FLAGS_MASK) == 0 &&
            unit->status_timers[STATUS_TIMER_POISON] == 0 &&
            unit->status_timers[STATUS_TIMER_PARALYSIS] == 0 &&
            current_hp != hp_max) {
            current_hp += hp_max / ADVANCE_REST_DIVISOR;
            if (current_hp > hp_max) {
                current_hp = hp_max;
            }
            unit->hp_current = (short) current_hp;
            fdps_blit_unit_sprite(scene, unit_index, rest_flash_recolor,
                                  ADVANCE_REST_FLASH_BLIT_MODE);
            any_unit_rested = 1;
        }
    }

    while ((inp(ADVANCE_VGA_INPUT_STATUS_1) &
            ADVANCE_VGA_STATUS_VERTICAL_RETRACE) == 0) {
        /* Spin until the retrace begins. */
    }
    while ((inp(ADVANCE_VGA_INPUT_STATUS_1) &
            ADVANCE_VGA_STATUS_VERTICAL_RETRACE) != 0) {
        /* And until it ends, so the present starts clear of it. */
    }
    fdps_blit_rect((unsigned int) (scene + ADVANCE_SCENE_WINDOW_AT),
                   ADVANCE_SCENE_PITCH,
                   (void *) (ADVANCE_VGA_SCREEN_BASE +
                             ADVANCE_SCREEN_WINDOW_AT),
                   ADVANCE_VGA_SCREEN_PITCH, ADVANCE_VIEW_WIDTH,
                   ADVANCE_VIEW_ROWS);
    if (any_unit_rested != 0) {
        fdps_play_sfx(ADVANCE_REST_SOUND);
    }
    delay(ADVANCE_REST_HOLD_MS);
    free(scene);
    fdps_render_view_frame();

    fdps_battle_run_turn_events(NPC_PHASE_SIDE);
    fdps_battle_tick_status_effects(NPC_PHASE_SIDE);
    if (data_fdps_chapter_event_or_battle_end_code != 0) {
        return;
    }
    fdps_battle_npc_turn_phase();
    if (data_fdps_chapter_event_or_battle_end_code != 0) {
        return;
    }

    fdps_play_vfs_animation(ADVANCE_ENEMY_PHASE_BANNER);
    fdps_units_clear_status_bit7();
    fdps_battle_run_turn_events(ENEMY_PHASE_SIDE);
    fdps_battle_tick_status_effects(ENEMY_PHASE_SIDE);
    if (data_fdps_chapter_event_or_battle_end_code != 0) {
        return;
    }
    fdps_cd_verify_disc_and_play_track(data_fdps_chapter_current_chapter_id,
                                       ADVANCE_CD_TRACK_ENEMY_PHASE);
    fdps_battle_enemy_turn_phase();
    if (data_fdps_chapter_event_or_battle_end_code != 0) {
        return;
    }

    data_fdps_battle_turn_counter++;
    fdps_play_vfs_animation(ADVANCE_PLAYER_PHASE_BANNER);
    data_fdps_map_cursor_draw_mode = ADVANCE_CURSOR_HIDDEN;
    fdps_units_clear_status_bit7();
    fdps_cd_verify_disc_and_play_track(data_fdps_chapter_current_chapter_id,
                                       ADVANCE_CD_TRACK_PLAYER_PHASE);
    fdps_battle_run_turn_events(PLAYER_PHASE_SIDE);
    fdps_battle_tick_status_effects(PLAYER_PHASE_SIDE);

    item_slot = fdps_unit_find_item_slot(REGEN_SWORD_UNIT_INDEX,
                                         REGEN_ITEM_MURAMASA);
    if (item_slot == REGEN_ITEM_NOT_HELD) {
        item_slot = fdps_unit_find_item_slot(REGEN_SWORD_UNIT_INDEX,
                                             REGEN_ITEM_MASAMUNE);
    }
    if (item_slot != REGEN_ITEM_NOT_HELD) {
        unit = fdps_get_unit_record(REGEN_SWORD_UNIT_INDEX);
        if ((unit->inventory_slots[item_slot * 2] & REGEN_ITEM_EQUIPPED) != 0 &&
            unit->flags == 0 &&
            unit->mp_current != unit->mp_max) {
            mp_restored = fdps_unit_restore_mp(REGEN_SWORD_UNIT_INDEX,
                                               REGEN_MP_AMOUNT);
            fdps_map_cursor_move_to_unit(REGEN_SWORD_UNIT_INDEX);
            fdps_show_number_indicator(mp_restored, REGEN_MP_GLYPH_BASE,
                                       REGEN_SWORD_UNIT_INDEX);
            fdps_play_indicator_queue();
        }
    }

    item_slot = fdps_unit_find_item_slot(REGEN_RING_UNIT_INDEX,
                                         REGEN_ITEM_KATAMI_RING);
    if (item_slot != REGEN_ITEM_NOT_HELD) {
        unit = fdps_get_unit_record(REGEN_RING_UNIT_INDEX);
        if ((unit->inventory_slots[item_slot * 2] & REGEN_ITEM_EQUIPPED) != 0 &&
            unit->flags == 0 &&
            unit->mp_current != unit->mp_max) {
            mp_restored = fdps_unit_restore_mp(REGEN_RING_UNIT_INDEX,
                                               REGEN_MP_AMOUNT);
            fdps_map_cursor_move_to_unit(REGEN_RING_UNIT_INDEX);
            fdps_show_number_indicator(mp_restored, REGEN_MP_GLYPH_BASE,
                                       REGEN_RING_UNIT_INDEX);
            fdps_play_indicator_queue();
        }
    }

    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if (fdps_unit_find_item_slot(unit_index, REGEN_ITEM_MANA_SHARD) !=
                REGEN_ITEM_NOT_HELD &&
            unit->flags == 0 &&
            unit->mp_current != unit->mp_max) {
            mp_restored = fdps_unit_restore_mp(unit_index, REGEN_MP_AMOUNT);
            fdps_map_cursor_move_to_unit(unit_index);
            fdps_show_number_indicator(mp_restored, REGEN_MP_GLYPH_BASE,
                                       unit_index);
            fdps_play_indicator_queue();
        }
    }

    data_fdps_map_cursor_draw_mode = ADVANCE_CURSOR_NORMAL;
    if (fdps_unit_is_retired(ADVANCE_FOCUS_UNIT_INDEX) == 0) {
        fdps_map_cursor_move_to_unit(ADVANCE_FOCUS_UNIT_INDEX);
    }
    fdps_flush_keyboard_queue();
    data_fdps_ui_play_active_flag = 1;
}

/* ---- fdps_battle_player_phase_loop, 0002bae0 --------------------------- */

/* The tile size the cursor steps, the view scrolls and the map extent is
   scaled by: IMUL EAX,EAX,0x18 at 0002bb1e and 0002bb2d, the SUB/ADD
   ...,0x18 of every cursor and view move, and the IMUL ...,0x18 of the unit
   tile handed to fdps_map_cursor_move_to at 0002bcc8 and 0002bcd6. */
#define PHASE_LOOP_TILE_PIXELS 0x18

/* The make codes the loop dispatches on, each a CMP dword ptr [EBP-0x24],n. */
#define PHASE_KEY_ESC 0x01
#define PHASE_KEY_ENTER 0x1c
#define PHASE_KEY_Z 0x2c
#define PHASE_KEY_SPACE 0x39
#define PHASE_KEY_F1 0x3b
#define PHASE_KEY_F2 0x3c
#define PHASE_KEY_HOME 0x47
#define PHASE_KEY_UP 0x48
#define PHASE_KEY_LEFT 0x4b
#define PHASE_KEY_KEYPAD_5 0x4c
#define PHASE_KEY_RIGHT 0x4d
#define PHASE_KEY_DOWN 0x50
#define PHASE_KEY_DELETE 0x53

/* MOV byte ptr [EAX],0xff at 0002bb13: the "no key" value the latch is primed
   with before the loop (keybd.h). */
#define PHASE_KEY_NONE 0xff

/* CMP dword ptr [EBP-0x2c],0x5 / JLE at 0002bb6f: how many passes of the same
   held code after the first are sent to the reduced dispatch instead of the
   full one.  Pass 0 of a code and pass 6 onwards take the full dispatch. */
#define PHASE_HOLD_SUPPRESS_PASSES 5

/* AND AL,0x85 at 0002bc89: the status-byte bits that make the unit cycle pass
   a record over -- bit 0 retired, bit 7 acted this turn, and bit 2 as well.
   This is NOT UNIT_BUSY_FLAGS_MASK: the phase engine's own eligibility test
   is 0x81, and the cycle alone also skips bit 2. */
#define PHASE_CYCLE_SKIP_FLAGS 0x85

/* AND AL,0x80 at 0002bd7a: the only status bit the Enter arm tests before
   handing a player unit its turn -- acted this turn. */
#define PHASE_UNIT_ACTED_FLAG 0x80

/* CMP dword ptr [EBP-0x8],-0x1 at 0002bd2f and 0002bdea:
   fdps_battle_find_unit_at_cursor's "no unit on this tile" (unit.h). */
#define PHASE_NO_UNIT_AT_CURSOR (-1)

/* The view's edge margins.  The view is nudged back one tile once the cursor
   is less than a tile from its left or top edge (CMP dword ptr [EBP-0x18],0x18
   / JL at 0002be21, [EBP-0x14] at 0002be5b), and on one tile once it is more
   than 0x108 across or 0x90 down (JLE at 0002be37 and 0002be71) -- the same
   two limits fdps_map_cursor_select_loop uses (mapcur.c).  The far clamps keep
   a 0x138 x 0xc0 window inside the map (SUB EAX,0x138 at 0002be3c, SUB
   EAX,0xc0 at 0002be76), which is the view fdps_render_view_frame presents. */
#define PHASE_VIEW_MAX_OFFSET_X 0x108
#define PHASE_VIEW_MAX_OFFSET_Y 0x90
#define PHASE_VIEW_WIDTH 0x138
#define PHASE_VIEW_HEIGHT 0xc0

/* The sound every accepted cursor step plays: MOV EAX,0x61e78 / PUSH EAX
   ahead of all four calls, the same literal fdps_map_cursor_select_loop
   pushes (mapcur.c).  fdps_play_sfx upper-cases what it is handed in place,
   which the data segment a string literal lands in allows (audio.h). */
#define PHASE_CURSOR_MOVE_SFX "Beep.wav"

/* 0002bae0.  The player phase on the battle map: one pass per frame, reading
   the latched make code, moving the cursor, cycling to the next unit that can
   still be given a turn, and handing the unit under the cursor its turn, until
   the battle ends or the system menu answers non-zero.  The plain -4s frame --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x50, nothing read above [EBP],
   one epilogue at 0002bea6 with a bare RET -- and the one caller, main at
   00029366, makes a bare CALL and reads data_fdps_chapter_event_or_battle_end_code
   straight after it, never EAX.

   ITS PACING IS ONE FRAME A PASS.  Every pass ends in fdps_render_view_frame
   (0002be8c), whose tick wait is the only clock the loop has, so the hold
   counter counts frames.

   THE LATCH IS PRIMED ONCE, BEFORE THE LOOP.  0002bb13 stores 0xff through the
   pointer fdps_keyboard_scancode_ptr answered; nothing in the loop stores it
   again.  fdps_flush_keyboard_queue, which the Enter and F2 arms call, rewinds
   the queue and leaves the latch alone (keybd.h), so after a unit's turn or a
   status window the latch still holds whatever the ISR last wrote into it.

   THE PREVIOUS CODE STARTS ON WHATEVER THE STACK HELD.  [EBP-0x30] is not
   written before the first compare at 0002bb4f -- the four prologue stores
   reach [EBP-0x2c], [EBP-0x28], [EBP-0x10] and [EBP-0xc] -- so the first pass
   compares the freshly primed 0xff against garbage.  It settles on its own,
   exactly as in fdps_map_cursor_select_loop (mapcur.c): 0xff matches no arm,
   and the first real key differs from the previous code and resets the
   counter.  Seeding it would be a store the original does not make.

   THE TWO DISPATCHES ARE DISJOINT.  A code seen for the first time (hold
   count 0) or held past five passes goes to the full dispatch: the arrows,
   the unit cycle and Enter/Space.  Passes 1 to 5 of a held code go to the
   reduced one, which knows only F1 (the overview) and F2/Home (the status
   window of the unit under the cursor).  So an arrow moves on its first frame
   and then repeats from the sixth, and F1, F2 and Home do nothing on the frame
   they are first seen and fire on the second -- the JLE at 0002bb73 jumps
   past the whole full chain to 0002bdc4, and the full chain has no F1, F2 or
   Home arm.

   AN ARROW THAT CANNOT MOVE FALLS THROUGH to the next test rather than
   stopping the chain: up at the top edge is compared against down, left,
   right and the rest in turn, and matches none of them.  So the chain is
   written as one else-if ladder whose rungs each carry their bound.

   THE UNIT CYCLE is ESC, Z, keypad 5 and Delete.  It starts at the index the
   previous cycle saved ([EBP-0xc], 0 on entry) and tries at most
   data_fdps_map_unit_count records, wrapping modulo that count (IDIV dword
   ptr [0x00060150], so a count of 0 divides by zero -- but the loop runs no
   iteration then, and the divide is only reached from inside it).  The first
   record with none of PHASE_CYCLE_SKIP_FLAGS and side 2 has the cursor walked
   onto it and the index after it saved; the paralysis counter is not
   consulted, so a paralysed unit is cycled to.  If none qualifies the saved
   index is left alone.

   ENTER AND SPACE resolve the unit under the cursor.  No unit opens the
   system menu, and its answer is the loop's own exit flag: the rest of the
   pass still scrolls the view and draws a frame, and the flag is tested at the
   top of the NEXT pass.  A unit zeroes data_fdps_battle_pending_xp_credit
   first, whatever it is; then a player unit that has not acted and is not
   paralysed takes its turn -- followed, if the battle did not end during it,
   by the end-of-phase check -- and any other unit only has its status window
   shown.

   THE VIEW SCROLL is decided from the cursor's offsets into the view taken
   once, before any of the four nudges, and each axis can move at most one
   tile a pass.

   THE BATTLE-END CODE IS READ ONLY AT THE BOTTOM OF A PASS (0002be91), after
   the frame is drawn, and turned into the exit flag there; the top of the loop
   tests only the flag.  So a code already set on entry still costs one full
   pass and one frame.

   The two record addresses are formed in-line, each through an
   argument-shaped slot and a result slot ([EBP-0x3c]/[EBP-0x40]/[EBP-0x44] in
   the cycle arm, [EBP-0x48]/[EBP-0x4c]/[EBP-0x50] in the Enter arm): that is
   fdps_get_unit_record (0002d210) expanded in place, and there is no CALL, so
   it is written here as the open-coded address.  The cycle arm forms its
   record twice from the same index with nothing in between that could move
   the array, so one pointer serves both reads. */
void fdps_battle_player_phase_loop(void)
{
    /* EBP-0x38: the latched make code the keyboard ISR writes (keybd.h). */
    unsigned char *scancode_latch;
    /* EBP-0x20 / EBP-0x1c: the map's full extent in pixels, from the two
       signed 16-bit tile dimensions at the head of the movement grid
       (MOVSX word ptr [EAX] and [EAX+0x2], gamedata.h). */
    int map_pixel_width;
    int map_pixel_height;
    /* EBP-0x28: non-zero once the loop is to end at the top of the next
       pass -- the system menu's answer, or 1 for a battle-end code. */
    int exit_requested;
    /* EBP-0x24: what the latch held this pass, widened without sign
       (XOR EAX,EAX / MOV AL,byte ptr [EDX]). */
    int scancode;
    /* EBP-0x30: what it held on the previous pass.  Deliberately left
       uninitialised -- see above. */
    int prev_scancode;
    /* EBP-0x2c: how many passes in a row, after the first, have read the same
       code.  Compared signed (JLE). */
    int hold_count;
    /* EBP-0xc: the index the next unit cycle starts from. */
    int next_cycle_index;
    /* EBP-0x10: the index the running unit cycle is looking at. */
    int cycle_index;
    /* EBP-0x8 in the cycle arm: how many records the cycle has tried. */
    int cycle_tries;
    /* EBP-0x8 in the Enter and F2 arms: the unit under the cursor, or
       PHASE_NO_UNIT_AT_CURSOR. */
    int cursor_unit_index;
    /* EBP-0x34: the record the arm being taken is looking at. */
    struct fdps_unit_record *unit;
    /* EBP-0x18 / EBP-0x14: the cursor's offset from the view origin, taken
       before the view is nudged. */
    int view_offset_x;
    int view_offset_y;

    hold_count = 0;
    exit_requested = 0;
    cycle_index = 0;
    next_cycle_index = 0;
    scancode_latch = fdps_keyboard_scancode_ptr();
    *scancode_latch = PHASE_KEY_NONE;
    map_pixel_width = (int) *(short *) data_fdps_battle_move_grid_ptr
                      * PHASE_LOOP_TILE_PIXELS;
    map_pixel_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2)
                       * PHASE_LOOP_TILE_PIXELS;

    while (exit_requested == 0) {
        fdps_cd_music_repeat_poll();
        scancode = (int) *scancode_latch;
        if (scancode == prev_scancode) {
            hold_count++;
        } else {
            prev_scancode = scancode;
            hold_count = 0;
        }

        if (hold_count == 0 || hold_count > PHASE_HOLD_SUPPRESS_PASSES) {
            if (scancode == PHASE_KEY_UP &&
                data_fdps_map_cursor_world_y >= PHASE_LOOP_TILE_PIXELS) {
                data_fdps_map_cursor_world_y -= PHASE_LOOP_TILE_PIXELS;
                fdps_play_sfx(PHASE_CURSOR_MOVE_SFX);
            } else if (scancode == PHASE_KEY_DOWN &&
                       map_pixel_height - PHASE_LOOP_TILE_PIXELS >
                       data_fdps_map_cursor_world_y) {
                data_fdps_map_cursor_world_y += PHASE_LOOP_TILE_PIXELS;
                fdps_play_sfx(PHASE_CURSOR_MOVE_SFX);
            } else if (scancode == PHASE_KEY_LEFT &&
                       data_fdps_map_cursor_world_x >= PHASE_LOOP_TILE_PIXELS) {
                data_fdps_map_cursor_world_x -= PHASE_LOOP_TILE_PIXELS;
                fdps_play_sfx(PHASE_CURSOR_MOVE_SFX);
            } else if (scancode == PHASE_KEY_RIGHT &&
                       map_pixel_width - PHASE_LOOP_TILE_PIXELS >
                       data_fdps_map_cursor_world_x) {
                data_fdps_map_cursor_world_x += PHASE_LOOP_TILE_PIXELS;
                fdps_play_sfx(PHASE_CURSOR_MOVE_SFX);
            } else if (scancode == PHASE_KEY_ESC || scancode == PHASE_KEY_Z ||
                       scancode == PHASE_KEY_KEYPAD_5 ||
                       scancode == PHASE_KEY_DELETE) {
                cycle_index = next_cycle_index;
                for (cycle_tries = 0; cycle_tries < data_fdps_map_unit_count;
                     cycle_tries++) {
                    unit = (struct fdps_unit_record *)
                           (data_fdps_map_unit_array_ptr +
                            cycle_index * (int) sizeof(struct fdps_unit_record));
                    if ((unit->flags & PHASE_CYCLE_SKIP_FLAGS) == 0 &&
                        (int) unit->side == PLAYER_PHASE_SIDE) {
                        fdps_map_cursor_move_to(
                            (int) unit->pos_x * PHASE_LOOP_TILE_PIXELS,
                            (int) unit->pos_y * PHASE_LOOP_TILE_PIXELS);
                        next_cycle_index =
                            (cycle_index + 1) % data_fdps_map_unit_count;
                        break;
                    }
                    cycle_index = (cycle_index + 1) % data_fdps_map_unit_count;
                }
            } else if (scancode == PHASE_KEY_SPACE ||
                       scancode == PHASE_KEY_ENTER) {
                fdps_flush_keyboard_queue();
                cursor_unit_index = fdps_battle_find_unit_at_cursor();
                if (cursor_unit_index != PHASE_NO_UNIT_AT_CURSOR) {
                    unit = (struct fdps_unit_record *)
                           (data_fdps_map_unit_array_ptr +
                            cursor_unit_index *
                            (int) sizeof(struct fdps_unit_record));
                    data_fdps_battle_pending_xp_credit = 0;
                    if ((int) unit->side == PLAYER_PHASE_SIDE &&
                        (unit->flags & PHASE_UNIT_ACTED_FLAG) == 0 &&
                        unit->status_timers[STATUS_TIMER_PARALYSIS] == 0) {
                        fdps_battle_unit_turn(cursor_unit_index);
                        if (data_fdps_chapter_event_or_battle_end_code == 0) {
                            fdps_battle_end_phase_if_all_units_done();
                        }
                    } else {
                        fdps_battle_show_unit_status_window(cursor_unit_index);
                    }
                } else {
                    exit_requested = fdps_battle_system_menu();
                }
            }
        } else if (scancode == PHASE_KEY_F1) {
            fdps_battle_map_overview();
        } else if (scancode == PHASE_KEY_F2 || scancode == PHASE_KEY_HOME) {
            fdps_flush_keyboard_queue();
            cursor_unit_index = fdps_battle_find_unit_at_cursor();
            if (cursor_unit_index != PHASE_NO_UNIT_AT_CURSOR) {
                fdps_battle_show_unit_status_window(cursor_unit_index);
            }
        }

        view_offset_x = data_fdps_map_cursor_world_x
                        - data_fdps_battle_view_window_origin_x;
        view_offset_y = data_fdps_map_cursor_world_y
                        - data_fdps_battle_view_window_origin_y;
        if (data_fdps_battle_view_window_origin_x >= PHASE_LOOP_TILE_PIXELS &&
            view_offset_x < PHASE_LOOP_TILE_PIXELS) {
            data_fdps_battle_view_window_origin_x -= PHASE_LOOP_TILE_PIXELS;
        }
        if (view_offset_x > PHASE_VIEW_MAX_OFFSET_X &&
            map_pixel_width - PHASE_VIEW_WIDTH >
            data_fdps_battle_view_window_origin_x) {
            data_fdps_battle_view_window_origin_x += PHASE_LOOP_TILE_PIXELS;
        }
        if (data_fdps_battle_view_window_origin_y >= PHASE_LOOP_TILE_PIXELS &&
            view_offset_y < PHASE_LOOP_TILE_PIXELS) {
            data_fdps_battle_view_window_origin_y -= PHASE_LOOP_TILE_PIXELS;
        }
        if (view_offset_y > PHASE_VIEW_MAX_OFFSET_Y &&
            map_pixel_height - PHASE_VIEW_HEIGHT >
            data_fdps_battle_view_window_origin_y) {
            data_fdps_battle_view_window_origin_y += PHASE_LOOP_TILE_PIXELS;
        }
        fdps_render_view_frame();
        if (data_fdps_chapter_event_or_battle_end_code != 0) {
            exit_requested = 1;
        }
    }
}

/* ---- fdps_battle_end_phase_if_all_units_done, 0002ea10 ----------------- */

/* 0002ea10.  Decides whether the player phase is over: one counted walk over
   the whole unit array, and a call to fdps_battle_advance_turn when no unit
   on side 2 can still be given a turn.  The plain -4s frame -- PUSH
   EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x18, nothing read above [EBP], one
   epilogue at 0002eaa2 with a bare RET -- and the one caller,
   fdps_battle_player_phase_loop, makes a bare CALL.

   THE FLAG is the dword at EBP-0x4, stored 1 before the walk and 0 by a unit
   that can still act, and it is never stored 1 again: the walk has no early
   exit (its only backward branch is JMP 0x0002ea2a, the loop's own) but the
   first idle unit settles the answer.  The test after the walk is CMP dword
   ptr [EBP-0x4],0x1 / JNZ.

   A UNIT CAN STILL ACT when all three hold, tested in this order: neither bit
   of UNIT_BUSY_FLAGS_MASK in the status byte at +5 (AND AL,0x81 / JNZ), the
   side byte at +6 equal to PLAYER_PHASE_SIDE (AND EAX,0xff / CMP EAX,0x2 /
   JZ), and the paralysis counter at +0x26, status_timers[4], zero (CMP byte
   ptr [EAX+0x26],0x0 / JZ).  Equality tests only, so signedness decides
   nothing here.  The poison counter beside it is not read: a poisoned unit
   still holds the phase open, a paralysed one does not.

   The bound is re-read from data_fdps_map_unit_count on every iteration (CMP
   EAX,dword ptr [0x00060150] inside the loop at 0002ea2d).

   The record address is formed in-line: the index copied into an
   argument-shaped slot at EBP-0x14, IMUL by 0x50, the base read from
   [0x00069cd8], the sum parked in a result slot at EBP-0x10 and copied to the
   local at EBP-0x8.  That is fdps_get_unit_record (0002d210) expanded in place
   -- the inline fingerprint rebuild_info/build_flags.md describes, a
   parameter slot plus a result slot -- and there is no CALL, so it is written
   here as the open-coded address, which is behaviourally the same thing.

   Nothing called returns anything read: fdps_battle_advance_turn is void and
   the epilogue follows its CALL directly. */
void fdps_battle_end_phase_if_all_units_done(void)
{
    /* EBP-0x4: 1 until a player unit that can still act is found. */
    int all_units_done;
    /* EBP-0xc: the walk's index into the unit array. */
    int unit_index;
    /* EBP-0x8: that unit's record. */
    struct fdps_unit_record *unit;

    all_units_done = 1;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = (struct fdps_unit_record *)
               (data_fdps_map_unit_array_ptr +
                unit_index * (int) sizeof(struct fdps_unit_record));
        if ((unit->flags & UNIT_BUSY_FLAGS_MASK) == 0 &&
            (int) unit->side == PLAYER_PHASE_SIDE &&
            unit->status_timers[STATUS_TIMER_PARALYSIS] == 0) {
            all_units_done = 0;
        }
    }
    if (all_units_done == 1) {
        fdps_battle_advance_turn();
    }
}
