/* vfs.c -- the .VFS container readers: opening a container, searching its
 * entry table and pulling a member out of it.
 *
 * See vfs.h for what each reader's caller has to know, and
 * resource_info/vfs.md for the container layout itself.  Nothing in this file
 * validates a container: the readers take the two header fields they need and
 * trust everything else, so a file that is not a .VFS is never rejected.
 *
 * fopen, fseek, fread, fclose and printf come from <stdio.h>; memcpy, memmove,
 * memset, strcmp, strlen and strupr from <string.h>; malloc from <stdlib.h>.
 * All twelve are real library calls in the original -- CALL 0x000435bc,
 * 0x000435f3 and 0x00042fe0
 * at 000399d4, 000399e0 and 000399ed for three of the string routines, and
 * CALL 0x0003d375, 0x00042cd0, 0x00042dd2 and 0x000435bc at 00039b32,
 * 00039b4b, 00039b5f and 00039b73 for malloc, memset, strlen and memcpy.
 * memmove is a fourth entry point of its own, CALL 0x0003d514 at 00039d0d,
 * 00039d41 and 00039d53, and only fdps_vfs_image_get_entry uses it.
 * Watcom 10.0a only expands memcpy, memset, strlen and strcmp into
 * instructions when the intrinsics are asked for, and -oi is not in this
 * build's flag set (rebuild_info/build_flags.md), so the plain declarations
 * are what reproduce the calls.
 *
 * exit comes from <stdlib.h> too and is a real call as well, CALL 0x00042e0f
 * at 00029430 in the fatal wrapper at the bottom of this file; the key wait
 * that precedes it is the game's own, declared in keybd.h.  free is a real
 * call as well, CALL 0x0003d478 at 0002a1cf, and it is the only place in this
 * module that releases anything -- every other allocation here belongs to the
 * caller.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "keybd.h"
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

/* Where an entry keeps the member's start in the container, counted from the
   start of the entry.  The original never spells it out either: MOV EAX,[EAX +
   0x27] at 00039c5d reads it off a base that already has the table's own 0x11
   folded in, and 0x27 minus 0x11 is 0x16.  resource_info/vfs.md has the same
   0x16 for the member start, behind the size at 0x0d, the duplicate size at
   0x11 and the reserved byte at 0x15. */
#define VFS_ENTRY_START_FIELD_OFFSET 0x16

/* How much of a handle's archive-path field is copied back out of it: PUSH 0xd
   at 00039c00, the memcpy count.  It is the whole field -- the 13 bytes
   between VFS_HANDLE_ARCHIVE_PATH_OFFSET and VFS_HANDLE_ENTRY_TABLE_OFFSET --
   and not a length measured off the string, so the copy is terminated only if
   fdps_vfs_open left a zero somewhere inside those 13 bytes. */
#define VFS_HANDLE_ARCHIVE_PATH_BYTES 0xd

/* 00039bd0.  Three nested ifs, each with an else that prints a diagnostic and
   leaves the preset NULL standing.

   All three tests are written in the positive form -- the index is not the
   miss marker, the file opened, the allocation worked -- because that is what
   the original's block order says they were.  Each condition is a CMP followed
   by a JZ to a handler placed AFTER the body it guards (JZ 0x00039cc5 at
   00039bfa, JZ 0x00039cb1 at 00039c2e, JZ 0x00039c8d at 00039c54), and each
   body ends by jumping over its own handler (JMP 0x00039ca3 at 00039c8b, JMP
   0x00039cc3 at 00039caf, JMP 0x00039cd7 at 00039cc3, the last two nested one
   inside the other).  Writing the tests the other way round is the same
   behaviour with the handlers laid out in front of the bodies instead
   (ADR-0001).

   The outer test is an equality against the -1 fdps_vfs_find_entry returns and
   not an ordering test, so nothing here depends on the index's signedness.

   The innermost handler is the one that carries weight: its body jumps to the
   fclose at 00039ca3 rather than to the exit, so the two arms rejoin ahead of
   the close and an allocation that failed still closes the file it opened.
   The reopen failure is the only path with no fclose, and it is also the only
   one that never opened a file.

   Nothing checks a result on the way through.  Both the fseek and the fread
   discard what they return (ADD ESP,0xc at 00039c72 and ADD ESP,0x10 at
   00039c88, EAX untouched), so a container too short to hold the member the
   directory describes hands back a buffer that was only partly filled and
   nothing in the function notices.

   The result is preset to NULL at 00039bdc and is the malloc's own slot, so
   the three failures all return it without a second store: the value that
   reaches the caller is either the buffer or the preset.  The last three
   instructions before the epilogue are the -od return spill through a
   compiler temporary, not a local of the author's, exactly as in
   fdps_vfs_image_entry_count above.

   The two entry fields are reached through a base with the table offset folded
   into it -- 0x1e for the size and 0x27 for the start -- and both are written
   out here as the table offset plus the field offset so that neither constant
   reads as something it is not.

   The archive path is copied to the stack before it is opened rather than
   being handed to fopen where it lies, and the copy is a fixed 13 bytes.  The
   16-byte buffer is what the frame gives it, and the frame comes out at the
   original's SUB ESP,0x28.  Two of its slots are not where the original put
   them: the original keeps all five locals at -0x8 through -0x18 with the
   return temporary at -0x4, while this source compiles to entry_index at -0x4
   and the temporary at -0x18, the other four and the path buffer unmoved
   (verified against the emitted VFS.OBJ).  The declarations are in the order
   that puts those four where the original has them, which is the same order
   that reproduces fdps_vfs_find_entry's frame exactly; what changes with the
   fifth slot is which of entry_index and the temporary the compiler hoists,
   and that is allocation rather than behaviour (ADR-0001).  Everything else
   the object file holds does line up, down to the order the three format
   strings and the "rb" land in the literal pool.

   entry_length is spent twice, on the malloc and on the fread, and is never
   compared, so its signedness is not observable; unsigned is what the u32 in
   resource_info/vfs.md and both of its consumers make it.  file_offset is what
   MOV EAX,[EAX + 0x27] loads and PUSH hands to fseek, a whole dword either
   way, and the seek's own parameter is long. */
void *fdps_vfs_load_file(char *name, void *vfs)
{
    char archive_path[16];
    FILE *fp;
    void *buffer;
    long file_offset;
    unsigned int entry_length;
    int entry_index;

    buffer = NULL;
    entry_index = fdps_vfs_find_entry(name, vfs);
    if (entry_index != -1) {
        memcpy(archive_path,
               (char *)vfs + VFS_HANDLE_ARCHIVE_PATH_OFFSET,
               VFS_HANDLE_ARCHIVE_PATH_BYTES);
        fp = fopen(archive_path, "rb");
        if (fp != NULL) {
            entry_length = *(unsigned int *)((char *)vfs
                                             + entry_index * VFS_ENTRY_SIZE
                                             + VFS_HANDLE_ENTRY_TABLE_OFFSET
                                             + VFS_ENTRY_SIZE_FIELD_OFFSET);
            buffer = malloc(entry_length);
            if (buffer != NULL) {
                file_offset = *(long *)((char *)vfs
                                        + entry_index * VFS_ENTRY_SIZE
                                        + VFS_HANDLE_ENTRY_TABLE_OFFSET
                                        + VFS_ENTRY_START_FIELD_OFFSET);
                fseek(fp, file_offset, SEEK_SET);
                fread(buffer, entry_length, 1, fp);
            } else {
                printf("Can't allocate memory for VFS_file: %s(%dbytes)\n",
                       name, entry_length);
            }
            fclose(fp);
        } else {
            printf("Can't open the source VFS_file: %s\n", archive_path);
        }
    } else {
        printf("Can't find the string: %s\n", name);
    }
    return buffer;
}

/* How much of a container's header is copied to the stack before either of the
   two fields is read: PUSH 0xb at 00039d03, the memmove count.  It is the 11
   bytes src/fdpstype.h models of a .VFS header and not the 35 the file gives
   it -- the 24-byte packer signature at 0x0b is past everything any
   instruction in the program reads (resource_info/vfs.md). */
#define VFS_IMAGE_HEADER_BYTES sizeof(struct fdps_vfs_image_header)

/* 00039cf0.  One loop with one branch inside it, in the same -od shape as
   fdps_vfs_find_entry above: the test at 00039d1c, the increment at 00039d26
   sitting ahead of the body, and the body from 00039d2b jumping back over it.
   The body's branch is TEST EAX,EAX / JNZ 0x00039d8b at 00039d74, whose taken
   side rejoins that increment and whose fall-through fills in both answers and
   jumps to the single exit -- so the match is a break out of the loop, not a
   second return, and the NULL written to [EBP + -0x8] at 00039cfc, before the
   header is even copied, is what survives a loop that runs out.

   What this reader is handed is a whole container resident in memory -- header,
   directory and members in one block -- and not a handle from fdps_vfs_open, so
   both fields it needs come off the header at the front of the image itself and
   the answer it returns aims back into that same block.  See vfs.h.

   Everything it reads it reads out of a copy.  The header goes to the stack at
   00039d0d, the 26-byte directory entry at 00039d41 and the entry's 13-byte
   name at 00039d53, and all three copies are memmove -- CALL 0x0003d514 three
   times -- where fdps_vfs_open and fdps_vfs_load_file copy with memcpy at
   0x000435bc.  None of the three regions can overlap its source, so which of
   the two is called is not observable; it is what the original calls, and
   spelling it memcpy would put a different symbol in the link.

   The entry count is read as a whole dword and compared UNSIGNED: CMP EAX,[EBP
   + -0x19] / JC 0x00039d2b at 00039d1f.  Both halves of that are the opposite
   of what the rest of the module does with the same field --
   fdps_vfs_image_entry_count takes one byte of it off the same kind of header,
   and fdps_vfs_find_entry takes the handle's copy a byte at a time -- so this
   is the one reader that can reach a member past 255.  Writing the compare
   signed would search nothing at all in a container claiming 0x80000000 or more
   (rebuild_info/pitfalls.md).

   The entry-table offset is read UNSIGNED too, XOR EAX,EAX / MOV AX,[EBP +
   -0x1b] at 00039d34, where fdps_vfs_open sign-extends the same header field
   into its seek (MOVSX at 00039b7d).  Every shipped container has 35 there
   (resource_info/vfs.md), so the two disagree only about a container no packer
   wrote.

   strupr sits inside the loop and rewrites the caller's own buffer while the
   entry's name is compared raw, exactly as in the two handle searches; vfs.h
   has what that costs the caller.

   out_size is written only on the match, MOV [EDX],EAX at 00039d7e, and there
   is no other store to it anywhere in the body, so a miss leaves the caller's
   slot holding whatever it held.

   The address of the entry is formed from the loop counter every iteration --
   IMUL EDX,[EBP + -0xc],0x1a at 00039d2d -- and the member's own address is
   formed a second time from the entry's field rather than from anything the
   loop kept, which is what -od does with the two expressions written out
   separately.

   The emitted body is the original's instruction for instruction: the three
   memmoves in that order with 0xb, 0x1a and 0xd, the IMUL by 0x1a, the
   zero-extended word load of the table offset, strupr on the argument and
   strcmp against the copy, the dword 0x0d into the entry copy stored through
   out_size and the dword at 0x16 added to the image pointer, and the same two
   jumps out of the loop.  What differs is the frame, SUB ESP,0x44 against the
   original's 0x4c, and it differs only in space nothing reads: the buffers come
   out 28 + 16 + 12 bytes deep followed by three dword slots, where the original
   leaves the header copy 16 bytes and has a fourth dword slot it never touches.
   Every offset into a buffer is the same offset into the same buffer -- the
   size field is 13 into the entry copy either way (verified against the emitted
   VFS.OBJ).  Frame allocation is not part of the standard (ADR-0001), and there
   is no declaration order that recovers those eight bytes without inventing a
   local the function does not have. */
void *fdps_vfs_image_get_entry(struct fdps_vfs_image_header *image, char *name,
                               unsigned int *out_size)
{
    char entry[VFS_ENTRY_SIZE];
    char entry_name[16];
    struct fdps_vfs_image_header header;
    unsigned int index;
    void *member;

    member = NULL;
    memmove(&header, image, VFS_IMAGE_HEADER_BYTES);
    for (index = 0; index < header.entry_count; index++) {
        memmove(entry,
                (char *)image + index * VFS_ENTRY_SIZE
                    + header.entry_table_offset,
                VFS_ENTRY_SIZE);
        memmove(entry_name, entry, VFS_ENTRY_NAME_BYTES);
        if (strcmp(entry_name, strupr(name)) == 0) {
            *out_size = *(unsigned int *)(entry + VFS_ENTRY_SIZE_FIELD_OFFSET);
            member = (char *)image
                     + *(unsigned int *)(entry + VFS_ENTRY_START_FIELD_OFFSET);
            break;
        }
    }
    return member;
}

/* 00029400.  One branch in the body, CMP dword ptr [EAX],0x0 / JNZ 0x00029438
   at 00029424, and the arm it guards ends in exit, so the ADD ESP,0x4 that
   follows the call at 00029435 is stack cleanup no execution reaches.  The
   frame is SUB ESP,0x0: there is no local here, and the loader's answer goes
   from EAX into the caller's slot without passing through one.

   Which argument is which comes from the call sites rather than from anything
   in the body, and all thirteen agree.  The three pushes are the out slot, the
   query and the container handle in that order -- MOV EAX,0x63fd8 / PUSH, MOV
   EAX,0x6160c / PUSH, MOV EAX,[EBP + 0x14] / PUSH at 0001893c through
   0001894b, the first of the nine in fdps_load_data_tables, and the same shape
   at the other four sites -- fdps_deploy_wave pushes a global there too, MOV
   EAX,0x60140 / PUSH at 00023899, and only the remaining three pass a frame
   local, LEA EAX,[EBP - 0x28] at 00017d3b, LEA EAX,[EBP - 0x10] at 00021981
   and the indexed LEA at 00021a1a.  So [EBP + 0x14] is the handle, [EBP +
   0x18] the query and [EBP + 0x1c] the slot.  The pair of pushes that builds the inner call then reverses the
   first two, PUSH [EBP + 0x14] before PUSH [EBP + 0x18] at 0002940f and
   00029413, which is what makes the handle fdps_vfs_load_file's second
   argument and the query its first.

   The pointer that gets tested is the one in the caller's slot, not the
   register that produced it: MOV EDX,[EBP + 0x1c] / MOV [EDX],EAX stores it at
   0002941c, and MOV EAX,[EBP + 0x1c] / CMP dword ptr [EAX],0x0 loads it back
   at 00029421.  So the answer is published before it is known to be good, the
   same way fdps_baseani_get_entry_or_exit publishes its global.

   No diagnostic is printed here.  fdps_vfs_load_file has already said which of
   its three failures happened, and what this function adds is the pause that
   keeps that line readable -- fdps_wait_any_key spins until the next make code
   arrives -- before exit(1) ends the process.

   The emitted VFS.OBJ carries this function instruction for instruction as the
   original has it, frame and dead cleanup included, differing only in that the
   compiler reaches the two pushed arguments with PUSH dword ptr [EBP + n] where
   the original loads each through EAX first.  That is instruction selection and
   not behaviour (ADR-0001). */
void fdps_vfs_load_file_or_exit(void *vfs, char *name, void **out)
{
    *out = fdps_vfs_load_file(name, vfs);
    if (*out == NULL) {
        fdps_wait_any_key();
        exit(1);
    }
}

/* 0002a140.  Two branches, and the arm each one guards ends the process, so
   the ADD ESP,0x4 that follows both exits -- 0002a17a and 0002a1c8 -- is stack
   cleanup no execution reaches, and there is exactly one path out of the
   function.

   Which argument is which comes from the call sites.  fdps_title_screen pushes
   the member name first and the container second, MOV EAX,0x61e24 / PUSH at
   0002a3be followed by MOV EAX,0x60128 / PUSH at 0002a3c4, so the container
   "MISC.VFS" lands at [EBP + 0x14] and the member name at [EBP + 0x18];
   fdps_play_vfs_animation builds the same pair the other way round at 0001eb0c
   and 0001eb10, its own argument as the member name and the same "MISC.VFS"
   literal as the container.  Every one of the forty-odd call sites cleans with
   ADD ESP,0x8 and takes the answer out of EAX.

   The failure message belongs to the container and not to the member: PUSH
   [EBP + 0x14] at 0002a161 is the container path, and the string at 0x61df8 is
   "file not found: '%s'".  So a container that will not open names itself,
   while a member that is not in the container prints nothing here at all --
   fdps_vfs_load_file has already printed which of its three failures happened
   and this function only adds the pause.  The two diagnostics therefore look
   nothing alike, which is worth keeping: the first says the archive, the second
   says the member.

   The middle of the body is fdps_vfs_load_file_or_exit's, expanded where it
   stands rather than called.  The original does the load, the store through a
   pointer to the result slot and the NULL test inline -- CALL 0x00039bd0 at
   0002a1a7 with no call to 00029400 anywhere in the body -- and the six frame
   slots at [EBP - 0x10] through [EBP - 0x24] are that expansion's copies of the
   three arguments it would otherwise have passed.  Writing a call to the
   wrapper here would put a CALL in the rebuild the original does not have, so
   the body stays expanded.  What the extra slots cost is frame size and
   nothing else: the store at 0002a1b2 goes through the address of [EBP - 0x8]
   and the test at 0002a1b7 reloads through the same address, which is the
   local itself either way (ADR-0001).  The emitted VFS.OBJ carries the rest of
   the body instruction for instruction, dead cleanup included, and differs in
   three ways that are all allocation or instruction selection: the frame is SUB
   ESP,0xc against the original's 0x24, the twenty-four bytes of difference
   being the six argument copies and the twelve moves that fill them; the three
   slots come out with loaded_block at -0x4, vfs at -0x8 and the -od return
   temporary at -0xc, where the original has vfs at -0xc, loaded_block at -0x8
   and the temporary at -0x4, and no declaration order recovers that layout
   while the six copies are missing from between them; and each pushed argument
   is reached with PUSH dword ptr [EBP + n] where the original loads it through
   EAX first, which is the same difference fdps_vfs_load_file_or_exit above
   shows.

   free(vfs) is on the single exit path only.  Neither failure arm reaches it,
   and neither needs to: both end the process.  The buffer that comes back is
   the malloc fdps_vfs_load_file made and it belongs to the caller. */
void *fdps_vfs_load_entry(char *vfs_path, char *entry_name)
{
    void *loaded_block;
    void *vfs;

    vfs = fdps_vfs_open(vfs_path);
    if (vfs == NULL) {
        printf("file not found: '%s'\n", vfs_path);
        exit(1);
    }
    loaded_block = fdps_vfs_load_file(entry_name, vfs);
    if (loaded_block == NULL) {
        fdps_wait_any_key();
        exit(1);
    }
    free(vfs);
    return loaded_block;
}
