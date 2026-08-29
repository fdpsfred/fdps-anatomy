/* maptile.c -- reading one battle map cell out of the loaded map layers.
 *
 * See maptile.h for the three layers and the attribute table this reads, and
 * for what the shared tile-info block at 00069d04..00069d0c holds.  Nothing
 * here owns state: the layers are the chapter loader's, and the tile-info
 * globals are a scratch block every battle routine reads straight after
 * calling in.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "maptile.h"

/* 0002ba00.  Six globals written, nothing returned, no callee and no branch:
   the whole body is one straight run of loads and stores, which is why there
   is no control flow below to match against the assembly.

   Three things here are easy to write differently by accident and all three
   are behaviour.

   The tile width is MOVSX word ptr [EAX+7] -- signed.  It is multiplied by
   tile_y and the product indexes the cell array, so a header word of 0xffff
   has to come out as -1 and step backwards; read unsigned it steps 65535 rows
   forward into whatever follows the block.  The tile id at the cell is MOVSX
   too, and it is scaled by 4 (LEA EDX,[EDX*0x4 + 0x0] at 0002ba4c) to reach
   the attribute row, so its sign survives all the way into a table index.

   The movement grid is indexed with the TERRAIN layer's width, not with its
   own.  The original loads the width once into [EBP-4] at 0002ba15 and is
   still using that same slot at 0002ba89 when it computes the grid cell
   address, and the grid's own 16-bit width at offset 0 of its block is never
   read here.  The two agree in practice because both layers are laid out over
   the same map, but reaching for data_fdps_battle_move_grid_ptr's header for
   tidiness would be a different program (rebuild_info/pitfalls.md).  The event
   layer, by contrast, really does reload the width from its own header at
   0002bab0 -- that is what [EBP-4] gets overwritten with -- so the two widths
   below are two variables and not one reused slot.

   The event code is widened with XOR AH,AH, zero extension, before the 16-bit
   store: a cell byte of 0xf0 becomes 240 and not -16, and the global it lands
   in is signed, so the widening is the only thing keeping the value positive.

   The four attribute bytes are stored out of order -- +2, +0, +1, +3 -- and
   the order is kept here because it is what the original does; no two of the
   four destinations overlap, so nothing depends on it. */
void fdps_map_load_tile_info(int tile_x, int tile_y)
{
    unsigned char *tile_map;
    struct fdps_map_cell_code_layer *event_layer;
    struct fdps_tile_attr_entry *tile_attr;
    struct fdps_move_grid_cell *grid_cell;
    int map_width;
    int event_layer_width;
    int tile_id;

    tile_map = data_fdps_scene_layer_tile_map_ptrs[0];
    map_width = (int) *(short *) (tile_map + 7);
    tile_id = (int) *(short *) (tile_map + 0xb +
                                2 * (tile_y * map_width + tile_x));
    data_fdps_map_tile_info_tile_id = (short) tile_id;

    tile_attr = (struct fdps_tile_attr_entry *)
                (data_fdps_scene_layer_tile_attr_ptr[0] + 0x11 +
                 4 * tile_id);
    data_fdps_map_tile_terrain_type = tile_attr->terrain_type;
    data_fdps_map_current_tile_attr_flags = tile_attr->flags;
    data_fdps_map_current_tile_attr_reserved = tile_attr->blend_level;
    data_fdps_map_tile_combat_backdrop_id = tile_attr->combat_backdrop_id;

    grid_cell = (struct fdps_move_grid_cell *)
                (data_fdps_battle_move_grid_ptr + 4 +
                 2 * (tile_y * map_width + tile_x));
    data_fdps_map_current_move_grid_marker = grid_cell->marker;

    event_layer = (struct fdps_map_cell_code_layer *)
                  data_fdps_map_cell_event_code_layer_ptr;
    event_layer_width = (int) event_layer->width;
    data_fdps_map_current_cell_event_code =
        (short) event_layer->cells[tile_y * event_layer_width + tile_x];
}
