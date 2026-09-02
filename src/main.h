/* main.h -- the program's entry point and the lifecycle of the globals that
 * live as long as the program does: the nine data tables read out of the VFS
 * container at startup, and the release of every global buffer at shutdown.
 *
 * The nine table pointers themselves are gamedata.c's, because table.c reads
 * them too; this file only loads and releases them.
 */
#ifndef MAIN_H
#define MAIN_H

/* Fills all nine of the game's global data-table pointers out of one already
   open VFS container: nine calls to fdps_vfs_load_file_or_exit in a row, each
   given the same container, one hard-coded member name and the address of the
   one global that member belongs in.  Nothing is returned, nothing is branched
   on and no loader result is inspected -- the wrapper ends the process itself
   when a member will not load, so a caller that comes back holds all nine.

   vfs is the handle fdps_vfs_open produced; this function only forwards it and
   never reads through it.  The member names go over as they are written here,
   in mixed case, and the loader upper-cases each of them IN PLACE, so the nine
   strings are permanently folded by the first call (see vfs.h).

   Every block it produces is malloc'd by the loader and owned by the program
   from then on: fdps_free_global_resource_buffers is what releases them, and
   nothing here checks whether a slot already held one, so calling this twice
   over the same globals leaks the first nine. */
extern void fdps_load_data_tables(void *vfs);
#pragma aux fdps_load_data_tables "*" parm caller [];

/* Hands all nine of the game's global data-table pointers to free(), in a
   fixed order, with no null test and no bounds of any kind: nine identical
   free() calls straight through.  Nothing is cleared afterwards, so every one
   of the nine globals is left dangling and calling this a second time is a
   double free -- it runs exactly once, at the end of shutdown, from
   fdps_shutdown_free_resources.  A pointer that is still null is passed to
   free() like any other; the CRT's own free() returns at once on null. */
extern void fdps_free_global_resource_buffers(void);
#pragma aux fdps_free_global_resource_buffers "*" parm caller [];

/* The whole of the program's teardown, called once from main and from nowhere
   else: every globally held allocation goes back to free(), and then the
   keyboard vector goes back to DOS.

   Twenty-five individually named global pointers are released, followed by
   three parallel scene-layer pointer arrays walked to
   data_fdps_scene_layer_count, followed by the nine data tables
   fdps_free_global_resource_buffers owns, and finally
   fdps_uninstall_keyboard_isr.  Eighteen of the twenty-five are freed with no
   null test and seven are guarded by one; the guard says which resources are
   optional -- a chapter's text, the portrait buffer, the move grid, the cel
   cache, the tile-event table, the event-code layer and the map unit array all
   exist only once something has loaded them -- and not that the CRT needs it,
   because free() returns at once on null either way.

   Nothing is cleared: not one of the pointers is stored back to, so every one
   of them is left dangling and a second call is a double free.  That is not a
   hazard in the shipped program, where the sole call site is followed by the
   process ending, but it is why this is a shutdown routine and not a general
   "release everything" that a restart could use.

   The keyboard hook comes down LAST, after every free.  A free() cannot be
   interrupted into by the game's own INT 09h handler in a way that matters --
   the handler only latches a scancode -- so the order is not a locking
   discipline; it is simply the order the original runs in, and the two halves
   are independent. */
extern void fdps_shutdown_free_resources(void);
#pragma aux fdps_shutdown_free_resources "*" parm caller [];

#endif
