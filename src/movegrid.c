/* movegrid.c -- the battle map's working movement grid.
 *
 * See movegrid.h for the block's layout and what a cell byte means.  Nothing
 * here owns state: every function reads the grid through
 * data_fdps_battle_move_grid_ptr and works in place on what it finds.
 */
#include <stddef.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "maptile.h"
#include "movegrid.h"

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
   function at 00010da0.  The binary keeps that copy but nothing calls it: the
   build used -od, so the compiler did no inlining and the expansion is in the
   original source, whichever way it was spelled there.  Emitting a call would
   put a CALL where the original has none.

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
