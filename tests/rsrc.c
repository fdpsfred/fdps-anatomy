/* tests/rsrc.c -- cover for src/rsrc.c.
 *
 * Three subjects, each starting at its own banner below: the file opens on
 * fdps_load_indexed_archive_entry at 00022e30, then comes
 * fdps_cache_cel_sprite_group at 00023050, which reads the real ICON.CEL
 * instead of a fixture and says there why, and last
 * fdps_field_load_chapter_resources at 000227e0, which reads the three real
 * field containers and says there why.
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

    /* The fixture file belongs to this run and to nothing else; leaving it
       behind would let a later run pass on a stale archive even after the
       write failed. */
    if (archive_ready) {
        remove(ARCHIVE_NAME);
        archive_ready = 0;
    }
}
