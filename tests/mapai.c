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
#include "keybd.h"
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
}
