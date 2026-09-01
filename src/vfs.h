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

/* Searches the same directory the same way and answers with the member's size
   in bytes instead of its index, or -1 when no entry matches.

   dir is what fdps_vfs_open hands back, exactly as for fdps_vfs_find_entry
   above, and every caller-visible property of that search holds here
   unchanged: the walk is front to back so a duplicated name resolves to the
   earlier entry, the query is upper-cased IN PLACE while the entry's own name
   is taken raw, the entry count is read one byte wide so only members 0..254
   are reachable and a container of exactly 256 searches nothing, and nothing
   about the handle is validated.

   The size it reports is the field at offset 0x0d of the entry -- the one
   fdps_vfs_load_file passes to malloc and to fread as the member's byte count.
   The entry carries the same number a second time at offset 0x11 and every
   shipped container has the two agreeing (resource_info/vfs.md), so no game
   file can tell the two fields apart; the assembly can, and 0x0d is the one
   that is read.

   -1 is unambiguous here in a way that fdps_vfs_read_entry_count's 0 is not:
   no member has a negative size, so the miss is distinguishable from any real
   answer.  A zero-byte member would report 0, and no shipped container holds
   one -- the smallest of the 1,202 members is Map41.cod in Field.vfs at 15
   bytes and the largest is Chapter.saf in Misc.vfs at 1,857,775.

   Nothing in the image calls this function or takes its address.  It is the
   unused member of the VFS reader's interface: the size a caller would want is
   already handed over by fdps_vfs_load_file, which finds the entry itself. */
extern int fdps_vfs_find_entry_size(char *name, void *dir);
#pragma aux fdps_vfs_find_entry_size "*" parm caller [];

/* Opens the container at path and builds the handle the two searches above and
   fdps_vfs_load_file walk, or returns NULL when the container will not open or
   the handle will not fit in memory.

   The handle is one malloc block holding a 17-byte header and then the
   container's whole directory read in verbatim:

     +0x00  u32   the member count, as the header's field at file offset 7 has
                  it -- the full 32 bits, although every reader that takes it
                  back out of a handle reads only the low byte
     +0x04  char  the path this handle was opened with, copied strlen bytes
                  long out of the caller's string
     +0x11  ...   one 26-byte directory entry per member, read from the file
                  offset the header's field at 5 names

   Two consequences of how the path is stored.  It is copied raw, so a path
   spelled in lower case is kept in lower case -- unlike a member name, which
   the searches fold.  And the 13 bytes between +4 and the table hold a
   terminator only because the header was zeroed first, which means a path of
   12 characters or fewer is terminated and a longer one runs over the start of
   the directory: the 13th character lands on the header's last byte and the
   14th on the first entry's name.  The width of the field is not merely the
   gap before the table -- fdps_vfs_load_file copies exactly 13 bytes back out
   of +4 (PUSH 0xd at 00039c00) into a stack buffer and hands that to fopen, so
   an unterminated field is what the reopen reads.

   No path the shipped game builds is long enough to reach that, but the margin
   is not a property of the image.  The containers installed on the hard disk
   are named by literals the call sites push -- "MISC.VFS" at 0x60128 and
   "IconAni.vfs", "Field.vfs", "Field1.vfs" and "Field2.vfs" in the block at
   0x600ec -- bare 8.3 names whose longest, BACKGRND.VFS, is exactly 12
   (resource_info/vfs.md), which uses the margin up without spending it.  The
   container on the CD is reached by a path composed at run time instead:
   fdps_cd_verify_disc_and_play_track formats "%s\Pack.vfs" (0x61ebc) into a
   stack buffer and passes it to fdps_vfs_load_entry, which hands its own
   argument straight to this function, and the "%s" is data_fdps_cdrom_path --
   the third whitespace token of Disk.no, which the installer writes as
   "CDROM at e:" so the token is a bare drive letter (program_info/cd_audio.md).
   That makes the path "e:\Pack.vfs" at 11 characters, and those two characters
   of headroom come from a text file on the player's disk rather than from
   anything in the executable.

   Nothing about the file is validated -- not the "VFS" magic, not the version,
   not the signature -- so any file at all is opened and the numbers at offsets
   5 and 7 are believed whatever they are.  The entry-table offset is read as a
   SIGNED 16-bit value and sign-extended into the seek, so a container whose
   table began past 0x7fff would seek backwards; every shipped container has 35
   there (resource_info/vfs.md).

   The size the member count implies is trusted twice over: it is what the
   malloc is asked for and what the directory read is told to fetch, both
   without a bound, so a header claiming more members than the file holds
   allocates for them and reads what there is.  Neither fread result is
   examined.

   The file is always closed, including on the malloc failure that returns
   NULL, so a caller that gets NULL has leaked nothing.  Freeing the handle is
   the caller's business; nothing here or in fdps_vfs_load_file frees it. */
extern void *fdps_vfs_open(char *path);
#pragma aux fdps_vfs_open "*" parm caller [];

/* Pulls one member called name out of the open container vfs into a buffer of
   its own, or returns NULL when the member is not there, the container will
   not reopen or the buffer will not fit in memory.

   vfs is a handle from fdps_vfs_open, and this function is the only reader
   that uses both halves of one: the directory, which it searches through
   fdps_vfs_find_entry, and the archive path at +4, which it copies out and
   hands to a fresh fopen.  The container is therefore opened a second time
   here -- fdps_vfs_open closed its own FILE before it returned -- and it is
   that stored path, not name and not anything global, that says which file is
   read.  Exactly 13 bytes of the field are copied, the whole of it, so a
   handle whose path filled all 13 hands fopen a string with no terminator in
   it (see fdps_vfs_open above for how a path that long would get there).

   Everything fdps_vfs_find_entry does to the query happens here: name is
   upper-cased IN PLACE before the compare, the entry's own name is taken raw,
   and the entry count is read one byte wide so only members 0..254 are
   reachable.  A caller that passes a string literal has that literal
   permanently upper-cased.

   The member's byte count comes from the entry's field at +0x0d and its start
   in the container from the field at +0x16; the duplicate size at +0x11 and
   the reserved byte at +0x15 are not read (resource_info/vfs.md).  The count
   is what the buffer is allocated to and what the read is asked for, both
   unbounded and both believed: a container whose directory disagrees with its
   own length allocates and reads what the directory claims.  Neither the seek
   nor the read is checked, so a member the file is too short to hold comes
   back as a buffer that was only partly filled, and the caller cannot tell.

   The buffer is malloc'd and belongs to the caller, who must free it.  The
   FILE is closed on every path that opened one, including the malloc failure
   that returns NULL, so a NULL answer has leaked nothing.  The handle itself
   is untouched and is never freed here.

   Each of the three failures prints its own diagnostic to stdout before it
   returns NULL -- "Can't find the string: %s" with the query, "Can't open the
   source VFS_file: %s" with the path out of the handle, and "Can't allocate
   memory for VFS_file: %s(%dbytes)" with the query and the byte count -- so
   the answer is a bare NULL that says which of the three went wrong only in
   the transcript.  fdps_vfs_load_file_or_exit is the wrapper that turns that
   NULL into fdps_wait_any_key followed by exit(1). */
extern void *fdps_vfs_load_file(char *name, void *vfs);
#pragma aux fdps_vfs_load_file "*" parm caller [];

#endif
