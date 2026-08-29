/* tests/mapai.c -- cover for src/mapai.c.
 *
 * Every expected value below is read off the assembly at 00013e10 -- MOVSX
 * word ptr [EAX] and MOVSX word ptr [EAX+2] on the movement grid pointer for
 * the two bounds, the JL loop tests, PUSH y / PUSH x into
 * fdps_map_load_tile_info, AND AL,0x60 / CMP EAX,0x20 on the attribute byte,
 * MOVSX word ptr [0x00069d06] / CMP EAX,[EBP+0x14] on the event code, the two
 * byte stores through [EBP+0x18], and the 0 / -1 the two exits load into
 * [EBP-4] -- and from the record layouts ticket 17 settled.  None of them is
 * read off the emitted C.
 *
 * The map is staged here rather than read from a game file: the function takes
 * its entire input from the four layer pointers, and the scan reaches its own
 * decision only through the globals fdps_map_load_tile_info republishes, so
 * pointing the pointers at local arrays is the only way to reach the body.
 * The callee is the real src/maptile.c one, so what a cell "is" travels the
 * whole way it does in the game: tile id -> attribute row -> flags byte.
 * Nothing below asserts what any global holds on its own; ticket 23 owns that.
 *
 * The three staged layers are all STAGE_W wide on purpose here, which is the
 * one place this file leans on tests/maptile.c rather than repeating it: which
 * width indexes which layer is that function's contract and is asserted there.
 * What is this function's contract is where the SCAN BOUNDS come from, and the
 * two tests that move the grid header away from the layer widths pin it.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "maptile.h"
#include "mapai.h"

#define STAGE_W 4
#define STAGE_H 3
#define STAGE_CELLS 32
#define STAGE_ATTR_ROWS 32

#define TERRAIN_CELLS_AT 0x0b
#define ATTR_ROWS_AT     0x11
#define EVENT_CELLS_AT   0x10

/* The two attribute kinds the 0x60 field can carry that this scan has to tell
   apart, plus the third value that exists only to break the naive bit test. */
#define KIND_CHEST 0x20
#define KIND_BURIED 0x40
#define KIND_OTHER 0x60

static unsigned char stage_tile_map[TERRAIN_CELLS_AT + STAGE_CELLS * 2];
static unsigned char stage_attr[ATTR_ROWS_AT + STAGE_ATTR_ROWS * 4];
static unsigned char stage_grid[4 + STAGE_CELLS * 2];
static unsigned char stage_event[EVENT_CELLS_AT + STAGE_CELLS];

/* Three bytes and not two: the third is a sentinel that says the function
   wrote exactly the two the assembly's two byte stores name. */
static unsigned char found_xy[3];

/* Build a 4x3 map on which no cell is a chest and every event code is zero, so
   a test states its case by naming the few cells it cares about and nothing
   else can match by accident.

   The tile id at cell i is i, so attribute row i is cell i's row and put_cell
   can address a cell's attributes by its own index.

   Both layer headers are filled with 0xaa before their width is written at +7,
   so a build that took a width from any other header offset would read 0xaaaa
   and miss every expectation below rather than coincidentally agreeing. */
static void stage(void)
{
    int i;

    for (i = 0; i < TERRAIN_CELLS_AT; i++) {
        stage_tile_map[i] = 0xaa;
    }
    *(short *) (stage_tile_map + 7) = (short) STAGE_W;
    for (i = 0; i < STAGE_CELLS; i++) {
        *(short *) (stage_tile_map + TERRAIN_CELLS_AT + i * 2) = (short) i;
    }

    for (i = 0; i < ATTR_ROWS_AT; i++) {
        stage_attr[i] = 0xaa;
    }
    for (i = 0; i < STAGE_ATTR_ROWS * 4; i++) {
        stage_attr[ATTR_ROWS_AT + i] = 0x00;
    }

    *(short *) stage_grid = (short) STAGE_W;
    *(short *) (stage_grid + 2) = (short) STAGE_H;
    for (i = 0; i < STAGE_CELLS; i++) {
        stage_grid[4 + i * 2] = 0x00;
        stage_grid[4 + i * 2 + 1] = 0xff;
    }

    for (i = 0; i < EVENT_CELLS_AT; i++) {
        stage_event[i] = 0xaa;
    }
    *(short *) (stage_event + 7) = (short) STAGE_W;
    for (i = 0; i < STAGE_CELLS; i++) {
        stage_event[EVENT_CELLS_AT + i] = 0x00;
    }

    found_xy[0] = 0xee;
    found_xy[1] = 0xee;
    found_xy[2] = 0xee;

    data_fdps_scene_layer_tile_map_ptrs[0] = stage_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = stage_attr;
    data_fdps_battle_move_grid_ptr = stage_grid;
    data_fdps_map_cell_event_code_layer_ptr = stage_event;
}

/* Give cell (x, y) an attribute flags byte and an event code.  Cell index is
   y * STAGE_W + x in both layers because both are staged STAGE_W wide. */
static void put_cell(int x, int y, unsigned char attr_flags,
                     unsigned char event_code)
{
    int cell;

    cell = y * STAGE_W + x;
    stage_attr[ATTR_ROWS_AT + cell * 4] = attr_flags;
    stage_event[EVENT_CELLS_AT + cell] = event_code;
}

/* The plain hit.  0 is the success value -- the caller at 000102d0 gates on
   TEST EAX,EAX / JZ -- and the column goes to out_xy[0], the row to out_xy[1],
   which is the order of the two byte stores at 00013e9d and 00013ea5. */
static void finds_the_chest_and_returns_zero(void)
{
    stage();
    put_cell(2, 1, KIND_CHEST, 7);

    CHECK_EQ(fdps_map_find_chest_cell(7, found_xy), 0);
    CHECK_EQ(found_xy[0], 2);
    CHECK_EQ(found_xy[1], 1);
}

/* A chest whose code is not the one asked for is not a hit, and the miss exit
   at 00013eb5 writes nothing at all: the buffer keeps its sentinels. */
static void a_miss_returns_minus_one_and_writes_nothing(void)
{
    stage();
    put_cell(2, 1, KIND_CHEST, 7);

    CHECK_EQ(fdps_map_find_chest_cell(9, found_xy), -1);
    CHECK_EQ(found_xy[0], 0xee);
    CHECK_EQ(found_xy[1], 0xee);
}

/* Exactly two bytes are written.  MOV byte ptr [EDX] and MOV byte ptr [EDX+1]
   are byte stores, so the third byte of the caller's buffer must survive; a
   pair of word or int stores would flatten it. */
static void writes_two_bytes_and_no_third(void)
{
    stage();
    put_cell(0, 0, KIND_CHEST, 3);

    CHECK_EQ(fdps_map_find_chest_cell(3, found_xy), 0);
    CHECK_EQ(found_xy[0], 0);
    CHECK_EQ(found_xy[1], 0);
    CHECK_EQ(found_xy[2], 0xee);
}

/* Kind 0x60 is the case that separates the real test from the naive one.
   AND AL,0x60 / CMP EAX,0x20 rejects it; the obvious (attr & 0x20) would
   accept it, because 0x60 & 0x20 is not zero, and the AI would be sent to the
   wrong cell (rebuild_info/pitfalls.md). */
static void kind_0x60_is_not_a_chest(void)
{
    stage();
    put_cell(1, 0, KIND_OTHER, 5);

    CHECK_EQ(fdps_map_find_chest_cell(5, found_xy), -1);
}

/* Buried treasure is the openable kind the player digs up, and this scan is
   for the AI: widening the test to the 0x20/0x40 pair that the player-side
   cursor search accepts would send an actor to one. */
static void kind_0x40_buried_treasure_is_not_a_chest(void)
{
    stage();
    put_cell(2, 0, KIND_BURIED, 6);

    CHECK_EQ(fdps_map_find_chest_cell(6, found_xy), -1);
}

/* Only the 0x60 field decides.  0xa3 carries the chest kind with every other
   bit of the byte set, and AND AL,0x60 throws them away, so it is still a
   hit -- a test written as an equality on the whole byte would miss it. */
static void bits_outside_the_kind_field_are_ignored(void)
{
    stage();
    put_cell(3, 0, (unsigned char) (KIND_CHEST | 0x83), 8);

    CHECK_EQ(fdps_map_find_chest_cell(8, found_xy), 0);
    CHECK_EQ(found_xy[0], 3);
    CHECK_EQ(found_xy[1], 0);
}

/* y is the outer loop and x the inner: [EBP-0x10] is the slot compared against
   the height and it is the one that advances in the outer JMP at 00013eb3.
   With one candidate at (1, 0) and another at (0, 1), row-major order reaches
   (1, 0) first; the transposed nesting would reach (0, 1) first. */
static void scan_is_row_major_with_y_outermost(void)
{
    stage();
    put_cell(1, 0, KIND_CHEST, 4);
    put_cell(0, 1, KIND_CHEST, 4);

    CHECK_EQ(fdps_map_find_chest_cell(4, found_xy), 0);
    CHECK_EQ(found_xy[0], 1);
    CHECK_EQ(found_xy[1], 0);
}

/* Within a row the lowest column wins, and the function returns on the first
   hit rather than carrying on to the last: the hit path at 00013ea8 stores 0
   and jumps straight to the epilogue. */
static void first_hit_in_a_row_wins(void)
{
    stage();
    put_cell(1, 2, KIND_CHEST, 4);
    put_cell(3, 2, KIND_CHEST, 4);

    CHECK_EQ(fdps_map_find_chest_cell(4, found_xy), 0);
    CHECK_EQ(found_xy[0], 1);
    CHECK_EQ(found_xy[1], 2);
}

/* The row bound is the movement grid header's word at +2 and nothing else.
   With the header cut to two rows the chest on row 2 is out of the scan even
   though all three layers are still three rows long and the terrain layer's
   own header is untouched; move the chest into row 1 and the same map finds
   it, so the -1 is the bound and not a broken fixture. */
static void row_bound_comes_from_the_grid_header(void)
{
    stage();
    *(short *) (stage_grid + 2) = (short) 2;
    put_cell(1, 2, KIND_CHEST, 4);

    CHECK_EQ(fdps_map_find_chest_cell(4, found_xy), -1);

    put_cell(1, 1, KIND_CHEST, 4);
    CHECK_EQ(fdps_map_find_chest_cell(4, found_xy), 0);
    CHECK_EQ(found_xy[0], 1);
    CHECK_EQ(found_xy[1], 1);
}

/* The column bound is the same header's word at +0, read before the loops and
   not the terrain width fdps_map_load_tile_info indexes with: the layers stay
   four wide while the header says three, and column 3 drops out of the scan. */
static void column_bound_comes_from_the_grid_header(void)
{
    stage();
    *(short *) stage_grid = (short) 3;
    put_cell(3, 0, KIND_CHEST, 4);

    CHECK_EQ(fdps_map_find_chest_cell(4, found_xy), -1);

    put_cell(2, 0, KIND_CHEST, 4);
    CHECK_EQ(fdps_map_find_chest_cell(4, found_xy), 0);
    CHECK_EQ(found_xy[0], 2);
    CHECK_EQ(found_xy[1], 0);
}

/* Both header words are MOVSX and both loop tests are JL, the signed compare.
   A width word of 0xffff is -1, so the inner loop never runs and the chest at
   (0, 0) is never looked at; read unsigned it would be 65535 and the scan
   would walk off the end of every layer. */
static void a_negative_width_scans_nothing(void)
{
    stage();
    *(short *) stage_grid = (short) -1;
    put_cell(0, 0, KIND_CHEST, 4);

    CHECK_EQ(fdps_map_find_chest_cell(4, found_xy), -1);
}

/* The same for the height word: -1 rows means the outer loop never runs. */
static void a_negative_height_scans_nothing(void)
{
    stage();
    *(short *) (stage_grid + 2) = (short) -1;
    put_cell(0, 0, KIND_CHEST, 4);

    CHECK_EQ(fdps_map_find_chest_cell(4, found_xy), -1);
}

/* The event code travels as an unsigned byte all the way to the comparison:
   fdps_map_load_tile_info widens the cell byte with XOR AH,AH into the signed
   16-bit global, and this function sign-extends that global with MOVSX before
   CMP EAX,[EBP+0x14].  So a cell byte of 0xff has to be found by 255 and must
   NOT be found by -1, which is what it would compare as had either step
   carried the byte's sign. */
static void event_code_is_compared_as_an_unsigned_byte(void)
{
    stage();
    put_cell(2, 2, KIND_CHEST, 0xff);

    CHECK_EQ(fdps_map_find_chest_cell(255, found_xy), 0);
    CHECK_EQ(found_xy[0], 2);
    CHECK_EQ(found_xy[1], 2);

    stage();
    put_cell(2, 2, KIND_CHEST, 0xff);
    CHECK_EQ(fdps_map_find_chest_cell(-1, found_xy), -1);
}

void run_mapai_tests(void)
{
    RUN_TEST(finds_the_chest_and_returns_zero);
    RUN_TEST(a_miss_returns_minus_one_and_writes_nothing);
    RUN_TEST(writes_two_bytes_and_no_third);
    RUN_TEST(kind_0x60_is_not_a_chest);
    RUN_TEST(kind_0x40_buried_treasure_is_not_a_chest);
    RUN_TEST(bits_outside_the_kind_field_are_ignored);
    RUN_TEST(scan_is_row_major_with_y_outermost);
    RUN_TEST(first_hit_in_a_row_wins);
    RUN_TEST(row_bound_comes_from_the_grid_header);
    RUN_TEST(column_bound_comes_from_the_grid_header);
    RUN_TEST(a_negative_width_scans_nothing);
    RUN_TEST(a_negative_height_scans_nothing);
    RUN_TEST(event_code_is_compared_as_an_unsigned_byte);

    /* Put the globals back before leaving.  stage() points four of them at
       this file's own arrays and the runners share one process: a later unit
       that expects an unallocated map would inherit live pointers into another
       translation unit's fixture and pass or fail for the wrong reason. */
    data_fdps_scene_layer_tile_map_ptrs[0] = NULL;
    data_fdps_scene_layer_tile_attr_ptr[0] = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
}
