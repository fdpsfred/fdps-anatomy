/* chevt2b.c -- the scripted chapter-event handlers of chapters 9 to 14.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt2b.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7, chevt2.c for chapter 8, chevt3.c for 15 to 19, chevt4.c for
 * 20 to 23, chevt5.c for 24 and 25, chevt5b.c for 26 and 27 and chevt6.c for
 * 28 to 30.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "deploy.h"
#include "text.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "chevt2b.h"

/* The slot of data_fdps_map_cell_event_triggered_flags (gamedata.h) the
   one-shot handlers latch: byte ptr [0x000640e8], element 0x10 of the 32-entry
   array based at 0x000640d8.  The array's own indexer is a cell's raw event
   code and the shipped M%02d.DTL event planes only reach codes 0 to 15, so
   element 0x10 is the first slot no map cell can name and the handlers keep
   their latch in it -- one slot shared by all of them, which is safe only
   because one chapter is loaded at a time and fdps_chapter_state_reset memsets
   the whole array when a chapter starts.  Being inside that array is also what
   makes the latch survive a save, because the save and load paths move all
   0x20 bytes to and from the slot image.

   src/chevt1.c and src/chpost3.c spell the same slot out for the same reason;
   it stays file-local at all three ends because no header owns it. */
#define CHAPTER_EVENT_ONE_SHOT_SLOT 0x10

/* The half of the AI byte the one merge in this file keeps: AND DL,0xf0 at
   00037a33 in the chapter 13 handler.  The four bits it preserves are flags
   other code reads on their own -- 0x40 in fdps_map_actor_take_best_action and
   0x80 in fdps_score_targets_for_item -- while the four it drops are the
   behaviour code fdps_map_actor_behavior_step isolates with AND AL,0xf and
   dispatches on.  src/unit.c, src/chevt1.c, src/chevt2.c and src/chevt3.c
   spell the same mask out for the same field; it stays file-local at every end
   because no header owns it. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* The mode 13h aperture and its row stride, PUSH 0xa0000 at 00037764 and
   PUSH 0x140 at 0003775f.  0xa0000 stays a literal because it is where the
   display adapter answers and not the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d at 0003775a,
   00037758 and 00037756: glyph fill, no cell background, and the shadow the
   outline colour becomes while the font's outline flag is clear.  Every
   ordinary line of spoken game text is drawn with these three. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* The wave chapter 9's reinforcements carry: PUSH 0x1 at 00037746, matched
   against byte 0x15 of each 0x1a-byte deployment record of the resident
   MAP%02d.DAT block.  It is a literal with no arithmetic and no read of
   data_fdps_battle_turn_counter anywhere in the body, so this handler always
   asks for wave 1 whenever it is run.  Six of map08.dat's 31 records carry it
   -- character ids 0x4c, 0x56 and 0x5d, two of each at level 13 and all on
   side 0 -- against the 24 wave-0 records the map opens with. */
#define CH09_ARRIVAL_WAVE 1

/* How that wave is placed: XOR EAX,EAX / PUSH EAX at 00037743..00037745, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each arrival goes on
   the nearest free walkable tile to its placement record's coordinates rather
   than on the coordinates themselves. */
#define CH09_PLACE_EXACT 0

/* The line spoken over the arrivals: PUSH 0x17 at 00037769, the last of the 24
   entries of chapter 9's own FDETXT%02d.TXT block -- its 24-word offset table
   ends at 0x742, which is that entry.  The entry opens with the portrait
   control code -0x11 followed by 0x4c, so it is spoken by character 0x4c, one
   of the six units the deployment on the line above has just brought on. */
#define CH09_ARRIVAL_TEXT_ID 0x17

/* 00037730.  Chapter 9's scheduled reinforcement wave: the map's six wave-1
   enemies march on and the line announcing them is painted over them.

   The frame is the family's standard Watcom four-push one with an empty local
   area -- PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB
   ESP,0x0 at 00037730..00037736 -- so there is no local here at all and every
   argument is computed straight into the pushes.  The two caller-cleans, ADD
   ESP,0xc after the deployment and ADD ESP,0x1c after the draw, are this
   function's own, and the RET at 0003777d carries no immediate, so the
   convention is the stack one at both ends of the call.

   THERE IS NO GUARD OF ANY KIND IN THE BODY: no one-shot latch, no test of
   data_fdps_battle_turn_counter and no compare anywhere -- the instruction
   after the argument-slot store at 0003773c is the XOR that builds the
   deployment's third argument.  What makes the event happen once is the data:
   map08.dat's turn-event table, the sixteen 3-byte entries
   fdps_battle_run_turn_events walks from offset 3 of the chapter script, names
   handler 0x0e in one entry only -- turn 0x0f, side 0 -- so a second call
   would append the same six records a second time and nothing here would stop
   it.

   The two calls are unconditional and in this order -- CALL 0x00023830 at
   0003774e, CALL 0x0001ff60 at 00037771 -- so the enemies are on the map
   before the line is spoken, the opposite of chapter 3's ambush handler, which
   speaks first.  It is also what makes the portrait in the message name a unit
   that is already standing on the field.

   THE MAP NUMBER IS THE CHAPTER GLOBAL AND NOT A LITERAL.  PUSH dword ptr
   [0x00069cf4] at 00037748 is data_fdps_chapter_current_chapter_id, the same
   argument the chapter 4, 6 and 10 handlers read and the opposite of the four
   chapter 3 handlers, which push the literal 2.  The deployment records still
   come from whichever MAP%02d.DAT is resident; what the number chooses is the
   MAP%02d.COD coordinates the arrivals are put down at.

   fdps_deploy_wave leaves nothing this body reads, and the cursor
   fdps_draw_text hands back in EAX is discarded: the ADD ESP,0x1c at 00037776
   is followed straight by the four POPs and the RET, with nothing in between
   that touches EAX.  Nothing sets EAX before that RET and no dispatcher reads
   what comes back, so the result is void.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003773c writes zero over the incoming slot before either
   call and nothing ever reads it back, so which unit the event fired for
   cannot reach anything this handler does; the store has no observable effect,
   because the slot belongs to the caller's outgoing argument area and the
   turn-event dispatcher drops it with its own stack cleanup. */
void fdps_chapter_09_event_deploy_wave_1(int unit_index)
{
    unit_index = 0;

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH09_ARRIVAL_WAVE,
                     CH09_PLACE_EXACT);
    fdps_draw_text(data_fdps_current_chapter_text_ptr, CH09_ARRIVAL_TEXT_ID,
                   (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                   MESSAGE_FG_COLOR, MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
}

/* The one turn the body compares for equality, CMP dword ptr
   [0x00069ce8],0x3 / JNZ at 0003778c, and the wave that turn brings on, PUSH
   0x1 at 00037798.  map09.dat carries eight records tagged wave 1, all
   spawning at tile (13, 2) -- the door the chapter opens behind. */
#define CH10_ANNOUNCED_WAVE_TURN 3
#define CH10_ANNOUNCED_WAVE 1

/* The upper bound of the middle arm, CMP dword ptr [0x00069ce8],0xd / JG at
   000377d0.  It is a SIGNED compare and it is the only test the arm has: there
   is no lower bound, so every turn from 13 downwards that is not 3 runs this
   path and asks for whatever wave the subtraction below produces. */
#define CH10_LAST_DOOR_TURN 0xd

/* What the turn counter is reduced by to name the wave, SUB EAX,0x4 at
   000377e5.  The scheduled turns 6 through 13 map onto waves 2 through 9, the
   eight pairs of records map09.dat puts at the two mid-map doors. */
#define CH10_DOOR_WAVE_TURN_BIAS 4

/* The wave every turn past 13 brings on, PUSH 0xb at 00037859: map09.dat's six
   wave-11 records, which spawn at tiles (3..5, 1..2).  Wave 10 is not this
   handler's -- it belongs to the same map's tile trigger above. */
#define CH10_LAST_WAVE 0xb

/* How all three deployments are placed: XOR EAX,EAX / PUSH EAX at 00037795,
   000377dd and 00037856, so fdps_deploy_wave passes 0 on to fdps_deploy_unit
   and each arrival goes on the nearest free walkable tile to its placement
   record's coordinates rather than on the coordinates themselves. */
#define CH10_TURN_PLACE_EXACT 0

/* The two lines this handler speaks: PUSH 0x12 at 000377bb for the wave-1
   arrival and PUSH 0x13 at 0003787c for the wave-11 one, both entries of the
   chapter's own FDETXT%02d.TXT block.  The middle arm speaks neither. */
#define CH10_ANNOUNCED_WAVE_TEXT_ID 0x12
#define CH10_LAST_WAVE_TEXT_ID 0x13

/* The two world pixels the cursor is walked to, PUSH 0x48 / PUSH 0xd8 at
   000377f7 and PUSH 0x168 / PUSH 0xd8 at 00037824.  At the 24-pixel tile step
   they are tiles (3, 9) and (15, 9), the two doors every record of waves 2
   through 9 spawns at.  fdps_map_cursor_move_to takes world pixels, not
   tiles. */
#define CH10_LEFT_DOOR_WORLD_X 0x48
#define CH10_RIGHT_DOOR_WORLD_X 0x168
#define CH10_DOOR_WORLD_Y 0xd8

/* How long the view rests on each door: the CMP against 0xc at 0003780d and
   0003783d.  Each frame costs one timer tick inside fdps_render_view_frame, so
   twelve is the length of the pause and not a repaint count. */
#define CH10_DOOR_HOLD_FRAMES 0xc

/* 00037780.  Chapter 10's turn-scheduled reinforcements: the wave-1 group with
   its announcement on turn 3, one enemy at each of the two mid-map doors with
   the view panning onto both of them on every turn from 6 to 13, and the
   wave-11 group with its own line on turn 19.

   The frame is the family's four-push one with an empty local area -- PUSH EBX
   / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 at
   00037780..00037786 -- so there is no local here at all and the frame counter
   has nowhere to live but the incoming argument slot.  Every caller-clean in
   the body is this function's own (ADD ESP,0xc after each deployment, ADD
   ESP,0x1c after each draw, ADD ESP,0x8 after each cursor move), the RET at
   00037890 carries no immediate, and the turn-event runner pushes one dword
   and drops it with ADD ESP,0x4 at 0002e146, so the convention is the stack
   one at both ends of the call.

   THE THREE ARMS ARE TWO TESTS, NOT A RANGE.  CMP dword ptr [0x00069ce8],0x3 /
   JNZ at 0003778c picks the announced arm, and CMP dword ptr [0x00069ce8],0xd
   / JG at 000377d0 picks between the door arm and the last-wave arm.  The
   second compare is signed and has no partner below it, so the door arm is
   every turn of 13 or less except 3 -- turn 0 asks for wave -4 and turn 2 for
   wave -2.  Neither matches a deployment record, so nothing arrives, but the
   cursor still crosses both doors and twenty-four frames are still composed.
   What confines the arm to waves 2 through 9 is map09.dat's turn table, which
   names this slot on turns 3, 6..13 and 19 only.

   THE WAVE THE MIDDLE ARM ASKS FOR IS SIGNED ARITHMETIC ON A SIGNED COUNTER.
   MOV EAX,[0x00069ce8] / SUB EAX,0x4 at 000377e0 pushes the difference as a
   full dword, so a counter below 4 sends a negative wave into
   fdps_deploy_wave, which compares it against each record's unsigned wave byte
   and matches nothing.  Widening either side to unsigned would turn that into
   a very large wave number instead, which still matches nothing -- but it also
   turns the JG into an unsigned test, and then a negative counter would take
   the last-wave arm and bring wave 11 on early.

   THE ORDER WITHIN EACH ARM IS WHAT THE PLAYER SEES.  The announced arms
   deploy first and draw second (CALL 0x00023830 then CALL 0x0001ff60), so the
   line is spoken over enemies already standing on the field; the door arm
   deploys first as well, then walks the cursor onto each door in turn and
   holds the view there, so the arrivals are on the map before the pan starts.

   THE MAP NUMBER IS THE CHAPTER GLOBAL AND NOT A LITERAL.  PUSH dword ptr
   [0x00069cf4] at 0003779a, 000377e9 and 0003785b is
   data_fdps_chapter_current_chapter_id, the same argument the chapter 4, 6 and
   9 handlers read.  The deployment records still come from whichever
   MAP%02d.DAT is resident; what the number chooses is the MAP%02d.COD
   coordinates the arrivals are put down at.

   THE FRAME COUNTER IS THE ARGUMENT SLOT.  MOV dword ptr [EBP+0x14],0x0 at
   00037806 and again at 00037836 writes zero over the incoming argument, and
   each loop compares and INCs that same slot, so the counter and the parameter
   are one storage location and there is no local frame to hold anything else.
   The other two arms never touch the slot.  The store has no observable effect
   on the caller either, because the slot belongs to its outgoing argument area
   and the turn-event runner drops it with its own ADD ESP,0x4.

   Both loops are the -od shape of a for statement: the compare at the top, a
   dead MOV EAX,[EBP+0x14] ahead of the INC, and the body reached by a JL past
   the exit jump.  Both are signed (JL) and both stop at 12.

   THERE IS NO LATCH AND NO OTHER GUARD.  Nothing in the body tests or writes
   the family's one-shot slot and nothing records that the handler ran, so
   every call the turn table makes fires in full.

   fdps_deploy_wave, fdps_map_cursor_move_to and fdps_render_view_frame all
   leave nothing this body reads.  fdps_draw_text hands back a cursor in EAX
   and both call sites discard it: after the first the next instruction is the
   JMP to the epilogue at 000377cb and after the second it is the epilogue
   itself.  Nothing sets EAX before the RET and no dispatcher reads what comes
   back, so the result is void.

   event_arg is the handler table's shared parameter.  The turn-event runner is
   the only dispatcher that reaches this slot and it pushes a literal 0 at
   0002e13e, and no arm reads the incoming value before overwriting it, so
   nothing a caller passes can change what the handler does. */
void fdps_chapter_10_event_deploy_wave_for_turn(int event_arg)
{
    if (data_fdps_battle_turn_counter == CH10_ANNOUNCED_WAVE_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH10_ANNOUNCED_WAVE, CH10_TURN_PLACE_EXACT);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH10_ANNOUNCED_WAVE_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter <= CH10_LAST_DOOR_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         data_fdps_battle_turn_counter -
                             CH10_DOOR_WAVE_TURN_BIAS,
                         CH10_TURN_PLACE_EXACT);

        fdps_map_cursor_move_to(CH10_LEFT_DOOR_WORLD_X, CH10_DOOR_WORLD_Y);
        /* The argument slot is the counter, as the assembly has it. */
        for (event_arg = 0;
             event_arg < CH10_DOOR_HOLD_FRAMES;
             event_arg++) {
            fdps_render_view_frame();
        }

        fdps_map_cursor_move_to(CH10_RIGHT_DOOR_WORLD_X, CH10_DOOR_WORLD_Y);
        for (event_arg = 0;
             event_arg < CH10_DOOR_HOLD_FRAMES;
             event_arg++) {
            fdps_render_view_frame();
        }
    } else {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH10_LAST_WAVE,
                         CH10_TURN_PLACE_EXACT);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH10_LAST_WAVE_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    }
}

/* The wave the ambush brings on: PUSH 0xa at 000378d9, matched against byte
   0x15 of each 0x1a-byte deployment record of the resident MAP%02d.DAT block.
   Ten of MAP09.DAT's records carry it. */
#define AMBUSH_WAVE 10

/* How the ten are placed: XOR EAX,EAX / PUSH EAX at 000378d6, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each unit goes on the
   nearest free walkable tile to its placement record's coordinates rather than
   on the coordinates themselves.  Wave 0, the group a map opens with, is the
   one deployed with this flag set. */
#define AMBUSH_PLACE_EXACT 0

/* 000378a0.  Chapter 10's stairway ambush: the first unit that is not on side
   0 to trigger the map's event tile brings on the ten reinforcements of wave
   10.

   fdps_get_unit_record is called before either test and unconditionally (CALL
   at 000378b0, with the result stored to [EBP-0x4] at 000378b8), and the
   record it hands back is read for one byte only, the side at record+6.  The
   two tests are the JNZ at 000378c2 over the latch and the JA at 000378cb over
   that byte, both jumping to the same epilogue, which is the short circuit
   written here: a non-zero latch means the side byte is never loaded.

   The side test is CMP byte ptr [EAX+0x6],0x0 / JA -- unsigned, so it is a
   plain "not 0" over the whole byte range and not a sign test.  Side 0 is the
   enemy, 1 the guest/neutral one and 2 the player's own roster, so what the
   test really keeps out is an enemy unit walking over the tile: the player's
   units and the guests spring the chapter's own ambush alike.

   The latch is the shared one-shot slot and it is written before the deploy
   call (MOV byte ptr [0x000640e8],0x1 at 000378cf), not after it, so a handler
   re-entered from inside fdps_deploy_wave could not fire twice either.  It is
   tested against 0 rather than against 1: any non-zero value in the slot
   blocks the body.

   The map number handed to fdps_deploy_wave is read out of
   data_fdps_chapter_current_chapter_id at the call site (PUSH dword ptr
   [0x00069cf4] at 000378db) and not out of anything this handler holds, so it
   is whatever chapter is loaded -- 9 for this one, which is the map09 the
   handler's table slot is only ever named from.

   Nothing sets EAX before the RET at 000378ef and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_10_event_deploy_wave_10(int unit_index)
{
    struct fdps_unit_record *triggering_unit;

    triggering_unit = fdps_get_unit_record(unit_index);

    if ((data_fdps_map_cell_event_triggered_flags[
             CHAPTER_EVENT_ONE_SHOT_SLOT] == 0) &&
        (triggering_unit->side != 0)) {
        data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_ONE_SHOT_SLOT] = 1;
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, AMBUSH_WAVE,
                         AMBUSH_PLACE_EXACT);
    }
}

/* The turn the four-corner wave is scheduled for, CMP dword ptr [0x00069ce8],
   0x4 / JNZ at 000378fc.  It is the only turn the equality test names; every
   other turn the map's table sends here falls through to the other arm. */
#define CH11_FOUR_CORNER_TURN 4

/* The two waves, and they are crossed over: PUSH 0x5 at 00037908 is what the
   turn-4 arm deploys and PUSH 0x4 at 00037943 is what the fall-through arm
   deploys.  Wave 5 is the sixteen records spread around the four corners of
   the map, wave 4 the sixteen that arrive as two tight groups along the top
   edge.  Writing the pair the way round the turn numbers suggest swaps what
   arrives on each turn AND sends both pans to the wrong units, because the two
   pan indices below only land on wave-4 units once wave 5 has already been
   appended to the array (rebuild_info/pitfalls.md). */
#define CH11_FOUR_CORNER_WAVE 5
#define CH11_TOP_GROUPS_WAVE 4

/* How both arms place what they bring on: XOR EAX,EAX / PUSH EAX at 00037905
   and at 00037940, so fdps_deploy_wave settles each unit on the nearest free
   walkable tile to its placement record's coordinates rather than on the
   coordinates themselves. */
#define CH11_TURN_PLACE_EXACT 0

/* The text entry both arms speak, PUSH 0xf at 0003792b and at 000379b6. */
#define CH11_ARRIVAL_TEXT_ID 0x0f

/* The two units the fall-through arm walks the cursor onto, PUSH 0x34 at
   00037953 and PUSH 0x2c at 0003797b, in that order.  They are the eleventh
   and the third record of wave 4, which lands on unit indices 0x2a..0x39 once
   the chapter's own force and the turn-4 wave are in the array ahead of it, so
   the first pan shows the top-left group and the second the top-right one.
   Both are literals in the instruction stream and neither is range checked. */
#define CH11_TOP_LEFT_GROUP_UNIT_INDEX 0x34
#define CH11_TOP_RIGHT_GROUP_UNIT_INDEX 0x2c

/* How long each pan holds, CMP dword ptr [EBP+0x14],0xc / JL at 00037964 and
   at 0003798c.  A composed frame costs one timer tick, so the count is how
   long the view stays on each group and not a number of anything drawn. */
#define CH11_PAN_HOLD_FRAMES 0xc

/* 000378f0.  Chapter 11's turn-scheduled reinforcements: on turn 4 the enemy
   wave that comes in around the four corners of the map, on any other turn --
   turn 7 is the only other one map10.dat schedules -- the two groups along the
   top edge, with the view panned onto each of them in turn.  Both arms end on
   the same spoken line.

   THE TURN TEST IS ONE EQUALITY AND THE FALL-THROUGH IS UNGUARDED.  CMP dword
   ptr [0x00069ce8],0x4 / JNZ at 000378fc..00037903 is the whole of the branch:
   every turn that is not 4 runs the second arm in full, and nothing in the
   body tests for turn 7.  What keeps the second arm off the other turns is the
   turn table in map10.dat, which names this handler slot twice and no more.

   THE WAVE NUMBERS ARE CROSSED RELATIVE TO THE TURNS.  Turn 4 asks for wave 5
   and the fall-through asks for wave 4; see the two defines above for why
   putting them the intuitive way round breaks the pans as well as the
   arrivals.

   THE MAP NUMBER IS THE CHAPTER GLOBAL AND NOT A LITERAL.  PUSH dword ptr
   [0x00069cf4] at 0003790a and 00037945 is
   data_fdps_chapter_current_chapter_id, so what the arrivals are placed by is
   whichever MAP%02d.COD the loaded chapter names.

   THE FRAME COUNTER IS THE ARGUMENT SLOT.  The frame is built with SUB ESP,0x0
   at 000378f6, so there is no local area at all: MOV dword ptr [EBP+0x14],0x0
   at 0003795d and again at 00037985 writes zero over the incoming argument and
   each loop compares and INCs that same slot.  Both loops are the -od shape of
   a for statement -- the compare at the top, a dead MOV EAX,[EBP+0x14] ahead
   of the INC, and the body reached by a JL past the exit jump -- and both are
   signed and stop at 12.  The store cannot be seen by the caller, because the
   slot belongs to its outgoing argument area and the turn-event runner drops
   it with its own ADD ESP,0x4.

   THERE IS NO LATCH.  Nothing in the body tests or writes the family's
   one-shot slot, so every call the turn table makes fires in full.

   fdps_deploy_wave, fdps_map_cursor_move_to_unit and fdps_render_view_frame
   all leave nothing this body reads.  fdps_draw_text hands back a cursor in
   EAX and both call sites discard it: after the first the next instruction is
   the JMP to the epilogue at 0003793b and after the second it is the epilogue
   itself.  Nothing sets EAX before the RET at 000379ca and no dispatcher reads
   what comes back, so the result is void.

   event_arg is the handler table's shared parameter.  The turn-event runner is
   the only dispatcher that reaches slot 17 and it pushes a literal 0, and
   neither arm reads the incoming value before overwriting it, so nothing a
   caller passes can change what the handler does. */
void fdps_chapter_11_event_deploy_wave_for_turn(int event_arg)
{
    if (data_fdps_battle_turn_counter == CH11_FOUR_CORNER_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH11_FOUR_CORNER_WAVE, CH11_TURN_PLACE_EXACT);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH11_ARRIVAL_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH11_TOP_GROUPS_WAVE, CH11_TURN_PLACE_EXACT);

        fdps_map_cursor_move_to_unit(CH11_TOP_LEFT_GROUP_UNIT_INDEX);
        /* The argument slot is the counter, as the assembly has it. */
        for (event_arg = 0;
             event_arg < CH11_PAN_HOLD_FRAMES;
             event_arg++) {
            fdps_render_view_frame();
        }

        fdps_map_cursor_move_to_unit(CH11_TOP_RIGHT_GROUP_UNIT_INDEX);
        for (event_arg = 0;
             event_arg < CH11_PAN_HOLD_FRAMES;
             event_arg++) {
            fdps_render_view_frame();
        }

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH11_ARRIVAL_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    }
}

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

/* The wave the handler brings on, PUSH 0x4 at 00037a66, and how it places it,
   XOR EAX,EAX / PUSH EAX at 00037a63: fdps_deploy_wave settles each arrival on
   the nearest free walkable tile to its placement record rather than on the
   record's own coordinates. */
#define CH14_ARRIVAL_WAVE 4
#define CH14_PLACE_EXACT 0

/* The two units the pans walk the cursor onto, PUSH 0x24 at 00037a76 and PUSH
   0x2a at 00037a9e, in that order.  Both are literals and neither is range
   checked; they land on one warrior of each of the wave's two bottom-edge
   blocks only because the chapter's own force and the map's earlier waves are
   already in the unit array ahead of wave 4. */
#define CH14_LEFT_GROUP_UNIT_INDEX 0x24
#define CH14_RIGHT_GROUP_UNIT_INDEX 0x2a

/* How long each pan holds, CMP dword ptr [EBP+0x14],0xc / JL at 00037a87 and
   at 00037aaf.  A composed frame costs one timer tick, so the count is how
   long the view stays on each block and not a number of anything drawn. */
#define CH14_PAN_HOLD_FRAMES 0xc

/* The line the handler speaks, PUSH 0x11 at 00037ad9. */
#define CH14_ARRIVAL_TEXT_ID 0x11

/* 00037a50.  Chapter 14's turn-6 event: brings on the map's wave 4 -- the
   sixteen level-13 warriors that arrive in two blocks along the bottom edge
   and the level-17 werewolf that comes in at the top left after the treasure
   -- pans the view onto one warrior of each block in turn, and speaks the line
   that announces them.

   THE BODY IS UNCONDITIONAL.  Between the argument-slot store at 00037a5c and
   the RET at 00037aed there is no compare against anything: no turn test, no
   side test and no read or write of the family's one-shot latch slot, so every
   call runs the whole of it and a second call appends wave 4 a second time.
   What keeps it to one firing is MAP13.DAT's turn table, which names this slot
   once.

   THE MAP NUMBER IS THE CHAPTER GLOBAL AND NOT A LITERAL.  PUSH dword ptr
   [0x00069cf4] at 00037a68 is data_fdps_chapter_current_chapter_id, so which
   MAP%02d.COD supplies the placement coordinates follows the loaded chapter.
   The deployment records themselves still come from whichever MAP%02d.DAT is
   resident.

   THE FRAME COUNTER IS THE ARGUMENT SLOT.  The frame is built with SUB ESP,0x0
   at 00037a56, so there is no local area at all: MOV dword ptr [EBP+0x14],0x0
   at 00037a5c writes zero over the incoming argument before the deployment,
   and 00037a80 and 00037aa8 write it again as each loop starts.  Both loops
   are the -od shape of a for statement -- the compare at the top, a dead MOV
   EAX,[EBP+0x14] ahead of the INC, and the body reached by a JL past the exit
   jump -- and both are signed and stop at 12.  None of the three stores can be
   seen by the caller, because the slot belongs to its outgoing argument area
   and the turn-event runner drops it with its own ADD ESP,0x4.

   THE PANS RUN AFTER THE DEPLOYMENT AND READ THE ARRAY AS IT THEN STANDS.  The
   deployment call at 00037a6e comes first, so the two indices below are read
   out of an array the wave has already been appended to.

   fdps_deploy_wave, fdps_map_cursor_move_to_unit and fdps_render_view_frame
   all leave nothing this body reads: the instruction after each of their stack
   cleanups is a PUSH of the next call's argument or the store that reseeds the
   counter, and the frame call's successor at 00037a8f overwrites EAX with the
   counter.  fdps_draw_text hands back a cursor in EAX and it is discarded --
   the ADD ESP,0x1c at 00037ae6 is followed straight by the four POPs and the
   RET.  Nothing sets EAX before that RET and no dispatcher reads what comes
   back, so the result is void.

   unit_index is the handler table's shared parameter.  The turn-event runner
   is the only dispatcher that reaches slot 19 and it pushes a literal 0, and
   nothing reads the incoming value before the store at 00037a5c overwrites it,
   so nothing a caller passes can change what the handler does. */
void fdps_chapter_14_event_deploy_wave_4(int unit_index)
{
    unit_index = 0;

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH14_ARRIVAL_WAVE,
                     CH14_PLACE_EXACT);

    fdps_map_cursor_move_to_unit(CH14_LEFT_GROUP_UNIT_INDEX);
    /* The argument slot is the counter, as the assembly has it. */
    for (unit_index = 0;
         unit_index < CH14_PAN_HOLD_FRAMES;
         unit_index++) {
        fdps_render_view_frame();
    }

    fdps_map_cursor_move_to_unit(CH14_RIGHT_GROUP_UNIT_INDEX);
    for (unit_index = 0;
         unit_index < CH14_PAN_HOLD_FRAMES;
         unit_index++) {
        fdps_render_view_frame();
    }

    fdps_draw_text(data_fdps_current_chapter_text_ptr, CH14_ARRIVAL_TEXT_ID,
                   (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                   MESSAGE_FG_COLOR, MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
}
