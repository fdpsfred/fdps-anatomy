/* rsrc.c -- chapter resource loading and freeing, and the .CEL sprite cache.
 *
 * See rsrc.h for what each reader's caller has to know.  The file owns no
 * state: every block a reader produces is handed back through a pointer slot
 * the caller supplied and belongs to the caller afterwards.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "rsrc.h"

/* The archive's offset table starts at file offset 6, past a six-byte header
   the reader never looks at, and holds one u32 per entry.  ADD EAX,0x6 onto
   LEA EAX,[EAX*0x4 + 0x0] at 00022e9c is the whole of the address
   arithmetic. */
#define ARCHIVE_OFFSET_TABLE_START 6

/* One fread of eight bytes brings back two adjacent table entries at once --
   PUSH 0x8 / PUSH 0x1 at 00022eb7 -- so the scratch block malloc'd for it is
   eight bytes and the second dword read out of it is the start of the entry
   after the one being loaded. */
#define ARCHIVE_OFFSET_PAIR_BYTES 8

/* 00022e30.  The two branches are the only ones in the body: CMP dword ptr
   [EAX],0x0 / JZ over the free at 00022e42, and CMP dword ptr [EBP + -0x4],0x0
   / JNZ over the not-found block at 00022e6a.  Everything else is straight
   line, and the not-found block ends in CALL exit, so the ADD ESP,0x4 at
   00022e85 that follows it is never executed.

   The caller's slot is re-read from [EBP + 0x18] before every use rather than
   held in a register across the calls, and both malloc results are stored
   straight back into it: the scratch block for the offset pair lives in the
   caller's slot too, and is freed out of it again before the entry block
   replaces it.  A caller that looked at its own pointer between those steps
   would see the scratch block, not the entry.

   Neither malloc is tested and neither fread's return is looked at -- the
   returned EAX is dead in both cases -- so a short archive or an exhausted
   heap is not detected here.  The size handed to malloc and to the second
   fread is the plain difference of the two table entries, MOV EAX,[EBP + -0xc]
   / SUB EAX,[EBP + -0x10] at 00022edd, with no test that it is positive. */
void fdps_load_indexed_archive_entry(char *filename, void **buffer,
                                     int entry_index)
{
    FILE *archive;
    int entry_start;
    int next_entry_start;
    int entry_size;

    if (*buffer != NULL) {
        free(*buffer);
    }
    archive = fopen(filename, "rb");
    if (archive == NULL) {
        printf("\n\n File not found %s!!! \a\n", filename);
        exit(1);
    }
    *buffer = malloc(ARCHIVE_OFFSET_PAIR_BYTES);
    fseek(archive, entry_index * 4 + ARCHIVE_OFFSET_TABLE_START, SEEK_SET);
    fread(*buffer, 1, ARCHIVE_OFFSET_PAIR_BYTES, archive);
    entry_start = ((int *) *buffer)[0];
    next_entry_start = ((int *) *buffer)[1];
    entry_size = next_entry_start - entry_start;
    free(*buffer);
    *buffer = malloc(entry_size);
    fseek(archive, entry_start, SEEK_SET);
    fread(*buffer, 1, entry_size, archive);
    fclose(archive);
}
