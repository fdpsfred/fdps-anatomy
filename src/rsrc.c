/* rsrc.c -- chapter resource loading and freeing, and the .CEL sprite cache.
 *
 * See rsrc.h for what each reader's caller has to know.  The archive readers
 * own no state: every block they produce is handed back through a pointer slot
 * the caller supplied and belongs to the caller afterwards.  The sprite cache
 * is the exception -- it is global, it is appended to for the life of the run,
 * and this file is the only thing that writes it.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
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

/* Sprites per group, the IMUL EAX,dword ptr [EBP + 0x14],0xc at 000230b6 that
   turns a group index into a table index.  It is also the element count of
   struct fdps_cel_cache_slot. */
#define CEL_GROUP_SPRITES 12

/* Cache slots the block reserves room for: 0x5A0 bytes of slot table divided
   by the 48 bytes of a slot.  Nothing in the function tests a count against
   it -- it is what the reserved region holds, not a limit anybody enforces. */
#define CEL_CACHE_MAX_SLOTS 30

/* MOV dword ptr [EBP + -0x10],0x5a0 at 00023063: the bytes reserved at the
   base of the cache block for the slot table, and so the offset the first
   group's pixels land at. */
#define CEL_CACHE_TABLE_BYTES \
    (CEL_CACHE_MAX_SLOTS * (int) sizeof(struct fdps_cel_cache_slot))

/* MOV dword ptr [EBP + -0x18],0x2970 at 0002305c: one fixed-size read brings
   back the sheet's offset table, 2652 u32 of it.  That is more entries than
   any sheet has -- ICON.CEL declares 1920 sprites, so 1921 entries -- and the
   read runs on into the first sprites' pixel bytes.  It does no harm because
   only the requested group's thirteen entries are ever looked at, but it does
   mean the reader will not work on a sheet shorter than 0x0f + 0x2970 bytes,
   and neither the count in the header nor the read's return value is
   consulted to find out. */
#define CEL_OFFSET_TABLE_BYTES 0x2970

/* 00023050.  Three exits share one epilogue: the seed branch when the cache is
   empty, the early return from the linear key search on a hit, and the append
   at the end of the miss path.  The count test at 000230ee is CMP
   [0x00069cf0],0x0 / JNZ, so "empty" and "not empty" are the two arms and the
   search only ever runs on a non-empty cache.

   Everything up to that test happens whatever the answer turns out to be: the
   sheet's offset table is read and freed, and the group's thirteen entries are
   copied into the frame, before anything knows whether the group is already
   cached.  A hit therefore still costs a 0x2970-byte read and a malloc/free
   pair, which is why a caller in a drawing loop asking for the same group
   every frame is not as cheap as the "already cached" answer suggests.

   The thirteenth entry is the first sprite of the NEXT group, and is read
   purely to close the last sprite: MOV EAX,[EBP + -0x1c] / SUB EAX,[EBP +
   -0x4c] at 000230d9 is entry twelve minus entry zero, the group's pixel bytes
   in one subtraction.  For the last group on a sheet that thirteenth entry is
   the table's own sentinel, so the arithmetic holds there too.

   The slot's offsets are stored relative to the cache block's base, not to the
   file and not as pointers, which is what lets the block be realloc'd on every
   miss without touching a slot that was already written.

   Neither malloc nor realloc is tested, no fread return is looked at, and
   group_index is used unchecked -- MOV EAX,[EAX] at 000230d1 reads wherever
   the scaled index lands. */
int fdps_cache_cel_sprite_group(int group_index, FILE *fp)
{
    int stream_offsets[CEL_GROUP_SPRITES + 1];
    unsigned int offset_table_bytes;
    unsigned int group_pixel_bytes;
    int table_region_bytes;
    void *offset_table;
    int i;
    int slot;

    offset_table_bytes = CEL_OFFSET_TABLE_BYTES;
    table_region_bytes = CEL_CACHE_TABLE_BYTES;

    fseek(fp, (long) sizeof(struct fdps_cel_header), SEEK_SET);
    offset_table = malloc(offset_table_bytes);
    fread(offset_table, 1, offset_table_bytes, fp);
    for (i = 0; i < CEL_GROUP_SPRITES + 1; i++) {
        stream_offsets[i] =
            ((int *) offset_table)[group_index * CEL_GROUP_SPRITES + i];
    }
    group_pixel_bytes = stream_offsets[CEL_GROUP_SPRITES] - stream_offsets[0];
    free(offset_table);

    if (data_fdps_cel_sprite_cache_count == 0) {
        data_fdps_cel_sprite_cache_group_ids[0] = group_index;
        data_fdps_cel_sprite_cache_ptr =
            malloc(table_region_bytes + group_pixel_bytes);
        fseek(fp, stream_offsets[0], SEEK_SET);
        fread(data_fdps_cel_sprite_cache_ptr + table_region_bytes, 1,
              group_pixel_bytes, fp);
        for (i = 0; i < CEL_GROUP_SPRITES; i++) {
            ((struct fdps_cel_cache_slot *)
             data_fdps_cel_sprite_cache_ptr)[0].sprite_offset[i] =
                table_region_bytes + (stream_offsets[i] - stream_offsets[0]);
        }
        data_fdps_cel_sprite_cache_count++;
        data_fdps_cel_sprite_cache_buffer_used =
            group_pixel_bytes + table_region_bytes;
        slot = 0;
    } else {
        for (i = 0; i < data_fdps_cel_sprite_cache_count; i++) {
            if (group_index == data_fdps_cel_sprite_cache_group_ids[i]) {
                return i;
            }
        }
        data_fdps_cel_sprite_cache_group_ids[i] = group_index;
        data_fdps_cel_sprite_cache_ptr =
            realloc(data_fdps_cel_sprite_cache_ptr,
                    data_fdps_cel_sprite_cache_buffer_used
                        + group_pixel_bytes);
        fseek(fp, stream_offsets[0], SEEK_SET);
        fread(data_fdps_cel_sprite_cache_ptr
                  + data_fdps_cel_sprite_cache_buffer_used,
              1, group_pixel_bytes, fp);
        for (i = 0; i < CEL_GROUP_SPRITES; i++) {
            ((struct fdps_cel_cache_slot *)
             data_fdps_cel_sprite_cache_ptr)
                [data_fdps_cel_sprite_cache_count].sprite_offset[i] =
                data_fdps_cel_sprite_cache_buffer_used
                    + (stream_offsets[i] - stream_offsets[0]);
        }
        data_fdps_cel_sprite_cache_buffer_used += group_pixel_bytes;
        data_fdps_cel_sprite_cache_count++;
        slot = data_fdps_cel_sprite_cache_count - 1;
    }
    return slot;
}
