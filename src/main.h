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

#endif
