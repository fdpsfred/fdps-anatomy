/* btlturn.c -- the battle's turn and phase engine.
 *
 * See btlturn.h for what the status byte at record +5 means and who reads it.
 * Everything here reaches the unit records through
 * data_fdps_map_unit_array_ptr and works in place on what it finds.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "aiscore.h"
#include "chapter.h"
#include "mapai.h"
#include "unit.h"
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
