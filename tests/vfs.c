/* tests/vfs.c -- cover for src/vfs.c.
 *
 * fdps_vfs_read_entry_count takes a path and nothing else, so every case here
 * is a real file opened by name from the staging directory.  Two of the three
 * are shipped game files listed in tests/gamefile.lst; the third is a name
 * that is deliberately not there.
 *
 * Expected values come from the assembly at 000398f0 and from the container
 * layout in resource_info/vfs.md, never from the emitted C:
 *
 *   MOV dword ptr [EBP-0x8],0x0 at 000398fc      the result is preset to 0
 *   CMP [EBP-0xc],0x0 / JZ 0003994e at 00039918  a failed open returns it
 *   PUSH 0x7 at 00039920                         the seek lands on offset 7
 *   PUSH 0x4 / PUSH 0x1 at 00039934              one item of four bytes
 *   MOV EAX,[EBP-0x8] at 0003994e                the whole dword is returned
 *
 * Nothing here fabricates a container.  The point of the ICON.CEL case is
 * exactly that the function has no format check, and a hand-built file would
 * be asserting against bytes this test wrote rather than against a file the
 * game ships.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testharn.h"
#include "vfs.h"

/* A real container, and the smallest one the game ships at 112,350 bytes.  Its
   header holds 131 at offset 7, which resource_info/vfs.md's layout confirms
   is the entry count: 35 bytes of header plus 26 * 131 = 3,406 bytes of entry
   table is 3,441, and the first member starts there. */
#define VFS_NAME "FIELD2.VFS"
#define VFS_ENTRY_COUNT 131

/* Not a container at all -- the unit sprite sheet, staged for tests/rsrc.c and
   tests/deploy.c.  resource_info/cel.md has its header: the magic "CEL", a
   version word at 3, the offset-table position at 5, then the sheet's shared
   sprite width at 7 and its height at 9, both 16-bit and both 24.  Read as the
   one little-endian dword at offset 7 that this function takes, those two
   words are 0x00180018. */
#define NON_VFS_NAME "ICON.CEL"
#define NON_VFS_HEADER_DWORD 0x00180018L

/* The low byte of that same dword, which is all fdps_vfs_image_entry_count
   sees: the low half of the sprite width's 24. */
#define NON_VFS_HEADER_BYTE 0x18

/* A name no staged file has, so fopen fails and the branch at 00039918 is
   taken.  It ends in .VFS on purpose: the function does not care what a file
   is called any more than it cares what is inside it. */
#define MISSING_NAME "NOTHERE.VFS"

/* The failed-open path.  It is not a separate exit: the jump lands on the same
   return the successful path reaches, so what comes back is the preset from
   000398fc.  That makes 0 ambiguous between a missing file and an empty
   container, and this case pins the missing-file half of it. */
static void missing_file_returns_zero(void)
{
    CHECK_EQ(fdps_vfs_read_entry_count(MISSING_NAME), 0);
}

/* The ordinary path against a real container: the entry count of FIELD2.VFS as
   the shipped file's header records it. */
static void real_container_entry_count(void)
{
    CHECK_EQ(fdps_vfs_read_entry_count(VFS_NAME), VFS_ENTRY_COUNT);
}

/* Three things at once, because a non-container file is the only shipped file
   whose dword at offset 7 is large enough to distinguish them.

   No format check: ICON.CEL has neither the "VFS" magic nor the version nor
   the signature, and it is read anyway rather than being rejected with 0.

   The seek offset is 7 and not 6 or 8: the dwords at those offsets are
   0x18001800 and 0x80001800, so only offset 7 gives 0x00180018.

   All 32 bits survive.  fdps_vfs_image_entry_count takes the low byte of this
   same field, and fdps_vfs_find_entry takes the low byte of the count out of
   an open handle; this one does not truncate, and a truncating read would hand
   back 0x18 rather than 0x00180018.  The
   second check states the surviving high half on its own so a failure says
   which half went missing. */
static void non_container_is_read_unchecked(void)
{
    CHECK_EQ(fdps_vfs_read_entry_count(NON_VFS_NAME), NON_VFS_HEADER_DWORD);
    CHECK_EQ(fdps_vfs_read_entry_count(NON_VFS_NAME) >> 16, 0x18);
}

/* fdps_vfs_image_entry_count takes an image already in memory rather than a
   path, so the file cases below have to bring the header in themselves.  Eleven
   bytes is the whole of what the program models of a .VFS header
   (src/fdpstype.h), and it is more than the one byte at offset 7 that is under
   test.  A short read leaves the struct half-filled, so every caller checks
   this returned 1 before it asserts anything about the contents. */
static int read_image_header(char *name, struct fdps_vfs_image_header *header)
{
    FILE *fp;
    size_t got;

    fp = fopen(name, "rb");
    if (fp == NULL) {
        return 0;
    }
    got = fread(header, sizeof(struct fdps_vfs_image_header), 1, fp);
    fclose(fp);
    return got == 1;
}

/* The same shipped container the path reader is checked against, so the two
   halves of the family are pinned to one number: FIELD2.VFS records 131 at
   offset 7 and 131 is below the 256 where the byte load starts to differ, so
   the in-memory reader has to agree with the on-disk one here. */
static void image_entry_count_real_container(void)
{
    struct fdps_vfs_image_header header;

    CHECK_EQ(read_image_header(VFS_NAME, &header), 1);
    CHECK_EQ(fdps_vfs_image_entry_count(&header), VFS_ENTRY_COUNT);
}

/* The one thing that separates this function from every dword reader in the
   module, and the reason the pitfall exists.  MOV AL,[EDX + 0x7] at 00039971
   takes one byte of a four-byte field, so what comes back is the count modulo
   256; no shipped container reaches 256 -- Field.vfs is the largest at 223 --
   so these three values are stated rather than read off a game file.

   255 is the last count that survives intact.  256 is the first that does not
   and it reports 0, which is the case a struct-field read of the u32 would get
   wrong by answering 256.  388 shows it is truncation and not saturation:
   388 - 256 is 132.

   The 255 case is doing second duty on the XOR EAX,EAX at 0003996c.  That zero
   extension is what makes a 0xff byte 255; a sign-extending load would make it
   -1, and CHECK_EQ widens both sides to long so the two do not compare
   equal. */
static void image_entry_count_truncates_to_low_byte(void)
{
    struct fdps_vfs_image_header header;

    header.entry_count = 255;
    CHECK_EQ(fdps_vfs_image_entry_count(&header), 255);
    header.entry_count = 256;
    CHECK_EQ(fdps_vfs_image_entry_count(&header), 0);
    header.entry_count = 388;
    CHECK_EQ(fdps_vfs_image_entry_count(&header), 132);
}

/* No format check, and offset 7 rather than 6 or 8.  ICON.CEL is not a
   container: it has neither the "VFS" magic nor the version nor the
   entry-table offset a container's header carries, and it is read anyway.
   resource_info/cel.md puts the sheet's shared sprite width at offset 7 as a
   16-bit 24, so the byte this function loads is 0x18, while the bytes at
   offsets 6 and 8 are both zero -- an off-by-one either way would report 0. */
static void image_entry_count_reads_offset_seven(void)
{
    struct fdps_vfs_image_header header;

    CHECK_EQ(read_image_header(NON_VFS_NAME, &header), 1);
    CHECK_EQ(fdps_vfs_image_entry_count(&header), NON_VFS_HEADER_BYTE);
}

/* fdps_vfs_find_entry walks an open handle rather than a container image, and
   the function that builds one -- fdps_vfs_open -- is not emitted yet, so the
   cases below build the handle the way 00039ab0 does.  The two offsets are the
   handle's own layout, and both sides of the module agree on them:

     0x11  where the directory table starts.  ADD EAX,0x11 at 000399cc is the
           search reading it; memset(handle, 0, 0x11) at 00039b43 and the fread
           into handle + 0x11 at 00039b9c are fdps_vfs_open writing it.
     0x1a  one directory entry.  IMUL EAX,[EBP + -0x8],0x1a at 000399c5, and 26
           bytes per entry in resource_info/vfs.md.

   Ahead of the table sit the dword entry count at 0 (MOV [EDX],EAX at
   00039b59) and the archive path at 4 (the memcpy into handle + 4 at
   00039b73).  The search reads only the count, so the path is left zeroed. */
#define HANDLE_ENTRY_TABLE_OFFSET 0x11
#define HANDLE_ENTRY_SIZE 0x1a

/* Builds a handle over the real directory of a shipped container: the count as
   fdps_vfs_open stores it, then the container's own entry table read in
   verbatim from the file offset its header names.  Nothing is fabricated --
   the names the search matches against are the names the packer wrote.
   Returns NULL if anything went wrong, and every caller checks. */
static char *open_real_handle(char *archive_name)
{
    struct fdps_vfs_image_header header;
    FILE *fp;
    char *handle;
    unsigned int table_bytes;

    if (!read_image_header(archive_name, &header)) {
        return NULL;
    }
    table_bytes = header.entry_count * HANDLE_ENTRY_SIZE;
    handle = (char *)malloc(HANDLE_ENTRY_TABLE_OFFSET + table_bytes);
    if (handle == NULL) {
        return NULL;
    }
    memset(handle, 0, HANDLE_ENTRY_TABLE_OFFSET);
    memcpy(handle, &header.entry_count, 4);
    fp = fopen(archive_name, "rb");
    if (fp == NULL) {
        free(handle);
        return NULL;
    }
    fseek(fp, (long)header.entry_table_offset, SEEK_SET);
    if (fread(handle + HANDLE_ENTRY_TABLE_OFFSET, table_bytes, 1, fp) != 1) {
        fclose(fp);
        free(handle);
        return NULL;
    }
    fclose(fp);
    return handle;
}

/* A handle with room for three entries, for the two cases no shipped container
   can show: a lower-case entry name, and an entry count above 255.  This one
   is memory the test writes, not a file it invents -- there is no container
   behind it and it is not pretending to be one. */
#define SYNTHETIC_ENTRIES 3
static char synthetic_handle[HANDLE_ENTRY_TABLE_OFFSET
                             + SYNTHETIC_ENTRIES * HANDLE_ENTRY_SIZE];

/* The count goes in as four explicit bytes so the case that matters -- a value
   that does not fit in one -- is stated rather than left to a cast. */
static void synthetic_handle_reset(unsigned long entry_count)
{
    memset(synthetic_handle, 0, sizeof(synthetic_handle));
    synthetic_handle[0] = (char)(entry_count & 0xffUL);
    synthetic_handle[1] = (char)((entry_count >> 8) & 0xffUL);
    synthetic_handle[2] = (char)((entry_count >> 16) & 0xffUL);
    synthetic_handle[3] = (char)((entry_count >> 24) & 0xffUL);
}

static void synthetic_handle_set_name(int index, char *entry_name)
{
    strcpy(synthetic_handle + HANDLE_ENTRY_TABLE_OFFSET
               + index * HANDLE_ENTRY_SIZE,
           entry_name);
}

/* Every query goes through a writable buffer.  That is not tidiness: strupr
   rewrites what it is handed, so passing a string literal would have the test
   modifying its own literal pool. */
#define QUERY_MAX 16

/* Four names out of FIELD2.VFS's real directory and one that is not in it.
   The indices are read off the shipped file's entry table, which
   resource_info/vfs.md's layout says starts at the offset in the header at 5
   and runs 26 bytes per entry: ATTR000.DAT is entry 0, ATTR010.DAT entry 1,
   ATTR610.DAT entry 64 and DSC64.DAT entry 130, the last of the 131.

   The two interior indices are what pin the stride.  A stride of 25 or 27
   would still find entry 0, because entry 0 begins where the table begins; it
   would land nowhere at all on entries 64 and 130.

   130 is doing second duty on the entry count: the loop bound is the count
   taken off the handle, so an index of 130 is only reachable if all 131 got
   there.  NOSUCH.DAT pins the other exit -- the -1 written at 0003999c
   survives when the loop runs out. */
static void find_entry_matches_real_container_names(void)
{
    char *handle;
    char query[QUERY_MAX];

    handle = open_real_handle(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    strcpy(query, "ATTR000.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, handle), 0);
    strcpy(query, "ATTR610.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, handle), 64);
    strcpy(query, "DSC64.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, handle), 130);
    strcpy(query, "NOSUCH.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, handle), -1);
    free(handle);
}

/* The half of the one-way folding that works, and the side effect that comes
   with it.  strupr at 000399e0 is applied to the query, so a lower-case query
   finds an upper-case entry; and because it works in place, the caller's
   buffer is upper-case afterwards.  The image's callers pass string literals,
   which is why that second assertion is about behaviour a caller can see and
   not about an implementation detail (rebuild_info/pitfalls.md). */
static void find_entry_uppercases_the_query_in_place(void)
{
    char *handle;
    char query[QUERY_MAX];

    handle = open_real_handle(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    strcpy(query, "attr010.dat");
    CHECK_EQ(fdps_vfs_find_entry(query, handle), 1);
    CHECK_EQ(strcmp(query, "ATTR010.DAT"), 0);
    free(handle);
}

/* The other half: the entry's own name is taken raw.  memcpy at 000399d4
   copies the 13 bytes out and strcmp at 000399ed compares them byte for byte,
   with no fold on that side, so an entry whose name is not upper-case cannot
   be found however the query is spelled.  A stricmp would find "lower.dat"
   both ways round and is the divergence the pitfall entry names.  All 1,202
   shipped member names are upper-case, so this case has to state its own
   directory. */
static void find_entry_does_not_fold_the_table_side(void)
{
    char query[QUERY_MAX];

    synthetic_handle_reset(2);
    synthetic_handle_set_name(0, "lower.dat");
    synthetic_handle_set_name(1, "UPPER.DAT");

    strcpy(query, "lower.dat");
    CHECK_EQ(fdps_vfs_find_entry(query, synthetic_handle), -1);
    strcpy(query, "LOWER.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, synthetic_handle), -1);
    strcpy(query, "upper.dat");
    CHECK_EQ(fdps_vfs_find_entry(query, synthetic_handle), 1);
}

/* MOV AL,[EDX] at 000399a8 takes one byte of the dword count fdps_vfs_open
   wrote, so the loop bound is the count modulo 256.  The entry that matches is
   entry 0 in all four cases and only the count changes, which is what makes
   this an assertion about the load and not about the search.

   1 finds it.  256 does not -- the low byte is 0, the loop runs no iterations
   and the -1 preset comes back, which is exactly what a dword read would get
   wrong by answering 0.  257 finds it again, so it is truncation and not
   saturation or a clamp.  0 is the empty container, the same answer as no
   match, and it is here because it is the one value where -1 is right for two
   different reasons.

   No shipped container reaches 256 -- Field.vfs is the largest at 223 -- so
   these counts are stated from the assembly, not read off a game file. */
static void find_entry_reads_the_entry_count_as_one_byte(void)
{
    char query[QUERY_MAX];

    synthetic_handle_reset(1);
    synthetic_handle_set_name(0, "A.DAT");
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, synthetic_handle), 0);

    synthetic_handle_reset(256);
    synthetic_handle_set_name(0, "A.DAT");
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, synthetic_handle), -1);

    synthetic_handle_reset(257);
    synthetic_handle_set_name(0, "A.DAT");
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, synthetic_handle), 0);

    synthetic_handle_reset(0);
    synthetic_handle_set_name(0, "A.DAT");
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, synthetic_handle), -1);
}

/* The loop stops at the first match rather than the last: the store at
   000399f9 is followed by JMP to the exit, not by the increment.  Two entries
   with the same name is not something the shipped containers do, so this is
   the branch structure being asserted directly. */
static void find_entry_stops_at_the_first_match(void)
{
    char query[QUERY_MAX];

    synthetic_handle_reset(3);
    synthetic_handle_set_name(0, "OTHER.DAT");
    synthetic_handle_set_name(1, "SAME.DAT");
    synthetic_handle_set_name(2, "SAME.DAT");

    strcpy(query, "SAME.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, synthetic_handle), 1);
}

void run_vfs_tests(void)
{
    RUN_TEST(missing_file_returns_zero);
    RUN_TEST(real_container_entry_count);
    RUN_TEST(non_container_is_read_unchecked);
    RUN_TEST(image_entry_count_real_container);
    RUN_TEST(image_entry_count_truncates_to_low_byte);
    RUN_TEST(image_entry_count_reads_offset_seven);
    RUN_TEST(find_entry_matches_real_container_names);
    RUN_TEST(find_entry_uppercases_the_query_in_place);
    RUN_TEST(find_entry_does_not_fold_the_table_side);
    RUN_TEST(find_entry_reads_the_entry_count_as_one_byte);
    RUN_TEST(find_entry_stops_at_the_first_match);
}
