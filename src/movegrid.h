/* movegrid.h -- the battle map's working movement grid: resetting it, marking
 * zones of control on it, flooding a movement range over it and tracing a path
 * back out of it.
 *
 * The grid is one heap block reached through data_fdps_battle_move_grid_ptr.
 * Its first four bytes are a header -- signed 16-bit tile width at +0, signed
 * 16-bit tile height at +2 -- and the width*height cells follow at +4 in
 * row-major order, two bytes each: cell (x, y) is at base + 4 + 2 * (y * width
 * + x).  A cell is struct fdps_move_grid_cell in src/fdpstype.h.
 *
 * Byte 0 of a cell is the zone-of-control flag byte: 0x40 means a unit stands
 * on this tile and it may not be entered, 0x80 means the tile adjoins a unit
 * so movement entering it must stop.  Byte 1 is the flood fill's marker, which
 * the fill relaxes downwards from the 0xff unreachable sentinel.
 *
 * fdps_field_load_chapter_resources allocates the block as 4 + width*height*2
 * bytes and writes the header; everything in this file works on what that
 * pointer holds and owns no state of its own.
 */
#ifndef MOVEGRID_H
#define MOVEGRID_H

#include "fdpstype.h"

/* 00063c50 and 00063930.  The flood fill's frontier: the tile columns and the
   tile rows of the cells one wave of fdps_move_grid_flood_fill_range has to
   expand from.  Each is a pair of 400-entry queues laid end to end -- buffer 0
   at index 0 and buffer 1 at index 400 -- and the fill alternates between them,
   reading the wave it is on out of one while appending the next wave into the
   other.  A one-byte entry, so a tile index above 255 could not be queued; no
   map in the game is that wide.

   Each is ONE array of 800 and not two of 400 (rebuild_info/pitfalls.md,
   contract B): the fill forms every index as buffer * 400 + slot, and a wave
   longer than 400 entries runs off the end of one buffer into the other, which
   only stays inside the object while the two halves are one declaration.  The
   two arrays are adjacent in bss -- 00063930 + 800 is 00063c50 -- and nothing
   indexes from one into the other, so they stay two globals.

   Nothing outside src/movegrid.c reads either of them: they are scratch for
   the one function, not a result anybody collects.  The definitions arrive
   with ticket 23 like every other data_fdps_ global. */
extern unsigned char data_fdps_battle_move_frontier_x[800];
extern unsigned char data_fdps_battle_move_frontier_y[800];

/* Blanks every cell of the grid so a new movement range or target mask can be
   computed over it: clears the two zone-of-control bits of byte 0, preserving
   the low six bits, and stores the 0xff unreachable sentinel into byte 1.
   Does nothing at all when the grid has not been allocated. */
extern void fdps_map_grid_reset(void);
#pragma aux fdps_map_grid_reset "*" parm caller [];

/* Marks one unit's zone of control into the grid: ORs 0x80 into each of the
   four orthogonal neighbours of (tile_x, tile_y) that exists, then ORs 0x40
   into the tile itself.  Both bits are sticky -- only fdps_map_grid_reset
   clears them -- so calling this once per unit accumulates every unit's zone
   into the one grid.  Does nothing at all when the grid has not been
   allocated. */
extern void fdps_move_grid_mark_zone_of_control(int tile_x, int tile_y);
#pragma aux fdps_move_grid_mark_zone_of_control "*" parm caller [];

/* ORs the movement-stops-here bit 0x80 into the single cell (tile_x, tile_y),
   leaving every other bit of the cell alone.  A tile carrying it may be
   entered but movement ends on it; only fdps_map_grid_reset clears it again.
   Neither coordinate is bounds-checked and the grid pointer is not checked for
   null: the cell is addressed and written unconditionally.  Nothing in the
   original image calls this. */
extern void fdps_move_grid_set_stop_flag(int tile_x, int tile_y);
#pragma aux fdps_move_grid_set_stop_flag "*" parm caller [];

/* Marks the zone of control of every unit on the side opposite the caller's
   into the grid, so the movement range flooded over it afterwards cannot be
   walked through them.  side_select is a TRUTH VALUE, not a side number: 0
   marks every unit whose side byte is non-zero, any non-zero value marks every
   unit whose side byte is 0.  Retired units (flags bit 0) are passed over.
   Callers reset the grid first, and a caller that wants the whole map's zones
   calls this twice, with 0 and then 1. */
extern void fdps_move_grid_mark_opposing_zones_of_control(int side_select);
#pragma aux fdps_move_grid_mark_opposing_zones_of_control "*" parm caller [];

/* Floods one unit's movement range over the grid: starting from the tile the
   unit stands on, relaxes the accumulated movement cost stored in byte 1 of
   every cell the unit can still afford to enter, so
   fdps_map_grid_collect_marked_tiles below reads out the range and
   fdps_move_path_trace walks the same bytes to build a path.

   class_move_cost is the acting unit's class record and only its move_cost[8]
   is touched: the cost of entering a tile is move_cost[terrain type of that
   tile].  move_points is the allowance, and a tile stays reachable while its
   accumulated cost is <= it.  Callers that want the whole map covered rather
   than one unit's real allowance pass 0x64.

   The two zone-of-control bits the marks above leave in byte 0 are read here:
   0x40 refuses the cell outright, 0x80 stores move_points into it instead of
   the cost that was computed.  Expects fdps_map_grid_reset to have run first,
   so every cell holds the 0xff sentinel a candidate cost can undercut.

   Neither the grid pointer nor the two scene layer pointers are checked, and
   neither start coordinate is range checked. */
extern void fdps_move_grid_flood_fill_range(
                struct fdps_class_record *class_move_cost,
                int start_x, int start_y, int move_points);
#pragma aux fdps_move_grid_flood_fill_range "*" parm caller [];

/* Takes the tiles that occupied units stand on back out of the movement range
   that has just been flooded over the grid, by storing the 0xff unreachable
   sentinel into byte 1 of each of their cells, so the moving unit may cross
   them but may not end its move on one.  The unit at exclude_unit_index keeps
   its own tile, so standing still stays legal, and retired units (flags bit 0)
   are passed over.  side_select is a TRUTH VALUE, not a side number: 0 blocks
   the units whose side byte is 0, any non-zero value blocks the units whose
   side byte is non-zero -- the opposite polarity to the identically named
   argument of fdps_move_grid_mark_opposing_zones_of_control above.  Neither
   the unit coordinates nor the grid pointer are checked. */
extern void fdps_move_grid_block_occupied_tiles(int exclude_unit_index,
                                                int side_select);
#pragma aux fdps_move_grid_block_occupied_tiles "*" parm caller [];

/* Reads the grid out into a flat list: writes one (x, y) pair of tile indices,
   x first, into out_coords for every cell whose marker byte is not the 0xff
   unreachable sentinel, and returns how many pairs that was.  The cells are
   visited in row-major order, so the pairs come out row by row.  The grid is
   not modified and the grid pointer is not checked for null.  Nothing bounds
   how many pairs are written: the caller's buffer has to be big enough for
   every cell of the map, and two of the callers in the original image hand
   over one that is not. */
extern int fdps_map_grid_collect_marked_tiles(unsigned char *out_coords);
#pragma aux fdps_map_grid_collect_marked_tiles "*" parm caller [];

/* Reads a route back out of the costs the flood fill left in byte 1 of every
   cell.  Three jobs behind one entry, selected by mode, which is read as one
   byte and is 0, 1 or 2 at every call site in the image:

   Modes 0 and 1 walk from (start_x, start_y) to (goal_x, goal_y), one
   orthogonal step at a time, taking the cheapest of the four neighbours that
   exist.  Each step appends a direction code -- 0 = y-1, 1 = x+1, 2 = y+1,
   3 = x-1 -- and the codes land in out_path in REVERSE order, last step first;
   the return is how many there are.  The two modes differ only over ties: mode
   0 takes a neighbour on a strictly lower cost alone, while mode 1 also takes
   an equal one as long as the previously recorded step used a different code,
   so its ties turn where mode 0's run straight.  Returns -1 without touching
   out_path when the start tile holds the 0xff unreachable sentinel.  The walk
   ends only by standing on the goal, so a grid whose costs do not lead there
   never terminates, and out_path has to have room for every step: the
   direction codes are staged in a 100-byte frame buffer with no bound.

   Mode 2 ignores goal_x, goal_y and start_y, and start_x is a TRUTH VALUE
   selecting a side rather than a coordinate: 0 keeps the units whose side byte
   is non-zero, any non-zero value keeps the units whose side byte is 0.  Of
   those, skipping the retired (flags bit 0), it finds the one standing on the
   lowest-cost cell -- ties going to the lower unit index -- writes that unit's
   tile column and row into out_path[0] and out_path[1] and returns the cost.
   A unit on an unreachable cell never matches, and -1 comes back when none
   did.

   The grid pointer is not checked for null and neither start coordinate is
   checked against the header. */
extern int fdps_move_path_trace(int goal_x, int goal_y,
                                unsigned char *out_path,
                                int start_x, int start_y, unsigned char mode);
#pragma aux fdps_move_path_trace "*" parm caller [];

/* Moves the unit at unit_index as close as it can get to (dest_x, dest_y) and
   answers whether a walk was played: 1 when it was, 0 when the tile that won
   is the one the unit already stands on.  The move is committed -- the walk is
   animated and the unit record's tile position advanced -- before it returns.

   side_select is the acting unit's side as a TRUTH VALUE and is forwarded
   unchanged to fdps_move_grid_mark_opposing_zones_of_control and
   fdps_move_grid_block_occupied_tiles above, whose readings of it are opposite
   in polarity: the units whose zones of control stop the walk are exactly the
   ones whose tiles the walk may cross but not finish on.

   The destination is a request and not a promise.  When no affordable route to
   it exists the function retries with the allowance and the zones of control
   lifted and, if that finds a route, slides the request back along it to the
   furthest tile the unit can really pay for.  Either way it then picks, out of
   every tile the unit can finish its move on, the one with the smallest
   Manhattan distance to the destination, ties going to the tile nearest the
   diagonal towards it and then to the earliest in row-major order.

   The grid is left reset.  Four separate (reset, mark, flood) rounds are run
   over it and the last thing done before the walk is a reset, so nothing a
   caller staged in the grid survives the call and nothing it computed is left
   behind for the caller to read. */
extern int fdps_battle_move_unit_toward(int dest_x, int dest_y, int unit_index,
                                        int side_select);
#pragma aux fdps_battle_move_unit_toward "*" parm caller [];

#endif
