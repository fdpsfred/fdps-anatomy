/* vfs.c -- the .VFS container readers: opening a container, searching its
 * entry table and pulling a member out of it.
 *
 * See vfs.h for what each reader's caller has to know, and
 * resource_info/vfs.md for the container layout itself.  Nothing in this file
 * validates a container: the readers take the two header fields they need and
 * trust everything else, so a file that is not a .VFS is never rejected.
 *
 * fopen, fseek, fread and fclose come from <stdio.h>; memcpy, memset, strcmp,
 * strlen and strupr from <string.h>; malloc from <stdlib.h>.  All ten are real
 * library calls in the original -- CALL 0x000435bc, 0x000435f3 and 0x00042fe0
 * at 000399d4, 000399e0 and 000399ed for three of the string routines, and
 * CALL 0x0003d375, 0x00042cd0, 0x00042dd2 and 0x000435bc at 00039b32,
 * 00039b4b, 00039b5f and 00039b73 for malloc, memset, strlen and memcpy.
 * Watcom 10.0a only expands memcpy, memset, strlen and strcmp into
 * instructions when the intrinsics are asked for, and -oi is not in this
 * build's flag set (rebuild_info/build_flags.md), so the plain declarations
 * are what reproduce the calls.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

/* Where an entry keeps the member's size, counted from the start of the entry.
   The original never spells it out: MOV EAX,[EAX + 0x1e] at 00039a90 reads it
   off a base that already has the 0x11 table offset folded into it, and 0x1e -
   0x11 is 0xd -- the first byte past the 13-byte name field.
   resource_info/vfs.md has the same 0x0d for the member size, with a second
   copy of it at 0x11 and the member's start at 0x16. */
#define VFS_ENTRY_SIZE_FIELD_OFFSET 0xd

/* 00039a20.  The same loop as fdps_vfs_find_entry above, instruction for
   instruction, up to what the match stores: the preset of -1 at 00039a2c, the
   byte-wide count at 00039a38, the -od loop with its test at 00039a44 and its
   increment ahead of the body at 00039a4e, the 13-byte memcpy, strupr on the
   query and strcmp against the raw entry name, and TEST EAX,EAX / JNZ
   0x00039a98 at 00039a85 whose taken side rejoins the increment.  Everything
   vfs.h says about that search applies here word for word.

   The one difference is the fall-through.  Where the index search stores the
   loop counter, this one re-forms the record address and loads a dword out of
   it -- IMUL EAX,[EBP + -0x8],0x1a / ADD EAX,[EBP + 0x18] / MOV EAX,[EAX +
   0x1e] at 00039a89 -- before storing that into the same result slot and
   jumping to the same single exit.  So the match is still a break and the -1
   still survives a loop that runs out.

   0x1e is a folded constant and not a field offset: it is the 0x11 the entry
   table starts at plus the 0xd the size field sits at inside the entry, and
   the two are written separately here so neither reads as the other.  The
   arithmetic is done a second time rather than reusing the address the memcpy
   was given, which is what the original does and what -od leaves alone.

   The load is a full dword, MOV EAX rather than MOVZX or MOV AL, so unlike the
   entry count nothing is truncated on the way out.  The value is never
   compared inside the function -- it is stored and returned -- so its
   signedness is not observable here; int is what the -1 preset in the same
   slot makes it. */
int fdps_vfs_find_entry_size(char *name, void *dir)
{
    char entry_name[16];
    int index;
    int found_size;
    int entry_count;

    found_size = -1;
    entry_count = *(unsigned char *)dir;
    for (index = 0; index < entry_count; index++) {
        memcpy(entry_name,
               (char *)dir + index * VFS_ENTRY_SIZE
                   + VFS_HANDLE_ENTRY_TABLE_OFFSET,
               VFS_ENTRY_NAME_BYTES);
        if (strcmp(entry_name, strupr(name)) == 0) {
            found_size = *(int *)((char *)dir + index * VFS_ENTRY_SIZE
                                  + VFS_HANDLE_ENTRY_TABLE_OFFSET
                                  + VFS_ENTRY_SIZE_FIELD_OFFSET);
            break;
        }
    }
    return found_size;
}

/* Where the header keeps the entry table's own file offset: PUSH 0x5 at
   00039b08, the middle argument of the second fseek.  resource_info/vfs.md has
   the same 0x05, and 35 in it for every shipped container. */
#define VFS_HEADER_TABLE_OFFSET_FIELD 5

/* How much of that field is read: PUSH 0x2 / PUSH 0x1 at 00039b1a.  Two bytes
   of what the format calls a u16 -- and read into a signed short here, which
   is what MOVSX at 00039b7d says about the value that reaches the seek. */
#define VFS_HEADER_TABLE_OFFSET_BYTES 2

/* Where a handle keeps the archive path it was opened with: ADD EAX,0x4 at
   00039b6f, the memcpy destination.  It sits between the dword member count at
   0 and the entry table at VFS_HANDLE_ENTRY_TABLE_OFFSET, so the field is the
   13 bytes 4..0x10. */
#define VFS_HANDLE_ARCHIVE_PATH_OFFSET 4

/* 00039ab0.  Two nested branches and a third that cannot fail.

   The outer one is CMP [EBP + -0x14],0x0 / JZ 0x00039bba at 00039ad8: a file
   that will not open jumps the whole body and lands on the return of the
   preset NULL from 00039abc.  The inner one is CMP [EBP + -0x8],0x0 / JZ
   0x00039ba8 at 00039b3d, which jumps the eight instructions that fill the
   handle and lands on the close, so a malloc that fails still closes the file
   and still returns NULL.  The third is CMP [EBP + -0x14],0x0 / JZ 0x00039bba
   at 00039ba8, guarding the fclose against a fp that the outer branch has
   already proved non-NULL.  It is written out here because it is in the
   original; the compiler does not fold it and neither does this.

   Nothing checks a result on the way through.  Both freads discard what they
   return (ADD ESP,0x10 with EAX untouched at 00039b03 and 00039b27), so a file
   too short to hold either field leaves that field as whatever the frame slot
   held; both fseeks discard theirs; and no field of the header but the two
   that are read is looked at, so there is no format check to fail.

   The entry count is stored into the handle as a whole dword, MOV [EDX],EAX at
   00039b59, and that is the only reader or writer in the module that treats it
   as 32 bits -- fdps_vfs_find_entry and fdps_vfs_find_entry_size take it back
   out one byte wide.  Writing it as a byte here would agree with them and be
   wrong: what the searches cannot reach is bytes 1..3 of a field this function
   really does fill (rebuild_info/pitfalls.md).

   The seek to the table sign-extends: MOVSX EAX,word ptr [EBP + -0x4].  The
   field is a u16 in the format, so the width the value is held at is
   behaviour, not spelling -- an offset of 0x8000 or more seeks backwards
   rather than forwards.  Nothing shipped has more than 35 there.

   malloc is asked for the count the header claims, and the directory read is
   given the same product a second time (IMUL at 00039b2a and again at
   00039b94) rather than the sum being kept, which is what -od does with the
   expression written twice.  Neither is bounded by the file's real size.

   The path copy is strlen bytes and not the field's 13 (CALL strlen at
   00039b5f feeding the memcpy count at 00039b67), so the terminator a caller
   sees is the zero the memset left, and a path longer than 12 characters
   overruns the header into the first directory entry.  See vfs.h.  The length
   is spent where it is produced rather than kept: PUSH EAX at 00039b67 hands
   strlen's return straight to the memcpy, and giving it a local of its own
   adds a frame slot and a store-and-reload that the original does not have
   (SUB ESP,0x18 instead of 0x14, verified against the emitted VFS.OBJ). */
void *fdps_vfs_open(char *path)
{
    FILE *fp;
    unsigned int entry_count;
    short table_offset;
    char *handle;

    handle = NULL;
    fp = fopen(path, "rb");
    if (fp != NULL) {
        fseek(fp, VFS_HEADER_ENTRY_COUNT_OFFSET, SEEK_SET);
        fread(&entry_count, VFS_HEADER_ENTRY_COUNT_BYTES, 1, fp);
        fseek(fp, VFS_HEADER_TABLE_OFFSET_FIELD, SEEK_SET);
        fread(&table_offset, VFS_HEADER_TABLE_OFFSET_BYTES, 1, fp);
        handle = (char *)malloc(entry_count * VFS_ENTRY_SIZE
                                + VFS_HANDLE_ENTRY_TABLE_OFFSET);
        if (handle != NULL) {
            memset(handle, 0, VFS_HANDLE_ENTRY_TABLE_OFFSET);
            *(unsigned int *)handle = entry_count;
            memcpy(handle + VFS_HANDLE_ARCHIVE_PATH_OFFSET, path,
                   strlen(path));
            fseek(fp, table_offset, SEEK_SET);
            fread(handle + VFS_HANDLE_ENTRY_TABLE_OFFSET,
                  entry_count * VFS_ENTRY_SIZE, 1, fp);
        }
        if (fp != NULL) {
            fclose(fp);
        }
    }
    return handle;
}
