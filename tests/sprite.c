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
 * The layer cases add the assembly at 00013fd0 -- XOR EDX,EDX / MOV DX,word
 * ptr [EAX+0x16] / CMP EDX,dword ptr [EAX+0x18] / JLE then CMP dword ptr
 * [EAX+0x18],0x0 / JGE for the tilemap index test, MOVSX EAX,word ptr [EAX]
 * and MOVSX EAX,word ptr [EAX+0x2] at 00014045 and 0001404e for the columns
 * and rows, MOVSX EAX,word ptr [EAX] at 000140a9 for each cell, the memmove
 * of 0x24 bytes at 00014063, MOV EAX,dword ptr [EBP+0x14] / MOV EAX,dword ptr
 * [EAX+0xc] at 00014084 for the x written at the top of every row, ADD dword
 * ptr [EBP+-0x3c],EAX at 000140c2 for the step across and ADD dword ptr
 * [EBP+-0x38],EAX at 000140ca for the step down -- and the tilemap record from
 * resource_info/saf.md (section descriptor 1, so the u16 item count is at
 * +0x16 and the u32 section start at +0x18; a record is the i16 column count,
 * the i16 row count and then one i16 tile number per cell in row-major order).
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
#include "audio.h"

/* Header offsets and the geometry the fixture states.  Repeated here rather
   than shared with src/sprite.c: these are what the format and the assembly
   encode, and a test that took them from the emitted source would only prove
   the code agrees with itself. */
#define SAF_CELL_WIDTH_AT 0x07
#define SAF_CELL_HEIGHT_AT 0x09
#define SAF_TILEMAP_COUNT_AT 0x16
#define SAF_TILEMAP_SECTION_START_AT 0x18
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

/* The tilemap section follows the tile streams: its four-entry offset table at
   0x44 and then the four records, the last of them ending at 0x78.  A .SAF
   really does lay section 1 before section 2, but nothing in either drawer
   walks from one section to the next -- both reach a record only through the
   descriptor in the header -- so the fixture puts the tilemaps after the tiles
   to leave the offsets the existing cell cases pin unchanged. */
#define TILEMAP_TABLE_AT 0x44
#define TILEMAP_COUNT 4
#define TILEMAP_GRID_AT 0x54          /* 3 columns x 2 rows */
#define TILEMAP_NEGATIVE_ROWS_AT 0x64 /* 1 column, -1 rows */
#define TILEMAP_ONE_DROPPED_AT 0x6a   /* 2 columns x 1 row, first cell is -1 */
#define TILEMAP_SINGLE_AT 0x72        /* 1 column x 1 row */

#define GRID_COLUMNS 3
#define GRID_ROWS 2

#define IMAGE_SIZE 0x200

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

/* The layer walk needs a surface three cells wide and two cells tall with room
   left over on all four sides, because the cell drawer refuses a cell whose x
   is not strictly inside pitch - cell width and whose y is not strictly inside
   rows - cell height.  16 by 8 leaves x accepted through 11 and y through 5,
   so a 3x2 grid of 4x2 cells starting at 1,1 -- last cell at x 9, y 3 -- is
   comfortably inside both, and a grid that walked one cell too far in either
   direction would still be inside the buffer and would show up as painted
   guards rather than as a fault. */
#define LAYER_PITCH 16
#define LAYER_ROWS 8
#define LAYER_BYTES (LAYER_PITCH * LAYER_ROWS)

static unsigned char layer_surface[LAYER_BYTES];

/* Row-major, columns first: with tiles alternating in both directions a cell
   drawn at the wrong grid position paints the wrong pixel value and not just
   the wrong place, so a walk that read the record as rows-then-columns is
   caught by the pixel and not only by the count. */
static int grid_cells[GRID_COLUMNS * GRID_ROWS] = {0, 1, 0, 1, 0, 1};
static int one_cell[1] = {0};
static int dropped_then_drawn[2] = {-1, 1};
static int single_cell[1] = {1};

static void stage_i16(int at, int value)
{
    stage_u16(at, (unsigned long) (value & 0xffff));
}

/* A tilemap record: the column count, the row count and cell_count tile
   numbers, all i16.  The cell count is passed rather than derived so a record
   with a negative row count can still carry a cell to walk into. */
static void stage_tilemap(int at, int columns, int rows, int *cells,
                          int cell_count)
{
    int index;

    stage_i16(at, columns);
    stage_i16(at + 2, rows);
    for (index = 0; index < cell_count; index++) {
        stage_i16(at + 4 + index * 2, cells[index]);
    }
}

static void stage_saf_with_tilemaps(void)
{
    stage_saf(2);
    stage_u16(SAF_TILEMAP_COUNT_AT, TILEMAP_COUNT);
    stage_u32(SAF_TILEMAP_SECTION_START_AT, TILEMAP_TABLE_AT);
    stage_u32(TILEMAP_TABLE_AT, TILEMAP_GRID_AT);
    stage_u32(TILEMAP_TABLE_AT + 4, TILEMAP_NEGATIVE_ROWS_AT);
    stage_u32(TILEMAP_TABLE_AT + 8, TILEMAP_ONE_DROPPED_AT);
    stage_u32(TILEMAP_TABLE_AT + 12, TILEMAP_SINGLE_AT);
    stage_tilemap(TILEMAP_GRID_AT, GRID_COLUMNS, GRID_ROWS, grid_cells,
                  GRID_COLUMNS * GRID_ROWS);
    stage_tilemap(TILEMAP_NEGATIVE_ROWS_AT, 1, -1, one_cell, 1);
    stage_tilemap(TILEMAP_ONE_DROPPED_AT, 2, 1, dropped_then_drawn, 2);
    stage_tilemap(TILEMAP_SINGLE_AT, 1, 1, single_cell, 1);
}

static void stage_layer_request(int x, int y, int tilemap_index, int blit_mode,
                                int blit_operand)
{
    memset(layer_surface, GUARD, LAYER_BYTES);
    draw_request[DRAW_REQUEST_DEST_BASE] = (int) layer_surface;
    draw_request[DRAW_REQUEST_DEST_PITCH] = LAYER_PITCH;
    draw_request[DRAW_REQUEST_DEST_ROWS] = LAYER_ROWS;
    draw_request[DRAW_REQUEST_X] = x;
    draw_request[DRAW_REQUEST_Y] = y;
    draw_request[DRAW_REQUEST_IMAGE] = (int) stage_image;
    draw_request[DRAW_REQUEST_ITEM_INDEX] = tilemap_index;
    draw_request[DRAW_REQUEST_BLIT_OPERAND] = blit_operand;
    draw_request[DRAW_REQUEST_BLIT_MODE] = blit_mode;
}

static int layer_painted_bytes(void)
{
    int index;
    int painted;

    painted = 0;
    for (index = 0; index < LAYER_BYTES; index++) {
        if (layer_surface[index] != GUARD) {
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

/* The whole 3x2 grid, cell by cell.  Row 0 is tiles 0, 1, 0 across columns 1,
   5 and 9 of rows 1 and 2; row 1 is tiles 1, 0, 1 across the same columns of
   rows 3 and 4.  Four things are pinned at once: the record's first i16 is the
   column count and its second the row count -- read the other way round this
   would be a 2x3 grid whose third row lands on surface row 5, which is checked
   as a guard; the cells are walked in row-major order, since the alternating
   tile values put a different pixel in every neighbour; x steps by the cell
   width, since column 5 is TILE1 and column 4 is still TILE0's last byte; and
   y steps by the cell height, since row 3 is the second grid row rather than
   row 2 or row 4. */
static void layer_paints_the_whole_grid(void)
{
    stage_saf_with_tilemaps();
    stage_layer_request(1, 1, 0, 0, 0);
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 0], GUARD);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], TILE0_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 4], TILE0_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 5], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 9], TILE0_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 12], TILE0_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 13], GUARD);
    CHECK_EQ(layer_surface[LAYER_PITCH * 2 + 5], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 1], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 5], TILE0_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 9], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 4 + 12], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 0 + 1], GUARD);
    CHECK_EQ(layer_surface[LAYER_PITCH * 5 + 1], GUARD);
    CHECK_EQ(layer_painted_bytes(),
             GRID_COLUMNS * GRID_ROWS * CELL_WIDTH * CELL_HEIGHT);
}

/* x is reset at the top of every row from the caller's block, so the second
   grid row starts back at the request's x.  Carrying the copy's running x on
   instead would put row 1's first cell at column 13, where the cell drawer
   would refuse it -- pitch - width is 12 -- and the surface would end up with
   three cells painted rather than six. */
static void every_row_starts_back_at_the_requests_x(void)
{
    stage_saf_with_tilemaps();
    stage_layer_request(1, 1, 0, 0, 0);
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 1], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 13], GUARD);
    CHECK_EQ(layer_painted_bytes(),
             GRID_COLUMNS * GRID_ROWS * CELL_WIDTH * CELL_HEIGHT);
}

/* The grid's top left corner is the request's own x and y, not a fixed origin:
   moved one column and one row on, every cell moves with it and the row above
   and the column to the left go back to holding guards. */
static void the_grid_origin_is_the_requests_x_and_y(void)
{
    stage_saf_with_tilemaps();
    stage_layer_request(2, 2, 0, 0, 0);
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(layer_surface[LAYER_PITCH * 2 + 2], TILE0_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 2 + 1], GUARD);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 2], GUARD);
    CHECK_EQ(layer_surface[LAYER_PITCH * 4 + 2], TILE1_PIXEL);
    CHECK_EQ(layer_painted_bytes(),
             GRID_COLUMNS * GRID_ROWS * CELL_WIDTH * CELL_HEIGHT);
}

/* Every cell is drawn through a copy of the request, so the caller's block
   comes back exactly as it went in -- its element 6 still the tilemap index
   and not the last cell's tile number, and its x still the origin and not the
   right edge of the last cell.  fdps_draw_composite_sprite relies on this: it
   reuses the same block for the next part of the sprite. */
static void the_callers_request_is_left_alone(void)
{
    stage_saf_with_tilemaps();
    stage_layer_request(1, 1, 0, 0, 0);
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(draw_request[DRAW_REQUEST_ITEM_INDEX], 0);
    CHECK_EQ(draw_request[DRAW_REQUEST_X], 1);
    CHECK_EQ(draw_request[DRAW_REQUEST_Y], 1);
    CHECK_EQ(draw_request[DRAW_REQUEST_DEST_PITCH], LAYER_PITCH);
}

/* CMP dword ptr [EAX+0x18],0x0 / JGE: a negative tilemap index is refused
   before the offset table is touched. */
static void negative_tilemap_index_draws_nothing(void)
{
    stage_saf_with_tilemaps();
    stage_layer_request(1, 1, -1, 0, 0);
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(layer_painted_bytes(), 0);
}

/* The upper bound is the item count itself: with four tilemaps, index 4 draws
   nothing and index 3 -- the single-cell record -- draws its one cell. */
static void tilemap_index_at_the_count_draws_nothing(void)
{
    stage_saf_with_tilemaps();
    stage_layer_request(1, 1, TILEMAP_COUNT, 0, 0);
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(layer_painted_bytes(), 0);

    stage_layer_request(1, 1, TILEMAP_COUNT - 1, 0, 0);
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], TILE1_PIXEL);
    CHECK_EQ(layer_painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* MOVSX, not a zero-extending load: the row count is signed, so a record
   claiming -1 rows leaves the outer loop unentered.  Read unsigned the same
   two bytes would be 65535 rows and the walk would run down the page past
   every byte of the surface. */
static void a_negative_row_count_draws_nothing(void)
{
    stage_saf_with_tilemaps();
    stage_layer_request(1, 1, 1, 0, 0);
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(layer_painted_bytes(), 0);
}

/* A cell whose tile number is out of range is dropped by the cell drawer and
   the walk carries on: the first cell of this record is -1 and paints nothing,
   and the second still lands one cell width along at column 5 rather than at
   the column the first one vacated. */
static void a_dropped_cell_does_not_stop_the_walk(void)
{
    stage_saf_with_tilemaps();
    stage_layer_request(1, 1, 2, 0, 0);
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], GUARD);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 5], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 8], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 2 + 5], TILE1_PIXEL);
    CHECK_EQ(layer_painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* The copy carries every slot the layer walk does not touch through to the
   cell drawer.  The destination surface and its pitch arrive -- the pitch
   reaches the dispatcher's global, seeded first with a value the call cannot
   produce -- and so does the mode: the same draw with mode 13 runs off the end
   of the dispatcher's compare chain and paints nothing while still publishing
   that pitch, which is how this tells "the call was made with mode 13" apart
   from "the cell never got there". */
static void the_copy_carries_the_surface_and_the_mode(void)
{
    stage_saf_with_tilemaps();
    stage_layer_request(1, 1, TILEMAP_COUNT - 1, 0, 0);
    data_fdps_graphics_rle_blit_dst_pitch = 0xffff;
    data_fdps_graphics_rle_blit_src_width = 0xffff;
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(data_fdps_graphics_rle_blit_dst_pitch, LAYER_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_src_width, CELL_WIDTH);

    stage_layer_request(1, 1, TILEMAP_COUNT - 1, 13, 0);
    data_fdps_graphics_rle_blit_dst_pitch = 0xffff;
    fdps_draw_tilemap_layer(draw_request);

    CHECK_EQ(layer_painted_bytes(), 0);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_pitch, LAYER_PITCH);
}

/* THE FRAME CASES.  Expected values come from the assembly at 00014140 -- MOV
   AX,word ptr [EAX+0xc] / AND EAX,0xffff / CMP EAX,dword ptr [EBP+-0x5c] / JLE
   then CMP dword ptr [EBP+-0x5c],0x0 / JGE for the frame index test, ADD
   EDX,dword ptr [EAX+0xe] / LEA EAX,[EAX*0x4] / ADD EDX,EAX / ADD EAX,dword
   ptr [EDX] for the frame address, MOVSX EAX,word ptr [EAX+0x8] at 00014208
   for the layer count, ADD dword ptr [EBP+-0x14],0xd at 000142a3 for the step
   from one layer record to the next, MOVSX EDX,word ptr [EAX+0x2] and MOVSX
   EDX,word ptr [EAX+0x4] at 00014268 and 0001427a for the layer's offsets
   against the caller's x and y, XOR EAX,EAX / MOV AX,word ptr [EDX] at 00014289
   for the tilemap number, CMP dword ptr [EAX+0x20],0x0 / JNZ at 00014221 for
   the caller's-mode guard, the flag chain at 00014232 and 00014241 with no
   else, MOV EDX,0x10 / SUB EDX,EAX at 0001424e for the level, and CMP byte ptr
   [EBP+0x18],0x0 / JZ at 000142ac for the sound -- and from the .SAF layout in
   resource_info/saf.md (frame is section 0, so its descriptor is the first
   10-byte one: u16 count at +0x0c, u32 start at +0x0e; a frame is i16 sound
   number, i16 duration, two hint bytes, a zero u16 and an i16 layer count,
   then that many 13-byte layers of u16 tilemap number, i16 x, i16 y, u8 blend
   flag, u8 blend level and five reserved bytes).  None of them is read off the
   emitted C.

   HOW A BLENDED PIXEL IS MADE TELL ITS LEVEL.  Mode 9 reaches
   fdps_rle_blit_translucent, which weights the source pixel through shade-ramp
   row level + 9 and the destination pixel through row level, folds the sum and
   looks the result up in the inverse colour cube (rleblend.h).  The two tables
   are staged here rather than left as ticket 23 will fill them: every ramp
   entry is zero except the one column the tile's pixel value indexes, which
   holds its own row number times sixteen, and the cube is a sentinel
   everywhere except its first eighteen entries, which hold 0x40 plus their
   index.  The destination is a guard byte, which indexes a zero, so a blended
   pixel comes back as 0x40 plus the source row -- that is, 0x40 plus 9 plus
   the level -- and reading it says which level the descriptor carried.  A
   level handed through unchanged instead of inverted picks the other row of
   the pair and lands on a different cube entry, so the two spellings cannot
   both pass.

   THE SOUND IS OBSERVED THROUGH THE LIBRARY, as tests/audio.c observes it: the
   real AIL is linked in, so the address it was handed is read back out of the
   sample structure.  The handle fixture is the smaller half of that file's --
   one free slot on every handle, a driver whose started flag is set so
   AIL_start_sample returns before touching hardware -- because the question
   here is only whether the call was made and with which two arguments. */

#define SAF_FRAME_COUNT_AT 0x0c
#define SAF_FRAME_SECTION_START_AT 0x0e
#define SAF_SOUND_COUNT_AT 0x2a
#define SAF_SOUND_SECTION_START_AT 0x2c

/* The frame section's offset table follows the last tilemap record at 0x78,
   and the six frames follow it: 10 bytes of header and 13 per layer each. */
#define FRAME_TABLE_AT 0x78
#define FRAME_COUNT 6
#define FRAME_TWO_LAYERS_AT 0x90          /* 2 layers, offsets 0,0 and 4,2 */
#define FRAME_NEGATIVE_OFFSET_AT 0xb4     /* 1 layer at -1,-1 */
#define FRAME_NO_LAYERS_AT 0xcb           /* layer count -1 */
#define FRAME_BLEND_THEN_OPAQUE_AT 0xd5   /* flag 1 then flag 0 */
#define FRAME_BLEND_THEN_OTHER_AT 0xf9    /* flag 1 then flag 2 */
#define FRAME_BLEND_LEVEL_B_AT 0x11d      /* 1 layer, the other blend level */

#define FRAME_TWO_LAYERS 0
#define FRAME_NEGATIVE_OFFSET 1
#define FRAME_NO_LAYERS 2
#define FRAME_BLEND_THEN_OPAQUE 3
#define FRAME_BLEND_THEN_OTHER 4
#define FRAME_BLEND_LEVEL_B 5

#define LAYER_RECORD_BYTES 13
#define SECOND_LAYER_DX 4
#define SECOND_LAYER_DY 2

/* The two sounds the frames name, and the frames that name them.  -1 is what a
   frame with no sound stores. */
#define SOUND_TABLE_AT 0x140
#define SOUND_COUNT 2
#define CLIP_0_AT 0x150
#define CLIP_1_AT 0x180
#define NO_SOUND (-1)
#define TWO_LAYERS_SOUND 1
#define NEGATIVE_OFFSET_SOUND 0

/* The shade ramp is eighteen rows of 256 and the translucent kernel reads the
   source through row level + 9; the cube is 4096 bytes of which only the low
   entries are ever reached here. */
#define RAMP_ROW_ENTRIES 0x100
#define RAMP_ROWS 18
#define RAMP_COMPLEMENT_ROWS 9
#define CUBE_ENTRIES 0x1000
#define CUBE_BASE 0x40
#define CUBE_SENTINEL 0x11

/* Two blend levels, both stored as the layer record's opacity field, and the
   cube entry each one must land on once the drawer has inverted it. */
#define BLEND_FIELD_A 13
#define BLEND_FIELD_B 10
#define BLENDED_PIXEL_A \
    (CUBE_BASE + RAMP_COMPLEMENT_ROWS + (16 - BLEND_FIELD_A))
#define BLENDED_PIXEL_B \
    (CUBE_BASE + RAMP_COMPLEMENT_ROWS + (16 - BLEND_FIELD_B))

/* A blend flag the assembly tests for neither value of. */
#define BLEND_FLAG_OTHER 2

/* The AIL sample fixture, the smaller half of tests/audio.c's: the vendor
   field offsets are that file's, taken from the library's own code. */
#define SAMPLE_STATUS 0x04
#define SAMPLE_ADDRESS 0x08
#define SAMPLE_WORDS (0x854 / 4 + 1)
#define SAMPLE_DRIVER 0x00
#define DRIVER_WORDS (0x80 / 4)
#define DRIVER_STARTED 0x54
#define STATUS_DONE 2
#define CLIP_SAMPLES_AT 8

static unsigned int free_sample[SAMPLE_WORDS];
static unsigned int fake_driver[DRIVER_WORDS];

static void stage_frame_record(int at, int sound_id, int layer_count)
{
    stage_i16(at, sound_id);
    stage_i16(at + 2, 1);
    stage_i16(at + 8, layer_count);
}

static void stage_layer_record(int at, int tilemap, int dx, int dy, int flag,
                               int level)
{
    stage_u16(at, (unsigned long) tilemap);
    stage_i16(at + 2, dx);
    stage_i16(at + 4, dy);
    stage_image[at + 6] = (unsigned char) flag;
    stage_image[at + 7] = (unsigned char) level;
}

static void stage_clip(int at, unsigned long rate, unsigned long length)
{
    stage_image[at] = 1;
    stage_image[at + 1] = 8;
    stage_u16(at + 2, rate);
    stage_u32(at + 4, length);
}

static void stage_blend_tables(void)
{
    int row;
    int index;

    for (index = 0; index < RAMP_ROWS * RAMP_ROW_ENTRIES; index++) {
        data_fdps_palette_shade_ramp_table[index] = 0;
    }
    for (row = 0; row < RAMP_ROWS; row++) {
        data_fdps_palette_shade_ramp_table[row * RAMP_ROW_ENTRIES
                                           + TILE1_PIXEL] =
            (unsigned int) (row * 16);
    }
    for (index = 0; index < CUBE_ENTRIES; index++) {
        data_fdps_inverse_palette_cube[index] = CUBE_SENTINEL;
    }
    for (index = 0; index < RAMP_ROWS; index++) {
        data_fdps_inverse_palette_cube[index] =
            (unsigned char) (CUBE_BASE + index);
    }
}

/* Every handle free, so the scan stops on slot 0 and the address it was given
   lands in free_sample. */
static void stage_sample_slots(void)
{
    int index;

    for (index = 0; index < DRIVER_WORDS; index++) {
        fake_driver[index] = 0;
    }
    fake_driver[DRIVER_STARTED / 4] = 1;
    for (index = 0; index < SAMPLE_WORDS; index++) {
        free_sample[index] = 0;
    }
    free_sample[SAMPLE_STATUS / 4] = STATUS_DONE;
    free_sample[SAMPLE_DRIVER / 4] = (unsigned int) fake_driver;
    for (index = 0; index < SFX_SAMPLE_SLOT_COUNT; index++) {
        data_fdps_audio_sample_handle_table[index] = free_sample;
    }
    data_fdps_audio_sfx_driver_available_flag = 1;
    data_fdps_audio_sfx_enabled_flag = 1;
}

/* Where in the staged image the address AIL was handed points, or -1 when
   nothing was handed over at all. */
static long played_clip_offset(void)
{
    unsigned int address;

    address = free_sample[SAMPLE_ADDRESS / 4];
    if (address == 0) {
        return -1;
    }
    return (long) (address - (unsigned int) stage_image);
}

/* The tilemap every layer below names is index 3, the single-cell record whose
   one cell is tile 1, so one layer paints one 4x2 cell of TILE1_PIXEL and the
   count of painted bytes is the count of layers that drew. */
static void stage_saf_with_frames(void)
{
    stage_saf_with_tilemaps();
    stage_blend_tables();
    stage_sample_slots();

    stage_u16(SAF_FRAME_COUNT_AT, FRAME_COUNT);
    stage_u32(SAF_FRAME_SECTION_START_AT, FRAME_TABLE_AT);
    stage_u32(FRAME_TABLE_AT + FRAME_TWO_LAYERS * 4, FRAME_TWO_LAYERS_AT);
    stage_u32(FRAME_TABLE_AT + FRAME_NEGATIVE_OFFSET * 4,
              FRAME_NEGATIVE_OFFSET_AT);
    stage_u32(FRAME_TABLE_AT + FRAME_NO_LAYERS * 4, FRAME_NO_LAYERS_AT);
    stage_u32(FRAME_TABLE_AT + FRAME_BLEND_THEN_OPAQUE * 4,
              FRAME_BLEND_THEN_OPAQUE_AT);
    stage_u32(FRAME_TABLE_AT + FRAME_BLEND_THEN_OTHER * 4,
              FRAME_BLEND_THEN_OTHER_AT);
    stage_u32(FRAME_TABLE_AT + FRAME_BLEND_LEVEL_B * 4, FRAME_BLEND_LEVEL_B_AT);

    stage_frame_record(FRAME_TWO_LAYERS_AT, TWO_LAYERS_SOUND, 2);
    stage_layer_record(FRAME_TWO_LAYERS_AT + 10, TILEMAP_COUNT - 1, 0, 0, 0, 0);
    stage_layer_record(FRAME_TWO_LAYERS_AT + 10 + LAYER_RECORD_BYTES,
                       TILEMAP_COUNT - 1, SECOND_LAYER_DX, SECOND_LAYER_DY,
                       0, 0);

    stage_frame_record(FRAME_NEGATIVE_OFFSET_AT, NEGATIVE_OFFSET_SOUND, 1);
    stage_layer_record(FRAME_NEGATIVE_OFFSET_AT + 10, TILEMAP_COUNT - 1,
                       -1, -1, 0, 0);

    stage_frame_record(FRAME_NO_LAYERS_AT, NO_SOUND, -1);

    stage_frame_record(FRAME_BLEND_THEN_OPAQUE_AT, NO_SOUND, 2);
    stage_layer_record(FRAME_BLEND_THEN_OPAQUE_AT + 10, TILEMAP_COUNT - 1,
                       0, 0, 1, BLEND_FIELD_A);
    stage_layer_record(FRAME_BLEND_THEN_OPAQUE_AT + 10 + LAYER_RECORD_BYTES,
                       TILEMAP_COUNT - 1, SECOND_LAYER_DX, SECOND_LAYER_DY,
                       0, 0);

    stage_frame_record(FRAME_BLEND_THEN_OTHER_AT, NO_SOUND, 2);
    stage_layer_record(FRAME_BLEND_THEN_OTHER_AT + 10, TILEMAP_COUNT - 1,
                       0, 0, 1, BLEND_FIELD_A);
    stage_layer_record(FRAME_BLEND_THEN_OTHER_AT + 10 + LAYER_RECORD_BYTES,
                       TILEMAP_COUNT - 1, SECOND_LAYER_DX, SECOND_LAYER_DY,
                       BLEND_FLAG_OTHER, 0);

    stage_frame_record(FRAME_BLEND_LEVEL_B_AT, NO_SOUND, 1);
    stage_layer_record(FRAME_BLEND_LEVEL_B_AT + 10, TILEMAP_COUNT - 1,
                       0, 0, 1, BLEND_FIELD_B);

    stage_u16(SAF_SOUND_COUNT_AT, SOUND_COUNT);
    stage_u32(SAF_SOUND_SECTION_START_AT, SOUND_TABLE_AT);
    stage_u32(SOUND_TABLE_AT, CLIP_0_AT);
    stage_u32(SOUND_TABLE_AT + 4, CLIP_1_AT);
    stage_clip(CLIP_0_AT, 11025, 0x10);
    stage_clip(CLIP_1_AT, 22050, 0x20);
}

static void stage_composite_request(int x, int y, int frame_index,
                                    int blit_mode)
{
    memset(layer_surface, GUARD, LAYER_BYTES);
    draw_request[DRAW_REQUEST_DEST_BASE] = (int) layer_surface;
    draw_request[DRAW_REQUEST_DEST_PITCH] = LAYER_PITCH;
    draw_request[DRAW_REQUEST_DEST_ROWS] = LAYER_ROWS;
    draw_request[DRAW_REQUEST_X] = x;
    draw_request[DRAW_REQUEST_Y] = y;
    draw_request[DRAW_REQUEST_IMAGE] = (int) stage_image;
    draw_request[DRAW_REQUEST_ITEM_INDEX] = frame_index;
    draw_request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    draw_request[DRAW_REQUEST_BLIT_MODE] = blit_mode;
}

/* Both layers of the frame, each at the request's origin plus its own offset.
   Three things are pinned at once: the walk steps thirteen bytes from one
   layer record to the next -- a step of twelve or fourteen reads the second
   layer's tilemap number out of the middle of the first record and draws
   nothing or the wrong grid; the second layer lands at x+4, y+2 rather than
   carrying the first layer's position on; and the layer count comes from the
   i16 at +0x08 of the frame rather than the zero u16 at +0x06, which would
   leave the walk unentered. */
static void composite_draws_every_layer_at_its_own_offset(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_TWO_LAYERS, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 0], GUARD);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 4], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 5], GUARD);
    CHECK_EQ(layer_surface[LAYER_PITCH * 2 + 1], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 4], GUARD);
    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 5], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 8], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 4 + 8], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 5 + 5], GUARD);
    CHECK_EQ(layer_painted_bytes(), 2 * CELL_WIDTH * CELL_HEIGHT);
}

/* MOVSX, not a zero-extending load: a layer whose x and y are -1 lands one
   column and one row back from the request's origin.  Read the same two bytes
   unsigned the layer would be aimed at x 65537, which the cell drawer refuses
   -- pitch - width is 12 -- and nothing at all would be painted. */
static void layer_offsets_are_signed(void)
{
    stage_saf_with_frames();
    stage_composite_request(2, 2, FRAME_NEGATIVE_OFFSET, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 2 + 4], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 2 + 2], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 1], GUARD);
    CHECK_EQ(layer_painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* MOVSX EAX,word ptr [EAX+0x8]: the layer count is signed, so a frame claiming
   -1 layers draws nothing.  Read unsigned the walk would run 65535 times over
   whatever follows the record. */
static void a_negative_layer_count_draws_nothing(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_NO_LAYERS, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_painted_bytes(), 0);
}

/* CMP dword ptr [EBP+-0x5c],0x0 / JGE: a negative frame index is refused
   before the offset table is touched, and the whole body -- sound included --
   is under that test. */
static void a_negative_frame_index_draws_nothing(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, -1, 0);
    fdps_draw_composite_sprite(draw_request, (char) 1);

    CHECK_EQ(layer_painted_bytes(), 0);
    CHECK_EQ(played_clip_offset(), -1);
}

/* The upper bound is the item count itself: with six frames, index 6 draws
   nothing and index 5 -- the last one -- draws its single layer. */
static void a_frame_index_at_the_count_draws_nothing(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_COUNT, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_painted_bytes(), 0);

    stage_composite_request(1, 1, FRAME_COUNT - 1, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* The index scales the frame section's offset table by four and the entry it
   reads is rebased onto the image, not onto the table: frame 1's record sits
   at 0xb4 and the table at 0x78, so an implementation that added the entry to
   the table address would land at 0x12c, in the middle of the last frame's
   layer record, and read its layer count out of whatever is there.  Frames 0
   and 1 differ in layer count and in offset, so either error shows. */
static void the_frame_table_entry_is_rebased_on_the_image(void)
{
    stage_saf_with_frames();
    stage_composite_request(2, 2, FRAME_NEGATIVE_OFFSET, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], TILE1_PIXEL);
    CHECK_EQ(layer_painted_bytes(), CELL_WIDTH * CELL_HEIGHT);

    stage_composite_request(2, 2, FRAME_TWO_LAYERS, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_surface[LAYER_PITCH * 2 + 2], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 4 + 6], TILE1_PIXEL);
    CHECK_EQ(layer_painted_bytes(), 2 * CELL_WIDTH * CELL_HEIGHT);
}

/* Every layer is drawn through a copy, so the caller's block comes back
   exactly as it went in: its element 6 still the frame index and not the last
   layer's tilemap number, its x and y still the origin, and its mode and
   operand still the caller's -- the blend setup writes only the copy.  Callers
   draw frame after frame out of one block and rely on it. */
static void the_callers_request_survives_the_frame(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_BLEND_THEN_OPAQUE, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(draw_request[DRAW_REQUEST_ITEM_INDEX], FRAME_BLEND_THEN_OPAQUE);
    CHECK_EQ(draw_request[DRAW_REQUEST_X], 1);
    CHECK_EQ(draw_request[DRAW_REQUEST_Y], 1);
    CHECK_EQ(draw_request[DRAW_REQUEST_BLIT_MODE], 0);
    CHECK_EQ(draw_request[DRAW_REQUEST_BLIT_OPERAND], 0);
    CHECK_EQ(draw_request[DRAW_REQUEST_IMAGE], (int) stage_image);
}

/* Blend flag 1 puts the copy into mode 9 with the three-dword descriptor, and
   flag 0 on the next layer puts it back to mode 0: the first cell comes back
   as a cube entry and the second as the tile's own pixel.  A mode left at 9
   for the second layer would blend it too, and a mode never set to 9 for the
   first would paint TILE1_PIXEL there. */
static void a_translucent_layer_blends_and_the_next_opaque_one_does_not(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_BLEND_THEN_OPAQUE, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], BLENDED_PIXEL_A);
    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 4], BLENDED_PIXEL_A);
    CHECK_EQ(layer_surface[LAYER_PITCH * 2 + 1], BLENDED_PIXEL_A);
    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 5], TILE1_PIXEL);
    CHECK_EQ(layer_surface[LAYER_PITCH * 4 + 8], TILE1_PIXEL);
    CHECK_EQ(layer_painted_bytes(), 2 * CELL_WIDTH * CELL_HEIGHT);
}

/* MOV EDX,0x10 / SUB EDX,EAX: the descriptor's level is 16 minus the record's
   field, and two different fields land on two different cube entries.  Field
   13 gives level 3 and reads shade-ramp row 12; field 10 gives level 6 and
   reads row 15.  Handing either field through unchanged would pick the
   complementary row -- row 3 and row 6 -- and land on cube entries 0x43 and
   0x46, neither of which is what is checked here. */
static void the_blend_level_is_sixteen_minus_the_records_field(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_BLEND_THEN_OPAQUE, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], BLENDED_PIXEL_A);

    stage_composite_request(1, 1, FRAME_BLEND_LEVEL_B, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], BLENDED_PIXEL_B);
    CHECK_EQ(layer_painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}

/* The flag chain is a test for 0 and a test for 1 and nothing else, so a layer
   carrying any other value leaves the copy holding whatever the layer before
   it set.  Here the second layer's flag is 2 and it comes out blended at the
   first layer's level, because neither the mode nor the descriptor was
   rewritten.  An else that reset the mode -- the natural thing to write --
   would paint TILE1_PIXEL there instead. */
static void an_unhandled_blend_flag_inherits_the_previous_layer(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_BLEND_THEN_OTHER, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], BLENDED_PIXEL_A);
    CHECK_EQ(layer_surface[LAYER_PITCH * 3 + 5], BLENDED_PIXEL_A);
    CHECK_EQ(layer_surface[LAYER_PITCH * 4 + 8], BLENDED_PIXEL_A);
    CHECK_EQ(layer_painted_bytes(), 2 * CELL_WIDTH * CELL_HEIGHT);
}

/* CMP dword ptr [EAX+0x20],0x0 / JNZ: the whole blend setup is skipped while
   the CALLER's own mode slot is non-zero, so the layers keep the caller's mode
   and their own blend fields are never read.  Mode 13 reaches no kernel at
   all, so nothing is painted -- where a blend setup that ran would have put
   the first layer into mode 9 and painted it. */
static void the_callers_blit_mode_suppresses_the_blend_setup(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_BLEND_THEN_OPAQUE, 13);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_painted_bytes(), 0);

    stage_composite_request(1, 1, FRAME_BLEND_THEN_OPAQUE, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(layer_surface[LAYER_PITCH * 1 + 1], BLENDED_PIXEL_A);
}

/* CMP byte ptr [EBP+0x18],0x0 / JZ: the sound is played only when the flag is
   non-zero, and it is the frame's own leading i16 that names it -- frame 0
   names sound 1 and frame 1 names sound 0, so an implementation that read the
   sound number from the wrong frame or from the wrong offset would hand AIL
   the other clip.  The image handed over is the request's own, which is what
   makes the played address land inside the staged buffer at all. */
static void the_frames_sound_plays_only_when_the_flag_is_set(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_TWO_LAYERS, 0);
    fdps_draw_composite_sprite(draw_request, (char) 0);

    CHECK_EQ(played_clip_offset(), -1);

    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_TWO_LAYERS, 0);
    fdps_draw_composite_sprite(draw_request, (char) 1);

    CHECK_EQ(played_clip_offset(), CLIP_1_AT + CLIP_SAMPLES_AT);

    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_NEGATIVE_OFFSET, 0);
    fdps_draw_composite_sprite(draw_request, (char) 1);

    CHECK_EQ(played_clip_offset(), CLIP_0_AT + CLIP_SAMPLES_AT);
}

/* A frame whose script has no sound stores -1, and the drawer hands it through
   sign-extended rather than suppressing the call: fdps_sfx_play is what
   rejects it, on its own lower-bound test (audio.h).  What is observable from
   here is that nothing plays, and that the layers drew anyway. */
static void a_frame_with_no_sound_plays_nothing_and_still_draws(void)
{
    stage_saf_with_frames();
    stage_composite_request(1, 1, FRAME_BLEND_LEVEL_B, 0);
    fdps_draw_composite_sprite(draw_request, (char) 1);

    CHECK_EQ(played_clip_offset(), -1);
    CHECK_EQ(layer_painted_bytes(), CELL_WIDTH * CELL_HEIGHT);
}


/* ------------------------------------------------------------------------
 * fdps_blit_command_sprite -- the Command.cel drawer.
 *
 * Expected values come from the assembly at 00015b90 -- LEA EAX,[EAX*0x4+0x0]
 * / ADD EAX,EDX / MOV EAX,dword ptr [EAX+0xf] / ADD EAX,EDX for the stream
 * address, the two loads of the sheet pointer from 0x000643ac at 00015ba6 and
 * 00015bae, and the seven pushes at 00015bbc through 00015bcf, whose
 * immediates are 0x0, 0x0, 0x16 and 0x19 -- and from resource_info/cel.md
 * (a 15-byte header, the offset table immediately after it at +0x0f with
 * sprite_count+1 u32 entries all measured from the start of the file, and
 * COMMAND.CEL's 76 sprites of 25 by 22).  None of them is read off the
 * emitted C.
 *
 * The sheet is staged as a byte buffer rather than read from the real
 * COMMAND.CEL because the drawer never opens a file: it reads a pointer to an
 * already-unpacked block out of a global, and a buffer is exactly that block.
 * Its header is written a byte at a time so the test makes no alignment
 * assumption of its own -- the offset table starts at 0x0f, so every entry in
 * it sits on an odd address.
 *
 * The sprites really are 25 by 22, the size the drawer hardwires, because a
 * fixture that shrank them would not be exercising the constants at all.  A
 * fill run covering 25 columns is one command byte, 0x18 -- op 00 with a
 * length field of 24, and a run is length + 1 pixels of the byte that follows
 * (resource_info/cel.md) -- so a whole sprite is 22 two-byte rows.
 *
 * WHAT THE FIXTURE PUTS WHERE, AND WHY.  Entry 3 of the four-entry table is a
 * real stream and not the file-size sentinel a shipped sheet stores there:
 * index 3 is out of range for a sheet declaring 3 sprites, and staging
 * something drawable there is what makes "the index is not range checked"
 * observable instead of a walk off the end of the buffer.  A second, decoy
 * offset table sits at 0x180 with every entry pointing at a stream of its own
 * colour, and the header's table-position field at +0x05 -- the field the
 * drawer never reads -- points at it, so any byte of that colour reaching the
 * surface means the header was consulted.
 *
 * The surface is 32 by 26 with the sprite aimed at column 3, row 2, which
 * leaves a guard row above, three guard rows below and seven guard columns to
 * the right: a sprite drawn a row or a column out of place lands on guards
 * rather than off the buffer, and the guard bytes checked on all four edges
 * are what pin 25 and 22 rather than merely the 550 bytes those multiply to.
 */
#define CEL_TABLE_FIELD_AT 0x05
#define CEL_WIDTH_FIELD_AT 0x07
#define CEL_HEIGHT_FIELD_AT 0x09
#define CEL_COUNT_FIELD_AT 0x0b
#define CEL_ENCODING_FIELD_AT 0x0d
#define CEL_TABLE_AT 0x0f

#define CEL_SPRITE_W 25
#define CEL_SPRITE_H 22
#define CEL_SPRITE_COUNT 3

/* Four fill sprites of 44 bytes each and one mixed sprite of 198, spaced so
   none of them touches the next, then the decoy table and its stream. */
#define CEL_SPRITE0_AT 0x20
#define CEL_SPRITE1_AT 0xb0
#define CEL_SPRITE2_AT 0x50
#define CEL_SPRITE3_AT 0x80
#define CEL_DECOY_TABLE_AT 0x180
#define CEL_DECOY_STREAM_AT 0x190
#define CEL_SHEET_SIZE 0x200

/* Command bytes: op in the top two bits, length minus one in the low six
   (resource_info/cel.md).  0x18 is a 25-pixel fill, 0xc4 a 5-pixel skip, 0x0e
   a 15-pixel fill and 0x84 a 5-byte literal; the last three add up to the 25
   columns a row must cover exactly. */
#define CEL_FILL_RUN_25 0x18
#define CEL_SKIP_RUN_5 0xc4
#define CEL_FILL_RUN_15 0x0e
#define CEL_LITERAL_RUN_5 0x84
#define CEL_SKIP_COLUMNS 5
#define CEL_FILL_COLUMNS 15
#define CEL_LITERAL_COLUMNS 5
#define CEL_MIXED_ROW_BYTES 9

/* No two of these, and none of them and the guard, are the same byte, so a
   pixel always says which stream wrote it. */
#define CEL_PIXEL0 0x11
#define CEL_PIXEL0_ALT 0x77
#define CEL_PIXEL2 0x33
#define CEL_PIXEL3 0x44
#define CEL_DECOY_PIXEL 0x99
#define CEL_MIXED_FILL 0x22
#define CEL_LITERAL_FIRST 0x61

#define CEL_PITCH 32
#define CEL_NARROW_PITCH 30
#define CEL_SURFACE_ROWS 26
#define CEL_SURFACE_BYTES (CEL_PITCH * CEL_SURFACE_ROWS)
#define CEL_DEST_X 3
#define CEL_DEST_Y 2

static unsigned char cel_sheet[CEL_SHEET_SIZE];
static unsigned char cel_sheet_alt[CEL_SHEET_SIZE];
static unsigned char cel_surface[CEL_SURFACE_BYTES];

static void cel_u16(unsigned char *sheet, int at, unsigned long value)
{
    sheet[at] = (unsigned char) (value & 0xff);
    sheet[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void cel_u32(unsigned char *sheet, int at, unsigned long value)
{
    sheet[at] = (unsigned char) (value & 0xff);
    sheet[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    sheet[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    sheet[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* 22 rows of one 25-pixel fill each: a whole sprite in the sheet's declared
   size, and the plainest stream that covers every column of every row. */
static void cel_fill_stream(unsigned char *sheet, int at, unsigned char pixel)
{
    int row;

    for (row = 0; row < CEL_SPRITE_H; row++) {
        sheet[at + row * 2] = CEL_FILL_RUN_25;
        sheet[at + row * 2 + 1] = pixel;
    }
}

/* 22 rows of skip 5, fill 15, literal 5.  The literal run is what separates
   the pass-through kernel from every remapping one: its five bytes reach the
   destination verbatim only under blit mode 0. */
static void cel_mixed_stream(unsigned char *sheet, int at)
{
    int row;
    int column;
    int base;

    for (row = 0; row < CEL_SPRITE_H; row++) {
        base = at + row * CEL_MIXED_ROW_BYTES;
        sheet[base] = CEL_SKIP_RUN_5;
        sheet[base + 1] = CEL_FILL_RUN_15;
        sheet[base + 2] = CEL_MIXED_FILL;
        sheet[base + 3] = CEL_LITERAL_RUN_5;
        for (column = 0; column < CEL_LITERAL_COLUMNS; column++) {
            sheet[base + 4 + column] =
                (unsigned char) (CEL_LITERAL_FIRST + column);
        }
    }
}

static void cel_stage_sheet(unsigned char *sheet, unsigned char sprite0_pixel)
{
    memset(sheet, 0, CEL_SHEET_SIZE);
    sheet[0] = 'C';
    sheet[1] = 'E';
    sheet[2] = 'L';
    cel_u16(sheet, 0x03, 1);
    cel_u16(sheet, CEL_TABLE_FIELD_AT, CEL_DECOY_TABLE_AT);
    cel_u16(sheet, CEL_WIDTH_FIELD_AT, CEL_SPRITE_W);
    cel_u16(sheet, CEL_HEIGHT_FIELD_AT, CEL_SPRITE_H);
    cel_u16(sheet, CEL_COUNT_FIELD_AT, CEL_SPRITE_COUNT);
    cel_u16(sheet, CEL_ENCODING_FIELD_AT, 2);

    cel_u32(sheet, CEL_TABLE_AT, CEL_SPRITE0_AT);
    cel_u32(sheet, CEL_TABLE_AT + 4, CEL_SPRITE1_AT);
    cel_u32(sheet, CEL_TABLE_AT + 8, CEL_SPRITE2_AT);
    cel_u32(sheet, CEL_TABLE_AT + 12, CEL_SPRITE3_AT);

    cel_fill_stream(sheet, CEL_SPRITE0_AT, sprite0_pixel);
    cel_mixed_stream(sheet, CEL_SPRITE1_AT);
    cel_fill_stream(sheet, CEL_SPRITE2_AT, CEL_PIXEL2);
    cel_fill_stream(sheet, CEL_SPRITE3_AT, CEL_PIXEL3);

    cel_u32(sheet, CEL_DECOY_TABLE_AT, CEL_DECOY_STREAM_AT);
    cel_u32(sheet, CEL_DECOY_TABLE_AT + 4, CEL_DECOY_STREAM_AT);
    cel_u32(sheet, CEL_DECOY_TABLE_AT + 8, CEL_DECOY_STREAM_AT);
    cel_u32(sheet, CEL_DECOY_TABLE_AT + 12, CEL_DECOY_STREAM_AT);
    cel_fill_stream(sheet, CEL_DECOY_STREAM_AT, CEL_DECOY_PIXEL);
}

/* The global is put back afterwards so the drawer's one dependency does not
   leak into whatever test file runs next. */
static void cel_blit(unsigned char *sheet, int pitch, int sprite_index)
{
    unsigned char *previous_sheet;

    memset(cel_surface, GUARD, CEL_SURFACE_BYTES);
    previous_sheet = data_fdps_command_sprite_sheet_ptr;
    data_fdps_command_sprite_sheet_ptr = sheet;
    fdps_blit_command_sprite(cel_surface + CEL_DEST_Y * pitch + CEL_DEST_X,
                             pitch, sprite_index);
    data_fdps_command_sprite_sheet_ptr = previous_sheet;
}

static int cel_painted(void)
{
    int index;
    int painted;

    painted = 0;
    for (index = 0; index < CEL_SURFACE_BYTES; index++) {
        if (cel_surface[index] != GUARD) {
            painted++;
        }
    }
    return painted;
}

/* How many of the 550 bytes of the 25x22 rectangle do not hold `pixel`. */
static int cel_wrong_pixels(int pitch, unsigned char pixel)
{
    int row;
    int column;
    int wrong;

    wrong = 0;
    for (row = 0; row < CEL_SPRITE_H; row++) {
        for (column = 0; column < CEL_SPRITE_W; column++) {
            if (cel_surface[(CEL_DEST_Y + row) * pitch + CEL_DEST_X + column]
                != pixel) {
                wrong++;
            }
        }
    }
    return wrong;
}

static int cel_bytes_holding(unsigned char pixel)
{
    int index;
    int found;

    found = 0;
    for (index = 0; index < CEL_SURFACE_BYTES; index++) {
        if (cel_surface[index] == pixel) {
            found++;
        }
    }
    return found;
}

/* The sprite is written at the pointer it was handed, 25 columns by 22 rows of
   it and not one byte more.  The four edge guards are the size assertion: the
   column at CEL_DEST_X + 25 and the row at CEL_DEST_Y + 22 are the first ones
   outside the rectangle the two pushed immediates describe, and the row above
   and the column to the left are outside it the other way. */
static void the_command_sprite_lands_at_the_destination_pointer(void)
{
    cel_stage_sheet(cel_sheet, CEL_PIXEL0);
    cel_blit(cel_sheet, CEL_PITCH, 0);
    CHECK_EQ(cel_wrong_pixels(CEL_PITCH, CEL_PIXEL0), 0);
    CHECK_EQ(cel_painted(), CEL_SPRITE_W * CEL_SPRITE_H);
    CHECK_EQ(cel_surface[CEL_DEST_Y * CEL_PITCH + CEL_DEST_X - 1], GUARD);
    CHECK_EQ(cel_surface[CEL_DEST_Y * CEL_PITCH + CEL_DEST_X + CEL_SPRITE_W],
             GUARD);
    CHECK_EQ(cel_surface[(CEL_DEST_Y - 1) * CEL_PITCH + CEL_DEST_X], GUARD);
    CHECK_EQ(cel_surface[(CEL_DEST_Y + CEL_SPRITE_H) * CEL_PITCH + CEL_DEST_X],
             GUARD);
}

/* Entry 2 of the table, not entry 0 and not the table's own address: the index
   is scaled by four and the entry it selects is rebased on the sheet. */
static void the_command_sprite_index_selects_the_table_entry(void)
{
    cel_stage_sheet(cel_sheet, CEL_PIXEL0);
    cel_blit(cel_sheet, CEL_PITCH, 2);
    CHECK_EQ(cel_wrong_pixels(CEL_PITCH, CEL_PIXEL2), 0);
    CHECK_EQ(cel_bytes_holding(CEL_PIXEL0), 0);
}

/* The header's table-position field points at the decoy table, whose every
   entry names the decoy stream.  Reading that field instead of hardwiring 0x0f
   would paint the decoy colour over the whole rectangle. */
static void the_command_offset_table_is_read_at_fifteen(void)
{
    cel_stage_sheet(cel_sheet, CEL_PIXEL0);
    cel_blit(cel_sheet, CEL_PITCH, 0);
    CHECK_EQ(cel_wrong_pixels(CEL_PITCH, CEL_PIXEL0), 0);
    CHECK_EQ(cel_bytes_holding(CEL_DECOY_PIXEL), 0);
}

/* Two sheets differing only in sprite 0's colour: the one the global points at
   is the one that gets drawn. */
static void the_command_sheet_comes_from_the_global(void)
{
    cel_stage_sheet(cel_sheet, CEL_PIXEL0);
    cel_stage_sheet(cel_sheet_alt, CEL_PIXEL0_ALT);
    cel_blit(cel_sheet_alt, CEL_PITCH, 0);
    CHECK_EQ(cel_wrong_pixels(CEL_PITCH, CEL_PIXEL0_ALT), 0);
    CHECK_EQ(cel_bytes_holding(CEL_PIXEL0), 0);
}

/* The three values the dispatcher publishes before handing over to a kernel:
   the width and the pitch as it was given them, and the row counter the
   pass-through kernel has counted down to zero over the 22 rows it drew. */
static void the_command_sprite_size_reaches_the_dispatcher(void)
{
    cel_stage_sheet(cel_sheet, CEL_PIXEL0);
    cel_blit(cel_sheet, CEL_PITCH, 0);
    CHECK_EQ(data_fdps_graphics_rle_blit_src_width, CEL_SPRITE_W);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_pitch, CEL_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The same surface walked at a stride of 30 instead of 32.  A drawer that had
   folded the caller's pitch into a constant would leave the rows 32 apart, and
   every byte after the first row would be in the wrong place. */
static void the_command_pitch_is_the_destination_row_stride(void)
{
    cel_stage_sheet(cel_sheet, CEL_PIXEL0);
    cel_blit(cel_sheet, CEL_NARROW_PITCH, 0);
    CHECK_EQ(cel_wrong_pixels(CEL_NARROW_PITCH, CEL_PIXEL0), 0);
    CHECK_EQ(cel_painted(), CEL_SPRITE_W * CEL_SPRITE_H);
}

/* Blit mode 0 is a constant in the push sequence, so the sprite goes through
   the pass-through kernel: the skipped columns keep the guard, the filled ones
   take the fill byte and the literal ones take their five stream bytes exactly
   as the stream holds them.  The painted count is 20 columns by 22 rows, the
   five skipped columns having written nothing. */
static void mode_zero_passes_the_command_stream_through_unchanged(void)
{
    int row;
    int column;
    int wrong_skipped;
    int wrong_filled;
    int wrong_literal;
    int at;

    cel_stage_sheet(cel_sheet, CEL_PIXEL0);
    cel_blit(cel_sheet, CEL_PITCH, 1);

    wrong_skipped = 0;
    wrong_filled = 0;
    wrong_literal = 0;
    for (row = 0; row < CEL_SPRITE_H; row++) {
        at = (CEL_DEST_Y + row) * CEL_PITCH + CEL_DEST_X;
        for (column = 0; column < CEL_SKIP_COLUMNS; column++) {
            if (cel_surface[at + column] != GUARD) {
                wrong_skipped++;
            }
        }
        for (column = 0; column < CEL_FILL_COLUMNS; column++) {
            if (cel_surface[at + CEL_SKIP_COLUMNS + column] != CEL_MIXED_FILL) {
                wrong_filled++;
            }
        }
        for (column = 0; column < CEL_LITERAL_COLUMNS; column++) {
            if (cel_surface[at + CEL_SKIP_COLUMNS + CEL_FILL_COLUMNS + column]
                != (unsigned char) (CEL_LITERAL_FIRST + column)) {
                wrong_literal++;
            }
        }
    }

    CHECK_EQ(wrong_skipped, 0);
    CHECK_EQ(wrong_filled, 0);
    CHECK_EQ(wrong_literal, 0);
    CHECK_EQ(cel_painted(),
             (CEL_FILL_COLUMNS + CEL_LITERAL_COLUMNS) * CEL_SPRITE_H);
}

/* The sheet declares three sprites and index 3 is drawn anyway: there is no
   compare against the count anywhere in the body. */
static void the_command_sprite_index_is_not_range_checked(void)
{
    cel_stage_sheet(cel_sheet, CEL_PIXEL0);
    cel_blit(cel_sheet, CEL_PITCH, CEL_SPRITE_COUNT);
    CHECK_EQ(cel_wrong_pixels(CEL_PITCH, CEL_PIXEL3), 0);
    CHECK_EQ(cel_painted(), CEL_SPRITE_W * CEL_SPRITE_H);
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
    RUN_TEST(layer_paints_the_whole_grid);
    RUN_TEST(every_row_starts_back_at_the_requests_x);
    RUN_TEST(the_grid_origin_is_the_requests_x_and_y);
    RUN_TEST(the_callers_request_is_left_alone);
    RUN_TEST(negative_tilemap_index_draws_nothing);
    RUN_TEST(tilemap_index_at_the_count_draws_nothing);
    RUN_TEST(a_negative_row_count_draws_nothing);
    RUN_TEST(a_dropped_cell_does_not_stop_the_walk);
    RUN_TEST(the_copy_carries_the_surface_and_the_mode);
    RUN_TEST(composite_draws_every_layer_at_its_own_offset);
    RUN_TEST(layer_offsets_are_signed);
    RUN_TEST(a_negative_layer_count_draws_nothing);
    RUN_TEST(a_negative_frame_index_draws_nothing);
    RUN_TEST(a_frame_index_at_the_count_draws_nothing);
    RUN_TEST(the_frame_table_entry_is_rebased_on_the_image);
    RUN_TEST(the_callers_request_survives_the_frame);
    RUN_TEST(a_translucent_layer_blends_and_the_next_opaque_one_does_not);
    RUN_TEST(the_blend_level_is_sixteen_minus_the_records_field);
    RUN_TEST(an_unhandled_blend_flag_inherits_the_previous_layer);
    RUN_TEST(the_callers_blit_mode_suppresses_the_blend_setup);
    RUN_TEST(the_frames_sound_plays_only_when_the_flag_is_set);
    RUN_TEST(a_frame_with_no_sound_plays_nothing_and_still_draws);
    RUN_TEST(the_command_sprite_lands_at_the_destination_pointer);
    RUN_TEST(the_command_sprite_index_selects_the_table_entry);
    RUN_TEST(the_command_offset_table_is_read_at_fifteen);
    RUN_TEST(the_command_sheet_comes_from_the_global);
    RUN_TEST(the_command_sprite_size_reaches_the_dispatcher);
    RUN_TEST(the_command_pitch_is_the_destination_row_stride);
    RUN_TEST(mode_zero_passes_the_command_stream_through_unchanged);
    RUN_TEST(the_command_sprite_index_is_not_range_checked);
}
