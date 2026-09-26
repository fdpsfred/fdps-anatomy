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
#include "mapcur.h"
#include "mapdraw.h"
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

/* The slot of data_fdps_map_cell_event_triggered_flags (gamedata.h) the
   handler below latches: byte ptr [0x000640e9], element 0x11 of the same
   32-entry array based at 0x000640d8, one past the slot every other one-shot
   handler of this family shares.

   It has to be a different slot, and this is the one place in the family where
   that matters: map02.dat's tile-event table names two handlers at once --
   cell event code 1 goes to table slot 1, chapter 3's ambush above, and code 2
   to table slot 4, the handler below -- so the two events are live on the same
   map at the same time and a shared latch would let whichever fired first
   suppress the other.  Codes 0 to 15 are all the shipped M%02d.DTL event
   planes reach, so 0x11 is as unreachable by a cell as 0x10 is.

   Everything else about it is the shared slot's story: fdps_chapter_state_reset
   memsets the whole array when a chapter starts, and the save and load paths
   move all 0x20 bytes to and from the slot image, so the latch is cleared
   between chapters and carried across a save.  The chapter 8, 19, 21, 24 and
   25 handlers name this same byte for their own purposes. */
#define CHAPTER_EVENT_SECOND_ONE_SHOT_SLOT 0x11

/* The line the tile-triggered reinforcement speaks: PUSH 0x16 at 00036ee9, one
   entry past the 0x13, 0x14 and 0x15 the two handlers above draw out of the
   same FDETXT%02d.TXT block. */
#define CH03_REINFORCE_TEXT_ID 0x16

/* The wave it brings on: PUSH 0xe at 00036efc, matched against byte 0x15 of
   each 0x1a-byte deployment record of the resident MAP%02d.DAT block.  Four of
   map02.dat's eighty records carry it -- table indices 20 to 23, all on side 0
   and all character id 71 at level 6 -- so this is four enemy units. */
#define CH03_REINFORCE_WAVE 0xe

/* 00036ea0.  Chapter 3's tile-triggered reinforcement: the first unit that is
   not on side 0 to step onto the map's second trigger region brings on the four
   wave-14 enemies, and the chapter speaks a line as they arrive.

   The frame is the family's four-push one, but with a local this time -- SUB
   ESP,0x4 at 00036ea6 -- because the record pointer has to survive the two
   compares.  The two caller-cleans, ADD ESP,0x1c after the draw and ADD ESP,0xc
   after the deployment, are this function's own, as is the ADD ESP,0x4 after
   the record lookup, so the convention is the stack one at both ends.

   fdps_get_unit_record is called first and unconditionally -- PUSH dword ptr
   [EBP+0x14] / CALL 0x0002d210 at 00036eac, the result stored to [EBP-0x4] at
   00036eb8 -- so the lookup happens even on a call the latch is going to
   refuse.  It is the only value this function takes off a CALL, and it is read
   for one byte only, the side at record+6.

   The two tests are the JNZ at 00036ec2 over the latch and the JNZ at 00036ecb
   over that byte, and both paths that fail land on the same epilogue, which is
   the short circuit written here: a non-zero latch means the side byte is
   never loaded, so a stale record pointer cannot be dereferenced on a refused
   call.  The side test is CMP byte ptr [EAX+0x6],0x0 / JNZ, a plain test
   against 0 over the whole unsigned byte and not a sign test.  Side 0 is the
   enemy, 1 the guest and 2 the player's roster, so what it keeps out is an
   enemy walking over the trigger tile.

   The latch is raised at 00036ecf, before either call rather than after the
   deployment, so a handler re-entered from inside either call could not run the
   body twice; it is tested against 0 rather than against 1, so any non-zero
   value in the slot blocks the body.

   Past the guard both calls are unconditional and in this order -- CALL
   0x0001ff60 at 00036ef1, CALL 0x00023830 at 00036f00 -- so the line is on
   screen before the enemies appear.

   The map number handed to fdps_deploy_wave is the literal 2 pushed at
   00036efe and not data_fdps_chapter_current_chapter_id, which the chapter 10,
   17 and 18 handlers read at the same argument, so the arrivals take map 2's
   MAP02.COD coordinates whatever chapter is loaded.  The placement flag is the
   zeroed EAX pushed at 00036ef9, so each unit goes on the nearest free walkable
   tile to its placement record rather than on the record's own tile.

   fdps_draw_text hands back a cursor in EAX and this handler discards it: the
   XOR EAX,EAX at 00036ef9 overwrites the register to build the deployment's
   third argument and nothing between the ADD ESP,0x1c and that XOR reads it.
   Nothing sets EAX between the second CALL's return and the RET at 00036f0e,
   and no dispatcher reads what comes back, so the result is void. */
void fdps_chapter_03_event_deploy_wave_14(int unit_index)
{
    struct fdps_unit_record *triggering_unit;

    triggering_unit = fdps_get_unit_record(unit_index);

    if ((data_fdps_map_cell_event_triggered_flags[
             CHAPTER_EVENT_SECOND_ONE_SHOT_SLOT] == 0) &&
        (triggering_unit->side != 0)) {
        data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_SECOND_ONE_SHOT_SLOT] = 1;
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH03_REINFORCE_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(CH03_PLACEMENT_MAP_NO, CH03_REINFORCE_WAVE,
                         CH03_PLACE_EXACT);
    }
}

/* The line the turn-limit defeat speaks: PUSH 0x17 at 00036f36, the last of the
   24 entries of the same FDETXT%02d.TXT block the three handlers above draw
   0x12, 0x13, 0x14, 0x15 and 0x16 out of.  The entry opens with the speaker
   code -0x11 followed by 102, so the line is spoken by character 0x66, the
   chapter's mage -- which portrait opens around it is that entry's own token
   stream and not anything decided here. */
#define CH03_TURN_LIMIT_TEXT_ID 0x17

/* The wave it brings on: PUSH 0xf at 00036f49, matched against byte 0x15 of
   each 0x1a-byte deployment record of the resident MAP%02d.DAT block.  44 of
   map02.dat's records carry it, all of them character id 0x47 at level 6 -- the
   sealed 石巨神 -- so this is by far the largest arrival any handler of the
   family asks for. */
#define CH03_TURN_LIMIT_WAVE 0xf

/* 00036f10.  Chapter 3's turn-limit defeat: the deadline for killing the
   chapter's mage runs out, the chapter speaks its last line, the sealed 石巨神
   pour onto the map and the battle ends in defeat.

   The frame is the family's standard four-push one with an empty local area --
   PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 at
   00036f10..00036f16 -- so there is no local here at all and every argument is
   computed straight into the pushes.  The two caller-cleans, ADD ESP,0x1c after
   the draw at 00036f43 and ADD ESP,0xc after the deployment at 00036f52, are
   this function's own, and the RET at 00036f63 carries no immediate, so the
   convention is the stack one at both ends of the call.

   There is no branch anywhere in the body and no guard of any kind: no one-shot
   latch, no test of data_fdps_battle_turn_counter and no test of the battle-end
   global it is about to write.  What makes it fire once is map02.dat's turn
   table, which names this slot in a single record -- turn 0x16, side 0 -- and
   nothing else in the shipped data names slot 5 at all.

   The three statements are in this order and the order is load bearing.  CALL
   0x0001ff60 at 00036f3e puts the line on screen first, CALL 0x00023830 at
   00036f4d brings the giants on second, and MOV dword ptr [0x00069da0],0x1 at
   00036f55 is the instruction straight after the deployment's stack cleanup.
   Nothing redraws the map between the deployment and the store, so the 44 units
   enter the array and the battle ends without the handler ever showing them --
   see the note in chevt1.h.  The store is an unconditional write of the literal
   1, the defeat code, not a compare-and-set and not an or, so a chapter already
   marked cleared is turned into a defeat by it.

   The map number handed to fdps_deploy_wave is the literal 2 pushed at
   00036f4b and not data_fdps_chapter_current_chapter_id, which the chapter 10,
   17 and 18 handlers push at the same argument, so the arrivals take map 2's
   MAP02.COD coordinates whatever chapter is loaded.  The placement flag is the
   zeroed EAX pushed at 00036f46..00036f48, so each giant goes on the nearest
   free walkable tile to its placement record rather than on the record's own
   tile.

   fdps_draw_text hands back a cursor in EAX and this handler discards it: the
   XOR EAX,EAX at 00036f46 overwrites the register to build the deployment's
   third argument, and nothing between the ADD ESP,0x1c and that XOR reads it.
   fdps_deploy_wave returns nothing, and the only instruction between its return
   and the RET is the store to the battle-end global, which is a store to memory
   and does not touch EAX -- so nothing sets EAX for the return either, and no
   dispatcher reads what comes back.  The result is void.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 00036f1c writes zero over the incoming slot before anything
   else happens and nothing ever reads it back, so which unit the event fired
   for cannot reach anything this handler does; the store has no observable
   effect, because the slot belongs to the caller's outgoing argument area and
   the turn-event runner drops it with ADD ESP,0x4 at 0002e146. */
void fdps_chapter_03_event_turn_limit_game_over(int unit_index)
{
    unit_index = 0;

    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                   CH03_TURN_LIMIT_TEXT_ID,
                   (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                   MESSAGE_FG_COLOR, MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
    fdps_deploy_wave(CH03_PLACEMENT_MAP_NO, CH03_TURN_LIMIT_WAVE,
                     CH03_PLACE_EXACT);
    data_fdps_chapter_event_or_battle_end_code = 1;
}

/* The turn the chapter speaks on rather than reinforcing: CMP dword ptr
   [0x00069ce8],0x3 at 00036f7c.  It is one of the two turns map03.dat's
   turn-event table names for this slot -- the three-byte (turn, handler, side)
   records (3, 6, 0) and (5, 6, 0) at file offset 3 -- and this compare is the
   only thing that tells the two firings apart.  There is no compare against the
   other one: every turn that is not 3 takes the reinforcement path. */
#define CH04_SPEAK_TURN 3

/* The two entries of chapter 4's own FDETXT%02d.TXT block the handler draws:
   PUSH 0x17 at 00036f98 on the speaking turn and PUSH 0x18 at 00036ffb behind
   the arrivals.  0x17 opens with the portrait control code -0x11 followed by 98
   and switches to -0x11 101 half way through, so it is an exchange between
   enemy templates 0x62 and 0x65; 0x18 is the announcement line and is spoken by
   0x65 alone. */
#define CH04_SPEAK_TEXT_ID 0x17
#define CH04_ARRIVAL_TEXT_ID 0x18

/* The wave the reinforcement asks for: PUSH 0x5 at 00036fb0, matched against
   byte 0x15 of each 0x1a-byte deployment record of the resident MAP%02d.DAT
   block.  Seven of map03.dat's records carry it -- one level-8 mage at template
   0x66, four level-7 infantry at 0x62 and two level-6 cavalry at 0x58 --
   scripted along the low-x, high-y edge of the map. */
#define CH04_ARRIVAL_WAVE 5

/* How that wave is placed: XOR EAX,EAX / PUSH EAX at 00036fad, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each arrival goes on the
   nearest free walkable tile to its placement record's coordinates rather than
   on the coordinates themselves. */
#define CH04_PLACE_EXACT 0

/* Which unit the map cursor is parked on before the arrivals are held on
   screen: PUSH 0x1f at 00036fc0.  It is one of the seven records the
   deployment on the line above has just appended, so the value is only correct
   after that call and only against map03.dat's own unit count. */
#define CH04_ARRIVAL_CURSOR_UNIT 0x1f

/* How long the view is held over them: CMP dword ptr [EBP+0x14],0xc / JL at
   00036fd1, so twelve composed frames.  fdps_render_view_frame paces itself to
   one timer tick a frame, which is what makes this a duration and not just
   twelve repaints. */
#define CH04_ARRIVAL_HOLD_FRAMES 0xc

/* The two inclusive index ranges the behaviour merge walks, off the constants
   staged at 0003700b/00037012 and 0003706b/00037072.  The first is four units
   and the second is one, and both are inclusive of their last index -- the
   compares at 0003703b and 0003709b are CMP EAX,<last> / JLE, signed and
   inclusive -- so a half-open first range drops unit 0x12 and a half-open
   second range skips unit 0x0d altogether. */
#define CH04_ADVANCE_FIRST_INDEX 0x0f
#define CH04_ADVANCE_LAST_INDEX 0x12
#define CH04_ADVANCE_LONE_INDEX 0x0d

/* 00036f70.  Chapter 4's turn-scheduled event: on the enemy pass of turn 3 it
   speaks the chapter's scripted line and stops there, and on the enemy pass of
   turn 5 it brings the map's wave-5 enemies onto the battlefield, holds the
   view over them, announces them and puts five of the map's units onto the
   advancing behaviour.

   The frame is the family's four-push one with a local area this time -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x40 at
   00036f70..00036f76 -- because the two inline range walks each need their own
   argument, parameter and record slots.  Every caller-clean in the body is this
   function's own (ADD ESP,0x1c after each draw, ADD ESP,0xc after the
   deployment, ADD ESP,0x4 after the cursor move and after each record lookup),
   the RET at 000370d1 carries no immediate, and the only dispatcher that
   reaches the slot pushes one dword and drops it with ADD ESP,0x4 at 0002e146,
   so the convention is the stack one at both ends of the call.

   THE ONLY TEST IN THE BODY IS AGAINST TURN 3.  CMP dword ptr
   [0x00069ce8],0x3 / JNZ at 00036f7c is the whole of it: there is no compare
   against 5 anywhere, no one-shot latch and no test of the battle-end global,
   so every turn that is not 3 runs the reinforcement path in full.  What makes
   the handler fire twice and only twice is map03.dat's turn table, which names
   slot 6 in exactly two records.

   The speaking path is one draw and a jump straight to the epilogue at
   00036fa8, so turn 3 deploys nothing, moves no cursor, renders no frame and
   leaves every unit's behaviour byte alone.

   The reinforcement path runs in this order and the order is what the player
   sees: the arrivals are appended first (CALL 0x00023830 at 00036fb8), the
   cursor is walked onto one of them second (CALL 0x0002da50 at 00036fc2), the
   view is held over them for twelve frames third (CALL 0x0002beb0 at 00036fe1,
   twelve times), and only then is the announcement drawn (CALL 0x0001ff60 at
   00037003).  Drawing the line before the frames would announce enemies the
   player has not been shown yet.

   THE MAP NUMBER IS THE CHAPTER GLOBAL AND NOT A LITERAL.  PUSH dword ptr
   [0x00069cf4] at 00036fb2 is data_fdps_chapter_current_chapter_id, the same
   argument the chapter 10, 17 and 18 handlers read and the opposite of the four
   chapter 3 handlers above, which push the literal 2.  The placement records
   still come from whichever MAP%02d.DAT is resident; what the number chooses is
   the MAP%02d.COD coordinates the arrivals are put down at.

   THE FRAME COUNTER IS THE ARGUMENT SLOT.  MOV dword ptr [EBP+0x14],0x0 at
   00036fca writes zero over the incoming argument before the first compare and
   the loop then compares and INCs that same slot, so the counter and the
   parameter are one storage location.  Which unit the event fired for is
   therefore gone by the time the loop starts, and it was never read before
   that: the turn-event runner pushes a literal 0 at 0002e13e anyway.  The store
   has no observable effect on the caller either, because the slot belongs to
   its outgoing argument area and it drops it with ADD ESP,0x4.

   The two behaviour merges are read-modify-writes of one byte -- MOV DL,byte
   ptr [EAX+0x34] / AND DL,0xf0 / MOV DH,byte ptr [EBP-0x14] / OR DH,DL / MOV
   byte ptr [EAX+0x34],DH at 00037058..00037066 and the same again at
   000370b8..000370c6 -- so the behaviour code goes to 0 and the two AI flag
   bits in the high nibble are carried across untouched.  Storing the mode whole
   would clear bits 0x40 and 0x80, which fdps_map_actor_take_best_action and
   fdps_score_targets_for_item read on their own.

   Both ranges are fdps_object_set_field34_low_nibble_range (00036b60) expanded
   inline with a constant argument triple -- (0xf, 0x12, 0) and (0xd, 0xd, 0) --
   the same expansion the chapter 2 and chapter 5 handlers carry and with the
   same fingerprint: three constants parked in one set of slots, copied into a
   second set, and only then the counter seeded from the first of them.  There
   is no CALL to that helper anywhere in the body; the only CALL in either loop
   is fdps_get_unit_record, once per iteration, so writing the ranges as calls
   to the helper would put a CALL in the rebuild that the original does not
   make.  That is also why the one-unit second range is written with a first and
   a last index that are equal.

   Nothing bounds the five indices and nothing reads data_fdps_map_unit_count;
   they are literals in the instruction stream and they are runtime unit
   indices, not deployment record numbers.  The unit array only grows and a
   retired unit keeps its index, so slots 0..2, the guest hero at 3, the
   heroine ICON03.DAT deploys at 4, the five cliff-top pursuers at 5..9 and the
   stand-in at 10 all hold their places even though the cutscene retires the
   pursuers and the stand-in, and wave 1 lands at 11..0x19 in record order.
   That puts 0x0d on record 3 (the ice mage) and 0x0f..0x12 on records 6..9,
   the eastern group map03.dat authors at behaviour code 2 (deployment record
   byte 0x11, copied into record byte 0x34 at 0002379d).  The merges therefore
   really take that group off holding position; they are not a no-op, and
   recomputing the constants from wave 1's record order alone would land them
   on other units.

   fdps_draw_text hands back a cursor in EAX and both call sites discard it:
   after the first the next instruction is the JMP to the epilogue, and after
   the second it is MOV dword ptr [EBP-0x20],0xf.  fdps_get_unit_record's result
   is the only value the body keeps off a CALL -- stored to [EBP-0x4] at
   00037052 and to [EBP-0x40] at 000370b2, then reloaded for the load and again
   for the store, so both halves of each merge address the record that
   iteration fetched.  Nothing sets EAX before the RET and no dispatcher reads
   what comes back, so the result is void. */
void fdps_chapter_04_event_for_turn(int unit_index)
{
    /* The record a behaviour merge is standing on, refetched per index. */
    struct fdps_unit_record *unit;
    /* Which unit of the two ranges the merge has reached. */
    int advancing_unit_index;

    if (data_fdps_battle_turn_counter == CH04_SPEAK_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH04_SPEAK_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        return;
    }

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH04_ARRIVAL_WAVE,
                     CH04_PLACE_EXACT);
    fdps_map_cursor_move_to_unit(CH04_ARRIVAL_CURSOR_UNIT);

    /* The argument slot is the counter, as the assembly has it. */
    for (unit_index = 0;
         unit_index < CH04_ARRIVAL_HOLD_FRAMES;
         unit_index++) {
        fdps_render_view_frame();
    }

    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                   CH04_ARRIVAL_TEXT_ID,
                   (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                   MESSAGE_FG_COLOR, MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);

    for (advancing_unit_index = CH04_ADVANCE_FIRST_INDEX;
         advancing_unit_index <= CH04_ADVANCE_LAST_INDEX;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }

    for (advancing_unit_index = CH04_ADVANCE_LONE_INDEX;
         advancing_unit_index <= CH04_ADVANCE_LONE_INDEX;
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
   last index written, not one past the end.  Chapter 5's map04.dat has 33
   deployment records, three of them wave 0xff and never deployed.  The guest
   hero (wave 0) takes index 5 behind the 5 party slots and the opening script
   appends the other 29 at indices 6..0x22 before a unit can reach the trigger
   tile -- the wave 2 and wave 3 units among them are retired again inside the
   cutscene but keep their indices -- so the walk ends on the last unit and
   stays inside the unit array; nothing here reads data_fdps_map_unit_count
   and nothing bounds the index.

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

/* The wave the ambush brings on: PUSH 0x2 at 0003717f, matched against byte
   0x15 of each 0x1a-byte deployment record of the resident MAP%02d.DAT block.
   Seven of map05.dat's 32 records carry it -- three of id 0x62 at level 10, two
   of id 0x5d at level 9, one of id 0x58 at level 8 and one of id 0x66 at level
   10 -- appended at unit indices 0x1d..0x23. */
#define CH06_ARRIVAL_WAVE 2

/* How that wave is placed: XOR EAX,EAX / PUSH EAX at 0003717c, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each arrival goes on the
   nearest free walkable tile to its placement record's coordinates rather than
   on the coordinates themselves.  It has to be 0 here: the seven MAP05.COD
   anchors are the block (2..5, 8..9) in the lower left and the wave-0 guest
   hero already stands at (3, 8), inside it. */
#define CH06_PLACE_EXACT 0

/* Which unit the map cursor is parked on before the arrivals are held on
   screen: PUSH 0x1e at 0003718f.  It is the second of the seven records the
   deployment on the line above has just appended, so the value is only correct
   after that call and only against map05.dat's own unit count. */
#define CH06_ARRIVAL_CURSOR_UNIT 0x1e

/* How long the view is held over them: CMP dword ptr [EBP+0x14],0xc / JL at
   000371a0, so twelve composed frames, the same hold the chapter 4 handler
   gives its own arrivals.  fdps_render_view_frame paces itself to one timer
   tick a frame, which is what makes this a duration. */
#define CH06_ARRIVAL_HOLD_FRAMES 0xc

/* The line spoken over the arrivals: PUSH 0xd at 000371ca, the last of the 14
   entries of chapter 6's own FDETXT%02d.TXT block.  The entry opens with the
   portrait control code -0x11 followed by 12, so it is spoken by character
   0x0c -- map05.dat's one side-1 record, the chapter's guest hero. */
#define CH06_ARRIVAL_TEXT_ID 0x0d

/* The inclusive index range the behaviour merge walks, off the constants staged
   at 000371da and 000371e1, with the compare at 0003720a being the signed
   inclusive JLE.  4 is the first index past chapter 6's four party slots and
   0x22 is one short of the last arrival, so the range takes in the guest hero
   at index 5 as well as every enemy -- the chapter 5 handler's copy of the same
   loop starts at 6 instead, to leave its own hero alone. */
#define CH06_ADVANCE_FIRST_INDEX 4
#define CH06_ADVANCE_LAST_INDEX 0x22

/* 00037170.  Chapter 6's cavalry-death ambush: the enemy cavalryman posted in
   the lower right of the map is killed, the map's second wave of reinforcements
   marches in at the lower left, the chapter's guest hero speaks over it and
   every unit already on the field is released from hold-position into the
   all-out attack.

   The frame is the family's four-push one with a local area -- PUSH EBX / PUSH
   ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x20 at
   00037170..00037176 -- because the inline range walk needs its argument,
   parameter and record slots.  Every caller-clean in the body is this
   function's own (ADD ESP,0xc after the deployment, ADD ESP,0x4 after the
   cursor move and after each record lookup, ADD ESP,0x1c after the draw), the
   RET at 00037240 carries no immediate, and the death-script runner pushes one
   dword and drops it with ADD ESP,0x4 at 0001dcb4, so the convention is the
   stack one at both ends of the call.

   THERE IS NO GUARD OF ANY KIND IN THE BODY: no one-shot latch, no test of
   data_fdps_battle_turn_counter and no compare anywhere except the two loop
   bounds.  What makes the event happen once is the data -- map05.dat gives the
   death script naming this slot to exactly one of its records -- so a second
   call would deploy the wave a second time.

   The four statements run in this order and the order is what the player sees:
   the arrivals are appended first (CALL 0x00023830 at 00037187), the cursor is
   walked onto one of them second (CALL 0x0002da50 at 00037191), the view is
   held over them third (CALL 0x0002beb0 at 000371b0, twelve times), and only
   then is the line drawn (CALL 0x0001ff60 at 000371d2) -- drawing it earlier
   would speak over enemies the player has not been shown.

   THE MAP NUMBER IS THE CHAPTER GLOBAL AND NOT A LITERAL.  PUSH dword ptr
   [0x00069cf4] at 00037181 is data_fdps_chapter_current_chapter_id, the same
   argument the chapter 4 handler above reads and the opposite of the four
   chapter 3 handlers, which push the literal 2.  The deployment records still
   come from whichever MAP%02d.DAT is resident; what the number chooses is the
   MAP%02d.COD coordinates the arrivals are put down at.

   THE FRAME COUNTER IS THE ARGUMENT SLOT.  MOV dword ptr [EBP+0x14],0x0 at
   00037199 writes zero over the incoming argument -- after the deployment and
   the cursor move, not before them -- and the loop compares and INCs that same
   slot, so the counter and the parameter are one storage location.  Which unit
   the event fired for is therefore gone by the time the loop starts and nothing
   read it before that; the store has no observable effect on the caller either,
   the slot belonging to its outgoing argument area.

   The range walk is fdps_object_set_field34_low_nibble_range (00036b60)
   expanded inline with the constant argument triple (4, 0x22, 0), the same
   expansion the chapter 2, 5 and 7 handlers carry and with the same
   fingerprint: the three constants are parked at [EBP-0x20], [EBP-0x1c] and
   [EBP-0x18] (000371da..000371e8), copied into a second set of slots at
   [EBP-0xc], [EBP-0x10] and [EBP-0x14] (000371ef..000371fe), and only then is
   the counter at [EBP-0x8] seeded from the first of them.  There is no CALL to
   that helper in the body; the only CALL in the loop is fdps_get_unit_record,
   once per iteration, so writing the range as a call to it would put a CALL in
   the rebuild that the original does not make.

   The merge is a read-modify-write of the one byte -- MOV DL,[EAX+0x34] / AND
   DL,0xf0 / MOV DH,[EBP-0x14] / OR DH,DL / MOV [EAX+0x34],DH at
   00037227..00037235 -- so the behaviour code goes to 0 and the two AI flag
   bits in the high nibble are carried across untouched.  Nothing bounds the
   indices and nothing reads data_fdps_map_unit_count; 4 and 0x22 are literals
   in the instruction stream, correct for map05's own deployment, which ends at
   unit 0x23.

   fdps_deploy_wave, fdps_map_cursor_move_to_unit and fdps_render_view_frame
   leave nothing this body reads, and the cursor fdps_draw_text hands back in
   EAX is discarded -- the next instruction after its stack cleanup is MOV dword
   ptr [EBP-0x20],0x4.  fdps_get_unit_record's result is the only value the body
   keeps off a CALL: stored to [EBP-0x4] at 00037221, then reloaded for the load
   and again for the store, so both halves of the merge address the record that
   iteration fetched.  Nothing sets EAX before the RET and the death-script
   runner ignores what comes back, so the result is void. */
void fdps_chapter_06_event_deploy_wave_2(int unit_index)
{
    /* The record the behaviour merge is standing on, refetched per index. */
    struct fdps_unit_record *unit;
    /* Which unit of the range the merge has reached. */
    int advancing_unit_index;

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH06_ARRIVAL_WAVE,
                     CH06_PLACE_EXACT);
    fdps_map_cursor_move_to_unit(CH06_ARRIVAL_CURSOR_UNIT);

    /* The argument slot is the counter, as the assembly has it. */
    for (unit_index = 0;
         unit_index < CH06_ARRIVAL_HOLD_FRAMES;
         unit_index++) {
        fdps_render_view_frame();
    }

    fdps_draw_text(data_fdps_current_chapter_text_ptr, CH06_ARRIVAL_TEXT_ID,
                   (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                   MESSAGE_FG_COLOR, MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);

    for (advancing_unit_index = CH06_ADVANCE_FIRST_INDEX;
         advancing_unit_index <= CH06_ADVANCE_LAST_INDEX;
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
