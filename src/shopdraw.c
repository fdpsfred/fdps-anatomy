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
#include "roster.h"
#include "shopdraw.h"
#include "sprite.h"
#include "table.h"
#include "text.h"
#include "unititem.h"

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

/* ------------------------------------------------------------------------
   fdps_shop_draw_member_entry, 00033230
   ------------------------------------------------------------------------ */

/* The walk cycle, MOV EBX,0x6 / DIV EBX / AND EAX,0x3 at 0003324b..00033259
   and the CMP ...,0x3 at 0003325f: six timer ticks a frame and the top of the
   two-bit phase folded back onto 1, so the icon rocks 0, 1, 2, 1 instead of
   snapping back to 0.  fdps_draw_map_unit_panel folds the same phase the same
   way (mapcur.c).

   THE DIVIDE IS UNSIGNED, DIV and not IDIV, which is why
   data_fdps_timer_tick_counter is declared unsigned (gamedata.h): past
   0x7fffffff the icon keeps walking instead of standing on one frame. */
#define MEMBER_ICON_WALK_FRAME_TICKS 6
#define MEMBER_ICON_WALK_FRAME_FOLDED 3
#define MEMBER_ICON_WALK_FRAME_FOLD_TO 1

/* Twelve sprites to a roster member in the .CEL sprite cache -- four facings
   of three walk frames -- so the set number is the roster index and the frame
   is added on top (IMUL EAX,[EBP+0x1c],0xc at 0003326c).  The cache block's
   offset table starts at the block's own base and not at a .CEL's +0xf, and
   an entry is the offset from that base to the sprite's stream (rsrc.h). */
#define MEMBER_SPRITES_PER_CACHE_SLOT 0x0c
#define MEMBER_CEL_ENTRY_BYTES 4

/* The icon and the two Command.cel cells under it.  0x2a is the sandy mound
   the icon stands on and 0x2b its right tail, one sprite width apart on row
   0x11 and starting two columns LEFT of dst (SUB EAX,0x2 at 00033280 against
   ADD EAX,0x17 at 00033299), so the entry's cell is wider than the icon. */
#define MEMBER_ICON_W 0x18
#define MEMBER_ICON_H 0x18
#define MEMBER_MOUND_ROW 0x11
#define MEMBER_MOUND_COLUMN (-2)
#define MEMBER_MOUND_TAIL_COLUMN 0x17
#define MEMBER_MOUND_SPRITE 0x2a
#define MEMBER_MOUND_TAIL_SPRITE 0x2b

/* The blend descriptor mode 9 reads, three dwords in the order the kernel
   takes them (rleblend.h), built on the stack exactly as the original builds
   it at 000332df..000332ed.  The two table addresses are the symbols and not
   the original's 0x653f0 and 0x643f0: the rebuild does not place either table
   where the original placed it. */
#define BLEND_DESC_SHADE_RAMP 0
#define BLEND_DESC_LEVEL 1
#define BLEND_DESC_CUBE 2
#define BLEND_DESC_SLOTS 3

/* The ghosting gate and how heavy the ghost is: CMP [0x00069cf4],0x17 / JL at
   000332c4 and CMP EAX,0x1 / JZ at 000332d8 over the record's character id,
   then PUSH 0x9 for the mode and 0xa for the level.  Character id 1 is
   法蓮娜, whom the caller's confirm gate locks out from the same chapter index
   on (roster.h has the same two literals for the same member).  The chapter
   test is signed and has no upper bound. */
#define MEMBER_GHOST_FIRST_CHAPTER 0x17
#define MEMBER_GHOST_CHAR_ID 1
#define MEMBER_GHOST_BLEND_LEVEL 0x0a
#define MEMBER_BLIT_MODE_OPAQUE 0
#define MEMBER_BLIT_MODE_TRANSLUCENT 9
#define MEMBER_BLIT_NO_OPERAND 0

/* The name.  Its message id is the record's character id plus one (INC EAX at
   0003336a) and it goes at row 9, column 0x1e -- LEA EAX,[EAX + EAX*0x8] over
   the pitch at 00033355.

   THE PITCH IT IS HANDED IS THE LITERAL 0x138, PUSH 0x138 at 0003334d, while
   the row it is placed on is the pitch ARGUMENT like everything else here.  So
   only fdps_draw_text's own row stepping is on 312: the cannot-equip line at
   00033540 is handed [EBP+0x18] instead, and the two disagree on any surface
   the one caller does not compose. */
#define MEMBER_NAME_TEXT_BIAS 1
#define MEMBER_NAME_ROW 9
#define MEMBER_NAME_COLUMN 0x1e
#define MEMBER_NAME_PITCH 0x138
#define MEMBER_NAME_FG_COLOR 0xd0

/* The cannot-equip line, message 0x1f7 at row 0x23 column 0xe in its own
   glyph colour, and the two colours both messages share.  A zero background
   leaves the glyph cell unfilled, so the text composites over the icon row the
   entry has already drawn (text.h). */
#define MEMBER_CANNOT_EQUIP_TEXT_ID 0x1f7
#define MEMBER_CANNOT_EQUIP_ROW 0x23
#define MEMBER_CANNOT_EQUIP_COLUMN 0x0e
#define MEMBER_CANNOT_EQUIP_FG_COLOR 0x2b
#define MEMBER_TEXT_BG_COLOR 0
#define MEMBER_TEXT_OUTLINE_COLOR 0x6d

/* The two labelled columns.  Command.cel sprite 0x2c carries "EV :" over
   "HIT:" and 0x2f "AP :" over "DP :", one caption cell holding both lines of
   its column, so the left column is evade over hit and the right one attack
   over defence.  The captions sit on the top row and the figures three columns
   inside them; the second line is nine rows lower. */
#define MEMBER_STAT_TOP_ROW 0x22
#define MEMBER_STAT_BOTTOM_ROW 0x2b
#define MEMBER_EV_HIT_CAPTION_SPRITE 0x2c
#define MEMBER_EV_HIT_CAPTION_COLUMN 0x04
#define MEMBER_AP_DP_CAPTION_SPRITE 0x2f
#define MEMBER_AP_DP_CAPTION_COLUMN 0x33
#define MEMBER_LEFT_FIGURE_COLUMN 0x1a
#define MEMBER_RIGHT_FIGURE_COLUMN 0x49
#define MEMBER_STAT_DIGITS 3
#define MEMBER_FIGURE_SHOW_PLUS 0

/* Which Number.cel colour row each figure is drawn in, the three literals
   stored into data_fdps_number_glyph_color_row across 000333bd..000333e7 and
   its three twins, and the 0 put back at 00033534 after the last figure. */
#define MEMBER_STAT_COLOR_ROW_LOWER 2
#define MEMBER_STAT_COLOR_ROW_HIGHER 3
#define MEMBER_STAT_COLOR_ROW_EQUAL 0

/* The four ints fdps_roster_preview_combat_stats_with_item writes, in its own
   order (roster.h) -- which is not the order they are drawn in. */
#define PREVIEW_STAT_ATTACK 0
#define PREVIEW_STAT_DEFENSE 1
#define PREVIEW_STAT_HIT 2
#define PREVIEW_STAT_EVADE 3
#define PREVIEW_STAT_SLOTS 4

/* 00033230.  Four stack arguments, caller-cleaned: the one call site, 0003303d
   inside fdps_shop_select_buy_target, pushes four dwords right to left -- the
   item id, the roster index, PUSH 0x138 and the cell pointer -- and follows the
   CALL with ADD ESP,0x10, while the body reads them at [EBP+0x14], [EBP+0x18],
   [EBP+0x1c] and [EBP+0x20] behind PUSH EBX/ESI/EDI/EBP and the return address.
   RET carries no immediate and the call site does not look at EAX.

   THE FOUR STATS ARE READ SIGNED AND COMPARED SIGNED.  MOVSX word ptr
   [EAX+0x4e] at 000333ae and its three twins, then JLE at 000333bb and JGE at
   000333cf: a member whose current stat is negative -- which a cursed weapon's
   modifier reaches, since fdps_roster_recompute_combat_stats does not clamp --
   sorts below every positive preview and colours its figure 3.  Read unsigned
   it would sort above and be coloured 2 instead.

   THE COLOUR SELECTOR IS ONLY TOUCHED ON THE PATH THAT DRAWS FIGURES.  The
   cannot-equip branch at 00033540 writes data_fdps_number_glyph_color_row
   neither before nor after, so whatever the caller left there stands; zeroing
   it on the way in or out of this function would be visible in the next
   figure some other screen draws (gamedata.h).

   THE TWO COLUMNS ARE DRAWN LEFT CAPTION, ITS TWO FIGURES, RIGHT CAPTION, ITS
   TWO FIGURES, not both captions and then all four figures.  A figure's column
   is inside its caption's 25-pixel cell, so the caption has to go down before
   the figures beside it or it paints over them.

   Two values are used after a CALL.  fdps_get_roster_record's is parked in
   [EBP-0x10] at 00033248 and every read of the member -- the character id
   twice and the four stat words -- loads it back from there.
   fdps_unit_can_equip_item's is tested where it lands, TEST EAX,EAX / JZ at
   0003338a with nothing between it and the CALL.  Nothing else here returns
   anything this function reads: fdps_roster_preview_combat_stats_with_item
   answers through its out_stats pointer, fdps_draw_text hands back a pen this
   caller drops, and neither blitter nor fdps_draw_number returns a value. */
void fdps_shop_draw_member_entry(unsigned char *dst, int pitch,
                                 int roster_index, int item_id)
{
    struct fdps_unit_record *member;
    /* The four figures as they would be WITH the item on, filled by the call
       below; the member's own record still holds what they are now. */
    int preview_stats[PREVIEW_STAT_SLOTS];
    int blend_desc[BLEND_DESC_SLOTS];
    int walk_frame;
    int sprite_index;
    unsigned char *icon_stream;
    /* The stat the member has now, reloaded from the record before each of the
       four comparisons -- one slot in the original, [EBP-0x4]. */
    int current_stat;

    member = fdps_get_roster_record(roster_index);

    walk_frame = (int) ((data_fdps_timer_tick_counter
                         / MEMBER_ICON_WALK_FRAME_TICKS) & 3);
    if (walk_frame == MEMBER_ICON_WALK_FRAME_FOLDED) {
        walk_frame = MEMBER_ICON_WALK_FRAME_FOLD_TO;
    }
    sprite_index = roster_index * MEMBER_SPRITES_PER_CACHE_SLOT + walk_frame;

    fdps_blit_command_sprite(dst + MEMBER_MOUND_ROW * pitch
                                 + MEMBER_MOUND_COLUMN,
                             pitch, MEMBER_MOUND_SPRITE);
    fdps_blit_command_sprite(dst + MEMBER_MOUND_ROW * pitch
                                 + MEMBER_MOUND_TAIL_COLUMN,
                             pitch, MEMBER_MOUND_TAIL_SPRITE);

    icon_stream = data_fdps_cel_sprite_cache_ptr
        + *(int *) (data_fdps_cel_sprite_cache_ptr
                    + sprite_index * MEMBER_CEL_ENTRY_BYTES);

    if (data_fdps_chapter_current_chapter_id >= MEMBER_GHOST_FIRST_CHAPTER
        && member->char_id == MEMBER_GHOST_CHAR_ID) {
        blend_desc[BLEND_DESC_SHADE_RAMP] =
            (int) data_fdps_palette_shade_ramp_table;
        blend_desc[BLEND_DESC_LEVEL] = MEMBER_GHOST_BLEND_LEVEL;
        blend_desc[BLEND_DESC_CUBE] = (int) data_fdps_inverse_palette_cube;
        fdps_blit_dispatch(icon_stream, dst, MEMBER_ICON_W, MEMBER_ICON_H,
                           pitch, (unsigned int) blend_desc,
                           MEMBER_BLIT_MODE_TRANSLUCENT);
    } else {
        fdps_blit_dispatch(icon_stream, dst, MEMBER_ICON_W, MEMBER_ICON_H,
                           pitch, MEMBER_BLIT_NO_OPERAND,
                           MEMBER_BLIT_MODE_OPAQUE);
    }

    fdps_roster_preview_combat_stats_with_item(roster_index, item_id,
                                               preview_stats);

    fdps_draw_text(data_fdps_all_game_text_ptr,
                   (int) member->char_id + MEMBER_NAME_TEXT_BIAS,
                   dst + MEMBER_NAME_ROW * pitch + MEMBER_NAME_COLUMN,
                   MEMBER_NAME_PITCH, MEMBER_NAME_FG_COLOR,
                   MEMBER_TEXT_BG_COLOR, MEMBER_TEXT_OUTLINE_COLOR);

    if (fdps_unit_can_equip_item(roster_index, item_id) == 0) {
        fdps_draw_text(data_fdps_all_game_text_ptr,
                       MEMBER_CANNOT_EQUIP_TEXT_ID,
                       dst + MEMBER_CANNOT_EQUIP_ROW * pitch
                           + MEMBER_CANNOT_EQUIP_COLUMN,
                       pitch, MEMBER_CANNOT_EQUIP_FG_COLOR,
                       MEMBER_TEXT_BG_COLOR, MEMBER_TEXT_OUTLINE_COLOR);
        return;
    }

    /* Left column: "EV :" over "HIT:". */
    fdps_blit_command_sprite(dst + MEMBER_STAT_TOP_ROW * pitch
                                 + MEMBER_EV_HIT_CAPTION_COLUMN,
                             pitch, MEMBER_EV_HIT_CAPTION_SPRITE);

    current_stat = (int) member->ev;
    if (preview_stats[PREVIEW_STAT_EVADE] < current_stat) {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_LOWER;
    } else if (current_stat < preview_stats[PREVIEW_STAT_EVADE]) {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_HIGHER;
    } else {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_EQUAL;
    }
    fdps_draw_number(dst + MEMBER_STAT_TOP_ROW * pitch
                         + MEMBER_LEFT_FIGURE_COLUMN,
                     pitch, preview_stats[PREVIEW_STAT_EVADE],
                     MEMBER_STAT_DIGITS, MEMBER_FIGURE_SHOW_PLUS);

    current_stat = (int) member->hit;
    if (preview_stats[PREVIEW_STAT_HIT] < current_stat) {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_LOWER;
    } else if (current_stat < preview_stats[PREVIEW_STAT_HIT]) {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_HIGHER;
    } else {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_EQUAL;
    }
    fdps_draw_number(dst + MEMBER_STAT_BOTTOM_ROW * pitch
                         + MEMBER_LEFT_FIGURE_COLUMN,
                     pitch, preview_stats[PREVIEW_STAT_HIT],
                     MEMBER_STAT_DIGITS, MEMBER_FIGURE_SHOW_PLUS);

    /* Right column: "AP :" over "DP :". */
    fdps_blit_command_sprite(dst + MEMBER_STAT_TOP_ROW * pitch
                                 + MEMBER_AP_DP_CAPTION_COLUMN,
                             pitch, MEMBER_AP_DP_CAPTION_SPRITE);

    current_stat = (int) member->ap;
    if (preview_stats[PREVIEW_STAT_ATTACK] < current_stat) {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_LOWER;
    } else if (current_stat < preview_stats[PREVIEW_STAT_ATTACK]) {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_HIGHER;
    } else {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_EQUAL;
    }
    fdps_draw_number(dst + MEMBER_STAT_TOP_ROW * pitch
                         + MEMBER_RIGHT_FIGURE_COLUMN,
                     pitch, preview_stats[PREVIEW_STAT_ATTACK],
                     MEMBER_STAT_DIGITS, MEMBER_FIGURE_SHOW_PLUS);

    current_stat = (int) member->dp;
    if (preview_stats[PREVIEW_STAT_DEFENSE] < current_stat) {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_LOWER;
    } else if (current_stat < preview_stats[PREVIEW_STAT_DEFENSE]) {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_HIGHER;
    } else {
        data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_EQUAL;
    }
    fdps_draw_number(dst + MEMBER_STAT_BOTTOM_ROW * pitch
                         + MEMBER_RIGHT_FIGURE_COLUMN,
                     pitch, preview_stats[PREVIEW_STAT_DEFENSE],
                     MEMBER_STAT_DIGITS, MEMBER_FIGURE_SHOW_PLUS);

    data_fdps_number_glyph_color_row = MEMBER_STAT_COLOR_ROW_EQUAL;
}
