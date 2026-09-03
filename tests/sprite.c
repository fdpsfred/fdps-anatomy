/* tests/sprite.c -- cover for src/sprite.c.
 *
 * Expected values come from the assembly at 00013ed0 -- XOR EAX,EAX / MOV
 * AX,word ptr [EDX+0x7] and the same pair at +0x9 for the cell size, XOR
 * EDX,EDX / MOV DX,word ptr [EAX+0x20] / CMP EDX,dword ptr [EAX+0x18] / JLE
 * then CMP dword ptr [EAX+0x18],0x0 / JGE for the index test, the four strict
 * compares at 00013f34 through 00013f5e for the placement test, ADD EDX,dword
 * ptr [EAX+0x22] / LEA EAX,[EAX*0x4] / ADD EDX,EAX / ADD EAX,dword ptr [EDX]
 * for the stream address, IMUL EDX,dword ptr [EAX+0x4] / ADD EAX,dword ptr
 * [EBP+-0x18] for the destination pixel, and the seven pushes at 00013f99
 * through 00013fbb -- and from the .SAF layout in resource_info/saf.md (cell
 * width and height as u16 at +0x07 and +0x09; four 10-byte section descriptors
 * from +0x0c, so the tile section's u16 item count is at +0x20 and its u32
 * start at +0x22; a section opens with `count` u32 offsets, every one of them
 * measured from the start of the file).  None of them is read off the emitted
 * C.
 *
 * The image is staged as a byte buffer rather than read from a game file
 * because a .SAF is not a loose file: all 525 of them live inside .VFS
 * containers and reach this code only as a block already unpacked into memory,
 * which is exactly the shape of the buffer below.  Its header fields are
 * written a byte at a time so the test makes no alignment assumption of its
 * own about fields the code reads unaligned.
 *
 * The cell is 4x2 rather than the shipped 24x24 so that a whole tile fits in a
 * surface small enough to check byte by byte, and both tile streams are two
 * fill runs (op 00, resource_info/cel.md) that cover exactly four columns per
 * row -- the encoder's hard condition, since a run that overshoots the row
 * wraps the decoder's width counter.
 *
 * WHAT MAKES A DROPPED CELL DISTINGUISHABLE FROM A DRAWN ONE.  The surface is
 * filled with a guard byte before every case and painted_bytes() counts how
 * many bytes stopped holding it, so a cell drawn one column or one row off is
 * caught by the guards it destroyed and not only by the pixels it failed to
 * write, and a case that expects nothing at all proves nothing was written
 * anywhere on the surface rather than just at the place it was aimed.
 */
#include <string.h>
#include "testharn.h"
#include "sprite.h"
#include "gamedata.h"

/* Header offsets and the geometry the fixture states.  Repeated here rather
   than shared with src/sprite.c: these are what the format and the assembly
   encode, and a test that took them from the emitted source would only prove
   the code agrees with itself. */
#define SAF_CELL_WIDTH_AT 0x07
#define SAF_CELL_HEIGHT_AT 0x09
#define SAF_TILE_COUNT_AT 0x20
#define SAF_TILE_SECTION_START_AT 0x22

#define CELL_WIDTH 4
#define CELL_HEIGHT 2

/* The header is 52 bytes, so the tile section's offset table starts at 0x34;
   its two entries take the eight bytes after it and the two streams follow. */
#define TILE_TABLE_AT 0x34
#define TILE0_STREAM_AT 0x3c
#define TILE1_STREAM_AT 0x40
#define TILE0_PIXEL 0xaa
#define TILE1_PIXEL 0xbb

#define IMAGE_SIZE 0x60

/* pitch - CELL_WIDTH is 4 and DEST_ROWS - CELL_HEIGHT is 4, so x and y are
   accepted at 1, 2 and 3 and refused at 0 and at 4: both bounds of the strict
   placement test sit inside the surface and can be walked. */
#define DEST_PITCH 8
#define DEST_ROWS 6
#define DEST_BYTES (DEST_PITCH * DEST_ROWS)
#define GUARD 0x5a

/* Neither pixel value nor the guard collides, so a painted byte is always
   telling the truth about which tile wrote it. */
static unsigned char stage_image[IMAGE_SIZE];
static unsigned char dest_surface[DEST_BYTES];
static int draw_request[DRAW_REQUEST_DWORDS];

static void stage_u16(int at, unsigned long value)
{
    stage_image[at] = (unsigned char) (value & 0xff);
    stage_image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void stage_u32(int at, unsigned long value)
{
    stage_image[at] = (unsigned char) (value & 0xff);
    stage_image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    stage_image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    stage_image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* One tile: two fill runs of four pixels, one per row.  Command 0x03 is op 00
   with a length field of 3, and a run is length + 1 pixels of the byte that
   follows it (resource_info/cel.md). */
static void stage_tile(int at, unsigned char pixel)
{
    stage_image[at] = 0x03;
    stage_image[at + 1] = pixel;
    stage_image[at + 2] = 0x03;
    stage_image[at + 3] = pixel;
}

static void stage_saf(unsigned long tile_count)
{
    memset(stage_image, 0, IMAGE_SIZE);
    stage_u16(SAF_CELL_WIDTH_AT, CELL_WIDTH);
    stage_u16(SAF_CELL_HEIGHT_AT, CELL_HEIGHT);
    stage_u16(SAF_TILE_COUNT_AT, tile_count);
    stage_u32(SAF_TILE_SECTION_START_AT, TILE_TABLE_AT);
    stage_u32(TILE_TABLE_AT, TILE0_STREAM_AT);
    stage_u32(TILE_TABLE_AT + 4, TILE1_STREAM_AT);
    stage_tile(TILE0_STREAM_AT, TILE0_PIXEL);
    stage_tile(TILE1_STREAM_AT, TILE1_PIXEL);
}

static void stage_request(int x, int y, int tile_index, int blit_mode,
                          int blit_operand)
{
    memset(dest_surface, GUARD, DEST_BYTES);
    draw_request[DRAW_REQUEST_DEST_BASE] = (int) dest_surface;
    draw_request[DRAW_REQUEST_DEST_PITCH] = DEST_PITCH;
    draw_request[DRAW_REQUEST_DEST_ROWS] = DEST_ROWS;
    draw_request[DRAW_REQUEST_X] = x;
    draw_request[DRAW_REQUEST_Y] = y;
    draw_request[DRAW_REQUEST_IMAGE] = (int) stage_image;
    draw_request[DRAW_REQUEST_ITEM_INDEX] = tile_index;
    draw_request[DRAW_REQUEST_BLIT_OPERAND] = blit_operand;
    draw_request[DRAW_REQUEST_BLIT_MODE] = blit_mode;
}

/* How many bytes of the surface stopped holding the guard.  A correctly placed
   4x2 cell paints exactly eight. */
static int painted_bytes(void)
{
    int index;
    int painted;

    painted = 0;
    for (index = 0; index < DEST_BYTES; index++) {
        if (dest_surface[index] != GUARD) {
            painted++;
        }
    }
    return painted;
}

/* The destination pixel is element 0 plus x plus y times the pitch, and the
   rectangle handed to the dispatcher is the image's cell size, so a cell at
   x 1 y 1 covers columns 1 to 4 of rows 1 and 2 and nothing else.  The second
   row landing at column 1 rather than column 5 is what pins the pitch the call
   was made with: the dispatcher derives its row advance as pitch - width. */
static void cell_lands_at_the_requests_x_and_y(void)
{
    stage_saf(2);
    stage_request(1, 1, 0, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(dest_surface[DEST_PITCH + 0], GUARD);
    CHECK_EQ(dest_surface[DEST_PITCH + 1], TILE0_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH + 4], TILE0_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH + 5], GUARD);
    CHECK_EQ(dest_surface[DEST_PITCH * 2 + 0], GUARD);
    CHECK_EQ(dest_surface[DEST_PITCH * 2 + 1], TILE0_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH * 2 + 4], TILE0_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH * 2 + 5], GUARD);
    CHECK_EQ(dest_surface[1], GUARD);
    CHECK_EQ(dest_surface[DEST_PITCH * 3 + 1], GUARD);
    CHECK_EQ(painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* The cell size reaches fdps_blit_dispatch as the source rectangle and the
   request's pitch as the destination pitch, which the dispatcher publishes
   into the three globals its kernels read (blit.h).  Seeded with a value the
   call cannot produce, so what is checked is what this call wrote and not what
   an earlier case left behind; the row count comes back at zero because the
   pass-through kernel counts it down. */
static void cell_size_and_pitch_reach_the_dispatcher(void)
{
    stage_saf(2);
    stage_request(1, 1, 0, 0, 0);
    data_fdps_graphics_rle_blit_dst_pitch = 0xffff;
    data_fdps_graphics_rle_blit_src_width = 0xffff;
    data_fdps_graphics_rle_blit_remaining_rows = 0xffff;
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(data_fdps_graphics_rle_blit_dst_pitch, DEST_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_src_width, CELL_WIDTH);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The index scales the tile section's offset table by four, and the entry it
   reads is rebased onto the image and not onto the table: tile 1's stream sits
   at file offset 0x40, so an implementation that added the entry to the table
   address instead would decode from 0x74 -- past every byte the fixture wrote
   -- and paint zeroes rather than this pixel. */
static void tile_index_selects_the_table_entry(void)
{
    stage_saf(2);
    stage_request(1, 1, 1, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(dest_surface[DEST_PITCH + 1], TILE1_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH + 4], TILE1_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH * 2 + 4], TILE1_PIXEL);
    CHECK_EQ(painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* CMP dword ptr [EAX+0x18],0x0 / JGE: a negative index is refused outright
   rather than indexing backwards out of the table. */
static void negative_tile_index_draws_nothing(void)
{
    stage_saf(2);
    stage_request(1, 1, -1, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(painted_bytes(), 0);
}

/* The upper bound is the count itself, not the count minus one: index 1 of a
   two-tile image draws and index 2 does not.  This is also what pins the count
   to the u16 at +0x20 -- read as the u32 that starts there it would be
   0x00340002, since the section start at +0x22 follows it immediately, and
   index 2 would be accepted and would draw. */
static void tile_index_at_the_count_draws_nothing(void)
{
    stage_saf(2);
    stage_request(1, 1, 2, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(painted_bytes(), 0);

    stage_request(1, 1, 1, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* CMP dword ptr [EBP+-0x18],0x0 / JLE: x is refused at zero, so column 0 of a
   destination is never drawn into however much room the cell has. */
static void x_of_zero_draws_nothing(void)
{
    stage_saf(2);
    stage_request(0, 1, 0, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(painted_bytes(), 0);
}

/* The same test one axis over: CMP dword ptr [EBP+-0x14],0x0 / JLE for y. */
static void y_of_zero_draws_nothing(void)
{
    stage_saf(2);
    stage_request(1, 0, 0, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(painted_bytes(), 0);
}

/* SUB EAX,cell width / CMP EAX,x / JG.  pitch - CELL_WIDTH is 4 here, so x 4
   is refused although the cell would fit exactly within the pitch, and x 3 --
   one inside the bound -- draws with its right edge at column 6.  This is the
   strictness the rebuild note is about: a cell that fails the test is dropped
   whole and not clipped. */
static void x_at_the_pitch_bound_draws_nothing(void)
{
    stage_saf(2);
    stage_request(DEST_PITCH - CELL_WIDTH, 1, 0, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(painted_bytes(), 0);
}

static void x_one_inside_the_pitch_bound_draws(void)
{
    stage_saf(2);
    stage_request(DEST_PITCH - CELL_WIDTH - 1, 1, 0, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(dest_surface[DEST_PITCH + 2], GUARD);
    CHECK_EQ(dest_surface[DEST_PITCH + 3], TILE0_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH + 6], TILE0_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH + 7], GUARD);
    CHECK_EQ(dest_surface[DEST_PITCH * 2 + 3], TILE0_PIXEL);
    CHECK_EQ(painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* SUB EAX,cell height / CMP EAX,y / JG, the same shape down the page:
   DEST_ROWS - CELL_HEIGHT is 4, so y 4 is refused and y 3 draws rows 3 and 4.
   The refusal is not the surface running out -- rows 4 and 5 exist and hold
   their guards afterwards. */
static void y_at_the_row_bound_draws_nothing(void)
{
    stage_saf(2);
    stage_request(1, DEST_ROWS - CELL_HEIGHT, 0, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(painted_bytes(), 0);
    CHECK_EQ(dest_surface[DEST_PITCH * 4 + 1], GUARD);
    CHECK_EQ(dest_surface[DEST_PITCH * 5 + 1], GUARD);
}

static void y_one_inside_the_row_bound_draws(void)
{
    stage_saf(2);
    stage_request(1, DEST_ROWS - CELL_HEIGHT - 1, 0, 0, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(dest_surface[DEST_PITCH * 3 + 1], TILE0_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH * 4 + 1], TILE0_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH * 4 + 4], TILE0_PIXEL);
    CHECK_EQ(dest_surface[DEST_PITCH * 5 + 1], GUARD);
    CHECK_EQ(painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* The last two pushes are element 7 then element 8, so the mode is the last
   slot of the request and the operand the one before it.  Mode 13 runs off the
   end of the dispatcher's compare chain and reaches no kernel at all, so
   nothing is painted -- while the three globals are still published, which is
   how this case tells "the call was made with mode 13" apart from "the cell
   was rejected before the call". */
static void blit_mode_comes_from_the_last_slot(void)
{
    stage_saf(2);
    stage_request(1, 1, 0, 13, 0);
    data_fdps_graphics_rle_blit_dst_pitch = 0xffff;
    data_fdps_graphics_rle_blit_src_width = 0xffff;
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(painted_bytes(), 0);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_pitch, DEST_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_src_width, CELL_WIDTH);
}

/* And the mirror of it: the same 13 in element 7 is the mode operand, which
   mode 0 never reads, so the cell draws exactly as it did with both slots
   zero.  Reading the operand slot as the mode would drop this cell. */
static void blit_operand_is_not_read_as_the_mode(void)
{
    stage_saf(2);
    stage_request(1, 1, 0, 0, 13);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(dest_surface[DEST_PITCH + 1], TILE0_PIXEL);
    CHECK_EQ(painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* Only the low byte of the mode slot reaches the dispatcher -- the assembly
   pushes the whole dword and fdps_blit_dispatch reads a byte off it -- so
   0x100 is mode 0 and draws, rather than being an out-of-range mode that
   draws nothing. */
static void only_the_low_byte_of_the_mode_is_read(void)
{
    stage_saf(2);
    stage_request(1, 1, 0, 0x100, 0);
    fdps_draw_tilemap_cell(draw_request);

    CHECK_EQ(dest_surface[DEST_PITCH + 1], TILE0_PIXEL);
    CHECK_EQ(painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

void run_sprite_tests(void)
{
    RUN_TEST(cell_lands_at_the_requests_x_and_y);
    RUN_TEST(cell_size_and_pitch_reach_the_dispatcher);
    RUN_TEST(tile_index_selects_the_table_entry);
    RUN_TEST(negative_tile_index_draws_nothing);
    RUN_TEST(tile_index_at_the_count_draws_nothing);
    RUN_TEST(x_of_zero_draws_nothing);
    RUN_TEST(y_of_zero_draws_nothing);
    RUN_TEST(x_at_the_pitch_bound_draws_nothing);
    RUN_TEST(x_one_inside_the_pitch_bound_draws);
    RUN_TEST(y_at_the_row_bound_draws_nothing);
    RUN_TEST(y_one_inside_the_row_bound_draws);
    RUN_TEST(blit_mode_comes_from_the_last_slot);
    RUN_TEST(blit_operand_is_not_read_as_the_mode);
    RUN_TEST(only_the_low_byte_of_the_mode_is_read);
}
