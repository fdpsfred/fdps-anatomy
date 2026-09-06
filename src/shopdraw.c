/* shopdraw.c -- the drawing half of the village shop screen.
 *
 * See shopdraw.h for what the file covers.  Everything here is a painter: it
 * reads the item and roster tables through their accessors, blits captions out
 * of the Command.cel sheet and puts figures down through fdps_draw_number, and
 * owns no state of its own.
 *
 * malloc and free come from <stdlib.h> and inp from <conio.h>, which is where
 * Watcom 10.0a declares each of them, and all three are real calls in the
 * original -- CALL 0x0003d375 at 000330f1, CALL 0x0003d478 at 0003321c and
 * CALL 0x0003d4e4 at 000331d4 and 000331e5 -- because the flag set carries no
 * -oi (rebuild_info/build_flags.md), so the plain declarations reproduce them.
 */
#include <stdlib.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "palcycle.h"
#include "shopdraw.h"
#include "sprite.h"
#include "table.h"
#include "text.h"

/* Where the item names sit in the game's one text block: entry 0xc9 plus the
   item id, ADD EAX,0xc9 at 00032a90.  fdps_draw_unit_inventory adds the same
   0xc9 to reach the same names for the carried-item list. */
#define ENTRY_NAME_TEXT_BASE 0xc9

/* The three colours the name is drawn in, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d at
   00032a80, 00032a7e and 00032a7c.  A zero background means the glyph cell is
   not filled before the glyph goes down, so the name composites over whatever
   the caller already drew there (text.h). */
#define ENTRY_TEXT_FG_COLOR 0xd0
#define ENTRY_TEXT_BG_COLOR 0
#define ENTRY_TEXT_OUTLINE_COLOR 0x6d

/* The entry's geometry, straight out of the address arithmetic.  Both captions
   go at column 0x54 (ADD EAX,0x54 at 00032ac0 and 00032ad2); the price
   caption's row is one ABOVE the entry's own (SUB EAX,dword ptr [EBP+0x1c], the
   pitch, at 00032acf) while the second line's is eight below it (LEA
   EAX,[EAX*0x8] over the pitch at 00032ab6).  The price figure has a column of
   its own, 0x64 (ADD EAX,0x64 at 00032af7), and the second line's figure is
   0x16 to the right of its caption (ADD EAX,0x16 at 00032b42 and its three
   twins) -- the same 0x16 bias fdps_draw_unit_inventory puts its figure at. */
#define ENTRY_CAPTION_COLUMN 0x54
#define ENTRY_PRICE_CAPTION_ROWS_UP 1
#define ENTRY_SECOND_LINE_ROWS_DOWN 8
#define ENTRY_PRICE_FIGURE_COLUMN 0x64
#define ENTRY_FIGURE_COLUMN_BIAS 0x16

/* The two field widths, PUSH 0x5 at 00032ae1 for the price and PUSH 0x4 at
   00032b31 and its three twins for the headline figure, and the show_plus flag
   every one of the five calls zeroes (XOR EAX,EAX / PUSH EAX in front of each).
   A price that reaches 100000 is drawn as five '?' glyphs and a headline figure
   that reaches 10000 as four, which is fdps_draw_number's own overflow
   behaviour (text.h) and not something this routine tests for. */
#define ENTRY_PRICE_DIGITS 5
#define ENTRY_STAT_DIGITS 4
#define ENTRY_FIGURE_SHOW_PLUS 0

/* The Command.cel captions.  0x29 is the money mark fdps_draw_party_gold puts
   in front of the purse, and the other five are the same caption set
   fdps_draw_unit_inventory picks from: attack power, defence power, HP
   recovery, MP recovery and the catch-all "????". */
#define ENTRY_CAPTION_PRICE 0x29
#define ENTRY_CAPTION_HP_RECOVERY 0x3e
#define ENTRY_CAPTION_MP_RECOVERY 0x3f
#define ENTRY_CAPTION_AP 0x40
#define ENTRY_CAPTION_DP 0x41
#define ENTRY_CAPTION_PLAIN 0x43

/* The two upper bounds the type byte is tested against, CMP EAX,0x15 at
   00032b15 and CMP EAX,0x27 at 00032b5d.  Types 1..0x15 are the weapons and
   0x16..0x27 the armour; assets/items.md lists what each code is. */
#define ITEM_TYPE_WEAPON_LAST 0x15
#define ITEM_TYPE_ARMOUR_LAST 0x27

/* The two use_effect codes that put a recovery amount on the second line, CMP
   EAX,0xb at 00032ba4 and CMP EAX,0xc at 00032be8.  Every other effect draws
   the catch-all caption and no figure at all. */
#define ITEM_USE_RESTORE_HP 0x0b
#define ITEM_USE_RESTORE_MP 0x0c

/* 00032a70.  Three stack arguments, caller-cleaned: the only call site,
   00032976 inside fdps_shop_select_item, pushes three dwords right to left --
   PUSH 0x138, PUSH EAX, PUSH dword ptr [EAX + EBP + -0x58] -- and follows the
   CALL with ADD ESP,0xc, while the body reads them at [EBP+0x14], [EBP+0x18]
   and [EBP+0x1c] behind PUSH EBX/ESI/EDI/EBP and the return address.  RET
   carries no immediate and nothing reads EAX afterwards.

   THE PRICE IS READ UNSIGNED AND THE THREE STATS SIGNED, which is the one place
   a uniform reading of the record goes wrong: MOV AX,word ptr [EAX+0x13] / AND
   EAX,0xffff at 00032ae6 against MOVSX EAX,word ptr [EAX+n] at 00032b36,
   00032b7c, 00032bc3 and 00032c07.  Shipped ITEM.DAT prices reach 45000, past
   what a signed 16-bit field holds, so a price read as a short draws a minus
   sign and five digits of nonsense in the shop list; the stats really are
   signed and a cursed weapon's negative attack power has to keep its sign.
   struct fdps_item_effect already declares both that way, so what has to
   survive here is only that nothing casts either of them.

   THE TYPE TEST IS TWO COMPARES AND NOT ONE: CMP byte ptr [EAX],0x0 / JBE at
   00032b06 steers a zero type away from the weapon branch before CMP EAX,0x15 /
   JLE at 00032b15 is reached, so type 0 falls through to the armour branch and
   a blank record is captioned "+DP" rather than "+AP".  Writing the weapon test
   as a plain type <= 0x15 loses that.  fdps_draw_unit_inventory classifies the
   same byte twice over for the same reason (statwin.h).

   The one value used after a CALL is fdps_get_item_record's: MOV dword ptr
   [EBP + -0x8],EAX at 00032ab0 parks EAX in the record slot and every read of
   the record loads it back from there.  Nothing else here looks at a return
   value -- fdps_draw_text hands back a pen this caller drops, and neither
   fdps_blit_command_sprite nor fdps_draw_number returns anything. */
void fdps_shop_draw_item_entry(int item_id, unsigned char *dest, int pitch)
{
    struct fdps_item_effect *item;
    unsigned char *second_line;

    fdps_draw_text(data_fdps_all_game_text_ptr,
                   item_id + ENTRY_NAME_TEXT_BASE, dest, pitch,
                   ENTRY_TEXT_FG_COLOR, ENTRY_TEXT_BG_COLOR,
                   ENTRY_TEXT_OUTLINE_COLOR);

    item = fdps_get_item_record(item_id);
    second_line = dest + ENTRY_SECOND_LINE_ROWS_DOWN * pitch
                  + ENTRY_CAPTION_COLUMN;

    /* The money mark first and the figure over it: the caption is 25 by 22 and
       starts one row up at column 0x54, the figure is at column 0x64 on the
       entry's own row, so the two overlap and the order is what leaves the
       digits on top. */
    fdps_blit_command_sprite(dest + ENTRY_CAPTION_COLUMN
                             - ENTRY_PRICE_CAPTION_ROWS_UP * pitch,
                             pitch, ENTRY_CAPTION_PRICE);
    fdps_draw_number(dest + ENTRY_PRICE_FIGURE_COLUMN, pitch, item->price,
                     ENTRY_PRICE_DIGITS, ENTRY_FIGURE_SHOW_PLUS);

    if (item->type > 0 && item->type <= ITEM_TYPE_WEAPON_LAST) {
        fdps_blit_command_sprite(second_line, pitch, ENTRY_CAPTION_AP);
        fdps_draw_number(second_line + ENTRY_FIGURE_COLUMN_BIAS, pitch,
                         item->ap, ENTRY_STAT_DIGITS, ENTRY_FIGURE_SHOW_PLUS);
    } else if (item->type <= ITEM_TYPE_ARMOUR_LAST) {
        fdps_blit_command_sprite(second_line, pitch, ENTRY_CAPTION_DP);
        fdps_draw_number(second_line + ENTRY_FIGURE_COLUMN_BIAS, pitch,
                         item->dp, ENTRY_STAT_DIGITS, ENTRY_FIGURE_SHOW_PLUS);
    } else if (item->use_effect == ITEM_USE_RESTORE_HP) {
        fdps_blit_command_sprite(second_line, pitch,
                                 ENTRY_CAPTION_HP_RECOVERY);
        fdps_draw_number(second_line + ENTRY_FIGURE_COLUMN_BIAS, pitch,
                         item->use_amount, ENTRY_STAT_DIGITS,
                         ENTRY_FIGURE_SHOW_PLUS);
    } else if (item->use_effect == ITEM_USE_RESTORE_MP) {
        fdps_blit_command_sprite(second_line, pitch,
                                 ENTRY_CAPTION_MP_RECOVERY);
        fdps_draw_number(second_line + ENTRY_FIGURE_COLUMN_BIAS, pitch,
                         item->use_amount, ENTRY_STAT_DIGITS,
                         ENTRY_FIGURE_SHOW_PLUS);
    } else {
        fdps_blit_command_sprite(second_line, pitch, ENTRY_CAPTION_PLAIN);
    }
}

/* ------------------------------------------------------------------------
   fdps_shop_render_buy_target_frame, 000330e0
   ------------------------------------------------------------------------ */

/* The page the frame is composed on: 312 bytes to a row, 76 rows, and the
   0x5ca0 the routine mallocs is exactly that product -- the dimensions of the
   ShopWin.Cel sheet the window sprite comes out of.  It is allocated and freed
   inside the call, so no state carries in the pixels, and it is never cleared
   after the malloc: the window sprite writes every byte of it that reaches the
   screen. */
#define BUY_TARGET_PAGE_PITCH 0x138
#define BUY_TARGET_PAGE_BYTES 0x5ca0

/* The window as it reaches the visible screen: 304 by 67 read from page offset
   0x3ad, which is page (row 3, column 5), and written at 0xa9c48, which is
   screen (8, 125) on the mode 13h framebuffer.  0xa0000 stays a literal
   because it is where the adapter answers and not the address of anything the
   linker places, and the page offsets are literals for the same reason -- they
   are positions inside a block this function allocated.

   THE STRIP'S OWN TOP THREE ROWS AND LEFTMOST FIVE COLUMNS NEVER REACH THE
   SCREEN, because the copy starts three rows and five columns into the page;
   page rows 67..69, which the strip does not cover at all, supply the bottom
   of the picture in their place. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define BUY_TARGET_WINDOW_PAGE_AT 0x3ad
#define BUY_TARGET_WINDOW_SCREEN_AT 0x9c48
#define BUY_TARGET_WINDOW_W 0x130
#define BUY_TARGET_WINDOW_H 0x43

/* Sprite 0 of ShopWin.Cel is the window frame and sprite 2 of SelBar.Cel is
   the bar behind the chosen column -- a 101 by 17 bar, 101 being the 0x65
   column stride of the three-column grid, so it covers exactly one column.
   The bar's row is fixed at 0x14 and only its column is an argument.  Both go
   through fdps_cel_blit_sprite in its plain opaque mode with no operand. */
#define BUY_TARGET_WINDOW_SPRITE 0
#define BUY_TARGET_SELECTION_BAR_SPRITE 2
#define BUY_TARGET_SELECTION_BAR_Y 0x14
#define BUY_TARGET_BLIT_OPERAND 0
#define BUY_TARGET_BLIT_MODE 0

/* The strip of the caller's grid that goes over the window and the bar: a full
   page width by 0x43 rows, through the colour-keyed blit, so palette index 0
   in the entry grid lets the window and the bar show through (blit.h). */
#define BUY_TARGET_STRIP_W 0x138
#define BUY_TARGET_STRIP_H 0x43

/* How many entries one visible row of the grid holds.  It is the whole of the
   down arrow's test: the arrow means there is another row of three below the
   one on show. */
#define BUY_TARGET_VISIBLE_ENTRIES 3

/* The two scroll arrows: Command.cel sprites 0x44/0x45 for up and 0x46/0x47
   for down, the second of each pair being the lit frame.  Which one is drawn
   is (tick / 5) & 1, so each frame of the blink lasts five timer ticks.

   THE DIVIDE IS UNSIGNED, DIV EBX at 00033174 and not IDIV, which is why
   data_fdps_timer_tick_counter is declared unsigned (gamedata.h): a counter
   that has run past 0x7fffffff keeps blinking at five ticks a frame instead of
   sticking on one of them.

   Page offset 0x6ae is page (row 5, column 150) and 0x4e96 is page (row 64,
   column 150).  A Command.cel cell is 25 by 22 and the down arrow's would run
   nine rows past the page, but all four arrow sprites are 7 by 5 triangles in
   the top-left of their cell and every row below the fifth is a single skip
   run, which stores nothing -- so neither blit writes outside the page
   (resource_info/cel.md). */
#define BUY_TARGET_ARROW_BLINK_TICKS 5
#define BUY_TARGET_ARROW_UP_SPRITE 0x44
#define BUY_TARGET_ARROW_DOWN_SPRITE 0x46
#define BUY_TARGET_ARROW_UP_PAGE_AT 0x6ae
#define BUY_TARGET_ARROW_DOWN_PAGE_AT 0x4e96

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, which is what the screen copy is timed against. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* 000330e0.  Four stack arguments, caller-cleaned: all five call sites, at
   00032d8f, 00032e47, 00032ee8, 00032f97 and 00033086 inside
   fdps_shop_select_buy_target, push four dwords right to left and follow the
   CALL with ADD ESP,0x10, while the body reads them at [EBP+0x14], [EBP+0x18],
   [EBP+0x1c] and [EBP+0x20] behind PUSH EBX/ESI/EDI/EBP and the return
   address.  RET carries no immediate and no call site looks at EAX.

   THE TWO RETRACE SPINS ARE NOT A PLAIN "WAIT FOR BLANKING".
   fdps_cycle_ui_palette has itself just waited for the retrace to begin
   (palcycle.h), so the first spin here normally falls straight through and
   only the second one blocks: the screen copy starts as the active display
   resumes, with the 125 scan lines above the window as head room, rather than
   inside the blanking interval.  Dropping either spin, or reordering them
   against the palette call, moves where the copy lands in the frame.

   THE DOWN ARROW'S TEST IS SIGNED AND STRICT, CMP EAX,dword ptr [0x00064114] /
   JGE at 000331a5, so a roster of exactly scroll_top + 3 draws no arrow: the
   window is already showing the last row.

   The one value used after a CALL is malloc's.  MOV dword ptr [EBP+-0x8],EAX
   at 000330f9 parks the page and every later use loads it back from there; it
   is used unchecked, there is no test for NULL anywhere, and it is freed at
   the end of the call.  Nothing else here reads a return value -- the four
   drawing calls and fdps_cycle_ui_palette return nothing -- except inp, whose
   AL the two spins test and keep nowhere. */
void fdps_shop_render_buy_target_frame(unsigned char *list_bitmap,
                                       int cursor_x, int src_row,
                                       int scroll_top)
{
    /* The 312 x 76 page the frame is composed on, back to front. */
    unsigned char *page;
    /* 0 or 1: which frame of the two-frame scroll arrows this pass shows. */
    int arrow_blink_phase;

    page = (unsigned char *) malloc(BUY_TARGET_PAGE_BYTES);

    fdps_cel_blit_sprite(data_fdps_village_window_sheet_ptr,
                         BUY_TARGET_WINDOW_SPRITE, page,
                         BUY_TARGET_PAGE_PITCH, 0, 0,
                         BUY_TARGET_BLIT_OPERAND, BUY_TARGET_BLIT_MODE);
    fdps_cel_blit_sprite(data_fdps_selection_bar_sheet_ptr,
                         BUY_TARGET_SELECTION_BAR_SPRITE, page,
                         BUY_TARGET_PAGE_PITCH, cursor_x,
                         BUY_TARGET_SELECTION_BAR_Y,
                         BUY_TARGET_BLIT_OPERAND, BUY_TARGET_BLIT_MODE);
    fdps_blit_transparent_rect(list_bitmap + src_row * BUY_TARGET_PAGE_PITCH,
                               BUY_TARGET_PAGE_PITCH, page,
                               BUY_TARGET_PAGE_PITCH, BUY_TARGET_STRIP_W,
                               BUY_TARGET_STRIP_H);

    arrow_blink_phase = (int) ((data_fdps_timer_tick_counter
                                / BUY_TARGET_ARROW_BLINK_TICKS) & 1);

    if (scroll_top != 0) {
        fdps_blit_command_sprite(page + BUY_TARGET_ARROW_UP_PAGE_AT,
                                 BUY_TARGET_PAGE_PITCH,
                                 arrow_blink_phase
                                     + BUY_TARGET_ARROW_UP_SPRITE);
    }

    if (scroll_top + BUY_TARGET_VISIBLE_ENTRIES
        < data_fdps_roster_member_count) {
        fdps_blit_command_sprite(page + BUY_TARGET_ARROW_DOWN_PAGE_AT,
                                 BUY_TARGET_PAGE_PITCH,
                                 arrow_blink_phase
                                     + BUY_TARGET_ARROW_DOWN_SPRITE);
    }

    fdps_cycle_ui_palette();

    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        /* Spin until the retrace begins.  fdps_cycle_ui_palette has just
           waited for the same edge, so this one normally falls through. */
    }
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        /* And until it ends, so the copy runs into the active display. */
    }

    fdps_blit_rect((unsigned int) (page + BUY_TARGET_WINDOW_PAGE_AT),
                   BUY_TARGET_PAGE_PITCH,
                   (void *) (VGA_SCREEN_BASE + BUY_TARGET_WINDOW_SCREEN_AT),
                   VGA_SCREEN_PITCH, BUY_TARGET_WINDOW_W,
                   BUY_TARGET_WINDOW_H);

    free(page);
}
