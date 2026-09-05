/* savepnl.h -- the save/load page: the whole 320x200 screen both save and load
 * are drawn on, and the three slot summary panels composed onto it.
 *
 * src/save.c drives the screens and owns the slot cursor; this pair of
 * functions is what puts a page in front of it (rebuild_info/code_layout.md).
 * The image the panels are read out of is FDE.SAV, whose shape on disc and
 * whose checksum and cipher belong to savefile.h.
 */
#ifndef SAVEPNL_H
#define SAVEPNL_H

#include "fdpstype.h"

/* 00024830.  Builds the whole save/load page and hands it back: the background
   named by `bg_cel_name` loaded out of MISC.VFS and blitted into a fresh
   320x200 page, the three slot summaries drawn onto it from FDE.SAV, and the
   party's icon groups reloaded into the sprite cache afterwards.  Both screens
   call it -- fdps_load_game_screen once and fdps_save_game_screen twice, the
   second time to redraw the page after a save has been written.

   `bg_cel_name` is a member of MISC.VFS: "Save.cel" for the save screen and
   "Load.cel" for the load screen.  It is UPPER-CASED IN THE CALLER'S OWN
   STORAGE by fdps_vfs_load_entry (vfs.h), so it cannot be a pointer into
   read-only memory.

   The answer is a 64000-byte heap block the caller owns and must free.  It is
   never null-checked here and malloc's answer is not tested.

   THE SCREEN READS FDE.SAV EVERY TIME IT IS BUILT and never caches it, which
   is what makes the save screen's second call show the slot that was just
   written.  A missing file is not an error: the image is filled with 0xff
   instead and all three slots read as never written.

   IT PUBLISHES data_fdps_ui_save_slot_occupied_flags, one entry per panel
   slot, which is the only thing that survives the call besides the page.

   IT LEAVES THE SPRITE CACHE HOLDING THE PARTY.  Each panel empties the cache
   and loads one group into it, so the cache is emptied once more on the way
   out and refilled from the roster in roster order -- without that the rest of
   the game would paint out of a one-entry cache
   (rebuild_info/pitfalls.md).  Anything a caller was holding into the cache
   before the call is stale afterwards.

   A container or a member that cannot be found ends the process inside
   fdps_vfs_load_entry (vfs.h), and a missing ICON.CEL reaches
   fdps_cache_cel_sprite_group as a null stream rather than being reported. */
extern unsigned char *fdps_saveload_screen_build(char *bg_cel_name);
#pragma aux fdps_saveload_screen_build "*" parm caller [];

/* 00024a40.  Draws one slot's summary panel: the leader's face and level, the
   chapter the slot holds and that chapter's title, and the date and time the
   slot was written.  fdps_saveload_screen_build calls it once per slot and it
   is the only caller.

   `dest` is the top-left byte of this slot's panel inside the page being
   composed -- the caller forms it as page + 0xd + (slot * 0x34 + 0x1a) * 0x140,
   so the three panels sit 52 rows apart -- and `pitch` is that page's row
   stride, 0x140 at the one call site.  `slot_record` is one 0xa28-byte slot of
   the decrypted FDE.SAV image, save_buffer + 0x312b + slot * 0xa28 at the call
   site; its first 0x9b0 bytes are the roster copy, so roster[0] is the party
   leader.

   A SLOT WHOSE chapter_index IS 0xff HAS NEVER BEEN WRITTEN.  It prints entry
   0x209 of data_fdps_all_game_text_ptr across the panel and draws nothing else
   -- no captions, no face, no figures.

   IT EMPTIES THE GLOBAL SPRITE CACHE AND LEAVES IT HOLDING ONE GROUP.  Drawing
   a written slot frees the cache buffer, zeroes the count and loads the
   leader's icon group into the slot that frees up, so on return the cache holds
   that one group and nothing else.  Every panel does it again, and the caller
   has to reload the party's groups after its slot loop or the rest of the game
   paints out of a one-entry cache (rebuild_info/pitfalls.md).

   IT MOVES data_fdps_number_glyph_color_row AND LEAVES IT AT 0.  The level is
   drawn in whatever row the global already held on entry; the chapter, the date
   and the time each set their own row, and 0 is written on the way out.  An
   unwritten slot does not touch it at all.

   The two messages are placed at raw byte offsets into the page rather than at
   a row times `pitch`, so they only land where they are meant to at a pitch of
   0x140.  Nothing is clipped and no pointer is checked: a missing ICON.CEL
   faults inside the cache loader and a missing chapter member ends the process
   inside fdps_vfs_load_entry (vfs.h). */
extern void fdps_draw_save_slot_panel(unsigned char *dest, int pitch,
                                      struct fdps_save_slot *slot_record);
#pragma aux fdps_draw_save_slot_panel "*" parm caller [];

#endif
