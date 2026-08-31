/* chevt1.c -- the scripted chapter-event handlers of chapters 2 to 7.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the three
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt1.h for what each handler does.  Nothing here owns state; the
 * battle-end code is gamedata.h's.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "chevt1.h"

/* 00036cd0.  Two stores and a return, with no branch in the body at all.

   The store that matters is MOV dword ptr [0x00069da0],0x1 at 00036ce3: an
   unconditional write of the literal 1, the defeat code, into the battle-end
   global.  It is not a compare-and-set and not an or -- whatever the global
   held is gone, so a handler that fires after the chapter was already marked
   cleared turns that clear into a defeat.

   The first store, MOV dword ptr [EBP + 0x14],0x0 at 00036cdc, writes zero
   over the incoming argument slot and nothing ever reads it back.  That is the
   shape every handler in this family is built from: the frame is empty, SUB
   ESP,0x0 at 00036cd6, and the handlers that need a counter run their loops on
   the argument slot itself -- 0003795d and 00037985 in the chapter 8 handler
   initialise it to 0 the same way and then compare and INC it.  This handler
   has no loop, so the initialisation is all that is left of it.  The write is
   kept because it is what the original does; it has no observable effect
   either way, since the slot belongs to the caller's outgoing argument area
   and the caller discards it with ADD ESP,0x4 the moment the call returns.

   Nothing sets EAX before the RET at 00036cf1, and the dispatcher at 0002e140
   ignores what comes back, so the result really is void and not an int the
   callers happen not to read. */
void fdps_chapter_event_set_game_over(int unit_index)
{
    unit_index = 0;
    data_fdps_chapter_event_or_battle_end_code = 1;
}

/* The half of the AI byte the merge below keeps: AND DL,0xf0 at 00036d63, and
   the same mask again at 00036dc3, 00036e23 and 00036e83.  The four bits it
   preserves are flags other code reads on their own -- 0x40 in
   fdps_map_actor_take_best_action and 0x80 in fdps_score_targets_for_item --
   while the four it drops are the behaviour code fdps_map_actor_behavior_step
   isolates with AND AL,0xf and dispatches on.  src/unit.c spells the same mask
   out for the same field; it stays file-local at both ends because no header
   owns it. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* The behaviour code the four merges OR in, and it is 0 at all four sites: the
   constant parked at [EBP-0x18], [EBP-0x2c], [EBP-0x4c] and [EBP-0x6c] is
   0x0, copied on into the slot the OR reads.  Mode 0 is the default chain that
   paths a unit toward the nearest opposing unit; mode 2, which is what the map
   file deploys these units in, holds position until the unit's threat weights
   cross a threshold. */
#define AI_BEHAVIOR_MODE_ADVANCE 0

/* 00036d00.  Chapter 2's turn-10 event: seven of the cave's enemies stop
   holding position and start advancing on the player.

   The body is four copies of one loop, over the inclusive index ranges
   0x13..0x15, 0x11..0x11, 0x0d..0x0d and 0x08..0x09, and each copy is
   fdps_object_set_field34_low_nibble_range (00036b60) expanded inline with a
   constant argument triple -- (0x13, 0x15, 0), (0x11, 0x11, 0), (0x0d, 0x0d, 0)
   and (8, 9, 0).  There is no CALL to that function anywhere in the body; the
   only CALL in it is fdps_get_unit_record, four times, once per loop.  Writing
   the four ranges here as calls to the helper would put a CALL in the rebuild
   that the original does not make.

   The expansion is visible in the frame: each copy parks its three constants
   in one set of locals (00036d13..00036d21 for the first), copies them into a
   second set (00036d28..00036d37), and only then seeds the counter, which is
   the argument slot / parameter copy pair an inline expansion leaves behind.
   That is also why every range is written with a first and a last index even
   where they are equal.

   The compare at 00036d43 -- CMP EAX,dword ptr [EBP-0x10] / JLE -- is signed
   and inclusive, and the same shape stands at 00036da3, 00036e03 and 00036e63.
   Inclusive is what makes the two single-index ranges run once each rather
   than not at all; the seven indices are 8, 9, 0x0d, 0x11, 0x13, 0x14 and
   0x15.

   The merge is a read-modify-write of the one byte and not an assignment: MOV
   DL,byte ptr [EAX+0x34] / AND DL,0xf0 / MOV DH,byte ptr [EBP-0x14] / OR DH,DL
   / MOV byte ptr [EAX+0x34],DH.  Storing the mode whole would clear the two AI
   flag bits the scorers read out of the high nibble.

   Nothing bounds the indices and nothing reads data_fdps_map_unit_count: the
   seven are literals in the instruction stream and they are correct for
   chapter 2's map, which deploys two player slots ahead of them.  The record
   is resolved through fdps_get_unit_record on every iteration, so the array
   base is re-read per record.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 00036d0c writes zero over the incoming slot and nothing
   ever reads it back, so which unit the event fired for cannot reach anything
   this handler does.  The slot belongs to the caller's outgoing argument area
   and the caller drops it with ADD ESP,0x4, so the store has no observable
   effect; it is kept because it is what the original does.

   Nothing sets EAX before the RET at 00036e99 and the dispatcher at 0002e140
   ignores what comes back, so the result is void. */
void fdps_chapter_02_event_enemies_advance(int unit_index)
{
    struct fdps_unit_record *unit;
    int advancing_unit_index;

    unit_index = 0;

    for (advancing_unit_index = 0x13;
         advancing_unit_index <= 0x15;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }

    for (advancing_unit_index = 0x11;
         advancing_unit_index <= 0x11;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }

    for (advancing_unit_index = 0x0d;
         advancing_unit_index <= 0x0d;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }

    for (advancing_unit_index = 8;
         advancing_unit_index <= 9;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }
}
