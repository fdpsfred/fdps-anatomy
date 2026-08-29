/* rsrc.h -- chapter resource loading and freeing, and the .CEL sprite cache.
 *
 * The readers here open a named file, pull one piece of it into a heap block
 * and hand that block to the caller through a pointer slot the caller owns.
 * None of them checks a malloc result and none of them reports a failure
 * after the open succeeded: a file that opens is assumed to be well formed.
 */
#ifndef RSRC_H
#define RSRC_H

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

#endif
