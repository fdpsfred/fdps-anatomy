/* chapter.c -- the chapter frame.
 *
 * See chapter.h for what the frame is.  Nothing here holds state: every value
 * this file writes is a game-state global gamedata.c owns, and the chapter it
 * acts on is data_fdps_chapter_current_chapter_id.
 */
#include <conio.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "deploy.h"
#include "keybd.h"
#include "palette.h"
#include "sprite.h"
#include "vfs.h"
#include "chapter.h"
#include "chinit1.h"
#include "chinit1b.h"
#include "chinit2.h"
#include "chinit2b.h"

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* 00060074. Slot n holds fdps_chapter_NN_init for NN = n + 1, in chapter order
   with no gaps or repeats; these thirty relocated code pointers are the
   table's whole content and the only thing selecting which setup script runs
   on chapter entry. */
typedef void (*chapter_init_handler_fn)(void);
chapter_init_handler_fn data_fdps_chapter_init_handler_table[30] = {
    fdps_chapter_01_init,
    fdps_chapter_02_init,
    fdps_chapter_03_init,
    fdps_chapter_04_init,
    fdps_chapter_05_init,
    fdps_chapter_06_init,
    fdps_chapter_07_init,
    fdps_chapter_08_init,
    fdps_chapter_09_init,
    fdps_chapter_10_init,
    fdps_chapter_11_init,
    fdps_chapter_12_init,
    fdps_chapter_13_init,
    fdps_chapter_14_init,
    fdps_chapter_15_init,
    fdps_chapter_16_init,
    fdps_chapter_17_init,
    fdps_chapter_18_init,
    fdps_chapter_19_init,
    fdps_chapter_20_init,
    fdps_chapter_21_init,
    fdps_chapter_22_init,
    fdps_chapter_23_init,
    fdps_chapter_24_init,
    fdps_chapter_25_init,
    fdps_chapter_26_init,
    fdps_chapter_27_init,
    fdps_chapter_28_init,
    fdps_chapter_29_init,
    fdps_chapter_30_init
};

/* End of global data. */

/* How many bytes of the per-cell event flag table are cleared: PUSH 0x20 at
   0002277e, which is the whole of data_fdps_map_cell_event_triggered_flags's
   32 entries (gamedata.h).  Written as the literal length the original pushes
   rather than as sizeof, so a later change to the declared size cannot quietly
   change how much of it a chapter start clears. */
#define MAP_CELL_EVENT_FLAG_COUNT 0x20

/* 00022750.  Zero-sized frame and no locals at all: PUSH EBX / PUSH ESI /
   PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 and a matching POP sequence
   at 000227d1, with nothing addressed off EBP anywhere in between.  Every
   value the body touches is a global at an absolute address, which is why
   there is no name in the C below that is not one of them.

   Stack calling convention, and this body shows it from both ends: it takes no
   argument and reads no register or stack slot before writing one, and the one
   call it passes something to pushes the value and cleans up itself -- PUSH
   dword ptr [0x00069cf4] / CALL 0x00022be0 / ADD ESP,0x4 at
   00022770..0002277b, with fdps_build_map_unit_array reading it at [EBP+0x14].

   No value is used after any CALL.  memset's return is discarded (ADD ESP,0xc
   and then a store of a literal), and both game callees return void: the
   32 call sites of fdps_flush_keyboard_queue push nothing and read nothing
   back (src/keybd.c), and fdps_build_map_unit_array's result is not read here
   either.

   Straight line, no branches: the disassembly has no conditional jump and no
   loop, so the C is the ten statements in the order the stores appear.

   THE TWO STORES TO data_fdps_map_cursor_draw_mode ARE BOTH REAL.  0 goes in
   at 0002275c, before the unit array is rebuilt, and 1 at 000227b8 after it;
   collapsing them into the single store to 1 that the final state would
   suggest is the obvious tidy-up.  Nothing in the call tree under
   fdps_build_map_unit_array reads that global -- its callees are the resource
   loader, the wave deployer, the record accessors, the sprite cache and the
   CRT, and the four functions in the image that read it
   (fdps_draw_map_cursor, fdps_map_cursor_move_to, fdps_map_cursor_select_loop
   and fdps_battle_spell_command) are in none of their callee lists -- so the
   intermediate 0 is not observable inside one call.  It is emitted because the
   original stores it, not because a reader of it is known.

   The chapter id is read from the global and NOT from anything this function
   was handed, because it was handed nothing.  fdps_build_map_unit_array itself
   ignores the argument when it loads the chapter's resources and uses the same
   global (src/deploy.c), so the two agree only because this call site passes
   that global in; the argument is what names the map%02d.cod placement file.

   The order matters at one point and only one: the array rebuild runs BEFORE
   the counters below it are cleared.  fdps_build_map_unit_array reaches
   fdps_deploy_wave, which deploys the map's wave-0 units, and those go down on
   the tiles the placement table names -- so a reset that cleared the view
   window and the cursor first would still end with the same numbers in them.
   The per-cell event memset is the one that is not interchangeable: it has to
   follow the load, because the chapter whose flags are being cleared is the
   one the load just brought in.

   Contract E: the memset destination is MOV EAX,0x640d8 / PUSH EAX, an
   absolute address the rebuild will not reproduce, so it is written as the
   symbol.  Every other operand here is already a dword reference to a named
   global. */
void fdps_chapter_state_reset(void)
{
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_chapter_event_or_battle_end_code = 0;

    fdps_build_map_unit_array(data_fdps_chapter_current_chapter_id);

    memset(data_fdps_map_cell_event_triggered_flags, 0,
           MAP_CELL_EVENT_FLAG_COUNT);

    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_map_cursor_draw_mode = 1;
    data_fdps_battle_turn_counter = 1;

    fdps_flush_keyboard_queue();
}

/* --- fdps_show_chapter_title_card @ 00020c60 ---------------------------- */

/* The container the two title-card members live in, and the members
   themselves.  Both member names reach fdps_vfs_load_entry, which upper-cases
   the MEMBER name in place before the compare, so these two literals are
   permanently folded to CHAPTER.SAF and CHAPTER.PAL by the first call and
   cannot live in read-only storage (vfs.h, rebuild_info/pitfalls.md).  The
   container name is copied raw and is left as it stands. */
#define TITLE_CARD_ARCHIVE "MISC.VFS"
#define TITLE_CARD_IMAGE_MEMBER "Chapter.saf"
#define TITLE_CARD_PALETTE_MEMBER "Chapter.pal"

/* The adapter: mode 13h's linear aperture and the extents of one frame of it.
   0xa0000 is where the display adapter answers rather than the address of
   anything the rebuild places, so it stays a literal (rebuild_info/
   pitfalls.md, contract E). */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8
#define VGA_SCREEN_BYTES 0xfa00

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit either fade looks at. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The off-screen page the card is composed on: 368 by 248 with the sprite
   drawn at (24, 24), and only the 320x200 window at that origin -- byte
   0x18 * 0x170 + 0x18 -- put on the adapter.  The 24-pixel margin on all four
   sides is load bearing: a composite sprite's part records carry signed
   offsets and nothing clips them, so a tight 320x200 page drawn at (0, 0)
   lets a part that hangs off the picture write outside the allocation
   (rebuild_info/pitfalls.md). */
#define CARD_PAGE_PITCH 0x170
#define CARD_PAGE_ROWS 0xf8
#define CARD_PAGE_BYTES 0x16480
#define CARD_PAGE_MARGIN 0x18
#define CARD_PAGE_WINDOW_AT 0x2298

/* A fade rewrites the whole DAC: entries 0 to 255, inclusive at both ends. */
#define FADE_FIRST_DAC_ENTRY 0
#define FADE_LAST_DAC_ENTRY 0xff

/* The fade's shape.  Sixteen steps of four, so a step number of 0 uploads the
   source palette untouched and a step number of 16 biases every component by
   -64, which the upload's clamp turns into black whatever the source held.
   The blackout before the card is drawn pushes the same -64 in one go, and it
   is written as its own constant because the assembly pushes the literal
   -0x40 there rather than deriving it from the step count. */
#define FADE_STEPS 0x10
#define FADE_STEP_DARKENING 4
#define FADE_BLACKOUT_BIAS 0x40

/* How long one fade step is held, and how long the card stands at full
   brightness between the two fades. */
#define FADE_STEP_MS 0x50
#define TITLE_CARD_HOLD_MS 0x3e8

/* 00020c60.  Standard stack frame -- PUSH EBX / ESI / EDI / EBP, MOV EBP,ESP,
   SUB ESP,0x30 -- with nothing read at [EBP+8] or above anywhere in the body,
   so it takes no argument, and all thirty call sites confirm it from the
   outside: each is a bare CALL 0x00020c60 with no push before it and no
   ADD ESP after it (00020eb9 in fdps_chapter_01_init through 0002162f in
   fdps_chapter_30_init).  EAX is not read after the return at any of them
   either, so it returns nothing.  Stack calling convention.

   VALUES USED AFTER A CALL, and what the assembly says each one is.  Three
   CALLs hand something back and all three are pointers taken straight out of
   EAX: the two fdps_vfs_load_entry results are stored at [EBP-0xc] and
   [EBP-0x4] (00020c80, 00020c97) and the malloc result at [EBP-0x30]
   (00020cbb).  The image pointer is later copied into the request at
   00020cf3, the palette pointer is re-read at 00020da0 and 00020e1b as both
   fades' source, and the page pointer is re-read at 00020d39 to form
   page + 0x2298 for the blit; all three are handed to free on the way out.
   Nothing else is read after a CALL: memset's return is discarded, and the
   four other callees return void.  The one place the decompiler could have
   gone wrong -- inp's answer feeding the retrace test -- is TEST AL,0x8 / JZ
   back to the CALL at 00020d6e, so the tested value really is that call's
   result.

   CONTROL FLOW.  Straight line to 00020d4a, then two counted loops with a
   spin inside each and one delay between them.  The first is MOV [EBP-8],0x10
   with CMP against 0 / JGE and DEC at the bottom, so the counter runs 16 down
   to 0 INCLUSIVE -- seventeen steps, the last of them at bias 0, which is
   what leaves the card at full brightness.  The second is MOV [EBP-8],0x1
   with CMP against 0x10 / JLE and INC, so it runs 1 up to 16 -- sixteen
   steps, the first at bias -4 and the last at -64, black.  The asymmetry is
   the point: the full-brightness step is played once rather than twice, and
   mirroring either bound plays it twice or not at all.  Both counters are
   signed -- JGE and JLE, not JAE and JBE -- and the first has to be, because
   its exit depends on the counter going below zero being seen as below zero.

   THE PAGE IS NOT CLEARED.  malloc's block is handed to the drawer as it
   comes, with no memset of its own; what makes that safe is that the card
   covers the whole 320x200 window, not that the page starts blank.

   THE CHAPTER IS THE GLOBAL.  MOV EAX,[0x00069cf4] at 00020ce8 goes into the
   request's item index, so which of the thirty cards is drawn is whatever
   data_fdps_chapter_current_chapter_id holds when the call is made.  The
   caller is not what puts it there.  0x00069cf4 has thirty-four writes in
   the whole image and not one is inside the entry handlers'
   00020e90..0002164f range.  Twenty-nine of them are the chapter-end
   handlers fdps_chapter_01_end .. fdps_chapter_29_end, each storing the
   next chapter's index as a literal immediately after CALL 0x00039e70 and
   just before its epilogue: 0003a440 stores 1, 0003ba39 stores 0x1d, and
   the twenty-seven between them run in address order chapter by chapter.
   fdps_chapter_30_end has no such write, chapter 30 being the last.  The
   other five enter a chapter from outside that sequence -- 0002a841 in
   fdps_title_screen (literal 0), 00023f58 in fdps_load_savegame and
   000245ae in fdps_load_game_screen (a byte out of the save record),
   0002ac1c in fdps_title_demo (literal 0x19) and 00021c26 in
   fdps_icon_script_run (a script operand byte).  The global is already
   correct when the handler runs.

   WHAT THE CALLER INHERITS.  The closing pair is a memset of the aperture and
   an upload of data_fdps_vga_main_palette_ptr at bias 0 -- a live palette
   over a cleared screen, not a black DAC -- so whatever comes next has to
   repaint.  The bias there is three PUSH 0x0 at 00020e47..00020e4b and not
   the -0x40 the fade ended on; carrying the fade's last bias through instead
   would leave the game running behind a black DAC. */
void fdps_show_chapter_title_card(void)
{
    /* The nine-dword draw request sprite.h describes, built on the stack at
       [EBP-0x30] .. [EBP-0x10] and handed to the composite drawer once. */
    int title_card_request[DRAW_REQUEST_DWORDS];
    /* Chapter.saf: the sprite bank holding one composite entry per chapter.
       This function owns it and frees it. */
    void *title_card_bank;
    /* Chapter.pal: the 256 six-bit RGB triples both fades ramp. */
    struct fdps_palette_entry *title_card_palette;
    /* The 368x248 page the card is composed on before any of it is seen. */
    unsigned char *work_page;
    /* How many steps of darkening the current fade step stands at: 0 is the
       source palette untouched and 16 is black.  The bias handed to the
       upload is its negation scaled by four, which is the NEG EAX /
       LEA EAX,[EAX*4] pair the assembly repeats once per channel. */
    int fade_step;

    title_card_bank = fdps_vfs_load_entry(TITLE_CARD_ARCHIVE,
                                          TITLE_CARD_IMAGE_MEMBER);
    title_card_palette = (struct fdps_palette_entry *)
        fdps_vfs_load_entry(TITLE_CARD_ARCHIVE, TITLE_CARD_PALETTE_MEMBER);

    memset((void *) VGA_SCREEN_BASE, 0, (size_t) VGA_SCREEN_BYTES);

    work_page = (unsigned char *) malloc((size_t) CARD_PAGE_BYTES);
    title_card_request[DRAW_REQUEST_DEST_BASE] = (int) work_page;
    title_card_request[DRAW_REQUEST_DEST_PITCH] = CARD_PAGE_PITCH;
    title_card_request[DRAW_REQUEST_DEST_ROWS] = CARD_PAGE_ROWS;
    title_card_request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    title_card_request[DRAW_REQUEST_BLIT_MODE] = 0;
    title_card_request[DRAW_REQUEST_X] = CARD_PAGE_MARGIN;
    title_card_request[DRAW_REQUEST_Y] = CARD_PAGE_MARGIN;
    title_card_request[DRAW_REQUEST_ITEM_INDEX] =
        data_fdps_chapter_current_chapter_id;
    title_card_request[DRAW_REQUEST_IMAGE] = (int) title_card_bank;

    fdps_set_palette_range(
        (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
        FADE_FIRST_DAC_ENTRY, FADE_LAST_DAC_ENTRY,
        -FADE_BLACKOUT_BIAS, -FADE_BLACKOUT_BIAS, -FADE_BLACKOUT_BIAS);

    fdps_draw_composite_sprite(title_card_request, 0);
    fdps_blit_rect((unsigned int) (work_page + CARD_PAGE_WINDOW_AT),
                   CARD_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                   VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);

    for (fade_step = FADE_STEPS; fade_step >= 0; fade_step--) {
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so each step of the ramp lands
               on its own displayed frame. */
        }

        fdps_set_palette_range(title_card_palette, FADE_FIRST_DAC_ENTRY,
                               FADE_LAST_DAC_ENTRY,
                               -fade_step * FADE_STEP_DARKENING,
                               -fade_step * FADE_STEP_DARKENING,
                               -fade_step * FADE_STEP_DARKENING);

        delay((unsigned int) FADE_STEP_MS);
    }

    delay((unsigned int) TITLE_CARD_HOLD_MS);

    for (fade_step = 1; fade_step <= FADE_STEPS; fade_step++) {
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* The same spin, for the same reason. */
        }

        fdps_set_palette_range(title_card_palette, FADE_FIRST_DAC_ENTRY,
                               FADE_LAST_DAC_ENTRY,
                               -fade_step * FADE_STEP_DARKENING,
                               -fade_step * FADE_STEP_DARKENING,
                               -fade_step * FADE_STEP_DARKENING);

        delay((unsigned int) FADE_STEP_MS);
    }

    memset((void *) VGA_SCREEN_BASE, 0, (size_t) VGA_SCREEN_BYTES);
    fdps_set_palette_range(
        (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
        FADE_FIRST_DAC_ENTRY, FADE_LAST_DAC_ENTRY, 0, 0, 0);

    free(title_card_bank);
    free(title_card_palette);
    free(work_page);
}
