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
   grid is not an error here, it is a silent return, and the callers rely on
   that -- fdps_battle_action_menu and fdps_deploy_unit call this before any
   chapter has been loaded.

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
