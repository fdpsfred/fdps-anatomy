/* maptile.h -- reading one battle map cell out of the loaded map layers into
 * the shared tile-info block at 00069d04..00069d0c.
 *
 * A battle map is three parallel layers over the same tile grid, each a heap
 * block with its own header:
 *
 *   the scene terrain layer, data_fdps_scene_layer_tile_map_ptrs[0]:
 *       signed 16-bit tile width at +7, 16-bit tile ids at +0xb in row-major
 *       order, so cell (x, y) is at base + 0xb + 2 * (y * width + x);
 *   the movement grid, data_fdps_battle_move_grid_ptr:
 *       four-byte header then two-byte struct fdps_move_grid_cell at +4
 *       (see movegrid.h);
 *   the cell event-code layer, data_fdps_map_cell_event_code_layer_ptr:
 *       struct fdps_map_cell_code_layer, one byte per cell at +0x10.
 *
 * The terrain layer's tile id then indexes the tileset attribute table,
 * data_fdps_scene_layer_tile_attr_ptr[0], whose 4-byte struct
 * fdps_tile_attr_entry rows start at +0x11.
 *
 * Nothing in this file owns a layer: they are allocated and filled by the
 * chapter resource loader, and everything here works on what those pointers
 * hold.
 */
#ifndef MAPTILE_H
#define MAPTILE_H

/* 00069d0a.  Byte 1 of the current tile's attribute row -- the blend_level
   field of struct fdps_tile_attr_entry.  fdps_map_load_tile_info writes it and
   no instruction in the image ever reads it back, which is why the global is
   named for what it is rather than for what it does; the field it is copied
   from is named, because the attribute row's own layout is settled.

   The definition arrives with ticket 23 like every other data_fdps_ global. */
extern unsigned char data_fdps_map_current_tile_attr_reserved;

/* Latches everything the rest of the battle code wants to know about one map
   cell into the shared tile-info globals, and returns nothing: the tile id at
   (tile_x, tile_y), the four bytes of that tile's attribute row, the cell's
   movement-grid marker byte and its event code.

   There is no bounds check and no null-pointer check anywhere in it, on any of
   the four blocks it dereferences.  Every caller is battle-time code running
   after a chapter has loaded. */
extern void fdps_map_load_tile_info(int tile_x, int tile_y);
#pragma aux fdps_map_load_tile_info "*" parm caller [];

/* Reports that a unit is on the cell at (tile_x, tile_y) and, if that cell
   carries a scripted chapter event which fires on the occasion being reported,
   leaves the event's handler index in data_fdps_chapter_pending_event_idx
   (gamedata.h) for the caller to dispatch.  Nothing else is written, no handler
   is called from here, and the slot is left alone when the cell has no event to
   report -- the caller seeded it with 0xff before the unit acted.

   The cell's own two-byte entry lives in the resident MAP%02d.DAT block, in the
   table that begins at image offset 0x33 and is indexed by the cell's event
   code minus one.  Byte 0 of the entry is the handler index, 0xff meaning the
   cell has no handler; byte 1 is the occasion the entry fires on.

   trigger_kind is the occasion being reported and is matched against that byte
   for equality: 0 as a unit finishes stepping onto the cell during movement, 1
   when a unit ends its turn on it.  Those are the only two values passed
   anywhere in the image.

   A cell whose attribute flags carry either of the bits 0x60 is a searchable
   cell, investigated from the action menu through a different table, and is
   reported as carrying nothing here whatever its event code says. */
extern void fdps_map_set_pending_tile_event(int tile_x, int tile_y,
                                            int trigger_kind);
#pragma aux fdps_map_set_pending_tile_event "*" parm caller [];

#endif
