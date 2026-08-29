/* tests/rsrc.c -- cover for src/rsrc.c.
 *
 * Two subjects, and the second one starts at its own banner below: everything
 * down to run_rsrc_tests covers fdps_load_indexed_archive_entry at 00022e30,
 * and after the banner comes fdps_cache_cel_sprite_group at 00023050, which
 * reads the real ICON.CEL instead of a fixture and says there why.
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

    /* The fixture file belongs to this run and to nothing else; leaving it
       behind would let a later run pass on a stale archive even after the
       write failed. */
    if (archive_ready) {
        remove(ARCHIVE_NAME);
        archive_ready = 0;
    }
}
