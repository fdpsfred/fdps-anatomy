/* mapcur.c -- the battle map cursor: drawing its overlay, the information
 * panel beside it, the select loop and the animated move between tiles.
 *
 * See mapcur.h for what each cursor mode paints.  Everything here works out of
 * the shared cursor globals in gamedata.h and owns no state of its own.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "mapcur.h"
#include "movegrid.h"
#include "sprite.h"

/* One map tile is 24 world pixels on each axis: the 0x18 that every displaced
   call site adds or subtracts, and the divisor of the mode 6 arm. */
#define CURSOR_TILE_STEP 0x18

/* The movement grid's cells start after its four-byte header (movegrid.h). */
#define MOVE_GRID_CELL_BASE 4

/* 0002c6a0.  A chain of six CMP dword ptr [0x00069cd0] / JNZ tests at
   0002c6ac, 0002c6d4, 0002c6fc, 0002c79c, 0002c930 and 0002cbc1, each arm
   ending in a JMP to the shared epilogue at 0002cc12.  A mode the chain does
   not name -- 0, or anything above 6 -- falls out of the last JNZ straight
   onto that epilogue, so it is silently nothing rather than an error.

   THE SHAPES ARE CALL LISTS, NOT LOOPS.  Every arm is written out one
   fdps_blit_cursor_tile call per cell in the original, and they are not the
   shapes a loop over a diamond would produce: mode 5 leaves the four tiles
   orthogonally adjacent to the centre blank, and its two lower diagonal cells
   take sprite 0x11 on the left and 0x10 on the right -- the only place in the
   whole list where the sprite ids do not run in reading order.  Folding any of
   this into a loop over a radius, or assuming the ids ascend with position,
   changes what the player sees (rebuild_info/pitfalls.md).

   The cursor position globals are re-read at every call site rather than being
   copied into the frame once (MOV EAX,[0x00069ccc] appears at each displaced
   push), and fdps_blit_cursor_tile writes to neither of them, so a single copy
   would be the same value each time.  They are spelled as the reads the
   original makes.

   MODE 6 IS THE TRAP.  This function is called from the frame compositor
   fdps_draw_scene_layers, and mode 6 draws nothing whatever: it divides both
   cursor coordinates by the tile size and clears byte 1 of the movement grid
   cell under the cursor -- the marker byte fdps_map_grid_reset fills with the
   0xff unreachable sentinel and fdps_map_grid_collect_marked_tiles reads to
   decide a cell is marked.  fdps_map_cursor_move_to draws a frame at every
   step of a cursor move while the mode is non-zero, so the tiles the cursor
   travels across are marked as a side effect of rendering them.  Hoisting the
   mode 6 arm out of the renderer, or drawing a different number of frames
   during a move, silently changes which tiles end up marked.

   Both divisions are signed -- SAR EDX,0x1f / IDIV EBX at 0002cbda and
   0002cbfd -- so a cursor pixel left of or above the map origin truncates
   towards zero and lands on tile 0, where an unsigned division would produce a
   tile index of hundreds of millions and write off the end of the block.  The
   grid width comes out of the header with MOVSX at 0002cbe5, signed for the
   same reason.

   The grid pointer is NOT checked for null here, unlike every function in
   src/movegrid.c: the original loads it at 0002cbdf and dereferences it
   immediately.  Nothing sets mode 6 before a chapter has been loaded, so the
   check the original leaves out is left out. */
void fdps_draw_map_cursor(unsigned char *scene_buffer)
{
    struct fdps_move_grid_cell *grid_cells;
    int grid_width;

    if (data_fdps_map_cursor_draw_mode == 1) {
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              0, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 2) {
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              1, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 3) {
        /* Radius-1 diamond: centre, then above, left, right, below. */
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              0x0e, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              2, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              3, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              4, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              5, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 4) {
        /* Radius-2 diamond, 13 tiles: centre, the four tips two tiles out,
           the four diagonals, then the four tiles one out. */
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              1, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              2, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              3, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              4, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              5, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              6, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              7, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              8, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              9, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              0x0a, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              0x0b, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              0x0c, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              0x0d, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 5) {
        /* Radius-3 diamond, 21 tiles: centre, the four tips three tiles out,
           the eight (1,2) and (2,1) cells of that outer ring in four sprite
           pairs, the four tips two tiles out, and the four diagonals one out
           -- the last of which take 0x0e / 0x0f above and 0x11 / 0x10 below,
           out of numeric order.  The four tiles orthogonally adjacent to the
           centre get no call at all. */
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              1, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 3,
                              2, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 3,
                              data_fdps_map_cursor_world_y,
                              3, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 3,
                              data_fdps_map_cursor_world_y,
                              4, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 3,
                              5, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              6, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              6, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              7, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              7, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              8, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              8, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              9, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              9, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              0x0a, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              0x0b, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              0x0c, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              0x0d, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              0x0e, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              0x0f, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              0x11, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              0x10, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 6) {
        grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
        grid_cells = (struct fdps_move_grid_cell *)
                     (data_fdps_battle_move_grid_ptr + MOVE_GRID_CELL_BASE);
        grid_cells[(data_fdps_map_cursor_world_y / CURSOR_TILE_STEP)
                       * grid_width
                   + data_fdps_map_cursor_world_x / CURSOR_TILE_STEP].marker =
            (unsigned char) 0;
    }
}
