/* vfs.c -- the .VFS container readers: opening a container, searching its
 * entry table and pulling a member out of it.
 *
 * See vfs.h for what each reader's caller has to know, and
 * resource_info/vfs.md for the container layout itself.  Nothing in this file
 * validates a container: the readers take the two header fields they need and
 * trust everything else, so a file that is not a .VFS is never rejected.
 */
#include <stddef.h>
#include <stdio.h>
#include "vfs.h"

/* Where the header keeps the entry count: PUSH 0x7 at 00039920, the middle
   argument of the fseek that precedes the read.  resource_info/vfs.md has the
   same 0x07 for the u32 entry count, three bytes of magic, a version word and
   the entry-table offset word ahead of it. */
#define VFS_HEADER_ENTRY_COUNT_OFFSET 7

/* How much of that field is read: PUSH 0x4 / PUSH 0x1 at 00039934, so one item
   of four bytes rather than four items of one.  Four is the whole field --
   fdps_vfs_image_entry_count takes only the low byte of the same field. */
#define VFS_HEADER_ENTRY_COUNT_BYTES 4

/* 000398f0.  One branch in the body: CMP dword ptr [EBP + -0xc],0x0 / JZ
   0x0003994e at 00039918, which jumps the seek, the read and the close and
   lands on the return of the preset value.  So the failed-open path is not a
   separate exit -- it is the same MOV EAX,[EBP + -0x8] at 0003994e that the
   successful path reaches, which is why a file that will not open and an empty
   container are indistinguishable to the caller.

   The result is preset before the open, MOV dword ptr [EBP + -0x8],0x0 at
   000398fc, and that preset is the only thing standing between a short file
   and an uninitialised return: the EAX fread hands back is dead here, never
   compared and never stored, so a read that filled nothing is not noticed.

   Nothing in the header but this one field is looked at.  There is no
   comparison against the "VFS" magic, the version or the entry-table offset
   anywhere in the body, so the function cannot fail a file for not being a
   container. */
int fdps_vfs_read_entry_count(char *path)
{
    FILE *fp;
    int entry_count;

    entry_count = 0;
    fp = fopen(path, "rb");
    if (fp != NULL) {
        fseek(fp, VFS_HEADER_ENTRY_COUNT_OFFSET, SEEK_SET);
        fread(&entry_count, VFS_HEADER_ENTRY_COUNT_BYTES, 1, fp);
        fclose(fp);
    }
    return entry_count;
}
