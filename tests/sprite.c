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

#define IMAGE_SIZE 0x80

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
}
