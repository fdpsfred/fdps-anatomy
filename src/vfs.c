/* vfs.c -- the .VFS container readers: opening a container, searching its
 * entry table and pulling a member out of it.
 *
 * See vfs.h for what each reader's caller has to know, and
 * resource_info/vfs.md for the container layout itself.  Nothing in this file
 * validates a container: the readers take the two header fields they need and
 * trust everything else, so a file that is not a .VFS is never rejected.
 *
 * fopen, fseek, fread and fclose come from <stdio.h>; memcpy, strcmp and
 * strupr from <string.h>.  All seven are real library calls in the original --
 * CALL 0x000435bc, 0x000435f3 and 0x00042fe0 at 000399d4, 000399e0 and
 * 000399ed for the three string routines.  Watcom 10.0a only expands memcpy
 * and strcmp into instructions when the intrinsics are asked for, and -oi is
 * not in this build's flag set (rebuild_info/build_flags.md), so the plain
 * declarations are what reproduce the calls.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
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

/* 00039960.  Straight line, no branch and no call: XOR EAX,EAX / MOV EDX,[EBP
   + 0x14] / MOV AL,[EDX + 0x7] / MOV [EBP + -0x4],EAX / MOV EAX,[EBP + -0x4].

   The load is MOV AL, one byte, although offset 7 is the header's 32-bit entry
   count and fdps_vfs_image_get_entry reads that same field off that same kind
   of pointer with a full dword move.  Writing the obvious `return
   image->entry_count;` therefore changes behaviour for any container of 256 or
   more members -- the original reports that container's count modulo 256,
   0 for exactly 256 -- so the byte load is taken through the field rather than
   over it (rebuild_info/pitfalls.md).  The XOR EAX,EAX ahead of the MOV AL is
   a zero extension, not a sign extension, which is what makes the result
   0..255 rather than -128..127.

   The frame is four bytes and holds no named local.  The spill to [EBP-4] and
   the reload are what -od does to the returned expression itself: writing the
   body as a bare return reproduces SUB ESP,0x4 and that one spill exactly,
   while giving the value a local of its own adds a second slot and a second
   spill (SUB ESP,0x8, verified against the emitted VFS.OBJ).  There is
   therefore nothing here to name -- the original's [EBP-4] is a compiler
   temporary, not a variable the author declared. */
unsigned int fdps_vfs_image_entry_count(struct fdps_vfs_image_header *image)
{
    return *(unsigned char *)&image->entry_count;
}

/* Where the directory table starts inside an open handle: ADD EAX,0x11 at
   000399cc.  It is the far side of the same 0x11 fdps_vfs_open lays out --
   memset(handle, 0, 0x11) at 00039b43 and fread into handle + 0x11 at
   00039b9c -- so ahead of the table sit the dword entry count at 0 and the
   13-byte archive path at 4. */
#define VFS_HANDLE_ENTRY_TABLE_OFFSET 0x11

/* One directory entry: IMUL EAX,[EBP + -0x8],0x1a at 000399c5.
   resource_info/vfs.md has the same 26 bytes, of which this function reads
   only the leading name. */
#define VFS_ENTRY_SIZE 0x1a

/* The width of an entry's name field, and the whole of what is copied out of
   it: PUSH 0xd at 000399c3, the memcpy count.  resource_info/vfs.md has the
   same 13 bytes, NUL-terminated with zero fill behind the terminator, which is
   what lets the copy be compared with strcmp at all. */
#define VFS_ENTRY_NAME_BYTES 0xd

/* 00039990.  One loop and one branch inside it.

   The loop is the ordinary -od shape: the test at 000399b4 (CMP EAX,[EBP +
   -0x10] / JL), the increment at 000399be ahead of the body, and the body
   from 000399c3 jumping back over it.  The body's own branch is TEST EAX,EAX
   / JNZ 0x00039a01 at 000399f5, where the fall-through stores the index into
   the result slot and jumps to the single exit -- so the match is a break out
   of the loop, not a second return, and the -1 that was written to [EBP + -0xc]
   at 0003999c before the loop is what survives when no entry matches.

   The count comes off the handle a byte at a time: XOR EAX,EAX / MOV AL,[EDX]
   at 000399a3, zero-extended into the full dword slot at [EBP + -0x10], even
   though fdps_vfs_open stored a whole dword there (MOV [EDX],EAX at 00039b59).
   Reading the field as the dword it is would let this search reach a 256th
   member the original cannot (rebuild_info/pitfalls.md), so the byte load is
   taken through the handle rather than over it.  The comparison against it is
   signed, JL rather than JB, which is why the count is held in an int; the
   zero-extended byte can never be negative, so the choice is not observable
   here, only faithful.

   strupr sits inside the loop rather than ahead of it, which is where the
   original calls it (CALL 0x000435f3 at 000399e0, between the memcpy and the
   strcmp of every iteration).  It rewrites the caller's buffer in place and
   the entry's own name is compared raw, so the case folding is one-way -- see
   vfs.h for what that costs the caller.

   The name is copied out to the stack before it is compared instead of being
   compared where it lies.  Thirteen bytes is the whole field, and the buffer
   is the 16 bytes the frame gives it: SUB ESP,0x20 covers the buffer at
   [EBP + -0x20] plus the three slots at -0x10, -0xc and -0x8 plus the -od
   return spill at -0x4.  The declarations are in the order that reproduces
   that assignment -- index at -0x8, found_index at -0xc, entry_count at -0x10
   -- verified against the emitted VFS.OBJ.  Any other order compiles to the
   same 0x20 frame with two of the three slots swapped, which is allocation
   rather than behaviour (ADR-0001); this order is simply the one that lines
   the two listings up for whoever reads them side by side. */
int fdps_vfs_find_entry(char *name, void *handle)
{
    char entry_name[16];
    int index;
    int found_index;
    int entry_count;

    found_index = -1;
    entry_count = *(unsigned char *)handle;
    for (index = 0; index < entry_count; index++) {
        memcpy(entry_name,
               (char *)handle + index * VFS_ENTRY_SIZE
                   + VFS_HANDLE_ENTRY_TABLE_OFFSET,
               VFS_ENTRY_NAME_BYTES);
        if (strcmp(entry_name, strupr(name)) == 0) {
            found_index = index;
            break;
        }
    }
    return found_index;
}
