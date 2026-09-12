/* chevt5b.c -- the scripted chapter-event handlers of chapters 26 and 27.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt5b.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7, chevt2.c for chapter 8, chevt2b.c for 9 to 14, chevt3.c
 * for 15 to 19, chevt4.c for 20 to 23, chevt5.c for 24 and 25 and chevt6.c
 * for 28 to 30.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "text.h"
#include "deploy.h"
#include "unit.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "chevt5b.h"

/* The screen's row stride every handler here hands fdps_draw_text, PUSH 0x140
   before each of the file's six draws: 000391ac, 00039285, 000392ec, 00039322,
   0003940e and 000394a6.  Where each draw sends its glyphs is a define of its
   own next to the handler that makes it. */
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d before each
   of those six draws: glyph fill, no cell background, and the shadow the
   outline colour becomes while the font's outline flag is clear. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* The element of data_fdps_map_cell_event_triggered_flags (gamedata.h) the
   chapter 26 ambush latches: byte ptr [0x000640e8] at 0003924b and 0003939c,
   element 0x10 of the 32-byte array based at 0x000640d8.  The cell event codes
   a map file can carry only reach 0 to 15, so element 0x10 is the first slot no
   map cell can name and the chapter handlers keep their latch in it -- one slot
   shared by all of them, which is safe only because one chapter is loaded at a
   time and fdps_chapter_state_reset memsets the whole array when a chapter
   starts.

   Being inside that array is what keeps the latch honest.  The chapter reset
   clears all 0x20 bytes and the save and load paths move the whole array to and
   from the slot image, so a function-local static in its place would leave the
   ambush spent across a chapter restart and across a reload
   (rebuild_info/pitfalls.md).

   src/chevt1.c, src/chevt2b.c, src/chevt4.c, src/chevt5.c and src/chpost3.c
   spell the same slot out for the same reason; it stays file-local at every end
   because no header owns it. */
#define CHAPTER_EVENT_ONE_SHOT_SLOT 0x10

/* The side byte value the ambush's second gate tests for, MOV AL,byte ptr
   [EAX+0x6] / AND EAX,0xff / CMP EAX,0x2 at 00039257..0003925f.  2 is the
   player's own roster, 0 the enemy and 1 the guest, so an enemy walking over
   the trigger tile cannot spring the ambush.  The load widens the byte with AND
   EAX,0xff and not with a sign extension, so this is an unsigned equality on
   the whole byte.  src/chevt4.c, src/chevt5.c, src/chevt6.c, src/combat.c,
   src/deploy.c and src/unitatk.c spell the same constant out; it stays
   file-local at every end because no header owns it. */
#define PLAYER_SIDE 2

/* The half of the AI byte the two behaviour sweeps here keep: AND DL,0xf0 at
   00039216 and 0003938c.  The four bits it preserves are flags other code reads
   on their own -- 0x40 in fdps_map_actor_take_best_action and 0x80 in
   fdps_score_targets_for_item -- while the four it drops are the behaviour code
   fdps_map_actor_behavior_step isolates with AND AL,0xf and dispatches on.
   src/unit.c, src/chevt1.c, src/chevt2.c, src/chevt3.c, src/chevt4.c,
   src/chevt5.c and src/chevt6.c spell the same mask out for the same field; it
   stays file-local at every end because no header owns it. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* Where chapter 26's advance order draws its line, PUSH 0xa0000 at 000391b1:
   the top-left corner of the visible mode-13h page, kept a literal because it
   is an address inside the display adapter's aperture rather than the address
   of anything the linker places (rebuild_info/pitfalls.md, contract E). */
#define CH26_TEXT_DEST 0x000a0000

/* The entry of the chapter's own FDETXT26.TXT block the order speaks, PUSH
   0x16 at 000391b6.  The message's token stream opens with speaker code -0x11
   and operand 0x43, so the line is spoken by character id 0x43, one of the
   chapter's four commanders. */
#define CH26_ADVANCE_TEXT_ID 0x16

/* The range the behaviour sweep covers and the mode it writes: 0x1a parked at
   [EBP-0x20] at 000391c6, 0x2d at [EBP-0x1c] at 000391cd and 0 at [EBP-0x18]
   at 000391d4.  0x1a is the first of the twenty records MAP25.DAT deploys in
   the holding mode after the twelve party slots and the fourteen records below
   them, 0x2d is the last of them, and mode 0 is the default movement chain
   that paths a unit toward the nearest opposing unit. */
#define CH26_ADVANCE_FIRST_UNIT_INDEX 0x1a
#define CH26_ADVANCE_LAST_UNIT_INDEX 0x2d
#define CH26_ADVANCE_BEHAVIOR_MODE 0

/* 00039190.  Chapter 26's turn-4 advance order: a line of the chapter's text
   is painted straight onto the screen and then the tower garrison's two
   holding groups are switched to the advancing behaviour mode.

   The frame is the family's four-push one with 0x20 bytes of locals -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x20 at
   00039190..00039196 -- so the one incoming dword sits at [EBP+0x14].  Both
   caller-cleans in the body are this function's own (ADD ESP,0x1c after the
   draw at 000391c3 and ADD ESP,0x4 after each record fetch at 0003920a), the
   RET at 0003922c carries no immediate, and fdps_battle_run_turn_events -- the
   dispatcher that reaches this table slot -- pushes one dword and drops it with
   ADD ESP,0x4, so the convention is the stack one at both ends of the call.

   THE INCOMING ARGUMENT IS NEVER READ.  MOV dword ptr [EBP+0x14],0x0 at
   0003919c is the only access to the slot in the whole body and nothing loads
   it afterwards.  The dispatcher pushes a literal 0 anyway, so no value is
   lost by the store.

   THE SWEEP IS INCLUSIVE AND SIGNED.  MOV EAX,[EBP-0x8] / CMP EAX,[EBP-0x10] /
   JLE at 000391f3..000391f9, so unit 0x2d is written and a rewrite with a
   strict less-than would leave it holding position.

   THE MERGE KEEPS THE HIGH NIBBLE.  MOV DL,byte ptr [EAX+0x34] / AND DL,0xf0 /
   MOV DH,byte ptr [EBP-0x14] / OR DH,DL / MOV byte ptr [EAX+0x34],DH at
   00039213..00039221 reads the byte back, drops only its low four bits and
   writes the merged value.  Writing the mode as a plain store also wipes bits
   0x40 and 0x80, which fdps_map_actor_take_best_action (00012c6f) and
   fdps_score_targets_for_item (0001337d) test as independent per-unit flags
   (rebuild_info/pitfalls.md).

   THE SWEEP IS AN INLINE EXPANSION AND NOT A CALL.  The three constants are
   parked at [EBP-0x20], [EBP-0x1c] and [EBP-0x18] and copied into a second set
   of slots at [EBP-0xc], [EBP-0x10] and [EBP-0x14] before the counter is
   seeded -- the fingerprint of fdps_object_set_field34_low_nibble_range
   expanded in place -- and the only CALL inside the loop is
   fdps_get_unit_record, once per iteration.  Writing the range as a call to
   that helper would put a CALL in the rebuild that the original does not make.

   ONE VALUE IS USED AFTER A CALL.  The fdps_draw_text cursor is discarded --
   the next instruction after the CALL at 000391be is the ADD ESP that cleans
   its arguments -- while fdps_get_unit_record's record pointer comes back in
   EAX and is stored to [EBP-0x4], which is re-read once for the load of the AI
   byte and again for the store, so both halves of the merge address the record
   that iteration fetched.  Nothing sets EAX before the RET and no dispatcher
   reads what comes back, so the result is void. */
void fdps_chapter_26_event_enemies_advance(int unit_index)
{
    /* The record the sweep is writing the behaviour mode into, re-resolved on
       every pass. */
    struct fdps_unit_record *advancing_unit;
    int advancing_unit_index;

    /* The store the original makes over its own argument slot and never reads
       back; nothing here counts in it. */
    unit_index = 0;

    fdps_draw_text(data_fdps_current_chapter_text_ptr, CH26_ADVANCE_TEXT_ID,
                   (unsigned char *) CH26_TEXT_DEST, VGA_SCREEN_PITCH,
                   MESSAGE_FG_COLOR, MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);

    for (advancing_unit_index = CH26_ADVANCE_FIRST_UNIT_INDEX;
         advancing_unit_index <= CH26_ADVANCE_LAST_UNIT_INDEX;
         advancing_unit_index++) {
        advancing_unit = fdps_get_unit_record(advancing_unit_index);
        advancing_unit->ai_behavior = (unsigned char)
            ((advancing_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             CH26_ADVANCE_BEHAVIOR_MODE);
    }
}

/* Where chapter 26's ambush draws its three lines, PUSH 0xa0000 at 0003928a,
   000392f1 and 00039327: the top-left corner of the visible mode-13h page,
   kept a literal because it is an address inside the display adapter's
   aperture rather than the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define CH26_WAVES_TEXT_DEST 0x000a0000

/* The three entries of the chapter's own FDETXT26.TXT block the ambush speaks,
   named for where they fall in the sequence rather than for what they say:
   PUSH 0xa at 0003928f is drawn once the enemy wave is on the board, PUSH 0xb
   at 000392f6 once the pan has come back, and PUSH 0xc at 0003932c once the
   allied wave has landed.  The shipped FDETXT26.TXT carries 25 entries, so all
   three are live; entry 10's token stream opens with speaker code -0x11 and
   operand 0x43 -- the same commander the turn-4 order above speaks through --
   entry 11's with operand 2 and entry 12's with operand 0x0c. */
#define CH26_WAVES_AFTER_ENEMY_WAVE_TEXT_ID 0x0a
#define CH26_WAVES_BEFORE_ALLY_WAVE_TEXT_ID 0x0b
#define CH26_WAVES_AFTER_ALLY_WAVE_TEXT_ID 0x0c

/* The two waves the ambush brings on, PUSH 0x2 at 0003926c and PUSH 0x3 at
   00039309, each matched against byte 0x15 of every 0x1a-byte deployment
   record of the resident MAP%02d.DAT block.  On MAP25.DAT wave 2 is a block of
   side-0 enemies and wave 3 a block of side-1 allies, which is why the two
   deployments are not interchangeable: the enemy wave lands before the pan and
   the allied one after it. */
#define CH26_WAVES_ENEMY_WAVE 2
#define CH26_WAVES_ALLY_WAVE 3

/* How every arrival is placed, XOR EAX,EAX / PUSH EAX at 00039269 and
   00039306: each settles on the nearest free walkable tile to its record's
   coordinates rather than on those coordinates themselves. */
#define CH26_WAVES_PLACE_ON_NEAREST_FREE_TILE 0

/* The two values written to data_fdps_map_cursor_draw_mode around the pan: 0
   at 0003929f, which is outside the 1..6 range fdps_draw_map_cursor dispatches
   on so no cursor is painted over the sweep, and 1 at 000392d9, the plain
   cursor tile.  The value found in the global is NOT saved and restored -- 1
   is a literal store -- so a mode other than 1 does not survive the ambush. */
#define CH26_WAVES_MAP_CURSOR_BLANK 0
#define CH26_WAVES_MAP_CURSOR_NORMAL 1

/* The world pixel the pan walks to, PUSH 0x378 at 000392a9 and PUSH 0xf0 at
   000392ae: tile (10, 37) at the 24 pixels a tile measures, the bottom of the
   map both waves arrive at.  Both coordinates are whole multiples of the tile,
   which is what keeps fdps_map_cursor_move_to's step count off zero. */
#define CH26_WAVES_PAN_X 0xf0
#define CH26_WAVES_PAN_Y 0x378

/* How long the hold at the far end of the pan lasts, CMP dword ptr
   [EBP+0x14],0xc / JL at 000392c2.  The test is a strict less-than against 12
   on a counter seeded with 0, so twelve frames are composed at the stop. */
#define CH26_WAVES_PAN_HOLD_FRAMES 0xc

/* The range the behaviour sweep covers and the mode it writes: 0xc parked at
   [EBP-0x24] at 0003933c, 0x4f at [EBP-0x20] at 00039343 and 0 at [EBP-0x1c]
   at 0003934a.  Twelve is the number of party slots a deployment lays down
   first, so the sweep starts at the first record that is not a party member.
   0x4f is a literal and is NOT the unit count -- which chapter 25's ambush in
   src/chevt5.c does read -- so every unit from index 0x50 upward is left on
   whatever behaviour mode it already carries, the two waves this handler has
   just appended included.  Mode 0 is the default movement chain that paths a
   unit toward the nearest opposing unit.

   On MAP25.DAT, the only map that reaches this slot, the two literals cover
   the wave-0 block exactly: the file's record count byte is 0x50 and the 80
   records split 68 tagged wave 0, seven tagged wave 2 and five tagged wave 3,
   so the twelve party slots plus the 68 wave-0 arrivals fill indices 0x00
   through 0x4f and the twelve units this handler appends fill 0x50 through
   0x5b.  The sweep therefore releases the garrison and nothing else -- neither
   the enemy wave nor the allied wave it has just brought on. */
#define CH26_WAVES_FIRST_RELEASED_INDEX 0xc
#define CH26_WAVES_LAST_RELEASED_INDEX 0x4f
#define CH26_WAVES_BEHAVIOR_MODE 0

/* 00039230.  Chapter 26's mid-map ambush: the first time a unit on the
   player's side walks onto the map's trigger tile, the enemy wave comes on,
   the view is panned down to the bottom of the map and held there, the allied
   relief wave comes on behind it, and the whole lower half of the deployment
   block is switched out of holding position into the advancing behaviour mode.

   The frame is the family's four-push one with 0x24 bytes of locals -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x24 at
   00039230..00039236 -- so the one incoming dword sits at [EBP+0x14].  Every
   caller-clean in the body is this function's own (ADD ESP,0xc after each
   deployment, ADD ESP,0x1c after each draw, ADD ESP,0x8 after the pan and ADD
   ESP,0x4 after each record fetch), the RET at 000393a9 carries no immediate,
   and the dispatcher that reaches this table slot pushes one dword and drops
   it with ADD ESP,0x4 -- MOV EAX,[EBP+0x14] / PUSH EAX / CALL dword ptr
   [EDX + 0x601c4] / ADD ESP,0x4 at 00015835..0001583f -- so the convention is
   the stack one at both ends of the call.

   THE RECORD IS FETCHED BEFORE EITHER GATE.  The CALL at 00039240 runs first
   and its result is parked at [EBP-0x4]; only then does CMP byte ptr
   [0x000640e8],0x0 / JNZ at 0003924b test the latch.  fdps_get_unit_record is
   a base-plus-index computation with no side effect, so the fetch is
   invisible, but moving it inside the gate would be a different function.

   BOTH GATES LEAVE BY THE SAME EXIT.  The JNZ at 00039252 and the JZ at
   00039262 both reach the JMP at 00039264 to the epilogue, so a latch that is
   already up and a unit on the wrong side are refused identically: nothing is
   deployed, nothing is drawn, the cursor mode is untouched and the latch is
   not spent.  An enemy or a guest that walks over the tile leaves the ambush
   armed for the unit that comes next.

   THE LATCH IS THE ONE CHAPTER 25's AMBUSH USES.  Both handlers test and set
   element 0x10 of the same block.  They cannot collide because the two events
   live on different maps and the block is cleared between chapters.

   THE ENEMY WAVE LANDS BEFORE ITS LINE AND THE ALLIED ONE AFTER ITS LINE.  The
   deployment at 00039274 comes ahead of the draw at 00039297, while the draw
   at 000392fe comes ahead of the deployment at 00039311 and a third draw at
   00039334 follows it.  What the player sees is therefore an enemy wave that
   is already standing when it is announced and an allied wave that is
   announced before it arrives; reordering either pair is visible.

   THE SWEEP IS INCLUSIVE AND SIGNED.  MOV EAX,[EBP-0xc] / CMP EAX,[EBP-0x14] /
   JLE at 00039369..0003936f, so unit 0x4f is written and a rewrite with a
   strict less-than would leave it holding position.

   THE MERGE KEEPS THE HIGH NIBBLE.  MOV DL,byte ptr [EAX+0x34] / AND DL,0xf0 /
   MOV DH,byte ptr [EBP-0x18] / OR DH,DL / MOV byte ptr [EAX+0x34],DH at
   00039389..00039397 reads the byte back, drops only its low four bits and
   writes the merged value.  The OR operand is 0 here, so the whole store looks
   like a plain zeroing; writing it as one also wipes bits 0x40 and 0x80, which
   fdps_map_actor_take_best_action (00012c6f) and fdps_score_targets_for_item
   (0001337d) test as independent per-unit flags (rebuild_info/pitfalls.md).

   THE SWEEP IS AN INLINE EXPANSION AND NOT A CALL.  The three constants are
   parked at [EBP-0x24], [EBP-0x20] and [EBP-0x1c] and copied into a second set
   of slots at [EBP-0x10], [EBP-0x14] and [EBP-0x18] before the counter is
   seeded -- the fingerprint of fdps_object_set_field34_low_nibble_range
   expanded in place -- and the only CALL inside the loop is
   fdps_get_unit_record, once per iteration.  Writing the range as a call to
   that helper would put a CALL in the rebuild that the original does not make.

   THE SWEEP IS NOT BOUNDED BY THE UNIT COUNT.  Both ends are literals, and on
   MAP25.DAT they land on the wave-0 block exactly, so the loop never reaches
   past a live record there; on a map carrying fewer records it would.  Adding
   a bound would be a different function.

   THE HOLD REUSES THE ARGUMENT SLOT.  MOV dword ptr [EBP+0x14],0x0 at
   000392bb seeds the twelve-frame counter over the incoming index, which by
   then has already been spent on the record fetch and is never read again.
   The counter is emitted as a local of its own here; nothing observes the
   difference.

   THE CURSOR MODE IS NOT SAVED AND RESTORED.  MOV dword ptr [0x00069cd0],0x1
   at 000392d9 is a literal store, so whatever the global held on entry is
   lost.

   NO VALUE IS USED AFTER A CALL EXCEPT THE TWO RECORD POINTERS.  All three
   fdps_draw_text cursors are discarded -- the next instruction after each CALL
   is the ADD ESP that cleans its arguments -- and fdps_deploy_wave,
   fdps_map_cursor_move_to and fdps_render_view_frame all return nothing.  The
   two fdps_get_unit_record results come back in EAX and are stored to [EBP-0x4]
   and [EBP-0x8]; the loop's copy is re-read once for the load of the AI byte
   and again for the store, so both halves of the merge address the record that
   iteration fetched.  Nothing sets EAX before the RET and no dispatcher reads
   what comes back, so the result is void. */
void fdps_chapter_26_event_deploy_waves_2_and_3(int unit_index)
{
    /* The unit that tripped the tile event, resolved before either gate; only
       its side byte is read. */
    struct fdps_unit_record *triggering_unit;
    /* The record the sweep is writing the behaviour mode into, re-resolved on
       every pass. */
    struct fdps_unit_record *released_unit;
    int released_unit_index;
    /* Which of the twelve frames the stop at the far end of the pan is
       holding for. */
    int hold_frame;

    triggering_unit = fdps_get_unit_record(unit_index);

    if (data_fdps_map_cell_event_triggered_flags[CHAPTER_EVENT_ONE_SHOT_SLOT]
            == 0
        && triggering_unit->side == PLAYER_SIDE) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH26_WAVES_ENEMY_WAVE,
                         CH26_WAVES_PLACE_ON_NEAREST_FREE_TILE);

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH26_WAVES_AFTER_ENEMY_WAVE_TEXT_ID,
                       (unsigned char *) CH26_WAVES_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        data_fdps_map_cursor_draw_mode = CH26_WAVES_MAP_CURSOR_BLANK;

        fdps_map_cursor_move_to(CH26_WAVES_PAN_X, CH26_WAVES_PAN_Y);
        for (hold_frame = 0;
             hold_frame < CH26_WAVES_PAN_HOLD_FRAMES;
             hold_frame++) {
            fdps_render_view_frame();
        }

        data_fdps_map_cursor_draw_mode = CH26_WAVES_MAP_CURSOR_NORMAL;

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH26_WAVES_BEFORE_ALLY_WAVE_TEXT_ID,
                       (unsigned char *) CH26_WAVES_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH26_WAVES_ALLY_WAVE,
                         CH26_WAVES_PLACE_ON_NEAREST_FREE_TILE);

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH26_WAVES_AFTER_ALLY_WAVE_TEXT_ID,
                       (unsigned char *) CH26_WAVES_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        for (released_unit_index = CH26_WAVES_FIRST_RELEASED_INDEX;
             released_unit_index <= CH26_WAVES_LAST_RELEASED_INDEX;
             released_unit_index++) {
            released_unit = fdps_get_unit_record(released_unit_index);
            released_unit->ai_behavior = (unsigned char)
                ((released_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 CH26_WAVES_BEHAVIOR_MODE);
        }

        data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_ONE_SHOT_SLOT] = 1;
    }
}

/* The element of data_fdps_map_cell_event_triggered_flags the wipe line
   latches, byte ptr [0x000640ea] at 000393c3 and 00039428 -- element 0x12 of
   the array based at 0x000640d8, and NOT element 0x10, which the ambush above
   and chapter 25's in src/chevt5.c share.  A sweep of the whole image for that
   address finds four instructions: these two and the pair in
   fdps_chapter_27_event_deploy_wave_1 at 00039453 and 00039533, which latches
   the same element for its own one-shot event one chapter later.  The two
   cannot collide because one chapter is loaded at a time and
   fdps_chapter_state_reset memsets the whole 0x20-byte block when a chapter
   starts; that memset, and the save and load paths that move the whole block
   to and from the slot image, are also why the latch has
   to live in the array and not in a function-local static, which would leave
   the line spoken across a chapter restart and across a reload
   (rebuild_info/pitfalls.md). */
#define CH26_WIPE_ONE_SHOT_SLOT 0x12

/* The seven unit records the wipe watches: ADD EAX,0x50 at 000393e6 over a
   counter the CMP dword ptr [EBP+0x14],0x7 / JL at 000393d3 runs from 0 to 6,
   so indices 0x50 through 0x56.  On MAP25.DAT those are exactly the seven
   wave-2 records fdps_chapter_26_event_deploy_waves_2_and_3 above appends: the
   twelve party slots occupy 0x00..0x0b, the map's 68 wave-0 records 0x0c..0x4f,
   and the five wave-3 allies land at 0x57..0x5b behind them.  Both numbers are
   literals and neither is derived from data_fdps_map_unit_count. */
#define CH26_WAVE_2_FIRST_UNIT_INDEX 0x50
#define CH26_WAVE_2_UNIT_COUNT 7

/* The entry of the chapter's own FDETXT26.TXT block the line is spoken from,
   PUSH 0x17 at 00039418.  Its token stream opens with speaker code -0x11 and
   operand 0x0c, MAP25.DAT's single wave-3 ally, so the line belongs to the
   guest hero the allied wave brings on and not to any of the seven it is spoken
   over. */
#define CH26_WIPE_TEXT_ID 0x17

/* Where the line is drawn, PUSH 0xa0000 at 00039413: the top-left corner of the
   visible mode-13h page, kept a literal because it is an address inside the
   display adapter's aperture rather than the address of anything the linker
   places (rebuild_info/pitfalls.md, contract E). */
#define CH26_WIPE_TEXT_DEST 0x000a0000

/* 000393b0.  Chapter 26's wave-2 wipe line: the death script all seven of the
   chapter's wave-2 reinforcements carry, which speaks one line once the last of
   them has gone and then latches itself off.

   The frame is the family's four-push one with a single 4-byte local -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x4 at
   000393b0..000393b6 -- and that local at [EBP-0x4] is the survivor flag, so
   the one incoming dword sits at [EBP+0x14].  Both caller-cleans in the body
   are this function's own (ADD ESP,0x4 after each retirement test at 000393ef
   and ADD ESP,0x1c after the draw at 00039425) and the RET at 00039435 carries
   no immediate, so the convention is the stack one at this end.  It is the
   stack one at the other end too: the dispatcher fdps_run_death_scripts reaches
   this table slot as MOV EAX,[EBP+0x14] / PUSH EAX / CALL dword ptr
   [EDX + 0x601c4] / ADD ESP,0x4 at 0001dcaa..0001dcb4, pushing one dword and
   cleaning it itself.

   BOTH GATES LEAVE BY THE SAME EXIT.  The JNZ at 000393ca on the latch and the
   JNZ at 00039403 on the survivor flag both go to the epilogue at 0003942f, so
   a firing that finds the line already spoken and a firing that finds one of
   the seven still standing are refused identically: nothing is drawn and the
   latch is not touched.

   THE LATCH TEST IS "NOT ZERO" AND NOT "NOT ONE".  CMP byte ptr
   [0x000640ea],0x0 at 000393c3, so any non-zero value in the element blocks the
   body.

   THE FLAG IS SEEDED BEFORE THE LATCH IS TESTED.  MOV dword ptr [EBP-0x4],0x0
   at 000393bc runs ahead of the CMP at 000393c3, which is why it is written
   above the gate here rather than inside it.  Nothing observes the difference;
   the store is kept where its own assembly has it.

   THE POLL DOES NOT STOP AT THE FIRST SURVIVOR.  The JNZ at 000393f4 skips only
   the flag store and the loop runs its full seven passes either way, so every
   firing costs seven calls.  fdps_unit_is_retired only reads a record, so a
   rewrite that broke out early would differ in nothing but the call count --
   and it would still be a different function.

   THE SENSE OF THE TEST IS INVERTED.  TEST EAX,EAX / JNZ at 000393f2 raises the
   flag when fdps_unit_is_retired answers 0, so the flag means "one of the seven
   is still standing" and the draw is guarded by it being clear.

   THE RANGE IS SEVEN RECORDS FROM 0x50 AND IS NOT BOUNDED BY THE UNIT COUNT.
   Both numbers are literals; nothing in the body reads
   data_fdps_map_unit_count, so on a map with fewer records the loop would read
   past the live ones.  On MAP25.DAT, the only map whose data reaches this slot,
   they land on the wave-2 block exactly.

   THE INCOMING ARGUMENT IS NEVER READ.  MOV dword ptr [EBP+0x14],0x0 at
   000393cc is the seed of the 0..6 counter, which the original keeps in its own
   argument slot; the value fdps_run_death_scripts pushed -- the index of the
   unit that made the killing action, not that of the dead unit whose script is
   running -- is spent by that store and nothing loads the slot before it.  The
   counter is emitted as a local of its own here; nothing observes the
   difference.

   ONE VALUE IS USED AFTER A CALL.  fdps_unit_is_retired's answer comes back in
   EAX and is tested by the TEST EAX,EAX at 000393f2 that follows the ADD ESP
   cleaning its argument, so it is the call's own result and not a leftover.
   fdps_draw_text's cursor is discarded -- the next instruction after the CALL
   at 00039420 is the ADD ESP,0x1c that cleans its arguments.  Nothing sets EAX
   before the RET and no dispatcher reads what comes back, so the result is
   void. */
void fdps_chapter_26_event_wave_2_defeated_line(int unit_index)
{
    /* Raised when one of the seven answers the retirement test with 0, so the
       line is drawn only while it is still clear. */
    int any_wave_2_unit_still_standing;
    /* Which of the seven wave-2 records is being asked, 0 to 6. */
    int wave_2_slot;

    any_wave_2_unit_still_standing = 0;

    /* The original seeds that counter over its own argument slot at 000393cc,
       which is the only access the body makes to the slot; the counter is a
       local of its own here, so this is what is left of that store. */
    unit_index = 0;

    if (data_fdps_map_cell_event_triggered_flags[CH26_WIPE_ONE_SHOT_SLOT]
            == 0) {
        for (wave_2_slot = 0;
             wave_2_slot < CH26_WAVE_2_UNIT_COUNT;
             wave_2_slot++) {
            if (fdps_unit_is_retired(CH26_WAVE_2_FIRST_UNIT_INDEX
                                     + wave_2_slot) == 0) {
                any_wave_2_unit_still_standing = 1;
            }
        }

        if (any_wave_2_unit_still_standing == 0) {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH26_WIPE_TEXT_ID,
                           (unsigned char *) CH26_WIPE_TEXT_DEST,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            data_fdps_map_cell_event_triggered_flags[
                CH26_WIPE_ONE_SHOT_SLOT] = 1;
        }
    }
}

/* The element of data_fdps_map_cell_event_triggered_flags this handler
   latches, byte ptr [0x000640ea] at 00039453 and 00039533 -- element 0x12 of
   the array based at 0x000640d8, the same element
   fdps_chapter_26_event_wave_2_defeated_line above uses and NOT element 0x10,
   which the ambush and chapter 25's handlers share.  A sweep of the whole image
   for 0x000640ea finds four instructions and they are those two pairs.  The two
   handlers cannot collide because one chapter is loaded at a time and
   fdps_chapter_state_reset memsets the whole 0x20-byte block when a chapter
   starts; that memset, and the save and load paths that move the whole block to
   and from the slot image, are also why the latch has to live in the array and
   not in a function-local static, which would leave the wave undeployed and the
   boss held in place on a chapter replay after a Game Over or a save load
   (rebuild_info/pitfalls.md).

   It is spelled separately from CH26_WIPE_ONE_SHOT_SLOT above because the two
   handlers are two chapters' events that happen to share one element, not one
   piece of state the two of them keep between them. */
#define CH27_WAVE_1_ONE_SHOT_SLOT 0x12

/* The four unit records the death script watches: ADD EAX,0xd at 0003947a over
   a counter the CMP dword ptr [EBP+0x14],0x4 / JL at 00039467 runs from 0 to 3,
   so indices 0x0d through 0x10.  On MAP26.DAT those are deployment records 1 to
   4 -- the chapter's four generals -- landing behind the map's twelve party
   slots at 0x00..0x0b, with record 0, the Mage King himself, at 0x0c.  All four
   carry the same death script, so this handler runs once per general killed and
   only the last of the four runs finds the whole group retired.  Both numbers
   are literals and neither is derived from data_fdps_map_unit_count. */
#define CH27_GENERALS_FIRST_UNIT_INDEX 0x0d
#define CH27_GENERALS_COUNT 4

/* Where the line is drawn, PUSH 0xa0000 at 000394ab: the top-left corner of the
   visible mode-13h page, kept a literal because it is an address inside the
   display adapter's aperture rather than the address of anything the linker
   places (rebuild_info/pitfalls.md, contract E). */
#define CH27_WAVE_1_TEXT_DEST 0x000a0000

/* The entry of the chapter's own FDETXT27.TXT block the line is spoken from,
   PUSH 0x14 at 000394b0.  Its token stream opens with speaker code -0x11 and
   operand 0x3f, the Mage King, so the line belongs to the boss the same body
   releases two statements later and not to any of the four generals it is
   spoken over. */
#define CH27_WAVE_1_TEXT_ID 0x14

/* The wave the reinforcements are tagged with, PUSH 0x1 at 000394c3, matched
   against byte 0x15 of every 0x1a-byte deployment record of the resident
   MAP%02d.DAT block; on MAP26.DAT that is a block of thirty records.  The map
   number handed alongside it is data_fdps_chapter_current_chapter_id, read from
   [0x00069cf4] at 000394c5, which is the map the chapter is playing and is what
   picks the "map%02d.cod" placement member. */
#define CH27_REINFORCEMENT_WAVE 1

/* How each arrival is placed, XOR EAX,EAX / PUSH EAX at 000394c0..000394c2: 0
   searches for the nearest free walkable tile around the placement record's own
   tile instead of taking that tile as given. */
#define CH27_PLACE_ON_NEAREST_FREE_TILE 0

/* The record the behaviour merge covers and the mode it writes: 0xc parked at
   [EBP-0x24] at 000394d3, 0xc again at [EBP-0x20] at 000394da and 0xb at
   [EBP-0x1c] at 000394e1.  The range is one record wide and it is unit index
   0x0c, MAP26.DAT's deployment record 0 -- the map's only level-40 unit, the
   Mage King, the same record fdps_chapter_27_post_action reads to declare the
   chapter cleared.  Mode 0xb is the spell-first chain that ends in a movement
   routine; the mode the map file deploys him in is 2, which strikes whatever is
   already in reach but never leaves its tile, so this is what takes him off his
   throne. */
#define CH27_RELEASED_FIRST_UNIT_INDEX 0x0c
#define CH27_RELEASED_LAST_UNIT_INDEX 0x0c
#define CH27_RELEASED_BEHAVIOR_MODE 0x0b

/* 00039440.  Chapter 27's four-generals death script: once the last of the Mage
   King's four generals is gone, his line is spoken, the map's wave-1
   reinforcements are brought onto the battlefield and he himself comes out of
   the hold-position behaviour.

   The frame is the family's four-push one with 0x24 bytes of locals -- PUSH EBX
   / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x24 at
   00039440..00039446 -- so the one incoming dword sits at [EBP+0x14].  Every
   caller-clean in the body is this function's own (ADD ESP,0x4 after each
   retirement test at 00039483, ADD ESP,0x1c after the draw at 000394bd, ADD
   ESP,0xc after the deployment at 000394d0 and ADD ESP,0x4 after each record
   fetch at 00039517) and the RET at 00039540 carries no immediate, so the
   convention is the stack one at this end.  It is the stack one at the other end
   too: the dispatcher fdps_run_death_scripts reaches this table slot as MOV
   EAX,[EBP+0x14] / PUSH EAX / CALL dword ptr [EDX + 0x601c4] / ADD ESP,0x4 at
   0001dcaa..0001dcb4, pushing one dword and cleaning it itself.

   BOTH GATES LEAVE BY THE SAME EXIT.  The JNZ at 0003945a on the latch and the
   JNZ at 00039497 on the survivor flag both go to the epilogue at 0003953a, so a
   firing that finds the wave already deployed and a firing that finds one of the
   four still standing are refused identically: nothing is drawn, nothing is
   deployed, no behaviour byte is touched and the latch is not written.

   THE LATCH TEST IS "NOT ZERO" AND NOT "NOT ONE".  CMP byte ptr
   [0x000640ea],0x0 at 00039453, so any non-zero value in the element blocks the
   body.

   THE FLAG IS SEEDED BEFORE THE LATCH IS TESTED.  MOV dword ptr [EBP-0x4],0x0 at
   0003944c runs ahead of the CMP at 00039453; nothing observes the difference
   and the store is kept where its own assembly has it.

   THE POLL DOES NOT STOP AT THE FIRST SURVIVOR.  The JNZ at 00039488 skips only
   the flag store and the loop runs its full four passes either way, so every
   firing costs four calls.  fdps_unit_is_retired only reads a record, so a
   rewrite that broke out early would differ in nothing but the call count -- and
   it would still be a different function.

   THE SENSE OF THE TEST IS INVERTED.  TEST EAX,EAX / JNZ at 00039486 raises the
   flag when fdps_unit_is_retired answers 0, so the flag means "one of the four
   is still standing" and the rest of the body is guarded by it being clear.

   THE BLOCK IS FOUR RECORDS FROM 0x0d AND IS NOT BOUNDED BY THE UNIT COUNT.
   Both numbers are literals; nothing in the body reads
   data_fdps_map_unit_count, so on a map with fewer records the poll would read
   past the live ones.  On MAP26.DAT, the only map whose data reaches this slot,
   they land on the four generals exactly.

   THE ORDER IS LINE, WAVE, RELEASE, LATCH.  The line is spoken while the
   reinforcements are still off the board and the boss is still holding, and the
   latch is written last, after everything else has happened.

   THE SWEEP IS AN INLINE EXPANSION AND NOT A CALL.  The three constants are
   parked at [EBP-0x24], [EBP-0x20] and [EBP-0x1c] and copied into a second set
   of slots at [EBP-0x10], [EBP-0x14] and [EBP-0x18] before the counter is seeded
   -- the fingerprint of fdps_object_set_field34_low_nibble_range expanded in
   place -- and the only CALL inside the loop is fdps_get_unit_record, once per
   iteration.  Writing the range as a call to that helper would put a CALL in the
   rebuild that the original does not make.

   THE SWEEP IS INCLUSIVE AND SIGNED, AND IT IS ONE RECORD WIDE.  MOV
   EAX,[EBP-0xc] / CMP EAX,[EBP-0x14] / JLE at 00039500..00039506 with both ends
   holding 0xc, so the loop runs exactly once; a rewrite with a strict less-than
   would leave the boss holding position.

   THE MERGE KEEPS THE HIGH NIBBLE.  MOV DL,byte ptr [EAX+0x34] / AND DL,0xf0 /
   MOV DH,byte ptr [EBP-0x18] / OR DH,DL / MOV byte ptr [EAX+0x34],DH at
   00039520..0003952e reads the byte back, drops only its low four bits and
   writes the merged value.  Writing the mode as a plain store also wipes bits
   0x40 and 0x80, which fdps_map_actor_take_best_action (00012c6f) and
   fdps_score_targets_for_item (0001337d) test as independent per-unit flags
   (rebuild_info/pitfalls.md).

   THE INCOMING ARGUMENT IS NEVER READ.  MOV dword ptr [EBP+0x14],0x0 at
   00039460 is the seed of the 0..3 counter, which the original keeps in its own
   argument slot; the value fdps_run_death_scripts pushed -- the index of the
   unit that made the killing action, not that of the dead general whose script
   is running -- is spent by that store and nothing loads the slot before it.
   The counter is emitted as a local of its own here; nothing observes the
   difference.

   TWO VALUES ARE USED AFTER A CALL.  fdps_unit_is_retired's answer comes back in
   EAX and is tested by the TEST EAX,EAX at 00039486 that follows the ADD ESP
   cleaning its argument, so it is the call's own result and not a leftover, and
   fdps_get_unit_record's record pointer comes back in EAX and is stored to
   [EBP-0x8], which is re-read once for the load of the AI byte and again for the
   store, so both halves of the merge address the record the fetch returned.  The
   fdps_draw_text cursor is discarded -- the next instruction after the CALL at
   000394b8 is the ADD ESP,0x1c that cleans its arguments -- and fdps_deploy_wave
   returns nothing.  Nothing sets EAX before the RET and no dispatcher reads what
   comes back, so the result is void.

   THE TEST DEPENDS ON THE CALLER'S ORDER.  fdps_run_death_scripts is reached
   only after fdps_play_death_animation_and_mark_dead has stored the retired bit
   into record+5, so the fourth general already reads as retired on the pass that
   kills him.  Running the scripts before the marking leaves the wave forever
   undeployed (rebuild_info/pitfalls.md).

   Table slot 43 at 00060270. */
void fdps_chapter_27_event_deploy_wave_1(int unit_index)
{
    /* Raised when one of the four answers the retirement test with 0, so the
       rest of the body runs only while it is still clear. */
    int any_general_still_standing;
    /* Which of the four general records is being asked, 0 to 3. */
    int general_slot;
    /* The record the behaviour merge is written into, re-resolved on every
       pass. */
    struct fdps_unit_record *released_unit;
    int released_unit_index;

    any_general_still_standing = 0;

    if (data_fdps_map_cell_event_triggered_flags[CH27_WAVE_1_ONE_SHOT_SLOT]
            == 0) {
        /* The store the original makes over its own argument slot at 00039460,
           which is the only access the body makes to the slot; the counter is a
           local of its own here, so this is what is left of that store. */
        unit_index = 0;

        for (general_slot = 0;
             general_slot < CH27_GENERALS_COUNT;
             general_slot++) {
            if (fdps_unit_is_retired(CH27_GENERALS_FIRST_UNIT_INDEX
                                     + general_slot) == 0) {
                any_general_still_standing = 1;
            }
        }

        if (any_general_still_standing == 0) {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH27_WAVE_1_TEXT_ID,
                           (unsigned char *) CH27_WAVE_1_TEXT_DEST,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);

            fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                             CH27_REINFORCEMENT_WAVE,
                             CH27_PLACE_ON_NEAREST_FREE_TILE);

            for (released_unit_index = CH27_RELEASED_FIRST_UNIT_INDEX;
                 released_unit_index <= CH27_RELEASED_LAST_UNIT_INDEX;
                 released_unit_index++) {
                released_unit = fdps_get_unit_record(released_unit_index);
                released_unit->ai_behavior = (unsigned char)
                    ((released_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                     CH27_RELEASED_BEHAVIOR_MODE);
            }

            data_fdps_map_cell_event_triggered_flags[
                CH27_WAVE_1_ONE_SHOT_SLOT] = 1;
        }
    }
}
