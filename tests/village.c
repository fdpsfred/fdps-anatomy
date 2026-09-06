/* tests/village.c -- cover for src/village.c.
 *
 * One section per function, each stating where its expected values come from.
 *
 * ---- fdps_check_secret_code_key, 000357a0 ---------------------------------
 *
 * Expected values come from the assembly at 000357a0 and from the 192 table
 * bytes the image holds at 000310c0, cross-checked against the strategy
 * guide's own dump of the same table (docs/guide, fdps/modify2: "each chapter
 * 8 bytes, from chapter 2 through chapter 25, beginning 4D 50 4D 1E").  None
 * of them is read off the emitted C.
 *
 * Both globals the function touches are set by every test before it calls, so
 * nothing here depends on what the not-yet-emitted data definitions hold.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "cdaudio.h"
#include "keybd.h"
#include "village.h"

/* Chapter id 5 is the player's chapter 6, whose row is 1f 12 2e 13 12 14 --
   S E C R E T.  Every keystroke but the last leaves a non-zero byte ahead of
   the position, so CMP byte ptr [...],0 fails and the function reports 0; the
   sixth advances the position to 6, where the row's terminator sits, and the
   JZ takes the return-1 path. */
static void village_secret_code_completes(void)
{
    data_fdps_chapter_current_chapter_id = 5;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x1f), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 1);
    CHECK_EQ(fdps_check_secret_code_key(0x12), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 2);
    CHECK_EQ(fdps_check_secret_code_key(0x2e), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 3);
    CHECK_EQ(fdps_check_secret_code_key(0x13), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 4);
    CHECK_EQ(fdps_check_secret_code_key(0x12), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 5);
    CHECK_EQ(fdps_check_secret_code_key(0x14), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 6);
}

/* The row selector is chapter id minus one: the effective base of the indexing
   is the stack buffer minus one row, [EAX*8 + EBP - 0xcc] against a buffer at
   EBP - 0xc4.  Chapter id 1 therefore uses row 0, 4d 50 4d 1e -- right, down,
   right, A -- which is the first code the guide lists, for the player's
   chapter 2. */
static void village_row_is_chapter_id_minus_one(void)
{
    data_fdps_chapter_current_chapter_id = 1;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x1e), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 4);
}

/* The same keystrokes under chapter id 2, whose row is the next one down --
   4d 4b 50 4d 1e, right left down right A.  The second key already breaks the
   match, which is what an emit that indexed the table with the chapter id
   itself would get wrong: it would answer 1 on the fourth key here. */
static void village_next_chapter_has_the_next_row(void)
{
    data_fdps_chapter_current_chapter_id = 2;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 1);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x4b), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x1e), 1);
}

/* Nothing resets the position on entry: the first compare reads the row at
   whatever the global already holds.  Position 3 of the SECRET row is 0x13, R,
   and matching it moves the position on to 4 without completing anything. */
static void village_resumes_at_stored_position(void)
{
    data_fdps_chapter_current_chapter_id = 5;
    data_fdps_secret_code_match_pos = 3;

    CHECK_EQ(fdps_check_secret_code_key(0x13), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 4);
}

/* A key that is neither the expected byte nor the row's first byte throws the
   attempt away: MOV [0x601c0],0, then the second compare fails too and the
   position is left at 0.  0x30 is B, which the SECRET row does not contain. */
static void village_mismatch_resets_position(void)
{
    data_fdps_chapter_current_chapter_id = 5;
    data_fdps_secret_code_match_pos = 3;

    CHECK_EQ(fdps_check_secret_code_key(0x30), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
}

/* A key that breaks the run but happens to be the code's own first byte starts
   the next attempt on that same keystroke: the zero store is followed by
   MOV [0x601c0],1.  S at position 3 -- where R was expected -- is the case. */
static void village_mismatch_restarts_on_first_byte(void)
{
    data_fdps_chapter_current_chapter_id = 5;
    data_fdps_secret_code_match_pos = 3;

    CHECK_EQ(fdps_check_secret_code_key(0x1f), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 1);
}

/* Row 15 -- chapter id 16, the player's chapter 17 -- is eight zero bytes, and
   so are rows 16, 20 and 21.  The caller never passes scancode 0, so the first
   compare can never hold, the restart compare against the row's zero first
   byte cannot hold either, and the position is pinned at 0 for good. */
static void village_chapter_without_a_code(void)
{
    data_fdps_chapter_current_chapter_id = 16;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x1e), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
    CHECK_EQ(fdps_check_secret_code_key(0x01), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
}

/* The longest code in the table is row 7, chapter id 8: 30 1e 13 31 1e 20 18,
   B A R N A D O, seven keys with the terminator in the row's last byte.  It is
   the terminator that ends the code, not the position reaching 8 -- that half
   of the || is unreachable with this table -- and the position is left at 7. */
static void village_longest_code_is_seven_keys(void)
{
    data_fdps_chapter_current_chapter_id = 8;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x30), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x1e), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x13), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x31), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x1e), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x20), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x18), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 7);
}

/* The table's last row is 23, chapter id 24, the player's chapter 25 -- the
   last chapter the guide lists a code for.  11 12 1f 14 is W E S T. */
static void village_last_chapter_code(void)
{
    data_fdps_chapter_current_chapter_id = 24;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x11), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x12), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x1f), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x14), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 4);
}

/* A two-key code, row 13 for chapter id 14: 02 06, the digits 1 and 5.  The
   completion is reported on the second key even though six bytes of the row
   are still ahead of the position. */
static void village_two_key_code(void)
{
    data_fdps_chapter_current_chapter_id = 14;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x02), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x06), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 2);
}

/* A code whose bytes repeat: row 22, chapter id 23, is 50 50 50 50 -- the down
   arrow four times.  Each key both extends the run and equals the row's first
   byte, so the restart branch is never reached and the count is the run
   length. */
static void village_repeated_key_code(void)
{
    data_fdps_chapter_current_chapter_id = 23;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 4);
}

/* ---- fdps_village_animate_walk_to_destination, 00031f60 ------------------
 *
 * Expected values come from the assembly at 00031f60: MOV EAX,[EAX*4 + 0x60174]
 * and the same at 0x6018c at 00031f84 and 00031f97 for the two coordinate
 * tables; CMP EBX,EAX / JLE at 00031ff2 with CMP [EBP-0x2c],0x0 / JLE at
 * 00031ff6 and CMP [EBP-0x28],0x0 / JLE at 0003200e for the facing; CMP
 * [EBP-0x38],0x6 / JL at 0003202b with MOV EBX,0x5 / IDIV at 0003208e and
 * 000320b6 for the six frames and the interpolation; IMUL EDX,[EBP-0x24],0xc /
 * IMUL EAX,[0x601a4],0x30 at 0003203e for the marker's cache entry; PUSH 0x6
 * with PUSH 0xae / PUSH 0x3 at 000320e9 for the empty plate frame; the two size
 * blocks at 0003210b and 0003218a with CMP [EBP-0x38],0x3 / JGE at 00032139 and
 * CMP [EBP-0x38],0x2 / JLE at 000321b2 for which plate is drawn at what size;
 * MOV [EBP-0x48],0x8 at 00032209 and 0xc at 0003228b behind CMP [EBP-0x38],0x0
 * / JLE and CMP [EBP-0x38],0x1 / JLE with CMP [EBP-0x38],0x5 / JL at 0003227d
 * for the two afterimages; and PUSH 0xfa00 / PUSH 0xa0000 at 00032325 for the
 * present.  None of them is read off the emitted C.
 *
 * HOW THE RUN IS WATCHED.  The page is allocated and freed inside the call and
 * the only thing that leaves the function is the memmove to 0xa0000, so the
 * adapter is where the output is read back from, exactly as the attack- and
 * vfs-animation cases in tests/anim.c do it.  Only the sixth frame survives to
 * the end of the call, so the five earlier ones are watched the way tests/anim.c
 * watches a fade: the timer ISR that drives data_fdps_timer_tick_counter also
 * copies five probe bytes off the adapter every tick, and every frame but the
 * first is guaranteed to be on the adapter across a whole tick, because each one
 * ends by spinning until the counter leaves the value the frame before it
 * latched.
 *
 * THE FIXTURE ENCODES WHAT IT WANTS TO IDENTIFY IN THE PIXEL VALUE.  Each of the
 * four facings gets its own marker colour, each sheet sprite its own plate
 * colour, and the blend tables are built so that a translucent pixel resolves to
 * a value that names both the marker it came from AND the blend level it was
 * drawn at: the shade ramp's two source rows are seeded differently, so level 8
 * lands in one band of the inverse cube and level 0xc in another.  A frame's
 * pixels therefore say which sprite, which facing and which trail step painted
 * them.
 *
 * The globals it stages are all pending data emits, so nothing here asserts what
 * any of them holds: every case writes them before it calls.
 */

#define VILL_VGA_BASE 0x000a0000
#define VILL_SCREEN_W 0x140
#define VILL_SCREEN_H 0xc8
#define VILL_SCREEN_BYTES (VILL_SCREEN_W * VILL_SCREEN_H)
#define VILL_MODE_TEXT 0x03
#define VILL_MODE_320X200X256 0x13
#define VILL_TIMER_VECTOR 8

/* What the caller's page is filled with, so anything the animation did not
   paint reads back as this. */
#define VILL_BACKGROUND_FILL 0x11

/* The sheet: 0x48 by 0x18, which is the plate box the function centres in, with
   seven sprites and the sentinel the .CEL layout carries after them.  Every
   sprite is one flat colour, two fill commands to a row -- 64 pixels then 8 --
   because a run carries at most 64 (resource_info/cel.md). */
#define VILL_SHEET_SPRITES 7
#define VILL_SHEET_W 0x48
#define VILL_SHEET_H 0x18
#define VILL_SHEET_TABLE_AT 15
#define VILL_SHEET_STREAM_AT (VILL_SHEET_TABLE_AT + (VILL_SHEET_SPRITES + 1) * 4)
#define VILL_SHEET_ROW_BYTES 4
#define VILL_SHEET_STREAM_BYTES (VILL_SHEET_H * VILL_SHEET_ROW_BYTES)
#define VILL_SHEET_BYTES \
    (VILL_SHEET_STREAM_AT + VILL_SHEET_SPRITES * VILL_SHEET_STREAM_BYTES)
#define VILL_SHEET_CMD_64 0x3f
#define VILL_SHEET_CMD_8 0x07

/* Sprite s is drawn in colour 0x40 + s, so sprite 6 -- the empty plate frame --
   is 0x46 and the two destinations under test are 0x40 and 0x41. */
#define VILL_PLATE_COLOR(sprite) (0x40 + (sprite))
#define VILL_PLATE_FRAME_SPRITE 6

/* The marker: 24 by 24, one fill command to a row. */
#define VILL_MARKER_W 0x18
#define VILL_MARKER_H 0x18
#define VILL_MARKER_ROW_BYTES 2
#define VILL_MARKER_STREAM_BYTES (VILL_MARKER_H * VILL_MARKER_ROW_BYTES)
#define VILL_MARKER_CMD_24 0x17

/* The cache block: thirty slots of twelve stream offsets, then the five
   streams.  Four of them are the facings of the slot under test and the fifth
   is what every other slot and every other cell points at, so a marker taken
   from the wrong slot or the wrong facing shows up as its own colour rather
   than reading off the end of the block. */
#define VILL_CACHE_SLOTS 30
#define VILL_CACHE_TABLE_BYTES \
    (VILL_CACHE_SLOTS * (int) sizeof(struct fdps_cel_cache_slot))
#define VILL_CACHE_STREAMS 5
#define VILL_CACHE_BYTES \
    (VILL_CACHE_TABLE_BYTES + VILL_CACHE_STREAMS * VILL_MARKER_STREAM_BYTES)
#define VILL_ROSTER_SLOT 2
#define VILL_CELLS_PER_FACING 3
#define VILL_FACINGS 4
#define VILL_CELLS_PER_SLOT (VILL_FACINGS * VILL_CELLS_PER_FACING)

/* Facing f is drawn in colour 0x21 + f, so the low nibble of a marker pixel is
   f + 1: 1 down, 2 left, 3 up, 4 right.  The stray stream is 0x2f. */
#define VILL_MARKER_COLOR(facing) (0x21 + (facing))
#define VILL_STRAY_COLOR 0x2f
#define VILL_FACING_DOWN 0
#define VILL_FACING_LEFT 1
#define VILL_FACING_UP 2
#define VILL_FACING_RIGHT 3

/* The blend fixture.  fdps_rle_blit_translucent weights the source pixel
   through one shade-ramp row and the destination pixel through another, adds
   them, shifts right by four, masks to 0x000f0f0f and looks the result up in
   the inverse cube (src/rleblend.c).  Both destination rows are left zero, so
   only the source pixel decides; level 8 reads row 17 and level 0xc reads row
   4, and those two rows are seeded so that the same marker pixel resolves to
   0xe0 + low nibble through one and 0xf0 + low nibble through the other.  A
   translucent pixel therefore names its own marker AND its own blend level. */
#define VILL_RAMP_ROW_ENTRIES 0x100
#define VILL_RAMP_ROW_LEVEL8 17
#define VILL_RAMP_ROW_LEVEL12 4
#define VILL_RAMP_DEST_ROW_LEVEL8 8
#define VILL_RAMP_DEST_ROW_LEVEL12 13
#define VILL_FAR_CUBE_BIAS 8
#define VILL_NEAR_GHOST_COLOR(facing) (0xe0 + (facing) + 1)
#define VILL_FAR_GHOST_COLOR(facing) (0xf0 + (facing) + 1)

/* The walk the sampled cases use: six steps 30 pixels apart along one row, so
   no two of them overlap the 24-pixel marker and each trail step can be read on
   its own.  10, 40, 70, 100, 130 and 160 are from_x + (to_x - from_x) * i / 5
   with the IDIV of 00032098 exact at every i. */
#define VILL_WALK_FROM_X 10
#define VILL_WALK_FROM_Y 50
#define VILL_WALK_TO_X 160
#define VILL_WALK_TO_Y 50
#define VILL_WALK_STEP_X 30
#define VILL_MARKER_MID 12

/* The two destinations every case uses, and the frame count. */
#define VILL_DEST_FROM 0
#define VILL_DEST_TO 1
#define VILL_FRAMES 6

/* The plate box and the three probes inside it.  A full 0x48 by 0x18 plate
   covers all three, a 0x3c by 0x14 one covers the second and the third and a
   0x30 by 0x10 one only the third: the corners follow from PLATE_BOX_X +
   (0x48 - width) / 2 and PLATE_BOX_Y + (0x18 - height) / 2, which put the
   three sizes at (3,174), (9,176) and (15,178). */
#define VILL_PLATE_BOX_X 3
#define VILL_PLATE_BOX_Y 0xae
#define VILL_PROBE_PLATE_FULL 0
#define VILL_PROBE_PLATE_MID 1
#define VILL_PROBE_PLATE_SMALL 2
#define VILL_PROBE_STEP0 3
#define VILL_PROBE_STEP1 4
#define VILL_PROBES 5
#define VILL_SAMPLE_MAX 64

static unsigned char vill_sheet[VILL_SHEET_BYTES];
static unsigned char vill_cache[VILL_CACHE_BYTES];
static unsigned char *vill_screen;
static unsigned char *vill_background;
static void (__interrupt __far *vill_saved_timer)();
static int vill_blocks_before;
static int vill_blocks_after;
static unsigned int vill_ticks_used;
static volatile int vill_sample_count;
static volatile unsigned char vill_samples[VILL_SAMPLE_MAX][VILL_PROBES];
static int vill_probe_at[VILL_PROBES];

static void __interrupt __far vill_timer_isr(void)
{
    int probe;

    ++data_fdps_timer_tick_counter;
    if (vill_sample_count < VILL_SAMPLE_MAX) {
        for (probe = 0; probe < VILL_PROBES; probe++) {
            vill_samples[vill_sample_count][probe] =
                ((unsigned char *) VILL_VGA_BASE)[vill_probe_at[probe]];
        }
        vill_sample_count = vill_sample_count + 1;
    }
    _chain_intr(vill_saved_timer);
}

/* Seven flat sprites of 0x48 by 0x18 behind a header that states that size, and
   an offset table whose entries are measured from the start of the file. */
static void vill_stage_sheet(void)
{
    struct fdps_cel_header *header;
    int sprite;
    int row;
    int at;

    memset(vill_sheet, 0, (size_t) VILL_SHEET_BYTES);
    header = (struct fdps_cel_header *) vill_sheet;
    header->magic[0] = 'C';
    header->magic[1] = 'E';
    header->magic[2] = 'L';
    header->sprite_width = VILL_SHEET_W;
    header->sprite_height = VILL_SHEET_H;
    header->sprite_count = VILL_SHEET_SPRITES;

    for (sprite = 0; sprite < VILL_SHEET_SPRITES; sprite++) {
        at = VILL_SHEET_STREAM_AT + sprite * VILL_SHEET_STREAM_BYTES;
        *(int *) (vill_sheet + VILL_SHEET_TABLE_AT + sprite * 4) = at;
        for (row = 0; row < VILL_SHEET_H; row++) {
            vill_sheet[at + row * VILL_SHEET_ROW_BYTES] = VILL_SHEET_CMD_64;
            vill_sheet[at + row * VILL_SHEET_ROW_BYTES + 1] =
                (unsigned char) VILL_PLATE_COLOR(sprite);
            vill_sheet[at + row * VILL_SHEET_ROW_BYTES + 2] = VILL_SHEET_CMD_8;
            vill_sheet[at + row * VILL_SHEET_ROW_BYTES + 3] =
                (unsigned char) VILL_PLATE_COLOR(sprite);
        }
    }
    *(int *) (vill_sheet + VILL_SHEET_TABLE_AT + VILL_SHEET_SPRITES * 4) =
        VILL_SHEET_BYTES;
}

/* Five flat 24 by 24 markers, and a slot table that points every cell of every
   slot at the stray one before the slot under test is given the four facings at
   cell 0 of each. */
static void vill_stage_cache(void)
{
    struct fdps_cel_cache_slot *slots;
    int stream;
    int slot;
    int cell;
    int row;
    int at;
    int color;

    memset(vill_cache, 0, (size_t) VILL_CACHE_BYTES);
    for (stream = 0; stream < VILL_CACHE_STREAMS; stream++) {
        at = VILL_CACHE_TABLE_BYTES + stream * VILL_MARKER_STREAM_BYTES;
        if (stream < VILL_FACINGS) {
            color = VILL_MARKER_COLOR(stream);
        } else {
            color = VILL_STRAY_COLOR;
        }
        for (row = 0; row < VILL_MARKER_H; row++) {
            vill_cache[at + row * VILL_MARKER_ROW_BYTES] = VILL_MARKER_CMD_24;
            vill_cache[at + row * VILL_MARKER_ROW_BYTES + 1] =
                (unsigned char) color;
        }
    }

    slots = (struct fdps_cel_cache_slot *) vill_cache;
    for (slot = 0; slot < VILL_CACHE_SLOTS; slot++) {
        for (cell = 0; cell < VILL_CELLS_PER_SLOT; cell++) {
            slots[slot].sprite_offset[cell] = VILL_CACHE_TABLE_BYTES
                + VILL_FACINGS * VILL_MARKER_STREAM_BYTES;
        }
    }
    for (cell = 0; cell < VILL_FACINGS; cell++) {
        slots[VILL_ROSTER_SLOT].sprite_offset[cell * VILL_CELLS_PER_FACING] =
            VILL_CACHE_TABLE_BYTES + cell * VILL_MARKER_STREAM_BYTES;
    }
    data_fdps_cel_sprite_cache_ptr = vill_cache;
}

static void vill_stage_blend(void)
{
    int pixel;

    memset(data_fdps_palette_shade_ramp_table, 0,
           sizeof(data_fdps_palette_shade_ramp_table));
    memset(data_fdps_inverse_palette_cube, 0,
           sizeof(data_fdps_inverse_palette_cube));

    for (pixel = 0; pixel < VILL_RAMP_ROW_ENTRIES; pixel++) {
        /* Level 8's source row: the pixel back unshifted, so the cube index is
           the pixel's own low nibble. */
        data_fdps_palette_shade_ramp_table[VILL_RAMP_ROW_LEVEL8
                                           * VILL_RAMP_ROW_ENTRIES + pixel] =
            (unsigned int) (pixel * 16);
        /* Level 0xc's source row: the low nibble carried eight cube entries
           further up. */
        data_fdps_palette_shade_ramp_table[VILL_RAMP_ROW_LEVEL12
                                           * VILL_RAMP_ROW_ENTRIES + pixel] =
            (unsigned int) (((pixel & 0x0f) + VILL_FAR_CUBE_BIAS) * 16);
    }
    /* Only the entries the four facings can reach: the low nibble of a marker
       pixel is its facing plus one, so 1..4 through level 8's row and 9..12
       through level 0xc's.  Writing a whole band of each would make the two
       overlap and the far ghost would come back as a near one. */
    for (pixel = 1; pixel <= VILL_FACINGS; pixel++) {
        data_fdps_inverse_palette_cube[pixel] = (unsigned char) (0xe0 + pixel);
        data_fdps_inverse_palette_cube[pixel + VILL_FAR_CUBE_BIAS] =
            (unsigned char) (0xf0 + pixel);
    }
    /* And what the stray marker resolves to, so a wrong cache slot or a wrong
       facing shows as its own value rather than as one of the four. */
    data_fdps_inverse_palette_cube[VILL_STRAY_COLOR & 0x0f] = 0xef;
}

static void vill_place(int destination, int x, int y)
{
    data_fdps_village_signboard_destination_x_table[destination] = x;
    data_fdps_village_destination_marker_y_table[destination] = y;
}

static void vill_stage(void)
{
    vill_stage_sheet();
    vill_stage_cache();
    vill_stage_blend();
    data_fdps_village_marker_roster_idx = VILL_ROSTER_SLOT;
    vill_place(VILL_DEST_FROM, VILL_WALK_FROM_X, VILL_WALK_FROM_Y);
    vill_place(VILL_DEST_TO, VILL_WALK_TO_X, VILL_WALK_TO_Y);
}

/* Put the cache pointer back the way a freshly started program has it, for the
   reason tests/anim.c gives: it holds a block the game's own loaders realloc
   and free, and leaving it pointing at a static here hands a later test a free
   of storage that never came from the heap. */
static void vill_unstage(void)
{
    data_fdps_cel_sprite_cache_ptr = NULL;
    free(vill_screen);
    free(vill_background);
    vill_screen = NULL;
    vill_background = NULL;
}

static void vill_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Used entries currently in the heap, so a case can say the page came back. */
static int vill_used_heap_blocks(void)
{
    struct _heapinfo entry;
    int used;

    used = 0;
    entry._pentry = NULL;
    while (_heapwalk(&entry) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY) {
            used++;
        }
    }
    return used;
}

/* One whole run, leaving the sixth frame in vill_screen[] and the per-tick
   probes in vill_samples[]. */
static void vill_run(int from_destination, int to_destination)
{
    unsigned int before_ticks;

    vill_screen = (unsigned char *) malloc((size_t) VILL_SCREEN_BYTES);
    vill_background = (unsigned char *) malloc((size_t) VILL_SCREEN_BYTES);
    CHECK_EQ(vill_screen != NULL && vill_background != NULL, 1);
    if (vill_screen == NULL || vill_background == NULL) {
        return;
    }
    memset(vill_background, VILL_BACKGROUND_FILL, (size_t) VILL_SCREEN_BYTES);

    vill_sample_count = 0;
    vill_probe_at[VILL_PROBE_PLATE_FULL] =
        VILL_PLATE_BOX_Y * VILL_SCREEN_W + VILL_PLATE_BOX_X;
    vill_probe_at[VILL_PROBE_PLATE_MID] =
        (VILL_PLATE_BOX_Y + 2) * VILL_SCREEN_W + VILL_PLATE_BOX_X + 6;
    vill_probe_at[VILL_PROBE_PLATE_SMALL] =
        (VILL_PLATE_BOX_Y + 4) * VILL_SCREEN_W + VILL_PLATE_BOX_X + 12;
    vill_probe_at[VILL_PROBE_STEP0] =
        (VILL_WALK_FROM_Y + VILL_MARKER_MID) * VILL_SCREEN_W
        + VILL_WALK_FROM_X + VILL_MARKER_MID;
    vill_probe_at[VILL_PROBE_STEP1] =
        (VILL_WALK_FROM_Y + VILL_MARKER_MID) * VILL_SCREEN_W
        + VILL_WALK_FROM_X + VILL_WALK_STEP_X + VILL_MARKER_MID;

    vill_blocks_before = vill_used_heap_blocks();
    vill_set_mode(VILL_MODE_320X200X256);

    vill_saved_timer = _dos_getvect(VILL_TIMER_VECTOR);
    _dos_setvect(VILL_TIMER_VECTOR, vill_timer_isr);
    before_ticks = data_fdps_timer_tick_counter;
    fdps_village_animate_walk_to_destination(vill_background, from_destination,
                                             to_destination, vill_sheet);
    vill_ticks_used = data_fdps_timer_tick_counter - before_ticks;
    _dos_setvect(VILL_TIMER_VECTOR, vill_saved_timer);

    memmove(vill_screen, (void *) VILL_VGA_BASE, (size_t) VILL_SCREEN_BYTES);
    vill_set_mode(VILL_MODE_TEXT);
    vill_blocks_after = vill_used_heap_blocks();
}

static int vill_pixel(int row, int col)
{
    if (vill_screen == NULL) {
        return -1;
    }
    return (int) vill_screen[row * VILL_SCREEN_W + col];
}

/* How many of the sampled ticks saw this value at this probe. */
static int vill_seen(int probe, int value)
{
    int index;
    int count;

    count = 0;
    for (index = 0; index < vill_sample_count; index++) {
        if ((int) vill_samples[index][probe] == value) {
            count++;
        }
    }
    return count;
}

/* Which sample first and last saw this value at this probe, or -1 for none. */
static int vill_first_seen(int probe, int value)
{
    int index;

    for (index = 0; index < vill_sample_count; index++) {
        if ((int) vill_samples[index][probe] == value) {
            return index;
        }
    }
    return -1;
}

static int vill_last_seen(int probe, int value)
{
    int index;
    int found;

    found = -1;
    for (index = 0; index < vill_sample_count; index++) {
        if ((int) vill_samples[index][probe] == value) {
            found = index;
        }
    }
    return found;
}

/* How many of the sampled ticks saw something else entirely. */
static int vill_strangers(int probe, int a, int b, int c, int d)
{
    int index;
    int value;
    int count;

    count = 0;
    for (index = 0; index < vill_sample_count; index++) {
        value = (int) vill_samples[index][probe];
        if (value != a && value != b && value != c && value != d) {
            count++;
        }
    }
    return count;
}

/* The marker walks a whole journey in six frames and the sixth lands it exactly
   on the destination: MOV EBX,0x5 / IDIV at 0003208e over i = 0..5, so at i = 5
   the interpolation subtracts the whole delta.  A walk 150 pixels long that
   ended one frame early would leave the marker at 130. */
static void village_walk_ends_on_the_destination(void)
{
    vill_stage();
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);

    CHECK_EQ(vill_pixel(VILL_WALK_TO_Y + VILL_MARKER_MID,
                        VILL_WALK_TO_X + VILL_MARKER_MID),
             VILL_MARKER_COLOR(VILL_FACING_RIGHT));
    CHECK_EQ(vill_pixel(VILL_WALK_TO_Y, VILL_WALK_TO_X),
             VILL_MARKER_COLOR(VILL_FACING_RIGHT));
    CHECK_EQ(vill_pixel(VILL_WALK_TO_Y + VILL_MARKER_H - 1,
                        VILL_WALK_TO_X + VILL_MARKER_W - 1),
             VILL_MARKER_COLOR(VILL_FACING_RIGHT));
    /* One pixel past the marker's bottom right corner is still the page. */
    CHECK_EQ(vill_pixel(VILL_WALK_TO_Y + VILL_MARKER_H,
                        VILL_WALK_TO_X + VILL_MARKER_W),
             VILL_BACKGROUND_FILL);
    vill_unstage();
}

/* The dominant axis decides the facing and the sign decides the direction: CMP
   EBX,EAX / JLE at 00031ff2 over abs(dx) and abs(dy), then CMP [EBP-0x2c],0x0 /
   JLE for 3 against 1 and CMP [EBP-0x28],0x0 / JLE for 0 against 2.  Each case
   reads the marker the sixth frame left on the destination, whose colour is the
   facing the cache entry was taken from. */
static void village_facing_is_the_dominant_axis(void)
{
    vill_stage();
    vill_place(VILL_DEST_FROM, 10, 50);
    vill_place(VILL_DEST_TO, 160, 50);
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);
    CHECK_EQ(vill_pixel(50 + VILL_MARKER_MID, 160 + VILL_MARKER_MID),
             VILL_MARKER_COLOR(VILL_FACING_RIGHT));
    vill_unstage();

    vill_stage();
    vill_place(VILL_DEST_FROM, 160, 50);
    vill_place(VILL_DEST_TO, 10, 50);
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);
    CHECK_EQ(vill_pixel(50 + VILL_MARKER_MID, 10 + VILL_MARKER_MID),
             VILL_MARKER_COLOR(VILL_FACING_LEFT));
    vill_unstage();

    vill_stage();
    vill_place(VILL_DEST_FROM, 100, 20);
    vill_place(VILL_DEST_TO, 100, 170);
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);
    CHECK_EQ(vill_pixel(170 + VILL_MARKER_MID, 100 + VILL_MARKER_MID),
             VILL_MARKER_COLOR(VILL_FACING_DOWN));
    vill_unstage();

    vill_stage();
    vill_place(VILL_DEST_FROM, 100, 170);
    vill_place(VILL_DEST_TO, 100, 20);
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);
    CHECK_EQ(vill_pixel(20 + VILL_MARKER_MID, 100 + VILL_MARKER_MID),
             VILL_MARKER_COLOR(VILL_FACING_UP));
    vill_unstage();
}

/* A diagonal of exactly equal legs goes to the vertical branch, because the
   compare that picks the axis is JLE and not JL: abs(dx) has to be strictly
   greater to take the horizontal side.  Writing that test the other way round
   would face this walk right instead of down. */
static void village_equal_legs_face_vertically(void)
{
    vill_stage();
    vill_place(VILL_DEST_FROM, 10, 10);
    vill_place(VILL_DEST_TO, 160, 160);
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);

    CHECK_EQ(vill_pixel(160 + VILL_MARKER_MID, 160 + VILL_MARKER_MID),
             VILL_MARKER_COLOR(VILL_FACING_DOWN));
    vill_unstage();
}

/* Two destinations at the same point leave both deltas zero, and the second
   test is JLE as well, so a delta of exactly zero reads as the negative
   direction and the marker faces up.  Every frame then draws the marker and its
   afterimage on the same 24 pixels, so what survives is the near afterimage --
   which is also what says the marker underneath it was facing up. */
static void village_zero_delta_faces_up(void)
{
    vill_stage();
    vill_place(VILL_DEST_FROM, 100, 100);
    vill_place(VILL_DEST_TO, 100, 100);
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);

    CHECK_EQ(vill_pixel(100 + VILL_MARKER_MID, 100 + VILL_MARKER_MID),
             VILL_NEAR_GHOST_COLOR(VILL_FACING_UP));
    vill_unstage();
}

/* The last frame carries ONE afterimage.  The near one is at step i - 1 and is
   drawn whenever i > 0, so on the sixth frame it sits at step 4; the far one is
   at step i - 2 but is guarded by 1 < i && i < 5, so on the sixth frame it is
   not drawn at all and step 3 is still the page.  Writing that guard as i > 1
   -- the shape the near one has -- would paint step 3 too. */
static void village_last_frame_trails_one_ghost(void)
{
    vill_stage();
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);

    CHECK_EQ(vill_pixel(VILL_WALK_FROM_Y + VILL_MARKER_MID,
                        VILL_WALK_FROM_X + 4 * VILL_WALK_STEP_X
                            + VILL_MARKER_MID),
             VILL_NEAR_GHOST_COLOR(VILL_FACING_RIGHT));
    CHECK_EQ(vill_pixel(VILL_WALK_FROM_Y + VILL_MARKER_MID,
                        VILL_WALK_FROM_X + 3 * VILL_WALK_STEP_X
                            + VILL_MARKER_MID),
             VILL_BACKGROUND_FILL);
    CHECK_EQ(vill_pixel(VILL_WALK_FROM_Y + VILL_MARKER_MID,
                        VILL_WALK_FROM_X + 2 * VILL_WALK_STEP_X
                            + VILL_MARKER_MID),
             VILL_BACKGROUND_FILL);
    CHECK_EQ(vill_pixel(VILL_WALK_FROM_Y + VILL_MARKER_MID,
                        VILL_WALK_FROM_X + VILL_MARKER_MID),
             VILL_BACKGROUND_FILL);
    vill_unstage();
}

/* Both afterimages, watched frame by frame at the first two steps of the walk.
   Step 0 is the marker on frame 0, the near afterimage on frame 1 and the far
   one on frame 2; step 1 is the marker on frame 1, the near one on frame 2 and
   the far one on frame 3.  The colours separate the two because the fixture
   gives level 8 and level 0xc different bands of the inverse cube, so seeing
   0xf4 at either probe is what says the far afterimage exists AND that it is
   drawn at 0xc rather than at 8. */
static void village_both_ghosts_trail_the_marker(void)
{
    vill_stage();
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);

    CHECK_EQ(vill_seen(VILL_PROBE_STEP1,
                       VILL_MARKER_COLOR(VILL_FACING_RIGHT)) >= 1, 1);
    CHECK_EQ(vill_seen(VILL_PROBE_STEP1,
                       VILL_NEAR_GHOST_COLOR(VILL_FACING_RIGHT)) >= 1, 1);
    CHECK_EQ(vill_seen(VILL_PROBE_STEP1,
                       VILL_FAR_GHOST_COLOR(VILL_FACING_RIGHT)) >= 1, 1);
    CHECK_EQ(vill_seen(VILL_PROBE_STEP0,
                       VILL_NEAR_GHOST_COLOR(VILL_FACING_RIGHT)) >= 1, 1);
    CHECK_EQ(vill_seen(VILL_PROBE_STEP0,
                       VILL_FAR_GHOST_COLOR(VILL_FACING_RIGHT)) >= 1, 1);
    /* Nothing but the marker, the two afterimages and the page is ever at
       either probe -- and in particular never the stray marker, which is what
       a cache slot or a facing read from the wrong place would show. */
    CHECK_EQ(vill_strangers(VILL_PROBE_STEP0,
                            VILL_MARKER_COLOR(VILL_FACING_RIGHT),
                            VILL_NEAR_GHOST_COLOR(VILL_FACING_RIGHT),
                            VILL_FAR_GHOST_COLOR(VILL_FACING_RIGHT),
                            VILL_BACKGROUND_FILL), 0);
    CHECK_EQ(vill_strangers(VILL_PROBE_STEP1,
                            VILL_MARKER_COLOR(VILL_FACING_RIGHT),
                            VILL_NEAR_GHOST_COLOR(VILL_FACING_RIGHT),
                            VILL_FAR_GHOST_COLOR(VILL_FACING_RIGHT),
                            VILL_BACKGROUND_FILL), 0);
    /* The trail moves on and never comes back: the far afterimage is the last
       thing either step sees, and the near one is never seen after it. */
    CHECK_EQ(vill_last_seen(VILL_PROBE_STEP1,
                            VILL_NEAR_GHOST_COLOR(VILL_FACING_RIGHT))
             < vill_first_seen(VILL_PROBE_STEP1,
                               VILL_FAR_GHOST_COLOR(VILL_FACING_RIGHT)), 1);
    vill_unstage();
}

/* The empty plate frame is sprite 6 drawn opaque at the box corner at the
   sheet's own full size on every frame, so wherever the scaled name plate does
   not reach, 0x46 shows underneath it.  On the middle frames the plate is 0x30
   by 0x10 centred in a 0x48 by 0x18 box, which leaves both outer probes on the
   frame. */
static void village_empty_plate_frame_sits_under_every_plate(void)
{
    vill_stage();
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);

    CHECK_EQ(vill_seen(VILL_PROBE_PLATE_FULL,
                       VILL_PLATE_COLOR(VILL_PLATE_FRAME_SPRITE)) >= 1, 1);
    CHECK_EQ(vill_seen(VILL_PROBE_PLATE_MID,
                       VILL_PLATE_COLOR(VILL_PLATE_FRAME_SPRITE)) >= 1, 1);
    /* The box never shows the page through it: the frame covers all of it. */
    CHECK_EQ(vill_seen(VILL_PROBE_PLATE_FULL, VILL_BACKGROUND_FILL), 0);
    CHECK_EQ(vill_seen(VILL_PROBE_PLATE_MID, VILL_BACKGROUND_FILL), 0);
    CHECK_EQ(vill_seen(VILL_PROBE_PLATE_SMALL, VILL_BACKGROUND_FILL), 0);
    CHECK_EQ(vill_strangers(VILL_PROBE_PLATE_FULL,
                            VILL_PLATE_COLOR(VILL_DEST_FROM),
                            VILL_PLATE_COLOR(VILL_DEST_TO),
                            VILL_PLATE_COLOR(VILL_PLATE_FRAME_SPRITE),
                            VILL_PLATE_COLOR(VILL_PLATE_FRAME_SPRITE)), 0);
    CHECK_EQ(vill_strangers(VILL_PROBE_PLATE_MID,
                            VILL_PLATE_COLOR(VILL_DEST_FROM),
                            VILL_PLATE_COLOR(VILL_DEST_TO),
                            VILL_PLATE_COLOR(VILL_PLATE_FRAME_SPRITE),
                            VILL_PLATE_COLOR(VILL_PLATE_FRAME_SPRITE)), 0);
    CHECK_EQ(vill_strangers(VILL_PROBE_PLATE_SMALL,
                            VILL_PLATE_COLOR(VILL_DEST_FROM),
                            VILL_PLATE_COLOR(VILL_DEST_TO),
                            VILL_PLATE_COLOR(VILL_PLATE_FRAME_SPRITE),
                            VILL_PLATE_COLOR(VILL_PLATE_FRAME_SPRITE)), 0);
    vill_unstage();
}

/* The outgoing plate shrinks over the first three frames and the incoming one
   grows over the last three, and the changeover is exact: CMP i,3 / JGE skips
   the outgoing draw and CMP i,2 / JLE skips the incoming one, so every frame
   draws exactly one.  The three probes are the three sizes' left edges, so the
   outgoing sprite's colour has to appear at the middle probe -- which only a
   0x3c-wide plate reaches -- and the incoming sprite's colour at the innermost
   one, and neither may ever appear at a probe its own sizes cannot cover. */
static void village_name_plate_shrinks_out_and_grows_in(void)
{
    vill_stage();
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);

    CHECK_EQ(vill_seen(VILL_PROBE_PLATE_MID,
                       VILL_PLATE_COLOR(VILL_DEST_FROM)) >= 1, 1);
    CHECK_EQ(vill_seen(VILL_PROBE_PLATE_SMALL,
                       VILL_PLATE_COLOR(VILL_DEST_FROM)) >= 1, 1);
    CHECK_EQ(vill_seen(VILL_PROBE_PLATE_SMALL,
                       VILL_PLATE_COLOR(VILL_DEST_TO)) >= 1, 1);
    CHECK_EQ(vill_seen(VILL_PROBE_PLATE_MID,
                       VILL_PLATE_COLOR(VILL_DEST_TO)) >= 1, 1);
    /* The changeover happens once and never comes back: every tick that saw
       the outgoing plate at the innermost probe came before every tick that
       saw the incoming one there. */
    CHECK_EQ(vill_last_seen(VILL_PROBE_PLATE_SMALL,
                            VILL_PLATE_COLOR(VILL_DEST_FROM))
             < vill_first_seen(VILL_PROBE_PLATE_SMALL,
                               VILL_PLATE_COLOR(VILL_DEST_TO)), 1);
    /* And the last frame has the incoming plate back at the box's own full
       0x48 by 0x18, corner to corner. */
    CHECK_EQ(vill_pixel(VILL_PLATE_BOX_Y, VILL_PLATE_BOX_X),
             VILL_PLATE_COLOR(VILL_DEST_TO));
    CHECK_EQ(vill_pixel(VILL_PLATE_BOX_Y + VILL_SHEET_H - 1,
                        VILL_PLATE_BOX_X + VILL_SHEET_W - 1),
             VILL_PLATE_COLOR(VILL_DEST_TO));
    vill_unstage();
}

/* Which sprite each half of the walk draws is the caller's own destination
   number, not a fixed pair: the outgoing draw pushes [EBP+0x18] and the
   incoming one [EBP+0x1c].  Running the same walk with the two swapped has to
   swap which colour the last frame leaves in the box. */
static void village_plates_are_the_two_destinations(void)
{
    vill_stage();
    vill_place(VILL_DEST_TO, VILL_WALK_FROM_X, VILL_WALK_FROM_Y);
    vill_place(VILL_DEST_FROM, VILL_WALK_TO_X, VILL_WALK_TO_Y);
    vill_run(VILL_DEST_TO, VILL_DEST_FROM);

    CHECK_EQ(vill_pixel(VILL_PLATE_BOX_Y, VILL_PLATE_BOX_X),
             VILL_PLATE_COLOR(VILL_DEST_FROM));
    /* And the plate that shrank away over the first three frames was the other
       one, sprite 1, which only the outgoing draw can have put there. */
    CHECK_EQ(vill_seen(VILL_PROBE_PLATE_SMALL,
                       VILL_PLATE_COLOR(VILL_DEST_TO)) >= 1, 1);
    CHECK_EQ(vill_last_seen(VILL_PROBE_PLATE_SMALL,
                            VILL_PLATE_COLOR(VILL_DEST_TO))
             < vill_first_seen(VILL_PROBE_PLATE_SMALL,
                               VILL_PLATE_COLOR(VILL_DEST_FROM)), 1);
    vill_unstage();
}

/* The whole 64,000-byte page reaches the adapter, and the caller's own page is
   only ever read: PUSH 0xfa00 / PUSH page / PUSH 0xa0000 at 00032325 with the
   frame composed in a block of its own that memmove copies the caller's into.
   A rewrite that drew into the caller's page instead would leave the plate and
   the marker in it. */
static void village_presents_the_page_and_keeps_the_background(void)
{
    vill_stage();
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);

    CHECK_EQ(vill_pixel(0, 0), VILL_BACKGROUND_FILL);
    CHECK_EQ(vill_pixel(VILL_SCREEN_H - 1, VILL_SCREEN_W - 1),
             VILL_BACKGROUND_FILL);
    CHECK_EQ((int) vill_background[VILL_PLATE_BOX_Y * VILL_SCREEN_W
                                   + VILL_PLATE_BOX_X],
             VILL_BACKGROUND_FILL);
    CHECK_EQ((int) vill_background[(VILL_WALK_TO_Y + VILL_MARKER_MID)
                                   * VILL_SCREEN_W + VILL_WALK_TO_X
                                   + VILL_MARKER_MID],
             VILL_BACKGROUND_FILL);
    vill_unstage();
}

/* Each frame takes its page from the heap and gives it back before the next one
   starts -- CALL malloc at 00032064 and CALL free at 00032352, both inside the
   loop -- so the heap is where it was when the call returns. */
static void village_frees_every_frames_page(void)
{
    vill_stage();
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);

    CHECK_EQ(vill_blocks_after, vill_blocks_before);
    vill_unstage();
}

/* Every frame ends waiting for the timer tick to move -- MOV EAX,[EBP-0x4] /
   CMP EAX,[0x00069d64] / JZ back at 0003233b, inside the loop and not after it.
   The first frame's latch is uninitialised and may end its wait at once, so
   five ticks are the guaranteed floor for six frames; a run that waited nowhere
   does not reach it, and one that latched the counter before the loop would
   cost six. */
static void village_paces_the_frames_with_the_tick(void)
{
    vill_stage();
    vill_run(VILL_DEST_FROM, VILL_DEST_TO);

    CHECK_EQ(vill_ticks_used >= (unsigned int) (VILL_FRAMES - 1), 1);
    vill_unstage();
}

/* ---- fdps_village_signboard_menu, 00031bc0 -------------------------------
 *
 * Expected values come from the assembly at 00031bc0: CMP EAX,dword ptr
 * [0x00064114] / JL at 00031bfc for the entry check; CMP dword ptr
 * [EBP-0xc],0x7f / JGE at 00031c43 for the threshold; CALL 0x000357a0 at
 * 00031c51 sitting AHEAD of every key compare with the arrows as its else, and
 * MOV dword ptr [EAX],0x5 at 00031c60 for what a completed code does; the
 * compares at 00031c84, 00031c8a, 00031cd5, 00031cdb, 00031d21, 00031d27 and
 * 00031d33 for the keys, with ADD EDX,0x4 / MOV EBX,0x5 / IDIV at 00031ca3 and
 * INC EDX / IDIV at 00031cf4 for the two cursor steps; MOV EBX,0x5 / DIV / AND
 * EAX,0x3 at 00031d53 with CMP,0x3 / MOV 0x1 at 00031d65 for the leg cell;
 * MOV byte ptr [EBP-0x4],0x1 / JMP 0x00031d53 at 00031d2d for the confirming
 * pass falling into its own frame; ADD EAX,0xf0 at 00031e8f with CMP,0x5 at
 * 00031e7c and the three MOV AL,byte ptr [EAX + EBP*0x1 + ...] at 00031ea8,
 * 00031ec5 and 00031ee2 for the DAC band; and the nine bytes each of the three
 * ramps that the image holds at 00031064, 0003106d and 00031076.  None of them
 * is read off the emitted C.
 *
 * THE SHEET IS THE SHIPPED ONE AND CANNOT BE STOOD IN FOR.  The menu holds
 * "MISC.VFS" and "CanBan.cel" as literals and takes no file argument, and a
 * container it cannot open ends the process at exit(1) rather than failing an
 * assertion, so every case here skips itself unless MISC.VFS is next to the
 * executable (tests/gamefile.lst stages it).  Nothing below asserts a pixel of
 * that sheet: what is asserted about the plate box is that something opaque
 * covered it, and every colour that is named is one this fixture put in the
 * marker cache itself.
 *
 * HOW THE KEYS GET IN.  fdps_read_keyboard_queue drains the ring the INT 09h
 * handler fills, and it owns only the read index (keybd.h), so a case can load
 * the ring's bytes and set the write index itself without an interrupt.  The
 * ring holds nine readable entries, the tenth position being what makes the
 * two indices equal again, and a pass that finds it empty reads 0xff -- which
 * the threshold drops -- so a script that never confirms would spin forever.
 * Every script below ends with Enter or Space.
 *
 * WHAT ELSE HAD TO BE PUT IN PLACE.  The arrows call fdps_play_sfx, which
 * looks its cue up in the pack behind data_fdps_audio_basewav_sfx_bank_buf_ptr
 * and does nothing at all when the name is not in it (audio.c), so the pack
 * here is an empty container: the cue misses and no voice is started.  The CD
 * poll at the top of every pass is disarmed by publishing -1 as the current
 * music index, which is the arm its own cheapest test takes (cdaudio.c), so no
 * device request is ever made.  And the unlock matcher sees every accepted
 * key, so the cases that are not about it run under a chapter whose row is
 * eight zero bytes and can never match.
 */

#define MENU_ARCHIVE "MISC.VFS"

/* Where the six destinations are put.  They are spread over two rows well
   clear of the plate box at rows 174..197, so no marker overlaps a plate and
   no two markers overlap each other. */
#define MENU_DEST_COUNT 6
#define MENU_DEST_ROW_Y0 40
#define MENU_DEST_ROW_Y1 80
#define MENU_DEST_COL_X0 100
#define MENU_DEST_COL_X1 140
#define MENU_DEST_COL_X2 180

/* The marker cache this section stages: the roster slot under test gets three
   distinct colours in its first three offsets and every other offset of every
   slot gets the stray, so a marker drawn from the wrong slot, or from a cell
   past the first facing's three, shows up as its own value. */
#define MENU_ROSTER_SLOT 3
#define MENU_ROSTER_COUNT 4
#define MENU_CELLS 3
#define MENU_CELL_COLOR(cell) (0x51 + (cell))
#define MENU_STRAY_COLOR 0x5f

/* What mode 13h leaves the adapter holding until the first frame is copied
   over it.  The ISR below starts sampling as soon as it is installed, and the
   sheet load ahead of the first frame is long enough for a tick, so this is an
   honest value for a probe to see and is accepted alongside the three cells. */
#define MENU_CLEARED 0x00

/* The five entries the menu animates, the two either side of them that it must
   not touch, and the guard triple written into those two.  0x2a and 0x15 are
   nowhere in the three ramps, whose values are all 0x31..0x3c. */
#define MENU_DAC_WRITE_INDEX 0x3c8
#define MENU_DAC_READ_INDEX 0x3c7
#define MENU_DAC_DATA 0x3c9
#define MENU_DAC_COMPONENT_MASK 0x3f
#define MENU_DAC_FIRST 0xf0
#define MENU_DAC_ENTRIES 5
#define MENU_DAC_READ_FIRST (MENU_DAC_FIRST - 1)
#define MENU_DAC_READ_COUNT (MENU_DAC_ENTRIES + 2)
#define MENU_DAC_BAND_AT 1
#define MENU_DAC_GUARD_BELOW 0
#define MENU_DAC_GUARD_ABOVE (MENU_DAC_ENTRIES + 1)
#define MENU_DAC_GUARD_LOW 0x2a
#define MENU_DAC_GUARD_HIGH 0x15
#define MENU_RAMP_BYTES 9
#define MENU_RAMP_PHASES 5

/* The keys, as fdps_keyboard_isr queues them. */
#define MENU_KEY_TAB 0x0f
#define MENU_KEY_ENTER 0x1c
#define MENU_KEY_SPACE 0x39
#define MENU_KEY_UP 0x48
#define MENU_KEY_LEFT 0x4b
#define MENU_KEY_RIGHT 0x4d
#define MENU_KEY_DOWN 0x50

/* Three codes the threshold has to drop: the boundary itself, a break code and
   the empty-ring marker. */
#define MENU_KEY_AT_THRESHOLD 0x7f
#define MENU_KEY_BREAK 0x9d
#define MENU_KEY_NONE 0xff
#define MENU_DROPPED_CODES 3

/* The chapters two cases pick their unlock row out of.  Chapter id 16's row is
   eight zero bytes, so no keystroke can ever match it; chapter id 10's is
   48 4b 50 4d -- up, left, down, right -- and ends on an arrow key, which is
   what separates the matcher's own arm from the arrow chain it precedes. */
#define MENU_CHAPTER_NO_CODE 16
#define MENU_CHAPTER_ARROW_CODE 10

#define MENU_SFX_PACK_BYTES 64
#define MENU_SCRIPT_MAX 9

/* Three pixels of the plate box, given as offsets inside the sheet's own 72 by
   24 sprite and as the values the shipped CANBAN.CEL decodes to there.  The
   sheet is 4-op RLE with a skip op (resource_info/cel.md), so a sprite leaves
   most of the box untouched and the three pixels below are the three ways that
   can come out:

     (0,0)   every one of the seven sprites skips it, so the page shows through
     (0,2)   only sprite 6, the empty plate frame, paints it -- 0xe6
     (5,14)  sprite 6 paints 0xe6, sprite 2 paints 0x80 over it and sprite 5
             paints 0xd0, while sprites 0, 1, 3 and 4 skip it and leave 0xe6

   So the third pixel says which name plate went on top, the second says the
   empty frame went down under it, and the first says a skipped pixel is left
   alone rather than filled.  The box's corner goes at
   (PLATE_BOX_X, PLATE_BOX_Y) = (3, 0xae). */
#define MENU_PLATE_ORIGIN_X 3
#define MENU_PLATE_ORIGIN_Y 0xae
#define MENU_PLATE_CLEAR_ROW 0
#define MENU_PLATE_CLEAR_COL 0
#define MENU_PLATE_FRAME_ROW 0
#define MENU_PLATE_FRAME_COL 2
#define MENU_PLATE_FRAME_PIXEL 0xe6
#define MENU_PLATE_NAME_ROW 5
#define MENU_PLATE_NAME_COL 14
#define MENU_PLATE_NAME_PIXEL_2 0x80
#define MENU_PLATE_NAME_PIXEL_5 0xd0

/* The three component ramps, quoted from the nine bytes each that the image
   holds at 00031064, 0003106d and 00031076.  Each is a five-long cycle with
   its first four values repeated behind it. */
static unsigned char menu_ramp_red[MENU_RAMP_BYTES] = {
    0x3c, 0x37, 0x31, 0x33, 0x37, 0x3c, 0x37, 0x31, 0x33
};
static unsigned char menu_ramp_green[MENU_RAMP_BYTES] = {
    0x3c, 0x39, 0x33, 0x36, 0x39, 0x3c, 0x39, 0x33, 0x36
};
static unsigned char menu_ramp_blue[MENU_RAMP_BYTES] = {
    0x3c, 0x3c, 0x3a, 0x3b, 0x3c, 0x3c, 0x3c, 0x3a, 0x3b
};

static unsigned char menu_sfx_pack[MENU_SFX_PACK_BYTES];
static unsigned char menu_dac[MENU_DAC_READ_COUNT][3];
static int menu_dest_x[MENU_DEST_COUNT] = {
    MENU_DEST_COL_X0, MENU_DEST_COL_X1, MENU_DEST_COL_X2,
    MENU_DEST_COL_X0, MENU_DEST_COL_X1, MENU_DEST_COL_X2
};
static int menu_dest_y[MENU_DEST_COUNT] = {
    MENU_DEST_ROW_Y0, MENU_DEST_ROW_Y0, MENU_DEST_ROW_Y0,
    MENU_DEST_ROW_Y1, MENU_DEST_ROW_Y1, MENU_DEST_ROW_Y1
};

/* MISC.VFS next to the executable, which every case here needs. */
static int menu_archive_present(void)
{
    FILE *fp;

    fp = fopen(MENU_ARCHIVE, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* Three flat 24 by 24 markers and a stray one, with a slot table that points
   every offset of every slot at the stray before the slot under test is given
   the three in its first three. */
static void menu_stage_cache(void)
{
    struct fdps_cel_cache_slot *slots;
    int stream;
    int slot;
    int cell;
    int row;
    int at;
    int color;

    memset(vill_cache, 0, (size_t) VILL_CACHE_BYTES);
    for (stream = 0; stream < VILL_CACHE_STREAMS; stream++) {
        at = VILL_CACHE_TABLE_BYTES + stream * VILL_MARKER_STREAM_BYTES;
        if (stream < MENU_CELLS) {
            color = MENU_CELL_COLOR(stream);
        } else {
            color = MENU_STRAY_COLOR;
        }
        for (row = 0; row < VILL_MARKER_H; row++) {
            vill_cache[at + row * VILL_MARKER_ROW_BYTES] = VILL_MARKER_CMD_24;
            vill_cache[at + row * VILL_MARKER_ROW_BYTES + 1] =
                (unsigned char) color;
        }
    }

    slots = (struct fdps_cel_cache_slot *) vill_cache;
    for (slot = 0; slot < VILL_CACHE_SLOTS; slot++) {
        for (cell = 0; cell < VILL_CELLS_PER_SLOT; cell++) {
            slots[slot].sprite_offset[cell] = VILL_CACHE_TABLE_BYTES
                + MENU_CELLS * VILL_MARKER_STREAM_BYTES;
        }
    }
    for (cell = 0; cell < MENU_CELLS; cell++) {
        slots[MENU_ROSTER_SLOT].sprite_offset[cell] =
            VILL_CACHE_TABLE_BYTES + cell * VILL_MARKER_STREAM_BYTES;
    }
    data_fdps_cel_sprite_cache_ptr = vill_cache;
}

/* Everything the menu reads that is not the sheet: the six destinations, the
   party the marker is taken from, a sound pack that holds nothing, a CD poll
   with no track selected, and a chapter with no unlock code. */
static void menu_stage(void)
{
    int destination;

    menu_stage_cache();
    vill_stage_blend();
    for (destination = 0; destination < MENU_DEST_COUNT; destination++) {
        vill_place(destination, menu_dest_x[destination],
                   menu_dest_y[destination]);
    }
    data_fdps_roster_member_count = MENU_ROSTER_COUNT;
    data_fdps_village_marker_roster_idx = MENU_ROSTER_SLOT;

    memset(menu_sfx_pack, 0, (size_t) MENU_SFX_PACK_BYTES);
    data_fdps_audio_basewav_sfx_bank_buf_ptr = menu_sfx_pack;

    data_fdps_audio_cd_current_music_index = -1;
    data_fdps_audio_bgm_enabled_flag = 0;
    data_fdps_audio_cd_repeat_tick_counter = 0;
    data_fdps_audio_cd_repeat_last_tick = data_fdps_timer_tick_counter;

    data_fdps_chapter_current_chapter_id = MENU_CHAPTER_NO_CODE;
    data_fdps_secret_code_match_pos = 0;
}

static void menu_unstage(void)
{
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    vill_unstage();
}

/* Loads the ring with the script and hands the read index back to its start.
   The write index is what says how much is readable, so nothing has to be
   cleared out of the bytes behind it. */
static void menu_queue(unsigned char *script, int count)
{
    int index;

    for (index = 0; index < SCANCODE_QUEUE_LEN; index++) {
        data_fdps_input_scancode_queue[index] = 0;
    }
    for (index = 0; index < count; index++) {
        data_fdps_input_scancode_queue[index] = script[index];
    }
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = count;
}

static void menu_write_dac(int entry, int red, int green, int blue)
{
    outp(MENU_DAC_WRITE_INDEX, entry);
    outp(MENU_DAC_DATA, red);
    outp(MENU_DAC_DATA, green);
    outp(MENU_DAC_DATA, blue);
}

/* The seven entries around the animated band, read back through the DAC's own
   read port before the mode change that would reset them. */
static void menu_read_dac(void)
{
    int entry;
    int component;

    for (entry = 0; entry < MENU_DAC_READ_COUNT; entry++) {
        outp(MENU_DAC_READ_INDEX, MENU_DAC_READ_FIRST + entry);
        for (component = 0; component < 3; component++) {
            menu_dac[entry][component] =
                (unsigned char) (inp(MENU_DAC_DATA) & MENU_DAC_COMPONENT_MASK);
        }
    }
}

/* One whole menu, leaving the last frame in vill_screen[], the band in
   menu_dac[] and the per-tick probes in vill_samples[]. */
static void menu_run(unsigned char *script, int count, int *selection,
                     int probe_destination)
{
    vill_screen = (unsigned char *) malloc((size_t) VILL_SCREEN_BYTES);
    vill_background = (unsigned char *) malloc((size_t) VILL_SCREEN_BYTES);
    CHECK_EQ(vill_screen != NULL && vill_background != NULL, 1);
    if (vill_screen == NULL || vill_background == NULL) {
        return;
    }
    memset(vill_background, VILL_BACKGROUND_FILL, (size_t) VILL_SCREEN_BYTES);

    vill_sample_count = 0;
    vill_probe_at[0] =
        (menu_dest_y[probe_destination] + VILL_MARKER_MID) * VILL_SCREEN_W
        + menu_dest_x[probe_destination] + VILL_MARKER_MID;
    vill_probe_at[1] =
        (MENU_PLATE_ORIGIN_Y + MENU_PLATE_FRAME_ROW) * VILL_SCREEN_W
        + MENU_PLATE_ORIGIN_X + MENU_PLATE_FRAME_COL;
    vill_probe_at[2] = 0;
    vill_probe_at[3] = 0;
    vill_probe_at[4] = 0;

    menu_queue(script, count);
    vill_set_mode(VILL_MODE_320X200X256);
    menu_write_dac(MENU_DAC_READ_FIRST, MENU_DAC_GUARD_LOW,
                   MENU_DAC_GUARD_LOW, MENU_DAC_GUARD_LOW);
    menu_write_dac(MENU_DAC_FIRST + MENU_DAC_ENTRIES, MENU_DAC_GUARD_HIGH,
                   MENU_DAC_GUARD_HIGH, MENU_DAC_GUARD_HIGH);
    vill_blocks_before = vill_used_heap_blocks();

    vill_saved_timer = _dos_getvect(VILL_TIMER_VECTOR);
    _dos_setvect(VILL_TIMER_VECTOR, vill_timer_isr);
    fdps_village_signboard_menu(vill_background, selection);
    _dos_setvect(VILL_TIMER_VECTOR, vill_saved_timer);

    menu_read_dac();
    memmove(vill_screen, (void *) VILL_VGA_BASE, (size_t) VILL_SCREEN_BYTES);
    vill_set_mode(VILL_MODE_TEXT);
    vill_blocks_after = vill_used_heap_blocks();
}

/* The pixel the marker's centre left on the last frame at this destination. */
static int menu_marker_pixel(int destination)
{
    return vill_pixel(menu_dest_y[destination] + VILL_MARKER_MID,
                      menu_dest_x[destination] + VILL_MARKER_MID);
}

/* A pixel of the plate box, addressed inside the sheet's own sprite. */
static int menu_plate_pixel(int row, int col)
{
    return vill_pixel(MENU_PLATE_ORIGIN_Y + row, MENU_PLATE_ORIGIN_X + col);
}

/* Whether a pixel is one of the three cells the roster slot under test can be
   drawn from -- which is what says it is neither the stray nor the page. */
static int menu_is_a_cell(int pixel)
{
    return pixel >= MENU_CELL_COLOR(0) && pixel <= MENU_CELL_COLOR(2);
}

/* Which phase of the ramp the band was left on, or -1 if it is on none of the
   five.  The five windows are distinct sequences, so at most one can hold. */
static int menu_band_phase(void)
{
    int phase;
    int entry;
    int matched;
    int found;

    found = -1;
    for (phase = 0; phase < MENU_RAMP_PHASES; phase++) {
        matched = 1;
        for (entry = 0; entry < MENU_DAC_ENTRIES; entry++) {
            if ((int) menu_dac[MENU_DAC_BAND_AT + entry][0]
                    != (int) menu_ramp_red[phase + entry]
                || (int) menu_dac[MENU_DAC_BAND_AT + entry][1]
                    != (int) menu_ramp_green[phase + entry]
                || (int) menu_dac[MENU_DAC_BAND_AT + entry][2]
                    != (int) menu_ramp_blue[phase + entry]) {
                matched = 0;
            }
        }
        if (matched != 0) {
            found = phase;
        }
    }
    return found;
}

/* Left and Up are the same arm and step the cursor BACK: ADD EDX,0x4 before a
   signed IDIV by 5 at 00031ca3, so entry 0 wraps to 4 and not to -1. */
static void menu_left_and_up_step_back(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    script[0] = MENU_KEY_LEFT;
    script[1] = MENU_KEY_ENTER;
    selection = 0;
    menu_run(script, 2, &selection, 4);
    CHECK_EQ(selection, 4);
    menu_unstage();

    menu_stage();
    script[0] = MENU_KEY_UP;
    script[1] = MENU_KEY_ENTER;
    selection = 3;
    menu_run(script, 2, &selection, 2);
    CHECK_EQ(selection, 2);
    menu_unstage();
}

/* Right and Down are the other arm and step forward, INC EDX before the same
   signed IDIV at 00031cf4, so entry 4 wraps to 0. */
static void menu_right_and_down_step_forward(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    script[0] = MENU_KEY_RIGHT;
    script[1] = MENU_KEY_ENTER;
    selection = 4;
    menu_run(script, 2, &selection, 0);
    CHECK_EQ(selection, 0);
    menu_unstage();

    menu_stage();
    script[0] = MENU_KEY_DOWN;
    script[1] = MENU_KEY_DOWN;
    script[2] = MENU_KEY_ENTER;
    selection = 0;
    menu_run(script, 3, &selection, 2);
    CHECK_EQ(selection, 2);
    menu_unstage();
}

/* Space confirms as well as Enter -- CMP,0x1c / JZ then CMP,0x39 / JNZ at
   00031d21 -- and the answer is whatever the cursor had reached. */
static void menu_space_confirms_like_enter(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    script[0] = MENU_KEY_RIGHT;
    script[1] = MENU_KEY_SPACE;
    selection = 1;
    menu_run(script, 2, &selection, 2);
    CHECK_EQ(selection, 2);
    menu_unstage();
}

/* Everything at or above 0x7f is dropped before the matcher and before the key
   chain: CMP dword ptr [EBP-0xc],0x7f / JGE at 00031c43, over a value the
   reader widened UNSIGNED with AND EAX,0xff at 00031c33.

   The first run says a dropped code moves nothing.  The three after it say it
   never reached the matcher either, which is the only way to see the guard at
   all -- none of these three codes is a menu key, so a build with no guard
   would still leave the cursor alone.  Chapter id 14's row is 02 06, so after
   0x02 the match position stands at 1, and the run finishes the code -- and
   selects the hidden sixth destination -- only because the code wedged between
   the two digits was thrown away.  Had it got through,
   fdps_check_secret_code_key would have missed on it, cleared the position,
   and 0x06 would then have missed on the row's first byte and left the cursor
   at 1.  So the 0x7f run fails under a JG, the other two fail under no guard
   at all, and all three fail under a signed widening, which would deliver 0x9d
   as -99 and 0xff as -1. */
static void menu_drops_codes_at_and_above_the_threshold(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    unsigned char dropped[MENU_DROPPED_CODES];
    int selection;
    int code;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    script[0] = MENU_KEY_AT_THRESHOLD;
    script[1] = MENU_KEY_BREAK;
    script[2] = MENU_KEY_NONE;
    script[3] = MENU_KEY_ENTER;
    selection = 2;
    menu_run(script, 4, &selection, 2);
    CHECK_EQ(selection, 2);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
    menu_unstage();

    dropped[0] = MENU_KEY_AT_THRESHOLD;
    dropped[1] = MENU_KEY_BREAK;
    dropped[2] = MENU_KEY_NONE;
    for (code = 0; code < MENU_DROPPED_CODES; code++) {
        menu_stage();
        /* Chapter id 14's unlock row is 02 06, the digits 1 and 5, and
           neither digit is a menu key. */
        data_fdps_chapter_current_chapter_id = 14;
        data_fdps_secret_code_match_pos = 0;
        script[0] = 0x02;
        script[1] = dropped[code];
        script[2] = 0x06;
        script[3] = MENU_KEY_ENTER;
        selection = 1;
        menu_run(script, 4, &selection, 5);
        CHECK_EQ(selection, 5);
        menu_unstage();
    }
}

/* Tab walks the marker's party member on by a signed modulus of the party
   size, MOV EDX,[0x000601a4] / INC / IDIV at 00031d39, and touches nothing
   else. */
static void menu_tab_cycles_the_marker_member(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    script[0] = MENU_KEY_TAB;
    script[1] = MENU_KEY_ENTER;
    selection = 1;
    data_fdps_village_marker_roster_idx = MENU_ROSTER_SLOT;
    menu_run(script, 2, &selection, 1);
    CHECK_EQ(data_fdps_village_marker_roster_idx, 0);
    CHECK_EQ(selection, 1);
    menu_unstage();

    menu_stage();
    script[0] = MENU_KEY_TAB;
    script[1] = MENU_KEY_TAB;
    script[2] = MENU_KEY_ENTER;
    selection = 1;
    data_fdps_village_marker_roster_idx = 1;
    menu_run(script, 3, &selection, 1);
    CHECK_EQ(data_fdps_village_marker_roster_idx, 3);
    menu_unstage();
}

/* The party index is range-checked once, on the way in, and the compare is
   JL on the signed pair: an index equal to the party size is already out of
   range and is reset, one below it is left alone.  A JLE there would keep the
   equal case and index one slot past the party. */
static void menu_resets_an_out_of_range_member_on_entry(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    script[0] = MENU_KEY_ENTER;
    selection = 0;
    data_fdps_village_marker_roster_idx = MENU_ROSTER_COUNT;
    menu_run(script, 1, &selection, 0);
    CHECK_EQ(data_fdps_village_marker_roster_idx, 0);
    menu_unstage();

    menu_stage();
    script[0] = MENU_KEY_ENTER;
    selection = 0;
    data_fdps_village_marker_roster_idx = MENU_ROSTER_COUNT - 1;
    menu_run(script, 1, &selection, 0);
    CHECK_EQ(data_fdps_village_marker_roster_idx, MENU_ROSTER_COUNT - 1);
    menu_unstage();
}

/* A completed unlock code selects the hidden sixth destination, MOV dword ptr
   [EAX],0x5 at 00031c60.  Chapter id 14's row is 02 06 -- the digits 1 and 5 --
   so the second keystroke finishes it, and neither digit is a menu key. */
static void menu_completed_code_selects_the_secret_shop(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    data_fdps_chapter_current_chapter_id = 14;
    data_fdps_secret_code_match_pos = 0;
    script[0] = 0x02;
    script[1] = 0x06;
    script[2] = MENU_KEY_ENTER;
    selection = 1;
    menu_run(script, 3, &selection, 5);
    CHECK_EQ(selection, 5);
    CHECK_EQ(menu_is_a_cell(menu_marker_pixel(5)), 1);
    /* And the plate the last frame drew is the sixth sprite, not the first
       five: 0xd0 is what sprite 5 paints where sprite 2 paints 0x80. */
    CHECK_EQ(menu_plate_pixel(MENU_PLATE_NAME_ROW, MENU_PLATE_NAME_COL),
             MENU_PLATE_NAME_PIXEL_5);
    menu_unstage();
}

/* The matcher runs AHEAD of the key chain and the arrows are its else, so the
   keystroke that finishes a code does not also move the cursor.  Chapter id
   10's row is 48 4b 50 4d -- up, left, down, right -- and its last key is an
   arrow: from entry 0 the first three walk the cursor to 4, 3 and 4, and the
   fourth completes the code and jumps to 5.  A build that tested the arrows
   first, or that let both arms run, would leave 0 here. */
static void menu_a_code_key_does_not_also_move_the_cursor(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    data_fdps_chapter_current_chapter_id = MENU_CHAPTER_ARROW_CODE;
    data_fdps_secret_code_match_pos = 0;
    script[0] = MENU_KEY_UP;
    script[1] = MENU_KEY_LEFT;
    script[2] = MENU_KEY_DOWN;
    script[3] = MENU_KEY_RIGHT;
    script[4] = MENU_KEY_ENTER;
    selection = 0;
    menu_run(script, 5, &selection, 5);
    CHECK_EQ(selection, 5);
    /* Enter broke the run that the fourth key had completed, and the row's
       first byte is not Enter either, so the position was left cleared. */
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
    menu_unstage();
}

/* The confirming pass still draws its whole frame: the flag is set at 00031d2d
   and the loop falls into the body from there, so what is on the adapter when
   the menu returns is a page composed this pass -- the caller's background,
   the marker at the chosen destination and both plates -- and not the frame
   before it.  A loop that ended where the key was read would leave the marker
   at the previous destination. */
static void menu_draws_a_frame_on_the_confirming_pass(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;
    int center;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    script[0] = MENU_KEY_RIGHT;
    script[1] = MENU_KEY_ENTER;
    selection = 1;
    menu_run(script, 2, &selection, 2);

    center = menu_marker_pixel(2);
    CHECK_EQ(menu_is_a_cell(center), 1);
    /* The marker is a flat 24 by 24, so its two far corners hold the same cell
       the centre does, and one pixel past the bottom right is still the page. */
    CHECK_EQ(vill_pixel(menu_dest_y[2], menu_dest_x[2]), center);
    CHECK_EQ(vill_pixel(menu_dest_y[2] + VILL_MARKER_H - 1,
                        menu_dest_x[2] + VILL_MARKER_W - 1), center);
    CHECK_EQ(vill_pixel(menu_dest_y[2] + VILL_MARKER_H,
                        menu_dest_x[2] + VILL_MARKER_W),
             VILL_BACKGROUND_FILL);
    /* The destination it came from is back to the page: every frame starts
       from a fresh copy of the caller's own. */
    CHECK_EQ(menu_marker_pixel(1), VILL_BACKGROUND_FILL);
    /* The empty frame went down, destination 2's own name plate went down on
       top of it, and a pixel both sprites skip is still the caller's page. */
    CHECK_EQ(menu_plate_pixel(MENU_PLATE_FRAME_ROW, MENU_PLATE_FRAME_COL),
             MENU_PLATE_FRAME_PIXEL);
    CHECK_EQ(menu_plate_pixel(MENU_PLATE_NAME_ROW, MENU_PLATE_NAME_COL),
             MENU_PLATE_NAME_PIXEL_2);
    CHECK_EQ(menu_plate_pixel(MENU_PLATE_CLEAR_ROW, MENU_PLATE_CLEAR_COL),
             VILL_BACKGROUND_FILL);
    CHECK_EQ(vill_pixel(0, 0), VILL_BACKGROUND_FILL);
    CHECK_EQ(vill_pixel(VILL_SCREEN_H - 1, VILL_SCREEN_W - 1),
             VILL_BACKGROUND_FILL);
    /* And the caller's own page was only ever read. */
    CHECK_EQ((int) vill_background[(menu_dest_y[2] + VILL_MARKER_MID)
                                   * VILL_SCREEN_W + menu_dest_x[2]
                                   + VILL_MARKER_MID],
             VILL_BACKGROUND_FILL);
    CHECK_EQ((int) vill_background[MENU_PLATE_ORIGIN_Y * VILL_SCREEN_W
                                   + MENU_PLATE_ORIGIN_X + 2],
             VILL_BACKGROUND_FILL);
    menu_unstage();
}

/* The marker is taken from the party member's own slot and from the first
   three of that slot's twelve offsets -- the cell number is the whole of the
   index at 00031d75, with no facing term ahead of it.  Every frame of a run
   that presses nothing is watched, so a slot or a cell read from anywhere else
   would show as the stray on one of them.  The cleared adapter is allowed
   because the sheet load ahead of the first frame outlasts a tick. */
static void menu_marker_comes_from_the_members_first_cells(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    script[0] = MENU_KEY_AT_THRESHOLD;
    script[1] = MENU_KEY_AT_THRESHOLD;
    script[2] = MENU_KEY_AT_THRESHOLD;
    script[3] = MENU_KEY_ENTER;
    selection = 4;
    menu_run(script, 4, &selection, 4);

    CHECK_EQ(vill_sample_count >= 1, 1);
    CHECK_EQ(vill_strangers(0, MENU_CELL_COLOR(0), MENU_CELL_COLOR(1),
                            MENU_CELL_COLOR(2), MENU_CLEARED), 0);
    CHECK_EQ(vill_seen(0, MENU_STRAY_COLOR), 0);
    CHECK_EQ(vill_seen(0, VILL_BACKGROUND_FILL), 0);
    CHECK_EQ(menu_is_a_cell(menu_marker_pixel(4)), 1);
    /* And the empty plate frame is redrawn on every one of those frames, not
       only on the last: PUSH 0x6 at 00031e19 is inside the loop. */
    CHECK_EQ(vill_strangers(1, MENU_PLATE_FRAME_PIXEL, MENU_CLEARED,
                            MENU_PLATE_FRAME_PIXEL, MENU_PLATE_FRAME_PIXEL),
             0);
    menu_unstage();
}

/* Five DAC entries from 0xf0 up are stepped through the sliding window, three
   components each, and nothing either side of them is touched.  The window is
   read at a phase this test cannot predict -- the frame counter the phase
   comes off is never seeded -- so what is asserted is that the band holds one
   of the five windows exactly, which pins all fifteen ramp bytes and the
   window's stride at once. */
static void menu_steps_the_five_dac_entries(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    script[0] = MENU_KEY_ENTER;
    selection = 0;
    menu_run(script, 1, &selection, 0);

    CHECK_EQ(menu_band_phase() >= 0, 1);
    CHECK_EQ((int) menu_dac[MENU_DAC_GUARD_BELOW][0], MENU_DAC_GUARD_LOW);
    CHECK_EQ((int) menu_dac[MENU_DAC_GUARD_BELOW][1], MENU_DAC_GUARD_LOW);
    CHECK_EQ((int) menu_dac[MENU_DAC_GUARD_BELOW][2], MENU_DAC_GUARD_LOW);
    CHECK_EQ((int) menu_dac[MENU_DAC_GUARD_ABOVE][0], MENU_DAC_GUARD_HIGH);
    CHECK_EQ((int) menu_dac[MENU_DAC_GUARD_ABOVE][1], MENU_DAC_GUARD_HIGH);
    CHECK_EQ((int) menu_dac[MENU_DAC_GUARD_ABOVE][2], MENU_DAC_GUARD_HIGH);
    menu_unstage();
}

/* The sheet is loaded once and freed once and every frame's page is given back
   before the next one starts -- CALL malloc at 00031d9e and CALL free at
   00031f39 inside the loop, CALL free at 00031f4a after it -- so the heap is
   where it was when the menu returns. */
static void menu_frees_the_sheet_and_every_page(void)
{
    unsigned char script[MENU_SCRIPT_MAX];
    int selection;

    if (menu_archive_present() == 0) {
        return;
    }

    menu_stage();
    script[0] = MENU_KEY_RIGHT;
    script[1] = MENU_KEY_AT_THRESHOLD;
    script[2] = MENU_KEY_LEFT;
    script[3] = MENU_KEY_ENTER;
    selection = 0;
    menu_run(script, 4, &selection, 0);

    CHECK_EQ(selection, 0);
    CHECK_EQ(vill_blocks_after, vill_blocks_before);
    menu_unstage();
}

void run_village_tests(void)
{
    RUN_TEST(village_secret_code_completes);
    RUN_TEST(village_row_is_chapter_id_minus_one);
    RUN_TEST(village_next_chapter_has_the_next_row);
    RUN_TEST(village_resumes_at_stored_position);
    RUN_TEST(village_mismatch_resets_position);
    RUN_TEST(village_mismatch_restarts_on_first_byte);
    RUN_TEST(village_chapter_without_a_code);
    RUN_TEST(village_longest_code_is_seven_keys);
    RUN_TEST(village_last_chapter_code);
    RUN_TEST(village_two_key_code);
    RUN_TEST(village_repeated_key_code);

    RUN_TEST(village_walk_ends_on_the_destination);
    RUN_TEST(village_facing_is_the_dominant_axis);
    RUN_TEST(village_equal_legs_face_vertically);
    RUN_TEST(village_zero_delta_faces_up);
    RUN_TEST(village_last_frame_trails_one_ghost);
    RUN_TEST(village_both_ghosts_trail_the_marker);
    RUN_TEST(village_empty_plate_frame_sits_under_every_plate);
    RUN_TEST(village_name_plate_shrinks_out_and_grows_in);
    RUN_TEST(village_plates_are_the_two_destinations);
    RUN_TEST(village_presents_the_page_and_keeps_the_background);
    RUN_TEST(village_frees_every_frames_page);
    RUN_TEST(village_paces_the_frames_with_the_tick);

    RUN_TEST(menu_left_and_up_step_back);
    RUN_TEST(menu_right_and_down_step_forward);
    RUN_TEST(menu_space_confirms_like_enter);
    RUN_TEST(menu_drops_codes_at_and_above_the_threshold);
    RUN_TEST(menu_tab_cycles_the_marker_member);
    RUN_TEST(menu_resets_an_out_of_range_member_on_entry);
    RUN_TEST(menu_completed_code_selects_the_secret_shop);
    RUN_TEST(menu_a_code_key_does_not_also_move_the_cursor);
    RUN_TEST(menu_draws_a_frame_on_the_confirming_pass);
    RUN_TEST(menu_marker_comes_from_the_members_first_cells);
    RUN_TEST(menu_steps_the_five_dac_entries);
    RUN_TEST(menu_frees_the_sheet_and_every_page);
}
