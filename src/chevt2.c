/* chevt2.c -- the scripted chapter-event handlers of chapters 8 to 14.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt2.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "chevt2.h"

/* The half of the AI byte the merge below keeps: AND DL,0xf0 at 00037a33.  The
   four bits it preserves are flags other code reads on their own -- 0x40 in
   fdps_map_actor_take_best_action and 0x80 in fdps_score_targets_for_item --
   while the four it drops are the behaviour code fdps_map_actor_behavior_step
   isolates with AND AL,0xf and dispatches on.  src/unit.c and src/chevt1.c
   spell the same mask out for the same field; it stays file-local at all three
   ends because no header owns it. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* The behaviour code the merge ORs in, and it is 0: the constant parked at
   [EBP-0x18] at 000379f1 is 0x0, copied on into [EBP-0x14], which is the slot
   the OR reads.  Mode 0 is the default chain that paths a unit toward the
   nearest opposing unit; mode 2, which is what map12.dat deploys all 37 of its
   units in, holds position and only scores attacks it can already make. */
#define AI_BEHAVIOR_MODE_ADVANCE 0

/* 000379d0.  Chapter 13's death-triggered event: 36 of the map's 37 enemies
   stop holding position and start advancing on the player.

   The body is one copy of the inline expansion the chapter 2, 5 and 7 handlers
   in chevt1.c carry -- fdps_object_set_field34_low_nibble_range (00036b60)
   with the constant argument triple (9, 0x2c, 0) -- and it has the same
   fingerprint: the three constants are parked at [EBP-0x20], [EBP-0x1c] and
   [EBP-0x18] (000379e3..000379f1), copied into a second set of slots at
   [EBP-0xc], [EBP-0x10] and [EBP-0x14] (000379f8..00037a07), and only then is
   the counter at [EBP-0x8] seeded from the first of them.  There is no CALL to
   that helper in the body; the only CALL is fdps_get_unit_record at 00037a22,
   once per iteration, so writing the range as a call to the helper would put a
   CALL in the rebuild that the original does not make.

   The compare at 00037a13 -- CMP EAX,dword ptr [EBP-0x10] / JLE 00037a1f -- is
   signed and inclusive, so the range is unit indices 9 through 0x2c and 0x2c is
   the last index written, not one past the end.  Chapter 13's map12.dat lays 9
   player records down at 0..8 and its own 37 deployment records at 9..0x2d, so
   the walk stays inside the unit array but stops one record short of it: unit
   index 0x2d keeps the behaviour the map gave it.  Nothing is range checked and
   data_fdps_map_unit_count is not consulted; both bounds are literals in the
   instruction stream.

   The merge is the same read-modify-write of the one byte as its siblings --
   MOV DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x14] / OR DH,DL /
   MOV [EAX+0x34],DH at 00037a30..00037a3e -- so the behaviour code goes to 0
   and the two AI flag bits in the high nibble are carried across untouched.

   The record pointer comes back in EAX from the CALL at 00037a22 and is stored
   to [EBP-0x4] at 00037a2a, then re-read at 00037a2d for the load and again at
   00037a36 for the store, so both halves of the merge address the record
   fetched by that iteration.

   There is no one-shot latch, unlike the chapter 5 handler: the instruction
   after the argument-slot store at 000379dc is the first of the three constant
   stores, with no compare between them.  The event is fired by a death script
   rather than a tile trigger, and the unit can only die once, so the data is
   what makes it happen once.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 000379dc writes zero over the incoming slot and nothing
   ever reads it back, so which unit died cannot reach anything this handler
   does; the store has no observable effect, because the slot belongs to the
   caller's outgoing argument area and the death-script runner drops it with
   ADD ESP,0x4 at 0001dcb4.

   Nothing sets EAX before the RET at 00037a49 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_13_event_enemies_advance(int unit_index)
{
    struct fdps_unit_record *unit;
    int advancing_unit_index;

    unit_index = 0;

    for (advancing_unit_index = 9;
         advancing_unit_index <= 0x2c;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }
}
