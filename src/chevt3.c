/* chevt3.c -- the scripted chapter-event handlers of chapters 15 to 19.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt3.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7 and chevt2.c for chapters 8 to 14.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "unit.h"
#include "chevt3.h"

/* The half of the AI byte the merge below keeps: AND DL,0xf0 at 00037b53.  The
   four bits it preserves are flags other code reads on their own -- 0x40 in
   fdps_map_actor_take_best_action at 00012c72 and 0x80 in
   fdps_score_targets_for_item at 00013380 -- while the four it drops are the
   behaviour code fdps_map_actor_behavior_step isolates with AND AL,0xf at
   00010062 and dispatches on.  src/unit.c, src/chevt1.c and src/chevt2.c spell
   the same mask out for the same field; it stays file-local at every end
   because no header owns it. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* The behaviour code the merge ORs in, and it is 0: the constant parked at
   [EBP-0x18] at 00037b11 is 0x0, copied on into [EBP-0x14] at 00037b27, which
   is the slot MOV DH,byte ptr [EBP-0x14] at 00037b59 reads.  Mode 0 is the
   default chain that paths a unit toward the nearest opposing unit; mode 2,
   which is what map14.dat deploys all of its units in, holds position and only
   scores attacks it can already make. */
#define AI_BEHAVIOR_MODE_ADVANCE 0

/* 00037af0.  Chapter 15's death-triggered event: the nine-unit enemy group
   holding the bridgehead in the top-right corner stops standing its ground and
   starts advancing on the party.

   The body is one copy of the inline expansion the chapter 2, 5 and 7 handlers
   in chevt1.c and the chapter 13 handler in chevt2.c carry --
   fdps_object_set_field34_low_nibble_range (00036b60) with the constant
   argument triple (0x1d, 0x25, 0) -- and it has the same fingerprint: the three
   constants are parked at [EBP-0x20], [EBP-0x1c] and [EBP-0x18]
   (00037b03..00037b11), copied into a second set of slots at [EBP-0xc],
   [EBP-0x10] and [EBP-0x14] (00037b18..00037b27), and only then is the counter
   at [EBP-0x8] seeded from the first of them at 00037b2a.  There is no CALL to
   that helper in the body; the only CALL is fdps_get_unit_record at 00037b42,
   once per iteration, so writing the range as a call to the helper would put a
   CALL in the rebuild that the original does not make.

   The compare at 00037b33 -- CMP EAX,dword ptr [EBP-0x10] / JLE 00037b3f -- is
   signed and inclusive, so the range is unit indices 0x1d through 0x25 and
   0x25 is the last index written, not one past the end.  Chapter 15's map14.dat
   lays 9 player records down at 0..8 and its own wave-0 deployment records
   1..43 at 9..0x33, so unit index = deployment record index + 8 and this range
   is records 21..29: the three barbarian warriors, two ice mages and three
   archers of the top-right bridgehead block, plus record 27, the beam turret
   that is the chapter's victory condition.  The turret has no movement
   allowance, so the mode change does not alter what it does; it is inside the
   range because the range is contiguous.  Nothing is range checked and
   data_fdps_map_unit_count is not consulted; both bounds are literals in the
   instruction stream.

   The merge is the same read-modify-write of the one byte as its siblings --
   MOV DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x14] / OR DH,DL /
   MOV [EAX+0x34],DH at 00037b50..00037b5e -- so the behaviour code goes to 0
   and the two AI flag bits in the high nibble are carried across untouched.

   The record pointer comes back in EAX from the CALL at 00037b42 and is stored
   to [EBP-0x4] at 00037b4a, then re-read at 00037b4d for the load and again at
   00037b56 for the store, so both halves of the merge address the record
   fetched by that iteration.

   There is no one-shot latch: the instruction after the argument-slot store at
   00037afc is the first of the three constant stores, with no compare between
   them.  The event is fired by a death script and the unit that carries it can
   only die once, so the data is what makes it happen once.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 00037afc writes zero over the incoming slot and nothing
   ever reads it back, so which unit the event fired for cannot reach anything
   this handler does; the store has no observable effect, because the slot
   belongs to the caller's outgoing argument area and the death-script runner
   drops it with ADD ESP,0x4 at 0001dcb4.

   Nothing sets EAX before the RET at 00037b69 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_15_event_activate_enemy_group(int unit_index)
{
    struct fdps_unit_record *unit;
    int advancing_unit_index;

    unit_index = 0;

    for (advancing_unit_index = 0x1d;
         advancing_unit_index <= 0x25;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }
}
