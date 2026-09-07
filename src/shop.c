/* shop.c -- the village shop's stock lookup, item picker and buying flow.
 *
 * See shop.h for what each entry point is asked and what it answers.  The
 * stock itself lives in the chapter's SHOP%02d.DAT image, which
 * fdps_load_field_chapter_resources parks in data_fdps_shop_stock_table_ptr
 * (gamedata.h); nothing here allocates or frees it.
 */
#include <stdlib.h>
#include <string.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "blit.h"
#include "cdaudio.h"
#include "keybd.h"
#include "msgwin.h"
#include "palcycle.h"
#include "shopdraw.h"
#include "sprite.h"
#include "table.h"
#include "text.h"
#include "unit.h"
#include "unititem.h"
#include "village.h"
#include "vilmenu.h"
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

/* ------------------------------------------------------------------------
   fdps_shop_select_buy_target, 00032c40
   ------------------------------------------------------------------------ */

/* The second cancel code.  The item picker above answers to Escape alone; this
   one takes Delete as well, CMP dword ptr [EBP-0x20],0x53 at 00032ca9 joining
   Escape's store. */
#define KEY_DELETE 0x53

/* What the item's type byte decides.  0x01 through 0x27 is equipment --
   weapons 0x01-0x15 and armour 0x16-0x27 (unititem.h) -- and gets the picker
   below, which previews what each member's combat stats would become.
   Everything else, type 0 and the 0x28 consumables and the 0x29-0x2c badges
   and plot items above them, has no stats to preview and is handed to the
   village's plain member grid instead.

   THE TEST IS A ZERO TEST AND A SIGNED UPPER BOUND, not a range: CMP dword ptr
   [EBP-0xc],0x0 / JZ at 00032c73 and CMP ...,0x27 / JLE at 00032c79.  Type 0
   therefore leaves on the FIRST branch and never reaches the bound. */
#define BUY_TARGET_LAST_EQUIPMENT_TYPE 0x27

/* The offscreen grid every member's entry is drawn into: 312 bytes to a row,
   335 rows, and the 0x19848 the pass mallocs is exactly that product.  It is
   not the page the frame is composed on -- fdps_shop_render_buy_target_frame
   allocates its own 312 x 76 one (shopdraw.h) -- it is the whole list, of which
   that routine copies one 0x43-row strip. */
#define BUY_TARGET_GRID_PITCH 0x138
#define BUY_TARGET_GRID_BYTES 0x19848

/* The grid's layout: three entries to a row, an entry 0x43 rows tall and 0x65
   columns wide, and the first entry's top-left corner at row 0xc column 9.
   Three is also the step both the cursor's vertical moves and the scroll top
   take, and the divisor that turns a roster index into its row and column. */
#define BUY_TARGET_ENTRIES_PER_ROW 3
#define BUY_TARGET_ENTRY_ROW_HEIGHT 0x43
#define BUY_TARGET_ENTRY_COLUMN_STRIDE 0x65
#define BUY_TARGET_ENTRY_ROW_ORIGIN 0x0c
#define BUY_TARGET_ENTRY_COLUMN_ORIGIN 9

/* Where the selection bar goes: the cursor's column in the grid, but measured
   from 5 rather than from the entries' own 9, so the bar sits four pixels left
   of the entry it is behind. */
#define BUY_TARGET_CURSOR_COLUMN_ORIGIN 5

/* The slide.  A move that changes the visible row is drawn as seven extra
   frames whose source row is stepped by step * 0x43 / 8, so the 67-pixel row
   scrolls in eight sub-steps and the eighth is the settled frame the pass draws
   anyway.  The divide is signed, IDIV-shaped SAR EAX,0x3 with the SBB
   correction at 00032d67, which costs nothing here because the step is always
   positive but is what the expression means. */
#define BUY_TARGET_SLIDE_STEPS 8

/* The roster slot that cannot be confirmed from chapter index 0x17 on, and the
   chapter that starts it.  Slot 3 is 法蓮娜, who has left the party by then.
   fdps_village_select_member locks the same slot from the same chapter
   (vilmenu.h) and fdps_shop_draw_member_entry ghosts the same member by a
   separate test on the record's character id (shopdraw.h). */
#define BUY_TARGET_LOCKED_FIRST_CHAPTER 0x17
#define BUY_TARGET_LOCKED_SLOT 3

/* What the loop's own result slot holds.  It doubles as the answer for the
   cancelled case: the epilogue hands -1 back for anything that is not a
   confirmation, which is the same -1 Escape and Delete put here. */
#define BUY_TARGET_RUNNING 0
#define BUY_TARGET_CANCELLED (-1)
#define BUY_TARGET_CONFIRMED 1

/* 00032c40.  One stack argument, caller-cleaned: the single call site inside
   the shop transaction loop at 00033b80 pushes the item id and follows the CALL
   with ADD ESP,0x4, and the body reads it at [EBP+0x14] behind PUSH
   EBX/ESI/EDI/EBP and the return address.  RET carries no immediate and the
   caller keeps EAX, which is the roster index.

   TWO PICKERS, ONE ENTRY POINT.  The item's type byte decides which, and for a
   non-equipment item the whole screen belongs to fdps_village_select_member:
   its answer is stored straight into the result slot at 00032c84 and returned
   unchanged, so a cancel there is this function's cancel and a roster index
   there is this function's roster index.  Nothing else in the non-equipment
   path runs -- neither global below is read, no frame is drawn, and the loop is
   not entered.

   THE CURSOR AND THE SCROLL TOP ARE NOT SEEDED HERE.  Both are globals that no
   other function in the image touches (shop.h) and this one neither clears nor
   clamps them on entry, so the screen reopens on the member picked last time
   even if the party has shrunk since.  Seeding either -- the obvious way to
   open a menu -- loses that.

   THE KEY CHAIN IS SHORT-CIRCUIT AND ITS ORDER IS THE ASSEMBLY'S: cancel,
   confirm, then right, left, up, down, each arrow arm one `if (code == k &&
   bound)` whose failure falls into the NEXT code's test -- JNZ and the failed
   bound both land on the same address.  No two codes can be equal so the
   ordering is not observable, but the guards are: a blocked move plays no sound
   and costs nothing but a frame.

   THE CONFIRM GATE IS WRITTEN AS TWO LITERALS AND NOT AS A PARTY TEST.  CMP
   dword ptr [0x00069cf4],0x17 / JL at 00032cc7 and CMP dword ptr
   [0x000601b4],0x3 / JZ at 00032cd0: from chapter index 0x17 on, a confirm with
   the cursor on roster slot 3 is dropped and the loop simply keeps running.
   The chapter test has NO UPPER BOUND, so rewriting it as "this member has left
   the party" would let 法蓮娜 be bought for again once she rejoins for the end
   of the game (rebuild_info/pitfalls.md).  It is only the confirm that is
   refused: the entry is still drawn and the cursor still stops on it.

   THE SLIDE BLITS FROM THE PREVIOUS PASS'S GRID, WHICH HAS ALREADY BEEN FREED.
   The grid is allocated at the BOTTOM of the loop and the slide frames at the
   top of the next one read the pointer left in the slot, so the animation draws
   out of a block that free() has already taken back -- and on the very first
   pass out of an uninitialised stack slot.  It works because Watcom's near heap
   hands the same block back to the next malloc of the same size with its
   contents intact.  Hoisting the allocation out of the loop, or clearing the
   pointer after the free, changes what the animation draws
   (rebuild_info/pitfalls.md).

   THE VERTICAL MOVES ALWAYS SLIDE AND THE HORIZONTAL ONES ONLY SOMETIMES.  Up
   and down step a whole row, so the visible row always changes; right and left
   slide only when the cursor has left the three entries the row shows, which is
   scroll + 3 <= cursor going forward and cursor < scroll coming back.

   THE PACE LATCH IS DELIBERATELY NOT INITIALISED, exactly as in
   fdps_shop_select_item above: every pass ends by spinning until
   data_fdps_timer_tick_counter differs from `last_tick` and then re-latching it,
   and `last_tick` is never seeded, so the first pass compares stack garbage and
   normally falls straight through the wait.  data_fdps_timer_tick_counter is
   volatile at its declaration (gamedata.h) because of that wait: nothing inside
   it writes the counter, so a build allowed to hoist the load would spin here
   forever.

   The values used after a CALL are three.  fdps_get_item_record's EAX is the
   item record, MOV dword ptr [EBP-0x28],EAX at 00032c66, and the only thing
   read through it is byte +0x00 -- XOR EAX,EAX / MOV AL,byte ptr [EDX] at
   00032c6e, zero-extended into an int slot, so the type is unsigned however the
   compare that follows is written.  fdps_village_select_member's EAX is the
   answer for the non-equipment path and is stored to the result slot with
   nothing between the CALL and the store.  fdps_read_scancode_auto_repeat's EAX
   is the scancode and goes to [EBP-0x20].  malloc's EAX is the entry grid, MOV
   dword ptr [EBP-0x2c],EAX at 00032fbb, used unchecked -- there is no test for
   NULL anywhere.  Nothing else here reads a return value: the CD poll, the
   sound, the two drawing calls, memset and free return nothing this function
   looks at.

   The slide counter and the draw loop's roster index are ONE stack slot in the
   original, [EBP-0x24], which under -od means one variable in the source since
   Watcom gives every declared local its own slot and does not coalesce.  They
   are two locals here because the two loops count different things and a name
   cannot say both; a second slot costs nothing observable (ADR-0001). */
int fdps_shop_select_buy_target(int item_id)
{
    /* The grid every member's entry is drawn into, rebuilt at the bottom of
       each pass and freed again before the next one reads it. */
    unsigned char *entry_grid;
    /* The offered item's ITEM.DAT record, read for its type byte alone. */
    struct fdps_item_effect *item_record;
    /* 1 to 7: which sub-step of a row slide is being drawn. */
    int slide_step;
    /* Which roster member's entry the draw loop is putting in the grid. */
    int member_index;
    /* One poll of the auto-repeat filter: a make code, or 0xff for nothing. */
    unsigned int scancode;
    /* Whether the loop is still running, and if not, how it stopped. */
    int loop_result;
    /* How big the entry grid is, in bytes -- the size handed to both malloc and
       the memset that clears it. */
    int grid_bytes;
    /* The member entry's top-left corner in the grid. */
    int entry_column;
    int entry_row;
    /* The offered item's type byte, widened without sign. */
    int item_type;
    /* The tick the previous pass finished on. */
    unsigned int last_tick;
    /* The answer, filled in once the loop has stopped. */
    int chosen_index;

    loop_result = BUY_TARGET_RUNNING;
    grid_bytes = BUY_TARGET_GRID_BYTES;

    item_record = fdps_get_item_record(item_id);
    item_type = (int) item_record->type;

    if (item_type == 0 || item_type > BUY_TARGET_LAST_EQUIPMENT_TYPE) {
        chosen_index = fdps_village_select_member();
        return chosen_index;
    }

    while (loop_result == BUY_TARGET_RUNNING) {
        fdps_cd_music_repeat_poll();
        scancode = fdps_read_scancode_auto_repeat();

        if (scancode == KEY_ESCAPE || scancode == KEY_DELETE) {
            loop_result = BUY_TARGET_CANCELLED;
        } else if (scancode == KEY_ENTER || scancode == KEY_SPACE) {
            if (data_fdps_chapter_current_chapter_id
                    < BUY_TARGET_LOCKED_FIRST_CHAPTER
                || data_fdps_shop_buy_target_cursor_idx
                   != BUY_TARGET_LOCKED_SLOT) {
                loop_result = BUY_TARGET_CONFIRMED;
            }
        } else if (scancode == KEY_RIGHT
                   && data_fdps_roster_member_count - 1
                      > data_fdps_shop_buy_target_cursor_idx) {
            fdps_play_sfx(PICKER_MOVE_SFX);
            data_fdps_shop_buy_target_cursor_idx++;
            if (data_fdps_shop_buy_target_scroll_offset
                    + BUY_TARGET_ENTRIES_PER_ROW
                <= data_fdps_shop_buy_target_cursor_idx) {
                for (slide_step = 1; slide_step < BUY_TARGET_SLIDE_STEPS;
                     slide_step++) {
                    fdps_shop_render_buy_target_frame(
                        entry_grid,
                        (data_fdps_shop_buy_target_cursor_idx
                         % BUY_TARGET_ENTRIES_PER_ROW)
                            * BUY_TARGET_ENTRY_COLUMN_STRIDE
                            + BUY_TARGET_CURSOR_COLUMN_ORIGIN,
                        (data_fdps_shop_buy_target_scroll_offset
                         / BUY_TARGET_ENTRIES_PER_ROW)
                            * BUY_TARGET_ENTRY_ROW_HEIGHT
                            + slide_step * BUY_TARGET_ENTRY_ROW_HEIGHT
                              / BUY_TARGET_SLIDE_STEPS,
                        data_fdps_shop_buy_target_scroll_offset);
                }
                data_fdps_shop_buy_target_scroll_offset +=
                    BUY_TARGET_ENTRIES_PER_ROW;
            }
        } else if (scancode == KEY_LEFT
                   && data_fdps_shop_buy_target_cursor_idx > 0) {
            fdps_play_sfx(PICKER_MOVE_SFX);
            data_fdps_shop_buy_target_cursor_idx--;
            if (data_fdps_shop_buy_target_cursor_idx
                < data_fdps_shop_buy_target_scroll_offset) {
                for (slide_step = 1; slide_step < BUY_TARGET_SLIDE_STEPS;
                     slide_step++) {
                    fdps_shop_render_buy_target_frame(
                        entry_grid,
                        (data_fdps_shop_buy_target_cursor_idx
                         % BUY_TARGET_ENTRIES_PER_ROW)
                            * BUY_TARGET_ENTRY_COLUMN_STRIDE
                            + BUY_TARGET_CURSOR_COLUMN_ORIGIN,
                        (data_fdps_shop_buy_target_scroll_offset
                         / BUY_TARGET_ENTRIES_PER_ROW)
                            * BUY_TARGET_ENTRY_ROW_HEIGHT
                            - slide_step * BUY_TARGET_ENTRY_ROW_HEIGHT
                              / BUY_TARGET_SLIDE_STEPS,
                        data_fdps_shop_buy_target_scroll_offset);
                }
                data_fdps_shop_buy_target_scroll_offset -=
                    BUY_TARGET_ENTRIES_PER_ROW;
            }
        } else if (scancode == KEY_UP
                   && data_fdps_shop_buy_target_cursor_idx
                      > BUY_TARGET_ENTRIES_PER_ROW - 1) {
            fdps_play_sfx(PICKER_MOVE_SFX);
            for (slide_step = 1; slide_step < BUY_TARGET_SLIDE_STEPS;
                 slide_step++) {
                fdps_shop_render_buy_target_frame(
                    entry_grid,
                    (data_fdps_shop_buy_target_cursor_idx
                     % BUY_TARGET_ENTRIES_PER_ROW)
                        * BUY_TARGET_ENTRY_COLUMN_STRIDE
                        + BUY_TARGET_CURSOR_COLUMN_ORIGIN,
                    (data_fdps_shop_buy_target_scroll_offset
                     / BUY_TARGET_ENTRIES_PER_ROW)
                        * BUY_TARGET_ENTRY_ROW_HEIGHT
                        - slide_step * BUY_TARGET_ENTRY_ROW_HEIGHT
                          / BUY_TARGET_SLIDE_STEPS,
                    data_fdps_shop_buy_target_scroll_offset);
            }
            data_fdps_shop_buy_target_cursor_idx -= BUY_TARGET_ENTRIES_PER_ROW;
            data_fdps_shop_buy_target_scroll_offset -=
                BUY_TARGET_ENTRIES_PER_ROW;
        } else if (scancode == KEY_DOWN
                   && data_fdps_roster_member_count
                      - BUY_TARGET_ENTRIES_PER_ROW
                      > data_fdps_shop_buy_target_cursor_idx) {
            fdps_play_sfx(PICKER_MOVE_SFX);
            for (slide_step = 1; slide_step < BUY_TARGET_SLIDE_STEPS;
                 slide_step++) {
                fdps_shop_render_buy_target_frame(
                    entry_grid,
                    (data_fdps_shop_buy_target_cursor_idx
                     % BUY_TARGET_ENTRIES_PER_ROW)
                        * BUY_TARGET_ENTRY_COLUMN_STRIDE
                        + BUY_TARGET_CURSOR_COLUMN_ORIGIN,
                    (data_fdps_shop_buy_target_scroll_offset
                     / BUY_TARGET_ENTRIES_PER_ROW)
                        * BUY_TARGET_ENTRY_ROW_HEIGHT
                        + slide_step * BUY_TARGET_ENTRY_ROW_HEIGHT
                          / BUY_TARGET_SLIDE_STEPS,
                    data_fdps_shop_buy_target_scroll_offset);
            }
            data_fdps_shop_buy_target_cursor_idx += BUY_TARGET_ENTRIES_PER_ROW;
            data_fdps_shop_buy_target_scroll_offset +=
                BUY_TARGET_ENTRIES_PER_ROW;
        }

        entry_grid = (unsigned char *) malloc((size_t) grid_bytes);
        memset(entry_grid, 0, (size_t) grid_bytes);

        for (member_index = 0;
             member_index < data_fdps_roster_member_count;
             member_index++) {
            entry_column = (member_index % BUY_TARGET_ENTRIES_PER_ROW)
                               * BUY_TARGET_ENTRY_COLUMN_STRIDE
                           + BUY_TARGET_ENTRY_COLUMN_ORIGIN;
            entry_row = (member_index / BUY_TARGET_ENTRIES_PER_ROW)
                        * BUY_TARGET_ENTRY_ROW_HEIGHT;
            fdps_shop_draw_member_entry(
                entry_grid
                    + (entry_row + BUY_TARGET_ENTRY_ROW_ORIGIN)
                      * BUY_TARGET_GRID_PITCH
                    + entry_column,
                BUY_TARGET_GRID_PITCH, member_index, item_id);
        }

        fdps_shop_render_buy_target_frame(
            entry_grid,
            (data_fdps_shop_buy_target_cursor_idx
             % BUY_TARGET_ENTRIES_PER_ROW)
                * BUY_TARGET_ENTRY_COLUMN_STRIDE
                + BUY_TARGET_CURSOR_COLUMN_ORIGIN,
            (data_fdps_shop_buy_target_scroll_offset
             / BUY_TARGET_ENTRIES_PER_ROW)
                * BUY_TARGET_ENTRY_ROW_HEIGHT,
            data_fdps_shop_buy_target_scroll_offset);

        free(entry_grid);

        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    if (loop_result > BUY_TARGET_RUNNING) {
        chosen_index = data_fdps_shop_buy_target_cursor_idx;
    } else {
        chosen_index = BUY_TARGET_CANCELLED;
    }

    return chosen_index;
}

/* ------------------------------------------------------------------------
   fdps_shop_buy_loop, 00033b80
   ------------------------------------------------------------------------ */

/* The two fixed places on the visible screen this loop writes to.  The gold
   readout is PUSH 0xa8208 in front of all four calls, screen (8, 104), and
   every message goes to 0xaa3d4, screen (20, 131) -- the same village text
   origin fdps_draw_text's own page break puts the pen at (text.c).  Both stay
   literals against VGA_SCREEN_BASE for the reason given at the head of the
   item picker: 0xa0000 is where the adapter answers, not the address of
   anything the linker places. */
#define GOLD_READOUT_SCREEN_AT 0x8208
#define BUY_MESSAGE_SCREEN_AT 0xa3d4

/* How far down a pass's remaining messages move once the trade-in offer has
   been put up, MOV dword ptr [EBP-0x18],0x26 at 00033ce1: 0x26 rows of the
   0x140-stride screen, which clears the prompt already standing at the origin.
   It is set BEFORE the offer is drawn and the offer itself is still drawn at
   the plain origin -- PUSH 0xaa3d4 at 00033d45 with no IMUL in front of it --
   so the shift reaches the bag-full, buy, refund and cannot-afford lines and
   nothing else. */
#define BUY_MESSAGE_ROWS_BELOW_PROMPT 0x26

/* The three colours every one of the five messages is drawn in, PUSH 0xd0 /
   PUSH 0x0 / PUSH 0x6d at each call site. */
#define BUY_TEXT_FG 0xd0
#define BUY_TEXT_BG 0
#define BUY_TEXT_OUTLINE 0x6d

/* The five entries of data_fdps_all_game_text_ptr this loop shows.  0x1f6 asks
   whether the gear already worn may be traded in, 0x1f8 whether the item is to
   be bought, 0x1f9 the same question when the trade-in has left the shop owing
   the player money, 0x1fa refuses because the bag is full and 0x213 refuses
   because the party cannot afford it.  Only the first three carry a prompt. */
#define MSG_TRADE_IN_OFFER 0x1f6
#define MSG_BUY_OFFER 0x1f8
#define MSG_REFUND_OFFER 0x1f9
#define MSG_BAG_IS_FULL 0x1fa
#define MSG_CANNOT_AFFORD 0x213

/* Where the names the trade-in offer substitutes into itself live in that same
   text block: a member's name is its roster record's character id plus one and
   an item's name is its ITEM.DAT id plus 0xc9, which is the numbering
   fdps_shop_draw_item_entry and fdps_draw_unit_inventory also add to
   (shopdraw.c, statunit.c). */
#define BUY_MEMBER_NAME_TEXT_BIAS 1
#define BUY_ITEM_NAME_TEXT_BASE 0xc9

/* The item type at which the trade-in switches from looking for the member's
   equipped weapon to looking for its equipped armour, CMP dword ptr
   [EBP-0x2c],0x15 / JG at 00033c84.  It is the weapon/armour split of the type
   field (assets/items.md), and the test is one-sided: everything above 0x15,
   the consumables and story items included, asks for armour. */
#define BUY_LAST_WEAPON_TYPE 0x15

/* What a full bag holds, CMP EAX,0x8 at 00033d9b -- the eight inventory
   entries of a unit record (unititem.h). */
#define BUY_BAG_CAPACITY 8

/* What the shop pays for the gear it takes in: three quarters of that item's
   own price, truncated toward zero.  LEA EDX,[EDX + EDX*0x2] with the signed
   divide by four at 00033d73 -- the SBB correction and the SAR pair, not a
   plain shift -- so the arithmetic is int-wide and signed however small the
   two numbers are. */
#define TRADE_IN_NUMERATOR 3
#define TRADE_IN_DENOMINATOR 4

/* fdps_prompt_two_choice's affirmative, TEST EAX,EAX / JNZ at 00033d62,
   00033e26 and 00033ed1: the left cell answers 0 and both the right cell and a
   cancel decline (msgwin.h). */
#define BUY_PROMPT_ACCEPTED 0

/* What fdps_village_animate_window_zoom's second argument means: the loop
   sweeps the caller's menu window shut once on the way in and back open before
   every picker. */
#define WINDOW_ZOOM_OPEN 0
#define WINDOW_ZOOM_CLOSE 1

/* The sound a settled payment plays, MOV EAX,0x61f58 in front of both calls.
   It is a fourth copy of a filename literal and not one of the three the other
   files name. */
#define BUY_PAYMENT_SFX "Incom.wav"

/* How long the screen holds after a settled payment, PUSH 0xc8 in front of
   both delay() calls: 200 milliseconds with the gold readout already
   repainted, so the player sees the new purse before the next picker. */
#define BUY_PAYMENT_HOLD_MS 200

/* What both pickers answer when the player backs out.  It is NOT what the
   loop's own control slot starts as: MOV dword ptr [EBP-0x34],0x0 at 00033b8c
   seeds it with zero so the entry test at 00033ba5 falls through into the
   first pass. */
#define BUY_CANCELLED (-1)

/* 00033b80.  Two stack arguments, caller-cleaned: all three call sites --
   fdps_village_item_menu at 000359d6, fdps_run_weapon_shop at 00036149 and
   fdps_run_secret_menu at 0003638d -- push the shop index and the screen page
   and follow the CALL with ADD ESP,0x8, and the body reads them at [EBP+0x14]
   and [EBP+0x18] behind PUSH EBX/ESI/EDI/EBP and the return address.  RET
   carries no immediate, EAX is never set before the epilogue and no call site
   looks at it.

   THE SHOP INDEX IS A BYTE PARAMETER although every caller pushes a whole
   dword: XOR EAX,EAX / MOV AL,byte ptr [EBP+0x18] at 00033bd0 reads the low
   byte alone and widens it without sign, which is the shape wcc386 gives an
   `unsigned char` argument.  It is 0, 1 or 2 and goes straight through
   fdps_shop_select_item to the stock row.

   THE PASS IS A CHAIN OF FOUR ABANDONMENTS AND THE ORDER OF THEM IS THE
   BEHAVIOUR.  A cancel out of the item picker ends the whole loop; a cancel
   out of the buy-target picker ends only the pass; a full bag refuses with
   0x1fa; and a price the party cannot afford refuses with 0x213.  Each of the
   first three is a jump to the loop's back edge at 00033f6f, so nothing after
   it in the pass runs.

   THE BAG-FULL REFUSAL SITS AFTER THE TRADE-IN OFFER AND IS SKIPPED BY IT.
   CMP byte ptr [EBP-0x4],0x0 / JNZ at 00033d89 jumps over the count entirely
   once the player has accepted the trade-in, because the traded item leaves
   the bag before the bought one arrives.  Hoisting the count above the offer
   -- the tidy way to write "refuse early" -- refuses purchases the original
   allows (rebuild_info/pitfalls.md).

   THE MEMBER INDEX IS ONE INDEX AND NOT TWO.  The same number goes to
   fdps_get_roster_record, which resolves through data_fdps_roster_array_ptr,
   and to every fdps_unit_* accessor, which resolve through
   data_fdps_map_unit_array_ptr; during the village phase
   fdps_load_field_chapter_resources has pointed the second at the roster
   block, so both reach one record.  Converting between a "roster index" and a
   "unit index" here breaks the shop (rebuild_info/pitfalls.md).

   THE PRICE IS PUBLISHED IN A GLOBAL AND THEN USED AS THE PASS'S RUNNING
   BALANCE.  data_fdps_dialog_last_action_value_param is the slot fdps_draw_text
   substitutes a figure from (gamedata.h), and this loop both writes the price
   into it and subtracts the trade-in credit from it in place, so what the offer
   shows and what the purse is charged are one number.  A negative balance -- a
   trade-in worth more than the item -- is negated in the same slot before the
   refund offer is drawn, so the figure the player is shown is what the shop
   pays out.

   THE TWO PRICE READS ARE SIGNED WIDENINGS OF AN UNSIGNED FIELD.  Both are
   MOV AX/DX,word ptr [record + 0x13] with the top half cleared, so a price is
   0..65535, but every test that follows is signed -- CMP dword ptr
   [0x00064038],0x0 / JLE at 00033dd9 and CMP EAX,dword ptr [0x000643a4] / JG
   at 00033deb -- because the balance can go negative and the purse is an int.

   The values used after a CALL are eight.  fdps_shop_select_item's EAX is the
   item id and is the loop's control, MOV dword ptr [EBP-0x34],EAX at 00033bde.
   fdps_get_item_record's EAX is the record, at 00033bf7 for the offered item
   and at 00033d07 for the one being traded in, and the only fields read
   through either are the type byte at +0 and the price word at +0x13.
   fdps_shop_select_buy_target's EAX is the roster index, MOV dword ptr
   [EBP-0x30],EAX at 00033c31.  fdps_unit_can_equip_item's EAX is tested twice
   -- once at 00033c55, whose answer is latched 0-or-1 as the auto-equip flag,
   and once at 00033cd8 as the second half of the trade-in guard, which is a
   SECOND call and not the latched flag read back.
   fdps_unit_find_equipped_slot's EAX is the slot, at 00033caa.
   fdps_unit_get_item_id's EAX is the equipped item's id, at 00033cf8.
   fdps_get_roster_record's EAX is the member record, at 00033d16.
   fdps_prompt_two_choice's EAX is the answer and is compared with 0 at all
   three offers.  fdps_unit_item_count's EAX is the bag count, tested against 8
   at 00033d9b and read again at 00033f4f for the slot the bought item landed
   in.  Nothing else here reads a return value: fdps_unit_add_item's answer is
   discarded, and the window sweeps, the gold readouts, the five fdps_draw_text
   calls, the sound and the delay return nothing this function looks at. */
void fdps_shop_buy_loop(unsigned char *screen_page, unsigned char shop_index)
{
    /* What the item picker last answered, and the loop's own control: the loop
       runs until the picker cancels.  It is seeded with 0 rather than with an
       item id because the entry test runs before the first picker does. */
    int item_id;
    /* The offered item's ITEM.DAT record, read for its type byte and its
       price. */
    struct fdps_item_effect *item_record;
    /* That type byte, widened without sign into an int slot. */
    int item_type;
    /* Which roster member the pass is buying for, or -1 when the player backed
       out of the target picker. */
    int buy_target;
    /* Whether that member may wear the offered item.  It is latched here and
       read again at the end of the pass, after two window sweeps and up to two
       prompts have run in between. */
    unsigned char auto_equip;
    /* Which kind of equipped gear the trade-in looks for: 0 the weapon, 1 the
       armour. */
    int want_armor;
    /* The inventory slot that gear is in, or -1 when the member is wearing
       none of that kind. */
    int equipped_slot;
    /* What is in that slot, and its record -- the price the credit is three
       quarters of. */
    int equipped_item_id;
    struct fdps_item_effect *equipped_record;
    /* The member's own roster record, read for the character id the offer
       names it by. */
    struct fdps_unit_record *member_record;
    /* Whether the player took the trade-in offer.  It decides three things:
       that the bag-full refusal is skipped, that the credit came off the
       balance, and that the old slot is emptied once the money has moved. */
    unsigned char trade_in_accepted;
    /* Whether money actually moved this pass, which is the one thing that lets
       the inventory change at all. */
    unsigned char payment_settled;
    /* How far down the screen this pass's remaining messages go, 0 until the
       trade-in offer has taken the origin. */
    int message_row_offset;
    /* How many things the member is carrying once the bought item is in, which
       is where the auto-equip finds it. */
    int bag_count;

    item_id = 0;
    fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);

    while (item_id != BUY_CANCELLED) {
        fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
        fdps_draw_party_gold(
            (unsigned char *) (VGA_SCREEN_BASE + GOLD_READOUT_SCREEN_AT),
            VGA_SCREEN_PITCH);
        item_id = fdps_shop_select_item((int) shop_index);

        if (item_id != BUY_CANCELLED) {
            item_record = fdps_get_item_record(item_id);
            item_type = (int) item_record->type;

            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
            fdps_draw_party_gold(
                (unsigned char *) (VGA_SCREEN_BASE + GOLD_READOUT_SCREEN_AT),
                VGA_SCREEN_PITCH);
            buy_target = fdps_shop_select_buy_target(item_id);
            message_row_offset = 0;

            if (buy_target != BUY_CANCELLED) {
                if (fdps_unit_can_equip_item(buy_target, item_id) != 0) {
                    auto_equip = 1;
                } else {
                    auto_equip = 0;
                }

                fdps_village_animate_window_zoom(screen_page,
                                                 WINDOW_ZOOM_OPEN);
                fdps_draw_party_gold(
                    (unsigned char *) (VGA_SCREEN_BASE
                                       + GOLD_READOUT_SCREEN_AT),
                    VGA_SCREEN_PITCH);

                if (item_type > BUY_LAST_WEAPON_TYPE) {
                    want_armor = 1;
                } else {
                    want_armor = 0;
                }
                equipped_slot = fdps_unit_find_equipped_slot(buy_target,
                                                             want_armor);
                trade_in_accepted = 0;
                data_fdps_dialog_last_action_value_param =
                    (int) item_record->price;

                if (equipped_slot != BUY_CANCELLED
                    && fdps_unit_can_equip_item(buy_target, item_id) != 0) {
                    message_row_offset = BUY_MESSAGE_ROWS_BELOW_PROMPT;
                    equipped_item_id = fdps_unit_get_item_id(buy_target,
                                                             equipped_slot);
                    equipped_record = fdps_get_item_record(equipped_item_id);
                    member_record = fdps_get_roster_record(buy_target);
                    data_fdps_dialog_last_action_text_id_param =
                        (int) member_record->char_id
                        + BUY_MEMBER_NAME_TEXT_BIAS;
                    data_fdps_dialog_subst_text_id_2 =
                        equipped_item_id + BUY_ITEM_NAME_TEXT_BASE;
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   MSG_TRADE_IN_OFFER,
                                   (unsigned char *) (VGA_SCREEN_BASE
                                                      + BUY_MESSAGE_SCREEN_AT),
                                   VGA_SCREEN_PITCH, BUY_TEXT_FG, BUY_TEXT_BG,
                                   BUY_TEXT_OUTLINE);
                    if (fdps_prompt_two_choice() == BUY_PROMPT_ACCEPTED) {
                        trade_in_accepted = 1;
                        data_fdps_dialog_last_action_value_param -=
                            (int) equipped_record->price * TRADE_IN_NUMERATOR
                            / TRADE_IN_DENOMINATOR;
                    }
                }

                if (trade_in_accepted != 0
                    || fdps_unit_item_count(buy_target) != BUY_BAG_CAPACITY) {
                    payment_settled = 0;

                    if (data_fdps_dialog_last_action_value_param > 0) {
                        if (data_fdps_dialog_last_action_value_param
                            > data_fdps_shared_party_total_gold) {
                            fdps_draw_text(
                                data_fdps_all_game_text_ptr, MSG_CANNOT_AFFORD,
                                (unsigned char *) (VGA_SCREEN_BASE
                                                   + BUY_MESSAGE_SCREEN_AT
                                                   + message_row_offset
                                                     * VGA_SCREEN_PITCH),
                                VGA_SCREEN_PITCH, BUY_TEXT_FG, BUY_TEXT_BG,
                                BUY_TEXT_OUTLINE);
                        } else {
                            fdps_draw_text(
                                data_fdps_all_game_text_ptr, MSG_BUY_OFFER,
                                (unsigned char *) (VGA_SCREEN_BASE
                                                   + BUY_MESSAGE_SCREEN_AT
                                                   + message_row_offset
                                                     * VGA_SCREEN_PITCH),
                                VGA_SCREEN_PITCH, BUY_TEXT_FG, BUY_TEXT_BG,
                                BUY_TEXT_OUTLINE);
                            if (fdps_prompt_two_choice()
                                == BUY_PROMPT_ACCEPTED) {
                                fdps_play_sfx(BUY_PAYMENT_SFX);
                                data_fdps_shared_party_total_gold -=
                                    data_fdps_dialog_last_action_value_param;
                                fdps_draw_party_gold(
                                    (unsigned char *)
                                        (VGA_SCREEN_BASE
                                         + GOLD_READOUT_SCREEN_AT),
                                    VGA_SCREEN_PITCH);
                                delay(BUY_PAYMENT_HOLD_MS);
                                payment_settled = 1;
                            }
                        }
                    } else {
                        data_fdps_dialog_last_action_value_param =
                            -data_fdps_dialog_last_action_value_param;
                        fdps_draw_text(
                            data_fdps_all_game_text_ptr, MSG_REFUND_OFFER,
                            (unsigned char *) (VGA_SCREEN_BASE
                                               + BUY_MESSAGE_SCREEN_AT
                                               + message_row_offset
                                                 * VGA_SCREEN_PITCH),
                            VGA_SCREEN_PITCH, BUY_TEXT_FG, BUY_TEXT_BG,
                            BUY_TEXT_OUTLINE);
                        if (fdps_prompt_two_choice() == BUY_PROMPT_ACCEPTED) {
                            fdps_play_sfx(BUY_PAYMENT_SFX);
                            data_fdps_shared_party_total_gold +=
                                data_fdps_dialog_last_action_value_param;
                            fdps_draw_party_gold(
                                (unsigned char *)
                                    (VGA_SCREEN_BASE + GOLD_READOUT_SCREEN_AT),
                                VGA_SCREEN_PITCH);
                            delay(BUY_PAYMENT_HOLD_MS);
                            payment_settled = 1;
                        }
                    }

                    if (payment_settled != 0) {
                        if (trade_in_accepted != 0) {
                            fdps_unit_remove_item(buy_target, equipped_slot);
                        }
                        fdps_unit_add_item(buy_target, item_id);
                        if (auto_equip != 0) {
                            bag_count = fdps_unit_item_count(buy_target);
                            fdps_unit_equip_slot(buy_target, bag_count - 1);
                        }
                        fdps_unit_recompute_combat_stats(buy_target);
                    }
                } else {
                    fdps_draw_text(
                        data_fdps_all_game_text_ptr, MSG_BAG_IS_FULL,
                        (unsigned char *) (VGA_SCREEN_BASE
                                           + BUY_MESSAGE_SCREEN_AT
                                           + message_row_offset
                                             * VGA_SCREEN_PITCH),
                        VGA_SCREEN_PITCH, BUY_TEXT_FG, BUY_TEXT_BG,
                        BUY_TEXT_OUTLINE);
                }
            }
        }
    }
}
