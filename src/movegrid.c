/* movegrid.c -- the battle map's working movement grid.
 *
 * See movegrid.h for the block's layout and what a cell byte means.  Nothing
 * here owns state: every function reads the grid through
 * data_fdps_battle_move_grid_ptr and works in place on what it finds.
 */
#include <stddef.h>
#include "fdpstype.h"
#include "gamedata.h"
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
