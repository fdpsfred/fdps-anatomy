/* spellmnu.c -- the spell list UI and the in-battle spell command: the page
 * drawer the status window and both spell menus share, the menus' own input
 * loops, and the command that runs a chosen spell
 * (rebuild_info/code_layout.md).
 *
 * The list is eight rows of a flat array of spell ids, collected out of the
 * unit record's five-byte bitmap by unitstat.c and never cached; everything
 * this file draws with comes from sprite.c, text.c and table.c, and the two
 * .CEL sheet pointers and the global text block belong to gamedata.h.
 *
 * The page drawer owns no state.  The wait loop does own one piece -- the tick
 * it last drew a frame on -- and spellmnu.h declares it; the map behind the
 * window comes from mapdraw.c and the presentation goes straight to the
 * adapter.
 */
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "aitarget.h"
#include "audio.h"
#include "blit.h"
#include "cmbspell.h"
#include "death.h"
#include "gamedata.h"
#include "keybd.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "movegrid.h"
#include "palcycle.h"
#include "sprite.h"
#include "statunit.h"
#include "statwin.h"
#include "table.h"
#include "text.h"
#include "unit.h"
#include "unitstat.h"
#include "spellmnu.h"

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* End of global data. */

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

/* The VGA graphics aperture as a flat linear address, the mode 13h scanline
   pitch and one whole frame.  All three are hard-coded in the original (PUSH
   0xa0000 at 00027bce, the PUSH 0x140 beside every window blit, PUSH 0xfa00 at
   000279e9) and stay literals here: 0xa0000 is where the display adapter
   answers, not the address of anything the linker places, so there is no
   symbol to reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_BYTES 0xfa00

/* The largest input code the wait loop below will hand back.  Everything above
   it -- the 0xff the scancode reader answers with when nothing has been
   pressed, and every break code -- keeps the loop running.  CMP dword ptr
   [EBP-0x14],0x7f / JLE at 00027907, a SIGNED compare, which is why the code
   is held in an int here rather than in the unsigned the reader returns. */
#define SCANCODE_LAST_MAKE_CODE 0x7f

/* The spell panel background and where it lands in the window image: a
   151 x 149 rectangle at its own 0x97 stride (PUSH 0x95 / PUSH 0x97 / PUSH 0x97
   at 00027943) blitted to row 47, column 152, which is the 0x3b58 of ADD
   EAX,0x3b58 at 00027955.  The same address is then handed to the page drawer,
   so the rows go down over the background that was just restored. */
#define PANEL_STRIDE 0x97
#define PANEL_W 0x97
#define PANEL_H 0x95
#define LIST_AREA_ROW 47
#define LIST_AREA_COL 152
#define LIST_AREA_AT (LIST_AREA_ROW * VGA_SCREEN_PITCH + LIST_AREA_COL)

/* The two scroll arrows.  Command.cel sprites 0x44/0x45 are the up arrow's two
   blink phases and 0x46/0x47 the down arrow's (ADD EAX,0x44 at 00027997 and ADD
   EAX,0x46 at 000279bf, each added to the blink bit), drawn at row 49 column
   220 and row 191 column 220 -- the 0x3e1c and 0xef9c of the two ADDs at
   000279a3 and 000279cb.

   The blink bit is (latch / 8) & 1, so a phase lasts eight ticks: MOV EDX /
   SAR EDX,0x1f / SHL EDX,0x3 / SBB / SAR EAX,0x3 at 00027927 is a SIGNED
   divide by eight and not a shift, and the AND that follows takes bit 0 of the
   quotient. */
#define UP_ARROW_ROW 49
#define UP_ARROW_COL 220
#define UP_ARROW_AT (UP_ARROW_ROW * VGA_SCREEN_PITCH + UP_ARROW_COL)
#define DOWN_ARROW_ROW 191
#define DOWN_ARROW_COL 220
#define DOWN_ARROW_AT (DOWN_ARROW_ROW * VGA_SCREEN_PITCH + DOWN_ARROW_COL)
#define UP_ARROW_SPRITE 0x44
#define DOWN_ARROW_SPRITE 0x46
#define ARROW_BLINK_TICKS 8

/* The page the map behind the window is composed on: 360 x 240 8bpp at pitch
   0x168, PUSH 0x15180 at 000279d9, with the 24-pixel apron on all four sides
   that fdps_draw_scene_layers needs.  Its visible window starts at page byte
   0x21d8, page coordinate (24,24), and lands at screen byte 0x504, screen
   coordinate (4,4). */
#define SCENE_PAGE_PITCH 0x168
#define SCENE_PAGE_BYTES 0x15180
#define SCENE_PAGE_WINDOW_AT 0x21d8
#define SCREEN_WINDOW_AT 0x504
#define SCREEN_WINDOW_W 0x138
#define SCREEN_WINDOW_H 0xc0

/* Where the composed window sits in a whole 320x200 frame and how big it is:
   columns 15..305 of every one of the 200 rows, PUSH 0x123 / PUSH 0xc8 with the
   two 0xf offsets at 00027bad and 00027bb9. */
#define WINDOW_AT 0x0f
#define WINDOW_W 0x123
#define WINDOW_H 0xc8

/* The caster's cell: one 24x24 sprite, 0x240 bytes at pitch 0x18 (PUSH 0x240 at
   00027ad6 and the three PUSH 0x18 at 00027b65), composed shadow first and walk
   frame over it, then copied whole into the window image at row 10, column 161
   -- the 0xd21 of ADD EAX,0xd21 at 00027b87. */
#define UNIT_SPRITE_W 0x18
#define UNIT_SPRITE_H 0x18
#define UNIT_SPRITE_BYTES 0x240
#define WINDOW_SPRITE_ROW 0x0a
#define WINDOW_SPRITE_COL 0xa1
#define WINDOW_SPRITE_AT \
    (WINDOW_SPRITE_ROW * VGA_SCREEN_PITCH + WINDOW_SPRITE_COL)

/* Sprite 3 of Shadow.cel, the shadow every unit in this window stands on (PUSH
   0x3 at 00027af4), and blit mode 0, the plain opaque RLE kernel, which is the
   last of the sprite blitter's eight arguments and so the first pushed. */
#define SHADOW_SPRITE_INDEX 3
#define BLIT_MODE_OPAQUE 0

/* The walk cycle.  The latch is taken modulo 16 and divided by 4, and a
   quotient of 3 is folded back to 1, so the three drawn frames run 0, 1, 2, 1
   and the cycle reads as a ping-pong rather than a snap back to the start.  MOV
   EBX,0x10 / IDIV at 00027ab7 and the SAR EAX,0x2 at 00027ac3, both signed. */
#define WALK_CYCLE_TICKS 0x10
#define WALK_CYCLE_TICKS_PER_FRAME 4

/* A cache slot's twelve stream offsets are four facings of three walk frames
   (struct fdps_cel_cache_slot, src/fdpstype.h), and the facing numbering is the
   one the whole game uses: 0 down, 1 left, 2 up, 3 right.  This window only
   ever draws the caster facing the player, so the facing displacement the
   assembly folds into LEA EAX,[EAX*0x4 + 0x0] at 00027b4b is zero. */
#define WALK_FRAMES_PER_FACING 3
#define FACING_DOWN 0

/* 000278e0.  Five stack arguments, caller-cleaned: all three call sites push
   five dwords right to left and follow the CALL with ADD ESP,0x14, the body
   reads them at [EBP+0x14] through [EBP+0x24] behind PUSH EBX/ESI/EDI/EBP and
   the return address, and RET carries no immediate.  The result is EAX, and
   fdps_spell_list_select_loop keeps it (MOV dword ptr [EBP-0x1c],EAX at
   000281d4) while fdps_battle_show_unit_status_window discards it.

   THE COLLECTOR RUNS ONCE, OUTSIDE THE LOOP.  CALL 0x00027840 at 000278f4 sits
   before the loop head at 000278ff, and only its count is kept -- the 40 ids it
   wrote into the stack buffer are never read here.  Moving it inside the loop
   would give the same picture and would walk the caster's bitmap on every
   frame.

   BOTH ANIMATION PHASES COME FROM THE LATCH, NOT FROM THE LIVE COUNTER, and
   the latch is advanced on the last line of the frame.  Latching the new tick
   at the top of the pass and deriving the blink and the walk frame from
   data_fdps_timer_tick_counter is the obvious loop and it shifts both
   animations by one tick (rebuild_info/pitfalls.md).

   Three branches inside the pass and one outside it.  The mode flag is tested
   twice, once for where the backdrop comes from and once for where the sprite
   row comes from, and the two tests are the opposite way round in the assembly
   -- JNZ at 00027a00 takes the village path, JZ at 00027b27 takes the field
   one -- which is why the second is written as a test against non-zero here.
   The two arrows are independent: list_top != 0 for the up arrow and the
   signed list_top + 8 < spell_count for the down one, so a nine-spell list
   scrolled to the bottom shows the up arrow alone.

   Nothing is read after any call except the three the results of which are
   named below: the collector's count, the reader's code, the record pointer
   and the three malloc blocks. */
int fdps_spell_list_window_wait_input(unsigned char *window_buf,
                                      unsigned char *panel_src,
                                      int unit_index, int list_top,
                                      int cursor_index)
{
    /* The ids the collector writes.  Only the count it returns is used, but
       the buffer is what the collector is given and its forty bytes are the
       whole of the frame's SUB ESP,0x54 that the eleven scalars do not
       occupy -- forty is also the most ids five bitmap bytes can hold, so it
       is exactly full-sized and nothing bounds the write (unitstat.h). */
    unsigned char spell_ids[SPELL_LIST_ID_BUFFER_BYTES];
    /* The caster's record, looked up once per frame.  Fetched on both sides of
       the mode flag and read only on the field side. */
    struct fdps_unit_record *unit;
    /* The 360x240 page the map behind the window is composed on, taken and
       given back inside one pass. */
    unsigned char *scene_page;
    /* The whole 320x200 frame this pass builds and presents. */
    unsigned char *frame;
    /* The 24x24 cell the shadow and the caster are composed in before the pair
       is copied into the window image. */
    unsigned char *sprite_cell;
    /* The RLE stream of the walk frame being drawn, addressed from the base of
       the sprite cache block. */
    unsigned char *sprite_stream;
    /* How many spells the caster knows, collected once on the way in; the down
       arrow is the only thing that reads it. */
    int spell_count;
    /* The code the last poll returned.  Held signed because the test that ends
       the loop is signed, and returned as it stands. */
    int input_code;
    /* Which of the two blink phases the arrows are drawn in this frame. */
    int arrow_blink;
    /* Which of the three walk frames of the caster's facing is drawn. */
    int walk_frame;
    /* The row of the sprite cache the walk frame is taken from: the caster's
       own slot in the field, and unit_index itself in a village. */
    int sprite_cache_slot;

    spell_count = fdps_unit_collect_known_spells(unit_index, spell_ids);

    for (;;) {
        input_code = (int) fdps_read_scancode_auto_repeat();
        if (input_code <= SCANCODE_LAST_MAKE_CODE) {
            break;
        }
        if ((int) data_fdps_timer_tick_counter
                == data_fdps_spell_list_window_last_tick) {
            continue;
        }

        fdps_cycle_ui_palette();
        arrow_blink =
            (data_fdps_spell_list_window_last_tick / ARROW_BLINK_TICKS) & 1;

        /* The background first and the eight rows over it, so the previous
           frame's rows are erased rather than drawn on top of. */
        fdps_blit_rect((unsigned int) panel_src, PANEL_STRIDE,
                       window_buf + LIST_AREA_AT, VGA_SCREEN_PITCH,
                       PANEL_W, PANEL_H);
        fdps_draw_spell_list_page(unit_index, list_top, cursor_index,
                                  window_buf + LIST_AREA_AT,
                                  VGA_SCREEN_PITCH);
        if (list_top != 0) {
            fdps_blit_command_sprite(window_buf + UP_ARROW_AT,
                                     VGA_SCREEN_PITCH,
                                     arrow_blink + UP_ARROW_SPRITE);
        }
        if (list_top + SPELL_LIST_ROWS < spell_count) {
            fdps_blit_command_sprite(window_buf + DOWN_ARROW_AT,
                                     VGA_SCREEN_PITCH,
                                     arrow_blink + DOWN_ARROW_SPRITE);
        }

        scene_page = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);
        frame = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);

        /* Where the picture behind the window comes from.  In the field the
           map is composed from scratch into the scene page and its view window
           is blitted into a cleared frame; in a village the saved page already
           holds a finished picture and is taken whole, and the blit into the
           scene page that precedes it fills a buffer nothing then reads.  The
           frame is cleared on the field path only. */
        if (data_fdps_village_mode_flag == 0) {
            fdps_draw_scene_layers(scene_page);
            memset(frame, 0, (size_t) VGA_SCREEN_BYTES);
            fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                           SCENE_PAGE_PITCH, frame + SCREEN_WINDOW_AT,
                           VGA_SCREEN_PITCH, SCREEN_WINDOW_W, SCREEN_WINDOW_H);
        } else {
            fdps_blit_rect((unsigned int)
                               (data_fdps_village_backdrop_page_ptr
                                + SCREEN_WINDOW_AT),
                           VGA_SCREEN_PITCH,
                           scene_page + SCENE_PAGE_WINDOW_AT,
                           SCENE_PAGE_PITCH, SCREEN_WINDOW_W, SCREEN_WINDOW_H);
            memmove(frame, data_fdps_village_backdrop_page_ptr,
                    (size_t) VGA_SCREEN_BYTES);
        }
        free(scene_page);

        walk_frame =
            (data_fdps_spell_list_window_last_tick % WALK_CYCLE_TICKS)
            / WALK_CYCLE_TICKS_PER_FRAME;
        if (walk_frame == 3) {
            walk_frame = 1;
        }

        /* The shadow goes down first and the walk frame over it, so the cell
           the window image receives is the pair composited. */
        sprite_cell = (unsigned char *) malloc((size_t) UNIT_SPRITE_BYTES);
        fdps_cel_blit_sprite(data_fdps_shadow_sprite_sheet_ptr,
                             SHADOW_SPRITE_INDEX, sprite_cell, UNIT_SPRITE_W,
                             0, 0, 0, BLIT_MODE_OPAQUE);

        /* The clamp is AFTER the shadow, not folded into the test above it,
           and it cannot fire from a non-negative latch: it is what a negative
           one -- the counter having run past 0x7fffffff -- would land on. */
        if (walk_frame < 0) {
            walk_frame = 0;
        }

        unit = fdps_get_unit_record(unit_index);
        if (data_fdps_village_mode_flag != 0) {
            sprite_cache_slot = unit_index;
        } else {
            sprite_cache_slot = (int) unit->sprite_cache_slot;
        }

        /* A stored offset is measured from the base of the cache block, not
           from the slot it was read out of. */
        sprite_stream = data_fdps_cel_sprite_cache_ptr
            + ((struct fdps_cel_cache_slot *) data_fdps_cel_sprite_cache_ptr)
                  [sprite_cache_slot].sprite_offset[
                      FACING_DOWN * WALK_FRAMES_PER_FACING + walk_frame];
        fdps_blit_dispatch(sprite_stream, sprite_cell, UNIT_SPRITE_W,
                           UNIT_SPRITE_H, UNIT_SPRITE_W, 0, BLIT_MODE_OPAQUE);
        fdps_blit_rect((unsigned int) sprite_cell, UNIT_SPRITE_W,
                       window_buf + WINDOW_SPRITE_AT, VGA_SCREEN_PITCH,
                       UNIT_SPRITE_W, UNIT_SPRITE_H);
        fdps_blit_rect((unsigned int) (window_buf + WINDOW_AT),
                       VGA_SCREEN_PITCH, frame + WINDOW_AT, VGA_SCREEN_PITCH,
                       WINDOW_W, WINDOW_H);

        /* The whole frame goes out, on both paths: unlike the unit status
           window this one has no village variant that presents the inner
           rectangle alone. */
        memmove((void *) VGA_SCREEN_BASE, frame, (size_t) VGA_SCREEN_BYTES);
        free(frame);
        free(sprite_cell);
        data_fdps_spell_list_window_last_tick =
            (int) data_fdps_timer_tick_counter;
    }

    return input_code;
}

/* The six make codes the selection loop acts on: CMP dword ptr [EBP-0x1c],0x48
   at 000281d7, 0x50 at 00028205, 0x1c at 00028239, 0x39 at 0002823f, 0x1 at
   00028281 and 0x53 at 00028287.  Every other code falls through to the
   redraw. */
#define SCANCODE_UP 0x48
#define SCANCODE_DOWN 0x50
#define SCANCODE_ENTER 0x1c
#define SCANCODE_SPACE 0x39
#define SCANCODE_ESC 0x01
#define SCANCODE_DELETE 0x53

/* The two answers: MOV dword ptr [EBP-0x4],0x1 at 00028276 and MOV dword ptr
   [EBP-0x4],0xffffffff at 0002828d. */
#define SPELL_SELECT_CONFIRMED 1
#define SPELL_SELECT_CANCELLED (-1)

/* How far the scroll position is dragged when the cursor walks off the bottom
   of the page: SUB EAX,0x7 at 0002822f, one row less than the page, so the
   cursor comes to rest on the last visible row rather than on the first. */
#define SPELL_LIST_SCROLL_BACK (SPELL_LIST_ROWS - 1)

/* 00028130.  Five stack arguments, caller-cleaned: the one call site at
   00027d2e pushes five dwords right to left and follows the CALL with ADD
   ESP,0x14, the body reads them at [EBP+0x14] through [EBP+0x24] behind PUSH
   EBX/ESI/EDI/EBP and the return address, and RET carries no immediate.  The
   answer is EAX and the caller keeps it (MOV dword ptr [EBP-0x3c],EAX at
   00027d36).

   THE LOOP HAS NO EXIT CONDITION.  JMP 0x00028169 at 00028296 is unconditional
   and the only two ways out are the two stores into [EBP-0x4]: a confirm the
   caster can pay for, and a cancel.  A code the dispatch does not name -- and
   the 0xff the reader answers with when nothing is down never gets this far,
   the wait swallowing everything above 0x7f -- simply repaints and waits
   again.

   BOTH INDICES ARE READ AND WRITTEN THROUGH THE CALLER'S POINTERS, EVERY TIME.
   Nothing is cached in a local: each redraw and each key dereferences them
   afresh, which is what lets the caller see the cursor the player left behind
   and recover the chosen spell from it.  The scroll follows the cursor only
   when the cursor has left the page -- CMP EDX,[EAX] / JLE at 000281f2 for the
   top and the +8 compare at 00028226 for the bottom -- so a caller that opens
   the menu with a cursor already off the page keeps that page until the cursor
   crosses an edge.

   THE COUNT IS COLLECTED ONCE, BEFORE THE LOOP, and only the down key reads
   it: a spell learned or lost while the menu is up neither extends nor
   shortens the travel, and the up key is bounded by zero alone.

   THE CONFIRM READS THE ID WITH *cursor_index AND NOT WITH THE ROW.  MOV
   EAX,[EAX] / MOV AL,byte ptr [EAX+EBP*1-0x44] / AND EAX,0xff at 00028248
   addresses the collected buffer with the whole list index and widens the byte
   without sign.  The cost is byte +5 of the MAGICDAT.DAT record, also
   zero-extended (MOV AL,byte ptr [EDX+0x5] at 00028264), and the caster's MP
   is the SIGNED word at record +0x44 (MOVSX EAX,word ptr [EAX+0x44] at
   0002826d) compared with a signed JL -- so a cost of 0x80 or more is a real
   cost of 128 or more and not a negative one that every caster could pay.
   Equal MP pays: the refusal is JL, strictly less.

   A refusal is silent.  No sound, no message, no cursor move -- straight back
   to the top of the loop, which is why a player who cannot afford the spell
   sees the list simply not respond.  Nothing here spends the MP either; the
   deduction is fdps_spell_deduct_mp_cost's job, later in the caller.

   Nothing is read after any call except the collector's count, the record
   pointers from fdps_get_unit_record and fdps_get_spell_record, and the wait's
   code.  fdps_blit_rect and the page drawer return nothing. */
int fdps_spell_list_select_loop(int unit_index, unsigned char *window_buf,
                                unsigned char *panel_src, int *list_top,
                                int *cursor_index)
{
    /* The ids the collector writes, in ascending id order.  Forty is the most
       five bitmap bytes can hold, so the buffer is exactly full-sized and
       nothing bounds the write (unitstat.h). */
    unsigned char spell_ids[SPELL_LIST_ID_BUFFER_BYTES];
    /* The casting unit's record, fetched once and read for its MP alone. */
    struct fdps_unit_record *caster;
    /* The MAGICDAT.DAT record of the spell being confirmed. */
    struct fdps_spell_effect *spell;
    /* Record +0x02, the caster's sprite cache slot.  Assigned at 00028166 and
       never read -- fdps_battle_spell_command does the same with the same
       byte, and the wait loop looks it up again for itself. */
    int sprite_cache_slot;
    /* How many spells the caster knows, collected once on the way in; the down
       key's travel limit and nothing else. */
    int spell_count;
    /* The make code the last wait came back with, held signed and compared as
       a full int. */
    int scancode;
    /* The MP the selected spell costs, widened out of the record byte without
       sign. */
    int mp_cost;

    spell_count = fdps_unit_collect_known_spells(unit_index, spell_ids);
    caster = fdps_get_unit_record(unit_index);
    sprite_cache_slot = (int) caster->sprite_cache_slot;

    for (;;) {
        /* The background first and the page over it, so the previous frame's
           rows are erased rather than drawn on top of. */
        fdps_blit_rect((unsigned int) panel_src, PANEL_STRIDE,
                       window_buf + LIST_AREA_AT, VGA_SCREEN_PITCH,
                       PANEL_W, PANEL_H);
        fdps_draw_spell_list_page(unit_index, *list_top, *cursor_index,
                                  window_buf + LIST_AREA_AT,
                                  VGA_SCREEN_PITCH);
        scancode = fdps_spell_list_window_wait_input(window_buf, panel_src,
                                                     unit_index, *list_top,
                                                     *cursor_index);

        if (scancode == SCANCODE_UP) {
            if (*cursor_index != 0) {
                *cursor_index = *cursor_index - 1;
                if (*list_top > *cursor_index) {
                    *list_top = *cursor_index;
                }
            }
        } else if (scancode == SCANCODE_DOWN) {
            if (spell_count - 1 > *cursor_index) {
                *cursor_index = *cursor_index + 1;
                if (*list_top + SPELL_LIST_ROWS <= *cursor_index) {
                    *list_top = *cursor_index - SPELL_LIST_SCROLL_BACK;
                }
            }
        } else if (scancode == SCANCODE_ENTER || scancode == SCANCODE_SPACE) {
            spell = fdps_get_spell_record((int) spell_ids[*cursor_index]);
            mp_cost = (int) spell->mp_cost;
            if ((int) caster->mp_current >= mp_cost) {
                return SPELL_SELECT_CONFIRMED;
            }
        } else if (scancode == SCANCODE_ESC || scancode == SCANCODE_DELETE) {
            return SPELL_SELECT_CANCELLED;
        }
    }
}

/* ------------------------------------------------------------------------
 * fdps_battle_spell_command @ 00027c20
 * ---------------------------------------------------------------------- */

/* How many slide-in steps the window is opened with: CMP dword ptr
   [EBP-0x30],0x9 / JL at 00027cf4.  fdps_close_status_window runs the same
   nine counting down. */
#define STATUS_WINDOW_ANIM_STEPS 9

/* The 151 x 149 copy of the window's list area the select loop erases each
   frame with, PUSH 0x57e3 at 00027c68.  PANEL_W and PANEL_H above are the two
   extents the blit that fills it is given. */
#define PANEL_BACKDROP_BYTES (PANEL_W * PANEL_H)

/* The map cursor is held in world pixels and this is the tile size every
   reader divides it by, MOV EBX,0x18 in front of each of the seven IDIV
   (gamedata.h). */
#define MAP_TILE_SIZE 0x18

/* What data_fdps_map_cursor_draw_mode is set to and what the spell's blast
   radius contributes to it.  0 paints nothing, 1 is the plain cursor, and
   radius + 2 is the diamond footprint fdps_draw_map_cursor paints round it
   (gamedata.h).  The bias is also how the radius is recovered on the map-wide
   arm, SUB EAX,0x2 at 00027f2f. */
#define CURSOR_OVERLAY_OFF 0
#define CURSOR_OVERLAY_PLAIN 1
#define CURSOR_FOOTPRINT_BIAS 2

/* MAGICDAT.DAT's cast-range byte carries the reach and the shape together, the
   same encoding fdps_collect_targets_in_range reads (aitarget.h): up to 0x0f
   it is a radius in tiles, and from 0x10 up it is a straight line whose length
   is the low nibble.  The test is CMP dword ptr [EBP-0xc],0xf / JLE at
   00027e46, so 0x0f is the last radius and the line arm is the one that falls
   through. */
#define CAST_RANGE_LAST_RADIUS 0x0f
#define CAST_RANGE_LINE_BIT 0x10

/* The min_dist argument every sweep here passes, PUSH 0x0 at 00027ddb,
   00027ed9 and 00027f41: the caster's own tile is kept.  The item menu is the
   one caller that ever drops it (item.c). */
#define AIM_SWEEP_KEEP_OWN_TILE 0

/* The line sweep's side filter, PUSH 0x1 at 00027e5f --
   fdps_collect_targets_in_line's select_enemy_side, and not a spell record
   byte (aitarget.h). */
#define LINE_SWEEP_KEEPS_ENEMY_SIDE 1

/* The sound a line spell's aim plays once the sweep is done, MOV EAX,0x61ba4 /
   PUSH EAX at 00027ebd.  It is passed as a writable string because the lookup
   upper-cases it in the caller's own storage (audio.h,
   rebuild_info/pitfalls.md). */
#define LINE_SWEEP_SOUND "Chess.wav"

/* The map-wide arm's four literals.  The sweep keeps side 0 only, PUSH 0x0 at
   00027f3f -- fdps_collect_targets_in_range's select_mode 0 (aitarget.h) --
   and the aim that follows is handed an empty candidate list, PUSH 0x0 at
   00027fa0, so the mode is all that steers it.  Mode 4 is the map-wide aim and
   mode 5 the one that never confirms, MOV dword ptr [EBP-0x3c],0x4 at 00027f85
   and 0x5 at 00027f92. */
#define MAP_WIDE_SELECT_MODE 0
#define MAP_WIDE_AIM_LIST_COUNT 0
#define MAP_WIDE_AIM_MODE 4
#define MAP_WIDE_AIM_MODE_NO_TARGET 5

/* 傳送術, the one spell that asks for a second tile after its target, CMP
   EAX,0x15 / JZ at 00027fc3, and the cursor mode that second pick runs in,
   PUSH 0x6 at 00027ff7 -- the mode that only accepts a tile the unit may stand
   on (mapcur.h). */
#define TELEPORT_SPELL_ID 0x15
#define DESTINATION_SELECT_MODE 6

/* The two hundred-byte stack buffers: the unit indices a sweep matches, and
   the death scripts collected after the spell has been applied.  They are
   [EBP-0xc8] and [EBP-0x12c], 0x64 bytes of frame each. */
#define TARGET_BUFFER_BYTES 100
#define DEATH_SCRIPT_BUFFER_BYTES 100

/* What the experience banked for this cast is divided by before it is paid:
   the caster's level byte, plus 30 when its portrait id is above 8, because a
   promoted form's level byte has restarted at 1.  CMP EAX,0x8 / JLE at
   000280cf, ADD dword ptr [EBP-0x30],0x1e at 000280d4.

   fdps_unit_heal_and_credit_exp adds the same 30 for portrait ids 0x0f..0x21
   (unitstat.c).  The two spans are different and the difference is what each
   function credits; they are not one rule written twice. */
#define LAST_UNPROMOTED_PORTRAIT_ID 8
#define PROMOTED_LEVEL_BONUS 0x1e

/* What this command answers with.  A cast is the select loop's own 1 handed
   straight back out -- MOV EAX,dword ptr [EBP-0x3c] at 00028116 -- and the -1
   is the only literal the function returns, MOV dword ptr [EBP-0x4],0xffffffff
   at 00027d73. */
#define SPELL_COMMAND_NOT_ACTED (-1)

/* 00027c20.  See spellmnu.h for what the player sees and what the answer
   means.

   THE SCROLL POSITION AND THE CURSOR ROW ARE INITIALISED IN FRONT OF THE RETRY
   LOOP, at 00027c2c and 00027c33 and not inside it, which is why a cancelled
   aim reopens the list on the same page with the same spell still highlighted.
   Declaring the two inside the loop body -- the obvious C -- sends the player
   back to the top of the list every time.

   THE ORIGINAL HOLDS SEVERAL OF THESE LOCALS IN ONE STACK SLOT: [EBP-0x30] is
   the animation step and then the caster's effective level, [EBP-0xc] the cast
   range and then the map-wide arm's radius, and [EBP-0x3c] the aim mode and
   then the answer.  Each pair is two different measurements and each is named
   as such here; a stack slot is not observable behaviour (ADR-0001). */
int fdps_battle_spell_command(int unit_index)
{
    /* Where the spell list is scrolled to and which entry the cursor is on.
       Both survive a cancelled aim; see the note above. */
    int list_top;
    int cursor_index;
    /* The whole 320x200 frame the window is composed in: the Status.cel
       artwork, then the caster's stat panel, then its spell list. */
    unsigned char *window_image;
    /* The screen as it stood before the window opened, which every animation
       frame is drawn over and the close is handed back. */
    void *background_frame;
    /* The pristine copy of the window's list area, which the select loop lays
       back over the list before it redraws it (spellmnu.h). */
    unsigned char *panel_backdrop;
    /* The caster's record.  Resolved once a pass, before the window is
       animated, and read again at the end of the pass for the experience
       divisor. */
    struct fdps_unit_record *caster;
    /* Which sprite cache slot the caster's walk cycle would come out of,
       record byte 2 widened unsigned.  Nothing reads it: the wait loop that
       needs it resolves the record for itself (spellmnu.h).  The load is kept
       because the original makes it. */
    int sprite_cache_slot;
    /* Which step of the window's slide-in is being drawn. */
    int step;
    /* What the last window or cursor loop answered: a cancel or a confirm. */
    int pick_result;
    /* The caster's known spell ids, collected afresh after the list has closed
       so that the row the cursor was left on can be turned back into an id. */
    unsigned char spell_ids[SPELL_LIST_ID_BUFFER_BYTES];
    /* The chosen spell's MAGICDAT.DAT record. */
    struct fdps_spell_effect *spell;
    /* That record's reach-and-shape byte, widened to the int the signed
       compare against 0x0f is made on. */
    int cast_range;
    /* The blast radius a map-wide spell covers, read back out of the cursor
       mode the line above installed it in. */
    int map_wide_radius;
    /* Which cursor mode the map-wide aim runs in: the one that confirms, or
       the one that cannot. */
    int map_wide_aim_mode;
    /* Where a sweep writes the unit indices it matched, one byte each. */
    unsigned char targets[TARGET_BUFFER_BYTES];
    /* How many units the spell about to be played out covers. */
    int target_count;
    /* The caster's record fetched a second time, for the tile a line spell
       sweeps from.  The aim runs between the two lookups, so this is a re-read
       rather than a reuse of caster (unit.h). */
    struct fdps_unit_record *line_origin_unit;
    /* The death scripts whatever the spell killed leaves behind, and how many
       of them there are. */
    unsigned char death_scripts[DEATH_SCRIPT_BUFFER_BYTES];
    int death_script_count;
    /* The caster's level, with the promoted form's 30 added. */
    int effective_level;

    list_top = 0;
    cursor_index = 0;

    for (;;) {
        /* The window: the artwork, a copy of the live screen to animate over,
           and the copy of the list area the select loop erases with. */
        window_image = (unsigned char *) fdps_load_status_cel_image();
        background_frame = malloc((size_t) VGA_SCREEN_BYTES);
        memmove(background_frame, (void *) VGA_SCREEN_BASE,
                (size_t) VGA_SCREEN_BYTES);
        panel_backdrop = (unsigned char *)
            malloc((size_t) PANEL_BACKDROP_BYTES);
        fdps_blit_rect((unsigned int) (window_image + LIST_AREA_AT),
                       VGA_SCREEN_PITCH, panel_backdrop, PANEL_STRIDE,
                       PANEL_W, PANEL_H);

        fdps_draw_unit_status_panel(unit_index, window_image);
        fdps_draw_spell_list_page(unit_index, list_top, cursor_index,
                                  window_image + LIST_AREA_AT,
                                  VGA_SCREEN_PITCH);

        caster = fdps_get_unit_record(unit_index);
        sprite_cache_slot = (int) caster->sprite_cache_slot;

        for (step = 0; step < STATUS_WINDOW_ANIM_STEPS; step++) {
            fdps_draw_status_window_anim_frame(background_frame, window_image,
                                               step);
        }

        pick_result = fdps_spell_list_select_loop(unit_index, window_image,
                                                  panel_backdrop, &list_top,
                                                  &cursor_index);
        fdps_close_status_window(window_image, background_frame);
        free(window_image);
        free(panel_backdrop);
        free(background_frame);

        if (pick_result == SPELL_SELECT_CANCELLED) {
            return SPELL_COMMAND_NOT_ACTED;
        }

        /* The list is collected again to turn the row the cursor was left on
           back into a spell id; the count the collector answers with is not
           looked at. */
        fdps_unit_collect_known_spells(unit_index, spell_ids);
        spell = fdps_get_spell_record((int) spell_ids[cursor_index]);
        data_fdps_map_cursor_draw_mode = (int) spell->area
                                         + CURSOR_FOOTPRINT_BIAS;
        cast_range = (int) spell->cast_range_flags;

        if (cast_range != 0) {
            /* An aimed spell: what is in reach of the cursor, then the aim,
               then the units the effect really covers about the tile the aim
               settled on. */
            target_count = fdps_collect_targets_in_range(
                data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                data_fdps_map_cursor_world_y / MAP_TILE_SIZE, targets,
                cast_range, AIM_SWEEP_KEEP_OWN_TILE,
                (int) spell->target_side);
            pick_result = fdps_map_cursor_select_loop(
                (int) spell->target_side, target_count, targets);
            fdps_map_grid_reset();

            /* BOTH SWEEPS RUN EVEN ON A CANCELLED AIM, and the second one is
               the count and the list a confirmed cast would be given. */
            if (cast_range > CAST_RANGE_LAST_RADIUS) {
                line_origin_unit = fdps_get_unit_record(unit_index);
                target_count = fdps_collect_targets_in_line(
                    data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                    data_fdps_map_cursor_world_y / MAP_TILE_SIZE, targets,
                    (int) line_origin_unit->pos_x,
                    (int) line_origin_unit->pos_y,
                    cast_range - CAST_RANGE_LINE_BIT,
                    LINE_SWEEP_KEEPS_ENEMY_SIDE);
                fdps_play_sfx(LINE_SWEEP_SOUND);
            } else {
                target_count = fdps_collect_targets_in_range(
                    data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                    data_fdps_map_cursor_world_y / MAP_TILE_SIZE, targets,
                    (int) spell->area, AIM_SWEEP_KEEP_OWN_TILE,
                    (int) spell->target_side);
            }
        } else {
            /* A map-wide spell: nothing is aimed, so the footprint goes back
               to a plain cursor and the sweep runs from where the cursor
               already is. */
            map_wide_radius = data_fdps_map_cursor_draw_mode
                              - CURSOR_FOOTPRINT_BIAS;
            data_fdps_map_cursor_draw_mode = CURSOR_OVERLAY_PLAIN;
            target_count = fdps_collect_targets_in_range(
                data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                data_fdps_map_cursor_world_y / MAP_TILE_SIZE, targets,
                map_wide_radius, AIM_SWEEP_KEEP_OWN_TILE,
                MAP_WIDE_SELECT_MODE);
            map_wide_aim_mode = MAP_WIDE_AIM_MODE;
            if (target_count == 0) {
                map_wide_aim_mode = MAP_WIDE_AIM_MODE_NO_TARGET;
            }
            pick_result = fdps_map_cursor_select_loop(map_wide_aim_mode,
                                                      MAP_WIDE_AIM_LIST_COUNT,
                                                      targets);
        }

        if (pick_result == SPELL_SELECT_CONFIRMED
            && spell_ids[cursor_index] == TELEPORT_SPELL_ID) {
            /* 傳送術 takes a second pick, the tile its one target is to be
               moved to.  A hit list that starts with the caster itself throws
               the whole cast away. */
            fdps_map_grid_reset();
            if ((int) targets[0] == unit_index) {
                pick_result = SPELL_SELECT_CANCELLED;
            }
            if (pick_result == SPELL_SELECT_CONFIRMED) {
                pick_result = fdps_map_cursor_select_loop(
                    DESTINATION_SELECT_MODE, (int) targets[0], NULL);
            }
            if (pick_result == SPELL_SELECT_CONFIRMED) {
                data_fdps_battle_teleport_dest_tile_x =
                    data_fdps_map_cursor_world_x / MAP_TILE_SIZE;
                data_fdps_teleport_destination_tile_y =
                    data_fdps_map_cursor_world_y / MAP_TILE_SIZE;
                data_fdps_map_cursor_draw_mode = CURSOR_OVERLAY_OFF;
                fdps_map_cursor_move_to_unit(unit_index);
                data_fdps_map_cursor_draw_mode = CURSOR_OVERLAY_PLAIN;
            }
        }

        fdps_map_grid_reset();

        if (pick_result == SPELL_SELECT_CONFIRMED) {
            fdps_combat_play_spell_on_targets(unit_index,
                                              (int) spell_ids[cursor_index],
                                              target_count, targets);
            death_script_count = fdps_collect_death_scripts(death_scripts);
            fdps_play_death_animation_and_mark_dead();
            fdps_run_death_scripts(unit_index, death_script_count,
                                   death_scripts);

            /* The haul is divided by the caster's level, so a haul smaller
               than the divisor rounds away to nothing. */
            effective_level = (int) caster->level;
            if ((int) caster->portrait_id > LAST_UNPROMOTED_PORTRAIT_ID) {
                effective_level += PROMOTED_LEVEL_BONUS;
            }
            data_fdps_battle_pending_xp_credit =
                data_fdps_battle_pending_xp_credit / effective_level;
            fdps_unit_award_exp_and_level_up(unit_index);
        }

        data_fdps_map_cursor_draw_mode = CURSOR_OVERLAY_PLAIN;
        fdps_map_cursor_move_to_unit(unit_index);

        if (pick_result == SPELL_SELECT_CONFIRMED) {
            return pick_result;
        }
    }
}
