/* rsrc.h -- chapter resource loading and freeing, and the .CEL sprite cache.
 *
 * The readers here open a named file, pull one piece of it into a heap block
 * and hand that block to the caller through a pointer slot the caller owns.
 * None of them checks a malloc result and none of them reports a failure
 * after the open succeeded: a file that opens is assumed to be well formed.
 */
#ifndef RSRC_H
#define RSRC_H

#include <stdio.h>

/* 00064060.  Which sprite group each cache slot holds, parallel to the slot
   table at the head of the block behind data_fdps_cel_sprite_cache_ptr and
   searched linearly by fdps_cache_cel_sprite_group.  Only the first
   data_fdps_cel_sprite_cache_count entries mean anything.

   Thirty entries because thirty struct fdps_cel_cache_slot is exactly the
   0x5A0-byte slot table the cache block reserves, and because 00064060 + 120
   is where the next global starts.  Nothing bounds-checks the count against
   it. */
extern int data_fdps_cel_sprite_cache_group_ids[30];

/* 0006411c.  How much of the cache block is in use, in bytes from its base:
   the 0x5A0-byte slot table plus every group's pixel bytes appended so far,
   and so the offset the next group's pixels are read into.  Unsigned. */
extern unsigned int data_fdps_cel_sprite_cache_buffer_used;

/* Loads entry number entry_index of an offset-table archive into the caller's
   buffer slot, replacing whatever that slot held.

   The archive layout this reads is six header bytes, then one u32 absolute
   file offset per entry followed by a sentinel offset one past the last
   entry's end.  Entry entry_index therefore starts at the u32 at file offset
   6 + entry_index * 4 and runs up to the u32 after it, so the entry's size is
   the difference between the two and the table must hold the sentinel.
   entry_index is not range-checked.

   That is not the .VFS layout the rest of the game reads -- a .VFS has a
   35-byte header and a 26-byte named entry table -- and no file shipped with
   the game has this one; nothing in the image calls this function.  It is the
   packed-archive reader from the studio's previous game carried over, where
   the six header bytes are that game's "LLLLLL" archive signature and the
   sentinel is the file's own length.  This reader never looks at either.

   buffer is both in and out: a non-NULL value found there is freed on entry,
   so it must hold NULL or a malloc'd block, and on return it holds the
   malloc'd entry, which the caller then owns.

   A failed open does not come back: it prints " File not found <name>!!! "
   with a bell and exits the process with status 1. */
extern void fdps_load_indexed_archive_entry(char *filename, void **buffer,
                                            int entry_index);
#pragma aux fdps_load_indexed_archive_entry "*" parm caller [];

/* Makes sure the twelve sprites of group group_index are in the global CEL
   cache and answers which cache slot they are in.  fp is an already open .CEL
   sheet, positioned anywhere: the reader seeks for itself and leaves the
   position where its last read ended.  It never closes fp.

   A .CEL sheet is a 15-byte struct fdps_cel_header followed at +0x0f by one
   u32 absolute file offset per sprite plus a sentinel one past the last
   sprite's stream.  Sprites are grouped twelve to a unit icon set (four
   facings of three walk frames), so group g owns table entries g*12 .. g*12+11
   and its pixel bytes run from entry g*12 up to entry g*12+12.

   The cache is one heap block: a slot table of thirty struct
   fdps_cel_cache_slot at its base, then every cached group's pixel bytes
   appended in the order the groups were asked for.  Each slot holds the twelve
   sprite streams' offsets rewritten as offsets from the block's own base, so a
   drawer adds data_fdps_cel_sprite_cache_ptr and has the stream.

   Asking for a group that is already cached costs a re-read of the sheet's
   offset table and nothing else -- the block is not touched and the existing
   slot comes back.  A group that is not cached is appended: the block is
   grown, so every pointer a caller was holding into it is stale afterwards.

   Nothing is range-checked.  group_index is scaled and used, a thirty-first
   distinct group writes its slot past the slot table and over the first cached
   group's pixels, and no malloc, realloc or fread result is tested. */
extern int fdps_cache_cel_sprite_group(int group_index, FILE *fp);
#pragma aux fdps_cache_cel_sprite_group "*" parm caller [];

/* Frees whatever the previously loaded chapter left in the field resource
   globals and reloads the whole set for data_fdps_chapter_current_chapter_id,
   then allocates and blanks the battle movement grid.  Takes nothing and
   returns nothing: the chapter number is the global, and everything it
   produces is published through the globals in gamedata.h.

   What it loads, and out of which container:

     Field.vfs   fdetxt%02d.txt  -> data_fdps_current_chapter_text_ptr
                                    (chapter number PLUS ONE, uniquely)
                 map%02d.dat     -> data_fdps_tile_event_data_table_ptr, whose
                                    bytes +1 and +2 it copies out into
                                    data_fdps_map_player_slot_count and
                                    data_fdps_map_char_spawn_count
     Field1.vfs  M%02d.dtl       -> data_fdps_map_cell_event_code_layer_ptr
     Field2.vfs  dsc%02d.dat     -> the layer count and the eight parallel
                                    per-layer globals, then freed
     Field1.vfs  m%02d%d.mpl     -> data_fdps_scene_layer_tile_map_ptrs[n]
     Field1.vfs  m%02d%d.cel     -> data_fdps_scene_layer_tile_sheet_ptrs[n]
     Field2.vfs  attr%02d%d.dat  -> data_fdps_scene_layer_tile_attr_ptr[n]

   The order matters to a caller in one respect: the per-layer arrays are
   freed against the OLD layer count before the new descriptor file replaces
   it, so a caller that changes data_fdps_scene_layer_count itself between two
   calls decides how many blocks this one releases.

   The movement grid is sized and stamped from layer 0's tile map header
   whatever the layer count turns out to be, and fdps_map_grid_reset blanks it
   before the return, so the grid is usable the moment this comes back.

   Nothing is validated.  No load result is tested -- fdps_vfs_load_entry ends
   the process instead of returning null -- the malloc is not tested, and the
   layer count read out of the descriptor file is not clamped against the six
   slots the parallel arrays hold. */
extern void fdps_field_load_chapter_resources(void);
#pragma aux fdps_field_load_chapter_resources "*" parm caller [];

#endif
