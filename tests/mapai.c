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
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "keybd.h"
#include "mapdraw.h"
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


/* ---- fdps_map_actor_move_toward_nearest_reachable_opponent, 000126b0 -------
 *
 * This one is an orchestrator: everything it decides it decides by driving
 * src/movegrid.c, src/table.c, src/unit.c, src/mapcur.c and src/walk.c, so the
 * fixture below is the whole battle-time environment those need -- the terrain
 * layer, the attribute table, the movement grid, the cell-event layer, the
 * PROMAP.DAT class table and the map unit array -- and every case is driven end
 * to end and read back off the unit record, the cursor draw mode and the return
 * value.
 *
 * Expected values come from the assembly at 000126b0 -- MOV AL,[EDX] and
 * [EDX+0x1] at 000126d2 and 000126dc for the tile the fill starts from,
 * [EDX+0x20] at 000126e7 with no INC before the PUSH at 000126f5, PUSH 0x64 at
 * 00012701 for the allowance, PUSH 0x0 at 0001271d for the side filter and
 * MOV EAX,0x2 at 00012717 for the trace mode, CMP EAX,-0x1 / JNZ at 00012735,
 * the two equality tests at 00012760 and 00012768 with the JNZ / JZ pair, the
 * two stores to 0x00069cd0 at 0001276d and 000127a6, and TEST EAX,EAX / JZ at
 * 0001279b -- and by walking the documented behaviour of the callees over the
 * fixture by hand.  None of them is read off the emitted C.
 *
 * The animation really runs, and the fixture is the one tests/movegrid.c uses
 * for fdps_battle_move_unit_toward and for the same reasons:
 * data_fdps_input_last_scancode is parked on the skip-animation code 3, which
 * suppresses every frame src/walk.c would draw, and every attribute row carries
 * flags 0x60, which makes fdps_map_set_pending_tile_event return before it
 * reaches the tile-event table.  The map cursor is parked on the acting unit's
 * own tile, so fdps_map_cursor_move_to_unit takes its zero-delta early return
 * and no frame is rendered there either.
 */

#define AI_W 5
#define AI_H 5
#define AI_CELLS (AI_W * AI_H)
#define AI_ATTR_ROWS 32
#define AI_UNITS 4
#define AI_CLASS_ROWS 8
#define AI_CLASS_STRIDE 0x0a
#define AI_TILE_PX 24

/* The sentinel the cursor draw mode is parked on before each case: neither of
   the two values the function stores, so "untouched" and "stored" are told
   apart rather than assumed. */
#define AI_DRAW_MODE_SENTINEL 7

static unsigned char ai_tile_map[TERRAIN_CELLS_AT + AI_CELLS * 2];
static unsigned char ai_attr[ATTR_ROWS_AT + AI_ATTR_ROWS * 4];
static unsigned char ai_grid[4 + AI_CELLS * 2];
static unsigned char ai_event[EVENT_CELLS_AT + AI_CELLS];
static unsigned char ai_class[AI_CLASS_ROWS * AI_CLASS_STRIDE];
static struct fdps_unit_record ai_units[AI_UNITS];

/* A 5x5 map of distinct tile ids over uniform terrain type 0, a class table
   whose every row costs 1 to enter any terrain, an empty unit array and a grid
   in the state fdps_map_grid_reset leaves it in -- which is what this function
   inherits, since it floods without resetting first. */
static void ai_stage(void)
{
    int i;
    int terrain;

    for (i = 0; i < TERRAIN_CELLS_AT; i++) {
        ai_tile_map[i] = 0xaa;
    }
    *(short *) (ai_tile_map + 7) = (short) AI_W;
    *(short *) (ai_tile_map + 9) = (short) AI_H;
    for (i = 0; i < AI_CELLS; i++) {
        *(short *) (ai_tile_map + TERRAIN_CELLS_AT + i * 2) = (short) i;
    }

    for (i = 0; i < ATTR_ROWS_AT; i++) {
        ai_attr[i] = 0xaa;
    }
    for (i = 0; i < AI_ATTR_ROWS; i++) {
        ai_attr[ATTR_ROWS_AT + i * 4] = 0x60;
        ai_attr[ATTR_ROWS_AT + i * 4 + 1] = 0x00;
        ai_attr[ATTR_ROWS_AT + i * 4 + 2] = 0x00;
        ai_attr[ATTR_ROWS_AT + i * 4 + 3] = 0x00;
    }

    *(short *) ai_grid = (short) AI_W;
    *(short *) (ai_grid + 2) = (short) AI_H;
    for (i = 0; i < AI_CELLS; i++) {
        ai_grid[4 + i * 2] = 0x00;
        ai_grid[4 + i * 2 + 1] = 0xff;
    }

    for (i = 0; i < EVENT_CELLS_AT; i++) {
        ai_event[i] = 0xaa;
    }
    *(short *) (ai_event + 7) = (short) AI_W;
    for (i = 0; i < AI_CELLS; i++) {
        ai_event[EVENT_CELLS_AT + i] = 0x00;
    }

    for (i = 0; i < AI_CLASS_ROWS * AI_CLASS_STRIDE; i++) {
        ai_class[i] = 0x00;
    }
    for (i = 0; i < AI_CLASS_ROWS; i++) {
        for (terrain = 0; terrain < 8; terrain++) {
            ai_class[i * AI_CLASS_STRIDE + terrain] = 1;
        }
    }

    for (i = 0; i < (int) sizeof(ai_units); i++) {
        ((unsigned char *) ai_units)[i] = 0x00;
    }

    data_fdps_scene_layer_tile_map_ptrs[0] = ai_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ai_attr;
    data_fdps_battle_move_grid_ptr = ai_grid;
    data_fdps_map_cell_event_code_layer_ptr = ai_event;
    data_fdps_class_table_ptr = ai_class;
    data_fdps_map_unit_array_ptr = (unsigned char *) ai_units;
    data_fdps_map_unit_count = 0;

    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_input_last_scancode = 3;
    data_fdps_map_cursor_draw_mode = AI_DRAW_MODE_SENTINEL;
}

/* Put one unit in the array.  Slot 0 is the actor in every case below, and the
   cursor is parked on its tile so that the cursor walk has nothing to do. */
static void ai_place(int slot, int x, int y, int side, int clazz, int move)
{
    ai_units[slot].pos_x = (unsigned char) x;
    ai_units[slot].pos_y = (unsigned char) y;
    ai_units[slot].side = (unsigned char) side;
    ai_units[slot].clazz = (unsigned char) clazz;
    ai_units[slot].move = (unsigned char) move;
    if (slot + 1 > data_fdps_map_unit_count) {
        data_fdps_map_unit_count = slot + 1;
    }
    if (slot == 0) {
        data_fdps_map_cursor_world_x = x * AI_TILE_PX;
        data_fdps_map_cursor_world_y = y * AI_TILE_PX;
    }
}

static void ai_set_cost(int row, int terrain_type, int cost)
{
    ai_class[row * AI_CLASS_STRIDE + terrain_type] = (unsigned char) cost;
}

static int ai_marker(int x, int y)
{
    return (int) ai_grid[4 + (y * AI_W + x) * 2 + 1];
}

/* With the actor the only unit on the map and its own side byte 0, nothing
   passes the search's side filter, fdps_move_path_trace answers -1 and the arm
   at 0001273a is taken: fdps_map_grid_reset runs, 0 is returned and the cursor
   draw mode is never written, so the sentinel survives.  The reset is what the
   marker assertions read: the fill had reached every tile of this open map, and
   every cell is back on the 0xff sentinel. */
static void ai_no_candidate_returns_zero_and_resets_the_grid(void)
{
    int moved;

    ai_stage();
    ai_place(0, 2, 2, 0, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_reachable_opponent(0, 0);

    CHECK_EQ(moved, 0);
    CHECK_EQ((int) ai_units[0].pos_x, 2);
    CHECK_EQ((int) ai_units[0].pos_y, 2);
    CHECK_EQ(ai_marker(2, 2), 0xff);
    CHECK_EQ(ai_marker(0, 0), 0xff);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_DRAW_MODE_SENTINEL);
}

/* The side filter is the literal 0 pushed at 0001271d and not side_select.
   Mode 2 reads that 0 as "keep the units whose side byte is non-zero"
   (movegrid.h), so on the NPC phase -- side_select 1, an actor whose own side
   byte is 1 -- the actor itself passes the filter and wins with cost 0 on its
   own tile.  The two equality tests at 00012760 and 00012768 then both hold,
   the move branch is skipped, and the handler answers 0 having moved nothing
   even though a side-0 opponent is standing three tiles away.  Had side_select
   been forwarded, that opponent would have been the answer and the actor would
   have walked. */
static void ai_side_filter_is_hard_coded_not_side_select(void)
{
    int moved;

    ai_stage();
    ai_place(0, 1, 1, 1, 0, 3);
    ai_place(1, 4, 1, 0, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_reachable_opponent(0, 1);

    CHECK_EQ(moved, 0);
    CHECK_EQ((int) ai_units[0].pos_x, 1);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_DRAW_MODE_SENTINEL);
}

/* The ordinary case, driven the whole way through the move.  The actor is on
   side 0 and the only other unit is on side 1, so the search keeps it, its tile
   (4,1) is three steps of cost 1 away and the trace answers 3.  That tile is
   not the actor's, so the move branch runs: with an allowance of 2 the actor
   cannot reach the opponent's tile and fdps_battle_move_unit_toward retargets
   the request to the furthest affordable tile on the route, (3,1).
   TEST EAX,EAX / JZ at 0001279b then takes the store of 1.
   The draw mode ends on 1 and not on the sentinel it entered with: the store at
   000127a6 is a plain store, not a restore. */
static void ai_walks_toward_the_nearest_reachable_opponent(void)
{
    int moved;

    ai_stage();
    ai_place(0, 1, 1, 0, 0, 2);
    ai_place(1, 4, 1, 1, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_reachable_opponent(0, 0);

    CHECK_EQ(moved, 1);
    CHECK_EQ((int) ai_units[0].pos_x, 3);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
    CHECK_EQ(ai_marker(1, 1), 0xff);
}

/* The two coordinate tests at 00012760 and 00012768 are joined by JNZ then JZ,
   which is "different if EITHER differs".  Here the winning tile shares the
   actor's column and differs only in its row, and the actor still walks; code
   that required both to differ would decline the actor instead.

   The actor stops one tile short of the opponent, at (2,3) and not (2,4).  The
   tile handed over is the opponent's OWN tile, and every round inside
   fdps_battle_move_unit_toward but the relaxed retry marks zones of control
   first, which puts the 0x40 "may not be entered" bit on that tile
   (movegrid.h); the walk is retargeted to the last tile of the route that the
   real range still reaches. */
static void ai_a_target_on_the_same_column_still_moves(void)
{
    int moved;

    ai_stage();
    ai_place(0, 2, 2, 0, 0, 2);
    ai_place(1, 2, 4, 1, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_reachable_opponent(0, 0);

    CHECK_EQ(moved, 1);
    CHECK_EQ((int) ai_units[0].pos_x, 2);
    CHECK_EQ((int) ai_units[0].pos_y, 3);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
}

/* The class code goes to fdps_get_class_record RAW, with no INC between the
   load at 000126e7 and the push at 000126f5, so an actor of class 3 floods the
   map with ROW 3's terrain costs and not row 4's.  Row 3 is made impassable --
   a cost of 200 exceeds the allowance of 100 on the first step -- while row 4,
   the row every other caller of the accessor would have reached for, still
   costs 1.  The fill therefore never leaves the actor's tile, the opponent
   stands on an unreachable cell, the trace answers -1 and nothing moves.
   Making row 3 cheap again moves the same actor over the same map, so the 0 is
   the class row and not a broken fixture. */
static void ai_class_row_is_the_raw_code_with_no_bias(void)
{
    int moved;

    ai_stage();
    ai_set_cost(3, 0, 200);
    ai_place(0, 1, 1, 0, 3, 2);
    ai_place(1, 4, 1, 1, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_reachable_opponent(0, 0);

    CHECK_EQ(moved, 0);
    CHECK_EQ((int) ai_units[0].pos_x, 1);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_DRAW_MODE_SENTINEL);

    ai_stage();
    ai_set_cost(3, 0, 1);
    ai_place(0, 1, 1, 0, 3, 2);
    ai_place(1, 4, 1, 1, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_reachable_opponent(0, 0);

    CHECK_EQ(moved, 1);
    CHECK_EQ((int) ai_units[0].pos_x, 3);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
}

/* PUSH 0x64 at 00012701 is a flat 100 and not the actor's own movement
   allowance at record byte +0x3b.  Every tile costs 30 here and the opponent is
   three tiles away, so its cell costs 90: inside the 100 the search floods with
   and outside the 60 the actor can really pay.  The search therefore finds it,
   the move retargets to the two tiles the actor can afford, and the handler
   answers 1.  Had the fill been given the actor's allowance the opponent's cell
   would have held the unreachable sentinel and the handler would have answered
   0 without moving anything. */
static void ai_fill_allowance_is_a_flat_hundred(void)
{
    int moved;

    ai_stage();
    ai_set_cost(0, 0, 30);
    ai_set_cost(1, 0, 30);
    ai_place(0, 1, 1, 0, 0, 60);
    ai_place(1, 4, 1, 1, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_reachable_opponent(0, 0);

    CHECK_EQ(moved, 1);
    CHECK_EQ((int) ai_units[0].pos_x, 3);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
}

/* ---- fdps_map_actor_move_toward_nearest_opponent, 000127c0 ----------------
 *
 * The straight-line sibling of the handler above, driven over the same fixture
 * and for the same reasons: it decides nothing on its own that is visible from
 * outside, so every case below is read back off the unit record, the cursor
 * draw mode and the return value after the whole chain has run.
 *
 * Expected values come from the assembly at 000127c0 -- MOV [EBP-0x24],0xffff
 * and MOV [EBP-0x14],0xffffffff at 000127cc and 000127d3 for the two sentinels,
 * XOR EAX,EAX / MOV AL,[EDX] and [EDX+0x1] at 000127f0 and 000127fa for the
 * actor's tile, CMP EAX,[0x00060150] / JL at 0001280f for the scan bound, the
 * CMP [EBP+0x18],0x0 / CMP byte ptr [EAX+0x6],0x0 pairs at 00012833..0001284f
 * for the side filter, AND AL,0x1 at 00012876 for the retired bit, the two
 * CALL 0x0003d364 with ADD EBX,EAX between at 000128a2..000128bb for the
 * distance, CMP EAX,[EBP-0x24] / JGE at 000128c3 for the strict replacement,
 * CMP [EBP-0x14],-0x1 / JNZ at 000128df, the two equality tests at 000128f1 and
 * 000128f9 joined JNZ then JZ, the two stores to 0x00069cd0 at 000128fe and
 * 00012937, and TEST EAX,EAX / JZ at 0001292c -- and by walking the documented
 * behaviour of the callees over the fixture by hand.  None of them is read off
 * the emitted C.
 *
 * Where a case ends in a walk, the endpoint is the one the sibling's cases
 * above already establish for this fixture: the tile handed over is occupied,
 * so fdps_battle_move_unit_toward retargets to the furthest tile of the route
 * the actor's own allowance still reaches.
 */

/* Nothing on the map but the actor, whose own side byte is 0 while side_select
 * 0 keeps only the non-zero sides.  The sweep scores nobody, [EBP-0x14] is
 * still -1 at 000128df and the exit at 000128e5 loads 0 without ever reaching
 * the two stores to the draw mode, so the sentinel survives. */
static void sl_no_opposing_unit_returns_zero(void)
{
    int moved;

    ai_stage();
    ai_place(0, 2, 2, 0, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 0);

    CHECK_EQ(moved, 0);
    CHECK_EQ((int) ai_units[0].pos_x, 2);
    CHECK_EQ((int) ai_units[0].pos_y, 2);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_DRAW_MODE_SENTINEL);
}

/* side_select really is consulted here, unlike the sibling's hard-coded 0, and
 * it is consulted as a TRUTH VALUE.
 *
 * First half: an actor of side 1 driven with side_select 1.  The filter's
 * second arm keeps only side byte 0, so the actor is not a candidate and the
 * side-0 unit at (4,1) is; the walk runs and the draw mode ends on 1.
 *
 * Second half: the same two units with side_select 0.  Now the filter keeps
 * every non-zero side, which is the actor itself at distance 0.  It wins, the
 * two equality tests at 000128f1 and 000128f9 both hold, the move branch is
 * skipped entirely and the draw mode is never written.  Comparing side numbers
 * -- record->side != actor->side -- would have kept the side-0 unit here and
 * walked. */
static void sl_side_select_is_a_truth_value_not_a_side_number(void)
{
    int moved;

    ai_stage();
    ai_place(0, 1, 1, 1, 0, 3);
    ai_place(1, 4, 1, 0, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 1);

    CHECK_EQ(moved, 1);
    CHECK_EQ((int) ai_units[0].pos_x, 3);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);

    ai_stage();
    ai_place(0, 1, 1, 1, 0, 3);
    ai_place(1, 4, 1, 0, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 0);

    CHECK_EQ(moved, 0);
    CHECK_EQ((int) ai_units[0].pos_x, 1);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_DRAW_MODE_SENTINEL);
}

/* AND AL,0x1 and nothing wider.  With bit 0 set the only unit that passes the
 * side filter is dropped and the handler answers 0 without touching the draw
 * mode; with bit 7 set instead -- the acted-this-turn flag, which shares the
 * byte -- the same unit is still a candidate and the actor walks.  A truth test
 * on the whole flags byte would have dropped it in both halves. */
static void sl_only_flags_bit_zero_retires_a_candidate(void)
{
    int moved;

    ai_stage();
    ai_place(0, 1, 1, 0, 0, 2);
    ai_place(1, 4, 1, 1, 0, 3);
    ai_units[1].flags = 0x01;

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 0);

    CHECK_EQ(moved, 0);
    CHECK_EQ((int) ai_units[0].pos_x, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_DRAW_MODE_SENTINEL);

    ai_stage();
    ai_place(0, 1, 1, 0, 0, 2);
    ai_place(1, 4, 1, 1, 0, 3);
    ai_units[1].flags = 0x80;

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 0);

    CHECK_EQ(moved, 1);
    CHECK_EQ((int) ai_units[0].pos_x, 3);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
}

/* The winner is the nearest candidate and not the first one found.  Unit 1 sits
 * four tiles east, unit 2 two tiles north; CMP EAX,[EBP-0x24] / JGE at 000128c3
 * lets the later index displace the earlier because its distance is smaller, so
 * the actor walks north to (0,1).  Code that kept the first match would have
 * sent it east to (2,2) instead. */
static void sl_nearest_candidate_wins_not_the_first_found(void)
{
    int moved;

    ai_stage();
    ai_place(0, 0, 2, 0, 0, 2);
    ai_place(1, 4, 2, 1, 0, 3);
    ai_place(2, 0, 0, 1, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 0);

    CHECK_EQ(moved, 1);
    CHECK_EQ((int) ai_units[0].pos_x, 0);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
}

/* Two things at once, because one fixture pins both.
 *
 * The distance is abs(dx) + abs(dy) and not the larger of the two: unit 1 at
 * (4,2) is 4 + 0 away and unit 2 at (2,0) is 2 + 2, so the sums tie at 4 while
 * the diagonal metric would make unit 2 a clear winner at 2.
 *
 * And a tie keeps the candidate already held, because the replacement is
 * guarded by JGE -- strictly smaller.  So unit 1, the lower index, wins the tie
 * and the actor walks east to (2,2); had the tie gone to the later index, or
 * had the metric been the diagonal one, it would have gone north-east
 * instead. */
static void sl_distance_is_the_sum_and_a_tie_keeps_the_lower_index(void)
{
    int moved;

    ai_stage();
    ai_place(0, 0, 2, 0, 0, 2);
    ai_place(1, 4, 2, 1, 0, 3);
    ai_place(2, 2, 0, 1, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 0);

    CHECK_EQ(moved, 1);
    CHECK_EQ((int) ai_units[0].pos_x, 2);
    CHECK_EQ((int) ai_units[0].pos_y, 2);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
}

/* The equality guard at 000128f1 and 000128f9, isolated from the side filter.
 * Unit 1 stands on the actor's own tile and wins at distance 0, so the move
 * branch is skipped even though unit 2 two tiles north was a perfectly movable
 * target: the guard is on the WINNING tile and not on "was anything found".
 * The draw mode is never written, which is what tells this apart from a walk
 * that simply produced no steps. */
static void sl_a_winner_on_the_actors_own_tile_moves_nothing(void)
{
    int moved;

    ai_stage();
    ai_place(0, 2, 2, 0, 0, 2);
    ai_place(1, 2, 2, 1, 0, 3);
    ai_place(2, 2, 0, 1, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 0);

    CHECK_EQ(moved, 0);
    CHECK_EQ((int) ai_units[0].pos_x, 2);
    CHECK_EQ((int) ai_units[0].pos_y, 2);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_DRAW_MODE_SENTINEL);
}

/* The scan bound is data_fdps_map_unit_count and not the size of the array.
 * The opponent is staged in slot 1 and then the count is cut back to 1, so
 * CMP EAX,[0x00060150] / JL at 0001280f stops the sweep before it, and the same
 * map with the count left alone walks the actor.  Cutting the count is also
 * what proves the scan starts at index 0 and runs upward. */
static void sl_scan_stops_at_the_unit_count(void)
{
    int moved;

    ai_stage();
    ai_place(0, 1, 1, 0, 0, 2);
    ai_place(1, 4, 1, 1, 0, 3);
    data_fdps_map_unit_count = 1;

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 0);

    CHECK_EQ(moved, 0);
    CHECK_EQ((int) ai_units[0].pos_x, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_DRAW_MODE_SENTINEL);

    ai_stage();
    ai_place(0, 1, 1, 0, 0, 2);
    ai_place(1, 4, 1, 1, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 0);

    CHECK_EQ(moved, 1);
    CHECK_EQ((int) ai_units[0].pos_x, 3);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
}

/* Terrain never reaches this search, which is the whole difference from the
 * sibling handler.  There is no flood fill and no fdps_get_class_record between
 * 000127c0 and the move call at 00012924: the sweep reads bytes +0, +1, +5 and
 * +6 of each record and nothing else.
 *
 * The fixture is the one that stops the path-cost sibling dead -- class 3 with
 * row 3 of the class table costing 200, from
 * ai_class_row_is_the_raw_code_with_no_bias -- and on it that sibling answers 0
 * and leaves the draw mode on its sentinel.  This handler walks the actor to
 * (3,1) on the same map instead.
 *
 * The walk still happens because the move is the only thing here that touches
 * the class table, and it fetches the row with the +1 every caller but the
 * sibling applies (00011a27): a class-3 actor moves on row 4, which this
 * fixture leaves at cost 1 per tile.  So row 3 being impassable is invisible
 * from end to end, which is the point. */
static void sl_terrain_cost_does_not_reach_the_search(void)
{
    int moved;

    ai_stage();
    ai_set_cost(3, 0, 200);
    ai_place(0, 1, 1, 0, 3, 2);
    ai_place(1, 4, 1, 1, 0, 3);

    moved = fdps_map_actor_move_toward_nearest_opponent(0, 0);

    CHECK_EQ(moved, 1);
    CHECK_EQ((int) ai_units[0].pos_x, 3);
    CHECK_EQ((int) ai_units[0].pos_y, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
}

/* ---- fdps_map_actor_behavior_step, 00010010 -------------------------------
 *
 * Only the two paths that RETURN BEFORE THE TAIL are driven here, and the tail
 * is why.  Every one of the eleven behaviour arms falls into it, and it ends in
 * fdps_render_view_frame (mapdraw.h), which spins on the VGA retrace bit and
 * then holds the caller until data_fdps_timer_tick_counter moves -- and nothing
 * in a test process moves it, because no timer handler is installed.  So the
 * arms are not reachable from a unit test at all; what is reachable, and what
 * the three cases below pin, is the retired gate, the low-nibble mask and
 * behaviour 8's jump straight past the tail to the epilogue.
 *
 * All three therefore assert an ABSENCE, and the flags byte is what carries it:
 * bit 7 is set by fdps_battle_mark_unit_done (btlturn.h) in the tail and by
 * nothing else on any of these paths, so a flags byte that still reads what the
 * case staged is the evidence that the function returned where it should have.
 *
 * bs_guard_the_tail below is not part of any expectation.  It parks
 * data_fdps_view_frame_last_tick off the tick counter so that a build which
 * wrongly reaches the tail comes back and FAILS a check, instead of spinning in
 * the frame clock forever and taking the whole run with it.
 *
 * Expected values come from the assembly at 00010010 -- AND AL,0x1 /
 * CMP dword ptr [EBP-0x34],0x0 / JNZ 0x0001074e at 0001004b..00010059 for the
 * gate, AND AL,0xf at 00010065 for the mask, CMP dword ptr [EBP-0x14],0x8 /
 * JZ 0x0001074e at 000104c1 for the idle arm, and OR byte ptr [EAX+0x5],0x80
 * at 00010745 for the bit the tail would have set -- and from the record layout
 * ticket 17 settled.  None of them is read off the emitted C.
 */

static void bs_guard_the_tail(void)
{
    data_fdps_view_frame_last_tick = 1;
}

/* Bit 0 of the flags byte retires a unit, and the gate reads nothing else.
   Behaviour 0 is the busiest arm there is -- three handlers and then a rest --
   so an actor that comes back on its own tile with its own flags byte came
   back through the gate, and not out of an arm that happened to decline it.
   The grid marker is checked too: the first of those three handlers floods the
   whole map, and this fixture's marker sentinel would not survive it. */
static void bs_a_retired_actor_is_left_alone(void)
{
    ai_stage();
    bs_guard_the_tail();
    ai_place(0, 2, 2, 0, 0, 3);
    ai_units[0].flags = 0x01;
    ai_units[0].ai_behavior = 0x00;

    fdps_map_actor_behavior_step(0, 0);

    CHECK_EQ((int) ai_units[0].flags, 0x01);
    CHECK_EQ((int) ai_units[0].pos_x, 2);
    CHECK_EQ((int) ai_units[0].pos_y, 2);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_DRAW_MODE_SENTINEL);
    CHECK_EQ(ai_marker(2, 2), 0xff);
}

/* Behaviour 8 is live -- the gate let it through, the flags byte is clear --
   and it does nothing whatever, the tail included.  The flags byte still has to
   read 0: bit 7 is set in the tail, which the JZ at 000104c1 jumps over.  The
   neighbours of record byte 0x34 are staged with values that are not 8 so that
   the case says which byte it means: 0x35 and 0x36 carry a destination and 0x3d
   an event slot, and none of the three is the behaviour. */
static void bs_behavior_eight_does_nothing_and_skips_the_tail(void)
{
    ai_stage();
    bs_guard_the_tail();
    ai_place(0, 2, 2, 0, 0, 3);
    ai_units[0].flags = 0x00;
    ai_units[0].ai_behavior = 0x08;
    ai_units[0].ai_dest_x = 3;
    ai_units[0].ai_dest_y = 5;
    ai_units[0].event_slot = 1;

    fdps_map_actor_behavior_step(0, 0);

    CHECK_EQ((int) ai_units[0].flags, 0x00);
    CHECK_EQ((int) ai_units[0].pos_x, 2);
    CHECK_EQ((int) ai_units[0].pos_y, 2);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_DRAW_MODE_SENTINEL);
}

/* The behaviour is the low nibble of byte 0x34 and the high nibble is no part
   of it.  0xf8 and 0x28 both have to be read as 8, and the byte must come back
   holding what it was given: this path writes nothing, so a build that masked
   the byte in place rather than in a copy would show up here. */
static void bs_the_behavior_is_the_low_nibble_of_byte_0x34(void)
{
    ai_stage();
    bs_guard_the_tail();
    ai_place(0, 2, 2, 0, 0, 3);
    ai_units[0].flags = 0x00;
    ai_units[0].ai_behavior = 0xf8;

    fdps_map_actor_behavior_step(0, 0);

    CHECK_EQ((int) ai_units[0].flags, 0x00);
    CHECK_EQ((int) ai_units[0].ai_behavior, 0xf8);
    CHECK_EQ((int) ai_units[0].pos_x, 2);

    ai_stage();
    bs_guard_the_tail();
    ai_place(0, 2, 2, 0, 0, 3);
    ai_units[0].flags = 0x00;
    ai_units[0].ai_behavior = 0x28;

    fdps_map_actor_behavior_step(0, 0);

    CHECK_EQ((int) ai_units[0].flags, 0x00);
    CHECK_EQ((int) ai_units[0].ai_behavior, 0x28);
}

/* ---- fdps_map_actor_take_best_action, 00012c10 ----------------------------
 *
 * This one runs the three real scorers in src/aiscore.c and then dispatches to
 * src/aiact.c, of which only fdps_map_actor_move_and_attack is emitted:
 * fdps_map_actor_cast_chosen_spell and fdps_map_actor_use_item are still
 * zero-returning stubs with no side effects.  So which of the five arms was
 * taken still cannot be told apart from outside this call, and what every case
 * below reads back is the pair the function itself produces: the answer, and
 * data_fdps_map_cursor_draw_mode, which is written 0 on the dispatch path and
 * not touched at all on the other.  The three score globals are read back too,
 * because they are what the decision was taken on and they are what the
 * fixture has to have arranged for the case to mean anything.  What the attack
 * arm's own effects are is tests/aiact.c's business, not this file's.
 *
 * THE ATTACK ARM IS LIVE, so every case here goes through tba_act, which puts
 * the machine in the graphics mode and on a moving timer for the length of the
 * dispatch.  A case that reaches that arm really walks the cursor, composes
 * frames, shows the gauges and swings; without the timer the first presented
 * frame would never return.  That is also why the wrapper is used by the cases
 * that expect no arm to run: a dispatch that went to the wrong arm fails an
 * assertion there instead of hanging the suite.
 *
 * Every case parks all three score globals on TBA_SCORE_SENTINEL first, which
 * is 0x5a and so well above the threshold.  Each scorer zeroes its own global
 * on entry, so a build that dropped one of the three calls would leave 0x5a
 * standing there and dispatch when the case expects 0 -- which is why the
 * no-score case below is also the test that all three searches are run.
 *
 * Expected values come from the assembly at 00012c10 -- the three CALLs at
 * 00012c24, 00012c34 and 00012c44 each passed [EBP+0x14] and [EBP+0x18]
 * unchanged, the two fdps_get_unit_record calls at 00012c50 and 00012c61 with
 * the second handed [0x00063f74], AND AL,0x40 at 00012c72, the MOVSX pair at
 * 00012c7f and 00012c86 with SUB EDX,EAX, the three CMP ...,0x6 with JGE / JL
 * at 00012c8f..00012caa, the five comparison pairs at 00012cba, 00012ceb,
 * 00012d86, 00012dcc and 00012dfa, CMP [0x00063f90],0x12 / JGE at 00012d1b,
 * CMP EAX,[EBP-0x14] / JLE at 00012d30, and
 * MOV dword ptr [0x00069cd0],0x0 / MOV dword ptr [EBP-0x4],0x1 at 00012e26 --
 * and by walking the documented behaviour of the three scorers over the fixture
 * by hand.  None of them is read off the emitted C.
 */

#define TBA_W 4
#define TBA_H 4
#define TBA_CELLS 32
#define TBA_ATTR_ROWS 32
#define TBA_UNITS 4
#define TBA_ITEMS 4
#define TBA_SPELLS 8
#define TBA_CLASSES 4
#define TBA_CLASS_STRIDE 0x0a

/* The equipped bit of an inventory entry's flag byte, the one
   fdps_unit_find_equipped_slot tests, and the empty bit the one
   fdps_unit_item_count tests.  An entry whose flag byte is plain 0 is carried
   but not equipped: counted by the bag walk, invisible to the weapon search. */
#define TBA_EQUIPPED 0x40
#define TBA_SLOT_EMPTY 0x80

/* An ITEM.DAT type inside the weapon span 1..0x15, so the equipped-slot search
   accepts the entry as a weapon. */
#define TBA_WEAPON_TYPE 1

/* The two ITEM.DAT use effects fdps_score_targets_for_item has a walk for:
   0x0b restores HP and scores 0, 3 or 8 a target on how hurt it is, 0x1e does
   damage and scores 8 or 0x12. */
#define TBA_USE_EFFECT_HEAL 0x0b
#define TBA_USE_EFFECT_DAMAGE 0x1e

/* A use_target byte that is neither 0 nor 1.  On the enemy phase the item
   scorer replaces it with (byte == 0), so 3 becomes select_mode 0 and keeps
   side 0 -- the actor's own side here -- and on the NPC phase it goes through
   unchanged, where select_mode 3 keeps side 2 and nothing in the fixture is on
   it (aitarget.h). */
#define TBA_USE_TARGET_OWN_SIDE 3

/* What the three score globals hold going in: a value no search below could
   produce, and one well above the threshold so that a search which never ran
   would change the answer rather than hide behind a 0. */
#define TBA_SCORE_SENTINEL 0x5a

/* What data_fdps_map_cursor_draw_mode holds going in: neither 0, the value the
   dispatch path stores, nor 1, so "untouched" and "stored" are told apart. */
#define TBA_DRAW_SENTINEL 7

/* WHAT THE ATTACK ARM NOW NEEDS, AND WHY THIS FIXTURE CARRIES IT.
   fdps_map_actor_move_and_attack (aiact.h) is emitted, so a case whose scores
   send the dispatch down the attack arm plays a whole attack for real: the
   cursor walks, frames are composed and presented, the two HP gauges go up and
   a swing is animated.  Everything below from here to tba_act is what those
   need and nothing more -- a cursor kit for the overlay, a unit gauge sheet, a
   resident animation container, and a handler on IRQ0 without which the first
   presented frame never returns.  Nothing here changes what the searches
   score: aiscore.c reads none of it. */
#define TBA_TIMER_VECTOR 8
#define TBA_MODE_TEXT 0x03
#define TBA_MODE_320X200X256 0x13
#define TBA_TILE_PX 24

/* Portrait id 0x80 is the record that has no map sprite at all, so a composed
   frame returns before it reaches for the unit sprite cache this file does not
   stage (mapdraw.c).  The searches never read the byte. */
#define TBA_NO_MAP_SPRITE 0x80

/* The Cusor.cel kit: only the offset table at 0x0f is read, and
   fdps_blit_cursor_tile takes 24 by 24 from its own constants.  One flat fill
   run per row is the encoding src/rle.c decodes, a command byte of len - 1
   followed by the pixel. */
#define TBA_CEL_TABLE_AT 0x0f
#define TBA_CURSOR_STREAM_AT 0x40
#define TBA_CURSOR_BYTES (TBA_CURSOR_STREAM_AT + TBA_TILE_PX * 2)
#define TBA_CURSOR_COLOR 0x21

/* The spell arm sets the cursor overlay to the spell record's reach plus 2, so
   a reach of 0 asks for sprite 1 where the attack arm only ever asks for
   sprite 0.  Every entry the table has room for before the stream is filled
   in; an entry left at zero aims the decoder at the sheet's own header and
   reads a kilobyte past the block. */
#define TBA_CURSOR_ENTRIES 12

/* Number.cel, the sheet the popups queued during a spell cast are blitted
   from: the MISS word when the roll refuses, one digit a cell when it lands.
   fdps_cel_blit_sprite takes the piece's size out of the SHEET's header at
   +0x07 and +0x09 (src/sprite.c), so a zeroed header is not a smaller sheet
   but a decoder whose row counter runs under zero and writes 65535 pixels a
   row.  The table is staged past 0x36, the last glyph the MISS word uses. */
#define TBA_GLYPH_ENTRIES 0x40
#define TBA_GLYPH_W 6
#define TBA_GLYPH_ROWS 8
#define TBA_GLYPH_STREAM_AT (TBA_CEL_TABLE_AT + TBA_GLYPH_ENTRIES * 4)
#define TBA_GLYPH_BYTES (TBA_GLYPH_STREAM_AT + TBA_GLYPH_ROWS * 2)
#define TBA_GLYPH_COLOR 0x23

/* The unit gauge sheet: three 43 by 6 graphics 0x102 bytes apart. */
#define TBA_GAUGE_GRAPHIC_STRIDE 0x102
#define TBA_GAUGE_BYTES (TBA_GAUGE_GRAPHIC_STRIDE * 3)

/* The resident BaseAni container, in the shape src/vfs.c reads it, holding one
   member: the attack animation, whose .SAF header declares no frames, so
   fdps_play_attack_animation resolves it and composes nothing.  The name is
   stored upper case because the lookup folds only the query. */
#define TBA_VFS_TABLE_OFFSET_AT 5
#define TBA_VFS_COUNT_AT 7
#define TBA_VFS_TABLE_AT 35
#define TBA_VFS_ENTRY_BYTES 26
#define TBA_VFS_ENTRY_SIZE_AT 0x0d
#define TBA_VFS_ENTRY_START_AT 0x16
#define TBA_VFS_MEMBER_AT (TBA_VFS_TABLE_AT + TBA_VFS_ENTRY_BYTES)
#define TBA_SAF_BYTES 0x20
#define TBA_SAF_FRAME_COUNT_AT 0x0c
#define TBA_VFS_BYTES (TBA_VFS_MEMBER_AT + TBA_SAF_BYTES)
#define TBA_ANIMATION_MEMBER "EASYANI.SAF"

/* A chapter index the palette cycler's switch does not list, so the frames a
   dispatching case presents change no DAC entry. */
#define TBA_INERT_CHAPTER 1

/* Any char_id other than 0, so the attack scorer's protagonist weighting stays
   out of every expected value below. */
#define TBA_NOBODY 5

static unsigned char tba_tilemap[TERRAIN_CELLS_AT + TBA_CELLS * 2];
static unsigned char tba_attr[ATTR_ROWS_AT + TBA_ATTR_ROWS * 4];
static unsigned char tba_event[EVENT_CELLS_AT + TBA_CELLS];
static unsigned char tba_grid[4 + TBA_CELLS * 2];
static unsigned char tba_classes[TBA_CLASSES * TBA_CLASS_STRIDE];
static unsigned char tba_cursor_kit[TBA_CURSOR_BYTES];
static unsigned char tba_glyphs[TBA_GLYPH_BYTES];
static unsigned char tba_gauge_sheet[TBA_GAUGE_BYTES];
static unsigned char tba_vfs[TBA_VFS_BYTES];
static void (__interrupt __far *tba_saved_timer)();
static struct fdps_unit_record tba_units[TBA_UNITS];
static struct fdps_item_effect tba_items[TBA_ITEMS];
static struct fdps_spell_effect tba_spells[TBA_SPELLS];

static void tba_zero(unsigned char *block, int bytes)
{
    int i;

    for (i = 0; i < bytes; i++) {
        block[i] = 0;
    }
}

static void __interrupt __far tba_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(tba_saved_timer);
}

static void tba_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void tba_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* The three sheets the attack arm draws out of.  What they hold does not
   matter to any assertion here; that they are addressable does. */
static void tba_stage_sheets(void)
{
    int row;
    int offset;
    int entry;

    tba_zero(tba_cursor_kit, (int) sizeof(tba_cursor_kit));
    for (entry = 0; entry < TBA_CURSOR_ENTRIES; entry++) {
        tba_u32(tba_cursor_kit, TBA_CEL_TABLE_AT + entry * 4,
                (unsigned long) TBA_CURSOR_STREAM_AT);
    }
    for (row = 0; row < TBA_TILE_PX; row++) {
        tba_cursor_kit[TBA_CURSOR_STREAM_AT + row * 2] =
            (unsigned char) (TBA_TILE_PX - 1);
        tba_cursor_kit[TBA_CURSOR_STREAM_AT + row * 2 + 1] = TBA_CURSOR_COLOR;
    }

    tba_zero(tba_glyphs, (int) sizeof(tba_glyphs));
    ((struct fdps_cel_header *) tba_glyphs)->sprite_width = TBA_GLYPH_W;
    ((struct fdps_cel_header *) tba_glyphs)->sprite_height = TBA_GLYPH_ROWS;
    for (entry = 0; entry < TBA_GLYPH_ENTRIES; entry++) {
        tba_u32(tba_glyphs, TBA_CEL_TABLE_AT + entry * 4,
                (unsigned long) TBA_GLYPH_STREAM_AT);
    }
    for (row = 0; row < TBA_GLYPH_ROWS; row++) {
        tba_glyphs[TBA_GLYPH_STREAM_AT + row * 2] =
            (unsigned char) (TBA_GLYPH_W - 1);
        tba_glyphs[TBA_GLYPH_STREAM_AT + row * 2 + 1] = TBA_GLYPH_COLOR;
    }

    for (offset = 0; offset < TBA_GAUGE_GRAPHIC_STRIDE; offset++) {
        tba_gauge_sheet[offset] = 10;
        tba_gauge_sheet[TBA_GAUGE_GRAPHIC_STRIDE + offset] = 20;
        tba_gauge_sheet[2 * TBA_GAUGE_GRAPHIC_STRIDE + offset] = 30;
    }

    tba_zero(tba_vfs, (int) sizeof(tba_vfs));
    tba_u16(tba_vfs, TBA_VFS_TABLE_OFFSET_AT, (unsigned int) TBA_VFS_TABLE_AT);
    tba_u32(tba_vfs, TBA_VFS_COUNT_AT, 1UL);
    strcpy((char *) tba_vfs + TBA_VFS_TABLE_AT, TBA_ANIMATION_MEMBER);
    tba_u32(tba_vfs, TBA_VFS_TABLE_AT + TBA_VFS_ENTRY_SIZE_AT,
            (unsigned long) TBA_SAF_BYTES);
    tba_u32(tba_vfs, TBA_VFS_TABLE_AT + TBA_VFS_ENTRY_START_AT,
            (unsigned long) TBA_VFS_MEMBER_AT);
    tba_vfs[TBA_VFS_MEMBER_AT] = 'S';
    tba_vfs[TBA_VFS_MEMBER_AT + 1] = 'A';
    tba_vfs[TBA_VFS_MEMBER_AT + 2] = 'F';
    tba_u16(tba_vfs, TBA_VFS_MEMBER_AT + TBA_SAF_FRAME_COUNT_AT, 0);
}

static void tba_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* One dispatch, run the way the game runs it: in a graphics mode with the
   timer moving, because whichever arm the scores pick may present frames.  A
   case that expects no arm to run pays only for the two mode switches. */
static int tba_act(int actor_index, int side_select)
{
    int acted;

    tba_set_mode(TBA_MODE_320X200X256);
    tba_saved_timer = _dos_getvect(TBA_TIMER_VECTOR);
    _dos_setvect(TBA_TIMER_VECTOR, tba_timer_isr);

    acted = fdps_map_actor_take_best_action(actor_index, side_select);

    _dos_setvect(TBA_TIMER_VECTOR, tba_saved_timer);
    tba_set_mode(TBA_MODE_TEXT);
    return acted;
}

/* A 4x4 battle over uniform terrain 0 with every class row costing 1 a step,
   an empty item table, an empty spell table, no units, and the grid in the
   state fdps_map_grid_reset leaves it in.  The three score globals go on their
   sentinel, the attack target index on unit 0 -- this function resolves that
   record whether or not the attack search published anything, so it has to
   name a record that exists -- and the cursor draw mode on its own. */
static void tba_stage(void)
{
    int i;
    int terrain;

    tba_zero(tba_tilemap, (int) sizeof(tba_tilemap));
    for (i = 0; i < TERRAIN_CELLS_AT; i++) {
        tba_tilemap[i] = 0xaa;
    }
    *(short *) (tba_tilemap + 7) = (short) TBA_W;
    for (i = 0; i < TBA_CELLS; i++) {
        *(short *) (tba_tilemap + TERRAIN_CELLS_AT + i * 2) = (short) i;
    }

    tba_zero(tba_attr, (int) sizeof(tba_attr));
    for (i = 0; i < ATTR_ROWS_AT; i++) {
        tba_attr[i] = 0xaa;
    }

    tba_zero(tba_event, (int) sizeof(tba_event));
    for (i = 0; i < EVENT_CELLS_AT; i++) {
        tba_event[i] = 0xaa;
    }
    *(short *) (tba_event + 7) = (short) TBA_W;

    tba_zero(tba_grid, (int) sizeof(tba_grid));
    *(short *) tba_grid = (short) TBA_W;
    *(short *) (tba_grid + 2) = (short) TBA_H;
    for (i = 0; i < TBA_CELLS; i++) {
        tba_grid[4 + i * 2] = 0x00;
        tba_grid[4 + i * 2 + 1] = 0xff;
    }

    tba_zero(tba_classes, (int) sizeof(tba_classes));
    for (i = 0; i < TBA_CLASSES; i++) {
        for (terrain = 0; terrain < 8; terrain++) {
            tba_classes[i * TBA_CLASS_STRIDE + terrain] = 1;
        }
    }

    tba_zero((unsigned char *) tba_units, (int) sizeof(tba_units));
    tba_zero((unsigned char *) tba_items, (int) sizeof(tba_items));
    tba_zero((unsigned char *) tba_spells, (int) sizeof(tba_spells));

    data_fdps_scene_layer_tile_map_ptrs[0] = tba_tilemap;
    data_fdps_scene_layer_tile_attr_ptr[0] = tba_attr;
    data_fdps_battle_move_grid_ptr = tba_grid;
    data_fdps_map_cell_event_code_layer_ptr = tba_event;
    data_fdps_class_table_ptr = tba_classes;
    data_fdps_map_unit_array_ptr = (unsigned char *) tba_units;
    data_fdps_item_effect_table_ptr = (unsigned char *) tba_items;
    data_fdps_battle_spell_effect_table_ptr = (unsigned char *) tba_spells;
    data_fdps_map_unit_count = 0;

    data_fdps_battle_ai_best_physical_score = TBA_SCORE_SENTINEL;
    data_fdps_battle_ai_best_spell_score = TBA_SCORE_SENTINEL;
    data_fdps_battle_ai_best_item_score = TBA_SCORE_SENTINEL;
    data_fdps_battle_ai_best_physical_target_idx = 0;
    data_fdps_map_ai_best_spell_id = 0;
    data_fdps_map_cursor_draw_mode = TBA_DRAW_SENTINEL;

    /* Everything from here down is for the two arms that play something, and
       nothing below is read by any of the three searches.  The animation flag
       is one of them and is named rather than inherited: left set, the attack
       arm plays the full-screen exchange instead, which composes its clip
       names out of a portrait id and blocks on a member no fixture here
       holds.

       The spell arm plays its cast on the map, which loads Emg%02d.saf out of
       the shipped MISC.VFS (tests/gamefile.lst) and floats a popup over every
       unit it resolved -- so the glyph sheet below is staged for its sake and
       not the attack's. */
    data_fdps_ui_battle_animation_enabled = 0;
    tba_stage_sheets();
    data_fdps_cursor_highlight_sprite_sheet_ptr = tba_cursor_kit;
    data_fdps_number_glyph_sheet_ptr = tba_glyphs;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_unit_gauge_sheet_ptr = tba_gauge_sheet;
    data_fdps_animation_baseani_archive_ptr = tba_vfs;
    data_fdps_scene_layer_count = 0;
    data_fdps_chapter_current_chapter_id = TBA_INERT_CHAPTER;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_input_last_scancode = 3;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_battle_pending_xp_credit = 0;
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    for (i = 0; i < SFX_SAMPLE_SLOT_COUNT; i++) {
        data_fdps_audio_sample_handle_table[i] = NULL;
    }
    for (i = 0; i < 6; i++) {
        data_fdps_battle_tile_attr_ap_modifier_table[i] = 0;
        data_fdps_battle_tile_attr_def_modifier_table[i] = 0;
    }
}

static void tba_unit(int index, int x, int y, int side, int ap, int dp,
                     int hp_current, int hp_max)
{
    tba_units[index].pos_x = (unsigned char) x;
    tba_units[index].pos_y = (unsigned char) y;
    tba_units[index].side = (unsigned char) side;
    tba_units[index].char_id = (unsigned char) TBA_NOBODY;
    tba_units[index].portrait_id = (unsigned char) TBA_NO_MAP_SPRITE;
    tba_units[index].ap = (short) ap;
    tba_units[index].dp = (short) dp;
    tba_units[index].hp_current = (short) hp_current;
    tba_units[index].hp_max = (short) hp_max;
    if (index + 1 > data_fdps_map_unit_count) {
        data_fdps_map_unit_count = index + 1;
    }
}

/* Mark all eight bag entries empty, which is what makes fdps_unit_item_count
   answer 0 and the item search return before it scores anything.  A zeroed
   record does NOT do that: a flag byte of 0 is an occupied entry. */
static void tba_empty_bag(int unit_index)
{
    int slot;

    for (slot = 0; slot < 8; slot++) {
        tba_units[unit_index].inventory_slots[slot * 2] = TBA_SLOT_EMPTY;
        tba_units[unit_index].inventory_slots[slot * 2 + 1] = 0;
    }
}

/* Occupy one bag entry.  `equipped` puts the 0x40 the weapon search looks for
   on the flag byte; without it the entry is carried but not wielded. */
static void tba_carry(int unit_index, int slot, int item_id, int equipped)
{
    tba_units[unit_index].inventory_slots[slot * 2] =
        (unsigned char) (equipped != 0 ? TBA_EQUIPPED : 0);
    tba_units[unit_index].inventory_slots[slot * 2 + 1] =
        (unsigned char) item_id;
}

static void tba_weapon(int item_id, int range_min, int range_max)
{
    tba_items[item_id].type = (unsigned char) TBA_WEAPON_TYPE;
    tba_items[item_id].range_min = (unsigned char) range_min;
    tba_items[item_id].range_max = (unsigned char) range_max;
}

static void tba_usable(int item_id, int use_effect, int use_amount,
                       int use_distance, int use_target, int use_radius)
{
    tba_items[item_id].use_effect = (unsigned char) use_effect;
    tba_items[item_id].use_amount = (short) use_amount;
    tba_items[item_id].use_distance = (unsigned char) use_distance;
    tba_items[item_id].use_target = (unsigned char) use_target;
    tba_items[item_id].use_radius = (unsigned char) use_radius;
}

static void tba_spell(int spell_id, int power, int cast_range, int area,
                      int mp_cost, int target_side)
{
    tba_spells[spell_id].power = (short) power;
    tba_spells[spell_id].cast_range_flags = (unsigned char) cast_range;
    tba_spells[spell_id].area = (unsigned char) area;
    tba_spells[spell_id].mp_cost = (unsigned char) mp_cost;
    tba_spells[spell_id].target_side = (unsigned char) target_side;
}

/* Set one learned bit in the five-byte bitmap at record +0x1a, the way
   fdps_unit_collect_known_spells reads it. */
static void tba_learns(int unit_index, int spell_id)
{
    tba_units[unit_index].spells_known_bitmap[spell_id / 8] |=
        (unsigned char) (1 << (spell_id % 8));
}

/* The record fields this function reads for itself, as opposed to the ones its
   three scorers read: the behaviour byte the tie flag is masked out of, and the
   two signed stat words the damage estimate is the difference of.  The spell
   record's power word is the other one -- it is read as the FIRST field of
   whatever fdps_get_spell_record answers, MOVSX word ptr [EAX] with no
   displacement, so a record whose power moved would feed the tie a different
   number. */
static void tba_reads_these_record_fields(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap), 0x48);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, dp), 0x4a);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, power), 0x00);
}

/* Nothing to attack, nothing to cast, nothing to use.  The actor has no weapon
   equipped, so the attack search leaves at 00012288; it knows no spell, so the
   spell search leaves at 00013493; its bag is empty, so the item search leaves
   at 000130bd.  All three globals therefore come back 0, the three CMP ...,0x6
   all take their JL, and the exit at 00012cae loads 0 without ever reaching the
   store to the cursor mode -- so the sentinel survives.
 *
 * That the three globals read 0 rather than 0x5a is the evidence that all three
 * searches were actually run: each of them zeroes its own global on entry and
 * nothing else in this function writes any of the three.
 *
 * The attack target index is read back too.  This function resolves a record
 * through it unconditionally and never writes it, so the 0 the fixture parked
 * there has to still be there. */
static void tba_no_score_reaches_the_threshold(void)
{
    tba_stage();
    tba_unit(0, 1, 1, 0, 10, 5, 100, 100);
    tba_empty_bag(0);

    CHECK_EQ(tba_act(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, TBA_DRAW_SENTINEL);
}

/* The physical score alone carries the decision.  The actor wields a reach-1
   weapon and cannot move, so its one candidate tile is its own and the enemy
   beside it is in reach: attack 10 against defence 3 is an estimate of 7, above
   the wound threshold and below the target's 100 HP, so the tier is 8.  It
   knows no spell and its only bag entry holds that same weapon, whose
   use_effect is 0, so the other two searches score 0.
 *
 * 8 > 0 and 8 > 0 is the first of the five comparisons, and the answer is 1
 * with the cursor mode stored 0 over the sentinel. */
static void tba_a_physical_score_alone_acts(void)
{
    tba_stage();
    tba_weapon(1, 1, 1);
    tba_unit(0, 0, 0, 0, 10, 5, 100, 100);
    tba_empty_bag(0);
    tba_carry(0, 0, 1, 1);
    tba_unit(1, 1, 0, 1, 0, 3, 100, 100);

    CHECK_EQ(tba_act(0, 0), 1);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 0);
}

/* The spell score alone carries the decision, which is the fourth comparison:
   the spell beats the physical score and is at least the item score.  The actor
   wields nothing and carries nothing, so those two searches score 0; it knows
   spell 1, a power-10 spell with a cast range of 3, no MP cost and target_side
   0, which the enemy-phase fork turns into "every non-zero side" -- the unit
   beside it.  100 HP is not below a power of 10, so the spell wounds and scores
   8. */
static void tba_a_spell_score_alone_acts(void)
{
    tba_stage();
    tba_spell(1, 10, 3, 0, 0, 0);
    tba_unit(0, 0, 0, 0, 10, 5, 100, 100);
    tba_units[0].mp_current = 10;
    tba_empty_bag(0);
    tba_learns(0, 1);
    tba_unit(1, 1, 0, 1, 0, 3, 100, 100);

    CHECK_EQ(tba_act(0, 0), 1);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 0);
}

/* The threshold is 6 and the compare is JL, so a score OF EXACTLY 6 acts.
 *
 * The actor carries a healing item aimed at its own tile with a radius of 1 and
 * a use_target of 3, which on the enemy phase selects its own side; it wields
 * nothing and knows no spell, so the item score is the whole decision.  Both
 * side-0 units sit on 40 of 100 HP, which is above a third and not above a
 * half, so fdps_score_targets_for_item scores each of them 3.
 *
 * First half: the unit count is cut to 1, so the collector sees only the actor,
 * the total is 3 and the three JL branches all hold -- 0 comes back and the
 * cursor mode is untouched.  Second half: the same map with both units visible
 * totals 6, the first CMP ...,0x6 / JGE holds, and 1 comes back with the cursor
 * mode stored.  A threshold written as strictly greater than 6 would answer 0
 * in the second half. */
static void tba_the_threshold_is_six_and_six_acts(void)
{
    tba_stage();
    tba_usable(3, TBA_USE_EFFECT_HEAL, 0, 0, TBA_USE_TARGET_OWN_SIDE, 1);
    tba_unit(0, 1, 1, 0, 10, 5, 40, 100);
    tba_empty_bag(0);
    tba_carry(0, 0, 3, 0);
    tba_unit(1, 1, 0, 0, 10, 5, 40, 100);
    data_fdps_map_unit_count = 1;

    CHECK_EQ(tba_act(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 3);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, TBA_DRAW_SENTINEL);

    tba_stage();
    tba_usable(3, TBA_USE_EFFECT_HEAL, 0, 0, TBA_USE_TARGET_OWN_SIDE, 1);
    tba_unit(0, 1, 1, 0, 10, 5, 40, 100);
    tba_empty_bag(0);
    tba_carry(0, 0, 3, 0);
    tba_unit(1, 1, 0, 0, 10, 5, 40, 100);

    CHECK_EQ(tba_act(0, 0), 1);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 6);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 0);
}

/* side_select is forwarded to all three searches unchanged -- the same
   [EBP+0x18] is pushed ahead of each of the three CALLs -- and the item search
   is the one that shows it here.  This is the fixture that scores 6 above, run
   with side_select 1 instead of 0: on the NPC phase the item's use_target byte
   goes through as it stands, and select_mode 3 keeps side 2 (aitarget.h), which
   nothing here is on, so no aim tile catches a target, the item score is 0 and
   the answer falls back to 0 with the cursor mode untouched.  A build that
   pushed a constant, or the actor's own side, would still score 6 here. */
static void tba_side_select_reaches_the_searches(void)
{
    tba_stage();
    tba_usable(3, TBA_USE_EFFECT_HEAL, 0, 0, TBA_USE_TARGET_OWN_SIDE, 1);
    tba_unit(0, 1, 1, 0, 10, 5, 40, 100);
    tba_empty_bag(0);
    tba_carry(0, 0, 3, 0);
    tba_unit(1, 1, 0, 0, 10, 5, 40, 100);

    CHECK_EQ(tba_act(0, 1), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, TBA_DRAW_SENTINEL);
}

/* THE DISPATCH IS FIVE COMPARISONS AND NOT A SELECTION OF THE MAXIMUM.  All
   three searches score 8 here: the actor wields a reach-1 weapon against an
   adjacent enemy on 100 HP, knows a power-10 spell that reaches the same enemy,
   and carries a damage item aimed at its own tile whose use_amount of 0 is
   below its own 100 HP.  With A == S == I every one of the five conditions is
   false, so NO action routine is called at all -- and the cursor mode is still
   stored 0 and 1 is still returned, which is what makes the caller believe the
   actor acted and skip its movement fallbacks.  An implementation that ran
   whichever option scored highest, or that answered 0 when no arm matched,
   fails this case.
 *
 * Which arm ran cannot be read back from here -- two of the three are still
   stubs and the third's effects are tests/aiact.c's subject; what this case
   pins is the pair that is this function's own -- the answer and the cursor
   mode -- on the arrangement that reaches none of them. */
static void tba_three_equal_scores_run_nothing_and_still_report_acted(void)
{
    tba_stage();
    tba_weapon(1, 1, 1);
    tba_usable(2, TBA_USE_EFFECT_DAMAGE, 0, 0, TBA_USE_TARGET_OWN_SIDE, 0);
    tba_spell(1, 10, 3, 0, 0, 0);
    tba_unit(0, 0, 0, 0, 10, 5, 100, 100);
    tba_units[0].mp_current = 10;
    tba_empty_bag(0);
    tba_carry(0, 0, 1, 1);
    tba_carry(0, 1, 2, 0);
    tba_learns(0, 1);
    tba_unit(1, 1, 0, 1, 0, 3, 100, 100);

    CHECK_EQ(tba_act(0, 0), 1);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 8);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 0);
}

/* The attack-versus-spell tie, the second of the five comparisons, driven all
   the way through fdps_get_spell_record.  The same actor as above without the
   damage item: the attack and the spell both score 8 and the item scores 0, so
   A == S and S > I holds and the arm at 00012d0a runs.  The published spell id
   is 1, below the 0x12 cut, so the tie is settled by weighing the spell's power
   word against the physical damage estimate -- and that estimate is the actor's
   attack word less the DEFENCE WORD OF THE UNIT THE ATTACK SEARCH PUBLISHED,
   read through data_fdps_battle_ai_best_physical_target_idx and not through any
   argument.
 *
 * Which way the tie went is invisible from here: the spell arm is still a
   stub.  What this case does pin is that the arm is reachable and survives the
   spell-record
   fetch -- a fetch that would fault on a null spell table, and that the
   original makes before the 0x12 test and on both of its outcomes -- and that
   the function still answers 1 with the cursor mode stored. */
static void tba_the_attack_and_spell_tie_resolves_the_spell_record(void)
{
    tba_stage();
    tba_weapon(1, 1, 1);
    tba_spell(1, 10, 3, 0, 0, 0);
    tba_unit(0, 0, 0, 0, 10, 5, 100, 100);
    tba_units[0].mp_current = 10;
    tba_empty_bag(0);
    tba_carry(0, 0, 1, 1);
    tba_learns(0, 1);
    tba_unit(1, 1, 0, 1, 0, 3, 100, 100);

    CHECK_EQ(tba_act(0, 0), 1);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 0);
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

    RUN_TEST(ai_no_candidate_returns_zero_and_resets_the_grid);
    RUN_TEST(ai_side_filter_is_hard_coded_not_side_select);
    RUN_TEST(ai_walks_toward_the_nearest_reachable_opponent);
    RUN_TEST(ai_a_target_on_the_same_column_still_moves);
    RUN_TEST(ai_class_row_is_the_raw_code_with_no_bias);
    RUN_TEST(ai_fill_allowance_is_a_flat_hundred);

    RUN_TEST(sl_no_opposing_unit_returns_zero);
    RUN_TEST(sl_side_select_is_a_truth_value_not_a_side_number);
    RUN_TEST(sl_only_flags_bit_zero_retires_a_candidate);
    RUN_TEST(sl_nearest_candidate_wins_not_the_first_found);
    RUN_TEST(sl_distance_is_the_sum_and_a_tie_keeps_the_lower_index);
    RUN_TEST(sl_a_winner_on_the_actors_own_tile_moves_nothing);
    RUN_TEST(sl_scan_stops_at_the_unit_count);
    RUN_TEST(sl_terrain_cost_does_not_reach_the_search);

    RUN_TEST(bs_a_retired_actor_is_left_alone);
    RUN_TEST(bs_behavior_eight_does_nothing_and_skips_the_tail);
    RUN_TEST(bs_the_behavior_is_the_low_nibble_of_byte_0x34);

    RUN_TEST(tba_reads_these_record_fields);
    RUN_TEST(tba_no_score_reaches_the_threshold);
    RUN_TEST(tba_a_physical_score_alone_acts);
    RUN_TEST(tba_a_spell_score_alone_acts);
    RUN_TEST(tba_the_threshold_is_six_and_six_acts);
    RUN_TEST(tba_side_select_reaches_the_searches);
    RUN_TEST(tba_three_equal_scores_run_nothing_and_still_report_acted);
    RUN_TEST(tba_the_attack_and_spell_tie_resolves_the_spell_record);

    /* Put the globals back before leaving.  stage() points four of them at
       this file's own arrays and the runners share one process: a later unit
       that expects an unallocated map would inherit live pointers into another
       translation unit's fixture and pass or fail for the wrong reason. */
    data_fdps_scene_layer_tile_map_ptrs[0] = NULL;
    data_fdps_scene_layer_tile_attr_ptr[0] = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;

    /* The same for the six the AI cases above stage on top of those four, plus
       the three view and cursor globals the walk they drive advances.  Every
       one goes back to the value the uninitialised bss holds. */
    data_fdps_class_table_ptr = NULL;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_input_last_scancode = 0;
    data_fdps_view_frame_last_tick = 0;

    /* And the same for the two data-table pointers and the five AI decision
       globals that only the take-best-action cases stage. */
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_battle_spell_effect_table_ptr = NULL;
    data_fdps_battle_ai_best_physical_score = 0;
    data_fdps_battle_ai_best_spell_score = 0;
    data_fdps_battle_ai_best_item_score = 0;
    data_fdps_battle_ai_best_physical_target_idx = 0;
    data_fdps_map_ai_best_spell_id = 0;
}
