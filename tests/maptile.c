/* tests/maptile.c -- cover for src/maptile.c.
 *
 * Every expected value below is read off the assembly at 0002ba00 --
 * MOVSX word ptr [EAX+7] for the width, IMUL/ADD/ADD EAX,EAX for the cell
 * index, MOVSX word ptr [EAX] for the tile id, LEA EDX,[EDX*0x4 + 0x0] and
 * ADD EAX,0x11 for the attribute row, the four byte stores from +2, +0, +1 and
 * +3, MOV AL,byte ptr [EAX+1] on the grid cell, and MOV AL,byte ptr [EAX+0x10]
 * / XOR AH,AH on the event layer -- and from the record layouts ticket 17
 * settled.  None of them is read off the emitted C.  The cases for
 * fdps_map_apply_triggered_cell_changes come the same way off 0002e910: the
 * two IMULs that pick which width indexes which layer, the two equality tests
 * on the masked attribute byte, INC word ptr on the tile id and the byte store
 * that clears the event code.
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

/* fdps_map_set_pending_tile_event takes no input of its own: it calls
   fdps_map_load_tile_info and then works entirely off the tile-info block that
   call leaves behind, so the four layers above have to be staged for it too and
   a cell is chosen by what the staging puts there.

   Cell (3, 2) is the one used below.  With terrain width 5 its tile id is
   2 * 5 + 3 = 13 and its attribute flags are 0x1d, which has both of the 0x60
   bits clear so the searchable-cell gate lets it through; with the event
   layer's own width of 3 its event code is 0x60 + (2 * 3 + 3) = 0x69.

   Every offset into the staged MAP%02d.DAT block below is written as a literal,
   never as base + code * 2 + 0x31 evaluated by the test.  An entry for event
   code 0x69 is at 0x69 * 2 + 0x31 = 0x103 and 0x104, and writing 0x103 rather
   than recomputing the formula is what makes the offset an expectation instead
   of a restatement of the code under test.

   The block is filled with 0xaa first, so an entry the function should not have
   reached reads back handler 0xaa and occasion 0xaa: not the 0xff sentinel, and
   matching neither of the two trigger kinds. */
#define STAGE_MAP_DAT_BYTES 0x300

/* 0x69's entry, and the two neighbours a wrong index would land on. */
#define ENTRY_69_HANDLER  0x103
#define ENTRY_69_TRIGGER  0x104
#define ENTRY_01_HANDLER  0x33
#define ENTRY_01_TRIGGER  0x34
#define ENTRY_00_HANDLER  0x31
#define ENTRY_00_TRIGGER  0x32
#define ENTRY_FF_HANDLER  0x22f
#define ENTRY_FF_TRIGGER  0x230

/* Row 13 of the staged attribute table, whose byte 0 is the flags the
   searchable-cell gate reads: 0x11 + 13 * 4. */
#define ATTR_ROW_13_FLAGS (ATTR_ROWS_AT + 13 * 4)

/* Cell 9 of the staged event layer -- the cell (3, 2) reaches at width 3. */
#define EVENT_CELL_9 (EVENT_CELLS_AT + 9)

/* The value the pending slot is seeded with.  Not 0xff, which is what a real
   caller seeds it with: 0xff is also the entry's "no handler" byte, so seeding
   with something the function can never write is what tells "left alone" apart
   from "wrote the sentinel". */
#define PENDING_SEED 0x5a

static unsigned char stage_map_dat[STAGE_MAP_DAT_BYTES];

/* Stage the four layers, hang an all-0xaa MAP%02d.DAT block off the chapter
   pointer with one live entry for cell (3, 2) -- handler 7, occasion 1 -- and
   seed the pending slot. */
static void stage_events(void)
{
    int i;

    stage();

    for (i = 0; i < STAGE_MAP_DAT_BYTES; i++) {
        stage_map_dat[i] = 0xaa;
    }
    stage_map_dat[ENTRY_69_HANDLER] = 0x07;
    stage_map_dat[ENTRY_69_TRIGGER] = 0x01;

    data_fdps_tile_event_data_table_ptr = stage_map_dat;
    data_fdps_chapter_pending_event_idx = PENDING_SEED;
}

/* Byte 1 of the entry is matched against trigger_kind for equality -- MOV
   EAX,[EBP-0x4] / CMP EAX,[EBP+0x1c] / JZ at 0002e0a1 -- and byte 0 is what
   lands in the slot.  Both trigger kinds the image passes are exercised, and
   the mismatch leaves the slot exactly as the caller seeded it: the function
   has no "clear it" path. */
static void stores_handler_when_occasion_matches(void)
{
    stage_events();
    fdps_map_set_pending_tile_event(3, 2, 1);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x07);

    stage_events();
    fdps_map_set_pending_tile_event(3, 2, 0);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, PENDING_SEED);

    stage_events();
    stage_map_dat[ENTRY_69_HANDLER] = 0x0c;
    stage_map_dat[ENTRY_69_TRIGGER] = 0x00;
    fdps_map_set_pending_tile_event(3, 2, 0);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x0c);
}

/* CMP dword ptr [EBP + -0x8],0xff / JZ at 0002e098: handler 0xff means the cell
   has no handler and the slot is not touched, even though the occasion matches
   and would otherwise fire. */
static void ignores_entry_whose_handler_is_ff(void)
{
    stage_events();
    stage_map_dat[ENTRY_69_HANDLER] = 0xff;
    stage_map_dat[ENTRY_69_TRIGGER] = 0x01;

    fdps_map_set_pending_tile_event(3, 2, 1);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, PENDING_SEED);
}

/* AND AL,0x60 at 0002e051 rejects the cell if EITHER bit is set, so all three
   of 0x20, 0x40 and 0x60 suppress the event.  The fourth case is the other
   half of the same instruction: 0x9f sets every bit the mask does not cover,
   and the event still fires, so the gate is two bits and not a comparison
   against a whole attribute value. */
static void searchable_cell_bits_suppress_the_event(void)
{
    stage_events();
    stage_attr[ATTR_ROW_13_FLAGS] = 0x20;
    fdps_map_set_pending_tile_event(3, 2, 1);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, PENDING_SEED);

    stage_events();
    stage_attr[ATTR_ROW_13_FLAGS] = 0x40;
    fdps_map_set_pending_tile_event(3, 2, 1);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, PENDING_SEED);

    stage_events();
    stage_attr[ATTR_ROW_13_FLAGS] = 0x60;
    fdps_map_set_pending_tile_event(3, 2, 1);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, PENDING_SEED);

    stage_events();
    stage_attr[ATTR_ROW_13_FLAGS] = 0x9f;
    fdps_map_set_pending_tile_event(3, 2, 1);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x07);
}

/* CMP word ptr [0x00069d06],0x0 / JZ at 0002e05c: event code 0 is the "no
   event" sentinel and the table is never touched.  The entry the code-0
   arithmetic would reach, at 0 * 2 + 0x31, is loaded with a handler and an
   occasion that would fire if it were read. */
static void event_code_zero_reads_no_entry(void)
{
    stage_events();
    stage_event[EVENT_CELL_9] = 0x00;
    stage_map_dat[ENTRY_00_HANDLER] = 0x03;
    stage_map_dat[ENTRY_00_TRIGGER] = 0x01;

    fdps_map_set_pending_tile_event(3, 2, 1);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, PENDING_SEED);
}

/* The table begins at image offset 0x33 and is indexed by the event code minus
   one, which the original spells as code * 2 + 0x31.  Code 1 therefore reads
   0x33 and 0x34, not 0x31 and 0x32: both pairs are loaded here and only the
   one at 0x33 may be the one that lands in the slot.

   The second half runs the largest code the event layer can hold.  The layer's
   byte is zero-extended into a signed 16-bit global, so 0xff arrives as 255 and
   its entry is at 255 * 2 + 0x31 = 0x22f; read as -1 the entry would be at
   0x2f, in front of the table, and read as -1 scaled it would be off the block
   entirely. */
static void entry_is_indexed_from_code_minus_one(void)
{
    stage_events();
    stage_event[EVENT_CELL_9] = 0x01;
    stage_map_dat[ENTRY_00_HANDLER] = 0x11;
    stage_map_dat[ENTRY_00_TRIGGER] = 0x00;
    stage_map_dat[ENTRY_01_HANDLER] = 0x22;
    stage_map_dat[ENTRY_01_TRIGGER] = 0x00;

    fdps_map_set_pending_tile_event(3, 2, 0);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x22);

    stage_events();
    stage_event[EVENT_CELL_9] = 0xff;
    stage_map_dat[ENTRY_FF_HANDLER] = 0x33;
    stage_map_dat[ENTRY_FF_TRIGGER] = 0x00;

    fdps_map_set_pending_tile_event(3, 2, 0);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x33);
}

/* fdps_map_apply_triggered_cell_changes walks the whole map, so the staging
   below adds the one header field the cases above never needed -- the terrain
   layer's height at +9 -- and replaces the event codes with values the 32-entry
   trigger table can hold.  stage() leaves the header filled with 0xaa, which is
   -21846 as a height and would end the walk before its first row.

   The map is 5 wide and 3 high, so the cells walked are terrain 0 through 14,
   and the event layer keeps its own width of 3: cell (x, y) is terrain
   y * 5 + x and event y * 3 + x, and the two indices differ from (1, 1) on.
   Terrain cell c starts out holding tile id c, so the attribute row a cell
   reaches is row c and one row can be aimed at one cell.

   Event code at event index e is e + 1, never 0 and never above 12, and the
   trigger table is flagged at the codes named below.  Which cells that leaves
   acting, row by row, and why each of the others does not:

     (0,0) c0  e0 code1  flags 0x20  code 1 not flagged   -- no
     (1,0) c1  e1 code2  flags 0x2f  flagged              -- ACTS
     (2,0) c2  e2 code3  flags 0xff  flagged              -- ACTS
     (3,0) c3  e3 code4  flags 0x13  kind 0x00            -- no
     (4,0) c4  e4 code5  flags 0x14  kind 0x00            -- no
     (0,1) c5  e3 code4  flags 0x20  flagged              -- ACTS
     (1,1) c6  e4 code5  flags 0x20  flagged              -- ACTS
     (2,1) c7  e5 code6  flags 0x60  flagged              -- ACTS
     (3,1) c8  e6 code7  flags 0x40  kind 0x40            -- no
     (4,1) c9  e7 code8  flags 0x00  kind 0x00            -- no
     (0,2) c10 e6 code7  flags 0x1a  kind 0x00            -- no
     (1,2) c11 e7 code8  flags 0x1b  kind 0x00            -- no
     (2,2) c12 e8 code9  flags 0x1c  kind 0x00            -- no
     (3,2) c13 e9 code10 flags 0x1d  kind 0x00            -- no
     (4,2) c14 e10 code11 flags 0x1e kind 0x00            -- no

   Row 3 is outside the height and is loaded so that it would act if it were
   ever reached: terrain cell 15 has attribute row 15 and event index 9.

   0x2f and 0xff are there because the gate reads the 0x60 field and nothing
   else: both are kind 0x20 with every other bit of the byte set one way or the
   other. */
#define TRIG_WIDTH  5
#define TRIG_HEIGHT 3

/* Attribute row r, byte 0 -- the flags the gate reads. */
#define ATTR_FLAGS_OF(r) (ATTR_ROWS_AT + (r) * 4)

/* Terrain cell c's tile id, and event cell e's code. */
#define TILE_AT(c)  (*(short *) (stage_tile_map + TERRAIN_CELLS_AT + (c) * 2))
#define EVENT_AT(e) (stage_event[EVENT_CELLS_AT + (e)])

static void stage_triggers(void)
{
    int i;

    stage();

    *(short *) (stage_tile_map + 9) = (short) TRIG_HEIGHT;

    for (i = 0; i < STAGE_CELLS; i++) {
        EVENT_AT(i) = (unsigned char) (1 + i % 12);
    }

    for (i = 0; i < 32; i++) {
        data_fdps_map_cell_event_triggered_flags[i] = 0;
    }
    data_fdps_map_cell_event_triggered_flags[2] = 1;
    data_fdps_map_cell_event_triggered_flags[3] = 1;
    data_fdps_map_cell_event_triggered_flags[4] = 1;
    data_fdps_map_cell_event_triggered_flags[5] = 1;
    data_fdps_map_cell_event_triggered_flags[6] = 1;
    data_fdps_map_cell_event_triggered_flags[7] = 1;
    data_fdps_map_cell_event_triggered_flags[8] = 1;
    data_fdps_map_cell_event_triggered_flags[10] = 1;

    stage_attr[ATTR_FLAGS_OF(0)] = 0x20;
    stage_attr[ATTR_FLAGS_OF(1)] = 0x2f;
    stage_attr[ATTR_FLAGS_OF(2)] = 0xff;
    stage_attr[ATTR_FLAGS_OF(5)] = 0x20;
    stage_attr[ATTR_FLAGS_OF(6)] = 0x20;
    stage_attr[ATTR_FLAGS_OF(7)] = 0x60;
    stage_attr[ATTR_FLAGS_OF(8)] = 0x40;
    stage_attr[ATTR_FLAGS_OF(9)] = 0x00;
    stage_attr[ATTR_FLAGS_OF(15)] = 0x20;
}

/* The five acting cells get both stores: INC word ptr [EAX] at 0002e9d8 on the
   tile id, so cell c comes back holding c + 1, and MOV byte ptr [EAX+0x10],0x0
   at 0002e9ed on the event byte the cell reached. */
static void bumps_tile_and_clears_code_on_triggered_cells(void)
{
    stage_triggers();
    fdps_map_apply_triggered_cell_changes();

    CHECK_EQ(TILE_AT(1), 2);
    CHECK_EQ(TILE_AT(2), 3);
    CHECK_EQ(TILE_AT(5), 6);
    CHECK_EQ(TILE_AT(6), 7);
    CHECK_EQ(TILE_AT(7), 8);

    CHECK_EQ(EVENT_AT(1), 0);
    CHECK_EQ(EVENT_AT(2), 0);
    CHECK_EQ(EVENT_AT(3), 0);
    CHECK_EQ(EVENT_AT(4), 0);
    CHECK_EQ(EVENT_AT(5), 0);
}

/* The three ways a cell fails the gate, each with the other half of the
   condition satisfied so that only the tested half can be what stopped it.

   Kind 0x40 is the case the two equality tests at 0002e9a1 and 0002e9a7 exist
   for: it is flagged in the table and it is a non-zero 0x60 field, and it is
   still left alone.  Kind 0x00 is flagged too.  Cell (0,0) is kind 0x20 and
   fails only because code 1 is not flagged. */
static void leaves_cells_that_fail_the_gate_alone(void)
{
    stage_triggers();
    fdps_map_apply_triggered_cell_changes();

    CHECK_EQ(TILE_AT(8), 8);
    CHECK_EQ(EVENT_AT(6), 7);

    CHECK_EQ(TILE_AT(9), 9);
    CHECK_EQ(EVENT_AT(7), 8);

    CHECK_EQ(TILE_AT(0), 0);
    CHECK_EQ(EVENT_AT(0), 1);
}

/* Cell (1,1) is terrain cell 6 at the terrain width and event cell 4 at the
   event layer's own width, and each store must use the width of the layer it
   writes.  Both wrong pairings are visible: bumping at the event width would
   hit terrain cell 4, clearing at the terrain width would hit event cell 6, and
   both of those belong to cells that must come through unchanged. */
static void indexes_each_layer_with_its_own_width(void)
{
    stage_triggers();
    fdps_map_apply_triggered_cell_changes();

    CHECK_EQ(TILE_AT(6), 7);
    CHECK_EQ(TILE_AT(4), 4);

    CHECK_EQ(EVENT_AT(4), 0);
    CHECK_EQ(EVENT_AT(6), 7);
}

/* The walk is bounded by the terrain header's own width and height.

   Terrain cell 5 is (0,1) and it acts once, so it comes back holding 6.  An x
   loop that ran one column past the width would visit (5,0), which is the same
   terrain cell 5 -- attribute row 5 is kind 0x20 and event cell 5 carries the
   flagged code 6 -- and would leave it holding 7.

   Row 3 is past the height and is the row that would act if it were walked:
   terrain cell 15 would become 16 and event cell 9 would be cleared. */
static void walks_only_the_header_width_and_height(void)
{
    stage_triggers();
    fdps_map_apply_triggered_cell_changes();

    CHECK_EQ(TILE_AT(5), 6);

    CHECK_EQ(TILE_AT(15), 15);
    CHECK_EQ(EVENT_AT(9), 10);
}

/* INC word ptr [EAX] increments two bytes and not four.  Terrain cell 2 acts,
   and it is loaded with 0xffff first: a tile id of -1 reaches attribute row -1,
   four bytes of the 0xaa header fill, whose byte 0 is kind 0x20, so the cell
   still passes the gate.  The word wraps to 0 and cell 3, which does not act,
   keeps its id; a 32-bit increment would carry into it and leave 4 there. */
static void increments_the_tile_id_as_a_16_bit_word(void)
{
    stage_triggers();
    TILE_AT(2) = (short) -1;

    fdps_map_apply_triggered_cell_changes();

    CHECK_EQ(TILE_AT(2), 0);
    CHECK_EQ(TILE_AT(3), 3);
}

void run_maptile_tests(void)
{
    RUN_TEST(reads_one_cell_from_each_layer);
    RUN_TEST(indexes_row_major);
    RUN_TEST(zero_extends_event_code);
    RUN_TEST(scales_tile_id_signed);
    RUN_TEST(reads_layer_width_signed);
    RUN_TEST(stores_handler_when_occasion_matches);
    RUN_TEST(ignores_entry_whose_handler_is_ff);
    RUN_TEST(searchable_cell_bits_suppress_the_event);
    RUN_TEST(event_code_zero_reads_no_entry);
    RUN_TEST(entry_is_indexed_from_code_minus_one);
    RUN_TEST(bumps_tile_and_clears_code_on_triggered_cells);
    RUN_TEST(leaves_cells_that_fail_the_gate_alone);
    RUN_TEST(indexes_each_layer_with_its_own_width);
    RUN_TEST(walks_only_the_header_width_and_height);
    RUN_TEST(increments_the_tile_id_as_a_16_bit_word);
}
