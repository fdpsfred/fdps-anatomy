/* btlmenu.c -- the battle screen's system menu.
 *
 * See btlmenu.h for what each one answers.  Everything here is a driver: the
 * ring these menus are drawn as, the sweep animations and the cursor loop all
 * live in menu.c, and what this file contributes is which entries a menu has,
 * which of them are greyed out this time, and what happens when one is chosen.
 * The commands an acting unit opens on its own turn are btlact.c.
 *
 * malloc and free come from <stdlib.h>, memmove from <string.h>, delay from
 * <i86.h>, access from <io.h> and the stream calls from <stdio.h>, which is
 * where Watcom 10.0a declares each of them; all of them are real calls in the
 * original -- CALL 0x00042438 at 00014ecc for access, CALL 0x0003d375 at
 * 000150cc and CALL 0x0003d478 at 0001530d for malloc and free, CALL
 * 0x0003d514 at 00015161 for memmove, CALL 0x0003d370 at 0001500c and
 * 00014c1a for delay
 * and CALL 0x0004265e / 0x0004270d / 0x00042a41 / 0x000428be for fopen, fread,
 * fwrite and fclose -- because the flag set carries no -oi
 * (rebuild_info/build_flags.md), so the plain declarations are what reproduce
 * them.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "btlend.h"
#include "btlturn.h"
#include "chapter.h"
#include "keybd.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "menu.h"
#include "movegrid.h"
#include "msgwin.h"
#include "savefile.h"
#include "text.h"
#include "unit.h"
#include "btlmenu.h"

/* The four entries of the system submenu in ring slot order -- 0 up, 1 left,
   2 right, 3 down (menu.h) -- and the Command.cel sub-image each shows, the
   four ints of the template at 00014700 copied onto the stack at
   00014eb4..00014eb7. */
#define SYSTEM_MENU_SLOTS 4
#define SYSTEM_ICON_OBJECTIVES 0x17
#define SYSTEM_ICON_SAVE 0x09
#define SYSTEM_ICON_LOAD 0x0a
#define SYSTEM_ICON_QUIT 0x08

/* Which slot does what.  The dispatch is a chain of equality tests in the
   order quit, objectives, save, load -- CMP dword ptr [EBP-0xc] against 3, 0,
   1 and 2 at 00014f94, 00015064, 0001507b and 00015393 -- and a value outside
   0..3 falls off the end of it and reopens the menu. */
#define MENU_ENTRY_OBJECTIVES 0
#define MENU_ENTRY_SAVE 1
#define MENU_ENTRY_LOAD 2
#define MENU_ENTRY_QUIT 3

/* The descriptor value that greys an entry out, MOV dword ptr [EBP-0x24],0x1
   at 00014ed8 and [EBP-0x28],0x1 at 00014f2e.  The ring reads any non-zero as
   greyed (menu.h); one is what this menu writes. */
#define MENU_ENTRY_DISABLED 1

/* struct fdps_unit_record's flags byte at +5 (fdpstype.h): bit 0 retires the
   unit and bit 7 says it has already acted this turn.  A unit that is still in
   the battle and has already acted is what greys the save entry out. */
#define UNIT_FLAG_RETIRED 0x01
#define UNIT_FLAG_ACTED 0x80

/* What fdps_menu_cursor_input_loop and fdps_prompt_two_choice answer with.
   The cursor loop's -1 is the only thing this function propagates; the
   prompt's 0 is the affirmative and both of its other answers decline
   (menu.h, msgwin.h). */
#define MENU_CURSOR_CANCELLED (-1)
#define PROMPT_ANSWER_YES 0

/* What this function answers with (btlmenu.h). */
#define SUBMENU_CANCELLED (-1)
#define SUBMENU_DONE 0
#define SUBMENU_QUIT_CONFIRMED 1

/* The face index the message window is opened with, PUSH 0x0 at 00014f9e,
   00015085 and 0001539d: portrait 0 of FACE.CEL, the same speaker for all
   three prompts. */
#define SUBMENU_FACE_INDEX 0

/* The nine entries of data_fdps_all_game_text_ptr this menu writes, pushed at
   00014fbb, 00014fef, 0001503a, 000150a2, 00015328, 0001535d, 000153ba,
   000153ee and 0001542d.  They are one contiguous family with the corrupt-save
   line 0x208 that fdps_load_savegame draws and the empty-slot line 0x209 of
   savepnl.c. */
#define TEXT_QUIT_QUESTION 0x1f0
#define TEXT_QUIT_CONFIRMED 0x1f1
#define TEXT_QUIT_DECLINED 0x1f2
#define TEXT_SAVE_QUESTION 0x1f3
#define TEXT_SAVE_DONE 0x203
#define TEXT_SAVE_DECLINED 0x204
#define TEXT_LOAD_QUESTION 0x205
#define TEXT_LOAD_CONFIRMED 0x206
#define TEXT_LOAD_DECLINED 0x207

/* Where in the message panel the two lines go, and in what colours.  0xaa44a
   is screen (138, 131) and 0xabc0a is screen (138, 150) -- the question sits
   on the panel's first text row and the answer that replaces it two rows down
   -- with 0x140 the mode 13h row stride.  All of them are addresses inside the
   display adapter's aperture rather than the address of anything the linker
   places, so they stay literals (contract E, rebuild_info/pitfalls.md).  The
   three colours are the standard message colours, PUSH 0x6d / PUSH 0x0 /
   PUSH 0xd0 in front of every one of the nine calls. */
#define MESSAGE_QUESTION_AT 0x000aa44a
#define MESSAGE_ANSWER_AT 0x000abc0a
#define VGA_SCREEN_PITCH 0x140
#define MESSAGE_TEXT_FG_COLOR 0xd0
#define MESSAGE_TEXT_BG_COLOR 0
#define MESSAGE_TEXT_OUTLINE_COLOR 0x6d

/* How long the answer is left on screen before the panel comes down, PUSH 0xc8
   at 00015002, 0001533b and 00015401, PUSH 0x1f4 at 0001504d and PUSH 0x12c at
   00015370 and 00015440.  An affirmative answer holds for 200 ms and a
   declined save or load for 300; the declined QUIT holds for 500, which is the
   one value of the six that is not shared with anything. */
#define ANSWER_HOLD_MS 200
#define DECLINED_HOLD_MS 300
#define QUIT_DECLINED_HOLD_MS 500

/* FDE.SAV and the two modes it is opened in, the literals at 0x00060118,
   0x0006157c and 0x00061580.  The name is the same string fdps_load_savegame
   and the save screens open (savefile.h). */
#define SAVE_FILE_NAME "FDE.SAV"
#define SAVE_FILE_READ_MODE "rb"
#define SAVE_FILE_WRITE_MODE "wb"

/* The whole image and where its checksum sits, PUSH 0x59cb at 000150c7,
   000150f8, 0001510b, 000152bb, 000152d5 and 000152ea and MOV [EDX+0x59c7],EAX
   at 000152cf -- the same two numbers fdps_load_savegame reads the file with
   (savefile.h). */
#define SAVE_IMAGE_BYTES 0x59cb
#define SAVE_CHECKSUM_AT 0x59c7

/* The four blocks of the battle resume image and the scalar header behind
   them, from the ADD EAX,<offset> in front of each memmove: 0x8a3 at 00015177,
   0x12a3 at 00015196, 0x30a3 at 000151af and 0x30c3 at 000151c0.  The field
   block is at offset 0 and needs no add.  Each length is the one that call
   site pushes -- PUSH 0x8a3 at 00015152, PUSH 0xa00 at 00015169, IMUL
   EAX,[0x00060150],0x50 at 00015185 and PUSH 0x20 at 000151a4 -- and the
   loader reads every one of them back at the same displacement, which is what
   makes them a contract rather than one function's opinion. */
#define SAVE_FIELD_BLOCK_BYTES 0x8a3
#define SAVE_ROSTER_AT 0x8a3
#define SAVE_ROSTER_BYTES 0xa00
#define SAVE_UNIT_ARRAY_AT 0x12a3
#define SAVE_TRIGGERED_FLAGS_AT 0x30a3
#define SAVE_TRIGGERED_FLAGS_BYTES 0x20
#define SAVE_RESUME_HEADER_AT 0x30c3
#define MAP_UNIT_RECORD_BYTES 0x50

/* Which byte of the scalar header each global goes into, as the offset from
   the header pointer the original keeps in [EBP-0x1c] -- the MOV byte ptr
   [EDX + <offset>],AL chain at 000151d0 through 000152a1.  fdps_load_savegame
   reads the same eighteen bytes back at the same offsets (savefile.c).

   +0x07 AND +0x08 ARE WRITTEN AS THE LITERAL ZERO, MOV byte ptr [EAX+0x7],0x0
   at 00015257 and [EAX+0x8],0x0 at 0001525b, and the loader reads neither, so
   the two bytes are a hole in the header and not state that goes missing on
   the way back in.

   +0x0a is the one field that is not a byte: MOV [EDX+0xa],EAX at 00015275 is
   a 32-bit store at an offset that is not four-byte aligned. */
#define RESUME_TURN_COUNTER 0x00
#define RESUME_UNIT_COUNT 0x01
#define RESUME_CHAPTER 0x02
#define RESUME_VIEW_ORIGIN_TILE_X 0x03
#define RESUME_VIEW_ORIGIN_TILE_Y 0x04
#define RESUME_CURSOR_TILE_X 0x05
#define RESUME_CURSOR_TILE_Y 0x06
#define RESUME_UNUSED_07 0x07
#define RESUME_UNUSED_08 0x08
#define RESUME_ROSTER_MEMBERS 0x09
#define RESUME_PARTY_GOLD 0x0a
#define RESUME_BATTLE_ANIM 0x0e
#define RESUME_TERRAIN_HUD 0x0f
#define RESUME_BGM_ENABLED 0x10
#define RESUME_SFX_ENABLED 0x11

/* One map tile is 24 pixels square, MOV EBX,0x18 in front of each of the four
   IDIVs at 000151fb, 00015216, 00015231 and 0001524c: the four globals hold
   world pixels and the header stores tile indices.

   THE DIVIDE IS SIGNED, CDQ-shaped -- the value is loaded into EAX, loaded a
   second time into EDX and SAR EDX,0x1f before the IDIV -- so a coordinate
   that ever went negative rounds toward zero rather than walking off into a
   huge quotient.  The four globals are signed ints (gamedata.h), which is what
   makes that the divide the compiler emits. */
#define MAP_TILE_SIZE 0x18

/* The four chapter slots behind the resume region and the byte that says a
   slot has never been written, from the four MOV byte ptr [EAX + <offset>],0xff
   at 0001512d, 00015137, 00015141 and 0001514b: 0x3b2b, 0x4553, 0x4f7b and
   0x59a3 are 0x312b + n * 0xa28 + 0xa00, which is struct fdps_save_slot's
   chapter_index in each of the four (fdpstype.h, save.c). */
#define SAVE_IMAGE_SLOTS_AT 0x312b
#define SAVE_SLOT_UNWRITTEN_CHAPTER 0xff

/* The four entries of the outer battle menu in ring slot order -- 0 up, 1
   left, 2 right, 3 down (menu.h) -- and the Command.cel sub-image each shows,
   the four ints of the template at 000146e0 copied onto the stack by the MOVSD
   run at 00014ac4..00014ac7.  The descriptor that goes with them is the
   all-zero template at 000146f0 (00014ad0..00014ad3), and nothing in this
   function ever writes into it, so no entry of this menu is ever greyed. */
#define OUTER_MENU_SLOTS 4
#define OUTER_ICON_SYSTEM 0x16
#define OUTER_ICON_ADVANCE_ALL 0x0b
#define OUTER_ICON_OPTIONS 0x0c
#define OUTER_ICON_END_TURN 0x13

/* Which slot does what: the dispatch is CMP dword ptr [EBP-0xc] against 0, 1,
   2 and 3 at 00014b6e, 00014ba3, 00014da6 and 00014db6, and a value outside
   0..3 falls off the end of the chain and reopens the menu. */
#define OUTER_ENTRY_SYSTEM 0
#define OUTER_ENTRY_ADVANCE_ALL 1
#define OUTER_ENTRY_OPTIONS 2
#define OUTER_ENTRY_END_TURN 3

/* The closing retraction this function plays itself rather than through
   fdps_menu_animate_close: radius 0x17 at 00014b17, ADD [EBP-0x14],-0x4 at
   00014b26 and CMP [EBP-0x14],0x0 / JG at 00014b1e, so the six frames are
   drawn at 0x17, 0x13, 0xf, 0xb, 7 and 3.  Those are the radii
   fdps_menu_animate_close draws too, but that function opens with the window
   cue (menu.h) and this loop does not, which is why it is written out here
   and not replaced by the call. */
#define CLOSE_SWEEP_FIRST_RADIUS 0x17
#define CLOSE_SWEEP_STEP 4

/* The two values the cursor overlay and the play-active flag are switched
   between.  Both are lowered once on entry, at 00014adb and 00014ae5, and
   raised again on the way out (gamedata.h). */
#define CURSOR_DRAW_MODE_HIDDEN 0
#define CURSOR_DRAW_MODE_NORMAL 1
#define PLAY_INACTIVE 0
#define PLAY_ACTIVE 1

/* The six entries of data_fdps_all_game_text_ptr the two confirmed arms write,
   pushed at 00014bca, 00014c02, 00014d7c, 00014ddd, 00014e11 and 00014e65: a
   question, the line that replaces it on yes, and the line that replaces it on
   no, for each of the two arms. */
#define TEXT_ADVANCE_ALL_QUESTION 0x1ea
#define TEXT_ADVANCE_ALL_CONFIRMED 0x1eb
#define TEXT_ADVANCE_ALL_DECLINED 0x1ec
#define TEXT_END_TURN_QUESTION 0x1ed
#define TEXT_END_TURN_CONFIRMED 0x1ee
#define TEXT_END_TURN_DECLINED 0x1ef

/* The end-turn arm's answer goes one row lower than every other answer in this
   file: PUSH 0xad3ca at 00014e0c and 00014e60 is screen (138, 169), where the
   advance-all arm's PUSH 0xabc0a at 00014bfd and 00014d77 is (138, 150).  Like
   the other two it is an address in the display adapter's aperture and stays
   a literal (contract E). */
#define MESSAGE_END_TURN_ANSWER_AT 0x000ad3ca

/* Both answers are held for 300 ms, PUSH 0x12c at 00014c15, 00014d8f,
   00014e24 and 00014e78, the yes as long as the no. */
#define OUTER_ANSWER_HOLD_MS 300

/* The face index both prompts open the message window with, PUSH 0x0 at
   00014bad and 00014dc0. */
#define OUTER_FACE_INDEX 0

/* Which units the advance-all arm walks.  record +5 AND AL,0x85 at 00014c9c
   skips any unit with bit 0 (retired), bit 7 (acted this turn) or bit 2 set;
   no instruction in the image stores bit 2 as an immediate or tests it on its
   own, so what it marks is not established and it is named only by its
   value.  record +6 must be 2, the player side (CMP EAX,0x2 at 00014cb2), and
   record +0x26, status_timers[4] the paralysis counter (aitarget.h), must be
   zero (00014cbc). */
#define ADVANCE_ALL_SKIP_FLAGS 0x85
#define ADVANCE_ALL_SIDE 2
#define ADVANCE_ALL_STATUS_PARALYSIS 4

/* fdps_battle_move_unit_toward's side_select, PUSH 0x1 at 00014cf5: the
   acting unit is on the player's side (movegrid.h). */
#define ADVANCE_ALL_SIDE_SELECT 1

/* The "no event is pending" value data_fdps_chapter_pending_event_idx is
   seeded with before each walk and tested against after it, MOV dword ptr
   [0x00069d90],0xff at 00014ceb and CMP dword ptr [0x00069d90],0xff at
   00014d0b (gamedata.h). */
#define NO_PENDING_EVENT 0xff

/* What this function answers when it does not pass the system submenu's
   answer through: MOV dword ptr [EBP-0x4],0x0 at 00014b62, 00014d5d and
   00014e49 (btlmenu.h). */
#define OUTER_MENU_DONE 0

/* 00014ab0.  See btlmenu.h for what the answer means and for what each entry
   does.

   THE CURSOR IS SET TO THE FIRST ENTRY ONCE, IN FRONT OF THE LOOP, MOV dword
   ptr [EBP-0xc],0x0 at 00014ad4 with the loop head at 00014aec.  So a menu
   reopened after a declined prompt, after the options menu or after a
   cancelled system submenu opens on the entry the player last chose -- the
   opposite of fdps_battle_system_submenu, which resets its cursor on every
   pass.

   THE OVERLAY AND THE PLAY-ACTIVE FLAG ARE LOWERED ONCE, ALSO IN FRONT OF THE
   LOOP, and raised again on each way out except one: the confirmed end-turn
   arm raises only the play-active flag (MOV byte ptr [0x00060159],0x1 at
   00014e42) and leaves data_fdps_map_cursor_draw_mode at the 0 stored on
   entry -- or at whatever fdps_battle_advance_turn left in it.

   THE VIEW IS REPAINTED BEFORE THE SELECTION IS ACTED ON, CALL 0x0002beb0 at
   00014b46, which is what takes the retracted ring off the screen, and on the
   cancelled pass as well.

   THE ADVANCE-ALL ARM WORKS FROM THE CURSOR'S TILE AS IT STOOD WHEN THE PROMPT
   WAS ANSWERED.  Both tile coordinates are taken once, by the signed IDIV by
   0x18 at 00014c3a and 00014c52, before the walk; the cursor is then moved
   onto each unit in turn and never put back, so every unit is sent to the
   same tile and the cursor ends on the last unit walked.

   THE PENDING EVENT IS FIRED WITH THE WALKING UNIT'S INDEX, PUSH EAX from
   [EBP-0x14] at 00014d27 in front of CALL dword ptr [EDX+0x601c4], and the
   slot is scaled straight into the table with no bound. */
int fdps_battle_system_menu(void)
{
    /* The two four-int arrays the ring menu is described by, in slot order up,
       left, right, down (menu.h), copied from the templates at 000146e0 and
       000146f0. */
    int menu_icons[OUTER_MENU_SLOTS] = {
        OUTER_ICON_SYSTEM, OUTER_ICON_ADVANCE_ALL,
        OUTER_ICON_OPTIONS, OUTER_ICON_END_TURN
    };
    int menu_disabled[OUTER_MENU_SLOTS] = { 0, 0, 0, 0 };
    /* [EBP-0xc]: the slot the cursor is on, carried from pass to pass. */
    int selected;
    /* [EBP-0x10]: what the cursor loop answered, -1 for a cancel. */
    int cursor_result;
    /* [EBP-0x10] again in the original: the confirmation prompt's answer, 0
       for yes. */
    int prompt_answer;
    /* [EBP-0x8]: what fdps_battle_system_submenu answered. */
    int submenu_result;
    /* [EBP-0x14]: the retraction's radius. */
    int close_radius;
    /* [EBP-0x14] again: the unit the advance-all walk is on, and [EBP-0x28]
       the record it names. */
    int unit_index;
    struct fdps_unit_record *unit;
    /* [EBP-0x1c] and [EBP-0x18]: the tile the map cursor stood on when the
       advance-all prompt was answered, the destination every unit is sent
       toward. */
    int cursor_tile_x;
    int cursor_tile_y;
    /* [EBP-0x24] and [EBP-0x20]: the walking unit's own tile, zero-extended
       from the record's two position bytes. */
    int unit_tile_x;
    int unit_tile_y;

    selected = OUTER_ENTRY_SYSTEM;
    data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
    data_fdps_ui_play_active_flag = PLAY_INACTIVE;

    for (;;) {
        fdps_menu_animate_open(menu_icons, menu_disabled, selected);
        cursor_result = fdps_menu_cursor_input_loop(menu_icons, menu_disabled,
                                                    &selected);
        for (close_radius = CLOSE_SWEEP_FIRST_RADIUS; close_radius > 0;
             close_radius -= CLOSE_SWEEP_STEP) {
            fdps_render_ring_menu_frame(menu_icons, menu_disabled,
                                        close_radius, selected);
        }
        fdps_render_view_frame();

        if (cursor_result == MENU_CURSOR_CANCELLED) {
            data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_NORMAL;
            data_fdps_ui_play_active_flag = PLAY_ACTIVE;
            return OUTER_MENU_DONE;
        }

        if (selected == OUTER_ENTRY_SYSTEM) {
            submenu_result = fdps_battle_system_submenu();
            if (submenu_result != SUBMENU_CANCELLED) {
                data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_NORMAL;
                data_fdps_ui_play_active_flag = PLAY_ACTIVE;
                return submenu_result;
            }
        } else if (selected == OUTER_ENTRY_ADVANCE_ALL) {
            fdps_message_window_open(OUTER_FACE_INDEX);
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           TEXT_ADVANCE_ALL_QUESTION,
                           (unsigned char *) MESSAGE_QUESTION_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
            prompt_answer = fdps_prompt_two_choice();
            if (prompt_answer == PROMPT_ANSWER_YES) {
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               TEXT_ADVANCE_ALL_CONFIRMED,
                               (unsigned char *) MESSAGE_ANSWER_AT,
                               VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                               MESSAGE_TEXT_BG_COLOR,
                               MESSAGE_TEXT_OUTLINE_COLOR);
                delay(OUTER_ANSWER_HOLD_MS);
                fdps_message_window_close();

                cursor_tile_x = data_fdps_map_cursor_world_x / MAP_TILE_SIZE;
                cursor_tile_y = data_fdps_map_cursor_world_y / MAP_TILE_SIZE;
                data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
                data_fdps_ui_play_active_flag = PLAY_INACTIVE;

                for (unit_index = 0; unit_index < data_fdps_map_unit_count;
                     unit_index++) {
                    unit = fdps_get_unit_record(unit_index);
                    if ((unit->flags & ADVANCE_ALL_SKIP_FLAGS) == 0 &&
                        (int) unit->side == ADVANCE_ALL_SIDE &&
                        unit->status_timers[ADVANCE_ALL_STATUS_PARALYSIS]
                            == 0) {
                        unit_tile_x = (int) unit->pos_x;
                        unit_tile_y = (int) unit->pos_y;
                        fdps_map_cursor_move_to(unit_tile_x * MAP_TILE_SIZE,
                                                unit_tile_y * MAP_TILE_SIZE);
                        data_fdps_chapter_pending_event_idx = NO_PENDING_EVENT;
                        fdps_battle_move_unit_toward(cursor_tile_x,
                                                     cursor_tile_y, unit_index,
                                                     ADVANCE_ALL_SIDE_SELECT);
                        if (data_fdps_chapter_pending_event_idx
                                != NO_PENDING_EVENT) {
                            data_fdps_chapter_event_handler_table
                                [data_fdps_chapter_pending_event_idx](
                                    unit_index);
                        }
                        fdps_battle_mark_unit_done(unit_index);
                    }
                }

                fdps_battle_advance_turn();
                data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_NORMAL;
                data_fdps_ui_play_active_flag = PLAY_ACTIVE;
                fdps_units_clear_status_bit7();
                return OUTER_MENU_DONE;
            }
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           TEXT_ADVANCE_ALL_DECLINED,
                           (unsigned char *) MESSAGE_ANSWER_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
            delay(OUTER_ANSWER_HOLD_MS);
            fdps_message_window_close();
        } else if (selected == OUTER_ENTRY_OPTIONS) {
            fdps_options_menu();
        } else if (selected == OUTER_ENTRY_END_TURN) {
            fdps_message_window_open(OUTER_FACE_INDEX);
            fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_END_TURN_QUESTION,
                           (unsigned char *) MESSAGE_QUESTION_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
            prompt_answer = fdps_prompt_two_choice();
            if (prompt_answer == PROMPT_ANSWER_YES) {
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               TEXT_END_TURN_CONFIRMED,
                               (unsigned char *) MESSAGE_END_TURN_ANSWER_AT,
                               VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                               MESSAGE_TEXT_BG_COLOR,
                               MESSAGE_TEXT_OUTLINE_COLOR);
                delay(OUTER_ANSWER_HOLD_MS);
                fdps_message_window_close();
                data_fdps_ui_play_active_flag = PLAY_INACTIVE;
                fdps_battle_advance_turn();
                data_fdps_ui_play_active_flag = PLAY_ACTIVE;
                return OUTER_MENU_DONE;
            }
            fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_END_TURN_DECLINED,
                           (unsigned char *) MESSAGE_END_TURN_ANSWER_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
            delay(OUTER_ANSWER_HOLD_MS);
            fdps_message_window_close();
        }
    }
}

/* 00014ea0.  See btlmenu.h for what the answer means and for what each entry
   does.

   THE TWO AVAILABILITY TESTS ARE BOTH WRITTEN AS "SET WHEN UNAVAILABLE".  The
   descriptor starts all zeros and a 1 is stored into it only on the negative
   case -- access() answering non-zero, which is FDE.SAV ABSENT, and a unit
   that is still in the battle having already acted -- so neither test is
   inverted and neither entry is ever put back once it has been greyed out.

   THE WHOLE UNIT ARRAY IS WALKED EVEN AFTER THE ANSWER IS KNOWN.  The loop has
   no early exit: the first acted unit greys the save entry and every later one
   stores the same 1 over it again.

   THE CURSOR IS PUT BACK ON THE FIRST ENTRY AT THE TOP OF EVERY PASS, MOV
   dword ptr [EBP-0xc],0x0 at 00014f37, which is inside the loop and not in
   front of it.  So a declined quit reopens the menu on the objectives entry
   rather than on the quit entry the player was standing on.

   THE VIEW IS REPAINTED BEFORE THE SELECTION IS ACTED ON, CALL 0x0002beb0 at
   00014f7d, which is what takes the retracted ring off the screen -- the
   closing animation stops three pixels out and erases nothing (menu.h).  The
   repaint happens on the cancelled pass too, before the -1 is returned.

   THE SAVE ARM OPENS THE FILE FOR WRITING BEFORE IT COMPUTES THE CHECKSUM,
   fopen at 000152b0 and CALL 0x00056898 at 000152c4 after it, so the old
   FDE.SAV is already truncated by the time the image is finished.  Neither
   fopen's answer nor fwrite's count is tested on the write path; only the read
   path tests its fopen, and that test is what picks between reading the four
   chapter slots back and stamping them empty. */
int fdps_battle_system_submenu(void)
{
    /* The two four-int arrays the ring menu is described by, in slot order up,
       left, right, down (menu.h).  Both are copied onto the stack from
       templates in the image, at 00014700 and 00014710. */
    int menu_icons[SYSTEM_MENU_SLOTS] = {
        SYSTEM_ICON_OBJECTIVES, SYSTEM_ICON_SAVE,
        SYSTEM_ICON_LOAD, SYSTEM_ICON_QUIT
    };
    int menu_disabled[SYSTEM_MENU_SLOTS] = { 0, 0, 0, 0 };
    /* The slot the cursor is on: 0 at the top of every pass, and afterwards
       whatever the cursor loop left in it. */
    int selected;
    /* What the cursor loop answered -- cancelled, or confirmed on `selected`.
       The original keeps this and the prompt's answer below in one stack slot,
       [EBP-0x14]. */
    int cursor_result;
    /* What the confirmation prompt answered: 0 is yes and everything else
       declines. */
    int prompt_answer;
    /* The unit the availability walk is on, and the record it names. */
    int unit_index;
    struct fdps_unit_record *unit;
    /* The whole FDE.SAV image being rebuilt, and the scalar header inside it.
       The original keeps the header pointer in [EBP-0x1c], the slot the record
       pointer above uses. */
    unsigned char *save_image;
    unsigned char *resume_header;
    /* FDE.SAV, opened first for reading and then again for writing; the
       original reuses one stack slot, [EBP-0x8], for both. */
    FILE *save_fp;

    if (access(SAVE_FILE_NAME, F_OK) != 0) {
        menu_disabled[MENU_ENTRY_LOAD] = MENU_ENTRY_DISABLED;
    }

    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if ((unit->flags & UNIT_FLAG_RETIRED) == 0
                && (unit->flags & UNIT_FLAG_ACTED) != 0) {
            menu_disabled[MENU_ENTRY_SAVE] = MENU_ENTRY_DISABLED;
        }
    }

    for (;;) {
        selected = MENU_ENTRY_OBJECTIVES;
        fdps_menu_animate_open(menu_icons, menu_disabled, selected);
        cursor_result = fdps_menu_cursor_input_loop(menu_icons, menu_disabled,
                                                    &selected);
        fdps_menu_animate_close(menu_icons, menu_disabled, selected);
        fdps_render_view_frame();

        if (cursor_result == MENU_CURSOR_CANCELLED) {
            return SUBMENU_CANCELLED;
        }

        if (selected == MENU_ENTRY_QUIT) {
            fdps_message_window_open(SUBMENU_FACE_INDEX);
            fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_QUIT_QUESTION,
                           (unsigned char *) MESSAGE_QUESTION_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
            prompt_answer = fdps_prompt_two_choice();
            if (prompt_answer == PROMPT_ANSWER_YES) {
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               TEXT_QUIT_CONFIRMED,
                               (unsigned char *) MESSAGE_ANSWER_AT,
                               VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                               MESSAGE_TEXT_BG_COLOR,
                               MESSAGE_TEXT_OUTLINE_COLOR);
                delay(ANSWER_HOLD_MS);
                fdps_message_window_close();
                data_fdps_shared_quit_game_requested = 1;
                return SUBMENU_QUIT_CONFIRMED;
            }
            fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_QUIT_DECLINED,
                           (unsigned char *) MESSAGE_ANSWER_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
            delay(QUIT_DECLINED_HOLD_MS);
            fdps_message_window_close();
            continue;
        }

        if (selected == MENU_ENTRY_OBJECTIVES) {
            fdps_battle_show_win_fail_window();
            return SUBMENU_DONE;
        }

        if (selected == MENU_ENTRY_SAVE) {
            fdps_message_window_open(SUBMENU_FACE_INDEX);
            fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_SAVE_QUESTION,
                           (unsigned char *) MESSAGE_QUESTION_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
            prompt_answer = fdps_prompt_two_choice();
            if (prompt_answer == PROMPT_ANSWER_YES) {
                save_image = (unsigned char *) malloc((size_t)
                                                      SAVE_IMAGE_BYTES);

                /* The four chapter slots are not this function's to write, so
                   the image they will be written back from is the one already
                   on disc.  Nothing tests malloc, and nothing tests fread's
                   count either: a short file is decrypted along with whatever
                   the fresh block held past its end. */
                save_fp = fopen(SAVE_FILE_NAME, SAVE_FILE_READ_MODE);
                if (save_fp != NULL) {
                    fread(save_image, 1, (size_t) SAVE_IMAGE_BYTES, save_fp);
                    fdps_xor_crypt_buffer(save_image,
                                          (unsigned int) SAVE_IMAGE_BYTES);
                    fclose(save_fp);
                } else {
                    ((struct fdps_save_slot *)
                     (save_image + SAVE_IMAGE_SLOTS_AT))[0].chapter_index =
                        SAVE_SLOT_UNWRITTEN_CHAPTER;
                    ((struct fdps_save_slot *)
                     (save_image + SAVE_IMAGE_SLOTS_AT))[1].chapter_index =
                        SAVE_SLOT_UNWRITTEN_CHAPTER;
                    ((struct fdps_save_slot *)
                     (save_image + SAVE_IMAGE_SLOTS_AT))[2].chapter_index =
                        SAVE_SLOT_UNWRITTEN_CHAPTER;
                    ((struct fdps_save_slot *)
                     (save_image + SAVE_IMAGE_SLOTS_AT))[3].chapter_index =
                        SAVE_SLOT_UNWRITTEN_CHAPTER;
                }

                memmove(save_image, data_fdps_tile_event_data_table_ptr,
                        (size_t) SAVE_FIELD_BLOCK_BYTES);
                memmove(save_image + SAVE_ROSTER_AT, data_fdps_roster_array_ptr,
                        (size_t) SAVE_ROSTER_BYTES);
                memmove(save_image + SAVE_UNIT_ARRAY_AT,
                        data_fdps_map_unit_array_ptr,
                        (size_t) (data_fdps_map_unit_count
                                  * MAP_UNIT_RECORD_BYTES));
                memmove(save_image + SAVE_TRIGGERED_FLAGS_AT,
                        data_fdps_map_cell_event_triggered_flags,
                        (size_t) SAVE_TRIGGERED_FLAGS_BYTES);

                resume_header = save_image + SAVE_RESUME_HEADER_AT;
                resume_header[RESUME_TURN_COUNTER] =
                    (unsigned char) data_fdps_battle_turn_counter;
                resume_header[RESUME_UNIT_COUNT] =
                    (unsigned char) data_fdps_map_unit_count;
                resume_header[RESUME_CHAPTER] =
                    (unsigned char) data_fdps_chapter_current_chapter_id;
                resume_header[RESUME_VIEW_ORIGIN_TILE_X] =
                    (unsigned char) (data_fdps_battle_view_window_origin_x
                                     / MAP_TILE_SIZE);
                resume_header[RESUME_VIEW_ORIGIN_TILE_Y] =
                    (unsigned char) (data_fdps_battle_view_window_origin_y
                                     / MAP_TILE_SIZE);
                resume_header[RESUME_CURSOR_TILE_X] =
                    (unsigned char) (data_fdps_map_cursor_world_x
                                     / MAP_TILE_SIZE);
                resume_header[RESUME_CURSOR_TILE_Y] =
                    (unsigned char) (data_fdps_map_cursor_world_y
                                     / MAP_TILE_SIZE);
                resume_header[RESUME_UNUSED_07] = 0;
                resume_header[RESUME_UNUSED_08] = 0;
                resume_header[RESUME_ROSTER_MEMBERS] =
                    (unsigned char) data_fdps_roster_member_count;
                *(int *) (resume_header + RESUME_PARTY_GOLD) =
                    data_fdps_shared_party_total_gold;
                resume_header[RESUME_BATTLE_ANIM] =
                    data_fdps_ui_battle_animation_enabled;
                resume_header[RESUME_TERRAIN_HUD] =
                    data_fdps_ui_terrain_hud_user_enabled;
                resume_header[RESUME_BGM_ENABLED] =
                    data_fdps_audio_bgm_enabled_flag;
                resume_header[RESUME_SFX_ENABLED] =
                    data_fdps_audio_sfx_enabled_flag;

                save_fp = fopen(SAVE_FILE_NAME, SAVE_FILE_WRITE_MODE);
                *(unsigned int *) (save_image + SAVE_CHECKSUM_AT) =
                    fdps_compute_save_checksum(save_image,
                                               (unsigned int)
                                               SAVE_IMAGE_BYTES);
                fdps_xor_crypt_buffer(save_image,
                                      (unsigned int) SAVE_IMAGE_BYTES);
                fwrite(save_image, 1, (size_t) SAVE_IMAGE_BYTES, save_fp);
                fclose(save_fp);
                free(save_image);

                fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_SAVE_DONE,
                               (unsigned char *) MESSAGE_ANSWER_AT,
                               VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                               MESSAGE_TEXT_BG_COLOR,
                               MESSAGE_TEXT_OUTLINE_COLOR);
                delay(ANSWER_HOLD_MS);
            } else {
                fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_SAVE_DECLINED,
                               (unsigned char *) MESSAGE_ANSWER_AT,
                               VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                               MESSAGE_TEXT_BG_COLOR,
                               MESSAGE_TEXT_OUTLINE_COLOR);
                delay(DECLINED_HOLD_MS);
            }
            fdps_message_window_close();
            fdps_flush_keyboard_queue();
            return SUBMENU_DONE;
        }

        if (selected == MENU_ENTRY_LOAD) {
            fdps_message_window_open(SUBMENU_FACE_INDEX);
            fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_LOAD_QUESTION,
                           (unsigned char *) MESSAGE_QUESTION_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
            prompt_answer = fdps_prompt_two_choice();
            if (prompt_answer == PROMPT_ANSWER_YES) {
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               TEXT_LOAD_CONFIRMED,
                               (unsigned char *) MESSAGE_ANSWER_AT,
                               VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                               MESSAGE_TEXT_BG_COLOR,
                               MESSAGE_TEXT_OUTLINE_COLOR);
                delay(ANSWER_HOLD_MS);
                fdps_message_window_close();
                fdps_load_savegame();
            } else {
                fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_LOAD_DECLINED,
                               (unsigned char *) MESSAGE_ANSWER_AT,
                               VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                               MESSAGE_TEXT_BG_COLOR,
                               MESSAGE_TEXT_OUTLINE_COLOR);
                delay(DECLINED_HOLD_MS);
                fdps_message_window_close();
            }
            fdps_flush_keyboard_queue();
            return SUBMENU_DONE;
        }
    }
}
