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

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* 00069d0a. Starts at zero in the image (bss); it is only ever stored by
   fdps_map_load_tile_info from tile attribute byte +1 and never read, so the
   initial value is unobservable. */
unsigned char data_fdps_map_current_tile_attr_reserved;

/* End of global data. */

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

/* 0002e030.  Everything this decides comes out of the tile-info block that the
   call above republishes, so the two functions are read together.

   Three things are behaviour rather than style.

   The attribute gate tests two bits and not a value.  MOV AL,[0x00069d08] /
   AND AL,0x60 / TEST EAX,EAX at 0002e04c rejects a cell whose 0x60 field holds
   0x20, 0x40 or 0x60 alike, and lets every other bit of the byte through
   untouched; a rebuild that compared the field against one searchable-cell
   constant would let the other one report an event.

   The entry offset is 0x31 and not 0x33.  The table starts at image offset
   0x33 and is indexed by the event code minus one, and the original spends
   neither instruction on that: it scales the raw code by two and folds the
   -1 into the displacement (ADD EAX,EAX / MOV AL,byte ptr [EDX + 0x31] at
   0002e06d).  Code 0 never reaches the arithmetic because it is the "no event"
   sentinel the gate above rejects, so the byte pair at 0x31 is never read.

   The two entry bytes are both loaded before either is tested, and each of the
   two loads re-reads the base pointer out of 0006013c (0002e06f and 0002e088).
   Neither costs anything to reproduce and both are kept: the loads are of a
   block nothing here writes, so the order and the count are unobservable, and
   two locals is what the original's own stack slots at [EBP-8] and [EBP-4]
   say it was written as. */
void fdps_map_set_pending_tile_event(int tile_x, int tile_y, int trigger_kind)
{
    int handler_index;
    int entry_trigger_kind;

    fdps_map_load_tile_info(tile_x, tile_y);

    if ((data_fdps_map_current_tile_attr_flags & 0x60) != 0) {
        return;
    }
    if (data_fdps_map_current_cell_event_code == 0) {
        return;
    }

    handler_index = data_fdps_tile_event_data_table_ptr
                    [data_fdps_map_current_cell_event_code * 2 + 0x31];
    entry_trigger_kind = data_fdps_tile_event_data_table_ptr
                         [data_fdps_map_current_cell_event_code * 2 + 0x32];

    if (handler_index != 0xff && entry_trigger_kind == trigger_kind) {
        data_fdps_chapter_pending_event_idx = (unsigned int) handler_index;
    }
}

/* 0002e910.  Two nested walks over the map, one call to the reader above per
   cell, and two stores on the cells that pass the gate.  No argument is read,
   nothing is returned and the epilogue purges nothing.

   Five things here are behaviour rather than style.

   The 0x60 field of the attribute byte is a four-value enumeration and not two
   independent bits, which is how its other readers treat it:
   fdps_map_find_chest_cell wants exactly 0x20 (CMP EAX,0x20 at 00013e84) and
   fdps_battle_search_cell_at_cursor wants non-zero but not 0x60 (00018550 and
   00018556).  This pass takes 0x20 and 0x60 and leaves 0x00 and 0x40 alone,
   which the assembly spells as the two equality tests at 0002e9a1 and 0002e9a7
   and which is written the same way below.  A single (kind & 0x20) test selects
   the same two values out of the four the field can hold, so the two spellings
   are equivalent (ADR-0001).

   The two widths are two variables.  The tile id is bumped at the terrain
   layer's width, from [EBP-0x10] (IMUL at 0002e9be), and the event-code byte is
   cleared at the event layer's own width, from [EBP-8] (IMUL at 0002e9de).  The
   two widths differ on every shipped map (resource_info/terrain.md), so a
   rebuild that used one width for both would clear the wrong event cells.

   The bump is a 16-bit increment, INC word ptr [EAX] at 0002e9d8: a cell
   holding 0xffff wraps to 0 and the neighbouring cell's id is untouched.

   Both header dimensions are MOVSX and both loop bounds are JL, so a dimension
   of 0xffff is -1 and the walk does nothing; read unsigned it would run 65535
   rows off the end of the block.

   The table index is the cell's event code with no bound test on it -- see
   data_fdps_map_cell_event_triggered_flags in gamedata.h for why the shipped
   maps keep it inside the 32 entries. */
void fdps_map_apply_triggered_cell_changes(void)
{
    struct fdps_map_cell_code_layer *event_layer;
    short *tile_cell;
    int map_width;
    int map_height;
    int event_layer_width;
    int searchable_kind;
    int cell_event_code;
    int tile_x;
    int tile_y;

    map_width = (int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0] + 7);
    map_height = (int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0] + 9);
    event_layer = (struct fdps_map_cell_code_layer *)
                  data_fdps_map_cell_event_code_layer_ptr;
    event_layer_width = (int) event_layer->width;

    for (tile_y = 0; tile_y < map_height; tile_y++) {
        for (tile_x = 0; tile_x < map_width; tile_x++) {
            fdps_map_load_tile_info(tile_x, tile_y);

            searchable_kind = data_fdps_map_current_tile_attr_flags & 0x60;
            cell_event_code = data_fdps_map_current_cell_event_code;

            if ((searchable_kind == 0x20 || searchable_kind == 0x60) &&
                data_fdps_map_cell_event_triggered_flags[cell_event_code]
                    != 0) {
                tile_cell = (short *) (data_fdps_scene_layer_tile_map_ptrs[0] +
                                       0xb +
                                       2 * (tile_y * map_width + tile_x));
                (*tile_cell)++;
                event_layer->cells[tile_y * event_layer_width + tile_x] = 0;
            }
        }
    }
}
