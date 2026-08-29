/* tests/rsrc.c -- cover for src/rsrc.c.
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

    /* The fixture file belongs to this run and to nothing else; leaving it
       behind would let a later run pass on a stale archive even after the
       write failed. */
    if (archive_ready) {
        remove(ARCHIVE_NAME);
        archive_ready = 0;
    }
}
