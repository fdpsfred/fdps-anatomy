/* vfs.h -- the .VFS container readers.
 *
 * A .VFS is the game's named-file archive: a 35-byte header, then one 26-byte
 * entry per member, then every member's bytes laid end to end with no gaps and
 * no padding.  The format is documented in resource_info/vfs.md; what matters
 * to the readers here is that the header's entry count is the u32 at file
 * offset 7 and that nothing in the program ever validates the "VFS" magic at
 * offset 0, the version at 3 or the signature at 0x0b.  There is therefore no
 * format check anywhere in this family: any file at all will be probed, and a
 * file that is not a container comes back with whatever its own bytes happen
 * to hold at the offset that was read.
 */
#ifndef VFS_H
#define VFS_H

#include "fdpstype.h"

/* Reads a container's entry count straight off disk, without opening the
   container or building a handle for it.

   It presets its result to 0, opens path with fopen(path, "rb") and, when that
   fails, falls straight through and hands that 0 back.  Otherwise it seeks to
   file offset 7 -- the header's entry-count field -- reads one 4-byte
   little-endian value into the result, closes the file and returns it.

   Two consequences follow from what it does not do.  Nothing else in the
   header is inspected, so there is no format check and a non-container file is
   probed just as willingly: it comes back holding whatever that file has at
   offsets 7 through 10.  And the fread result is discarded, so a file too
   short to hold 11 bytes returns whatever fread managed to store -- the preset
   0 if it stored nothing.

   The whole 32-bit field survives, which is what separates this from the rest
   of the family: fdps_vfs_image_entry_count reads the same offset-7 field as a
   single byte, and fdps_vfs_find_entry takes the count back out of an open
   handle a byte at a time although fdps_vfs_open stored it there as a dword.
   Both therefore see only the low 8 bits, which is why a container is limited
   to 255 usable members.

   0 is ambiguous: it is both "the file would not open" and "the container is
   empty".  Nothing in the image calls this function or takes its address, so
   no caller in the original has to tell those apart. */
extern int fdps_vfs_read_entry_count(char *path);
#pragma aux fdps_vfs_read_entry_count "*" parm caller [];

/* Reads a container's entry count out of an image already resident in memory,
   as the in-memory counterpart of fdps_vfs_read_entry_count above.

   image is the base of a whole container -- the 35-byte header, then one
   26-byte entry per member, then the members' bytes -- and not a handle from
   fdps_vfs_open: offset 7 of a handle lands inside the archive path it keeps
   at handle+4.  The nested BaseAni.vfs and BaseWav.vfs images the game holds
   resident are the shape this takes.

   Only the low byte of the header's u32 entry count comes back, so the result
   is 0..255 and a container of 256 members reports 0.  The truncation is this
   function's own rather than the format's: fdps_vfs_image_get_entry reads the
   same field off the same kind of pointer as a whole dword.  It is what caps a
   container at 255 usable members, and no shipped container comes near the
   cap -- the largest is Field.vfs at 223.

   Nothing is validated: the "VFS" magic at offset 0, the version at 3 and the
   entry-table offset at 5 are never looked at, so any image at all is read and
   a non-container comes back holding whatever its own byte 7 is.

   Nothing in the image calls this function or takes its address; it reaches
   the link as part of the VFS module object. */
extern unsigned int fdps_vfs_image_entry_count(struct fdps_vfs_image_header *image);
#pragma aux fdps_vfs_image_entry_count "*" parm caller [];

/* Searches an open container's directory for a member called name and returns
   the entry's index, or -1 when no entry matches.

   handle is what fdps_vfs_open hands back, not a container image: the entry
   count as a dword at offset 0, the 13-byte archive path at 4, then one
   26-byte directory entry per member from offset 0x11 onwards.  The search
   walks that table from the front and stops at the first match, so a container
   holding the same name twice always resolves to the earlier entry.

   Two things about it are visible to the caller and neither is optional.

   The query is upper-cased IN PLACE.  strupr is applied to name and the
   entry's own name is taken raw, so the folding is one-way: a mixed-case query
   such as "Turn.saf" matches an upper-case entry, while an entry whose name is
   not upper-case can never be found however the query is spelled.  Writing the
   comparison as stricmp would change both halves of that.  The in-place
   rewrite reaches the caller's buffer, and the callers in the image pass
   string literals, so a literal in the image is permanently upper-cased by the
   first lookup that uses it -- which is why name cannot be const and why those
   literals cannot live in read-only storage (rebuild_info/pitfalls.md).

   The entry count is taken from the handle one byte wide although fdps_vfs_open
   stored a whole dword there, so only members 0..254 of a container are ever
   reachable and a container of exactly 256 members searches nothing at all.
   That is the same 255-member cap fdps_vfs_image_entry_count imposes from the
   image side; no shipped container comes near it, the largest being Field.vfs
   at 223.

   Nothing validates the handle: a pointer that is not one is walked just as
   willingly, with whatever its byte 0 holds taken as the member count.

   Its only caller is fdps_vfs_load_file. */
extern int fdps_vfs_find_entry(char *name, void *handle);
#pragma aux fdps_vfs_find_entry "*" parm caller [];

#endif
