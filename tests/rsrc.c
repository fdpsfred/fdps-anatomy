/* tests/rsrc.c -- cover for src/rsrc.c.
 *
 * Four subjects, each starting at its own banner below: the file opens on
 * fdps_load_indexed_archive_entry at 00022e30, then comes
 * fdps_cache_cel_sprite_group at 00023050, which reads the real ICON.CEL
 * instead of a fixture and says there why, then
 * fdps_field_load_chapter_resources at 000227e0, which reads the three real
 * field containers and says there why, and last
 * fdps_load_field_chapter_resources at 00031540, the village side's reload,
 * which needs only FIELD.VFS and the same real sheet.
 *
 * Expected values come from the assembly at 00022e30: ADD EAX,0x6 onto LEA
 * EAX,[EAX*0x4 + 0x0] for the seek to the table, PUSH 0x8 / PUSH 0x1 for the
 * eight bytes read there, MOV EAX,[EBP + -0xc] / SUB EAX,[EBP + -0x10] for the
 * entry's size, and the PUSH 0x0 in front of both fseek calls for SEEK_SET.
 * None of them is read off the emitted C.
 *
 * The archive the tests read is written by the fixture below rather than
 * staged out of fdps_game_files through tests/gamefile.lst, and that is not a
 * stand-in for a real game file: no file shipped with the game has this
 * layout, which is why nothing in the image calls this reader.  So the file is
 * built from the constants the assembly itself hard-codes -- header six bytes,
 * u32 offsets from there, size as the difference of two adjacent ones -- and
 * what the tests measure is whether the emitted C addresses that file the way
 * the assembly does.  A reader that took the header as four bytes, or scaled
 * the index by eight, or took the size from the wrong pair, reads the wrong
 * bytes out of this file and says so.
 *
 * Three entries plus the sentinel, with deliberately unequal sizes and a
 * distinct byte ramp each, so an entry can be identified by its content alone
 * and a size taken from the wrong table slot is visible in the block the heap
 * hands back.
 *
 * The not-found path is not exercised anywhere below: it ends in exit(1), so a
 * test of it would take the whole run with it.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "rsrc.h"

/* 8.3, and written into the run directory the test image starts in. */
#define ARCHIVE_NAME "IDXARC.DAT"

/* Six bytes the reader never looks at -- it seeks straight past them. */
#define HEADER_BYTES 6

/* Three entries and the sentinel that closes the last one. */
#define TABLE_SLOTS 4

#define ENTRY0_START (HEADER_BYTES + TABLE_SLOTS * 4)
#define ENTRY0_SIZE 16
#define ENTRY1_START (ENTRY0_START + ENTRY0_SIZE)
#define ENTRY1_SIZE 5
#define ENTRY2_START (ENTRY1_START + ENTRY1_SIZE)
#define ENTRY2_SIZE 200
#define ARCHIVE_END (ENTRY2_START + ENTRY2_SIZE)

/* First byte of each entry's ramp; byte i of entry n is FIRST_n + i. */
#define ENTRY0_FIRST 0xa0
#define ENTRY1_FIRST 0xb0
#define ENTRY2_FIRST 0xc0

/* One block of the heap is enough to tell a slot that was freed from one that
   was leaked, and small enough to be nothing. */
#define STAGED_BLOCK_BYTES 64

/* Comfortably more loads than the CRT has file handles for (_NFILES is 20),
   so a reader that never closed its file would run out during the loop. */
#define REPEATED_LOADS 24

static int archive_ready = 0;

static void write_dword(FILE *fp, int value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char) (value & 0xff);
    bytes[1] = (unsigned char) ((value >> 8) & 0xff);
    bytes[2] = (unsigned char) ((value >> 16) & 0xff);
    bytes[3] = (unsigned char) ((value >> 24) & 0xff);
    fwrite(bytes, 1, 4, fp);
}

static void write_ramp(FILE *fp, int first_byte, int count)
{
    unsigned char byte;
    int i;

    for (i = 0; i < count; i++) {
        byte = (unsigned char) ((first_byte + i) & 0xff);
        fwrite(&byte, 1, 1, fp);
    }
}

/* Builds the archive on disk and answers whether it is there and the right
   length.  Every test asks first and refuses to call the reader when the
   answer is no: a missing file sends the reader down its not-found path,
   which exits the process instead of failing a check. */
static int stage_archive(void)
{
    FILE *fp;
    long written;

    fp = fopen(ARCHIVE_NAME, "wb");
    if (fp == NULL) {
        return 0;
    }
    write_ramp(fp, 'A', HEADER_BYTES);
    write_dword(fp, ENTRY0_START);
    write_dword(fp, ENTRY1_START);
    write_dword(fp, ENTRY2_START);
    write_dword(fp, ARCHIVE_END);
    write_ramp(fp, ENTRY0_FIRST, ENTRY0_SIZE);
    write_ramp(fp, ENTRY1_FIRST, ENTRY1_SIZE);
    write_ramp(fp, ENTRY2_FIRST, ENTRY2_SIZE);
    written = ftell(fp);
    fclose(fp);
    return written == (long) ARCHIVE_END;
}

static void ensure_archive(void)
{
    if (!archive_ready) {
        archive_ready = stage_archive();
    }
}

/* How many bytes of the block continue the ramp that starts at first_byte.
   count back means the whole entry arrived; anything less locates the first
   byte that came from somewhere else in the file. */
static int leading_ramp_bytes(void *block, int first_byte, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        if (bytes[i] != (unsigned char) ((first_byte + i) & 0xff)) {
            break;
        }
    }
    return i;
}

/* Used entries currently in the heap.  A used entry becomes a free entry the
   moment it is released -- possibly merged with a neighbour, which is why the
   free entries are not counted and the used ones are. */
static int used_heap_blocks(void)
{
    struct _heapinfo entry;
    int used;

    used = 0;
    entry._pentry = NULL;
    while (_heapwalk(&entry) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY) {
            used++;
        }
    }
    return used;
}

/* Entry 0 starts at the first u32 of the table, so its content pins the seek
   to file offset 6 and the SEEK_SET origin at once: a table read from offset 4
   or from the current position brings back other bytes entirely. */
static void loads_entry_zero(void)
{
    void *buffer;

    ensure_archive();
    CHECK_EQ(archive_ready, 1);
    if (!archive_ready) {
        return;
    }
    buffer = NULL;
    fdps_load_indexed_archive_entry(ARCHIVE_NAME, &buffer, 0);
    CHECK_EQ(buffer != NULL, 1);
    CHECK_EQ(leading_ramp_bytes(buffer, ENTRY0_FIRST, ENTRY0_SIZE),
             ENTRY0_SIZE);
    free(buffer);
}

/* Entry 1 is five bytes between two much larger ones, so it also says the
   size came from its own pair of table slots rather than from a neighbour's:
   sixteen or two hundred bytes of ramp would not match here. */
static void loads_entry_one(void)
{
    void *buffer;

    ensure_archive();
    CHECK_EQ(archive_ready, 1);
    if (!archive_ready) {
        return;
    }
    buffer = NULL;
    fdps_load_indexed_archive_entry(ARCHIVE_NAME, &buffer, 1);
    CHECK_EQ(leading_ramp_bytes(buffer, ENTRY1_FIRST, ENTRY1_SIZE),
             ENTRY1_SIZE);
    free(buffer);
}

/* Entry 2 is the one that pins the index scaling: at four bytes per slot its
   pair is the u32s at file offsets 14 and 18, and at any other stride the
   reader lands on a different pair and copies a different part of the file. */
static void loads_entry_two(void)
{
    void *buffer;

    ensure_archive();
    CHECK_EQ(archive_ready, 1);
    if (!archive_ready) {
        return;
    }
    buffer = NULL;
    fdps_load_indexed_archive_entry(ARCHIVE_NAME, &buffer, 2);
    CHECK_EQ(leading_ramp_bytes(buffer, ENTRY2_FIRST, ENTRY2_SIZE),
             ENTRY2_SIZE);
    free(buffer);
}

/* The block is malloc'd with the difference of the two table entries, so the
   heap's own idea of its size has to bracket that difference: at least the
   entry's size, since the reader then freads that many bytes into it, and
   nowhere near the next entry's.  The bounds are loose on purpose -- the
   allocator rounds a request up by an amount this test has no business
   predicting -- and the fine discrimination between one entry's size and
   another's is done by the content checks above, where a size read from the
   wrong pair of table slots comes with a wrong start offset too. */
static void sizes_the_block_from_the_offset_difference(void)
{
    void *small_entry;
    void *large_entry;
    long small_block_bytes;
    long large_block_bytes;

    ensure_archive();
    CHECK_EQ(archive_ready, 1);
    if (!archive_ready) {
        return;
    }
    small_entry = NULL;
    fdps_load_indexed_archive_entry(ARCHIVE_NAME, &small_entry, 1);
    small_block_bytes = (long) _msize(small_entry);
    large_entry = NULL;
    fdps_load_indexed_archive_entry(ARCHIVE_NAME, &large_entry, 2);
    large_block_bytes = (long) _msize(large_entry);

    CHECK_EQ(small_block_bytes >= (long) ENTRY1_SIZE, 1);
    CHECK_EQ(small_block_bytes < (long) ENTRY2_SIZE, 1);
    CHECK_EQ(large_block_bytes >= (long) ENTRY2_SIZE, 1);

    free(small_entry);
    free(large_entry);
}

/* CMP dword ptr [EAX],0x0 / JZ guards a single CALL free, so a slot that
   already holds a block gives that block back before the entry replaces it.
   The measurement is the difference between the two cases: starting from null
   the heap ends one used entry up, and starting from a live block it ends
   level, because the old block went back.  The eight-byte scratch block does
   not show in either count -- it is freed before the reader returns. */
static void frees_a_live_slot_before_loading(void)
{
    void *buffer;
    int before;
    int from_null;
    int from_live;

    ensure_archive();
    CHECK_EQ(archive_ready, 1);
    if (!archive_ready) {
        return;
    }
    buffer = NULL;
    before = used_heap_blocks();
    fdps_load_indexed_archive_entry(ARCHIVE_NAME, &buffer, 0);
    from_null = used_heap_blocks() - before;
    free(buffer);

    buffer = malloc(STAGED_BLOCK_BYTES);
    before = used_heap_blocks();
    fdps_load_indexed_archive_entry(ARCHIVE_NAME, &buffer, 0);
    from_live = used_heap_blocks() - before;
    free(buffer);

    CHECK_EQ(from_null, 1);
    CHECK_EQ(from_live, 0);
}

/* MOV dword ptr [EDX],EAX at 00022f02 stores the second malloc's result into
   the caller's slot, so a slot that arrived holding something else comes back
   holding the entry.  What is asserted is the content, not the address: the
   staged block is freed on the way in, and an allocator is entitled to hand
   the same address straight back for the entry, so an address that did not
   change says nothing either way. */
static void leaves_the_entry_in_the_callers_slot(void)
{
    void *buffer;
    unsigned char *staged;
    int i;

    ensure_archive();
    CHECK_EQ(archive_ready, 1);
    if (!archive_ready) {
        return;
    }
    staged = (unsigned char *) malloc(STAGED_BLOCK_BYTES);
    for (i = 0; i < STAGED_BLOCK_BYTES; i++) {
        staged[i] = 0xee;
    }
    buffer = staged;
    fdps_load_indexed_archive_entry(ARCHIVE_NAME, &buffer, 1);
    CHECK_EQ(buffer != NULL, 1);
    CHECK_EQ(leading_ramp_bytes(buffer, ENTRY1_FIRST, ENTRY1_SIZE),
             ENTRY1_SIZE);
    free(buffer);
}

/* Two mallocs and two frees in one call, one of them into and out of the
   caller's own slot.  A walkable heap afterwards is what says the scratch
   block was released by its own address and the entry block was not released
   twice. */
static void heap_is_intact_after_a_load(void)
{
    void *buffer;

    ensure_archive();
    CHECK_EQ(archive_ready, 1);
    if (!archive_ready) {
        return;
    }
    buffer = NULL;
    fdps_load_indexed_archive_entry(ARCHIVE_NAME, &buffer, 2);
    CHECK_EQ(_heapchk(), _HEAPOK);
    free(buffer);
}

/* CALL fclose at 00022f31 gives the handle back, so the reader can be called
   as often as the caller likes.  Twenty-four loads is past _NFILES: a reader
   that leaked its handle does not fail this check, it takes the run down at
   its own fopen, which is the detection.  The check that survives to run is
   that a file can still be opened afterwards. */
static void closes_the_archive_each_time(void)
{
    void *buffer;
    FILE *probe;
    int i;
    int intact_loads;

    ensure_archive();
    CHECK_EQ(archive_ready, 1);
    if (!archive_ready) {
        return;
    }
    intact_loads = 0;
    buffer = NULL;
    for (i = 0; i < REPEATED_LOADS; i++) {
        fdps_load_indexed_archive_entry(ARCHIVE_NAME, &buffer, 1);
        if (leading_ramp_bytes(buffer, ENTRY1_FIRST, ENTRY1_SIZE)
            == ENTRY1_SIZE) {
            intact_loads++;
        }
    }
    free(buffer);
    CHECK_EQ(intact_loads, REPEATED_LOADS);

    probe = fopen(ARCHIVE_NAME, "rb");
    CHECK_EQ(probe != NULL, 1);
    if (probe != NULL) {
        fclose(probe);
    }
}

/* ---------------------------------------------------------------------- */
/* fdps_cache_cel_sprite_group at 00023050.
 *
 * These read the real ICON.CEL, staged through tests/gamefile.lst.  They have
 * to: the reader takes a fixed 0x2970-byte bite out of the sheet's offset
 * table whatever the sheet's declared sprite count is, so anything short of a
 * real sheet is not a smaller fixture, it is a file the reader runs off the
 * end of.
 *
 * Expected values come from the assembly and from the layout in
 * src/fdpstype.h, and the numbers they are checked against are read out of
 * ICON.CEL here, independently: PUSH 0xf for the seek to the offset table,
 * IMUL EAX,[EBP + 0x14],0xc at 000230b6 for the twelve-entry group stride, the
 * thirteen-iteration copy loop at 000230a6 (CMP ...,0xd), MOV EAX,[EBP + -0x1c]
 * / SUB EAX,[EBP + -0x4c] at 000230d9 for the group's pixel byte count, and
 * MOV dword ptr [EBP + -0x10],0x5a0 at 00023063 for the slot table the pixels
 * are appended after.  A reader that scaled the group index by anything but
 * twelve, seeked to any base but 0x0f, sized the group from the wrong pair of
 * entries or wrote a slot offset relative to the file instead of to the block
 * lands on other bytes of ICON.CEL and the content checks say so.
 */
#define CEL_NAME "ICON.CEL"

/* struct fdps_cel_header is fifteen bytes and the offset table starts right
   after it. */
#define CEL_TABLE_START 15

/* Sprites per group, and slots the block's table region holds. */
#define CEL_GROUP_SPRITES 12
#define CEL_TABLE_BYTES (30 * (int) sizeof(struct fdps_cel_cache_slot))

/* Bytes of a sprite stream compared against the file.  Every stream in
   ICON.CEL is several hundred bytes, so this stays inside the shortest of
   them; what it has to be is long enough that two different streams cannot
   agree by accident. */
#define CEL_PIXEL_PROBE_BYTES 48

/* Three groups picked apart from each other and away from group 0, so a slot
   index confused with a group id, or a stride of anything but twelve, reads
   somewhere else in the sheet.  Group 0 is included because its offsets are
   the first thing in the table and so pin the 0x0f seek on their own. */
#define CEL_GROUP_A 0
#define CEL_GROUP_B 5
#define CEL_GROUP_C 37

static int cel_ready = 0;

/* Whether the staged sheet is there and long enough to survive the reader's
   fixed-size table read.  Every test asks first: the reader tests no result it
   gets back, so a short file does not fail a check, it reads rubbish. */
static int cel_sheet_present(void)
{
    FILE *fp;
    long size;

    fp = fopen(CEL_NAME, "rb");
    if (fp == NULL) {
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    return size >= (long) (CEL_TABLE_START + 0x2970);
}

static void ensure_cel_sheet(void)
{
    if (!cel_ready) {
        cel_ready = cel_sheet_present();
    }
}

/* The thirteen absolute file offsets group group_index owns, read out of the
   sheet one entry at a time so that this shares no arithmetic with the code
   under test.  Little-endian by hand rather than by fread of an int, because
   what the file holds is a byte order, not this machine's. */
static int read_group_offsets(int group_index, int *offsets)
{
    FILE *fp;
    unsigned char raw[4];
    int i;
    int ok;

    fp = fopen(CEL_NAME, "rb");
    if (fp == NULL) {
        return 0;
    }
    ok = 1;
    for (i = 0; i < CEL_GROUP_SPRITES + 1; i++) {
        fseek(fp, CEL_TABLE_START
                  + (long) (group_index * CEL_GROUP_SPRITES + i) * 4,
              SEEK_SET);
        if (fread(raw, 1, 4, fp) != 4) {
            ok = 0;
            break;
        }
        offsets[i] = (int) raw[0] | ((int) raw[1] << 8)
                     | ((int) raw[2] << 16) | ((int) raw[3] << 24);
    }
    fclose(fp);
    return ok;
}

/* How many of the first count bytes at file_offset in the sheet are also at
   block.  count back means the cached bytes are the file's bytes; anything
   less locates the first byte that came from somewhere else. */
static int matching_pixel_bytes(long file_offset, unsigned char *block,
                                int count)
{
    FILE *fp;
    unsigned char raw;
    int i;

    fp = fopen(CEL_NAME, "rb");
    if (fp == NULL) {
        return -1;
    }
    fseek(fp, file_offset, SEEK_SET);
    for (i = 0; i < count; i++) {
        if (fread(&raw, 1, 1, fp) != 1 || raw != block[i]) {
            break;
        }
    }
    fclose(fp);
    return i;
}

/* Puts the cache back to the state the program starts in.  bss zero is the
   real initial state, so this is not a fixture, it is the state the seed
   branch is defined against. */
static void reset_cel_cache(void)
{
    if (data_fdps_cel_sprite_cache_ptr != NULL) {
        free(data_fdps_cel_sprite_cache_ptr);
        data_fdps_cel_sprite_cache_ptr = NULL;
    }
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_buffer_used = 0;
}

/* The cache block's slot table, which is what the block's first 0x5A0 bytes
   are. */
static struct fdps_cel_cache_slot *cache_slots(void)
{
    return (struct fdps_cel_cache_slot *) data_fdps_cel_sprite_cache_ptr;
}

static int cache_one_group(int group_index)
{
    FILE *fp;
    int slot;

    fp = fopen(CEL_NAME, "rb");
    if (fp == NULL) {
        return -1;
    }
    slot = fdps_cache_cel_sprite_group(group_index, fp);
    fclose(fp);
    return slot;
}

/* The empty-cache arm: CMP dword ptr [0x00069cf0],0x0 / JNZ at 000230ee falls
   through here, and everything it writes is checked -- the key, the count, the
   block, the byte cursor and all twelve slot offsets.  The offsets are what
   pin the whole address chain at once: they are 0x5A0 plus each stream's
   distance from the group's first stream, so they can only come out right if
   the table was read at 0x0f and indexed by twelve. */
static void cel_seeds_the_cache_on_the_first_call(void)
{
    int offsets[CEL_GROUP_SPRITES + 1];
    int slot;
    int group_bytes;

    ensure_cel_sheet();
    CHECK_EQ(cel_ready, 1);
    if (!cel_ready) {
        return;
    }
    CHECK_EQ(read_group_offsets(CEL_GROUP_A, offsets), 1);
    group_bytes = offsets[CEL_GROUP_SPRITES] - offsets[0];

    reset_cel_cache();
    slot = cache_one_group(CEL_GROUP_A);

    CHECK_EQ(slot, 0);
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 1);
    CHECK_EQ(data_fdps_cel_sprite_cache_group_ids[0], CEL_GROUP_A);
    CHECK_EQ(data_fdps_cel_sprite_cache_ptr != NULL, 1);
    CHECK_EQ(data_fdps_cel_sprite_cache_buffer_used,
             CEL_TABLE_BYTES + group_bytes);

    CHECK_EQ(cache_slots()[0].sprite_offset[0], CEL_TABLE_BYTES);
    CHECK_EQ(cache_slots()[0].sprite_offset[7],
             CEL_TABLE_BYTES + (offsets[7] - offsets[0]));
    CHECK_EQ(cache_slots()[0].sprite_offset[11],
             CEL_TABLE_BYTES + (offsets[11] - offsets[0]));

    CHECK_EQ(matching_pixel_bytes(offsets[0],
                                  data_fdps_cel_sprite_cache_ptr
                                      + cache_slots()[0].sprite_offset[0],
                                  CEL_PIXEL_PROBE_BYTES),
             CEL_PIXEL_PROBE_BYTES);
    CHECK_EQ(matching_pixel_bytes(offsets[7],
                                  data_fdps_cel_sprite_cache_ptr
                                      + cache_slots()[0].sprite_offset[7],
                                  CEL_PIXEL_PROBE_BYTES),
             CEL_PIXEL_PROBE_BYTES);
    reset_cel_cache();
}

/* The key hit: CMP EAX,[EDX + 0x64060] / JNZ at 000231cd returns the loop
   index and jumps straight to the epilogue, so nothing is appended and neither
   counter moves.  Asking twice for the same group has to be idempotent -- the
   callers do exactly that, once per drawn unit. */
static void cel_returns_the_cached_slot_for_a_repeated_group(void)
{
    int first_slot;
    int second_slot;
    unsigned int used_after_first;

    ensure_cel_sheet();
    CHECK_EQ(cel_ready, 1);
    if (!cel_ready) {
        return;
    }
    reset_cel_cache();
    first_slot = cache_one_group(CEL_GROUP_B);
    used_after_first = data_fdps_cel_sprite_cache_buffer_used;
    second_slot = cache_one_group(CEL_GROUP_B);

    CHECK_EQ(first_slot, 0);
    CHECK_EQ(second_slot, 0);
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 1);
    CHECK_EQ(data_fdps_cel_sprite_cache_buffer_used, used_after_first);
    reset_cel_cache();
}

/* The miss arm on a non-empty cache: the block is realloc'd, the new group's
   pixels are read in at the old byte cursor and the slot at index count gets
   offsets measured from that cursor, IMUL EAX,[0x00069cf0],0xc at 00023271
   being the slot the count selects.  The first group's slot and pixels are
   re-checked afterwards, because a realloc that moved the block is exactly
   where offsets stored as pointers rather than as base-relative distances
   would come apart. */
static void cel_appends_a_second_group(void)
{
    int offsets_a[CEL_GROUP_SPRITES + 1];
    int offsets_b[CEL_GROUP_SPRITES + 1];
    int slot_b;
    unsigned int used_after_first;

    ensure_cel_sheet();
    CHECK_EQ(cel_ready, 1);
    if (!cel_ready) {
        return;
    }
    CHECK_EQ(read_group_offsets(CEL_GROUP_A, offsets_a), 1);
    CHECK_EQ(read_group_offsets(CEL_GROUP_B, offsets_b), 1);

    reset_cel_cache();
    cache_one_group(CEL_GROUP_A);
    used_after_first = data_fdps_cel_sprite_cache_buffer_used;
    slot_b = cache_one_group(CEL_GROUP_B);

    CHECK_EQ(slot_b, 1);
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 2);
    CHECK_EQ(data_fdps_cel_sprite_cache_group_ids[1], CEL_GROUP_B);
    CHECK_EQ(data_fdps_cel_sprite_cache_buffer_used,
             used_after_first
                 + (offsets_b[CEL_GROUP_SPRITES] - offsets_b[0]));

    CHECK_EQ(cache_slots()[1].sprite_offset[0], (int) used_after_first);
    CHECK_EQ(cache_slots()[1].sprite_offset[9],
             (int) used_after_first + (offsets_b[9] - offsets_b[0]));
    CHECK_EQ(matching_pixel_bytes(offsets_b[9],
                                  data_fdps_cel_sprite_cache_ptr
                                      + cache_slots()[1].sprite_offset[9],
                                  CEL_PIXEL_PROBE_BYTES),
             CEL_PIXEL_PROBE_BYTES);

    CHECK_EQ(cache_slots()[0].sprite_offset[0], CEL_TABLE_BYTES);
    CHECK_EQ(matching_pixel_bytes(offsets_a[0],
                                  data_fdps_cel_sprite_cache_ptr
                                      + cache_slots()[0].sprite_offset[0],
                                  CEL_PIXEL_PROBE_BYTES),
             CEL_PIXEL_PROBE_BYTES);
    reset_cel_cache();
}

/* The search walks every filled slot, not just the last one: with three groups
   cached, each of them still answers with its own index and nothing is
   appended.  A search that compared only the newest key, or that stopped at
   the wrong bound, appends a duplicate and the count moves. */
static void cel_finds_a_group_cached_earlier(void)
{
    ensure_cel_sheet();
    CHECK_EQ(cel_ready, 1);
    if (!cel_ready) {
        return;
    }
    reset_cel_cache();
    CHECK_EQ(cache_one_group(CEL_GROUP_A), 0);
    CHECK_EQ(cache_one_group(CEL_GROUP_B), 1);
    CHECK_EQ(cache_one_group(CEL_GROUP_C), 2);

    CHECK_EQ(cache_one_group(CEL_GROUP_B), 1);
    CHECK_EQ(cache_one_group(CEL_GROUP_A), 0);
    CHECK_EQ(cache_one_group(CEL_GROUP_C), 2);
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 3);
    CHECK_EQ(data_fdps_cel_sprite_cache_group_ids[2], CEL_GROUP_C);
    reset_cel_cache();
}

/* CALL free at 000230e6 gives the 0x2970-byte offset-table block back before
   either arm runs, so a call leaves at most the cache block behind: one more
   used heap entry when it seeds, none when it appends, none when it hits.
   Every arm is measured, because the free is common to all three and a leak
   there is 10,608 bytes per call in a routine the drawing code calls per
   unit. */
static void cel_frees_the_offset_table_scratch(void)
{
    int before;
    int after_seed;
    int after_append;
    int after_hit;

    ensure_cel_sheet();
    CHECK_EQ(cel_ready, 1);
    if (!cel_ready) {
        return;
    }
    reset_cel_cache();
    before = used_heap_blocks();
    cache_one_group(CEL_GROUP_A);
    after_seed = used_heap_blocks();
    cache_one_group(CEL_GROUP_B);
    after_append = used_heap_blocks();
    cache_one_group(CEL_GROUP_A);
    after_hit = used_heap_blocks();

    CHECK_EQ(after_seed - before, 1);
    CHECK_EQ(after_append - after_seed, 0);
    CHECK_EQ(after_hit - after_append, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    reset_cel_cache();
}


/* ---------------------------------------------------------------------------
   fdps_field_load_chapter_resources at 000227e0.

   This one is run whole against the three real containers, because there is
   nothing in it to stand in for: it takes no argument, formats seven member
   names out of a global and hands each to fdps_vfs_load_entry, and a
   fabricated container would only be a copy of the shipped ones with the same
   names in it.  A missing member does not fail an assertion either -- the
   loader it calls waits for a key and then exits -- so every case here skips
   itself unless all three containers are next to the executable
   (tests/gamefile.lst stages FIELD.VFS, FIELD1.VFS and FIELD2.VFS).

   Every expected number below is a byte of a shipped resource, read out of
   the containers with tools/vfs_dump and quoted here as a literal, or the
   layout the assembly at 000227e0 imposes on it:

     MAP00.DAT bytes +1 and +2 are 1 and 22, MAP05.DAT's are 4 and 32
     DSC00.DAT is 36 bytes -- one dword count of 1 and one 0x20-byte record
     DSC05.DAT is 68 -- a count of 2 and two records
     DSC00 record 0 dwords are 0, 0, 8, 8, 0, 0 with bytes 9 and 1 at +0x18
       and +0x1c; DSC05 record 1 is 0, 0, 4, 2, 0, 0 with bytes 8 and 0
     M000.MPL's header words at +7 and +9 are 30 and 24
     M050.MPL's are 30 and 12, and M051.MPL's are 23 and 12
     FDETXT01.TXT opens with the bytes 32 0 40 0; FDETXT02.TXT opens 30 0 38 0
       and FDETXT06.TXT opens 28 0 36 0, so those four bytes say which member
       was loaded
     the four block kinds carry their own three-byte tags: "MPL", "CEL",
       "ATR" and "DTL"

   Chapter 5 is the second chapter tested because its DSC file is the only
   shape in the game with two layers, so it is what tells a record stride of
   0x20 apart from any other: layer 1's fields are read from dword index 9 and
   from nowhere else.  It is also what separates the grid from the layer it is
   sized by -- chapter 5's layer 0 is 30 tiles wide and its layer 1 is 23. */

#define FIELD_CONTAINER "FIELD.VFS"
#define FIELD1_CONTAINER "FIELD1.VFS"
#define FIELD2_CONTAINER "FIELD2.VFS"

/* The two chapters the cases load, and what the shipped files say about
   them. */
#define CH_ONE_LAYER 0
#define CH_TWO_LAYER 5

#define CH0_LAYER_COUNT 1
#define CH0_PLAYER_SLOTS 1
#define CH0_CHAR_SPAWNS 22
#define CH0_MAP_WIDTH 30
#define CH0_MAP_HEIGHT 24

#define CH5_LAYER_COUNT 2
#define CH5_PLAYER_SLOTS 4
#define CH5_CHAR_SPAWNS 32
#define CH5_LAYER0_WIDTH 30
#define CH5_LAYER0_HEIGHT 12
#define CH5_LAYER1_WIDTH 23

/* The tile map header words the loader reads, and the movement grid header it
   writes (src/movegrid.h). */
#define MPL_WIDTH_OFFSET 7
#define MPL_HEIGHT_OFFSET 9
#define GRID_CELLS_OFFSET 4

static int field_ready = 0;
static int field_checked = 0;
static int field_loaded = 0;

static int container_present(char *name)
{
    FILE *fp;

    fp = fopen(name, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static void ensure_field_containers(void)
{
    if (field_checked) {
        return;
    }
    field_checked = 1;
    if (container_present(FIELD_CONTAINER)
        && container_present(FIELD1_CONTAINER)
        && container_present(FIELD2_CONTAINER)) {
        field_ready = 1;
    }
}

/* Zeroing, not freeing.  Six of the globals this function frees on entry are
   staged by earlier test units -- tests/aiscore.c, tests/aitarget.c,
   tests/combat.c, tests/deploy.c and tests/mapai.c all point them at their own
   static arrays, and every one of those files sorts before this one -- so the
   first load of the run has to start from the state a fresh process is in,
   which is null everywhere and a layer count of zero.  Handing a static array
   to free() is not something a later assertion would get to report. */
static void clear_field_globals(void)
{
    int layer;

    for (layer = 0; layer < 6; layer++) {
        data_fdps_scene_layer_tile_map_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_sheet_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_attr_ptr[layer] = NULL;
    }
    data_fdps_scene_layer_count = 0;
    data_fdps_current_chapter_text_ptr = NULL;
    data_fdps_tile_event_data_table_ptr = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
}

/* The blocks the loads above left behind, released and the globals put back
   the way a fresh process has them, so the test units that sort after this one
   see what they would have seen if these cases had never run. */
static void free_field_globals(void)
{
    int layer;

    for (layer = 0; layer < data_fdps_scene_layer_count; layer++) {
        free(data_fdps_scene_layer_tile_map_ptrs[layer]);
        free(data_fdps_scene_layer_tile_sheet_ptrs[layer]);
        free(data_fdps_scene_layer_tile_attr_ptr[layer]);
    }
    free(data_fdps_current_chapter_text_ptr);
    free(data_fdps_tile_event_data_table_ptr);
    free(data_fdps_map_cell_event_code_layer_ptr);
    free(data_fdps_battle_move_grid_ptr);
    clear_field_globals();
    field_loaded = 0;
}

/* After the first load every later one goes through the function's own free
   path, which is what the original does between chapters and what the reload
   case below measures. */
static void field_load(int chapter)
{
    if (!field_loaded) {
        clear_field_globals();
    }
    data_fdps_chapter_current_chapter_id = chapter;
    fdps_field_load_chapter_resources();
    field_loaded = 1;
}

static int has_tag(unsigned char *block, char *tag)
{
    if (block == NULL) {
        return 0;
    }
    return block[0] == (unsigned char) tag[0]
        && block[1] == (unsigned char) tag[1]
        && block[2] == (unsigned char) tag[2];
}

/* DSC00.DAT is 36 bytes: a dword count and one 0x20-byte record.  A loader
   that scaled the record differently, or that took the count from anywhere but
   the first dword, disagrees here before it disagrees anywhere else. */
static void field_reads_the_layer_count_from_the_descriptor(void)
{
    ensure_field_containers();
    CHECK_EQ(field_ready, 1);
    if (!field_ready) {
        return;
    }
    field_load(CH_ONE_LAYER);
    CHECK_EQ(data_fdps_scene_layer_count, CH0_LAYER_COUNT);

    field_load(CH_TWO_LAYER);
    CHECK_EQ(data_fdps_scene_layer_count, CH5_LAYER_COUNT);
}

/* Each of the seven members has to land in its own global, and the three-byte
   tag at the head of each block says which kind of file arrived: a tile map
   written into the sheet slot, or the attribute table read out of Field1
   instead of Field2, shows up as the wrong tag rather than as a null. */
static void field_publishes_each_block_in_its_own_slot(void)
{
    ensure_field_containers();
    CHECK_EQ(field_ready, 1);
    if (!field_ready) {
        return;
    }
    field_load(CH_ONE_LAYER);

    CHECK_EQ(has_tag(data_fdps_map_cell_event_code_layer_ptr, "DTL"), 1);
    CHECK_EQ(has_tag(data_fdps_scene_layer_tile_map_ptrs[0], "MPL"), 1);
    CHECK_EQ(has_tag(data_fdps_scene_layer_tile_sheet_ptrs[0], "CEL"), 1);
    CHECK_EQ(has_tag(data_fdps_scene_layer_tile_attr_ptr[0], "ATR"), 1);
    CHECK_EQ(data_fdps_current_chapter_text_ptr != NULL, 1);
    CHECK_EQ(data_fdps_tile_event_data_table_ptr != NULL, 1);
    CHECK_EQ(data_fdps_battle_move_grid_ptr != NULL, 1);
}

/* The chapter text is the one member whose number is the chapter PLUS ONE, so
   chapter 0 must come back holding FDETXT01.TXT.  Its first four bytes are
   32 0 40 0; chapter 0's own number would have named a member that does not
   exist, and the neighbouring chapters open with different bytes. */
static void field_takes_the_chapter_text_one_number_up(void)
{
    ensure_field_containers();
    CHECK_EQ(field_ready, 1);
    if (!field_ready) {
        return;
    }
    field_load(CH_ONE_LAYER);

    CHECK_EQ(data_fdps_current_chapter_text_ptr[0], 32);
    CHECK_EQ(data_fdps_current_chapter_text_ptr[1], 0);
    CHECK_EQ(data_fdps_current_chapter_text_ptr[2], 40);
    CHECK_EQ(data_fdps_current_chapter_text_ptr[3], 0);
}

/* Bytes +1 and +2 of the resident map block, cached into two separate ints.
   Chapter 0's are 1 and 22 and chapter 5's are 4 and 32, so a reader that took
   both from the same offset, or that started at +0, says so. */
static void field_caches_the_map_header_counts(void)
{
    ensure_field_containers();
    CHECK_EQ(field_ready, 1);
    if (!field_ready) {
        return;
    }
    field_load(CH_ONE_LAYER);
    CHECK_EQ(data_fdps_map_player_slot_count, CH0_PLAYER_SLOTS);
    CHECK_EQ(data_fdps_map_char_spawn_count, CH0_CHAR_SPAWNS);
    CHECK_EQ(data_fdps_map_player_slot_count,
             (int) data_fdps_tile_event_data_table_ptr[1]);
    CHECK_EQ(data_fdps_map_char_spawn_count,
             (int) data_fdps_tile_event_data_table_ptr[2]);

    field_load(CH_TWO_LAYER);
    CHECK_EQ(data_fdps_map_player_slot_count, CH5_PLAYER_SLOTS);
    CHECK_EQ(data_fdps_map_char_spawn_count, CH5_CHAR_SPAWNS);
}

/* One 0x20-byte record scattered into eight parallel globals.  Chapter 0 pins
   the six dword offsets and the two byte offsets against record 0; chapter 5
   pins the stride, because its record 1 begins at dword index 9 and holds
   different numbers in every field that is not zero. */
static void field_scatters_the_layer_descriptor_record(void)
{
    ensure_field_containers();
    CHECK_EQ(field_ready, 1);
    if (!field_ready) {
        return;
    }
    field_load(CH_ONE_LAYER);
    CHECK_EQ(data_fdps_scene_layer_scroll_x_accumulator[0], 0);
    CHECK_EQ(data_fdps_scene_layer_scroll_offset_y[0], 0);
    CHECK_EQ(data_fdps_scene_layer_parallax_factor_x[0], 8);
    CHECK_EQ(data_fdps_scene_layer_parallax_factor_y[0], 8);
    CHECK_EQ(data_fdps_scene_layer_scroll_x_step[0], 0);
    CHECK_EQ(data_fdps_scene_layer_scroll_step_y[0], 0);
    CHECK_EQ(data_fdps_scene_layer_draw_depth[0], 9);
    CHECK_EQ(data_fdps_scene_layer_tile_attr_mode[0], 1);

    field_load(CH_TWO_LAYER);
    CHECK_EQ(data_fdps_scene_layer_parallax_factor_x[0], 8);
    CHECK_EQ(data_fdps_scene_layer_parallax_factor_y[0], 8);
    CHECK_EQ(data_fdps_scene_layer_draw_depth[0], 9);
    CHECK_EQ(data_fdps_scene_layer_tile_attr_mode[0], 1);
    CHECK_EQ(data_fdps_scene_layer_scroll_x_accumulator[1], 0);
    CHECK_EQ(data_fdps_scene_layer_scroll_offset_y[1], 0);
    CHECK_EQ(data_fdps_scene_layer_parallax_factor_x[1], 4);
    CHECK_EQ(data_fdps_scene_layer_parallax_factor_y[1], 2);
    CHECK_EQ(data_fdps_scene_layer_scroll_x_step[1], 0);
    CHECK_EQ(data_fdps_scene_layer_scroll_step_y[1], 0);
    CHECK_EQ(data_fdps_scene_layer_draw_depth[1], 8);
    CHECK_EQ(data_fdps_scene_layer_tile_attr_mode[1], 0);
}

/* The grid header is stamped from LAYER 0's tile map, not from the last layer
   loaded.  Chapter 5 is where that is visible: its layer 0 is 30 tiles wide
   and its layer 1 is 23, and the loop that loads them ends on layer 1. */
static void field_sizes_the_grid_from_layer_zero(void)
{
    ensure_field_containers();
    CHECK_EQ(field_ready, 1);
    if (!field_ready) {
        return;
    }
    field_load(CH_ONE_LAYER);
    CHECK_EQ(*(short *) data_fdps_battle_move_grid_ptr, CH0_MAP_WIDTH);
    CHECK_EQ(*(short *) (data_fdps_battle_move_grid_ptr + 2), CH0_MAP_HEIGHT);
    CHECK_EQ((int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0]
                               + MPL_WIDTH_OFFSET), CH0_MAP_WIDTH);
    CHECK_EQ((int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0]
                               + MPL_HEIGHT_OFFSET), CH0_MAP_HEIGHT);

    field_load(CH_TWO_LAYER);
    CHECK_EQ(*(short *) data_fdps_battle_move_grid_ptr, CH5_LAYER0_WIDTH);
    CHECK_EQ(*(short *) (data_fdps_battle_move_grid_ptr + 2),
             CH5_LAYER0_HEIGHT);
    CHECK_EQ((int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[1]
                               + MPL_WIDTH_OFFSET), CH5_LAYER1_WIDTH);
}

/* fdps_map_grid_reset is the last thing the function does, and it reads its
   bounds out of the header this function stamped.  So the cell at the far end
   of a 30 by 24 grid carrying the 0xff sentinel says three things at once: the
   block is big enough for width*height cells, the header went in before the
   reset ran, and the reset ran at all.  Fresh malloc'd bytes are not 0xff. */
static void field_blanks_the_grid_before_returning(void)
{
    struct fdps_move_grid_cell *cells;
    int last;

    ensure_field_containers();
    CHECK_EQ(field_ready, 1);
    if (!field_ready) {
        return;
    }
    field_load(CH_ONE_LAYER);
    cells = (struct fdps_move_grid_cell *)
            (data_fdps_battle_move_grid_ptr + GRID_CELLS_OFFSET);
    last = CH0_MAP_WIDTH * CH0_MAP_HEIGHT - 1;

    CHECK_EQ(cells[0].marker, 0xff);
    CHECK_EQ(cells[0].flags & 0xc0, 0);
    CHECK_EQ(cells[last].marker, 0xff);
    CHECK_EQ(cells[last].flags & 0xc0, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

/* The layer arrays are freed against the count the PREVIOUS chapter left, not
   against the one the new descriptor file is about to publish.  Going from
   chapter 5's two layers to chapter 0's one is therefore six blocks released
   and three taken, while the four singletons and the grid are each freed and
   reallocated -- a net loss of exactly three live blocks.  A loop that freed
   against the new count would leak three and come out at zero. */
static void field_frees_the_previous_chapters_layers(void)
{
    int before;
    int after;

    ensure_field_containers();
    CHECK_EQ(field_ready, 1);
    if (!field_ready) {
        return;
    }
    field_load(CH_TWO_LAYER);
    before = used_heap_blocks();
    field_load(CH_ONE_LAYER);
    after = used_heap_blocks();

    CHECK_EQ(after - before, -3);
    CHECK_EQ(data_fdps_scene_layer_count, CH0_LAYER_COUNT);
    CHECK_EQ(_heapchk(), _HEAPOK);
}


/* ---------------------------------------------------------------------------
   fdps_load_field_chapter_resources at 00031540.

   The village side's reload.  It needs only one container, FIELD.VFS, and the
   real ICON.CEL, both already staged by tests/gamefile.lst, and it cannot be
   stood in for any more than the field loader above can: it takes no argument,
   formats both member names out of a global, and a member it cannot find sends
   fdps_vfs_load_entry into fdps_wait_any_key and then exit(1).  So every case
   here skips itself unless both inputs are next to the executable.

   The roster it walks IS supplied, because the roster block is a global
   pointer and nothing here allocates it: the fixture below points it at a
   static array of four records whose portrait_id, side and char_id are all
   different, so a loop that read the record's neighbouring bytes instead of
   +0x7 caches a different set of groups and the cache keys say so.

   Every expected number below is a byte of a shipped resource, read out of
   FIELD.VFS with tools/vfs_dump and quoted here as a literal, or the layout
   the assembly at 00031540 imposes on it:

     FDETXT01.TXT opens with the bytes 32 0 40 0 and FDETXT06.TXT with
       28 0 36 0, so those four bytes say which member arrived
     SHOP00.DAT is 36 bytes opening 0 1 2 3 4 5 6 100 200, SHOP05.DAT opens
       180 181 255 ... with 3 and 4 at +12 and +13, and SHOP01.DAT -- the
       member a chapter number wrongly incremented would have named -- opens
       180 222
     the record field the sprite loop reads is portrait_id at +0x7 of struct
       fdps_unit_record, widened AND EAX,0xff at 000316d3
     the roster stride is 0x50, IMUL EAX,dword ptr [EBP + -0x8],0x50 at
       000316c4

   The heap-block deltas are read off the same assembly rather than off the
   emitted C.  One call from a clean state takes two blocks in (one per
   fdps_vfs_load_entry), one for the sprite cache it seeds, and gives back
   whatever the guarded frees release.  There is no free of the shop pointer
   anywhere in the body, which is why a second call comes out one block up. */

#define VILLAGE_ROSTER_SLOTS 4

/* Four groups well apart from each other, one of them repeated, so the cache
   has to dedupe: three distinct keys out of four members.  All four are inside
   ICON.CEL's 160 groups. */
#define VILLAGE_PORTRAIT_0 0
#define VILLAGE_PORTRAIT_1 5
#define VILLAGE_PORTRAIT_2 5
#define VILLAGE_PORTRAIT_3 37

/* A group nothing in the fixture roster asks for, seeded into the cache before
   a call so that finding it gone proves the cache was emptied. */
#define VILLAGE_STALE_GROUP 60

/* The two chapters the cases load. */
#define VILLAGE_CH_A 0
#define VILLAGE_CH_B 5

/* What chapter 0 and chapter 5 put in the two loaded blocks. */
#define CH0_TEXT_BYTE_0 32
#define CH0_TEXT_BYTE_2 40
#define CH5_TEXT_BYTE_0 28
#define CH5_TEXT_BYTE_2 36

#define CH0_SHOP_BYTE_0 0
#define CH0_SHOP_BYTE_1 1
#define CH0_SHOP_BYTE_7 100
#define CH0_SHOP_BYTE_8 200
#define CH5_SHOP_BYTE_0 180
#define CH5_SHOP_BYTE_1 181
#define CH5_SHOP_BYTE_12 3
#define CH5_SHOP_BYTE_13 4

/* Comfortably more calls than the CRT has file handles for (_NFILES is 20), so
   a body that leaked its ICON.CEL handle runs out during the loop. */
#define VILLAGE_REPEATED_CALLS 24

static struct fdps_unit_record village_roster[VILLAGE_ROSTER_SLOTS];
static int village_ready = 0;
static int village_checked = 0;

static void ensure_village_inputs(void)
{
    if (village_checked) {
        return;
    }
    village_checked = 1;
    ensure_cel_sheet();
    if (cel_ready && container_present(FIELD_CONTAINER)) {
        village_ready = 1;
    }
}

/* The party the loader walks.  side at +0x6 and char_id at +0x8 carry values
   that are neither each other nor the portrait ids, so a loop reading either
   neighbour caches four distinct groups instead of three and keys them
   differently. */
static void village_stage_roster(int chapter)
{
    int slot;
    static unsigned char portraits[VILLAGE_ROSTER_SLOTS] = {
        VILLAGE_PORTRAIT_0, VILLAGE_PORTRAIT_1,
        VILLAGE_PORTRAIT_2, VILLAGE_PORTRAIT_3
    };

    for (slot = 0; slot < VILLAGE_ROSTER_SLOTS; slot++) {
        memset(&village_roster[slot], 0, sizeof(struct fdps_unit_record));
        village_roster[slot].side = (unsigned char) (100 + slot * 2);
        village_roster[slot].portrait_id = portraits[slot];
        village_roster[slot].char_id = (unsigned char) (101 + slot * 2);
    }
    data_fdps_roster_array_ptr = (unsigned char *) village_roster;
    data_fdps_roster_member_count = VILLAGE_ROSTER_SLOTS;
    data_fdps_chapter_current_chapter_id = chapter;
}

/* Back to the state a fresh process is in.  The map unit pointer is dropped
   and not freed, because after a call it aliases the static roster above; the
   layer slots are nulled because the loader frees them without nulling
   them. */
static void village_reset(void)
{
    int layer;

    if (data_fdps_current_chapter_text_ptr != NULL) {
        free(data_fdps_current_chapter_text_ptr);
        data_fdps_current_chapter_text_ptr = NULL;
    }
    if (data_fdps_shop_stock_table_ptr != NULL) {
        free(data_fdps_shop_stock_table_ptr);
        data_fdps_shop_stock_table_ptr = NULL;
    }
    reset_cel_cache();
    for (layer = 0; layer < 6; layer++) {
        data_fdps_scene_layer_tile_map_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_sheet_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_attr_ptr[layer] = NULL;
    }
    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_roster_array_ptr = NULL;
    data_fdps_roster_member_count = 0;
}

/* INC EAX at 00031568 sits between the chapter counter and the sprintf, and it
   is on this name only.  Chapter 0 must therefore come back holding
   FDETXT01.TXT and chapter 5 FDETXT06.TXT; the members either side of those
   open with different bytes. */
static void village_takes_the_chapter_text_one_number_up(void)
{
    ensure_village_inputs();
    CHECK_EQ(village_ready, 1);
    if (!village_ready) {
        return;
    }
    village_reset();
    village_stage_roster(VILLAGE_CH_A);
    fdps_load_field_chapter_resources();
    CHECK_EQ(data_fdps_current_chapter_text_ptr[0], CH0_TEXT_BYTE_0);
    CHECK_EQ(data_fdps_current_chapter_text_ptr[1], 0);
    CHECK_EQ(data_fdps_current_chapter_text_ptr[2], CH0_TEXT_BYTE_2);
    CHECK_EQ(data_fdps_current_chapter_text_ptr[3], 0);

    village_stage_roster(VILLAGE_CH_B);
    fdps_load_field_chapter_resources();
    CHECK_EQ(data_fdps_current_chapter_text_ptr[0], CH5_TEXT_BYTE_0);
    CHECK_EQ(data_fdps_current_chapter_text_ptr[2], CH5_TEXT_BYTE_2);
    village_reset();
}

/* PUSH dword ptr [0x00069cf4] at 00031593 with no INC in front of it: the shop
   table takes the chapter number as it stands.  Chapter 0's SHOP00.DAT is a
   ramp starting at 0 and chapter 0 incremented would have named SHOP01.DAT,
   which opens 180 222 -- so the first two bytes alone separate the two
   readings, and chapter 5 pins that the name is formatted at all. */
static void village_takes_the_shop_table_at_the_plain_number(void)
{
    ensure_village_inputs();
    CHECK_EQ(village_ready, 1);
    if (!village_ready) {
        return;
    }
    village_reset();
    village_stage_roster(VILLAGE_CH_A);
    fdps_load_field_chapter_resources();
    CHECK_EQ(data_fdps_shop_stock_table_ptr[0], CH0_SHOP_BYTE_0);
    CHECK_EQ(data_fdps_shop_stock_table_ptr[1], CH0_SHOP_BYTE_1);
    CHECK_EQ(data_fdps_shop_stock_table_ptr[7], CH0_SHOP_BYTE_7);
    CHECK_EQ(data_fdps_shop_stock_table_ptr[8], CH0_SHOP_BYTE_8);

    village_stage_roster(VILLAGE_CH_B);
    fdps_load_field_chapter_resources();
    CHECK_EQ(data_fdps_shop_stock_table_ptr[0], CH5_SHOP_BYTE_0);
    CHECK_EQ(data_fdps_shop_stock_table_ptr[1], CH5_SHOP_BYTE_1);
    CHECK_EQ(data_fdps_shop_stock_table_ptr[12], CH5_SHOP_BYTE_12);
    CHECK_EQ(data_fdps_shop_stock_table_ptr[13], CH5_SHOP_BYTE_13);
    village_reset();
}

/* MOV EAX,[0x00064114] / MOV [0x00060150],EAX and MOV EAX,[0x00064108] / MOV
   [0x00069cd8],EAX at 000315e8: the roster block itself becomes the map's unit
   array and the count follows it.  An alias, not a copy -- the two pointers
   have to be equal afterwards, and the old array, which was a block of its
   own, has to have been released. */
static void village_hands_the_roster_block_to_the_map(void)
{
    int before;
    int after;

    ensure_village_inputs();
    CHECK_EQ(village_ready, 1);
    if (!village_ready) {
        return;
    }
    village_reset();
    village_stage_roster(VILLAGE_CH_A);
    data_fdps_map_unit_array_ptr = malloc(3 * 0x50);
    data_fdps_map_unit_count = 3;

    before = used_heap_blocks();
    fdps_load_field_chapter_resources();
    after = used_heap_blocks();

    CHECK_EQ(data_fdps_map_unit_count, VILLAGE_ROSTER_SLOTS);
    CHECK_EQ(data_fdps_map_unit_array_ptr == data_fdps_roster_array_ptr, 1);
    /* Two members loaded and one cache block seeded, against the old unit
       array released. */
    CHECK_EQ(after - before, 2);
    CHECK_EQ(_heapchk(), _HEAPOK);
    village_reset();
}

/* CMP EAX,dword ptr [0x00064108] / JNZ at 000315d0 is the second half of the
   guard, and it is the half that matters: the first call leaves the two
   pointers equal, so the second call must NOT free what they both point at.
   The roster here is a static array, so a free of it is a heap corruption
   rather than a leak, which _heapchk sees.

   The block count is what says the shop pointer is still not freed before it
   is replaced: the chapter text is released and reloaded for no change, the
   cache is released and reseeded for no change, and the one block the second
   call adds is the shop table the first call's is lost to. */
static void village_keeps_the_roster_block_on_a_second_call(void)
{
    int before;
    int after;

    ensure_village_inputs();
    CHECK_EQ(village_ready, 1);
    if (!village_ready) {
        return;
    }
    village_reset();
    village_stage_roster(VILLAGE_CH_A);
    fdps_load_field_chapter_resources();

    before = used_heap_blocks();
    fdps_load_field_chapter_resources();
    after = used_heap_blocks();

    CHECK_EQ(after - before, 1);
    CHECK_EQ(data_fdps_map_unit_array_ptr == data_fdps_roster_array_ptr, 1);
    CHECK_EQ(data_fdps_map_unit_count, VILLAGE_ROSTER_SLOTS);
    CHECK_EQ(_heapchk(), _HEAPOK);
    village_reset();
}

/* The layer loop at 00031603 frees three blocks per layer against the count it
   finds, and MOV dword ptr [0x00069cdc],0x0 at 00031662 then zeroes it: a
   village has no tile layers and nothing here reloads them.  Two staged layers
   are six blocks released against the two loaded and the one cache block
   seeded, so the count moves by minus three. */
static void village_frees_the_layer_blocks_and_zeroes_the_count(void)
{
    int before;
    int after;
    int layer;

    ensure_village_inputs();
    CHECK_EQ(village_ready, 1);
    if (!village_ready) {
        return;
    }
    village_reset();
    village_stage_roster(VILLAGE_CH_A);
    for (layer = 0; layer < 2; layer++) {
        data_fdps_scene_layer_tile_map_ptrs[layer] = malloc(STAGED_BLOCK_BYTES);
        data_fdps_scene_layer_tile_sheet_ptrs[layer] =
            malloc(STAGED_BLOCK_BYTES);
        data_fdps_scene_layer_tile_attr_ptr[layer] = malloc(STAGED_BLOCK_BYTES);
    }
    data_fdps_scene_layer_count = 2;

    before = used_heap_blocks();
    fdps_load_field_chapter_resources();
    after = used_heap_blocks();

    CHECK_EQ(data_fdps_scene_layer_count, 0);
    CHECK_EQ(after - before, -3);
    CHECK_EQ(_heapchk(), _HEAPOK);
    village_reset();
}

/* One fdps_cache_cel_sprite_group per roster member, keyed on the record's
   portrait_id at +0x7.  Four members with three distinct portraits give three
   cache slots in the order the members are walked; a loop that read side at
   +0x6 or char_id at +0x8 would give four slots keyed 100 102 104 106 or
   101 103 105 107, and a stride other than 0x50 would key them off other
   records' bytes. */
static void village_caches_one_group_per_roster_portrait(void)
{
    ensure_village_inputs();
    CHECK_EQ(village_ready, 1);
    if (!village_ready) {
        return;
    }
    village_reset();
    village_stage_roster(VILLAGE_CH_A);
    fdps_load_field_chapter_resources();

    CHECK_EQ(data_fdps_cel_sprite_cache_count, 3);
    CHECK_EQ(data_fdps_cel_sprite_cache_group_ids[0], VILLAGE_PORTRAIT_0);
    CHECK_EQ(data_fdps_cel_sprite_cache_group_ids[1], VILLAGE_PORTRAIT_1);
    CHECK_EQ(data_fdps_cel_sprite_cache_group_ids[2], VILLAGE_PORTRAIT_3);
    CHECK_EQ(data_fdps_cel_sprite_cache_ptr != NULL, 1);
    village_reset();
}

/* CMP dword ptr [0x00069cf0],0x0 / JZ at 0003166c releases the cache block and
   MOV dword ptr [0x00069cf0],0x0 at 00031683 zeroes the count, both before the
   sheet is opened.  A group cached beforehand therefore cannot survive: the
   first member's portrait has to land in slot 0.  With an empty roster nothing
   refills it and the cache stays empty, which is the same reset seen on its
   own. */
static void village_empties_the_sprite_cache_first(void)
{
    ensure_village_inputs();
    CHECK_EQ(village_ready, 1);
    if (!village_ready) {
        return;
    }
    village_reset();
    village_stage_roster(VILLAGE_CH_A);
    CHECK_EQ(cache_one_group(VILLAGE_STALE_GROUP), 0);
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 1);

    fdps_load_field_chapter_resources();
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 3);
    CHECK_EQ(data_fdps_cel_sprite_cache_group_ids[0], VILLAGE_PORTRAIT_0);

    village_reset();
    village_stage_roster(VILLAGE_CH_A);
    CHECK_EQ(cache_one_group(VILLAGE_STALE_GROUP), 0);
    data_fdps_roster_member_count = 0;
    fdps_load_field_chapter_resources();
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 0);
    /* PUSH dword ptr [0x00060138] / CALL free at 00031675 is followed by MOV
       dword ptr [0x00069cf0],0x0 and by no store to 0x00060138 anywhere in the
       function, so the count goes to zero and the POINTER is left at the block
       that was just released.  With members to walk the loop's first
       fdps_cache_cel_sprite_group replaces it on the empty-cache arm; with an
       empty roster nothing does, and the pointer dangles.  That is the
       original's behaviour and src/rsrc.c must keep it, so the case has to
       drop the stale value itself -- the fixture's teardown frees a non-null
       cache pointer, which on this one would be a second free of the same
       block. */
    CHECK_EQ(data_fdps_cel_sprite_cache_ptr != NULL, 1);
    data_fdps_cel_sprite_cache_ptr = NULL;
    CHECK_EQ(_heapchk(), _HEAPOK);
    village_reset();
}

/* CALL fclose at 000316e7 gives the ICON.CEL handle back on every call, so the
   village can be re-entered as often as the player likes.  A body that leaked
   the handle does not fail a check, it takes the run down at its own fopen
   past _NFILES; what survives to be asserted is that a file can still be
   opened afterwards.  One roster member per call keeps the sheet reads down --
   the handle is opened and closed whatever the member count is. */
static void village_closes_the_icon_sheet_each_time(void)
{
    int call;
    FILE *fp;

    ensure_village_inputs();
    CHECK_EQ(village_ready, 1);
    if (!village_ready) {
        return;
    }
    village_reset();
    village_stage_roster(VILLAGE_CH_A);
    data_fdps_roster_member_count = 1;
    for (call = 0; call < VILLAGE_REPEATED_CALLS; call++) {
        fdps_load_field_chapter_resources();
    }
    fp = fopen(CEL_NAME, "rb");
    CHECK_EQ(fp != NULL, 1);
    if (fp != NULL) {
        fclose(fp);
    }
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 1);
    village_reset();
}

void run_rsrc_tests(void)
{
    RUN_TEST(loads_entry_zero);
    RUN_TEST(loads_entry_one);
    RUN_TEST(loads_entry_two);
    RUN_TEST(sizes_the_block_from_the_offset_difference);
    RUN_TEST(frees_a_live_slot_before_loading);
    RUN_TEST(leaves_the_entry_in_the_callers_slot);
    RUN_TEST(heap_is_intact_after_a_load);
    RUN_TEST(closes_the_archive_each_time);

    RUN_TEST(cel_seeds_the_cache_on_the_first_call);
    RUN_TEST(cel_returns_the_cached_slot_for_a_repeated_group);
    RUN_TEST(cel_appends_a_second_group);
    RUN_TEST(cel_finds_a_group_cached_earlier);
    RUN_TEST(cel_frees_the_offset_table_scratch);

    RUN_TEST(field_reads_the_layer_count_from_the_descriptor);
    RUN_TEST(field_publishes_each_block_in_its_own_slot);
    RUN_TEST(field_takes_the_chapter_text_one_number_up);
    RUN_TEST(field_caches_the_map_header_counts);
    RUN_TEST(field_scatters_the_layer_descriptor_record);
    RUN_TEST(field_sizes_the_grid_from_layer_zero);
    RUN_TEST(field_blanks_the_grid_before_returning);
    RUN_TEST(field_frees_the_previous_chapters_layers);

    /* The chapter blocks belong to this file and to nothing after it: the
       units that sort later stage these same globals with statics of their
       own, and one of them left holding a heap block from here would be freed
       by whatever loads a chapter next. */
    if (field_loaded) {
        free_field_globals();
    }

    /* The village cases run after that cleanup and not before it: they write
       the same chapter-text and layer globals, and starting from the state a
       fresh process is in is what makes their heap-block deltas mean
       anything. */
    RUN_TEST(village_takes_the_chapter_text_one_number_up);
    RUN_TEST(village_takes_the_shop_table_at_the_plain_number);
    RUN_TEST(village_hands_the_roster_block_to_the_map);
    RUN_TEST(village_keeps_the_roster_block_on_a_second_call);
    RUN_TEST(village_frees_the_layer_blocks_and_zeroes_the_count);
    RUN_TEST(village_caches_one_group_per_roster_portrait);
    RUN_TEST(village_empties_the_sprite_cache_first);
    RUN_TEST(village_closes_the_icon_sheet_each_time);

    /* The fixture file belongs to this run and to nothing else; leaving it
       behind would let a later run pass on a stale archive even after the
       write failed. */
    if (archive_ready) {
        remove(ARCHIVE_NAME);
        archive_ready = 0;
    }
}
