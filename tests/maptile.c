/* tests/maptile.c -- cover for src/maptile.c.
 *
 * Every expected value below is read off the assembly at 0002ba00 --
 * MOVSX word ptr [EAX+7] for the width, IMUL/ADD/ADD EAX,EAX for the cell
 * index, MOVSX word ptr [EAX] for the tile id, LEA EDX,[EDX*0x4 + 0x0] and
 * ADD EAX,0x11 for the attribute row, the four byte stores from +2, +0, +1 and
 * +3, MOV AL,byte ptr [EAX+1] on the grid cell, and MOV AL,byte ptr [EAX+0x10]
 * / XOR AH,AH on the event layer -- and from the record layouts ticket 17
 * settled.  None of them is read off the emitted C.
 *
 * The four blocks are staged here rather than read from a game file: the
 * function takes its entire input from the four layer pointers, so pointing
 * them at local arrays is the only way to reach the body.  Nothing below
 * asserts what any global holds on its own; ticket 23 owns that.
 *
 * The three staged widths are deliberately all different -- terrain 5, grid 7,
 * event layer 3 -- because two of this function's real contracts are which
 * width indexes which layer: the movement grid is indexed with the terrain
 * layer's width and never with the one in its own header, while the event
 * layer is indexed with its own.
 */
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "maptile.h"

#define STAGE_CELLS 32
#define STAGE_ATTR_ROWS 16

#define TERRAIN_CELLS_AT 0x0b
#define ATTR_ROWS_AT     0x11
#define EVENT_CELLS_AT   0x10

static unsigned char stage_tile_map[TERRAIN_CELLS_AT + STAGE_CELLS * 2];
static unsigned char stage_attr[ATTR_ROWS_AT + STAGE_ATTR_ROWS * 4];
static unsigned char stage_grid[4 + STAGE_CELLS * 2];
static unsigned char stage_event[EVENT_CELLS_AT + STAGE_CELLS];

/* Rebuild all four blocks in their standard shape and hang the layer pointers
   off them.

   Both headers are filled with 0xaa first and only then have their width
   written at +7, so a build that took the width from any other header offset
   would read 0xaaaa (-21846) and miss every expectation below rather than
   coincidentally agreeing.

   The cell payloads are distinct per index and distinct per layer -- tile id
   i, grid flags 0x90+i, grid marker 0x50+i, event code 0x60+i -- so one
   assertion says both that the right cell was reached and that the right byte
   of it was taken. */
static void stage(void)
{
    int i;

    for (i = 0; i < TERRAIN_CELLS_AT; i++) {
        stage_tile_map[i] = 0xaa;
    }
    *(short *) (stage_tile_map + 7) = (short) 5;
    for (i = 0; i < STAGE_CELLS; i++) {
        *(short *) (stage_tile_map + TERRAIN_CELLS_AT + i * 2) = (short) i;
    }

    for (i = 0; i < ATTR_ROWS_AT; i++) {
        stage_attr[i] = 0xaa;
    }
    for (i = 0; i < STAGE_ATTR_ROWS; i++) {
        stage_attr[ATTR_ROWS_AT + i * 4] = (unsigned char) (0x10 + i);
        stage_attr[ATTR_ROWS_AT + i * 4 + 1] = (unsigned char) (0x20 + i);
        stage_attr[ATTR_ROWS_AT + i * 4 + 2] = (unsigned char) (0x30 + i);
        stage_attr[ATTR_ROWS_AT + i * 4 + 3] = (unsigned char) (0x40 + i);
    }

    *(short *) stage_grid = (short) 7;
    *(short *) (stage_grid + 2) = (short) 4;
    for (i = 0; i < STAGE_CELLS; i++) {
        stage_grid[4 + i * 2] = (unsigned char) (0x90 + i);
        stage_grid[4 + i * 2 + 1] = (unsigned char) (0x50 + i);
    }

    for (i = 0; i < EVENT_CELLS_AT; i++) {
        stage_event[i] = 0xaa;
    }
    *(short *) (stage_event + 7) = (short) 3;
    for (i = 0; i < STAGE_CELLS; i++) {
        stage_event[EVENT_CELLS_AT + i] = (unsigned char) (0x60 + i);
    }

    data_fdps_scene_layer_tile_map_ptrs[0] = stage_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = stage_attr;
    data_fdps_battle_move_grid_ptr = stage_grid;
    data_fdps_map_cell_event_code_layer_ptr = stage_event;
}

/* One call fills all six destinations, and each one comes from a different
   place.  With terrain width 5 the cell is index 2 * 5 + 3 = 13, so the tile
   id is 13 and the attribute row is row 13: flags 0x1d, blend 0x2d, terrain
   0x3d, backdrop 0x4d, landing in the four globals the four byte stores name.

   The grid marker is byte 1 of grid cell 13 -- 0x5d, not the 0x9d in byte 0 --
   and cell 13 is where the TERRAIN width puts it; the grid's own header says
   7, which would have made it cell 17 and marker 0x61.

   The event code is at the event layer's own width: 2 * 3 + 3 = 9, so 0x69.
   Cell 13 of that layer holds 0x6d, so borrowing the terrain width there would
   be visible too. */
static void reads_one_cell_from_each_layer(void)
{
    stage();
    fdps_map_load_tile_info(3, 2);

    CHECK_EQ(data_fdps_map_tile_info_tile_id, 13);
    CHECK_EQ(data_fdps_map_current_tile_attr_flags, 0x1d);
    CHECK_EQ(data_fdps_map_current_tile_attr_reserved, 0x2d);
    CHECK_EQ(data_fdps_map_tile_terrain_type, 0x3d);
    CHECK_EQ(data_fdps_map_tile_combat_backdrop_id, 0x4d);
    CHECK_EQ(data_fdps_map_current_move_grid_marker, 0x5d);
    CHECK_EQ(data_fdps_map_current_cell_event_code, 0x69);
}

/* The index is y * width + x and not x * width + y: (1, 0) is cell 1 in every
   layer, where the transposed form would be cell 5 of the terrain and grid and
   cell 3 of the event layer.  Asserting a second cell also rules out a body
   that reached a fixed cell and happened to agree once. */
static void indexes_row_major(void)
{
    stage();
    fdps_map_load_tile_info(1, 0);

    CHECK_EQ(data_fdps_map_tile_info_tile_id, 1);
    CHECK_EQ(data_fdps_map_current_tile_attr_flags, 0x11);
    CHECK_EQ(data_fdps_map_tile_terrain_type, 0x31);
    CHECK_EQ(data_fdps_map_current_move_grid_marker, 0x51);
    CHECK_EQ(data_fdps_map_current_cell_event_code, 0x61);
}

/* XOR AH,AH at 0002bacc widens the event byte with zero and not with its sign,
   and the global it goes into is signed, so the two high byte values are the
   ones that tell the widenings apart: 0x80 must arrive as 128 and 0xff as 255,
   where sign extension would give -128 and -1. */
static void zero_extends_event_code(void)
{
    stage();
    stage_event[EVENT_CELLS_AT] = 0x80;
    fdps_map_load_tile_info(0, 0);
    CHECK_EQ(data_fdps_map_current_cell_event_code, 128);

    stage_event[EVENT_CELLS_AT] = 0xff;
    fdps_map_load_tile_info(0, 0);
    CHECK_EQ(data_fdps_map_current_cell_event_code, 255);
}

/* The tile id is MOVSX'd and then scaled by 4 into the attribute table, so a
   negative id steps backwards from the table's first row.  Id -1 puts the row
   at base + 0x11 - 4 = base + 0x0d, four bytes inside what the standard
   staging fills with 0xaa; they are overwritten here so each of the four
   destinations can be told apart.

   Read unsigned the id would be 65535 and the row would be 0x11 + 262140 bytes
   into nothing at all, so this is the assertion that pins the signedness. */
static void scales_tile_id_signed(void)
{
    stage();
    *(short *) (stage_tile_map + TERRAIN_CELLS_AT) = (short) -1;
    stage_attr[ATTR_ROWS_AT - 4] = 0x81;
    stage_attr[ATTR_ROWS_AT - 3] = 0x82;
    stage_attr[ATTR_ROWS_AT - 2] = 0x83;
    stage_attr[ATTR_ROWS_AT - 1] = 0x84;

    fdps_map_load_tile_info(0, 0);

    CHECK_EQ(data_fdps_map_tile_info_tile_id, -1);
    CHECK_EQ(data_fdps_map_current_tile_attr_flags, 0x81);
    CHECK_EQ(data_fdps_map_current_tile_attr_reserved, 0x82);
    CHECK_EQ(data_fdps_map_tile_terrain_type, 0x83);
    CHECK_EQ(data_fdps_map_tile_combat_backdrop_id, 0x84);
}

/* The terrain width is MOVSX'd too.  With width -1 the cell for (0, 1) is
   index -1, two bytes below the cell array and so inside the header, where a
   tile id of 4 is planted; row 4 of the attribute table is terrain 0x34.

   A build that read the width unsigned would take 65535 rows forward instead
   and land far outside the staged block, so its failure here may be a fault
   rather than a mismatch.  That is the same thing the original would do, and
   it is why the sign is worth an assertion of its own. */
static void reads_layer_width_signed(void)
{
    stage();
    *(short *) (stage_tile_map + 7) = (short) -1;
    *(short *) (stage_tile_map + TERRAIN_CELLS_AT - 2) = (short) 4;

    fdps_map_load_tile_info(0, 1);

    CHECK_EQ(data_fdps_map_tile_info_tile_id, 4);
    CHECK_EQ(data_fdps_map_tile_terrain_type, 0x34);
}

void run_maptile_tests(void)
{
    RUN_TEST(reads_one_cell_from_each_layer);
    RUN_TEST(indexes_row_major);
    RUN_TEST(zero_extends_event_code);
    RUN_TEST(scales_tile_id_signed);
    RUN_TEST(reads_layer_width_signed);
}
