/* vilmenu.c -- the village's party-member picker and the item loops built on
 * it.
 *
 * See vilmenu.h for what the picker is asked and what it answers.  Nothing
 * here owns a resource: the window frame, the selection bar, the command
 * sprite sheet, the walking-icon cache, the roster array and the message
 * table are all globals the chapter loader filled (gamedata.h), and the only
 * storage this file allocates is the one offscreen page a pass draws into and
 * frees again before it ends.
 */
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "cdaudio.h"
#include "keybd.h"
#include "msgwin.h"
#include "palcycle.h"
#include "sprite.h"
#include "statwin.h"
#include "table.h"
#include "text.h"
#include "unit.h"
#include "unititem.h"
#include "audio.h"
#include "village.h"
#include "vilmenu.h"

/* The grid's offscreen page: 312 bytes to a row, 76 rows, and the 0x5ca0 a
   pass mallocs is exactly that product.  It is rebuilt from nothing every pass
   and freed at the end of it, so no state carries in the pixels. */
#define GRID_PAGE_PITCH 0x138
#define GRID_PAGE_BYTES 0x5ca0

/* The window as it reaches the visible screen: 304 by 67 read from page offset
   0x3ad, which is page (5, 3), and written at 0xa9c48, which is screen
   (8, 125) on the mode 13h framebuffer.  0xa0000 stays a literal because it is
   where the adapter answers and not the address of anything the linker places,
   and the page offsets are literals for the same reason -- they are positions
   inside a block this function allocated. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define GRID_WINDOW_PAGE_AT 0x3ad
#define GRID_WINDOW_SCREEN_AT 0x9c48
#define GRID_WINDOW_W 0x130
#define GRID_WINDOW_H 0x43

#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The six cells: three columns 0x65 apart starting at x 0x0b, two rows 0x1b
   apart starting at y 0x09, filled left to right and top to bottom.  Six is
   also the distance the window top keeps from the cursor, and three -- one
   row -- is the step both the window and the vertical cursor moves take.

   The column and the row come out of ONE signed division of the cell number by
   three, remainder for the column and quotient for the row (MOV EBX,0x3 / SAR
   EDX,0x1f / IDIV EBX twice, at 000338b4 and 000338cd). */
#define GRID_VISIBLE_CELLS 6
#define GRID_COLUMNS 3
#define GRID_ROW_STEP 3
#define GRID_CELL_X_BASE 0x0b
#define GRID_CELL_X_STRIDE 0x65
#define GRID_CELL_Y_BASE 0x09
#define GRID_CELL_Y_STRIDE 0x1b

/* Sprite 0 of ShopWin.Cel is the window frame and sprite 2 of SelBar.Cel is
   the highlight behind the entry the cursor stands on, hung six pixels left of
   the cell and five below its top so the icon sits inside it.  Both go through
   fdps_cel_blit_sprite in its plain opaque mode with no operand. */
#define GRID_WINDOW_SPRITE 0
#define GRID_HIGHLIGHT_SPRITE 2
#define HIGHLIGHT_X_LIFT 6
#define HIGHLIGHT_Y_DROP 5
#define GRID_BLIT_OPERAND 0
#define GRID_BLIT_MODE 0

/* Command.cel sprites 0x2a and 0x2b are the left and right halves of the stand
   the walking icon is posed on, laid 0x10 scanlines below the cell top with
   their halves 0x17 apart and the pair starting two pixels left of the cell.
   fdps_blit_command_sprite puts a fixed 25 by 22 block down with no clipping
   (sprite.h). */
#define STAND_LEFT_SPRITE 0x2a
#define STAND_RIGHT_SPRITE 0x2b
#define STAND_X_LIFT 2
#define STAND_RIGHT_X_OFFSET 0x17
#define STAND_Y_DROP 0x10

/* The walking icon.  The sprite cache holds twelve stream offsets per member,
   four facings of three walk frames, and ITS offset table starts at the base
   rather than at a .CEL file's +0x0f -- it is a table
   fdps_cache_cel_sprite_group builds, not a loaded sheet (mapdraw.c).  The
   slot is the ROSTER INDEX here -- IMUL EAX,dword ptr [EBP-0xc],0xc at
   0003398b -- and NOT the record's own sprite_cache_slot, which is what the
   battle map indexes by.  It lines up because
   fdps_load_field_chapter_resources clears the cache and then caches one group
   per roster member in roster order (00031540), so slot i is member i.

   The frame is (tick / 6) & 3 with the value 3 folded back onto 1, which rocks
   the icon 0, 1, 2, 1 instead of jumping from 2 to 0.  The divide is DIV and
   not IDIV (MOV EBX,0x6 / XOR EDX,EDX / DIV EBX at 0003396a), which is why
   data_fdps_timer_tick_counter is unsigned at its declaration. */
#define MEMBER_SPRITES_PER_CACHE_SLOT 0x0c
#define CEL_SUB_IMAGE_ENTRY_BYTES 4
#define WALK_FRAME_TICKS 6
#define WALK_FRAME_MASK 3
#define WALK_FRAME_FOLD_FROM 3
#define WALK_FRAME_FOLD_TO 1
#define MEMBER_ICON_W 0x18
#define MEMBER_ICON_H 0x18

/* The three dwords of a mode-9 blend descriptor (rleblend.h): the shade ramp
   base, the blend level and the inverse colour cube base.  They are one block
   because the address of the first is what is handed to the kernel, so they
   have to be adjacent and in this order. */
#define BLEND_DESCRIPTOR_RAMP 0
#define BLEND_DESCRIPTOR_LEVEL 1
#define BLEND_DESCRIPTOR_CUBE 2
#define BLEND_DESCRIPTOR_DWORDS 3
#define GHOST_BLEND_LEVEL 10
#define BLIT_MODE_TRANSLUCENT 9

/* The member's name: message entry char_id + 1 of the resident Fdetxt00.txt
   table, drawn 0x1c pixels right of the cell and six scanlines down, in the
   standard glyph colour 0xd0 over no background with outline 0x6d. */
#define NAME_X_OFFSET 0x1c
#define NAME_Y_OFFSET 6
#define NAME_TEXT_ID_BIAS 1
#define NAME_FG_COLOR 0xd0
#define NAME_BG_COLOR 0
#define NAME_OUTLINE_COLOR 0x6d

/* The two scroll arrows: Command.cel sprites 0x44/0x45 for up and 0x46/0x47
   for down, the second of each pair being the lit frame.  Which one is drawn
   is (tick / 5) & 1, so each frame of the blink lasts five timer ticks.

   THE DOWN ARROW IS DRAWN OFF THE END OF THE PAGE.  fdps_blit_command_sprite
   puts a fixed 25 by 22 block down with no clipping (sprite.h) and page offset
   0x4e96 is row 64 of the 76 the page has, so its last ten rows run past the
   0x5ca0 block.  It stays where it is: the screen window only ever shows its
   top six rows, so moving it to fit the page changes the picture
   (rebuild_info/pitfalls.md). */
#define ARROW_BLINK_TICKS 5
#define ARROW_BLINK_MASK 1
#define ARROW_UP_SPRITE 0x44
#define ARROW_DOWN_SPRITE 0x46
#define ARROW_UP_PAGE_AT 0x6ae
#define ARROW_DOWN_PAGE_AT 0x4e96

/* The make codes the grid acts on.  Everything else, the filter's 0xff "no key
   this poll" included, falls through the chain and only costs a frame. */
#define KEY_ESCAPE 0x01
#define KEY_ENTER 0x1c
#define KEY_SPACE 0x39
#define KEY_UP 0x48
#define KEY_LEFT 0x4b
#define KEY_RIGHT 0x4d
#define KEY_DELETE 0x53
#define KEY_DOWN 0x50

/* What the loop's own result slot holds.  The epilogue hands -1 back for
   anything that is not a confirmation, which is the same -1 a cancel put
   here. */
#define GRID_RUNNING 0
#define GRID_CANCELLED (-1)
#define GRID_CONFIRMED 1

/* 法蓮娜 leaves the party at chapter index 0x17, and from there on her entry
   can be stood on but not confirmed, and is drawn ghosted.  THE TWO ARE
   SEPARATE TESTS AND HAVE TO STAY SEPARATE: the refusal is on the CURSOR being
   roster slot 3 (CMP dword ptr [0x000601bc],0x3 at 00033727) and the ghosting
   is on the drawn record's CHARACTER ID being 1 (MOV AL,byte ptr [EAX + 0x8]
   at 000339bd).  Folding them into one condition ties the refusal to whoever
   happens to occupy slot 3 and the ghosting to whoever the cursor is on
   (rebuild_info/pitfalls.md). */
#define MEMBER_LOCKED_FROM_CHAPTER 0x17
#define MEMBER_LOCKED_ROSTER_SLOT 3
#define MEMBER_LOCKED_CHAR_ID 1

/* The sound every accepted cursor move plays.  This function pushes the copy
   of the string at 0x61f4c -- MOV EAX,0x61f4c ahead of all four calls -- which
   is the same literal village.c names. */
#define GRID_MOVE_SFX "Beep.wav"

/* 000336d0.  No arguments and caller-cleaned: all six call sites -- 00032c7f,
   00033fa8, 0003403e, 0003424e, 00034373 and 0003445e -- push nothing, adjust
   ESP by nothing afterwards and take the answer out of EAX.  The prologue is
   the ordinary PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x3c and the body
   reads no slot above EBP.

   NEITHER THE CURSOR NOR THE WINDOW IS SEEDED HERE.  Both are the file globals
   in vilmenu.h and nothing in the image resets them, not even against the
   current member count, so the grid reopens exactly where the last visit left
   it.  Making them locals seeded to 0 loses that (rebuild_info/pitfalls.md).

   THE KEY CHAIN IS SHORT-CIRCUIT AND ITS ORDER IS THE ASSEMBLY'S: cancel,
   confirm, right, left, up, down.  Each arrow arm is one `if (code == k &&
   bound)` whose failure falls into the NEXT code's test -- JNZ and the failed
   bound both land on the same address.  No two codes can be equal so the
   ordering is not observable, but the guards are: a blocked move plays no
   sound and costs nothing but a frame.

   THE VERTICAL MOVES ARE NOT CLAMPED AGAINST THE ROW, ONLY AGAINST THE LIST.
   Up needs a cursor of 3 or more and Down needs three more entries to exist,
   so in a party whose size is not a multiple of three the last entries are
   reachable by Right from the one before them but not by Down from the row
   above.

   THE RECORD IS RESOLVED BEFORE THE CELL IS TESTED AGAINST THE MEMBER COUNT.
   fdps_get_roster_record is called for all six cells, including ones past the
   end of the party (CALL 00023950 at 0003389a sits ahead of the CMP at
   000338a8), so an out-of-range slot forms a pointer that is then never
   dereferenced.  It is address arithmetic on a global and reads nothing
   (table.h), so it is harmless; moving the call inside the test is a different
   program that happens to behave the same, and leaving it where the assembly
   has it costs nothing.

   THE FRAME IS DRAWN AFTER THE KEY IS HANDLED AND BEFORE THE LOOP TEST, so the
   pass that reads a cancel or a confirm still builds, presents and paces a
   whole frame before the function returns.

   THE PACE LATCH IS DELIBERATELY NOT INITIALISED.  Every pass ends by spinning
   until data_fdps_timer_tick_counter differs from `last_tick` and then
   re-latching it, and `last_tick` is never seeded, so the first pass compares
   stack garbage and normally falls straight through the wait.  Seeding it adds
   a tick to the opening of every grid (rebuild_info/pitfalls.md).
   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of that wait: nothing inside the loop writes the counter, so a build
   allowed to hoist the load would spin here forever.

   The values used after a CALL are three.  fdps_read_scancode_auto_repeat's
   EAX is the scancode and goes to [EBP-0x14] at 000336f7.  malloc's EAX is the
   page, MOV dword ptr [EBP-0x24],EAX at 0003384d, used unchecked -- there is
   no test for NULL anywhere -- and freed at the end of the pass.
   fdps_get_roster_record's EAX is the member record, MOV dword ptr
   [EBP-0x2c],EAX at 000338a2, and only its char_id byte at +8 is ever read.
   fdps_draw_text answers a pen position that this caller drops (ADD ESP,0x1c
   at 00033a75 with no use of EAX), and the drawing, CD and palette calls
   return nothing this function looks at. */
int fdps_village_select_member(void)
{
    /* The answer, filled in once the loop has stopped. */
    int selected_member_index;
    /* Whether the loop is still running, and if not, how it stopped. */
    int loop_result;
    /* One poll of the auto-repeat filter: a make code, or 0xff for nothing. */
    unsigned int scancode;
    /* The pass's offscreen page. */
    unsigned char *page;
    /* Which of the six visible cells is being drawn, 0 to 5. */
    int cell_index;
    /* Which roster entry that cell is showing. */
    int entry_index;
    /* That entry's record, resolved for every cell whether or not it is in
       the party. */
    struct fdps_unit_record *member;
    /* The cell's top left corner in the page. */
    int cell_x;
    int cell_y;
    /* 0, 1, 2 or 1: which frame of the member's walk cycle this pass shows. */
    int walk_frame;
    /* That frame's entry in the sprite cache, and the RLE stream it names. */
    int sprite_index;
    unsigned char *icon_stream;
    /* The mode-9 descriptor a departed member's ghosted icon is drawn
       through, rebuilt on every cell that needs it. */
    int ghost_descriptor[BLEND_DESCRIPTOR_DWORDS];
    /* 0 or 1: which frame of the two-frame scroll arrows this pass shows. */
    int arrow_blink_phase;
    /* The tick the previous pass finished on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    loop_result = GRID_RUNNING;

    while (loop_result == GRID_RUNNING) {
        fdps_cd_music_repeat_poll();
        scancode = fdps_read_scancode_auto_repeat();

        if (scancode == KEY_ESCAPE || scancode == KEY_DELETE) {
            loop_result = GRID_CANCELLED;
        } else if (scancode == KEY_ENTER || scancode == KEY_SPACE) {
            if (data_fdps_chapter_current_chapter_id
                    < MEMBER_LOCKED_FROM_CHAPTER
                || data_fdps_village_member_select_cursor_idx
                   != MEMBER_LOCKED_ROSTER_SLOT) {
                loop_result = GRID_CONFIRMED;
            }
        } else if (scancode == KEY_RIGHT
                   && data_fdps_village_member_select_cursor_idx
                      < data_fdps_roster_member_count - 1) {
            data_fdps_village_member_select_cursor_idx++;
            if (data_fdps_village_member_grid_scroll_offset
                + GRID_VISIBLE_CELLS
                <= data_fdps_village_member_select_cursor_idx) {
                data_fdps_village_member_grid_scroll_offset += GRID_ROW_STEP;
            }
            fdps_play_sfx(GRID_MOVE_SFX);
        } else if (scancode == KEY_LEFT
                   && data_fdps_village_member_select_cursor_idx > 0) {
            data_fdps_village_member_select_cursor_idx--;
            if (data_fdps_village_member_select_cursor_idx
                < data_fdps_village_member_grid_scroll_offset) {
                data_fdps_village_member_grid_scroll_offset -= GRID_ROW_STEP;
            }
            fdps_play_sfx(GRID_MOVE_SFX);
        } else if (scancode == KEY_UP
                   && data_fdps_village_member_select_cursor_idx
                      > GRID_ROW_STEP - 1) {
            data_fdps_village_member_select_cursor_idx -= GRID_ROW_STEP;
            if (data_fdps_village_member_select_cursor_idx
                < data_fdps_village_member_grid_scroll_offset) {
                data_fdps_village_member_grid_scroll_offset -= GRID_ROW_STEP;
            }
            fdps_play_sfx(GRID_MOVE_SFX);
        } else if (scancode == KEY_DOWN
                   && data_fdps_village_member_select_cursor_idx
                      < data_fdps_roster_member_count - GRID_ROW_STEP) {
            data_fdps_village_member_select_cursor_idx += GRID_ROW_STEP;
            if (data_fdps_village_member_grid_scroll_offset
                + GRID_VISIBLE_CELLS
                <= data_fdps_village_member_select_cursor_idx) {
                data_fdps_village_member_grid_scroll_offset += GRID_ROW_STEP;
            }
            fdps_play_sfx(GRID_MOVE_SFX);
        }

        page = (unsigned char *) malloc(GRID_PAGE_BYTES);
        fdps_cel_blit_sprite(data_fdps_village_window_sheet_ptr,
                             GRID_WINDOW_SPRITE, page, GRID_PAGE_PITCH,
                             0, 0, GRID_BLIT_OPERAND, GRID_BLIT_MODE);

        for (cell_index = 0; cell_index < GRID_VISIBLE_CELLS; cell_index++) {
            entry_index = data_fdps_village_member_grid_scroll_offset
                          + cell_index;
            member = fdps_get_roster_record(entry_index);

            if (entry_index < data_fdps_roster_member_count) {
                cell_x = (cell_index % GRID_COLUMNS) * GRID_CELL_X_STRIDE
                         + GRID_CELL_X_BASE;
                cell_y = (cell_index / GRID_COLUMNS) * GRID_CELL_Y_STRIDE
                         + GRID_CELL_Y_BASE;

                if (entry_index
                    == data_fdps_village_member_select_cursor_idx) {
                    fdps_cel_blit_sprite(data_fdps_selection_bar_sheet_ptr,
                                         GRID_HIGHLIGHT_SPRITE, page,
                                         GRID_PAGE_PITCH,
                                         cell_x - HIGHLIGHT_X_LIFT,
                                         cell_y + HIGHLIGHT_Y_DROP,
                                         GRID_BLIT_OPERAND, GRID_BLIT_MODE);
                }

                fdps_blit_command_sprite(page
                                             + (cell_y + STAND_Y_DROP)
                                               * GRID_PAGE_PITCH
                                             + cell_x - STAND_X_LIFT,
                                         GRID_PAGE_PITCH, STAND_LEFT_SPRITE);
                fdps_blit_command_sprite(page
                                             + (cell_y + STAND_Y_DROP)
                                               * GRID_PAGE_PITCH
                                             + cell_x + STAND_RIGHT_X_OFFSET,
                                         GRID_PAGE_PITCH, STAND_RIGHT_SPRITE);

                walk_frame = (int) ((data_fdps_timer_tick_counter
                                     / WALK_FRAME_TICKS) & WALK_FRAME_MASK);
                if (walk_frame == WALK_FRAME_FOLD_FROM) {
                    walk_frame = WALK_FRAME_FOLD_TO;
                }
                sprite_index = entry_index * MEMBER_SPRITES_PER_CACHE_SLOT
                               + walk_frame;
                icon_stream = data_fdps_cel_sprite_cache_ptr
                    + *(int *) (data_fdps_cel_sprite_cache_ptr
                                + sprite_index * CEL_SUB_IMAGE_ENTRY_BYTES);

                if (data_fdps_chapter_current_chapter_id
                        < MEMBER_LOCKED_FROM_CHAPTER
                    || member->char_id != MEMBER_LOCKED_CHAR_ID) {
                    fdps_blit_dispatch(icon_stream,
                                       page + cell_y * GRID_PAGE_PITCH
                                           + cell_x,
                                       MEMBER_ICON_W, MEMBER_ICON_H,
                                       GRID_PAGE_PITCH, GRID_BLIT_OPERAND,
                                       GRID_BLIT_MODE);
                } else {
                    ghost_descriptor[BLEND_DESCRIPTOR_RAMP] =
                        (int) data_fdps_palette_shade_ramp_table;
                    ghost_descriptor[BLEND_DESCRIPTOR_LEVEL] =
                        GHOST_BLEND_LEVEL;
                    ghost_descriptor[BLEND_DESCRIPTOR_CUBE] =
                        (int) data_fdps_inverse_palette_cube;
                    fdps_blit_dispatch(icon_stream,
                                       page + cell_y * GRID_PAGE_PITCH
                                           + cell_x,
                                       MEMBER_ICON_W, MEMBER_ICON_H,
                                       GRID_PAGE_PITCH,
                                       (unsigned int) ghost_descriptor,
                                       BLIT_MODE_TRANSLUCENT);
                }

                fdps_draw_text(data_fdps_all_game_text_ptr,
                               member->char_id + NAME_TEXT_ID_BIAS,
                               page + (cell_y + NAME_Y_OFFSET)
                                   * GRID_PAGE_PITCH
                                   + cell_x + NAME_X_OFFSET,
                               GRID_PAGE_PITCH, NAME_FG_COLOR, NAME_BG_COLOR,
                               NAME_OUTLINE_COLOR);
            }
        }

        arrow_blink_phase = (int) ((data_fdps_timer_tick_counter
                                    / ARROW_BLINK_TICKS) & ARROW_BLINK_MASK);

        if (data_fdps_village_member_grid_scroll_offset != 0) {
            fdps_blit_command_sprite(page + ARROW_UP_PAGE_AT, GRID_PAGE_PITCH,
                                     arrow_blink_phase + ARROW_UP_SPRITE);
        }

        if (data_fdps_village_member_grid_scroll_offset + GRID_VISIBLE_CELLS
            < data_fdps_roster_member_count) {
            fdps_blit_command_sprite(page + ARROW_DOWN_PAGE_AT,
                                     GRID_PAGE_PITCH,
                                     arrow_blink_phase + ARROW_DOWN_SPRITE);
        }

        fdps_cycle_ui_palette();
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the present starts clear of it. */
        }
        fdps_blit_rect((unsigned int) (page + GRID_WINDOW_PAGE_AT),
                       GRID_PAGE_PITCH,
                       (void *) (VGA_SCREEN_BASE + GRID_WINDOW_SCREEN_AT),
                       VGA_SCREEN_PITCH, GRID_WINDOW_W, GRID_WINDOW_H);
        free(page);

        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    if (loop_result > GRID_RUNNING) {
        selected_member_index = data_fdps_village_member_select_cursor_idx;
    } else {
        selected_member_index = GRID_CANCELLED;
    }

    return selected_member_index;
}

/* ------------------------------------------------------------------
 * fdps_village_member_status_loop @ 00033f80
 * ------------------------------------------------------------------ */

/* The whole mode 13h frame, PUSH 0xfa00 as memmove's count at 00033fc8 over a
   destination of PUSH 0xa0000.  0xa0000 is where the adapter answers and not
   the address of anything the linker places, so it stays the literal
   VGA_SCREEN_BASE already defined above. */
#define VGA_SCREEN_BYTES 0xfa00

/* fdps_village_animate_window_zoom's second argument, which is only tested
   against zero (CMP byte ptr [EBP+0x18],0x0 at 000325de): the sweep runs open
   before the picker and shut after it.  XOR EAX,EAX / PUSH EAX at 00033f99 and
   MOV EAX,0x1 / PUSH EAX at 00033fb0. */
#define WINDOW_ZOOM_OPEN 0
#define WINDOW_ZOOM_CLOSE 1

/* 00033f80.  One stack argument, caller-cleaned: all three call sites --
   fdps_village_item_menu at 00035a00, fdps_run_church_screen at 00035c27 and
   fdps_run_secret_menu at 000363c5 -- push their own page and follow the CALL
   with ADD ESP,0x4, and the body reads it at [EBP+0x14] behind PUSH
   EBX/ESI/EDI/EBP and the return address.  RET carries no immediate, EAX is
   never set before the epilogue and none of the three call sites looks at it.

   The control flow is one loop with one test in it.  The entry test CMP dword
   ptr [EBP-0x4],-0x1 / JZ at 00033f93 is the top of the loop and the JMP at
   00033fea is its back edge, so this is a while whose condition is checked
   before the first pass -- which is why the slot is seeded with zero at
   00033f8c rather than with a member index: zero is simply the value that is
   not -1.  The second test at 00033fc2 is the if, and its taken arm jumps to
   the back edge rather than out.  Cancelling in the picker is the only way to
   reach the epilogue.

   The one value used after a CALL is fdps_village_select_member's EAX, stored
   to the loop's slot by MOV dword ptr [EBP-0x4],EAX at 00033fad and then
   compared and passed on from there.  The two zoom calls, memmove and
   fdps_battle_show_unit_status_window are each followed straight by their
   stack cleanup with EAX untouched, so nothing they answer is read here -- in
   particular this function never learns whether a status window was actually
   drawn.

   THE INDEX IS FED TO THE BATTLE UNIT ARRAY AND NOT TO THE ROSTER.
   fdps_village_select_member answers an index into the roster
   (data_fdps_roster_array_ptr, bounded by data_fdps_roster_member_count) and
   fdps_battle_show_unit_status_window resolves it through fdps_get_unit_record
   against data_fdps_map_unit_array_ptr.  The two agree only because
   fdps_load_field_chapter_resources points data_fdps_map_unit_array_ptr at the
   roster base for the whole field and village phase, and every caller of this
   function is reached from fdps_run_village_phase behind that assignment.
   Resolving the pick here instead -- through fdps_get_roster_record, or
   through a village-local array -- shows a different member
   (rebuild_info/pitfalls.md).

   THE BACKDROP COPY IS NOT A REDRAW AND CANNOT BE DROPPED.
   fdps_battle_show_unit_status_window snapshots the live screen on the way in
   and paints that snapshot back on the way out (statwin.h), so what this
   memmove puts on the adapter is what the player is left looking at once the
   window closes.  Leaving it out because the picture on screen already looks
   right makes the window save the closing zoom's last frame and restore that
   instead. */
void fdps_village_member_status_loop(unsigned char *screen_page)
{
    /* Which party member the player last confirmed in the picker, and the
       loop's only exit: -1 is the picker's cancel and nothing else stops
       this. */
    int picked_member_index;

    picked_member_index = 0;

    while (picked_member_index != GRID_CANCELLED) {
        fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
        picked_member_index = fdps_village_select_member();
        fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);

        if (picked_member_index != GRID_CANCELLED) {
            memmove((void *) VGA_SCREEN_BASE, screen_page,
                    (size_t) VGA_SCREEN_BYTES);
            fdps_battle_show_unit_status_window(picked_member_index);
        }
    }
}

/* ------------------------------------------------------------------
 * fdps_village_item_sell_loop @ 00034000
 * ------------------------------------------------------------------ */

/* Where both sell messages are written: 0xaa3d4, which is column 20, row 131
   of the mode 13h screen, inside the window frame the zoom has just opened.
   And where the gold readout is repainted: 0xa8208, column 8, row 104, the one
   place all seven village and shop screens draw it (village.h).  Both are
   literals on top of VGA_SCREEN_BASE for the reason that base is one -- they
   are positions on the adapter and not the addresses of anything the linker
   places. */
#define SELL_MESSAGE_SCREEN_AT 0xa3d4
#define GOLD_READOUT_SCREEN_AT 0x8208

/* The two message entries this screen draws.  0x1fb is the refusal a member
   with an empty bag gets, and prints one substitution -- the member's name;
   0x1fc is the offer, and prints both -- the item's name and the figure. */
#define SELL_REFUSED_TEXT_ID 0x1fb
#define SELL_OFFER_TEXT_ID 0x1fc

/* Where the name substitution's contents come from.  Item names live at
   message entry 0xc9 + item id; character names live at 1 + character id,
   which is the NAME_TEXT_ID_BIAS the grid above already draws with -- it is
   one table and one bias, not two that happen to agree. */
#define ITEM_TEXT_ID_BIAS 0xc9

/* The colours both messages are drawn in: the same standard glyph, background
   and outline trio the grid's member names use. */
#define SELL_TEXT_FG_COLOR 0xd0
#define SELL_TEXT_BG_COLOR 0
#define SELL_TEXT_OUTLINE_COLOR 0x6d

/* Three quarters of the listed price, truncated.  THE MULTIPLICATION IS SIGNED
   AND THE DIVISION IS TOO, over a price that is not: the field at +0x13 of the
   ITEM.DAT record is an unsigned 16-bit price, zero-extended (MOV DX,word ptr
   [EDX+0x13] / AND EDX,0xffff at 00034161), and what is then divided is the
   int the promotion produced -- LEA EDX,[EDX+EDX*0x2] for the times three and
   SAR EDX,0x1f / SHL EDX,0x2 / SBB EAX,EDX / SAR EAX,0x2 at 00034170 for the
   divide by four, which is the round-toward-zero idiom and not a plain shift.
   The value can never be negative here, so the two agree on every input; the
   division is written as a division because that is what the assembly does. */
#define SELL_PRICE_NUMERATOR 3
#define SELL_PRICE_DENOMINATOR 4

/* fdps_unit_item_select_window's second argument: 0 lists every entry rather
   than only the ones with a use effect (unititem.h). */
#define SELL_LIST_EVERY_ENTRY 0

/* What the inventory list answers when the player backs out of it, and the one
   answer of the yes/no prompt that sells -- the left cell, which is also the
   one highlighted on entry (msgwin.h). */
#define ITEM_LIST_CANCELLED (-1)
#define PROMPT_ANSWER_YES 0

/* The sound a settled sale plays, MOV EAX,0x61f58 in front of the call.  It is
   the same literal shop.c names for a settled payment. */
#define SELL_PAYMENT_SFX "Incom.wav"

/* How long the screen holds after a settled sale, PUSH 0xc8 at 000341ea: 200
   milliseconds with the gold readout already repainted, so the player sees the
   new purse before the picker comes back. */
#define SELL_PAYMENT_HOLD_MS 200

/* 00034000.  One stack argument, caller-cleaned: all three call sites --
   fdps_village_item_menu at 000359e4, fdps_run_weapon_shop at 00036157 and
   fdps_run_secret_menu at 0003639b -- push their own page and follow the CALL
   with ADD ESP,0x4, and the body reads it at [EBP+0x14] behind PUSH
   EBX/ESI/EDI/EBP and the return address.  RET carries no immediate, EAX is
   never set before the epilogue and none of the three call sites looks at it.

   The control flow is one loop with three tests nested inside it.  The entry
   test CMP dword ptr [EBP-0x14],-0x1 / JZ at 00034025 is the top of the loop
   and the JMP at 00034203 is its back edge, so this is a while whose condition
   is checked before the first pass -- which is why the slot is seeded with
   zero at 0003400c rather than with a member index: zero is simply the value
   that is not -1.  Every arm inside, taken or not, falls to that same back
   edge, so cancelling in the picker is the only way to reach the epilogue.

   THE INNER `picked_member_index != GRID_CANCELLED` IS ALREADY TRUE AND IT IS
   STILL THERE.  CMP dword ptr [EBP-0x14],-0x1 / JZ at 000340e9 sits between
   the slot seed and the call to the inventory list, inside an arm the outer
   test at 0003404a has already established the index is not -1 in.  At -od
   nothing folds it away, and the short-circuit shape is visible in the
   assembly -- the failed compare and the list's own -1 land on the same
   address.  It is written out because that is the program; it costs one
   compare per sale and decides nothing.

   THE SLOT IS SEEDED TO ZERO BEFORE EVERY VISIT TO THE LIST, at 000340e2, and
   the list's cursor starts from what the caller left there (unititem.h), so
   the bag always opens on the first entry however the last visit ended.

   THE OFFER IS PUBLISHED BEFORE THE PROMPT AND READ BACK OUT OF THE GLOBAL
   AFTERWARDS.  MOV [0x00064038],EAX at 0003417b writes it and MOV
   EAX,[0x00064038] / ADD dword ptr [0x000643a4],EAX at 000341cd adds that same
   slot to the purse, so the figure the confirmation message printed and the
   figure the player is paid are one value and not two computations of it.
   Recomputing the price at the payment instead would be a different program
   the moment anything between the two touched the slot.

   THE STATS ARE REWORKED AFTER THE SALE AND ONLY AFTER A SALE.  What was sold
   may have been equipped -- fdps_unit_remove_item unequips it (unititem.h) --
   so fdps_unit_recompute_combat_stats closes the arm; nothing recomputes on
   the declined or cancelled paths because nothing changed on them.

   THE BACKDROP COPY IS NOT A REDRAW AND CANNOT BE DROPPED.  The inventory
   window snapshots the live screen on the way in and paints that snapshot back
   on the way out (unititem.h), so what this memmove puts on the adapter is
   what the player is left looking at once the list closes.

   The values used after a CALL are five.  fdps_village_select_member's EAX is
   the roster index, stored to the loop's slot by MOV dword ptr [EBP-0x14],EAX
   at 00034043 and compared and passed on from there.  fdps_unit_item_count's
   EAX is tested where it stands -- TEST EAX,EAX / JNZ at 0003406e with no
   store -- so the count is a branch and never a value.  fdps_get_unit_record's
   EAX is the member record, MOV dword ptr [EBP-0x8],EAX at 0003408d, and only
   its char_id byte at +8 is ever read.  fdps_unit_item_select_window's EAX is
   likewise compared where it stands, CMP EAX,-0x1 / JZ at 00034105, and the
   slot it chose comes back through the pointer instead.
   fdps_unit_get_item_id's EAX is the item id, MOV dword ptr [EBP-0xc],EAX at
   0003413f, used for the message entry and for the record lookup;
   fdps_get_item_record's EAX is that record, MOV dword ptr [EBP-0x4],EAX at
   0003415b, read only for its price.  fdps_prompt_two_choice's EAX is tested
   in place, TEST EAX,EAX / JNZ at 000341ab.  fdps_draw_text answers a pen
   position that this caller drops (ADD ESP,0x1c at 000341a3 with no use of
   EAX), and the zoom, the gold readout, memmove, the removal, the sound, the
   delay and the stat recomputation return nothing this function looks at. */
void fdps_village_item_sell_loop(unsigned char *screen_page)
{
    /* Which party member the player last confirmed in the picker, and the
       loop's only exit: -1 is the picker's cancel and nothing else stops
       this. */
    int picked_member_index;
    /* Which of that member's eight inventory entries the list came back on.
       Seeded to zero before every visit, which is where the list's cursor
       starts. */
    int picked_slot;
    /* The ITEM.DAT id of the entry standing in that slot. */
    int item_id;
    /* The member's own record, taken only on the refusal path and read only
       for its character id. */
    struct fdps_unit_record *member;
    /* The item's own record, read only for its price. */
    struct fdps_item_effect *item;

    picked_member_index = 0;
    fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);

    while (picked_member_index != GRID_CANCELLED) {
        fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
        picked_member_index = fdps_village_select_member();

        if (picked_member_index != GRID_CANCELLED) {
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);

            if (fdps_unit_item_count(picked_member_index) == 0) {
                fdps_village_animate_window_zoom(screen_page,
                                                 WINDOW_ZOOM_OPEN);
                member = fdps_get_unit_record(picked_member_index);
                data_fdps_dialog_last_action_text_id_param =
                    member->char_id + NAME_TEXT_ID_BIAS;
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               SELL_REFUSED_TEXT_ID,
                               (unsigned char *) (VGA_SCREEN_BASE
                                                  + SELL_MESSAGE_SCREEN_AT),
                               VGA_SCREEN_PITCH, SELL_TEXT_FG_COLOR,
                               SELL_TEXT_BG_COLOR, SELL_TEXT_OUTLINE_COLOR);
            } else {
                memmove((void *) VGA_SCREEN_BASE, screen_page,
                        (size_t) VGA_SCREEN_BYTES);
                picked_slot = 0;

                if (picked_member_index != GRID_CANCELLED
                    && fdps_unit_item_select_window(picked_member_index,
                                                    SELL_LIST_EVERY_ENTRY,
                                                    &picked_slot)
                       != ITEM_LIST_CANCELLED) {
                    fdps_village_animate_window_zoom(screen_page,
                                                     WINDOW_ZOOM_OPEN);
                    fdps_draw_party_gold((unsigned char *)
                                             (VGA_SCREEN_BASE
                                              + GOLD_READOUT_SCREEN_AT),
                                         VGA_SCREEN_PITCH);
                    item_id = fdps_unit_get_item_id(picked_member_index,
                                                    picked_slot);
                    data_fdps_dialog_last_action_text_id_param =
                        item_id + ITEM_TEXT_ID_BIAS;
                    item = fdps_get_item_record(item_id);
                    data_fdps_dialog_last_action_value_param =
                        item->price * SELL_PRICE_NUMERATOR
                        / SELL_PRICE_DENOMINATOR;
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   SELL_OFFER_TEXT_ID,
                                   (unsigned char *)
                                       (VGA_SCREEN_BASE
                                        + SELL_MESSAGE_SCREEN_AT),
                                   VGA_SCREEN_PITCH, SELL_TEXT_FG_COLOR,
                                   SELL_TEXT_BG_COLOR,
                                   SELL_TEXT_OUTLINE_COLOR);

                    if (fdps_prompt_two_choice() == PROMPT_ANSWER_YES) {
                        fdps_unit_remove_item(picked_member_index,
                                              picked_slot);
                        fdps_play_sfx(SELL_PAYMENT_SFX);
                        data_fdps_shared_party_total_gold =
                            data_fdps_shared_party_total_gold
                            + data_fdps_dialog_last_action_value_param;
                        fdps_draw_party_gold((unsigned char *)
                                                 (VGA_SCREEN_BASE
                                                  + GOLD_READOUT_SCREEN_AT),
                                             VGA_SCREEN_PITCH);
                        delay(SELL_PAYMENT_HOLD_MS);
                        fdps_unit_recompute_combat_stats(picked_member_index);
                    }
                }
            }
        }
    }
}

/* The village text box all three of the hand-over messages are painted into,
   PUSH 0xaa3d4 at 000342bf, 0003435b and 000343c5: the mode 13h framebuffer
   plus row 131, column 20.  The box and the colour trio are the same ones the
   sell counter above draws its two messages in; they are stated again here
   because these are this function's own literals and not a constant it
   borrows from the counter. */
#define TRANSFER_MESSAGE_SCREEN_AT 0xa3d4
#define TRANSFER_TEXT_FG_COLOR 0xd0
#define TRANSFER_TEXT_BG_COLOR 0
#define TRANSFER_TEXT_OUTLINE_COLOR 0x6d

/* The three entries of data_fdps_all_game_text_ptr this loop can draw.  0x1fb
   is the refusal a giver with an empty bag gets and is the same entry the sell
   counter refuses with; 0x1fd asks who the item is for and is the only message
   with two substitutions in it, the giver's name and the item's; 0x1fa is the
   receiver's bag being full. */
#define TRANSFER_EMPTY_BAG_TEXT_ID 0x1fb
#define TRANSFER_ASK_RECEIVER_TEXT_ID 0x1fd
#define TRANSFER_BAG_FULL_TEXT_ID 0x1fa

/* fdps_unit_item_select_window's second argument: 0 lists every entry rather
   than only the ones with a use effect (unititem.h). */
#define TRANSFER_LIST_EVERY_ENTRY 0

/* CMP EAX,0x8 at 00034391.  The receiver is refused on an EXACT eight, which
   is the count a bag with all eight entries occupied answers -- the test is an
   equality and not a >=, so a bag that somehow held more would be handed the
   item rather than refused. */
#define INVENTORY_SLOTS_FULL 8

/* 00034210.  One stack argument, caller-cleaned: the body reads the page at
   [EBP+0x14] behind PUSH EBX/ESI/EDI/EBP and the return address, RET carries
   no immediate, and every call site -- fdps_village_item_menu, the church, the
   weapon shop and the secret menu, each pushing the page it blitted its own
   backdrop into -- follows the CALL with ADD ESP,0x4.  EAX is never set before
   the epilogue and no caller looks at it.

   The control flow is one loop with five tests nested inside it.  The entry
   test CMP dword ptr [EBP-0x14],-0x1 / JZ at 00034235 is the top of the loop
   and the JMP at 0003440b is its back edge, so the condition is checked before
   the first pass -- which is why the giver slot is seeded with zero at
   0003421c rather than with a member index: zero is simply a value that is not
   -1.  Every arm inside, taken or not, falls to that same back edge, so
   cancelling in the member picker is the only way to reach the epilogue, and
   the window is left open on the way out for the caller to close.

   THE INNER `picked_giver_index != GRID_CANCELLED` IS ALREADY TRUE AND IT IS
   STILL THERE.  CMP dword ptr [EBP-0x14],-0x1 / JZ at 000342f9 sits between
   the slot seed and the call to the inventory list, inside an arm the outer
   test at 00034256 has already established the index is not -1 in.  At -od
   nothing folds it away, and the short-circuit shape is visible in the
   assembly -- the failed compare and the list's own -1 land on the same
   address.  It is written out because that is the program; it costs one
   compare per hand-over and decides nothing.  The sell counter above carries
   the identical dead test in the identical place.

   THE GIVER'S NAME IS PUBLISHED BEFORE THE BAG IS COUNTED, at 0003427b, on
   every confirmed pick and not only on the refusal.  The record is fetched at
   00034264 the moment the picker answers and byte +8 plus one goes into
   data_fdps_dialog_last_action_text_id_param before the count at 00034296
   chooses an arm, so a pass that goes on to a real hand-over has already left
   the giver's name in that slot -- which is what message 0x1fd's -4 code then
   expands.  Publishing it inside the empty-bag arm instead, the way the sell
   counter does, would leave 0x1fd naming whoever the previous message named.

   THE WINDOW IS CLOSED AND REOPENED TO WIPE THE PICKER GRID OFF IT.  The zoom
   with 1 at 0003428a and the one with 0 that opens the next message are a
   pair: the grid is drawn into the open window, so a message can only be shown
   on a clean one.  The full-bag arm at 00034396 does the same pair a second
   time for exactly that reason, closing a window that is already open because
   the receiver's picker has just drawn its grid into it.

   THE BACKDROP COPY IS NOT A REDRAW AND CANNOT BE DROPPED.  The inventory
   window snapshots the live screen on the way in and paints that snapshot back
   on the way out (unititem.h), so what this memmove puts on the adapter is
   what the player is left looking at once the list closes.

   THE ITEM CROSSES AS A BARE ID.  fdps_unit_get_item_id reads the id out of
   the giver's entry and fdps_unit_add_item stores that id into the receiver's
   first free entry with a zeroed flag byte (unititem.h), so an equipped item
   arrives unequipped.  Moving the giver's two-byte entry across instead would
   carry the equipped bit 0x40 with it, and since only the giver's stats are
   reworked the receiver would gain the item's modifiers without ever wearing
   it.

   ONLY THE GIVER'S STATS ARE REWORKED.  fdps_unit_recompute_combat_stats runs
   on the giver at 000343f3 because what left the bag may have been equipped;
   nothing recomputes the receiver, and nothing recomputes on any of the four
   arms that move no item.

   The values used after a CALL are five.  fdps_village_select_member's EAX is
   a roster index, stored to the loop's giver slot by MOV dword ptr
   [EBP-0x14],EAX at 00034253 on the first call and to the receiver slot by MOV
   dword ptr [EBP-0x8],EAX at 00034378 on the second.  fdps_get_unit_record's
   EAX is the giver's record, MOV dword ptr [EBP-0x4],EAX at 0003426c, and only
   its char_id byte at +8 is ever read -- MOV AL,byte ptr [EAX+0x8] / AND
   EAX,0xff, an unsigned byte.  fdps_unit_item_count's EAX is tested where it
   stands, TEST EAX,EAX / JNZ at 0003429e for the giver and CMP EAX,0x8 / JNZ
   at 00034391 for the receiver, so the count is a branch and never a value.
   fdps_unit_item_select_window's EAX is likewise compared where it stands, CMP
   EAX,-0x1 / JZ at 00034315, and the slot it chose comes back through the
   pointer at [EBP-0x10] instead.  fdps_unit_get_item_id's EAX is the item id,
   MOV dword ptr [EBP-0xc],EAX at 0003432e, used for the message substitution
   and pushed whole to fdps_unit_add_item at 000343fb.  fdps_draw_text answers a
   pen position that this caller drops -- ADD ESP,0x1c with no use of EAX at
   000342d4, 00034370 and 000343da -- and the zoom, memmove, the removal, the
   stat recomputation and the addition return nothing this function looks
   at. */
void fdps_village_item_transfer_loop(unsigned char *screen_page)
{
    /* Which party member the player last confirmed as the giver, and the
       loop's only exit: -1 is the picker's cancel and nothing else stops
       this.  It survives a completed hand-over, so the next pass simply asks
       for another giver. */
    int picked_giver_index;
    /* Which of the giver's eight inventory entries the list came back on.
       Seeded to zero before the visit, which is where the list's cursor
       starts. */
    int picked_slot;
    /* The ITEM.DAT id standing in that entry, read once and used both for the
       message substitution and for the entry the receiver is given. */
    int item_id;
    /* Which party member the player confirmed as the receiver. */
    int picked_receiver_index;
    /* The giver's own record, read only for its character id. */
    struct fdps_unit_record *giver;

    picked_giver_index = 0;
    fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);

    while (picked_giver_index != GRID_CANCELLED) {
        fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
        picked_giver_index = fdps_village_select_member();

        if (picked_giver_index != GRID_CANCELLED) {
            giver = fdps_get_unit_record(picked_giver_index);
            data_fdps_dialog_last_action_text_id_param =
                giver->char_id + NAME_TEXT_ID_BIAS;
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);

            if (fdps_unit_item_count(picked_giver_index) == 0) {
                fdps_village_animate_window_zoom(screen_page,
                                                 WINDOW_ZOOM_OPEN);
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               TRANSFER_EMPTY_BAG_TEXT_ID,
                               (unsigned char *) (VGA_SCREEN_BASE
                                                  + TRANSFER_MESSAGE_SCREEN_AT),
                               VGA_SCREEN_PITCH, TRANSFER_TEXT_FG_COLOR,
                               TRANSFER_TEXT_BG_COLOR,
                               TRANSFER_TEXT_OUTLINE_COLOR);
            } else {
                memmove((void *) VGA_SCREEN_BASE, screen_page,
                        (size_t) VGA_SCREEN_BYTES);
                picked_slot = 0;

                if (picked_giver_index != GRID_CANCELLED
                    && fdps_unit_item_select_window(picked_giver_index,
                                                    TRANSFER_LIST_EVERY_ENTRY,
                                                    &picked_slot)
                       != ITEM_LIST_CANCELLED) {
                    item_id = fdps_unit_get_item_id(picked_giver_index,
                                                    picked_slot);
                    data_fdps_dialog_subst_text_id_2 =
                        item_id + ITEM_TEXT_ID_BIAS;
                    fdps_village_animate_window_zoom(screen_page,
                                                     WINDOW_ZOOM_OPEN);
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   TRANSFER_ASK_RECEIVER_TEXT_ID,
                                   (unsigned char *)
                                       (VGA_SCREEN_BASE
                                        + TRANSFER_MESSAGE_SCREEN_AT),
                                   VGA_SCREEN_PITCH, TRANSFER_TEXT_FG_COLOR,
                                   TRANSFER_TEXT_BG_COLOR,
                                   TRANSFER_TEXT_OUTLINE_COLOR);
                    picked_receiver_index = fdps_village_select_member();

                    if (picked_receiver_index != GRID_CANCELLED) {
                        if (fdps_unit_item_count(picked_receiver_index)
                            == INVENTORY_SLOTS_FULL) {
                            fdps_village_animate_window_zoom(screen_page,
                                                             WINDOW_ZOOM_CLOSE);
                            fdps_village_animate_window_zoom(screen_page,
                                                             WINDOW_ZOOM_OPEN);
                            fdps_draw_text(data_fdps_all_game_text_ptr,
                                           TRANSFER_BAG_FULL_TEXT_ID,
                                           (unsigned char *)
                                               (VGA_SCREEN_BASE
                                                + TRANSFER_MESSAGE_SCREEN_AT),
                                           VGA_SCREEN_PITCH,
                                           TRANSFER_TEXT_FG_COLOR,
                                           TRANSFER_TEXT_BG_COLOR,
                                           TRANSFER_TEXT_OUTLINE_COLOR);
                        } else {
                            fdps_unit_remove_item(picked_giver_index,
                                                  picked_slot);
                            fdps_unit_recompute_combat_stats(
                                picked_giver_index);
                            fdps_unit_add_item(picked_receiver_index, item_id);
                        }
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------
 * fdps_village_member_equip_loop @ 00034420
 * ------------------------------------------------------------------ */

/* Where the refusal is written: 0xaa3d4, column 20, row 131 of the mode 13h
   screen, inside the window frame the zoom has just opened.  It is the same
   box and the same colour trio the sell counter and the hand-over above draw
   their messages in; they are stated again because these are this function's
   own literals -- PUSH 0xaa3d4 at 000344bd, PUSH 0x140, 0xd0, 0 and 0x6d at
   000344b8 to 000344af -- and not constants it borrows from either of them. */
#define EQUIP_MESSAGE_SCREEN_AT 0xa3d4
#define EQUIP_TEXT_FG_COLOR 0xd0
#define EQUIP_TEXT_BG_COLOR 0
#define EQUIP_TEXT_OUTLINE_COLOR 0x6d

/* The one entry this loop can draw, PUSH 0x1fb at 000344c2: the refusal a
   member with an empty bag gets, which is the same entry the sell counter and
   the hand-over refuse with.  It prints one substitution, the member's name,
   and ends in the page-break token, so fdps_draw_text does not come back until
   a key is pressed (text.h). */
#define EQUIP_EMPTY_BAG_TEXT_ID 0x1fb

/* 00034420.  One stack argument, caller-cleaned: both call sites --
   fdps_run_weapon_shop at 00036173 and fdps_run_secret_menu at 000363b7 --
   push the page they blitted their own backdrop into and follow the CALL with
   ADD ESP,0x4, and the body reads it at [EBP+0x14] behind PUSH
   EBX/ESI/EDI/EBP and the return address.  RET carries no immediate, EAX is
   never set before the epilogue and neither call site looks at it.

   The control flow is one loop with two tests nested inside it.  The entry
   test CMP dword ptr [EBP-0x8],-0x1 / JZ at 00034445 is the top of the loop
   and the JMP at 0003450b is its back edge, so the condition is checked before
   the first pass -- which is why the slot is seeded with zero at 0003442c
   rather than with a member index: zero is simply a value that is not -1.
   Both arms of the count test, taken or not, fall to that same back edge, so
   cancelling in the picker is the only way to reach the epilogue -- and the
   pick that cancels reaches the epilogue through the loop test rather than
   directly, which is what leaves the window open on the way out for the caller
   to close or to reopen.

   THE TWO ARMS RUN THE ANIMATION IN OPPOSITE DIRECTIONS AND THAT IS NOT A
   MISTAKE.  XOR EAX,EAX / PUSH EAX at 00034480 opens a window that the picker
   has just drawn its grid into and is therefore already open; MOV EAX,0x1 /
   PUSH EAX at 000344d7 closes it.  The open animation rebuilds every step from
   screen_page (village.h), so replaying it is how the grid is wiped off the
   frame before the message is written into it -- the sell counter and the
   hand-over reach the same clean window by closing first and opening after,
   and this one saves the close.  Substituting the pair here draws the message
   over the grid.

   THE BACKDROP COPY IS NOT A REDRAW AND CANNOT BE DROPPED.
   fdps_unit_equip_window snapshots the live screen on the way in and paints
   that snapshot back on the way out (unititem.h), so what this memmove puts on
   the adapter is what the player is left looking at once the equip screen
   closes.  Leaving it out because the picture on screen already looks right
   makes the window save the closing zoom's last frame and restore that
   instead.

   THE INDEX IS FED TO THE BATTLE UNIT ARRAY AND NOT TO THE ROSTER, exactly as
   in the three loops above: fdps_village_select_member answers an index into
   the roster and fdps_unit_item_count, fdps_get_unit_record and
   fdps_unit_equip_window all resolve it against data_fdps_map_unit_array_ptr,
   which the field and village bring-up has pointed at the roster base
   (gamedata.h).

   NOTHING IS PUBLISHED ON THE PATH THAT EQUIPS.
   data_fdps_dialog_last_action_text_id_param is written inside the refusal arm
   only, at 000344aa, unlike the hand-over above which writes it on every
   confirmed pick.  A body that hoisted it out of the arm would leave the equip
   screen's own messages naming this member.

   The values used after a CALL are three.  fdps_village_select_member's EAX is
   the roster index, stored to the loop's slot by MOV dword ptr [EBP-0x8],EAX
   at 00034463 and compared and passed on from there.  fdps_unit_item_count's
   EAX is tested where it stands -- TEST EAX,EAX / JNZ at 0003447c with no
   store -- so the count is a branch and never a value.  fdps_get_unit_record's
   EAX is the member's record, MOV dword ptr [EBP-0x4],EAX at 0003449b, and
   only its char_id byte at +8 is read -- MOV AL,byte ptr [EAX+0x8] / AND
   EAX,0xff / INC EAX, an unsigned byte plus one.  fdps_draw_text answers a pen
   position that this caller drops (ADD ESP,0x1c at 000344d2 with no use of
   EAX), and the zoom, memmove and fdps_unit_equip_window return nothing this
   function looks at. */
void fdps_village_member_equip_loop(unsigned char *screen_page)
{
    /* The member's own record, taken only on the refusal path and read only
       for its character id. */
    struct fdps_unit_record *member;
    /* Which party member the player last confirmed in the picker, and the
       loop's only exit: -1 is the picker's cancel and nothing else stops
       this. */
    int picked_member_index;

    picked_member_index = 0;
    fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);

    while (picked_member_index != GRID_CANCELLED) {
        fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
        picked_member_index = fdps_village_select_member();

        if (picked_member_index != GRID_CANCELLED) {
            if (fdps_unit_item_count(picked_member_index) == 0) {
                fdps_village_animate_window_zoom(screen_page,
                                                 WINDOW_ZOOM_OPEN);
                member = fdps_get_unit_record(picked_member_index);
                data_fdps_dialog_last_action_text_id_param =
                    member->char_id + NAME_TEXT_ID_BIAS;
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               EQUIP_EMPTY_BAG_TEXT_ID,
                               (unsigned char *) (VGA_SCREEN_BASE
                                                  + EQUIP_MESSAGE_SCREEN_AT),
                               VGA_SCREEN_PITCH, EQUIP_TEXT_FG_COLOR,
                               EQUIP_TEXT_BG_COLOR, EQUIP_TEXT_OUTLINE_COLOR);
            } else {
                fdps_village_animate_window_zoom(screen_page,
                                                 WINDOW_ZOOM_CLOSE);
                memmove((void *) VGA_SCREEN_BASE, screen_page,
                        (size_t) VGA_SCREEN_BYTES);
                fdps_unit_equip_window(picked_member_index);
            }
        }
    }
}
