/* main.h -- the program's entry point and the lifecycle of the globals that
 * live as long as the program does: the nine data tables read out of the VFS
 * container at startup, and the release of every global buffer at shutdown.
 *
 * The nine table pointers themselves are gamedata.c's, because table.c reads
 * them too; this file only loads and releases them.
 */
#ifndef MAIN_H
#define MAIN_H

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
