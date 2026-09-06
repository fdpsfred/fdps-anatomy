/* village.c -- the village phase: the between-battle town screen.
 *
 * See village.h for what the phase covers.  This file holds the signboard
 * menu's walk animation, and the menu's hidden entry: the per-chapter
 * secret-shop unlock code and the state machine that matches one keystroke at
 * a time against it.
 *
 * abs, malloc and free come from <stdlib.h>, memmove from <string.h> and inp
 * from <conio.h>, which is where Watcom 10.0a declares each of them, and all
 * five are real calls in the original -- CALL 0x0003d364 at 00031fdc and
 * 00031fea, CALL 0x0003d375 at 00032064, CALL 0x0003d478 at 00032352, CALL
 * 0x0003d514 at 0003207c and 00032333 and CALL 0x0003d4e4 at 00032308 and
 * 00032319 -- because the flag set carries no -oi
 * (rebuild_info/build_flags.md), so the plain declarations are what reproduce
 * them.
 */
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "sprite.h"
#include "village.h"

/* The 320x200 8bpp page the whole game composes in, its pitch and its size,
   and the adapter's own linear frame buffer.  PUSH 0xfa00 at 0003205f and
   00032325, PUSH 0x140 at 000320c4 and PUSH 0xa0000 at 0003232e.  0xa0000 is
   written as a literal because it is where the display adapter answers and not
   the address of anything this rebuild places. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_BYTES 0xfa00

/* The adapter's input status register and its vertical retrace bit -- PUSH
   0x3da / TEST AL,0x8 at 00032303 and 00032314. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The blit modes this file asks fdps_blit_dispatch for (blit.h): the opaque
   pass-through for the marker and the empty plate frame, the scaler for the
   name plate that is growing or shrinking, and the translucent kernel for the
   marker's afterimages. */
#define BLIT_MODE_OPAQUE 0
#define BLIT_MODE_SCALED 4
#define BLIT_MODE_TRANSLUCENT 9

/* The three slots of a mode-9 blend descriptor, named as src/rleblend.c names
   them.  The address of the first is what reaches the kernel, so all three
   have to be one adjacent block in this order. */
#define BLEND_DESC_SHADE_RAMP 0
#define BLEND_DESC_LEVEL 1
#define BLEND_DESC_CUBE 2
#define BLEND_DESC_SLOTS 3

/* The game's direction codes, the ones fdps_animate_move_path dispatches on:
   0 down, 1 left, 2 up, 3 right.  MOV dword ptr [EBP-0x24] of 0x3, 0x1, 0x0
   and 0x2 at 00031ffc, 00032005, 00032014 and 0003201d. */
#define FACING_DOWN 0
#define FACING_LEFT 1
#define FACING_UP 2
#define FACING_RIGHT 3

/* A cache slot's twelve stream offsets are four facings of three walk cells
   (struct fdps_cel_cache_slot, src/fdpstype.h).  The marker is drawn from cell
   0 of its facing on every one of the six frames -- IMUL EDX,[EBP-0x24],0xc at
   0003203e with no cell displacement added -- so it slides across without
   cycling its legs. */
#define WALK_CELLS_PER_FACING 3
#define WALK_CELL_STANDING 0

/* The marker sprite's size, PUSH 0x18 / PUSH 0x18 at 000320c9. */
#define MARKER_W 0x18
#define MARKER_H 0x18

/* Six frames, i = 0..5, and the interpolation divisor is the LAST frame index
   rather than the count: CMP dword ptr [EBP-0x38],0x6 / JL at 0003202b and MOV
   EBX,0x5 / IDIV at 0003208e.  So the last frame lands the marker exactly on
   the destination. */
#define WALK_FRAMES 6
#define WALK_LAST_FRAME 5

/* The name plate's box on the screen -- PUSH 0xae / PUSH 0x3 at 000320ed for
   the corner, and 0x48 by 0x18 for the size the two centring terms subtract
   from at 0003215c and 00032145.  The plate is drawn at a multiple of a
   twelfth of that width by a quarter of its height, so it steps 72x24, 60x20,
   48x16 and back. */
#define PLATE_BOX_X 3
#define PLATE_BOX_Y 0xae
#define PLATE_BOX_W 0x48
#define PLATE_BOX_H 0x18
#define PLATE_WIDTH_STEP 0xc
#define PLATE_HEIGHT_STEP 4

/* Sprite 6 of the sheet is the empty plate frame, redrawn opaque under every
   frame's name plate: PUSH 0x6 at 000320fd. */
#define PLATE_FRAME_SPRITE 6

/* Which frame the plate being drawn changes over on.  The two tests are
   written against different constants -- CMP [EBP-0x38],0x3 / JGE at 00032139
   skips the outgoing plate and CMP [EBP-0x38],0x2 / JLE at 000321b2 skips the
   incoming one -- and they meet exactly here, so every frame draws exactly one
   plate and no frame draws both. */
#define PLATE_SWAP_FRAME 3

/* The two afterimages' blend levels, MOV dword ptr [EBP-0x48],0x8 at 00032209
   and 0xc at 0003228b.  A higher level is fainter (src/rleblend.h), so the
   older trail step is the dimmer one. */
#define GHOST_NEAR_LEVEL 8
#define GHOST_FAR_LEVEL 0xc

/* 00031f60.  Four stack arguments, caller-cleaned: all three call sites in
   fdps_village_signboard_menu push four dwords right to left and follow the
   CALL with ADD ESP,0x10, at 00031c7c, 00031ccd and 00031d1c, the body reads
   them at [EBP+0x14] through [EBP+0x20] behind PUSH EBX/ESI/EDI/EBP and the
   return address, and RET carries no immediate.  EAX is never set before the
   epilogue and no call site looks at it.

   The facing is the dominant axis of the whole journey, taken once before the
   loop and held for all six frames: abs(dx) against abs(dy) with CMP EBX,EAX /
   JLE at 00031ff2, so a tie goes to the vertical branch, and each branch then
   tests its own delta with JLE, so a delta of exactly zero reads as the
   negative direction.  Two destinations at the same point therefore face the
   marker up.

   THE TICK LATCH IS NEVER SEEDED.  The frame wait at 0003233b compares an
   uninitialised local against data_fdps_timer_tick_counter and only then
   latches it, so the first of the six frames ends its wait immediately unless
   the stack happens to hold the live tick.  Seeding it from the counter before
   the loop, which is the intuitive C, adds a tick to the first frame.

   THE SECOND AFTERIMAGE IS GUARDED AT BOTH ENDS.  CMP [EBP-0x38],0x1 / JLE
   with CMP [EBP-0x38],0x5 / JL at 0003227d is 1 < i && i < 5 and not i > 1, so
   the last frame deliberately carries a single ghost while the middle ones
   carry two.

   Every frame allocates its own composing page and frees it before the next
   one starts, so the animation holds one 64,000-byte block at a time and none
   at all across the return. */
void fdps_village_animate_walk_to_destination(unsigned char *background,
                                              int from_destination,
                                              int to_destination,
                                              unsigned char *signboard_cel)
{
    /* The descriptor the two afterimages are drawn through.  Only the level
       changes between them; the two table addresses are written once. */
    int blend_desc[BLEND_DESC_SLOTS];
    /* Where the marker starts and where it is walking to, in screen pixels. */
    int from_x;
    int from_y;
    int to_x;
    int to_y;
    /* The whole journey, read only to choose which way the marker faces. */
    int delta_x;
    int delta_y;
    /* That choice, one of the four direction codes. */
    int facing;
    /* Which of the six frames of the walk is being drawn. */
    int frame;
    /* Cell 0 of the chosen facing, out of the marker's own cache slot. */
    unsigned char *marker_stream;
    /* The 64,000-byte page this frame is composed in. */
    unsigned char *page;
    /* Where the marker's top left corner goes; reused for each afterimage's
       corner in turn, exactly as the original reuses the two slots. */
    int marker_x;
    int marker_y;
    /* The name plate's drawn size and the packed pair mode 4 reads it as. */
    int plate_width;
    int plate_height;
    int plate_size;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    blend_desc[BLEND_DESC_SHADE_RAMP] =
        (int) data_fdps_palette_shade_ramp_table;
    blend_desc[BLEND_DESC_CUBE] = (int) data_fdps_inverse_palette_cube;

    from_x = data_fdps_village_signboard_destination_x_table[from_destination];
    from_y = data_fdps_village_destination_marker_y_table[from_destination];
    to_x = data_fdps_village_signboard_destination_x_table[to_destination];
    to_y = data_fdps_village_destination_marker_y_table[to_destination];
    delta_x = to_x - from_x;
    delta_y = to_y - from_y;
    if (abs(delta_x) > abs(delta_y)) {
        if (delta_x > 0) {
            facing = FACING_RIGHT;
        } else {
            facing = FACING_LEFT;
        }
    } else {
        if (delta_y > 0) {
            facing = FACING_DOWN;
        } else {
            facing = FACING_UP;
        }
    }

    for (frame = 0; frame < WALK_FRAMES; frame++) {
        /* A stored offset is measured from the base of the cache block, not
           from the slot it was read out of.  It is resolved afresh every frame
           because that is where the original resolves it. */
        marker_stream = data_fdps_cel_sprite_cache_ptr
            + ((struct fdps_cel_cache_slot *) data_fdps_cel_sprite_cache_ptr)
                  [data_fdps_village_marker_roster_idx].sprite_offset[
                      facing * WALK_CELLS_PER_FACING + WALK_CELL_STANDING];
        page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
        memmove(page, background, (size_t) VGA_SCREEN_BYTES);

        marker_x = from_x - (from_x - to_x) * frame / WALK_LAST_FRAME;
        marker_y = from_y - (from_y - to_y) * frame / WALK_LAST_FRAME;
        fdps_blit_dispatch(marker_stream,
                           page + marker_y * VGA_SCREEN_PITCH + marker_x,
                           MARKER_W, MARKER_H, VGA_SCREEN_PITCH, 0,
                           BLIT_MODE_OPAQUE);
        fdps_cel_blit_sprite(signboard_cel, PLATE_FRAME_SPRITE, page,
                             VGA_SCREEN_PITCH, PLATE_BOX_X, PLATE_BOX_Y, 0,
                             BLIT_MODE_OPAQUE);

        /* The outgoing plate, full size on the first frame and two thirds of
           it by the third.  Both sizes are worked out whether or not the frame
           draws them, which is what the original does. */
        plate_height = (WALK_FRAMES - frame) * PLATE_HEIGHT_STEP;
        plate_width = (WALK_FRAMES - frame) * PLATE_WIDTH_STEP;
        plate_size = (plate_height << 16) + plate_width;
        if (frame < PLATE_SWAP_FRAME) {
            fdps_cel_blit_sprite(signboard_cel, from_destination, page,
                                 VGA_SCREEN_PITCH,
                                 PLATE_BOX_X + (PLATE_BOX_W - plate_width) / 2,
                                 PLATE_BOX_Y
                                     + (PLATE_BOX_H - plate_height) / 2,
                                 (unsigned int) plate_size,
                                 BLIT_MODE_SCALED);
        }

        /* The incoming plate, the same three sizes back the other way. */
        plate_height = (frame + 1) * PLATE_HEIGHT_STEP;
        plate_width = (frame + 1) * PLATE_WIDTH_STEP;
        plate_size = (plate_height << 16) + plate_width;
        if (frame >= PLATE_SWAP_FRAME) {
            fdps_cel_blit_sprite(signboard_cel, to_destination, page,
                                 VGA_SCREEN_PITCH,
                                 PLATE_BOX_X + (PLATE_BOX_W - plate_width) / 2,
                                 PLATE_BOX_Y
                                     + (PLATE_BOX_H - plate_height) / 2,
                                 (unsigned int) plate_size,
                                 BLIT_MODE_SCALED);
        }

        /* The trail: where the marker stood one frame ago, and where it stood
           two frames ago, both drawn translucent over the frame just
           composed. */
        if (frame > 0) {
            blend_desc[BLEND_DESC_LEVEL] = GHOST_NEAR_LEVEL;
            marker_x = from_x
                       - (from_x - to_x) * (frame - 1) / WALK_LAST_FRAME;
            marker_y = from_y
                       - (from_y - to_y) * (frame - 1) / WALK_LAST_FRAME;
            fdps_blit_dispatch(marker_stream,
                               page + marker_y * VGA_SCREEN_PITCH + marker_x,
                               MARKER_W, MARKER_H, VGA_SCREEN_PITCH,
                               (unsigned int) blend_desc,
                               BLIT_MODE_TRANSLUCENT);
        }
        if (frame > 1 && frame < WALK_LAST_FRAME) {
            blend_desc[BLEND_DESC_LEVEL] = GHOST_FAR_LEVEL;
            marker_x = from_x
                       - (from_x - to_x) * (frame - 2) / WALK_LAST_FRAME;
            marker_y = from_y
                       - (from_y - to_y) * (frame - 2) / WALK_LAST_FRAME;
            fdps_blit_dispatch(marker_stream,
                               page + marker_y * VGA_SCREEN_PITCH + marker_x,
                               MARKER_W, MARKER_H, VGA_SCREEN_PITCH,
                               (unsigned int) blend_desc,
                               BLIT_MODE_TRANSLUCENT);
        }

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* Spin until the retrace that may already be running has ended. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* And until the next one begins, so the whole page reaches the
               adapter inside the blanking interval. */
        }
        memmove((void *) VGA_SCREEN_BASE, page, (size_t) VGA_SCREEN_BYTES);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
        free(page);
    }
}

/* 000357a0.  One keystroke of the chapter's secret-shop unlock code.

   The table is a 24-row, eight-byte-per-row block of keyboard make codes that
   the original copies into a stack buffer on every call -- MOV ECX,0x30 /
   LEA EDI,[EBP-0xc4] / MOV ESI,0x310c0 / REP MOVSD -- so it is an ordinary
   automatic array with an initializer here and NOT a static const table.  The
   difference is visible: with chapter id 0 the row selector below reads the
   eight bytes underneath the buffer, which are whatever the previous frame
   left on the stack, whereas a static table would read the eight zero bytes
   that happen to sit in front of the block at 0x000310b8.

   The row selector is off by one.  The effective address the original builds
   is [EAX*8 + EBP - 0xcc] with EAX the chapter id, and the buffer starts at
   EBP - 0xc4, so the base of the indexing is the buffer MINUS one row: the row
   used is chapter_codes[chapter_id - 1].  The chapter id is 0-based, so row 0
   belongs to chapter id 1, the chapter the player sees as chapter 2.  The
   strategy guide's dump of this same table agrees independently -- "each
   chapter 8 bytes, from chapter 2 through chapter 25, beginning 4D 50 4D 1E"
   (docs/guide, fdps/modify2) -- and 4D 50 4D 1E is exactly row 0.  Writing the
   obvious chapter_codes[chapter_id] would give every chapter the next one's
   code.

   The byte read is compared as an unsigned value against the parameter: MOV AL
   / AND EAX,0xff / CMP EAX,[EBP+0x14], so the table's codes above 0x7f -- none
   in the shipped block, but the compare is what decides -- would still match a
   positive scancode rather than arriving negative.

   Four rows are all zero (rows 15, 16, 20 and 21, the chapters the player sees
   as 17, 18, 22 and 23), and the caller never passes scancode 0, so in those
   chapters the first compare always fails and the match position can never
   leave 0: those chapters have no code at all.

   The table does not cover every chapter.  The game has thirty (chapters/),
   so the chapter id runs 0..29, while the table's rows answer for ids 1..24
   only.  The original does not check that, and the six ids outside the table
   read the frame itself rather than the buffer, at EBP - 0xcc + id * 8: id 0
   lands eight bytes BELOW the buffer, on whatever the previous frame left
   there, and ids 25..29 land above it -- on the return-value slot, then the
   saved EBP, EDI, ESI and EBX, then the return address, and at id 28 on the
   incoming scancode itself.  That last one matches whatever key was pressed
   and has a zero byte behind it, so it reports the code complete on the first
   keystroke.  None of that is emulated here or special-cased: the array is
   indexed exactly as the original indexes it and lands on the same slots of
   the same frame, which is the only way the out-of-range chapters behave the
   same way at all.  A bounds check, a clamp, or a static const table would
   each change what those six chapters do.

   The row is scanned by the two branches below and never by a loop: a match
   advances the position and reports completion when the NEXT byte is the row's
   0 terminator or the position has reached 8, and a mismatch throws the
   attempt away but immediately re-opens it if the offending keystroke happens
   to be the code's own first byte.  The position-reached-8 half is unreachable
   with the shipped table, whose longest code is seven keys followed by a
   terminator, but it is in the original and the compare order is the
   original's: the terminator is tested first. */
int fdps_check_secret_code_key(int scancode)
{
    /* Row n is the code for chapter id n + 1, the player's chapter n + 2. */
    unsigned char chapter_codes[24][8] = {
        { 0x4d, 0x50, 0x4d, 0x1e },                          /* right down right A */
        { 0x4d, 0x4b, 0x50, 0x4d, 0x1e },                    /* right left down right A */
        { 0x48, 0x4b, 0x50, 0x4d, 0x48 },                    /* up left down right up */
        { 0x1f, 0x18, 0x1e, 0x13 },                          /* S O A R */
        { 0x1f, 0x12, 0x2e, 0x13, 0x12, 0x14 },              /* S E C R E T */
        { 0x02, 0x03, 0x03, 0x02, 0x02 },                    /* 1 2 2 1 1 */
        { 0x34, 0x34, 0x34, 0x34 },                          /* . . . . */
        { 0x30, 0x1e, 0x13, 0x31, 0x1e, 0x20, 0x18 },        /* B A R N A D O */
        { 0x12, 0x31, 0x14, 0x12, 0x13 },                    /* E N T E R */
        { 0x48, 0x4b, 0x50, 0x4d },                          /* up left down right */
        { 0x4b, 0x50, 0x4d },                                /* left down right */
        { 0x03, 0x0b, 0x0c, 0x05, 0x0b },                    /* 2 0 - 4 0 */
        { 0x31, 0x17, 0x22, 0x23, 0x14 },                    /* N I G H T */
        { 0x02, 0x06 },                                      /* 1 5 */
        { 0x26, 0x18, 0x14, 0x18, 0x13, 0x17, 0x1e },        /* L O T O R I A */
        { 0 },
        { 0 },
        { 0x02, 0x03 },                                      /* 1 2 */
        { 0x02, 0x04, 0x03, 0x05 },                          /* 1 3 2 4 */
        { 0x23, 0x12, 0x13, 0x18 },                          /* H E R O */
        { 0 },
        { 0 },
        { 0x50, 0x50, 0x50, 0x50 },                          /* down down down down */
        { 0x11, 0x12, 0x1f, 0x14 }                           /* W E S T */
    };

    if (chapter_codes[data_fdps_chapter_current_chapter_id - 1]
                     [data_fdps_secret_code_match_pos] == scancode) {
        data_fdps_secret_code_match_pos++;
        if (chapter_codes[data_fdps_chapter_current_chapter_id - 1]
                         [data_fdps_secret_code_match_pos] == 0 ||
            data_fdps_secret_code_match_pos == 8) {
            return 1;
        }
    } else {
        data_fdps_secret_code_match_pos = 0;
        /* The original reads the row's byte 0 as a literal index here and does
           not re-read the position it has just zeroed: MOV EAX,[0x69cf4] /
           LEA EAX,[EAX*8] with no ADD of [0x601c0]. */
        if (chapter_codes[data_fdps_chapter_current_chapter_id - 1][0] == scancode) {
            data_fdps_secret_code_match_pos = 1;
        }
    }
    return 0;
}
