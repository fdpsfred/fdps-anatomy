/* spellmnu.c -- the spell list UI and the in-battle spell command: the page
 * drawer the status window and both spell menus share, the menus' own input
 * loops, and the command that runs a chosen spell
 * (rebuild_info/code_layout.md).
 *
 * The list is eight rows of a flat array of spell ids, collected out of the
 * unit record's five-byte bitmap by unitstat.c and never cached; everything
 * this file draws with comes from sprite.c, text.c and table.c, and the two
 * .CEL sheet pointers and the global text block belong to gamedata.h.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "sprite.h"
#include "table.h"
#include "text.h"
#include "unitstat.h"
#include "spellmnu.h"

/* The stack buffer the spell ids are collected into: SUB ESP,0x40 at 000276f6
   buys 64 bytes, of which the six scalars at [EBP-0x18] through [EBP-0x04]
   take 24 and the array at [EBP-0x40] the remaining 40.  Forty is also the
   most ids the collector can ever write -- five bitmap bytes of eight bits --
   so the buffer is exactly full-sized and nothing bounds the write
   (unitstat.h). */
#define SPELL_LIST_ID_BUFFER_BYTES 40

/* One page of the list and the distance between two rows in scanlines: CMP
   dword ptr [EBP-0x14],0x8 at 00027720 for the eight rows, and the four IMUL
   ...,0x11 that build the rows' y coordinates. */
#define SPELL_LIST_ROWS 8
#define SPELL_LIST_ROW_PITCH 0x11

/* Where the four pieces of a row sit inside it.  The selection bar and the
   name share scanline +8; the spell icon is on +9 and the MP caption on +0x0d,
   each of them the top scanline of a 22-tall Command.cel sprite rather than a
   baseline.  x is 0 for the bar, 3 for the icon, 0x13 for the name and 0x66
   for the caption, and the MP figure stands 0x16 to the right of the caption's
   own left edge (ADD EAX,0x16 at 00027825, off the saved caption address and
   not recomputed from dest). */
#define SPELL_LIST_SEL_BAR_X 0
#define SPELL_LIST_SEL_BAR_Y 8
#define SPELL_LIST_ICON_X 3
#define SPELL_LIST_ICON_Y 9
#define SPELL_LIST_NAME_X 0x13
#define SPELL_LIST_NAME_Y 8
#define SPELL_LIST_MP_CAPTION_X 0x66
#define SPELL_LIST_MP_CAPTION_Y 0x0d
#define SPELL_LIST_MP_FIGURE_X_BIAS 0x16

/* The three sprites: SelBar.cel's only entry, and Command.cel's spell icon and
   MP caption (PUSH 0x0 at 00027766, PUSH 0x20 at 00027791, PUSH 0x42 at
   000277fb). */
#define SPELL_LIST_SEL_BAR_SPRITE 0
#define SPELL_LIST_ICON_SPRITE 0x20
#define SPELL_LIST_MP_CAPTION_SPRITE 0x42

/* Where the spell names live in data_fdps_all_game_text_ptr: entry 0x1be plus
   the spell id (ADD EAX,0x1be at 000277d3). */
#define SPELL_NAME_TEXT_BASE 0x1be

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d at 000277b5,
   000277b3 and 000277b1. */
#define SPELL_LIST_TEXT_FG_COLOR 0xd0
#define SPELL_LIST_TEXT_BG_COLOR 0
#define SPELL_LIST_TEXT_OUTLINE_COLOR 0x6d

/* The width of the MP figure and its sign flag: PUSH 0x4 at 00027810 and a
   zeroed EAX pushed behind it at 0002780d.  Four digits are zero-padded, so a
   cost of 12 draws "0012"; the padded branch's overflow guard cannot fire on a
   cost that came out of a byte (text.h). */
#define SPELL_LIST_MP_DIGITS 4
#define SPELL_LIST_MP_SHOW_PLUS 0

/* 000276f0.  Five stack arguments, caller-cleaned: all five call sites push
   five dwords right to left and follow the CALL with ADD ESP,0x14, the body
   reads them at [EBP+0x14] through [EBP+0x24] behind PUSH EBX/ESI/EDI/EBP and
   the return address, and RET carries no immediate.  Nothing reads EAX
   afterwards; the function returns nothing.

   Two branches and one loop.  The collector's count is tested against zero
   once (CMP dword ptr [EBP-0x18],0x0 / JZ to the epilogue at 00027713), the
   row counter runs 0..7 against the signed JL at 00027724, and inside it the
   list index is tested against the count with the signed JGE at 00027742 --
   the skip that ends the page early when the list runs out.  cursor_index is
   compared for equality only, so its -1 is not a sign test but simply a value
   no list index can take.

   THE ID IS FETCHED WITH THE LIST INDEX, NOT THE ROW.  MOV EDX,dword ptr
   [EBP-0xc] / MOV AL,byte ptr [EDX+EBP*1-0x40] at 00027778 addresses the
   buffer with list_top + row, so the second page of the status window reads
   spell_ids[8..15] while drawing rows 0..7.  Indexing it with the row would
   redraw page one under page two's scroll position and would look right at
   list_top 0.

   BOTH BYTES ARE ZERO-EXTENDED.  XOR EAX,EAX / MOV AL for the id at 00027776
   and MOV AL,byte ptr [EAX+0x5] / AND EAX,0xff for the MP cost at 00027815 --
   neither is a MOVSX, so an id or a cost above 0x7f stays positive.  Reading
   either as a signed char would push a negative text id and a negative figure.

   Nothing is read after any of the six calls except the record pointer
   fdps_get_spell_record returns, which is kept at [EBP-0x4] and dereferenced
   once, at +5, for the MP cost.  fdps_draw_text's return is discarded (ADD
   ESP,0x1c at 000277e4 and no read of EAX), and the other four return
   nothing.

   The MP caption's address is worked out once into [EBP-0x8] and used twice,
   for the caption blit and then plus 0x16 for the figure; the icon's and the
   name's are each built fresh from dest. */
void fdps_draw_spell_list_page(int unit_index, int list_top, int cursor_index,
                               unsigned char *dest, int pitch)
{
    unsigned char spell_ids[SPELL_LIST_ID_BUFFER_BYTES];
    struct fdps_spell_effect *spell;
    unsigned char *mp_caption_dest;
    int spell_count;
    int row;
    int list_index;
    int spell_id;

    spell_count = fdps_unit_collect_known_spells(unit_index, spell_ids);
    if (spell_count == 0) {
        return;
    }

    for (row = 0; row < SPELL_LIST_ROWS; row++) {
        list_index = list_top + row;
        if (list_index < spell_count) {
            if (cursor_index == list_index) {
                fdps_cel_blit_sprite(data_fdps_selection_bar_sheet_ptr,
                                     SPELL_LIST_SEL_BAR_SPRITE, dest, pitch,
                                     SPELL_LIST_SEL_BAR_X,
                                     row * SPELL_LIST_ROW_PITCH
                                     + SPELL_LIST_SEL_BAR_Y, 0, 0);
            }

            spell_id = spell_ids[list_index];
            spell = fdps_get_spell_record(spell_id);

            fdps_blit_command_sprite(dest
                                     + (row * SPELL_LIST_ROW_PITCH
                                        + SPELL_LIST_ICON_Y) * pitch
                                     + SPELL_LIST_ICON_X,
                                     pitch, SPELL_LIST_ICON_SPRITE);

            fdps_draw_text(data_fdps_all_game_text_ptr,
                           spell_id + SPELL_NAME_TEXT_BASE,
                           dest
                           + (row * SPELL_LIST_ROW_PITCH
                              + SPELL_LIST_NAME_Y) * pitch
                           + SPELL_LIST_NAME_X, pitch,
                           SPELL_LIST_TEXT_FG_COLOR,
                           SPELL_LIST_TEXT_BG_COLOR,
                           SPELL_LIST_TEXT_OUTLINE_COLOR);

            mp_caption_dest = dest
                + (row * SPELL_LIST_ROW_PITCH + SPELL_LIST_MP_CAPTION_Y)
                  * pitch
                + SPELL_LIST_MP_CAPTION_X;
            fdps_blit_command_sprite(mp_caption_dest, pitch,
                                     SPELL_LIST_MP_CAPTION_SPRITE);
            fdps_draw_number(mp_caption_dest + SPELL_LIST_MP_FIGURE_X_BIAS,
                             pitch, spell->mp_cost, SPELL_LIST_MP_DIGITS,
                             SPELL_LIST_MP_SHOW_PLUS);
        }
    }
}
