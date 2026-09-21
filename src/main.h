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

/* Everything the program holds for its whole life, put in place in one call
   from main and released only by fdps_shutdown_free_resources: the eight font
   metrics, the keyboard vector, the roster block, the nine data tables,
   fifteen resources out of two containers, two composite gauge sheets built
   from two more, and the two palette lookup tables.  It takes nothing, returns
   nothing and reports nothing -- every result is a global, and a failure ends
   the process instead of coming back.

   The order matters in three places and nowhere else.  The font metrics are
   seeded before anything can draw.  Misc.vfs supplies the nine data tables and
   thirteen of the fifteen resources and is closed before Field.vfs is opened,
   so only one container handle is ever live.  And the palette tables are built
   last, because the builder needs the two palettes this function loaded.

   Three kinds of failure, none of which returns.  A container that will not
   open prints its own message and exits: Misc.vfs and Field.vfs each have
   their own literal, and the two disagree about case.  A member that will not
   load has already been named by fdps_vfs_load_file, so this only waits for a
   key and exits.  And a malloc that comes back null is not tested at all --
   the roster block and the two composite buffers go straight to memset and to
   the blitter.

   The two composites are built rather than loaded.  EasyBar.cel's first three
   sub-images become one 0x306-byte buffer of three 43x6 gauges laid end to
   end, and Bar.cel's first three become one 0xaf8-byte buffer of three 117x8
   bars; both source sheets are freed and only the composites survive, which is
   why nothing else in the game ever opens either name.

   The palette tables come out of a disk cache.  If FMer1.tmp is present the
   two tables are read straight out of Mer1.tmp and Mer2.tmp; if it is not,
   they are computed twice -- once from Fight.pal, written out as FMer1.tmp and
   FMer2.tmp, and once from Fde.pal, written out as Mer1.tmp and Mer2.tmp --
   and the Fde.pal pair is what the program runs on either way.  Nothing ever
   reads the Fight.pal pair back: FMer1.tmp exists so that the branch has
   something to test for, FMer2.tmp is not even that, and no other code in the
   image names either.  Only FMer1.tmp is tested for, and no fopen is checked,
   so a directory holding FMer1.tmp but not Mer1.tmp reads through a null
   FILE *.  Deleting the four files is what makes the game pick up an edited
   palette; nothing else invalidates the cache.

   It runs exactly once.  A second call would install the keyboard vector over
   its own handler and overwrite every pointer it filled the first time,
   leaking all eighteen blocks and the nine tables. */
extern void fdps_load_global_resources(void);
#pragma aux fdps_load_global_resources "*" parm caller [];

/* The C entry point, called by the Watcom startup's __CMain, which pushes argc
   and argv (neither is read) and hands the result to exit().  Checks that the
   game was installed (DISK.NO exists) and that fdps_cdrom_detect answers 1,
   ending the process with exit(1) and a two-line message otherwise; reads the
   CD path prefix out of Disk.No's third token into data_fdps_cdrom_path;
   starts audio at 25 Hz, loads the global resources, enters VGA mode 13h;
   then runs the title screen and the outer battle loop until
   data_fdps_shared_quit_game_requested is raised, dispatching each battle's
   request code (1 game over, 2 chapter cleared); and finally shuts down,
   returns to text mode 3 and prints the farewell line.  Returns that last
   printf's result, which becomes the process exit code.

   The symbol is literally `main` -- the CRT's contract, the one name in the
   rebuild without the fdps_ prefix (rebuild_info/naming.md).  The unit-test
   image compiles this file with main renamed away, because its own generated
   entry is also main (rebuild_info/emit_pipeline.md). */
extern int main(int argc, char **argv);
#pragma aux main "*" parm caller [];

#endif
