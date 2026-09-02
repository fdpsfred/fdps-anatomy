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
#include "movegrid.h"
#include "rsrc.h"
#include "vfs.h"

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

/* Bytes of stack the member names are formatted into.  SUB ESP,0x38 at
   000227e6 with the six other locals occupying [EBP-0x18]..[EBP-0x4] leaves
   exactly 32 bytes at [EBP-0x38], and every name built below is an 8.3 name
   that fits several times over. */
#define RESOURCE_NAME_SIZE 32

/* DSC%02d.DAT is one dword layer count followed by one 0x20-byte descriptor
   record per layer, so the record for layer n begins at dword index n * 8 + 1
   -- LEA EAX,[EAX*0x8 + 0x0] / INC EAX at 000229a2, kept in its own local
   because every one of the eight scatter statements re-forms the address from
   it.  Six int fields follow at dword offsets 0..5 of the record and two byte
   fields sit at byte offsets 0x18 and 0x1c of it.

   Both byte fields are read a byte at a time -- MOV DL,byte ptr [EAX + 0x18]
   at 00022a79 and MOV AL,byte ptr [EAX + 0x1c] at 00022a92 -- not as a dword
   narrowed afterwards. */
#define LAYER_RECORD_DWORDS 8
#define LAYER_DRAW_DEPTH_OFFSET 0x18
#define LAYER_TILE_ATTR_MODE_OFFSET 0x1c

/* Byte +1 and byte +2 of the resident MAP%02d.DAT header, widened with
   AND EAX,0xff at 000228e7 and 000228f9: unsigned, so a count of 0x80 and up
   is 128 and up and not a negative number (contract C). */
#define MAP_PLAYER_SLOT_COUNT_OFFSET 1
#define MAP_CHAR_SPAWN_COUNT_OFFSET 2

/* The tile map header's two signed 16-bit dimensions, both read MOVSX at
   00022b85 and 00022b91 (gamedata.h). */
#define TILE_MAP_WIDTH_OFFSET 7
#define TILE_MAP_HEIGHT_OFFSET 9

/* 000227e0.  Everything the chapter's field phase draws from, freed and
   reloaded in one pass, and the only writer of data_fdps_scene_layer_count.
   No argument and no result: the chapter it loads is
   data_fdps_chapter_current_chapter_id, and both call sites push nothing and
   look at nothing afterwards (00022bec and 00023f5d).

   The layer-freeing loop at the top runs against the count the PREVIOUS
   chapter left behind, because the count is not replaced until the new
   descriptor file has been read.  The slots it frees are not nulled, so
   between that loop and the reload the three arrays hold dangling pointers;
   nothing runs in between.  On the first call of a run the count is zero and
   the loop does nothing, which is the only reason a freshly started game does
   not free six uninitialised pointers.

   The four singleton blocks are each guarded by their own non-null test and
   the descriptor blob is not -- it is a local, loaded and freed within the
   call.  Nothing else here is guarded: no fdps_vfs_load_entry result is
   tested (that function ends the process rather than returning null), the
   malloc at the end is not tested, and the layer count taken out of the
   descriptor file is not clamped against the six slots the arrays hold.
   Every DSC file the game ships is 36 or 68 bytes, one or two layers
   (resource_info/vfs.md), so the shipped data is what keeps it in range.

   Only "fdetxt%02d.txt" is formatted with the chapter number PLUS ONE
   (INC EAX at 0002286e); every other name below takes the chapter number as
   it stands.  The containers are three: Field.vfs holds the chapter text and
   the resident map block, Field1.vfs the event-code layer, the tile maps and
   the tile sheets, and Field2.vfs the layer descriptors and the tile
   attribute tables.

   The working movement grid is sized from LAYER 0's tile map whatever the
   layer count is, and both dimensions are read MOVSX -- a header word of
   0xffff has to come out as -1, which makes the product negative and the
   allocation four bytes (contract C).  The two dimensions are stamped into
   the block's first two words before fdps_map_grid_reset is called, because
   that function reads its bounds straight back out of them. */
void fdps_field_load_chapter_resources(void)
{
    char resource_name[RESOURCE_NAME_SIZE];
    int *layer_descriptors;
    int layer_record_dword;
    int map_tile_width;
    int map_tile_height;
    int grid_bytes;
    int layer;

    for (layer = 0; layer < data_fdps_scene_layer_count; layer++) {
        free(data_fdps_scene_layer_tile_map_ptrs[layer]);
        free(data_fdps_scene_layer_tile_sheet_ptrs[layer]);
        free(data_fdps_scene_layer_tile_attr_ptr[layer]);
    }

    if (data_fdps_current_chapter_text_ptr != NULL) {
        free(data_fdps_current_chapter_text_ptr);
    }
    sprintf(resource_name, "fdetxt%02d.txt",
            data_fdps_chapter_current_chapter_id + 1);
    data_fdps_current_chapter_text_ptr =
        fdps_vfs_load_entry("Field.vfs", resource_name);

    if (data_fdps_tile_event_data_table_ptr != NULL) {
        free(data_fdps_tile_event_data_table_ptr);
    }
    sprintf(resource_name, "map%02d.dat",
            data_fdps_chapter_current_chapter_id);
    data_fdps_tile_event_data_table_ptr =
        fdps_vfs_load_entry("Field.vfs", resource_name);
    data_fdps_map_player_slot_count = (int)
        data_fdps_tile_event_data_table_ptr[MAP_PLAYER_SLOT_COUNT_OFFSET];
    data_fdps_map_char_spawn_count = (int)
        data_fdps_tile_event_data_table_ptr[MAP_CHAR_SPAWN_COUNT_OFFSET];

    if (data_fdps_map_cell_event_code_layer_ptr != NULL) {
        free(data_fdps_map_cell_event_code_layer_ptr);
    }
    sprintf(resource_name, "M%02d.dtl", data_fdps_chapter_current_chapter_id);
    data_fdps_map_cell_event_code_layer_ptr =
        fdps_vfs_load_entry("Field1.vfs", resource_name);

    sprintf(resource_name, "dsc%02d.dat",
            data_fdps_chapter_current_chapter_id);
    layer_descriptors = fdps_vfs_load_entry("Field2.vfs", resource_name);
    data_fdps_scene_layer_count = *layer_descriptors;

    for (layer = 0; layer < data_fdps_scene_layer_count; layer++) {
        layer_record_dword = layer * LAYER_RECORD_DWORDS + 1;
        data_fdps_scene_layer_scroll_x_accumulator[layer] =
            layer_descriptors[layer_record_dword];
        data_fdps_scene_layer_scroll_offset_y[layer] =
            layer_descriptors[layer_record_dword + 1];
        data_fdps_scene_layer_parallax_factor_x[layer] =
            layer_descriptors[layer_record_dword + 2];
        data_fdps_scene_layer_parallax_factor_y[layer] =
            layer_descriptors[layer_record_dword + 3];
        data_fdps_scene_layer_scroll_x_step[layer] =
            layer_descriptors[layer_record_dword + 4];
        data_fdps_scene_layer_scroll_step_y[layer] =
            layer_descriptors[layer_record_dword + 5];
        data_fdps_scene_layer_draw_depth[layer] =
            ((unsigned char *) (layer_descriptors + layer_record_dword))
                [LAYER_DRAW_DEPTH_OFFSET];
        data_fdps_scene_layer_tile_attr_mode[layer] =
            ((unsigned char *) (layer_descriptors + layer_record_dword))
                [LAYER_TILE_ATTR_MODE_OFFSET];

        sprintf(resource_name, "m%02d%d.mpl",
                data_fdps_chapter_current_chapter_id, layer);
        data_fdps_scene_layer_tile_map_ptrs[layer] =
            fdps_vfs_load_entry("Field1.vfs", resource_name);
        sprintf(resource_name, "m%02d%d.cel",
                data_fdps_chapter_current_chapter_id, layer);
        data_fdps_scene_layer_tile_sheet_ptrs[layer] =
            fdps_vfs_load_entry("Field1.vfs", resource_name);
        sprintf(resource_name, "attr%02d%d.dat",
                data_fdps_chapter_current_chapter_id, layer);
        data_fdps_scene_layer_tile_attr_ptr[layer] =
            fdps_vfs_load_entry("Field2.vfs", resource_name);
    }
    free(layer_descriptors);

    if (data_fdps_battle_move_grid_ptr != NULL) {
        free(data_fdps_battle_move_grid_ptr);
    }
    map_tile_width = (int) *(short *)
        (data_fdps_scene_layer_tile_map_ptrs[0] + TILE_MAP_WIDTH_OFFSET);
    map_tile_height = (int) *(short *)
        (data_fdps_scene_layer_tile_map_ptrs[0] + TILE_MAP_HEIGHT_OFFSET);
    grid_bytes = map_tile_width * map_tile_height * 2 + 4;
    data_fdps_battle_move_grid_ptr = malloc(grid_bytes);
    *(short *) data_fdps_battle_move_grid_ptr = (short) map_tile_width;
    *(short *) (data_fdps_battle_move_grid_ptr + 2) = (short) map_tile_height;
    fdps_map_grid_reset();
}

/* Bytes of stack the village loader formats its two member names into.  SUB
   ESP,0x1c at 00031546 with the counter at [EBP-0x8] and the sheet handle at
   [EBP-0x4] leaves exactly 20 bytes at [EBP-0x1c], and the longer of the two
   names, "fdetxt%02d.txt", is fifteen with its terminator. */
#define VILLAGE_NAME_SIZE 20

/* Stride of one record in the roster block: IMUL EAX,dword ptr [EBP + -0x8],
   0x50 at 000316c4.  A literal and not sizeof(struct fdps_unit_record) for the
   reason roster.c gives -- 0x50 is the block's own layout and the struct agrees
   with it only while it stays byte-packed (rebuild_info/pitfalls.md). */
#define UNIT_RECORD_STRIDE 0x50

/* 00031540.  The village phase's reload of the chapter resources, called by
   fdps_run_village_phase at 00031314 and fdps_run_bar_shop at 00035e78.
   Neither pushes anything in front of the call, neither cleans a stack purge
   after it and neither looks at EAX -- what is left in it is the fclose result
   -- so this takes nothing and returns nothing.

   Straight line apart from four branches, and every one of them is a guard
   rather than a fork: the null test on the chapter text, the pair that decides
   whether the old map unit array is a block of its own, the layer-freeing loop
   and the null test on the sprite cache.  The two loops are the compiler's
   -od shape, the body sitting below the increment with a JMP back to the test.

   The order the two member names are formatted in is what separates them:
   "fdetxt%02d.txt" takes the chapter number PLUS ONE (INC EAX at 00031568)
   and "Shop%02d.dat" takes it as it stands (PUSH dword ptr [0x00069cf4] at
   00031593).  Both come out of the same container and both go through the same
   20-byte stack buffer.

   The map unit array is taken over rather than copied: the roster's own block
   becomes the map array and the count follows it.  The free in front of that
   is guarded by BOTH a non-zero count and a pointer that differs from the
   roster's, so the second call in a row finds the alias already in place and
   frees nothing -- without that second test the roster block would be released
   while data_fdps_roster_array_ptr still pointed at it.

   The shop table has no such guard and no free of its own: the pointer is
   overwritten with the new block and the old one is lost.  That is the
   original's behaviour and not an omission here.

   The sprite cache is emptied and refilled from the party, one group per
   roster member keyed on the record's portrait_id -- MOV AL,byte ptr [EAX +
   0x7] / AND EAX,0xff at 000316d0, so the field is widened unsigned (contract
   C).  Each slot number that comes back is discarded; what the loop is for is
   the side effect of the group being resident, and it is fdps_deploy_wave that
   later stores slot numbers into the records.  Emptying the cache first is
   what makes the first member's group land in slot 0. */
void fdps_load_field_chapter_resources(void)
{
    char resource_name[VILLAGE_NAME_SIZE];
    struct fdps_unit_record *roster_member;
    FILE *icon_cel_fp;
    int layer;
    int member;

    if (data_fdps_current_chapter_text_ptr != NULL) {
        free(data_fdps_current_chapter_text_ptr);
    }
    sprintf(resource_name, "fdetxt%02d.txt",
            data_fdps_chapter_current_chapter_id + 1);
    data_fdps_current_chapter_text_ptr =
        fdps_vfs_load_entry("Field.vfs", resource_name);

    sprintf(resource_name, "Shop%02d.dat",
            data_fdps_chapter_current_chapter_id);
    data_fdps_shop_stock_table_ptr =
        fdps_vfs_load_entry("Field.vfs", resource_name);

    if (data_fdps_map_unit_count != 0
        && data_fdps_map_unit_array_ptr != data_fdps_roster_array_ptr) {
        free(data_fdps_map_unit_array_ptr);
    }
    data_fdps_map_unit_count = data_fdps_roster_member_count;
    data_fdps_map_unit_array_ptr = data_fdps_roster_array_ptr;

    for (layer = 0; layer < data_fdps_scene_layer_count; layer++) {
        free(data_fdps_scene_layer_tile_map_ptrs[layer]);
        free(data_fdps_scene_layer_tile_sheet_ptrs[layer]);
        free(data_fdps_scene_layer_tile_attr_ptr[layer]);
    }
    data_fdps_scene_layer_count = 0;

    if (data_fdps_cel_sprite_cache_count != 0) {
        free(data_fdps_cel_sprite_cache_ptr);
    }
    data_fdps_cel_sprite_cache_count = 0;

    icon_cel_fp = fopen("ICON.CEL", "rb");
    for (member = 0; member < data_fdps_roster_member_count; member++) {
        roster_member = (struct fdps_unit_record *)
                        (data_fdps_roster_array_ptr
                         + member * UNIT_RECORD_STRIDE);
        fdps_cache_cel_sprite_group((int) roster_member->portrait_id,
                                    icon_cel_fp);
    }
    fclose(icon_cel_fp);
}
