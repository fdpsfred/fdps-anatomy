/* mapai.c -- the map AI: one computer-controlled actor's behaviour on its
 * turn, and the map queries that decision needs.
 *
 * See mapai.h for what each entry point promises.  Nothing here owns state:
 * the map layers, the movement grid and the unit records all belong to other
 * files, and everything below works on what those pointers hold.
 */
#include "gamedata.h"
#include "maptile.h"
#include "mapai.h"

/* 00013e10.  The map dimensions come from the MOVEMENT GRID's header -- MOVSX
   word ptr [EAX] and MOVSX word ptr [EAX+2] on data_fdps_battle_move_grid_ptr
   at 00013e21 and 00013e2c -- and not from the terrain layer's header at +7,
   which is the width fdps_map_load_tile_info goes on to index all three layers
   with.  The two agree in practice because the layers cover the same map; the
   scan simply trusts that, and there is no bounds check and no null test
   anywhere in the function, on the grid pointer or on anything else.

   Both header words are read with MOVSX, signed, and the loop tests are JL,
   the signed compare.  A header word of 0xffff has to come out as -1 so the
   loop body never runs; read unsigned it would walk 65535 rows.

   Both dimensions are latched into their own stack slots before the loops
   ([EBP-0xc] and [EBP-8]) and nothing in the body writes either, so they are
   two ordinary locals and not a per-iteration reload.

   The kind test is CMP EAX,0x20 after AND AL,0x60 -- an equality on the
   two-bit field, not a bit test.  The obvious (attr & 0x20) also accepts kind
   0x60, and widening it to the 0x20/0x40 pair that the player-side cursor
   search accepts adds buried treasure, which the original never sends an AI
   actor to (rebuild_info/pitfalls.md).

   The event code is compared as a full int: MOVSX EAX,word ptr [0x00069d06]
   then CMP EAX,[EBP+0x14].  The global is signed and only ever receives a
   zero-extended cell byte, so the comparison is against 0..255.

   The coordinates go out as byte stores, MOV byte ptr [EDX] and MOV byte ptr
   [EDX+1]: the caller's buffer is two bytes and nothing past them is written.

   The return sense is 0 for found and -1 for not found, which is what the
   caller's TEST EAX,EAX / JZ at 000102d0 gates on. */
int fdps_map_find_chest_cell(int cell_code, unsigned char *out_xy)
{
    int grid_width;
    int grid_height;
    int tile_x;
    int tile_y;

    grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
    grid_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2);

    for (tile_y = 0; tile_y < grid_height; tile_y++) {
        for (tile_x = 0; tile_x < grid_width; tile_x++) {
            fdps_map_load_tile_info(tile_x, tile_y);

            if ((data_fdps_map_current_tile_attr_flags & 0x60) == 0x20 &&
                (int) data_fdps_map_current_cell_event_code == cell_code) {
                out_xy[0] = (unsigned char) tile_x;
                out_xy[1] = (unsigned char) tile_y;
                return 0;
            }
        }
    }

    return -1;
}
