/* chevt5.c -- the scripted chapter-event handlers of chapters 24 to 27.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt5.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7, chevt2.c for 8 to 14, chevt3.c for 15 to 19, chevt4.c for
 * 20 to 23 and chevt6.c for 28 to 30.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "text.h"
#include "deploy.h"
#include "chevt5.h"

/* The destination the handler hands fdps_draw_text and the screen's row
   stride, PUSH 0xa0000 and PUSH 0x140 before each of the six draws: the
   top-left corner of the visible mode-13h page.  0xa0000 stays a literal
   because it is an address inside the display adapter's aperture rather than
   the address of anything the linker places (rebuild_info/pitfalls.md,
   contract E). */
#define CH24_TURN_TEXT_DEST 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d before each
   draw: glyph fill, no cell background, and the shadow the outline colour
   becomes while the font's outline flag is clear. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* The six turns the handler names, CMP dword ptr [0x00069ce8],... at 00038cd3,
   00038d17, 00038d48, 00038d8c, 00038dd0 and 00038e11.  They are tested in
   this order and not in numeric order, because it is the order MAP23.DAT's
   turn-event table lists its six live records in: (5, 8, 7, 10, 13, 15), every
   one of them routed to table slot 36.  All six tests are equalities, so the
   turn counter's signedness cannot be seen here. */
#define CH24_WAVE_2_TURN 5
#define CH24_LINE_ONLY_TURN 8
#define CH24_SHAN_ARRIVAL_TURN 7
#define CH24_WAVE_4_TURN 10
#define CH24_WAVE_3_TURN 13
#define CH24_WAVE_5_TURN 15

/* The entries of the chapter's own FDETXT24.TXT block each turn speaks, PUSH
   0x10 at 00038cef, PUSH 0x11 at 00038d33, PUSH 0xe at 00038d77, PUSH 0x12 at
   00038dbb, PUSH 0x13 at 00038dec and PUSH 0x14 at 00038e2d, out of the block
   data_fdps_current_chapter_text_ptr holds.  Turn 7's is the lowest of the six
   and is not in step with the turn order either. */
#define CH24_WAVE_2_TEXT_ID 0x10
#define CH24_LINE_ONLY_TEXT_ID 0x11
#define CH24_SHAN_ARRIVAL_TEXT_ID 0x0e
#define CH24_WAVE_4_TEXT_ID 0x12
#define CH24_WAVE_3_TEXT_ID 0x13
#define CH24_WAVE_5_TEXT_ID 0x14

/* The waves each turn brings on, PUSH 0x2 at 00038d02, PUSH 0x6 at 00038d54,
   PUSH 0x4 at 00038d98, PUSH 0x3 at 00038dff and PUSH 0x5 at 00038e40.  The
   wave number is a literal per case and is never derived from the turn: the
   turns are 5, 7, 10, 13, 15 and the waves they ask for are 2, 6, 4, 3, 5, so
   no offset from the turn produces them.  Wave 6 is MAP23.DAT's one
   player-side record, 珊 the 法師 at character id 0x0a, and the other four are
   groups of enemies. */
#define CH24_WAVE_2 2
#define CH24_SHAN_WAVE 6
#define CH24_WAVE_4 4
#define CH24_WAVE_3 3
#define CH24_WAVE_5 5

/* How every wave here is placed, XOR EAX,EAX / PUSH EAX before each of the
   five deployment calls: zero, so fdps_deploy_wave passes 0 on to
   fdps_deploy_unit and each arrival settles on the nearest free walkable tile
   to its placement record's coordinates rather than on those coordinates
   themselves. */
#define CH24_TURN_PLACE_ON_NEAREST_FREE_TILE 0

/* 00038cc0.  Chapter 24's turn-scheduled event handler: the one slot MAP23.DAT
   names for all six of the chapter's turn events, running whichever of them is
   due for the turn that has just been finished.

   The frame is the family's four-push one with no locals at all -- PUSH EBX /
   PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 at
   00038cc0..00038cc6 -- so the one incoming dword sits at [EBP+0x14].  Every
   caller-clean in the body is this function's own (ADD ESP,0x1c after each
   draw and ADD ESP,0xc after each deployment), the RET at 00038e54 carries no
   immediate, and fdps_battle_run_turn_events pushes one dword and drops it
   with ADD ESP,0x4 at 0002e146, so the convention is the stack one at both
   ends of the call.

   THE CALL ORDER INSIDE A CASE IS BEHAVIOUR.  Turns 5, 13 and 15 draw and then
   deploy; turns 7 and 10 deploy and then draw.  See chevt5.h.

   THE CHAIN HAS NO DEFAULT BRANCH.  Six equality tests, each falling through a
   JNZ into the next, and the last JNZ at 00038e18 goes to the epilogue, so a
   turn none of the six names leaves the body having done nothing.  The six are
   mutually exclusive, so nothing depends on which of them is tested first.

   THE WAVE IS A LITERAL PER CASE.  Unlike the chapter 23 handler next door,
   which biases the turn counter by four to get its wave key, nothing here is
   computed from the turn -- see the wave constants above.

   THE INCOMING ARGUMENT IS NEVER READ.  MOV dword ptr [EBP+0x14],0x0 at
   00038ccc is the only access to the slot in the whole body and nothing loads
   it afterwards.  fdps_battle_run_turn_events, the only dispatcher that
   reaches this table slot, pushes a literal 0 at 0002e13e, so no value is lost
   by that.

   NO VALUE IS USED AFTER A CALL.  Each fdps_draw_text cursor is discarded --
   the next instruction is the ADD ESP that cleans its arguments -- and
   fdps_deploy_wave returns nothing.  Nothing sets EAX before the RET and no
   dispatcher reads what comes back, so the result is void. */
void fdps_chapter_24_event_deploy_wave_for_turn(int event_arg)
{
    /* The store the original makes over its own argument slot and never reads
       back; nothing here counts in it. */
    event_arg = 0;

    if (data_fdps_battle_turn_counter == CH24_WAVE_2_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_WAVE_2_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH24_WAVE_2,
                         CH24_TURN_PLACE_ON_NEAREST_FREE_TILE);
    } else if (data_fdps_battle_turn_counter == CH24_LINE_ONLY_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_LINE_ONLY_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter == CH24_SHAN_ARRIVAL_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH24_SHAN_WAVE,
                         CH24_TURN_PLACE_ON_NEAREST_FREE_TILE);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_SHAN_ARRIVAL_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter == CH24_WAVE_4_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH24_WAVE_4,
                         CH24_TURN_PLACE_ON_NEAREST_FREE_TILE);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_WAVE_4_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter == CH24_WAVE_3_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_WAVE_3_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH24_WAVE_3,
                         CH24_TURN_PLACE_ON_NEAREST_FREE_TILE);
    } else if (data_fdps_battle_turn_counter == CH24_WAVE_5_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_WAVE_5_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH24_WAVE_5,
                         CH24_TURN_PLACE_ON_NEAREST_FREE_TILE);
    }
}
