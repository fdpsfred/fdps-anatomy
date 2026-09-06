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
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
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
}
