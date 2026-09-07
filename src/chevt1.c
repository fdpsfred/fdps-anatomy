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
#include "deploy.h"
#include "text.h"
#include "unit.h"
#include "chevt1.h"

/* The mode 13h aperture and its row stride, PUSH 0xa0000 and PUSH 0x140 in
   front of all three of the chapter 3 handler's draws (00036bda / 00036bd5,
   00036c08 / 00036c03 and 00036c48 / 00036c43).  0xa0000 stays a literal here
   because it is where the display adapter answers and not the address of
   anything the linker places (rebuild_info/pitfalls.md, contract E). */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d in front of
   each of the same three draws: glyph fill, no cell background, and the shadow
   the outline colour becomes while the font's outline flag is clear.  Every
   ordinary line of spoken game text is drawn with these three. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* The two turn numbers the chapter 3 handler tests for, CMP dword ptr
   [0x00069ce8],0x3 at 00036bc3 and CMP dword ptr [0x00069ce8],0xe at 00036bf1.
   They are the first and the last of the twelve turns map02.dat's turn-event
   table schedules this slot for, so the chapter speaks as the reinforcements
   start arriving and again as the last of them arrives. */
#define CH03_FIRST_WAVE_TURN 3
#define CH03_LAST_WAVE_TURN 0xe

/* The three entries of the chapter's own FDETXT03.TXT block the handler draws:
   PUSH 0x13 at 00036bdf, PUSH 0x15 at 00036c0d and PUSH 0x14 at 00036c4d.
   0x13 and 0x15 are the 魔導士's lines, spoken on the first and the last of the
   scheduled turns; 0x14 is 尤利安's answer, drawn after the deployment. */
#define CH03_FIRST_TURN_TEXT_ID 0x13
#define CH03_LAST_TURN_TEXT_ID 0x15
#define CH03_AFTER_DEPLOY_TEXT_ID 0x14

/* What the handler takes off the battle turn counter to get the wave it asks
   for: DEC EAX at 00036c25, on the dword loaded from
   data_fdps_battle_turn_counter one instruction earlier.  The turn-event runner
   fires while the counter still holds the turn whose phase has just ended, so
   the map's turn 3 record asks for wave 2 and its turn 14 record for wave 13. */
#define CH03_WAVE_TURN_OFFSET 1

/* The placement file the wave is put down through: PUSH 0x2 at 00036c27, a
   literal and not the chapter global the chapter 10, 17 and 18 handlers read.
   It names "map02.cod" only -- the deployment records themselves come from
   whichever MAP%02d.DAT is resident -- so getting it wrong moves the arriving
   units to another map's coordinates without changing which of them arrive. */
#define CH03_PLACEMENT_MAP_NO 2

/* How that wave is placed: XOR EAX,EAX / PUSH EAX at 00036c1d, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each unit goes on the
   nearest free walkable tile to its placement record's coordinates rather than
   on the coordinates themselves. */
#define CH03_PLACE_EXACT 0

/* 00036bb0.  Chapter 3's turn-scheduled reinforcement event: on each of the
   player turns 3 through 14 it brings that turn's wave of 石巨神 onto the map,
   and on the first and the last of those turns the chapter speaks as well.

   The frame is the standard Watcom four-push one with an empty local area --
   PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 at
   00036bb0..00036bb6 -- so there is no local here at all and every argument is
   computed straight into the pushes.  The caller-cleans ADD ESP,0x1c after each
   draw and ADD ESP,0xc after the deployment are this function's own, which is
   what makes the convention the stack one.

   Everything it does is decided by data_fdps_battle_turn_counter: the chapter
   state reset sets it to 1, the save load restores it, and the turn driver
   raises it once per turn cycle.

   The two speech tests are one if/else -- the JNZ at 00036bca falls into the
   0xe test and the first arm's JMP at 00036bef jumps over it -- so at most one
   of the two lines is drawn.  They cannot both be reached anyway, 3 and 0xe
   being different values, but the shape is the assembly's.

   The deployment is unconditional and sits between the two halves of the
   chapter 3 test.  THE TEST FOR TURN 3 IS WRITTEN TWICE ON PURPOSE, once at
   00036bc3 and again at 00036c31, because entry 0x13 is spoken before the
   giants arrive and entry 0x14 after them.  Collapsing the two into one
   if-block puts both lines on screen before the units appear.

   The wave key is the raw decrement with nothing on either side of it: no
   compare, no table and no lower bound.  A counter of 0 would give a negative
   key, which matches no deployment record -- fdps_deploy_wave compares an
   unsigned wave byte against this int -- and a key clamped to 0 would instead
   match the map's whole opening army and deploy it a second time.

   Nothing guards the deployment and nothing records that it ran, so the handler
   fires its wave every time it is reached; what makes each wave arrive once is
   map02.dat's turn table naming the slot once per turn, twelve times, for turns
   3 through 14.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 00036bbc writes zero over the incoming slot before the
   counter is read and nothing ever reads it back, so which unit the event fired
   for cannot reach anything this handler does; the store has no observable
   effect, because the slot belongs to the caller's outgoing argument area and
   the turn-event runner drops it with ADD ESP,0x4 at 0002e146.

   Nothing sets EAX between the last CALL's return and the RET at 00036c61, and
   no dispatcher reads what comes back, so the result is void. */
void fdps_chapter_03_event_deploy_wave_for_turn(int unit_index)
{
    unit_index = 0;

    if (data_fdps_battle_turn_counter == CH03_FIRST_WAVE_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH03_FIRST_TURN_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter == CH03_LAST_WAVE_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH03_LAST_TURN_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    }

    fdps_deploy_wave(CH03_PLACEMENT_MAP_NO,
                     data_fdps_battle_turn_counter - CH03_WAVE_TURN_OFFSET,
                     CH03_PLACE_EXACT);

    if (data_fdps_battle_turn_counter == CH03_FIRST_WAVE_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH03_AFTER_DEPLOY_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    }
}

/* The slot of data_fdps_map_cell_event_triggered_flags (gamedata.h) that the
   one-shot handlers latch: byte ptr [0x000640e8], which is element 0x10 of the
   32-entry array based at 0x000640d8.

   The array's own indexer is a cell's raw event code and the shipped M%02d.DTL
   event planes only ever use codes 0 to 15, so element 0x10 is the first slot
   no map cell can reach and the handlers use it as private storage.  It is one
   slot shared by all of them -- fdps_chapter_03_event_deploy_wave_1 at
   00036c83, fdps_chapter_05_event_enemies_advance at 000370f3, the chapter 8,
   10, 15, 16, 19, 21, 23, 25, 26 and 30 handlers, and the chapter 15 and 26
   post-action checks all name the same address -- which is safe only because
   one chapter is loaded at a time and fdps_chapter_state_reset memsets the
   whole array when a chapter starts.

   Being inside the array is also what makes the latch survive a save: the save
   and load paths move all 0x20 bytes to and from offset 0x30a3 of the slot
   image, so a chapter reloaded after its event fired does not fire it again. */
#define CHAPTER_EVENT_ONE_SHOT_SLOT 0x10

/* The line the ambush speaks: PUSH 0x12 at 00036ca6, an entry of the same
   FDETXT%02d.TXT block the turn-scheduled handler above draws 0x13, 0x14 and
   0x15 out of. */
#define CH03_AMBUSH_TEXT_ID 0x12

/* The wave the ambush brings on: PUSH 0x1 at 00036cb9.  It is a literal, with
   no arithmetic and no read of data_fdps_battle_turn_counter anywhere in the
   body, which is what separates this handler from the turn-scheduled one
   above -- the ambush always asks for wave 1 whenever it is tripped. */
#define CH03_AMBUSH_WAVE 1

/* 00036c70.  Chapter 3's ambush trigger: the first time a unit steps onto the
   trigger region in the middle of map 2 the chapter speaks one line and the
   five wave-1 enemies arrive.

   The frame is the family's standard Watcom four-push one with an empty local
   area -- PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0
   at 00036c70..00036c76 -- so there is no local here at all and every argument
   is computed straight into the pushes.  The two caller-cleans, ADD ESP,0x1c
   after the draw and ADD ESP,0xc after the deployment, are this function's own,
   and the RET at 00036cc9 carries no immediate, so the convention is the stack
   one at both ends of the call.

   The guard is the shared one-shot latch: CMP byte ptr [0x000640e8],0x0 / JNZ
   straight to the epilogue at 00036c83..00036c8a, then MOV byte ptr
   [0x000640e8],0x1 at 00036c8c.  The test is against 0 and not against 1, so
   any non-zero value in the slot blocks the body, and the latch is raised
   before the draw rather than after the deployment, so a handler re-entered
   from inside either call could not run the body twice either.

   Past the guard both calls are unconditional and they are in this order --
   CALL 0x0001ff60 at 00036cae, CALL 0x00023830 at 00036cbd -- so the line is on
   screen before the enemies appear.

   The map number is the literal 2 pushed at 00036cbb and not the chapter global
   the chapter 10, 17 and 18 handlers read, so the arrivals take map 2's
   coordinates whatever chapter is loaded, and the placement flag is the zeroed
   EAX pushed at 00036cb6..00036cb8, so each unit goes on the nearest free
   walkable tile to its placement record rather than on the record's own tile.

   fdps_draw_text hands back a cursor in EAX and this handler discards it: the
   XOR EAX,EAX at 00036cb6 overwrites the register to build the deployment's
   third argument, and nothing between the ADD ESP,0x1c and that XOR reads it.
   Nothing sets EAX between the second CALL's return and the RET, and no
   dispatcher reads what comes back, so the result is void.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 00036c7c writes zero over the incoming slot before the
   latch is even tested and nothing ever reads it back, so which unit walked
   onto the trigger tile cannot reach anything this handler does; the store has
   no observable effect, because the slot belongs to the caller's outgoing
   argument area and the caller drops it with ADD ESP,0x4. */
void fdps_chapter_03_event_deploy_wave_1(int unit_index)
{
    unit_index = 0;

    if (data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_ONE_SHOT_SLOT] != 0) {
        return;
    }
    data_fdps_map_cell_event_triggered_flags[CHAPTER_EVENT_ONE_SHOT_SLOT] = 1;

    fdps_draw_text(data_fdps_current_chapter_text_ptr, CH03_AMBUSH_TEXT_ID,
                   (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                   MESSAGE_FG_COLOR, MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
    fdps_deploy_wave(CH03_PLACEMENT_MAP_NO, CH03_AMBUSH_WAVE,
                     CH03_PLACE_EXACT);
}

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

/* 000370e0.  Chapter 5's ambush: every unit the map has deployed beyond the
   player's own five and the guest hero stops holding position and starts
   advancing, so the imperial army attacks all at once.

   The guard is the one-shot latch, CMP byte ptr [0x000640e8],0x0 / JNZ to the
   epilogue at 000370f3, then MOV byte ptr [0x000640e8],0x1 at 000370fc.  The
   test is against 0 and not against 1, so any non-zero value in the slot blocks
   the body; the latch is written before the loop rather than after it, so a
   handler re-entered from inside the loop could not run it twice either.

   The loop is fdps_object_set_field34_low_nibble_range (00036b60) expanded
   inline with the constant argument triple (6, 0x22, 0), the same expansion the
   chapter 2 handler above carries four copies of and with the same fingerprint:
   the three constants are parked at [EBP-0x20], [EBP-0x1c] and [EBP-0x18]
   (00037103..00037111), copied into a second set of slots at [EBP-0xc],
   [EBP-0x10] and [EBP-0x14] (00037118..00037127), and only then is the counter
   seeded from the first of them.  There is no CALL to that helper in the body;
   the only CALL is fdps_get_unit_record, once per iteration, so writing the
   range as a call to the helper would put a CALL in the rebuild that the
   original does not make.

   The compare at 00037133 -- CMP EAX,dword ptr [EBP-0x10] / JLE -- is signed
   and inclusive, so the range is unit indices 6 through 0x22 and 0x22 is the
   last index written, not one past the end.  Chapter 5's map04.dat deploys 33
   records behind the 5 party slots and the opening script has all of them on
   the field before a unit can reach the trigger tile, so the walk stays inside
   the unit array; nothing here reads data_fdps_map_unit_count and nothing
   bounds the index.

   The merge is the same read-modify-write of the one byte as the chapter 2
   handler's -- MOV DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x14] / OR DH,DL /
   MOV [EAX+0x34],DH at 00037150..0003715e -- so the behaviour code goes to 0
   and the two AI flag bits in the high nibble are carried across untouched.

   The record pointer comes back in EAX from the CALL at 00037142 and is stored
   to [EBP-0x4] at 0003714a, then re-read for the load and again for the store,
   so both halves of the merge address the record fetched by that iteration.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 000370ec writes zero over the incoming slot before the
   guard and nothing ever reads it back, so which unit walked onto the trigger
   tile cannot reach anything this handler does; the store has no observable
   effect, because the slot belongs to the caller's outgoing argument area and
   the caller drops it with ADD ESP,0x4.

   Nothing sets EAX before the RET at 00037169 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_05_event_enemies_advance(int unit_index)
{
    struct fdps_unit_record *unit;
    int advancing_unit_index;

    unit_index = 0;

    if (data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_ONE_SHOT_SLOT] != 0) {
        return;
    }
    data_fdps_map_cell_event_triggered_flags[CHAPTER_EVENT_ONE_SHOT_SLOT] = 1;

    for (advancing_unit_index = 6;
         advancing_unit_index <= 0x22;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }
}

/* 00037250.  Chapter 7's turn-2 event: the arena's five opponents stop holding
   position and start advancing on the player.

   The body is one copy of the same inline expansion the two handlers above
   carry -- fdps_object_set_field34_low_nibble_range (00036b60) with the
   constant argument triple (4, 8, 0) -- and it has the same fingerprint: the
   three constants are parked at [EBP-0x20], [EBP-0x1c] and [EBP-0x18]
   (00037263..00037271), copied into a second set of slots at [EBP-0xc],
   [EBP-0x10] and [EBP-0x14] (00037278..00037287), and only then is the counter
   at [EBP-0x8] seeded from the first of them.  There is no CALL to that helper
   in the body; the only CALL is fdps_get_unit_record at 000372a2, once per
   iteration, so writing the range as a call to the helper would put a CALL in
   the rebuild that the original does not make.

   The compare at 00037293 -- CMP EAX,dword ptr [EBP-0x10] / JLE 0003729f -- is
   signed and inclusive, so the range is unit indices 4 through 8 and 8 is the
   last index written, not one past the end.  That last index is the one the
   event exists for: chapter 7's map06.dat puts 4 party records at indices 0..3,
   and Icon06.dat's two deploy opcodes place the four mercenaries at 4..7 with
   behaviour byte 0 and the champion at 8 with behaviour byte 2.  Only she is in
   the hold-position mode when this fires; the other four are already in mode 0
   and the loop is a no-op for them.  Nothing is range checked and
   data_fdps_map_unit_count is not consulted.

   The merge is the same read-modify-write of the one byte -- MOV DL,[EAX+0x34]
   / AND DL,0xf0 / MOV DH,[EBP-0x14] / OR DH,DL / MOV [EAX+0x34],DH at
   000372b0..000372be -- so the behaviour code goes to 0 and the two AI flag
   bits in the high nibble are carried across untouched.

   The record pointer comes back in EAX from the CALL and is stored to [EBP-0x4]
   at 000372aa, then re-read at 000372ad for the load and again at 000372b6 for
   the store, so both halves of the merge address the record fetched by that
   iteration.

   There is no one-shot latch here, unlike the chapter 5 handler: the first
   instruction after the frame is the argument-slot store and the loop follows
   it with no compare in between.  The event is fired by a turn-event record
   rather than a tile trigger, and map06.dat holds exactly one -- turn 2, phase
   0 -- so the data, not the code, is what makes it happen once.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003725c writes zero over the incoming slot and nothing
   ever reads it back, so which unit the event fired for cannot reach anything
   this handler does; the store has no observable effect, because the slot
   belongs to the caller's outgoing argument area and the caller drops it with
   ADD ESP,0x4 at 0002e146.

   Nothing sets EAX before the RET at 000372c9 and the dispatcher at 0002e140
   ignores what comes back, so the result is void. */
void fdps_chapter_07_event_enemies_advance(int unit_index)
{
    struct fdps_unit_record *unit;
    int advancing_unit_index;

    unit_index = 0;

    for (advancing_unit_index = 4;
         advancing_unit_index <= 8;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }
}
