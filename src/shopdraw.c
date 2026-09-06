/* shopdraw.c -- the drawing half of the village shop screen.
 *
 * See shopdraw.h for what the file covers.  Everything here is a painter: it
 * reads the item and roster tables through their accessors, blits captions out
 * of the Command.cel sheet and puts figures down through fdps_draw_number, and
 * owns no state of its own.
 */
#include "fdpstype.h"
#include "gamedata.h"
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
