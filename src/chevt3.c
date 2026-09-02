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
#include "gamedata.h"
#include "unit.h"
#include "deploy.h"
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

/* 00038020.  Chapter 16's turn-scheduled event: one block of the map's enemies
   stops holding position and starts advancing on the party, and which block
   depends on the turn the counter is standing at.

   The whole body is a test on data_fdps_battle_turn_counter followed by one of
   two copies of the same inline expansion the chapter 15 handler above carries
   -- fdps_object_set_field34_low_nibble_range (00036b60) with a constant
   argument triple.  The turn-5 copy stages (0x19, 0x22, 0) at [EBP-0x20],
   [EBP-0x1c] and [EBP-0x18] (0003803c..0003804a) and the other stages
   (0x0a, 0x19, 0) at [EBP-0x24], [EBP-0x28] and [EBP-0x2c]
   (0003809e..000380ac); each then copies its triple into a second set of slots
   before seeding the counter from the first of them, which is that helper's
   fingerprint.  There is no CALL to the helper in the body -- the only CALL in
   either loop is fdps_get_unit_record, at 0003807b and 000380dd, once per
   iteration -- so writing either range as a call to it would put a CALL in the
   rebuild that the original does not make.

   The test at 00038033 is CMP dword ptr [0x00069ce8],0x5 / JNZ 0003809e, an
   equality on the turn counter with the second range as the fall-through, so
   only turn 5 reaches the first loop and every other turn reaches the second.
   Both compares between the counter and the top bound -- CMP EAX,[EBP-0x10] /
   JLE at 0003806c and CMP EAX,[EBP-0x34] / JLE at 000380ce -- are signed and
   inclusive, so 0x22 and 0x19 are the last indices written in their loops, not
   one past the end.  Index 0x19 is the top of one range and the bottom of the
   other and is released by whichever branch runs.

   Chapter 16's map15.dat lays 10 player records down at 0..9 and its 25 enemy
   deployment records at 0x0a..0x22, so unit index = deployment record index +
   10 and the two ranges are records 15..24 and 0..15.  Nothing is range
   checked and data_fdps_map_unit_count is not consulted; all four bounds are
   literals in the instruction stream.

   Each merge is the same read-modify-write of the one byte as the chapter 15
   handler's -- MOV DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x14] / OR DH,DL
   / MOV [EAX+0x34],DH at 00038089..00038097, and the same five at
   000380eb..000380f9 -- so the behaviour code goes to 0 and the two AI flag
   bits in the high nibble are carried across untouched.

   The record pointer comes back in EAX from each CALL and is stored to
   [EBP-0x4] (00038083) or [EBP-0x40] (000380e5), then re-read for the load and
   again for the store, so both halves of each merge address the record that
   iteration fetched.

   There is no one-shot latch: the only compare in the body is the one on the
   turn counter, and nothing records that a branch has run.  The map's own turn
   table is what makes each branch happen once.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003802c writes zero over the incoming slot before the
   counter is read and nothing ever reads it back, so which unit the event
   fired for cannot reach anything this handler does; the store has no
   observable effect, because the slot belongs to the caller's outgoing
   argument area and the turn-event runner drops it with ADD ESP,0x4 at
   0002e146.

   Nothing sets EAX before the RET at 00038104 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_16_event_enemies_advance_for_turn(int unit_index)
{
    struct fdps_unit_record *unit;
    int advancing_unit_index;

    unit_index = 0;

    if (data_fdps_battle_turn_counter == 5) {
        for (advancing_unit_index = 0x19;
             advancing_unit_index <= 0x22;
             advancing_unit_index++) {
            unit = fdps_get_unit_record(advancing_unit_index);
            unit->ai_behavior = (unsigned char)
                ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ADVANCE);
        }
    } else {
        for (advancing_unit_index = 0x0a;
             advancing_unit_index <= 0x19;
             advancing_unit_index++) {
            unit = fdps_get_unit_record(advancing_unit_index);
            unit->ai_behavior = (unsigned char)
                ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ADVANCE);
        }
    }
}

/* What the chapter 17 handler below takes off the battle turn counter to get
   the wave it asks for: SUB EAX,0x7 at 0003812b, on the dword loaded from
   data_fdps_battle_turn_counter one instruction earlier.  The counter is
   1-based and the turn-event runner fires while it still holds the turn whose
   phase has just ended, so the map's turn 8 record asks for wave 1 and its
   turn 9 record for wave 2. */
#define CH17_WAVE_TURN_OFFSET 7

/* How that wave is placed: XOR EAX,EAX / PUSH EAX at 00038123, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each unit goes on the
   nearest free walkable tile to its placement record's coordinates rather than
   on the coordinates themselves.  Wave 0, the group a map opens with, is the
   one deployed with this flag set. */
#define CH17_PLACE_EXACT 0

/* 00038110.  Chapter 17's turn-scheduled reinforcement event: brings on the
   wave of the current map's deployment table that is due for the turn just
   played.

   The whole body is one call.  The frame is the standard Watcom four-push one
   with an empty local area -- PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP /
   MOV EBP,ESP / SUB ESP,0x0 at 00038110..00038116 -- so there is no local
   here at all and the three arguments are computed straight into the pushes:
   XOR EAX,EAX / PUSH EAX, then MOV EAX,[0x00069ce8] / SUB EAX,0x7 / PUSH EAX,
   then PUSH dword ptr [0x00069cf4], at 00038123..0003812f.  The caller-cleans
   ADD ESP,0xc at 0003813a is this function's own, which is what makes the
   convention the stack one.

   The map number is data_fdps_chapter_current_chapter_id read at the call site
   and not anything this handler holds, so it is whichever chapter is loaded --
   the same way the chapter 10 ambush in chevt2.c reads it.

   The wave key is the raw subtraction with nothing on either side of it: no
   compare, no table and no lower bound.  Adding the guard that looks obvious
   would change behaviour rather than protect it, because a turn below 8 gives
   a negative key, which matches no deployment record -- fdps_deploy_wave
   compares an unsigned wave byte against this int -- while a key clamped to 0
   would match the map's whole opening army and deploy it a second time.

   Nothing guards the call and nothing records that it ran, so the handler
   fires its wave every time it is reached; what makes each wave arrive once is
   the map's turn table naming the slot once per turn.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003811c writes zero over the incoming slot before the
   counter is read and nothing ever reads it back, so which unit the event
   fired for cannot reach anything this handler does; the store has no
   observable effect, because the slot belongs to the caller's outgoing
   argument area and the turn-event runner drops it with ADD ESP,0x4 at
   0002e146.

   Nothing sets EAX between the CALL's return and the RET at 00038141, and no
   dispatcher reads what comes back, so the result is void. */
void fdps_chapter_17_event_deploy_wave_for_turn(int unit_index)
{
    unit_index = 0;

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                     data_fdps_battle_turn_counter - CH17_WAVE_TURN_OFFSET,
                     CH17_PLACE_EXACT);
}

/* The place_exact argument the chapter 18 handler below hands
   fdps_deploy_wave: XOR EAX,EAX / PUSH EAX at 00038163, so zero.  Zero is the
   value that does NOT take the placement record's tile as given -- it sends
   fdps_deploy_unit off to search the map for the nearest unoccupied walkable
   tile to those coordinates and put the unit there instead. */
#define CH18_PLACE_NEAREST_FREE_TILE 0

/* 00038150.  Chapter 18's turn-scheduled reinforcement event: brings on the
   wave of the current map's deployment table whose number is the battle turn
   counter's own value.

   The whole body is one call.  The frame is the standard Watcom four-push one
   with an empty local area -- PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP /
   MOV EBP,ESP / SUB ESP,0x0 at 00038150..00038156 -- so there is no local here
   at all and the three arguments are computed straight into the pushes:
   XOR EAX,EAX / PUSH EAX, then PUSH dword ptr [0x00069ce8], then PUSH dword
   ptr [0x00069cf4], at 00038163..0003816c.  The caller-cleans ADD ESP,0xc at
   00038177 is this function's own, which is what makes the convention the
   stack one.

   This is the chapter 17 handler above with the subtraction taken out, and the
   absence of it is the whole difference: the turn counter is pushed as it
   stands, with no offset, no compare, no table and no bound on either side of
   it, so the wave asked for is exactly the number the counter holds.  The
   shipped data is what that arrangement is built around.  map17.dat -- map
   index 17, the player's chapter 18 -- schedules slot 25 for side 0, the enemy
   phase, on turns 4, 5, 6, 7, 8, 9, 10, 11 and 13, and its 65 deployment
   records carry four units tagged with each of waves 4, 5, 6, 7, 8, 9, 10 and
   11 and fifteen tagged wave 13.  Wave number and turn number are the same set
   of nine values, which is why no adjustment is wanted here and why putting
   chapter 17's offset back in would deploy nothing on any of the nine.

   The map number is data_fdps_chapter_current_chapter_id read at the call site
   and not anything this handler holds, so it is whichever chapter is loaded.

   Nothing guards the call and nothing records that it ran, so the handler
   fires its wave every time it is reached; what makes each wave arrive once is
   the map's turn table naming the slot once per turn.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003815c writes zero over the incoming slot before either
   global is read and nothing ever reads it back, so which unit the event fired
   for cannot reach anything this handler does; the store has no observable
   effect, because the slot belongs to the caller's outgoing argument area and
   the turn-event runner drops it with ADD ESP,0x4 at 0002e146.

   Nothing sets EAX between the CALL's return and the RET at 0003817e, and no
   dispatcher reads what comes back, so the result is void. */
void fdps_chapter_18_event_deploy_wave_for_turn(int unit_index)
{
    unit_index = 0;

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                     data_fdps_battle_turn_counter,
                     CH18_PLACE_NEAREST_FREE_TILE);
}
