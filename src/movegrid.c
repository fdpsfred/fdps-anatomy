/* movegrid.c -- the battle map's working movement grid.
 *
 * See movegrid.h for the block's layout and what a cell byte means.  Nothing
 * here owns state: every function reads the grid through
 * data_fdps_battle_move_grid_ptr and works in place on what it finds.
 */
#include <stddef.h>
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "maptile.h"
#include "movegrid.h"
#include "table.h"
#include "unit.h"
#include "walk.h"

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* 00063930. All 800 bytes are zero in the image (bss); the flood fill seeds
   entry 0 and fills each wave before reading it, so no initial value is ever
   observed. */
unsigned char data_fdps_battle_move_frontier_y[800];

/* 00063c50. Starts all zero (bss); fdps_move_grid_flood_fill_range seeds entry
   0 of the active 400-byte bank before reading any entry, so no initial value
   is observed. */
unsigned char data_fdps_battle_move_frontier_x[800];

/* End of global data. */

/* 00010b20.  CMP dword ptr [0x00060144],0 / JZ to the epilogue: an unallocated
   grid is not an error here, it is a silent return.  Which of the fourteen
   callers can actually reach it with a null grid is not established -- all
   fourteen are battle-time routines that normally run after
   fdps_field_load_chapter_resources has allocated it -- so the check is
   reproduced because the original has it, not because a caller is known to
   depend on it.

   The two dimensions are read with MOVSX from 16-bit header words, and the
   loop test is CMP EAX,[EBP-8] / JG, the signed compare.  Both halves of that
   matter: a header word of 0xffff is -1, the product is negative, and the loop
   body never runs.  Reading the header as unsigned, or testing the bound with
   an unsigned compare, turns that into a walk of four billion cells over the
   heap.

   The product is recomputed from the two dimension slots on every iteration
   (MOV EAX,[EBP-0x10] / IMUL EAX,[EBP-0xc] at 00010b5e), which is what the
   loop condition below spells; nothing in the body writes either slot, so it
   is the same value each time.

   AND byte ptr [EAX],0x3f keeps the low six bits deliberately.  No code in the
   program writes or reads them, so clearing the whole byte would look
   equivalent -- it is not what the original does, and the bits are the only
   record that they exist. */
void fdps_map_grid_reset(void)
{
    struct fdps_move_grid_cell *cell;
    int grid_width;
    int grid_height;
    int cell_index;

    if (data_fdps_battle_move_grid_ptr == NULL) {
        return;
    }

    cell = (struct fdps_move_grid_cell *)
           (data_fdps_battle_move_grid_ptr + 4);
    grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
    grid_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2);

    for (cell_index = 0;
         cell_index < grid_width * grid_height;
         cell_index++) {
        cell->flags = (unsigned char) (cell->flags & 0x3f);
        cell->marker = (unsigned char) 0xff;
        cell++;
    }
}

/* 00010c30.  Same silent return on an unallocated grid as the reset above:
   CMP dword ptr [0x00060144],0 / JZ straight to the epilogue.  Its one caller
   fdps_move_grid_mark_opposing_zones_of_control walks the unit array and hands
   over unit record bytes +0 and +1, so a battle that has not loaded a map
   cannot reach here with a live unit list -- the check is reproduced because
   the original has it, not because a caller is known to need it.

   The four neighbour marks are written out here rather than calling
   fdps_move_grid_set_stop_flag, which is the same three lines as a standalone
   function at 00010da0.  The binary keeps that copy but nothing calls it.  The
   original's game units were compiled with -oe=25, which expands small
   same-unit callees on its own (rebuild_info/build_flags.md), so the assembly
   does not settle how the original spelled this.  It does not need to: the
   rebuild's build has no -oe yet, so emitting a call would put a CALL where the
   original has none, and under ADR-0001 writing the marks out reproduces the
   behaviour either way.

   What the shape here does say is that this is not 00010da0's body with its
   parameters bound once: each of the four sites gets its own five-dword group
   in the frame, two more than a single binding needs.  That is recorded as an
   open concern rather than resolved, because macro and four hand-written copies
   are the same token stream after preprocessing and no reading of the binary
   can separate them.

   Each neighbour re-reads the width word out of the header instead of using
   the copy taken above, and only the centre uses the copy (MOVSX word ptr
   [EAX] at 00010c84, 00010cc8, 00010d0f and 00010d56 against MOV EAX,[EBP-8]
   at 00010d79).  That is faithfully reproduced: it is invisible on a sane
   header, but a negative width sends a neighbour's index below the cell array
   and the OR lands in the header itself, after which the two would disagree.

   Both bounds are the signed compare -- MOV EAX,[EBP-8] / DEC / CMP EAX,
   [EBP+0x14] / JLE skips the mark, so the tile is marked only while
   width-1 > tile_x -- and both dimensions arrive through MOVSX.  Read either
   header word as unsigned and a width of 0xffff becomes 65535 rather than -1,
   which turns the guard the wrong way and marks a cell off the end of the
   block.

   There is no bounds check on the centre tile: whatever (tile_x, tile_y) is,
   the 0x40 goes in at base + 4 + 2 * (tile_y * width + tile_x).  The four
   neighbour guards do not amount to one, because they test the neighbour's own
   coordinate and not the centre's -- tile_x == width is marked without
   complaint.

   Both bits are ORed in.  Byte 0 also carries six low bits nothing else in the
   program reads, and byte 1 the flood fill's marker; a store instead of an OR
   would drop the low bits, drop the other zone bit, and stop the grid
   accumulating one unit's zone on top of another's, which is the whole point
   of the loop in the caller. */
void fdps_move_grid_mark_zone_of_control(int tile_x, int tile_y)
{
    struct fdps_move_grid_cell *left_cell;
    struct fdps_move_grid_cell *up_cell;
    struct fdps_move_grid_cell *right_cell;
    struct fdps_move_grid_cell *down_cell;
    struct fdps_move_grid_cell *unit_cell;
    int grid_width;
    int grid_height;

    if (data_fdps_battle_move_grid_ptr == NULL) {
        return;
    }

    grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
    grid_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2);

    if (tile_x != 0) {
        left_cell = (struct fdps_move_grid_cell *)
                    (data_fdps_battle_move_grid_ptr + 4 +
                     2 * ((int) *(short *) data_fdps_battle_move_grid_ptr *
                              tile_y +
                          (tile_x - 1)));
        left_cell->flags = (unsigned char) (left_cell->flags | 0x80);
    }

    if (tile_y != 0) {
        up_cell = (struct fdps_move_grid_cell *)
                  (data_fdps_battle_move_grid_ptr + 4 +
                   2 * ((int) *(short *) data_fdps_battle_move_grid_ptr *
                            (tile_y - 1) +
                        tile_x));
        up_cell->flags = (unsigned char) (up_cell->flags | 0x80);
    }

    if (grid_width - 1 > tile_x) {
        right_cell = (struct fdps_move_grid_cell *)
                     (data_fdps_battle_move_grid_ptr + 4 +
                      2 * ((int) *(short *) data_fdps_battle_move_grid_ptr *
                               tile_y +
                           (tile_x + 1)));
        right_cell->flags = (unsigned char) (right_cell->flags | 0x80);
    }

    if (grid_height - 1 > tile_y) {
        down_cell = (struct fdps_move_grid_cell *)
                    (data_fdps_battle_move_grid_ptr + 4 +
                     2 * ((int) *(short *) data_fdps_battle_move_grid_ptr *
                              (tile_y + 1) +
                          tile_x));
        down_cell->flags = (unsigned char) (down_cell->flags | 0x80);
    }

    unit_cell = (struct fdps_move_grid_cell *)
                (data_fdps_battle_move_grid_ptr + 4 +
                 2 * (tile_y * grid_width + tile_x));
    unit_cell->flags = (unsigned char) (unit_cell->flags | 0x40);
}

/* 00010da0.  The one-cell form of the 0x80 mark that the function above writes
   out inline four times over.  Nothing in the image calls this entry and
   nothing takes its address -- get_xrefs_to 0x00010da0 comes back empty -- so
   it is code the linker kept from an object whose other functions are used.
   It is emitted anyway because it is compiler-emitted game code and its
   absence would be a hole in the image, not because a call site exists.

   It has no null check, unlike both functions above.  The first two
   instructions of the body are MOV EAX,[0x00060144] / MOVSX EAX,word ptr
   [EAX]: the pointer is dereferenced before anything has looked at it.
   Adding the guard its neighbours have is the obvious tidy-up and it would
   not be this function.

   The width word arrives through MOVSX, the signed read, and the address is
   formed as base + 4 + 2 * (width * tile_y + tile_x) with the multiply taking
   tile_y ([EBP+0x18]) and the add taking tile_x ([EBP+0x14]).  A header width
   of 0xffff is therefore -1 and puts the cell two bytes *below* the array, in
   the header itself; read unsigned it would be 65535 and land 128KB past the
   end of the block.  Neither coordinate is checked against the header at all.

   OR byte ptr [EAX],0x80 leaves byte 0's other bits and byte 1's flood-fill
   marker where they are.  A store would drop the 0x40 that says a unit stands
   on the tile, which is exactly the bit the caller of a zone-of-control mark
   has just set, and would stop zones accumulating. */
void fdps_move_grid_set_stop_flag(int tile_x, int tile_y)
{
    struct fdps_move_grid_cell *cell;

    cell = (struct fdps_move_grid_cell *)
           (data_fdps_battle_move_grid_ptr + 4 +
            2 * ((int) *(short *) data_fdps_battle_move_grid_ptr * tile_y +
                 tile_x));
    cell->flags = (unsigned char) (cell->flags | 0x80);
}

/* 00010b90.  Walks the whole map unit array once and hands every unit on the
   opposing side to fdps_move_grid_mark_zone_of_control above, so the range
   flood fill that runs next cannot walk through them.

   The side test uses the TRUTH VALUE of the record's side byte on both sides,
   never its number: CMP dword ptr [EBP+0x14],0x0 against CMP byte ptr
   [EAX+0x6],0x0, twice.  Side bytes run 0 enemy, 1 neutral, 2 player, so
   side_select 1 marks exactly the units whose side byte is 0.  The obvious C
   -- unit->side != side_select -- agrees with this on side_select 0 and 1 and
   is still wrong: fdps_deploy_unit's sweep passes 1 and would then also stamp
   a zone of control on every player unit.

   The record pointer is cached from the global before the loop and stepped by
   ADD dword ptr [EBP-0x4],0x50 at 00010c19, on the skip path as well as after
   a mark, rather than being recomputed from the index.  Both dimensions of
   that matter for equivalence only if the global moved mid-walk, which nothing
   here does; it is written the way the original walks it.

   The two tile bytes are read into their slots at the top of the body, before
   the retired bit is even looked at (XOR EAX,EAX / MOV AL,byte ptr [EDX] at
   00010bc3 and 00010bcd), and both are zero-extended -- a tile column of 0xff
   is 255, not -1.

   The loop bound is CMP EAX,dword ptr [0x00060150] / JL, the signed compare,
   so a negative unit count walks nothing at all.  The retired test is AND
   AL,0x1: only bit 0, never the whole byte, so a unit that has already acted
   this turn still projects its zone of control. */
void fdps_move_grid_mark_opposing_zones_of_control(int side_select)
{
    struct fdps_unit_record *unit;
    int unit_index;
    int tile_x;
    int tile_y;

    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    for (unit_index = 0;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        tile_x = (int) unit->pos_x;
        tile_y = (int) unit->pos_y;
        if (((unit->flags & 1) == 0) &&
            (((side_select == 0) && (unit->side != 0)) ||
             ((side_select != 0) && (unit->side == 0)))) {
            fdps_move_grid_mark_zone_of_control(tile_x, tile_y);
        }
        unit++;
    }
}

/* 00010de0.  The movement range itself, and the reason the grid exists: a
   breadth-first relaxation outwards from the tile the unit stands on, writing
   each reached cell's accumulated cost into byte 1.  Byte 1 comes in holding
   fdps_map_grid_reset's 0xff sentinel, so "cheaper than what is already there"
   and "not reached yet" are the same test, and fdps_move_path_trace walks the
   costs this leaves to build the route the unit actually takes.

   There is no null-grid guard, unlike fdps_map_grid_reset and
   fdps_move_grid_mark_zone_of_control: MOV EAX,[0x00060144] / MOVSX EAX,word
   ptr [EAX] at 00010df0 dereferences the pointer as the first thing the body
   does, and the two scene layer pointers below are dereferenced unchecked in
   the same way.  Adding the guard the neighbours have would not be this
   function.

   Both dimensions arrive through MOVSX (00010e06, 00010e0f), the signed read,
   and both edge guards are the signed compare -- CMP EDX,EAX / JGE at 00011081
   and 000111ce.  A header width of 0xffff is -1, width-1 is -2 and no column
   passes the guard; read unsigned it is 65535 and the fill walks straight off
   the end of the block.  Neither start coordinate is checked against either
   dimension: the cost-0 store at 00010e3c lands wherever the arithmetic puts
   it.

   The frontier is two waves of queued tile coordinates in
   data_fdps_battle_move_frontier_x / _y (movegrid.h), a 0/1 selector held in a
   byte, and the two entry counts on the stack.  Every index into the two
   arrays is formed as buffer * 400 + slot (IMUL ...,0x190), which is why they
   are declared as one array of 800 apiece rather than two of 400.

   x and y are re-read out of the frontier array at every use rather than
   latched into a local -- the assembly reloads them at 00010ed7, 00010f8f,
   00011028, 0001103e and so on -- and that is reproduced here through the two
   slot pointers.  It is invisible while a wave stays under 400 entries.  It
   stops being invisible above that: an append at buffer 0 slot 400 is the same
   byte as buffer 1 slot 0, so an overlong wave rewrites the very entries the
   loop is still walking, and a latched copy would then expand a tile the
   original no longer has queued.

   Each entry tries its four neighbours in the order up, right, down, left, and
   the guards are the neighbour's own coordinate: y != 0, x < width-1,
   y < height-1, x != 0.  Diagonals are not tried at all -- the range is
   4-connected.

   The acceptance test is candidate <= move_points, zone bit 0x40 clear, and
   candidate < the cost already stored.  The last one is strict, so of two
   routes arriving at the same cost the first one wins and the second neither
   stores nor queues; loosening it to <= would queue a great many more cells
   without changing a single cost byte, which is a difference the movement
   range cannot show and the frontier can.

   Bit 0x80 -- movement entering this tile has to stop -- does NOT stop the
   fill at the cell.  It replaces the candidate with move_points, stores that,
   and still queues the cell (00010ff0-00011064).  Storing the real cost and
   skipping the queue, or treating the cell as impassable, are both the obvious
   reading and both put different bytes into the grid: what the original is
   doing is starving further expansion out of the tile -- every neighbour of it
   now costs more than the allowance -- while leaving the tile itself relaxable
   again by a cheaper route (rebuild_info/pitfalls.md).

   Cost 0 is written into the start tile but the start tile is never queued
   with a cost of its own beyond that, and nothing stops a cell being queued
   twice in one wave; both are how the original behaves.

   The three dead reads in every direction block are kept.  Each one loads the
   low byte of the neighbour's tile id out of the terrain layer, masks it with
   0x3ff and looks up byte 2 of that tile id's attribute row -- the terrain
   type -- into a local nothing ever reads (00010f3f-00010f82).  It is the
   inline ancestor of the fdps_map_load_tile_info call that follows and
   computes the same terrain type properly.  They are pure loads, so dropping
   them changes no value the function produces; they are emitted because they
   are three dereferences of two pointers this function never checks, and a
   rebuild that omitted them would survive a null or short layer that the
   original faults on.  Note that they index the terrain layer with the
   MOVEMENT GRID's row stride and not with the width in the terrain layer's own
   header at +7, which is what fdps_map_load_tile_info uses; the two agree only
   while the two headers do.

   The terrain type is read back out of data_fdps_map_tile_terrain_type after
   the call, not out of a return value: the CALL at 00010fb2 is followed by XOR
   EAX,EAX / MOV AL,[0x00069d09], so EAX is discarded.  The cost byte is then
   class_move_cost->move_cost[that type], indexed with no bound of any kind --
   a terrain type above 7 reads past move_cost into the rest of the class
   record. */
void fdps_move_grid_flood_fill_range(struct fdps_class_record *class_move_cost,
                                     int start_x, int start_y, int move_points)
{
    unsigned char *grid_cells;
    unsigned char *attr_terrain_column;
    unsigned char *tile_map_cursor;
    unsigned char *entry_x_slot;
    unsigned char *entry_y_slot;
    struct fdps_move_grid_cell *cell;
    struct fdps_move_grid_cell *neighbour;
    short frontier_count[2];
    unsigned char buffer_select;
    int grid_width;
    int grid_height;
    int cell_stride;
    int row_stride;
    int tile_byte_offset;
    int current_cost;
    int neighbour_flags;
    int neighbour_cost;
    int neighbour_tile_id;
    int neighbour_terrain_type;
    int candidate_cost;
    int current_buffer;
    int next_buffer;
    int entry;

    buffer_select = 0;
    grid_cells = data_fdps_battle_move_grid_ptr;
    attr_terrain_column = data_fdps_scene_layer_tile_attr_ptr[0] + 0x13;
    grid_width = (int) *(short *) grid_cells;
    grid_height = (int) *(short *) (grid_cells + 2);
    cell_stride = 2;
    row_stride = grid_width * 2;
    grid_cells += 4;

    ((struct fdps_move_grid_cell *)
     (grid_cells + start_x * cell_stride + start_y * row_stride))->marker = 0;

    data_fdps_battle_move_frontier_x[buffer_select * 400] =
        (unsigned char) start_x;
    data_fdps_battle_move_frontier_y[buffer_select * 400] =
        (unsigned char) start_y;
    frontier_count[buffer_select] = 1;

    while (frontier_count[buffer_select] != 0) {
        current_buffer = (int) buffer_select;
        next_buffer = (int) (unsigned char) (buffer_select ^ 1);
        frontier_count[next_buffer] = 0;

        for (entry = 0;
             entry < (int) frontier_count[current_buffer];
             entry++) {
            entry_x_slot =
                &data_fdps_battle_move_frontier_x[current_buffer * 400 + entry];
            entry_y_slot =
                &data_fdps_battle_move_frontier_y[current_buffer * 400 + entry];
            tile_byte_offset = cell_stride * (int) *entry_x_slot +
                               row_stride * (int) *entry_y_slot;
            cell = (struct fdps_move_grid_cell *)
                   (grid_cells + tile_byte_offset);
            tile_map_cursor = data_fdps_scene_layer_tile_map_ptrs[0] +
                              tile_byte_offset + 0x0b;
            current_cost = (int) cell->marker;

            if (*entry_y_slot != 0) {
                neighbour = cell - grid_width;
                neighbour_tile_id =
                    (int) *(tile_map_cursor - row_stride) & 0x3ff;
                neighbour_flags = (int) neighbour->flags;
                neighbour_cost = (int) neighbour->marker;
                neighbour_terrain_type =
                    (int) attr_terrain_column[neighbour_tile_id * 4];
                fdps_map_load_tile_info((int) *entry_x_slot,
                                        (int) *entry_y_slot - 1);
                candidate_cost = current_cost + (int)
                    class_move_cost->move_cost[data_fdps_map_tile_terrain_type];
                if ((candidate_cost <= move_points) &&
                    ((neighbour_flags & 0x40) == 0) &&
                    (candidate_cost < neighbour_cost)) {
                    if ((neighbour_flags & 0x80) != 0) {
                        candidate_cost = move_points;
                    }
                    neighbour->marker = (unsigned char) candidate_cost;
                    data_fdps_battle_move_frontier_x[
                        next_buffer * 400 +
                        (int) frontier_count[next_buffer]] = *entry_x_slot;
                    data_fdps_battle_move_frontier_y[
                        next_buffer * 400 +
                        (int) frontier_count[next_buffer]] =
                            (unsigned char) (*entry_y_slot - 1);
                    frontier_count[next_buffer] =
                        (short) (frontier_count[next_buffer] + 1);
                }
            }

            if ((int) *entry_x_slot < grid_width - 1) {
                neighbour = cell + 1;
                neighbour_tile_id =
                    (int) *(tile_map_cursor + cell_stride) & 0x3ff;
                neighbour_flags = (int) neighbour->flags;
                neighbour_cost = (int) neighbour->marker;
                neighbour_terrain_type =
                    (int) attr_terrain_column[neighbour_tile_id * 4];
                fdps_map_load_tile_info((int) *entry_x_slot + 1,
                                        (int) *entry_y_slot);
                candidate_cost = current_cost + (int)
                    class_move_cost->move_cost[data_fdps_map_tile_terrain_type];
                if ((candidate_cost <= move_points) &&
                    ((neighbour_flags & 0x40) == 0) &&
                    (candidate_cost < neighbour_cost)) {
                    if ((neighbour_flags & 0x80) != 0) {
                        candidate_cost = move_points;
                    }
                    neighbour->marker = (unsigned char) candidate_cost;
                    data_fdps_battle_move_frontier_x[
                        next_buffer * 400 +
                        (int) frontier_count[next_buffer]] =
                            (unsigned char) (*entry_x_slot + 1);
                    data_fdps_battle_move_frontier_y[
                        next_buffer * 400 +
                        (int) frontier_count[next_buffer]] = *entry_y_slot;
                    frontier_count[next_buffer] =
                        (short) (frontier_count[next_buffer] + 1);
                }
            }

            if ((int) *entry_y_slot < grid_height - 1) {
                neighbour = cell + grid_width;
                neighbour_tile_id =
                    (int) *(tile_map_cursor + row_stride) & 0x3ff;
                neighbour_flags = (int) neighbour->flags;
                neighbour_cost = (int) neighbour->marker;
                neighbour_terrain_type =
                    (int) attr_terrain_column[neighbour_tile_id * 4];
                fdps_map_load_tile_info((int) *entry_x_slot,
                                        (int) *entry_y_slot + 1);
                candidate_cost = current_cost + (int)
                    class_move_cost->move_cost[data_fdps_map_tile_terrain_type];
                if ((candidate_cost <= move_points) &&
                    ((neighbour_flags & 0x40) == 0) &&
                    (candidate_cost < neighbour_cost)) {
                    if ((neighbour_flags & 0x80) != 0) {
                        candidate_cost = move_points;
                    }
                    neighbour->marker = (unsigned char) candidate_cost;
                    data_fdps_battle_move_frontier_x[
                        next_buffer * 400 +
                        (int) frontier_count[next_buffer]] = *entry_x_slot;
                    data_fdps_battle_move_frontier_y[
                        next_buffer * 400 +
                        (int) frontier_count[next_buffer]] =
                            (unsigned char) (*entry_y_slot + 1);
                    frontier_count[next_buffer] =
                        (short) (frontier_count[next_buffer] + 1);
                }
            }

            if (*entry_x_slot != 0) {
                neighbour = cell - 1;
                neighbour_tile_id =
                    (int) *(tile_map_cursor - cell_stride) & 0x3ff;
                neighbour_flags = (int) neighbour->flags;
                neighbour_cost = (int) neighbour->marker;
                neighbour_terrain_type =
                    (int) attr_terrain_column[neighbour_tile_id * 4];
                fdps_map_load_tile_info((int) *entry_x_slot - 1,
                                        (int) *entry_y_slot);
                candidate_cost = current_cost + (int)
                    class_move_cost->move_cost[data_fdps_map_tile_terrain_type];
                if ((candidate_cost <= move_points) &&
                    ((neighbour_flags & 0x40) == 0) &&
                    (candidate_cost < neighbour_cost)) {
                    if ((neighbour_flags & 0x80) != 0) {
                        candidate_cost = move_points;
                    }
                    neighbour->marker = (unsigned char) candidate_cost;
                    data_fdps_battle_move_frontier_x[
                        next_buffer * 400 +
                        (int) frontier_count[next_buffer]] =
                            (unsigned char) (*entry_x_slot - 1);
                    data_fdps_battle_move_frontier_y[
                        next_buffer * 400 +
                        (int) frontier_count[next_buffer]] = *entry_y_slot;
                    frontier_count[next_buffer] =
                        (short) (frontier_count[next_buffer] + 1);
                }
            }
        }

        buffer_select = (unsigned char) next_buffer;
    }
}

/* 000118f0.  The third step of the movement-range sequence every caller runs:
   mark the opposing zones of control, flood the range, then take the occupied
   tiles back out of it.  A cell's byte 1 is the flood fill's step cost and
   0xff is its unreachable sentinel, so storing 0xff there drops the tile out
   of the range while leaving byte 0's zone bits alone -- the tile can still be
   crossed, it just cannot be stopped on.

   The side test uses only the TRUTH VALUE of the record's side byte, never its
   number: CMP dword ptr [EBP+0x18],0x0 against CMP byte ptr [EAX+0x6],0x0, at
   0001197a and 00011983 / 00011994.  Side bytes run 0 enemy, 1 NPC, 2 player
   while fdps_battle_unit_turn passes the literal 1 for a player unit, so the
   obvious `unit->side == side_select` would block only the NPC units during a
   player unit's move and let that unit finish standing on a comrade.  The
   partition the original applies is {enemy} against {NPC, player}, which is a
   real rule: NPC units obstruct the player exactly as comrades do, and never
   obstruct the enemy AI.  The polarity is the complement of the identically
   named argument of fdps_move_grid_mark_opposing_zones_of_control above, and
   both callers hand the same value to the two of them.

   Unlike fdps_map_grid_reset and fdps_move_grid_mark_zone_of_control there is
   no null-grid guard: MOV EAX,[0x00060144] / MOVSX EAX,word ptr [EAX] at
   000118fc dereferences the pointer as the first thing the body does.  Adding
   the guard the neighbours have would not be this function.

   The width word arrives through MOVSX, the signed read, and it is taken once
   before the loop rather than re-read per unit.  A header width of 0xffff is
   -1 and sends the cell address below the array into the header; read unsigned
   it would be 65535 and land far past the block.

   The record is skipped on index alone -- CMP EAX,dword ptr [EBP+0x14] / JZ at
   00011931 compares the loop counter with the argument, not any field of the
   record -- and the record pointer is stepped by ADD dword ptr [EBP-0x4],0x50
   at 000119a0 on every path, the skips included.

   Both tile bytes are zero-extended (MOV AL,byte ptr [EAX+1] / AND EAX,0xff at
   00011939), so a column byte of 0xff is 255 and not -1, and neither is
   checked against the header before the store: the cell address is formed and
   written wherever it lands.  The loop bound is CMP EAX,dword ptr [0x00060150]
   / JL, the signed compare, so a negative unit count walks nothing.

   The retired test is AND AL,0x1: bit 0 alone, so a unit that has already
   acted this turn still blocks the tile it stands on. */
void fdps_move_grid_block_occupied_tiles(int exclude_unit_index,
                                         int side_select)
{
    struct fdps_unit_record *unit;
    struct fdps_move_grid_cell *cell;
    int grid_width;
    int tile_index;
    int unit_index;

    grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (unit_index = 0;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        if (unit_index != exclude_unit_index) {
            tile_index = grid_width * (int) unit->pos_y + (int) unit->pos_x;
            cell = (struct fdps_move_grid_cell *)
                   (data_fdps_battle_move_grid_ptr + 4 + 2 * tile_index);
            if ((unit->flags & 1) == 0) {
                if (side_select == 0) {
                    if (unit->side == 0) {
                        cell->marker = (unsigned char) 0xff;
                    }
                } else if (unit->side != 0) {
                    cell->marker = (unsigned char) 0xff;
                }
            }
        }
        unit++;
    }
}

/* 00011da0.  The readout the whole grid exists for: whatever the flood fill or
   the targeting mask left behind is turned into a flat list of (x, y) tile
   indices for the caller to iterate, and the count comes back.  A cell counts
   as marked when byte 1 is anything but the 0xff sentinel, so the same test
   yields a movement range after 00010de0 has relaxed the costs down and a
   target list after 00011e50 has written 0 into the mask's tiles.

   There is no null-grid guard, unlike fdps_map_grid_reset and
   fdps_move_grid_mark_zone_of_control above: MOV EAX,[0x00060144] / ADD EAX,5
   at 00011db3 dereferences nothing yet, but MOVSX EAX,word ptr [EAX] three
   instructions later does, with nothing having looked at the pointer.  Adding
   the guard the neighbours have would not be this function.

   Both dimensions arrive through MOVSX and both loop tests are JL, the signed
   compare (00011de2 and 00011dfb).  A header word of 0xffff is -1 and the loop
   it bounds does not run at all; read either word unsigned and it is 65535,
   and the scan walks 65535 cells off the end of the heap block.

   The cursor is set to grid + 5 once, before the loops, and stepped by ADD
   dword ptr [EBP-0x8],0x2 at 00011e33 -- byte 1 of the next cell, on the
   skipped path as well as the collected one.  It is never recomputed from x
   and y, which is why the walk stays correct only while the two loop bounds
   are the same header words the block was sized from: it runs straight through
   the cells in row-major order and the coordinates are counted alongside it
   rather than derived from it.  Expressed here as a cell pointer starting at
   grid + 4 whose marker byte is read, which addresses the same byte the
   original's cursor holds.

   The two coordinate stores are byte stores (MOV AL,byte ptr [EBP-0x1c] / MOV
   byte ptr [EDX],AL at 00011e18), so an index above 255 would be written
   truncated; no map in the game is that wide, and widening the output to a
   short would change the stride the callers walk.

   Nothing bounds the number of pairs written -- there is no capacity argument
   and no compare against one.  Callers 00013040 and 00013420 hand over a
   0x190-byte block, room for 200 pairs, which a wide movement range overruns.
   Adding a capacity parameter or growing those allocations is the obvious fix
   and it is not what the original does; the signature and the callers' sizes
   are reproduced as they are (rebuild_info/pitfalls.md).

   The grid itself is only read.  Callers wipe it afterwards with
   fdps_map_grid_reset. */
int fdps_map_grid_collect_marked_tiles(unsigned char *out_coords)
{
    struct fdps_move_grid_cell *scan_cell;
    int grid_width;
    int grid_height;
    int marked_count;
    int tile_x;
    int tile_y;

    marked_count = 0;
    scan_cell = (struct fdps_move_grid_cell *)
                (data_fdps_battle_move_grid_ptr + 4);
    grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
    grid_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2);

    for (tile_y = 0; tile_y < grid_height; tile_y++) {
        for (tile_x = 0; tile_x < grid_width; tile_x++) {
            if (scan_cell->marker != 0xff) {
                out_coords[0] = (unsigned char) tile_x;
                out_coords[1] = (unsigned char) tile_y;
                out_coords += 2;
                marked_count++;
            }
            scan_cell++;
        }
    }

    return marked_count;
}

/* 00011460.  The route back out of the grid, and the reason the fill above
   wrote a cost into every reachable cell: modes 0 and 1 step from
   (start_x, start_y) to (goal_x, goal_y) over falling costs and hand the
   caller one direction code per step.  Mode 2 is a second job sharing the
   frame -- it ignores the goal entirely, scans the unit array and answers
   which unit of the selected side stands on the cheapest cell.

   mode is read as a byte and never as the dword the caller pushes: XOR EAX,EAX
   / MOV AL,byte ptr [EBP+0x28] at 0001149a and 000116e6, CMP byte ptr
   [EBP+0x28],0x0 at 00011637.  The zero extension rather than MOVSX is what
   makes it unsigned.  All five call sites pass a literal -- 0 at 00011a95,
   00011d2f and 00015609, 1 at 00011adf, 2 at 0001272d -- and a mode that is
   none of the three leaves the direction byte at [EBP-0x4] unwritten, which is
   reproduced here as the uninitialised step_dir rather than papered over.

   There is no null-grid guard, unlike fdps_map_grid_reset and
   fdps_move_grid_mark_zone_of_control: MOV EAX,[0x00060144] / MOVSX EAX,word
   ptr [EAX] at 0001146c is the first thing the body does.  Both header words
   arrive through MOVSX, the signed read, and both edge guards are signed
   compares (MOV EAX,[EBP-0x38] / DEC / CMP EAX,[EBP+0x20] / JLE at 00011670,
   and the same shape at 0001169a), so a width of 0xffff is -1 and no column
   passes the guard; read unsigned it is 65535 and the walk leaves the block.

   The direction codes are 0 = y-1, 1 = x+1, 2 = y+1, 3 = x-1 and 4 = no move,
   and the four neighbours are tried in that order.  Code 4 records nothing and
   moves nothing, so a step that finds no neighbour spins: the only way out of
   the loop is standing on the goal.  The codes are collected into a 100-byte
   frame buffer -- the gap between [EBP-0xa0] and the named slots at [EBP-0x3c]
   -- and copied into out_path REVERSED at 000118bf, so the caller reads the
   last step of the walk first.  Nothing bounds the buffer; a walk longer than
   100 steps writes over the rest of the frame.

   Rebuild note: the acceptance threshold is reseeded from the START tile's
   cost at the top of every step, not from the cost of the tile the walk is
   standing on.  MOV EAX,[EBP-0x28] / MOV [EBP-0x20],EAX at 00011631 opens the
   loop and [EBP-0x28] is written exactly once, at 00011600, before the loop is
   entered.  The obvious C -- reseeding from the current cell's marker -- is a
   different function: where no neighbour undercuts the tile it is standing on,
   the original still steps to any neighbour cheaper than the START tile, while
   the tightened form leaves the direction at 4, moves nothing, records nothing
   and never reaches the goal.

   Mode 0 takes a neighbour only on a strictly lower cost (JL at 0001165e,
   00011688, 000116b2 and 000116d9), so a tie leaves the heading the scan order
   has already chosen.  Mode 1 also accepts an equal cost (JLE at 0001173b,
   00011795 and 000117ec) provided a direction has already been chosen this
   step, at least one step has been recorded, and the last RECORDED step used a
   different code -- MOV AL,byte ptr [EAX + EBP*0x1 + 0xffffff5f] at 00011759
   reads walk_dirs[step_count - 1].  That is what makes mode 1 turn on a tie
   where mode 0 runs straight.  The north neighbour is exempt in both modes: it
   is the strict compare either way.

   The last of the four neighbours does not store the cost it accepted, in
   either mode -- 000116dd and 00011820 write the direction byte alone.  The
   build is -od, which does not delete a dead store (rebuild_info/
   build_flags.md), so that is what the source said and not what the optimiser
   did; the value is dead regardless, because the next step reseeds from
   start_cost.

   In mode 2 start_x is a TRUTH VALUE selecting a side and not a coordinate:
   CMP dword ptr [EBP+0x20],0x0 against CMP byte ptr [EAX+0x6],0x0, twice, at
   000114dd-000114f9, so 0 keeps the units whose side byte is non-zero and any
   non-zero value keeps the units whose side byte is 0.  start_y and both goal
   arguments are not read at all on this path.  The retired test re-resolves
   the record with a SECOND call to fdps_get_unit_record at 0001151b rather
   than reusing the pointer the side test just took, and carries four frame
   temporaries of its own at [EBP-0xa4] down to [EBP-0xb0]: it is an expansion
   of fdps_unit_is_retired's body, not a call to it.  Emitting the call would
   put a CALL in the rebuild that the original does not have, and dropping the
   second lookup would remove one the original performs.  The mask is AND
   AL,0x1, bit 0 alone, so a unit that has merely acted this turn still counts.

   The cost compare is JGE at 00011580, so of two units on equally cheap cells
   the lower index wins, and the sentinel test at 00011593 is against the same
   0xff a cell holds when the fill never reached it -- a unit standing on an
   unreachable cell therefore never matches, and -1 comes back for "no unit"
   and for nothing else.  out_path receives that unit's two tile bytes and
   nothing more: the caller at 0001272d hands over a four-byte stack local. */
int fdps_move_path_trace(int goal_x, int goal_y, unsigned char *out_path,
                         int start_x, int start_y, unsigned char mode)
{
    unsigned char walk_dirs[100];
    struct fdps_unit_record *unit;
    struct fdps_move_grid_cell *cell;
    unsigned char *grid_cells;
    unsigned char *marker_ptr;
    unsigned char step_dir;
    int grid_width;
    int grid_height;
    int cell_stride;
    int row_stride;
    int unit_index;
    int nearest_unit_index;
    int lowest_cost;
    int unit_cost;
    int start_cost;
    int best_cost;
    int neighbour_cost;
    int step_count;
    int reached_goal;
    int path_index;

    grid_cells = data_fdps_battle_move_grid_ptr;
    grid_width = (int) *(short *) grid_cells;
    grid_height = (int) *(short *) (grid_cells + 2);
    cell_stride = 2;
    row_stride = grid_width * 2;
    grid_cells += 4;

    if (mode == 2) {
        lowest_cost = 0xff;
        for (unit_index = 0;
             unit_index < data_fdps_map_unit_count;
             unit_index++) {
            unit = fdps_get_unit_record(unit_index);
            if (((start_x == 0) && (unit->side != 0)) ||
                ((start_x != 0) && (unit->side == 0))) {
                if ((fdps_get_unit_record(unit_index)->flags & 1) == 0) {
                    cell = (struct fdps_move_grid_cell *)
                           (grid_cells + (int) unit->pos_x * cell_stride +
                            (int) unit->pos_y * row_stride);
                    unit_cost = (int) cell->marker;
                    if (unit_cost < lowest_cost) {
                        lowest_cost = unit_cost;
                        nearest_unit_index = unit_index;
                    }
                }
            }
        }
        if (lowest_cost == 0xff) {
            return -1;
        }
        unit = fdps_get_unit_record(nearest_unit_index);
        out_path[0] = unit->pos_x;
        out_path[1] = unit->pos_y;
        return lowest_cost;
    }

    step_count = 0;
    reached_goal = 0;
    cell = (struct fdps_move_grid_cell *)
           (grid_cells + start_x * cell_stride + start_y * row_stride);
    start_cost = (int) cell->marker;
    if (start_cost == 0xff) {
        return -1;
    }

    do {
        cell = (struct fdps_move_grid_cell *)
               (grid_cells + start_x * cell_stride + start_y * row_stride);
        marker_ptr = &cell->marker;
        best_cost = start_cost;

        if (mode == 0) {
            step_dir = 4;
            if (start_y != 0) {
                neighbour_cost = (int) *(marker_ptr - row_stride);
                if (neighbour_cost < best_cost) {
                    best_cost = neighbour_cost;
                    step_dir = 0;
                }
            }
            if (grid_width - 1 > start_x) {
                neighbour_cost = (int) *(marker_ptr + cell_stride);
                if (neighbour_cost < best_cost) {
                    best_cost = neighbour_cost;
                    step_dir = 1;
                }
            }
            if (grid_height - 1 > start_y) {
                neighbour_cost = (int) *(marker_ptr + row_stride);
                if (neighbour_cost < best_cost) {
                    best_cost = neighbour_cost;
                    step_dir = 2;
                }
            }
            if (start_x != 0) {
                neighbour_cost = (int) *(marker_ptr - cell_stride);
                if (neighbour_cost < best_cost) {
                    step_dir = 3;
                }
            }
        } else if (mode == 1) {
            step_dir = 4;
            if (start_y != 0) {
                neighbour_cost = (int) *(marker_ptr - row_stride);
                if (neighbour_cost < best_cost) {
                    best_cost = neighbour_cost;
                    step_dir = 0;
                }
            }
            if (grid_width - 1 > start_x) {
                neighbour_cost = (int) *(marker_ptr + cell_stride);
                if ((neighbour_cost <= best_cost) &&
                    ((neighbour_cost < best_cost) ||
                     ((step_dir != 4) && (step_count != 0) &&
                      (walk_dirs[step_count - 1] != 1)))) {
                    best_cost = neighbour_cost;
                    step_dir = 1;
                }
            }
            if (grid_height - 1 > start_y) {
                neighbour_cost = (int) *(marker_ptr + row_stride);
                if ((neighbour_cost <= best_cost) &&
                    ((neighbour_cost < best_cost) ||
                     ((step_dir != 4) && (step_count != 0) &&
                      (walk_dirs[step_count - 1] != 2)))) {
                    best_cost = neighbour_cost;
                    step_dir = 2;
                }
            }
            if (start_x != 0) {
                neighbour_cost = (int) *(marker_ptr - cell_stride);
                if ((neighbour_cost <= best_cost) &&
                    ((neighbour_cost < best_cost) ||
                     ((step_dir != 4) && (step_count != 0) &&
                      (walk_dirs[step_count - 1] != 3)))) {
                    step_dir = 3;
                }
            }
        }

        if (step_dir == 2) {
            start_y++;
        } else if (step_dir == 3) {
            start_x--;
        } else if (step_dir == 0) {
            start_y--;
        } else if (step_dir == 1) {
            start_x++;
        }

        if ((start_x == goal_x) && (start_y == goal_y)) {
            reached_goal = 1;
        }

        if (step_dir != 4) {
            walk_dirs[step_count] = step_dir;
            step_count++;
        }
    } while (reached_goal == 0);

    for (path_index = 0; path_index < step_count; path_index++) {
        out_path[(step_count - 1) - path_index] = walk_dirs[path_index];
    }

    return step_count;
}

/* Bytes malloc'd for the direction-code buffer: PUSH 0x64 at 00011a3d.  One
   byte per step, and it is the same 100 that fdps_move_path_trace stages its
   own frame copy in before handing the codes over, so neither side bounds a
   path longer than that. */
#define MOVE_STEP_PATH_BYTES 100

/* Bytes malloc'd for the reachable-tile list: PUSH 0x800 at 00011a4a.  Two
   bytes a tile, so 1024 tiles -- fdps_map_grid_collect_marked_tiles bounds
   nothing itself and a map with more marked cells than that overruns this. */
#define MOVE_REACHABLE_COORD_BYTES 0x800

/* The allowance the relaxed retry floods with: PUSH 0x64 at 00011aaf, a
   literal and not the unit's own move points.  It is larger than any real
   map's diameter, so the retry answers "is there a route at all", ignoring
   what the unit can afford. */
#define MOVE_UNLIMITED_ALLOWANCE 100

/* Larger than any Manhattan distance a real map can produce, so the first
   candidate tile always beats it: MOV dword ptr [EBP-0x1c],0xff at 00011c1a
   and MOV dword ptr [EBP-0xc],0xff at 00011c21.  It is a seed and not a
   sentinel -- nothing tests for it afterwards -- but it is also a bound: a
   candidate 255 or more tiles away in Manhattan terms is refused and the
   destination the caller asked for stands. */
#define MOVE_NO_CANDIDATE_YET 0xff

/* 000119e0.  The whole move command behind one call: decide where the unit can
   actually get to, pick the reachable tile closest to where it was asked to
   go, and walk it there.  Answers 1 when a walk was played and 0 when the tile
   that won is the one the unit already stands on.

   side_select is the acting unit's side as a TRUTH VALUE, not a coordinate and
   not a mode.  It is forwarded unchanged to
   fdps_move_grid_mark_opposing_zones_of_control, which reads 0 as "mark the
   units whose side byte is non-zero", and to
   fdps_move_grid_block_occupied_tiles, which reads 0 as "block the units whose
   side byte is 0" -- opposite polarities, one value, so the units whose zones
   of control stop the walk are exactly the ones whose tiles it may cross but
   not finish on.  fdps_battle_system_menu pushes a literal 1 at 00014cf5 for a
   player unit; the four AI entries forward their own second argument.

   THE GRID IS REBUILT UP TO FIVE TIMES AND ONLY TWO OF THE ROUNDS MATCH.

   Round 1 (00011a5a) is the only one that does NOT reset the grid first: it
   marks and floods over whatever the caller left in it.  Round 2, the relaxed
   retry (00011aaa), resets and then floods WITHOUT marking any zone of
   control, which is what lets it find a route through the tiles round 1
   refuses.  When that retry finds a route, the retarget rebuild (00011af4)
   resets, marks and floods with the unit's real allowance before the probe
   reads the grid -- the same setup as round 4.  Rounds 3 (00011bc6) and 4
   (00011cef) are the full sequence -- reset, mark, flood -- and round 3 alone
   also calls fdps_move_grid_block_occupied_tiles, because it is the only
   round whose result is read out as a list of tiles the unit may finish on.
   The retarget rebuild and round 4 aside, collapsing any two of them into
   one shared setup changes which tiles the walk may use.

   THE RETARGET REPLAY WALKS THE PATH FORWARDS AND SO INVERTS EVERY CODE.
   fdps_move_path_trace is called with the unit's tile as the goal and the
   destination as the start, so its codes describe steps from the destination
   back to the unit and arrive reversed (movegrid.h).  Replaying them from the
   unit's tile therefore means undoing each one: 0 becomes y+1, 1 becomes x-1,
   2 becomes y-1 and 3 becomes x+1, the same table src/walk.c dispatches on.
   The fourth arm is a default (JMP at 00011b88, with no fourth compare), so
   any code above 2 steps right.

   THE PROBE READS THE MOVE GRID AND NOT THE TERRAIN.  Each replayed tile is
   fed to fdps_map_load_tile_info purely so that
   data_fdps_map_current_move_grid_marker republishes that cell's flood-fill
   cost, and the tile is kept when the cost is anything but the 0xff
   unreachable sentinel.  The retarget rebuild flooded with the unit's REAL
   allowance, so what the loop does is slide the request back along the
   blocked route to the furthest tile the unit can pay for.  Every step is tested and the LAST one
   that passes wins, not the first.

   ABS IS CALLED FIVE TIMES A TILE AND THE TWO EXPRESSIONS SHARE NOTHING.
   00011c6c, 00011c7d, 00011c91, 00011ca2 and 00011cad: the Manhattan distance
   and the skew each recompute both absolute differences, and the skew takes an
   absolute value of its own.  The flag set carries no -oi,
   so __INLINE_FUNCTIONS__ is not defined and stdlib.h leaves abs a real call
   (rebuild_info/build_flags.md).  Both differences are measured against the
   REQUESTED destination in [EBP+0x14] / [EBP+0x18] -- which the retarget above
   may already have moved -- and never against the best candidate so far.

   THE TIE-BREAK IS STRICT AT BOTH LEVELS AND BOTH COMPARES ARE SIGNED.  JL at
   00011cbe takes a strictly smaller distance; on an exact tie JL at 00011cce
   takes a strictly smaller skew.  Equal on both loses, so the earliest tile in
   the collect order -- row-major, so the topmost and then the leftmost --
   keeps the win.  The skew is abs(abs(dx) - abs(dy)): among tiles the same
   number of steps away it prefers one lying on the diagonal towards the target
   over one straight out along a row or a column.

   THE FINAL TEST ASKS FOR A NON-ZERO COUNT, NOT A POSITIVE ONE.  CMP dword ptr
   [EBP-0x44],0x0 / JZ at 00011d3f: a step count of -1 -- what
   fdps_move_path_trace answers when the chosen tile is unreachable -- is
   handed to fdps_animate_move_path, where the signed loop bound turns it into
   no steps at all, and this function still reports 1.  Spelling the test as
   > 0 would report 0 there instead.  The case is not reachable in practice:
   the flood always costs the unit's own tile at 0 and
   fdps_move_grid_block_occupied_tiles exempts the acting unit, so that tile is
   always in the collect list and always beats the 0xff seed, which leaves the
   chosen tile reachable by construction.

   Both tile bytes and the direction byte widen through XOR EAX,EAX / MOV AL
   (00011b57, 00011c4c, 00011c5d), so 0xff is 255 everywhere and never -1.

   The two buffers are freed on the one exit path; there is no early return
   anywhere in the body. */
int fdps_battle_move_unit_toward(int dest_x, int dest_y, int unit_index,
                                 int side_select)
{
    /* The acting unit's record, and its row of the PROMAP.DAT class table. */
    struct fdps_unit_record *unit;
    struct fdps_class_record *class_move_cost;
    /* malloc'd: one direction code per step of the traced route. */
    unsigned char *step_path;
    /* malloc'd: the (x, y) byte pairs of every tile the unit can reach. */
    unsigned char *reachable_coords;
    /* The unit's movement allowance, record byte +0x3b. */
    int move_points;
    /* The tile the unit is standing on now. */
    int start_x;
    int start_y;
    /* The unit's class code, record byte +0x20; the table row is this plus
       one. */
    int class_index;
    /* What fdps_move_path_trace last answered: steps, or -1 for no route. */
    int step_count;
    /* How many tiles fdps_map_grid_collect_marked_tiles listed. */
    int reachable_count;
    /* Cursors into the two lists above. */
    int step_index;
    int tile_slot;
    /* The direction code of the step being replayed. */
    int direction_code;
    /* Where the replay has walked to so far. */
    int walk_x;
    int walk_y;
    /* The tile the unit will actually be sent to. */
    int chosen_x;
    int chosen_y;
    /* The candidate tile this pass of the selection loop is judging. */
    int tile_x;
    int tile_y;
    /* That candidate's Manhattan distance from the destination, and how far
       off the diagonal towards it the candidate sits. */
    int tile_distance;
    int tile_skew;
    /* The same two measures for the best candidate seen so far.  The
       initialiser is the store at 000119ec, which the assignment before the
       selection loop makes again; -od keeps both. */
    int best_distance = MOVE_NO_CANDIDATE_YET;
    int best_skew;
    /* The answer: 1 when a walk was played. */
    int moved;

    unit = fdps_get_unit_record(unit_index);
    move_points = (int) unit->move;
    start_x = (int) unit->pos_x;
    start_y = (int) unit->pos_y;
    class_index = (int) unit->clazz;
    class_move_cost = fdps_get_class_record(class_index + 1);

    step_path = malloc(MOVE_STEP_PATH_BYTES);
    reachable_coords = malloc(MOVE_REACHABLE_COORD_BYTES);

    /* Round 1: can the unit reach the tile it was asked for, at its own
       expense and around the zones of control? */
    fdps_move_grid_mark_opposing_zones_of_control(side_select);
    fdps_move_grid_flood_fill_range(class_move_cost, start_x, start_y,
                                    move_points);
    step_count = fdps_move_path_trace(start_x, start_y, step_path,
                                      dest_x, dest_y, 0);

    if (step_count == -1) {
        /* Round 2: is there a route at all, ignoring both the allowance and
           the zones of control? */
        fdps_map_grid_reset();
        fdps_move_grid_flood_fill_range(class_move_cost, start_x, start_y,
                                        MOVE_UNLIMITED_ALLOWANCE);
        step_count = fdps_move_path_trace(start_x, start_y, step_path,
                                          dest_x, dest_y, 1);

        if (step_count != -1) {
            /* Put the real range back under the probe, then slide the request
               along that route to the furthest tile inside it. */
            fdps_map_grid_reset();
            fdps_move_grid_mark_opposing_zones_of_control(side_select);
            fdps_move_grid_flood_fill_range(class_move_cost, start_x, start_y,
                                            move_points);

            walk_x = start_x;
            walk_y = start_y;
            chosen_x = dest_x;
            chosen_y = dest_y;

            for (step_index = 0; step_index < step_count; step_index++) {
                direction_code = (int) step_path[step_index];
                if (direction_code == 0) {
                    walk_y++;
                } else if (direction_code == 1) {
                    walk_x--;
                } else if (direction_code == 2) {
                    walk_y--;
                } else {
                    walk_x++;
                }

                fdps_map_load_tile_info(walk_x, walk_y);
                if (data_fdps_map_current_move_grid_marker != 0xff) {
                    chosen_x = walk_x;
                    chosen_y = walk_y;
                }
            }

            dest_x = chosen_x;
            dest_y = chosen_y;
        }
    }

    /* Round 3: the range the unit may actually finish its move inside, read
       out as a flat list of tiles. */
    fdps_map_grid_reset();
    fdps_move_grid_mark_opposing_zones_of_control(side_select);
    fdps_move_grid_flood_fill_range(class_move_cost, start_x, start_y,
                                    move_points);
    fdps_move_grid_block_occupied_tiles(unit_index, side_select);
    reachable_count = fdps_map_grid_collect_marked_tiles(reachable_coords);

    chosen_x = dest_x;
    chosen_y = dest_y;
    best_distance = MOVE_NO_CANDIDATE_YET;
    best_skew = MOVE_NO_CANDIDATE_YET;

    for (tile_slot = 0; tile_slot < reachable_count; tile_slot++) {
        tile_x = (int) reachable_coords[tile_slot * 2];
        tile_y = (int) reachable_coords[tile_slot * 2 + 1];

        tile_distance = abs(tile_x - dest_x) + abs(tile_y - dest_y);
        tile_skew = abs(abs(tile_x - dest_x) - abs(tile_y - dest_y));

        if (tile_distance < best_distance
            || (tile_distance == best_distance && tile_skew < best_skew)) {
            chosen_x = tile_x;
            chosen_y = tile_y;
            best_distance = tile_distance;
            best_skew = tile_skew;
        }
    }

    /* Round 4: the range once more, this time to trace the route the walk
       plays back.  The occupied tiles are NOT taken out of it here, so the
       route may cross the ally round 3 refused to let the unit stop on. */
    fdps_map_grid_reset();
    fdps_move_grid_mark_opposing_zones_of_control(side_select);
    fdps_move_grid_flood_fill_range(class_move_cost, start_x, start_y,
                                    move_points);
    step_count = fdps_move_path_trace(start_x, start_y, step_path,
                                      chosen_x, chosen_y, 0);
    fdps_map_grid_reset();

    if (step_count != 0) {
        fdps_animate_move_path(unit_index, step_path, step_count);
        moved = 1;
    } else {
        moved = 0;
    }

    free(step_path);
    free(reachable_coords);

    return moved;
}
