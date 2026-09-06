/* shop.c -- the village shop's stock lookup, item picker and buying flow.
 *
 * See shop.h for what each entry point is asked and what it answers.  The
 * stock itself lives in the chapter's SHOP%02d.DAT image, which
 * fdps_load_field_chapter_resources parks in data_fdps_shop_stock_table_ptr
 * (gamedata.h); nothing here allocates or frees it.
 */
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "blit.h"
#include "cdaudio.h"
#include "keybd.h"
#include "palcycle.h"
#include "shopdraw.h"
#include "sprite.h"
#include "shop.h"

/* 00031700.  Walks one twelve-byte row of the shop stock table and packs the
   stocked ids down into the caller's array.

   The loop bound is a literal twelve, CMP dword ptr [EBP-0x10],0xc / JL, and
   there is no second exit: 0xff jumps to the increment at 00031763, not out of
   the loop.  Writing the row's empty slot as a terminator would truncate most
   shipped shops and empty SHOP03.DAT's weapon row entirely
   (rebuild_info/pitfalls.md).

   The row byte is read zero-extended -- XOR EAX,EAX / MOV AL,byte ptr [EDX] at
   00031739 -- which is why data_fdps_shop_stock_table_ptr is an unsigned char
   pointer.  Ids above 0x7f are ordinary stock, so the sign matters twice over:
   for the 0xff test and for the id that lands in the array.

   The two indices are separate.  The row is stepped by the loop counter and
   the destination by the write counter, which only advances on a stocked slot
   (INC dword ptr [EBP-0xc] at 00031760 sits inside the taken branch), so the
   holes are squeezed out and the returned count is the number of stocked
   slots, not the position of the last one. */
int fdps_shop_collect_stock_items(int shop_index, int *out_item_ids)
{
    int write_count;
    int slot;
    int item_id;

    write_count = 0;
    for (slot = 0; slot < 12; slot++) {
        item_id = data_fdps_shop_stock_table_ptr[shop_index * 12 + slot];
        if (item_id != 0xff) {
            out_item_ids[write_count] = item_id;
            write_count++;
        }
    }
    return write_count;
}

/* How many ints of stack the picker sets aside for the shop's stock, SUB
   ESP,0x58 covering [EBP-0x58] through [EBP-0x2c].  It is the twelve slots of
   one stock row, which is the most fdps_shop_collect_stock_items can write; the
   count that comes back bounds every read of it, so a shop stocking less leaves
   the tail untouched and unread. */
#define PICKER_STOCK_SLOTS 12

/* The picker's offscreen page: 312 bytes to a row, 76 rows, and the 0x5ca0 the
   pass mallocs is exactly that product.  It is rebuilt from nothing on every
   pass and freed at the end of it, so no state carries in the pixels. */
#define PICKER_PAGE_PITCH 0x138
#define PICKER_PAGE_BYTES 0x5ca0

/* The window as it reaches the visible screen: 304 by 67 read from page offset
   0x3ad, which is page (5, 3), and written at 0xa9c48, which is screen (8, 125)
   on the mode 13h framebuffer.  0xa0000 stays a literal because it is where the
   adapter answers and not the address of anything the linker places, and the
   two page offsets below are literals for the same reason -- they are positions
   inside a block this function allocated. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define PICKER_WINDOW_PAGE_AT 0x3ad
#define PICKER_WINDOW_SCREEN_AT 0x9c48
#define PICKER_WINDOW_W 0x130
#define PICKER_WINDOW_H 0x43

/* The six cells: two columns 0x91 apart starting at x 0x10, three rows 0x13
   apart starting at y 0x0b, filled left to right and top to bottom.  Six is
   also the distance the window top keeps from the cursor, and two -- one row --
   is the step both the window and the vertical cursor moves take. */
#define PICKER_VISIBLE_CELLS 6
#define PICKER_ROW_STEP 2
#define PICKER_CELL_X_BASE 0x10
#define PICKER_CELL_X_STRIDE 0x91
#define PICKER_CELL_Y_BASE 0x0b
#define PICKER_CELL_Y_STRIDE 0x13

/* Sprite 0 of ShopWin.Cel is the window frame and sprite 1 of SelBar.Cel is the
   bar under the highlighted entry, which is hung five pixels left and one pixel
   above the cell so it surrounds the text rather than sitting on it.  Both go
   through fdps_cel_blit_sprite in its plain opaque mode with no operand. */
#define PICKER_WINDOW_SPRITE 0
#define PICKER_SELECTION_BAR_SPRITE 1
#define SELECTION_BAR_X_LIFT 5
#define SELECTION_BAR_Y_LIFT 1
#define PICKER_BLIT_OPERAND 0
#define PICKER_BLIT_MODE 0

/* The two scroll arrows: Command.cel sprites 0x44/0x45 for up and 0x46/0x47 for
   down, the second of each pair being the lit frame.  Which one is drawn is
   (tick / 5) & 1, so each frame of the blink lasts five timer ticks.

   THE DOWN ARROW IS DRAWN OFF THE END OF THE PAGE.  fdps_blit_command_sprite
   puts a fixed 25 by 22 block down with no clipping (sprite.h) and page offset
   0x4e96 is row 64 of the 76 the page has, so its last ten rows run past the
   0x5ca0 block.  It stays where it is: the screen window only ever shows its
   top six rows, so moving it to fit the page changes the picture
   (rebuild_info/pitfalls.md). */
#define ARROW_BLINK_TICKS 5
#define ARROW_UP_SPRITE 0x44
#define ARROW_DOWN_SPRITE 0x46
#define ARROW_UP_PAGE_AT 0x6ae
#define ARROW_DOWN_PAGE_AT 0x4e96

/* The make codes the picker acts on.  Everything else, the filter's 0xff "no
   key this poll" included, falls through the chain and only costs a frame. */
#define KEY_ESCAPE 0x01
#define KEY_ENTER 0x1c
#define KEY_UP 0x48
#define KEY_LEFT 0x4b
#define KEY_RIGHT 0x4d
#define KEY_DOWN 0x50
#define KEY_SPACE 0x39

/* What the loop's own result slot holds.  It doubles as the answer for the
   cancelled case: the epilogue hands -1 back for anything that is not a
   confirmation, which is the same -1 Escape put here. */
#define PICKER_RUNNING 0
#define PICKER_CANCELLED (-1)
#define PICKER_CONFIRMED 1

/* The sound every accepted cursor move plays.  This function pushes the copy of
   the string at 0x61f4c -- MOV EAX,0x61f4c ahead of all four calls -- which is
   a third literal holding the same eight characters as the ones at 0x61b04 and
   0x61e78 that audio.c and mapcur.c name. */
#define PICKER_MOVE_SFX "Beep.wav"

/* 00032710.  One stack argument, caller-cleaned: the single call site inside
   the shop transaction loop at 00033b80 pushes shop_index and follows the CALL
   with ADD ESP,0x4, and the body reads it at [EBP+0x14] behind PUSH
   EBX/ESI/EDI/EBP and the return address.  RET carries no immediate and the
   caller keeps EAX, which is the item id.

   THE CURSOR AND THE WINDOW TOP ARE NOT SEEDED HERE.  Both are globals that no
   other function in the image touches (shop.h) and the only thing this function
   does to them on entry is the count test at 00032736: CMP EAX,dword ptr
   [EBP-0x24] / JL skips the reset, so the pair is cleared only when the saved
   cursor is at or past this shop's stock count.  Zeroing them unconditionally
   -- the obvious way to open a menu -- loses the reopen-where-you-left-it
   behaviour that is the whole reason they are globals and not locals.

   ONLY THE CURSOR IS CLAMPED, NOT THE TOP.  The reset is one test on the cursor
   and it clears both, so a saved top past the end of a smaller shop's stock
   survives whenever the saved cursor is still inside that stock; the picker
   then paints a page of empty cells.  Adding a second test on the top would be
   the tidy thing and the original does not have it.

   THE KEY CHAIN IS SHORT-CIRCUIT AND ITS ORDER IS THE ASSEMBLY'S.  Each arrow
   arm is one `if (code == k && bound)` whose failure falls into the NEXT code's
   test -- JNZ and the failed bound both land on the same address -- and the
   order is right, left, up, down, which is not the order the arms appear in at
   any other level of the code.  No two codes can be equal so the ordering is
   not observable, but the guards are: a blocked move plays no sound and costs
   nothing but a frame.

   THE VERTICAL MOVES ARE NOT CLAMPED AGAINST THE ROW, ONLY AGAINST THE LIST.
   Up needs a cursor of 2 or more and Down needs two more entries to exist, so
   in a shop with an odd stock the last entry is reachable by Right from the one
   before it but not by Down from the row above.

   THE FRAME IS DRAWN AFTER THE KEY IS HANDLED AND BEFORE THE LOOP TEST, so the
   pass that reads Escape or Enter still builds, presents and paces a whole
   frame before the function returns.

   THE PACE LATCH IS DELIBERATELY NOT INITIALISED.  Every pass ends by spinning
   until data_fdps_timer_tick_counter differs from `last_tick` and then
   re-latching it, and `last_tick` is never seeded, so the first pass compares
   stack garbage: it normally falls straight through the wait and its blink
   phase is whatever that garbage divided by five happened to be.  Seeding it
   adds a tick to the opening of every shop visit (rebuild_info/pitfalls.md).
   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of that wait: nothing inside it writes the counter, so a build
   allowed to hoist the load would spin here forever.

   The values used after a CALL are two.  fdps_shop_collect_stock_items' EAX is
   the stock count, MOV dword ptr [EBP-0x24],EAX at 00032733, and every bound in
   the function is read back from that slot.  malloc's EAX is the page, MOV
   dword ptr [EBP-0x28],EAX at 000328a2, used unchecked -- there is no test for
   NULL anywhere -- and freed at the end of the pass.
   fdps_read_scancode_auto_repeat's EAX is the scancode and goes to [EBP-0xc];
   nothing else here looks at a return value, and the drawing calls and the CD
   and palette polls return nothing this function reads.

   The blink phase is a local here and lives in the ARGUMENT slot in the
   original -- MOV dword ptr [EBP+0x14],EAX at 00032992 stores it over
   shop_index, which nothing reads after the collect call at 0003272b.  Under
   -od Watcom gives every declared local its own slot and does not coalesce, so
   what that says is that the original source assigned the phase to the
   parameter.  A separate local costs a stack slot and nothing else (ADR-0001);
   the parameter keeps the name of the one thing it means on the way in. */
int fdps_shop_select_item(int shop_index)
{
    /* The answer, filled in once the loop has stopped. */
    int selected_item_id;
    /* The tick the previous pass finished on, and the source of the arrow
       blink phase.  Unsigned because the blink divide is DIV and not IDIV. */
    unsigned int last_tick;
    /* One poll of the auto-repeat filter: a make code, or 0xff for nothing. */
    unsigned int scancode;
    /* Whether the loop is still running, and if not, how it stopped. */
    int loop_result;
    /* Which entry of the stock list a cell is showing. */
    int entry_index;
    /* Which of the six visible cells is being drawn, 0 to 5. */
    int cell_index;
    /* That cell's top left corner in the page. */
    int cell_y;
    int cell_x;
    /* How many entries this shop is stocking, and what they are. */
    int stock_count;
    unsigned char *page;
    int stock_item_ids[PICKER_STOCK_SLOTS];
    /* 0 or 1: which frame of the two-frame scroll arrows this pass shows. */
    int arrow_blink_phase;

    loop_result = PICKER_RUNNING;
    stock_count = fdps_shop_collect_stock_items(shop_index, stock_item_ids);

    if (data_fdps_shop_item_picker_cursor_idx >= stock_count) {
        data_fdps_shop_item_list_scroll_offset = 0;
        data_fdps_shop_item_picker_cursor_idx = 0;
    }

    while (loop_result == PICKER_RUNNING) {
        fdps_cd_music_repeat_poll();
        scancode = fdps_read_scancode_auto_repeat();

        if (scancode == KEY_ESCAPE) {
            loop_result = PICKER_CANCELLED;
        } else if (scancode == KEY_ENTER || scancode == KEY_SPACE) {
            loop_result = PICKER_CONFIRMED;
        } else if (scancode == KEY_RIGHT
                   && data_fdps_shop_item_picker_cursor_idx
                      < stock_count - 1) {
            data_fdps_shop_item_picker_cursor_idx++;
            if (data_fdps_shop_item_list_scroll_offset + PICKER_VISIBLE_CELLS
                <= data_fdps_shop_item_picker_cursor_idx) {
                data_fdps_shop_item_list_scroll_offset += PICKER_ROW_STEP;
            }
            fdps_play_sfx(PICKER_MOVE_SFX);
        } else if (scancode == KEY_LEFT
                   && data_fdps_shop_item_picker_cursor_idx > 0) {
            data_fdps_shop_item_picker_cursor_idx--;
            if (data_fdps_shop_item_picker_cursor_idx
                < data_fdps_shop_item_list_scroll_offset) {
                data_fdps_shop_item_list_scroll_offset -= PICKER_ROW_STEP;
            }
            fdps_play_sfx(PICKER_MOVE_SFX);
        } else if (scancode == KEY_UP
                   && data_fdps_shop_item_picker_cursor_idx > 1) {
            data_fdps_shop_item_picker_cursor_idx -= PICKER_ROW_STEP;
            if (data_fdps_shop_item_picker_cursor_idx
                < data_fdps_shop_item_list_scroll_offset) {
                data_fdps_shop_item_list_scroll_offset -= PICKER_ROW_STEP;
            }
            fdps_play_sfx(PICKER_MOVE_SFX);
        } else if (scancode == KEY_DOWN
                   && data_fdps_shop_item_picker_cursor_idx
                      < stock_count - PICKER_ROW_STEP) {
            data_fdps_shop_item_picker_cursor_idx += PICKER_ROW_STEP;
            if (data_fdps_shop_item_list_scroll_offset + PICKER_VISIBLE_CELLS
                <= data_fdps_shop_item_picker_cursor_idx) {
                data_fdps_shop_item_list_scroll_offset += PICKER_ROW_STEP;
            }
            fdps_play_sfx(PICKER_MOVE_SFX);
        }

        page = (unsigned char *) malloc(PICKER_PAGE_BYTES);
        fdps_cel_blit_sprite(data_fdps_village_window_sheet_ptr,
                             PICKER_WINDOW_SPRITE, page, PICKER_PAGE_PITCH,
                             0, 0, PICKER_BLIT_OPERAND, PICKER_BLIT_MODE);

        for (cell_index = 0; cell_index < PICKER_VISIBLE_CELLS; cell_index++) {
            entry_index = cell_index + data_fdps_shop_item_list_scroll_offset;

            if (entry_index < stock_count) {
                cell_x = (cell_index & 1) * PICKER_CELL_X_STRIDE
                         + PICKER_CELL_X_BASE;
                cell_y = (cell_index / 2) * PICKER_CELL_Y_STRIDE
                         + PICKER_CELL_Y_BASE;

                if (data_fdps_shop_item_picker_cursor_idx == entry_index) {
                    fdps_cel_blit_sprite(data_fdps_selection_bar_sheet_ptr,
                                         PICKER_SELECTION_BAR_SPRITE, page,
                                         PICKER_PAGE_PITCH,
                                         cell_x - SELECTION_BAR_X_LIFT,
                                         cell_y - SELECTION_BAR_Y_LIFT,
                                         PICKER_BLIT_OPERAND,
                                         PICKER_BLIT_MODE);
                }

                fdps_shop_draw_item_entry(stock_item_ids[entry_index],
                                          page + cell_y * PICKER_PAGE_PITCH
                                              + cell_x,
                                          PICKER_PAGE_PITCH);
            }
        }

        arrow_blink_phase = (int) ((last_tick / ARROW_BLINK_TICKS) & 1);

        if (data_fdps_shop_item_list_scroll_offset != 0) {
            fdps_blit_command_sprite(page + ARROW_UP_PAGE_AT,
                                     PICKER_PAGE_PITCH,
                                     arrow_blink_phase + ARROW_UP_SPRITE);
        }

        if (data_fdps_shop_item_list_scroll_offset + PICKER_VISIBLE_CELLS
            < stock_count) {
            fdps_blit_command_sprite(page + ARROW_DOWN_PAGE_AT,
                                     PICKER_PAGE_PITCH,
                                     arrow_blink_phase + ARROW_DOWN_SPRITE);
        }

        fdps_cycle_ui_palette();
        fdps_blit_rect((unsigned int) (page + PICKER_WINDOW_PAGE_AT),
                       PICKER_PAGE_PITCH,
                       (void *) (VGA_SCREEN_BASE + PICKER_WINDOW_SCREEN_AT),
                       VGA_SCREEN_PITCH, PICKER_WINDOW_W, PICKER_WINDOW_H);

        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
        free(page);
    }

    if (loop_result > PICKER_RUNNING) {
        selected_item_id =
            stock_item_ids[data_fdps_shop_item_picker_cursor_idx];
    } else {
        selected_item_id = PICKER_CANCELLED;
    }

    return selected_item_id;
}
