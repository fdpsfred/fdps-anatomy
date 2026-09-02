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
   the cases below build the handle the way 00039ab0 does rather than calling
   fdps_vfs_open to build it: a search checked against a handle its own module
   wrote would pass just as happily if both sides had the layout wrong.  The
   two offsets are that layout, and both sides of the module agree on them:

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

/* fdps_vfs_find_entry_size walks the same directory as fdps_vfs_find_entry and
   differs only in what a match hands back, so the cases below re-run the four
   behaviours that search is pinned on -- the stride, the byte-wide count, the
   one-way fold and the first-match break -- through the size answer instead of
   the index answer.  They are not duplicates of those cases: an index search
   that is right and a size read that is off by a field would still agree on
   every index, and none of the assertions above would move.

   Three offsets inside a 26-byte entry, from resource_info/vfs.md and from the
   folded 0x1e at 00039a90 (0x1e minus the table's own 0x11 is 0x0d):

     0x0d  the member's size, the field this function reads
     0x11  a second copy of the same size, which the program never reads
     0x16  the member's start in the container, which it never reads either

   Every shipped container has 0x0d and 0x11 holding the same number, so no
   game file can separate them; the synthetic directory below can. */
#define ENTRY_SIZE_FIELD 0x0d
#define ENTRY_SIZE_DUP_FIELD 0x11
#define ENTRY_START_FIELD 0x16

/* Writes one little-endian dword into a field of one entry of any handle,
   synthetic or real.  Explicit bytes rather than a cast, for the same reason
   synthetic_handle_reset uses them: the values under test are chosen to be
   distinguishable, and a stored dword has to be exactly the bytes the test
   meant.  This is the only place the entry-field address is computed, so the
   fdps_vfs_load_file cases at the bottom of the file and the
   fdps_vfs_find_entry_size cases here cannot drift apart about the layout. */
static void handle_set_entry_dword(char *handle, int index, int field_offset,
                                   unsigned long value)
{
    char *field;

    field = handle + HANDLE_ENTRY_TABLE_OFFSET + index * HANDLE_ENTRY_SIZE
            + field_offset;
    field[0] = (char)(value & 0xffUL);
    field[1] = (char)((value >> 8) & 0xffUL);
    field[2] = (char)((value >> 16) & 0xffUL);
    field[3] = (char)((value >> 24) & 0xffUL);
}

static void synthetic_handle_set_dword(int index, int field_offset,
                                       unsigned long value)
{
    handle_set_entry_dword(synthetic_handle, index, field_offset, value);
}

/* The sizes the packer wrote for four members of the shipped FIELD2.VFS, read
   out of its own entry table at the offset its header names.  They pair with
   the indices the fdps_vfs_find_entry cases use, so the two searches are
   pinned to the same three entries: ATTR000.DAT is entry 0, ATTR610.DAT entry
   64, DSC64.DAT entry 130 and last of the 131.

   None of the 1,202 shipped members is zero bytes -- the smallest is
   MAP41.COD in FIELD.VFS at 15 -- which is why -1 is a safe miss marker for
   this function in a way that fdps_vfs_read_entry_count's 0 is not. */
#define ATTR000_SIZE 1553
#define ATTR610_SIZE 917
#define DSC64_SIZE 36

/* Entry 0's member start, 3441, which is where the entry table ends: 35 bytes
   of header plus 26 * 131.  It is here as the value a read of the wrong field
   would produce -- the field at 0x16 rather than 0x0d -- and it is nowhere
   near 1553, so an off-by-a-field is not a near miss. */
#define ATTR000_START 3441

/* The real container, through the size answer: three members' sizes as the
   shipped file records them, and a name that is not in it.

   The two interior entries pin the 0x1a stride the same way the index cases
   do, and harder: a wrong stride still finds entry 0 but now has to land on
   the size field of the wrong record, and every member of FIELD2.VFS has a
   different size.  Entry 130 is only reachable if the loop bound walked all
   131.  NOSUCH.DAT pins the loop-exhausted exit and the -1 written at
   00039a2c, which for this function is the whole of the failure signal. */
static void find_entry_size_reports_real_member_sizes(void)
{
    char *handle;
    char query[QUERY_MAX];

    handle = open_real_handle(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    strcpy(query, "ATTR000.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, handle), ATTR000_SIZE);
    strcpy(query, "ATTR610.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, handle), ATTR610_SIZE);
    strcpy(query, "DSC64.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, handle), DSC64_SIZE);
    strcpy(query, "NOSUCH.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, handle), -1);
    free(handle);
}

/* Which of the entry's three dwords is read.  Against a real container this is
   untestable for the first two: 0x0d and 0x11 always agree there.  So the
   synthetic directory states three deliberately different values in the one
   entry and the answer says which field the load at 00039a90 landed on --
   0x0d, not the duplicate at 0x11 and not the member start at 0x16.

   The second half re-states it from the real file: entry 0 of FIELD2.VFS holds
   1553 at 0x0d and 3441 at 0x16, so the shipped container agrees with the
   synthetic one about which field is not being read. */
static void find_entry_size_reads_the_size_field(void)
{
    char *handle;
    char query[QUERY_MAX];

    synthetic_handle_reset(1);
    synthetic_handle_set_name(0, "A.DAT");
    synthetic_handle_set_dword(0, ENTRY_SIZE_FIELD, 111);
    synthetic_handle_set_dword(0, ENTRY_SIZE_DUP_FIELD, 222);
    synthetic_handle_set_dword(0, ENTRY_START_FIELD, 333);
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), 111);

    handle = open_real_handle(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    strcpy(query, "ATTR000.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, handle), ATTR000_SIZE);
    CHECK_EQ(ATTR000_SIZE == ATTR000_START, 0);
    free(handle);
}

/* The whole dword survives.  MOV EAX,[EAX + 0x1e] at 00039a90 is a full 32-bit
   load, unlike the MOV AL that takes the entry count, so a size above 255 and
   a size above 65535 both come back intact.  0x00010001 is chosen so a load
   truncated to a byte or to a word would answer 1 rather than something
   obviously wrong.  No shipped member needs the top half of the dword -- the
   largest is CHAPTER.SAF in MISC.VFS at 1,857,775 -- so these two values are
   stated from the instruction, not read off a container. */
static void find_entry_size_reads_a_whole_dword(void)
{
    char query[QUERY_MAX];

    synthetic_handle_reset(1);
    synthetic_handle_set_name(0, "A.DAT");
    synthetic_handle_set_dword(0, ENTRY_SIZE_FIELD, 0x00010001UL);
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), 0x00010001L);

    synthetic_handle_set_dword(0, ENTRY_SIZE_FIELD, 0UL);
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), 0);
}

/* The one-way fold, both halves, through the size answer.  strupr at 00039a70
   rewrites the query in place so a lower-case query finds an upper-case entry
   and the caller's buffer is upper-case afterwards; the entry's own name is
   memcpy'd out raw at 00039a64 and compared byte for byte at 00039a7d, so a
   lower-case entry name is unreachable however the query is spelled.  A
   stricmp would find it both ways round, which is the divergence
   resource_info/vfs.md and rebuild_info/pitfalls.md both name.

   The miss answer here is -1 and not the entry's size, which is the assertion
   that separates "not found" from "found and read wrong" -- the lower-case
   entry is given a size of 777 precisely so a fold on the table side would
   show up as 777 rather than as a plausible-looking failure. */
static void find_entry_size_folds_only_the_query(void)
{
    char query[QUERY_MAX];

    synthetic_handle_reset(2);
    synthetic_handle_set_name(0, "lower.dat");
    synthetic_handle_set_dword(0, ENTRY_SIZE_FIELD, 777);
    synthetic_handle_set_name(1, "UPPER.DAT");
    synthetic_handle_set_dword(1, ENTRY_SIZE_FIELD, 888);

    strcpy(query, "lower.dat");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), -1);
    strcpy(query, "LOWER.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), -1);
    strcpy(query, "upper.dat");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), 888);
    CHECK_EQ(strcmp(query, "UPPER.DAT"), 0);
}

/* The loop bound, taken one byte wide by MOV AL,[EDX] at 00039a38 out of the
   dword fdps_vfs_open stored.  The matching entry is entry 0 every time and
   only the count changes: 1 finds it, 256 does not because the low byte is 0
   and the loop runs no iterations, 257 finds it again so it is truncation
   rather than saturation, and 0 is the empty container.

   The size answer sharpens the 256 case over the index one.  A dword read of
   the count would answer 0 there, and 0 is also a legitimate size for a
   zero-byte member -- so the entry is given a size of 999, which makes the two
   outcomes 999 and -1 rather than 0 and -1.

   No shipped container reaches 256; Field.vfs is the largest at 223. */
static void find_entry_size_reads_the_entry_count_as_one_byte(void)
{
    char query[QUERY_MAX];

    synthetic_handle_reset(1);
    synthetic_handle_set_name(0, "A.DAT");
    synthetic_handle_set_dword(0, ENTRY_SIZE_FIELD, 999);
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), 999);

    synthetic_handle_reset(256);
    synthetic_handle_set_name(0, "A.DAT");
    synthetic_handle_set_dword(0, ENTRY_SIZE_FIELD, 999);
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), -1);

    synthetic_handle_reset(257);
    synthetic_handle_set_name(0, "A.DAT");
    synthetic_handle_set_dword(0, ENTRY_SIZE_FIELD, 999);
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), 999);

    synthetic_handle_reset(0);
    synthetic_handle_set_name(0, "A.DAT");
    synthetic_handle_set_dword(0, ENTRY_SIZE_FIELD, 999);
    strcpy(query, "A.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), -1);
}

/* The store at 00039a93 is followed by a JMP to the exit rather than by the
   increment, so the first match wins.  Two entries carry the same name with
   different sizes, which is what makes the answer say which of them the loop
   stopped on -- a search that ran to the end would report 20 instead of 10. */
static void find_entry_size_stops_at_the_first_match(void)
{
    char query[QUERY_MAX];

    synthetic_handle_reset(3);
    synthetic_handle_set_name(0, "OTHER.DAT");
    synthetic_handle_set_dword(0, ENTRY_SIZE_FIELD, 5);
    synthetic_handle_set_name(1, "SAME.DAT");
    synthetic_handle_set_dword(1, ENTRY_SIZE_FIELD, 10);
    synthetic_handle_set_name(2, "SAME.DAT");
    synthetic_handle_set_dword(2, ENTRY_SIZE_FIELD, 20);

    strcpy(query, "SAME.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, synthetic_handle), 10);
}

/* fdps_vfs_open is the writer of the handle the two searches above read, and
   open_real_handle at the top of this file is a second, independent writer of
   the same thing: it builds the handle out of the container's own bytes with
   plain library calls, from the layout in resource_info/vfs.md rather than
   from src/vfs.c.  The cases below hold the two against each other, which is
   what makes them an assertion about the container on disk and not about the
   emitted code agreeing with itself.

   The path a handle keeps sits between the count and the table:

     +0x04  the path, strlen bytes of it, terminated only by the zero the
            memset at 00039b43 left behind (memcpy at 00039b73)
     +0x11  the directory (fread at 00039ba0)

   VFS_NAME is ten characters, so its terminator lands at +0x0e and the two
   bytes after it stay zero -- that is the margin a path of 12 characters uses
   up exactly and a longer one spends. */
#define HANDLE_ARCHIVE_PATH_OFFSET 4

/* The same container in lower case.  DOS matches a filename without regard to
   case, so this opens the same file; what it separates is the path copy from
   the name search, which folds its query to upper case in place. */
#define VFS_NAME_LOWER "field2.vfs"

/* Enough repeats to run a DOS process out of file handles if the fclose at
   00039bb2 were not there.  Each handle is freed, so what is being counted is
   open files and not memory. */
#define OPEN_REPEATS 40

/* The handle the function builds against the handle the test builds: the same
   count in the same dword slot, and the container's whole directory, byte for
   byte, at the same offset.

   The directory comparison is what pins the second header field.  The table is
   fetched from the offset the header at +5 names (PUSH 0x5 at 00039b08), and
   FIELD2.VFS puts its table at 35 -- so a seek to a hard-coded 0 or to the
   entry count's own 7 would fill the handle with header and signature bytes
   instead of names, and 3,406 bytes of directory is not something two
   different wrong offsets could agree on.

   The count is asserted as a full dword.  FIELD2.VFS's 131 fits in a byte, so
   this case cannot separate the dword store at 00039b59 from a byte one; what
   it does establish is that the field the searches read one byte wide is the
   field this function wrote. */
static void open_real_container_matches_a_hand_built_handle(void)
{
    char *handle;
    char *reference;

    handle = (char *)fdps_vfs_open(VFS_NAME);
    reference = open_real_handle(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    CHECK_EQ(reference != NULL, 1);
    if (handle == NULL || reference == NULL) {
        free(handle);
        free(reference);
        return;
    }
    CHECK_EQ(*(unsigned int *)handle, VFS_ENTRY_COUNT);
    CHECK_EQ(memcmp(handle + HANDLE_ENTRY_TABLE_OFFSET,
                    reference + HANDLE_ENTRY_TABLE_OFFSET,
                    VFS_ENTRY_COUNT * HANDLE_ENTRY_SIZE),
             0);
    free(handle);
    free(reference);
}

/* The path the handle keeps, and the zeroed tail behind it.  The memcpy is
   strlen bytes long, so nothing writes the terminator: the byte at +0x0e is
   the memset's, and if the header were not zeroed first, or the copy were 13
   bytes wide, the string would run on into the two bytes after it.  Those two
   are asserted separately so a failure says which of the two writes went
   wrong. */
static void open_stores_the_archive_path(void)
{
    char *handle;

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    CHECK_EQ(strcmp(handle + HANDLE_ARCHIVE_PATH_OFFSET, VFS_NAME), 0);
    CHECK_EQ(handle[HANDLE_ARCHIVE_PATH_OFFSET + strlen(VFS_NAME)], 0);
    CHECK_EQ(handle[HANDLE_ENTRY_TABLE_OFFSET - 1], 0);
    free(handle);
}

/* The path is copied raw.  strupr is the search's business and not this
   function's -- there is no call to it anywhere in 00039ab0 -- so a container
   opened by a lower-case path keeps a lower-case path in its handle, while a
   member looked up by a lower-case name is folded before the compare. */
static void open_copies_the_path_without_folding_case(void)
{
    char *handle;

    handle = (char *)fdps_vfs_open(VFS_NAME_LOWER);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    CHECK_EQ(strcmp(handle + HANDLE_ARCHIVE_PATH_OFFSET, VFS_NAME_LOWER), 0);
    free(handle);
}

/* The failed-open exit: JZ 0x00039bba at 00039adc jumps everything and returns
   the NULL preset from 00039abc.  Unlike fdps_vfs_read_entry_count's 0, this
   answer is unambiguous -- a container that opens always produces a handle
   unless malloc fails. */
static void open_missing_file_returns_null(void)
{
    CHECK_EQ(fdps_vfs_open(MISSING_NAME) == NULL, 1);
}

/* The file is closed before the handle is handed back (CALL 0x000428be at
   00039bb2).  Nothing about a single handle can show that, so the container is
   opened forty times over: a function that left its FILE open would run the
   process out of stream slots long before the fortieth and start returning
   NULL from fopen. */
static void open_closes_the_container(void)
{
    char *handle;
    int repeat;
    int opened;

    opened = 0;
    for (repeat = 0; repeat < OPEN_REPEATS; repeat++) {
        handle = (char *)fdps_vfs_open(VFS_NAME);
        if (handle != NULL) {
            opened++;
            free(handle);
        }
    }
    CHECK_EQ(opened, OPEN_REPEATS);
}

/* The handle is the searches' input, so the two halves of the module are run
   end to end once: an interior entry to pin the stride the fread laid down,
   and the size of the last of the 131 to pin that the whole directory arrived.
   Both expected values are the shipped file's own, already used above against
   a hand-built handle. */
static void open_handle_drives_the_searches(void)
{
    char *handle;
    char query[QUERY_MAX];

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    strcpy(query, "ATTR610.DAT");
    CHECK_EQ(fdps_vfs_find_entry(query, handle), 64);
    strcpy(query, "DSC64.DAT");
    CHECK_EQ(fdps_vfs_find_entry_size(query, handle), DSC64_SIZE);
    free(handle);
}

/* fdps_vfs_load_file is the only reader that uses both halves of a handle: it
   searches the directory through fdps_vfs_find_entry and then reopens the
   container by the archive path the handle keeps at +4.  So every case below
   drives a handle fdps_vfs_open really built over a shipped container, and the
   file the loader reopens is the staged FIELD2.VFS itself.

   The member starts pair with the sizes and indices the cases above already
   use, and both come out of FIELD2.VFS's own entry table at the offset its
   header names, read with resource_info/vfs.md's layout: entry 0 is
   ATTR000.DAT at 3,441, entry 64 is ATTR610.DAT at 104,429 and entry 130 is
   DSC64.DAT at 112,314.  DSC64.DAT is the last of the 131 and its start plus
   its size is exactly the container's 112,350 bytes, which is what makes it
   the case that a start read one field too far cannot survive. */
#define ATTR000_INDEX 0
#define ATTR610_START 104429L
#define DSC64_INDEX 130
#define DSC64_START 112314L

/* A byte count no DOS/4GW image can satisfy, so malloc returns NULL and the
   branch at 00039c54 is taken.  It has to come from a directory the test wrote
   -- every shipped member fits easily, the largest anywhere being
   CHAPTER.SAF in MISC.VFS at 1,857,775 bytes. */
#define UNALLOCATABLE_SIZE 0x7FF00000UL

/* Big enough for the largest member used here, ATTR000.DAT at 1,553 bytes. */
#define MEMBER_MAX 2048
static char member_expected[MEMBER_MAX];

/* Reads a span of the container with plain library calls, so the bytes a case
   compares against are the file's own and not anything src/vfs.c produced.
   Returns 0 rather than asserting, and every caller checks. */
static int read_member_bytes(char *archive_name, long start,
                             unsigned int length)
{
    FILE *fp;
    size_t got;

    if (length > (unsigned int)MEMBER_MAX) {
        return 0;
    }
    fp = fopen(archive_name, "rb");
    if (fp == NULL) {
        return 0;
    }
    fseek(fp, start, SEEK_SET);
    got = fread(member_expected, length, 1, fp);
    fclose(fp);
    return got == 1;
}

/* Loads one member through the function under test and holds it against the
   same span of the container read independently.  Three checks: the load
   produced a buffer, the reference read worked, and the two agree byte for
   byte.  Every query goes through a writable buffer because the search folds
   it to upper case in place. */
static void check_member(char *handle, char *member_name, long start,
                         unsigned int length)
{
    char query[QUERY_MAX];
    char *member;

    strcpy(query, member_name);
    member = (char *)fdps_vfs_load_file(query, handle);
    CHECK_EQ(member != NULL, 1);
    CHECK_EQ(read_member_bytes(VFS_NAME, start, length), 1);
    if (member == NULL) {
        return;
    }
    CHECK_EQ(memcmp(member, member_expected, length), 0);
    free(member);
}

/* Three members of the shipped container, whole, out of a handle the module
   built for itself.  This is the function's ordinary path end to end: the
   search, the reopen through the handle's stored path, the size taken from the
   entry's field at 0x0d and the seek to the start at its 0x16.

   The three indices are what make it an assertion about the arithmetic and not
   just about entry 0.  Entry 0 begins where the table begins, so a wrong
   stride still finds it; entry 64 and entry 130 land on a different record
   under any stride but 26, and a record that is not the matched one holds a
   different start and a different length, which shows up as bytes that do not
   compare equal rather than as a near miss.  Entry 130 is also the last of the
   131 -- its 36 bytes end exactly at the container's 112,350 -- so a start
   read from a neighbouring field would run off the end of the file and leave
   the buffer holding whatever the short read did not fill. */
static void load_file_returns_whole_members(void)
{
    char *handle;

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    check_member(handle, "ATTR000.DAT", ATTR000_START, ATTR000_SIZE);
    check_member(handle, "ATTR610.DAT", ATTR610_START, ATTR610_SIZE);
    check_member(handle, "DSC64.DAT", DSC64_START, DSC64_SIZE);
    free(handle);
}

/* The first of the three failures: fdps_vfs_find_entry answers -1, the
   equality at 00039bf6 takes its branch, and what comes back is the NULL
   preset from 00039bdc.  The second check is the evidence that the query
   really did reach the search -- strupr rewrote the caller's buffer on the way
   through -- so a NULL that came from somewhere else would not look like this
   one. */
static void load_file_missing_member_returns_null(void)
{
    char *handle;
    char query[QUERY_MAX];

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    strcpy(query, "nosuch.dat");
    CHECK_EQ(fdps_vfs_load_file(query, handle) == NULL, 1);
    CHECK_EQ(strcmp(query, "NOSUCH.DAT"), 0);
    free(handle);
}

/* The query is handed to the search untouched, so everything the search does
   to it is this function's behaviour too: a lower-case name finds an
   upper-case entry and the caller's buffer comes back upper-cased.  It is also
   what pins the argument order -- the two pointers the other way round would
   have the search reading a member name as a handle and would find nothing. */
static void load_file_folds_the_query_like_the_search(void)
{
    char *handle;
    char query[QUERY_MAX];
    char *member;

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    strcpy(query, "dsc64.dat");
    member = (char *)fdps_vfs_load_file(query, handle);
    CHECK_EQ(member != NULL, 1);
    CHECK_EQ(strcmp(query, "DSC64.DAT"), 0);
    free(member);
    free(handle);
}

/* Which file is reopened.  The memcpy at 00039c00 takes 13 bytes from the
   handle's +4 and fopen is given that copy, so the container the bytes come
   out of is named by the handle and not by the query and not by anything the
   caller passes twice.  Pointing the field at a name nothing staged is the
   only way to see it: the search still succeeds, the entry is still found, and
   the answer is still NULL because the second open failed.

   The second half puts a lower-case spelling of the same container in the
   field.  DOS matches a filename without regard to case, so it opens, which
   says the path is used exactly as it was stored -- there is no strupr on this
   side, unlike the member name. */
static void load_file_reopens_the_path_out_of_the_handle(void)
{
    char *handle;
    char query[QUERY_MAX];
    char *member;

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    strcpy(handle + HANDLE_ARCHIVE_PATH_OFFSET, MISSING_NAME);
    strcpy(query, "DSC64.DAT");
    CHECK_EQ(fdps_vfs_load_file(query, handle) == NULL, 1);

    strcpy(handle + HANDLE_ARCHIVE_PATH_OFFSET, VFS_NAME_LOWER);
    strcpy(query, "DSC64.DAT");
    member = (char *)fdps_vfs_load_file(query, handle);
    CHECK_EQ(member != NULL, 1);
    free(member);
    free(handle);
}

/* Which of the entry's two identical size fields is read, and the malloc
   failure at the same time.  Every shipped container has 0x0d and 0x11 holding
   the same number, so the only way to separate them is to make them disagree
   in a handle already in memory -- and a byte count no allocation can satisfy
   turns the disagreement into an answer, because the field that is read
   decides whether malloc succeeds.

   0x0d unallocatable and 0x11 real answers NULL; 0x0d real and 0x11
   unallocatable loads the member whole.  A read of 0x11 gives exactly the
   opposite pair, so neither half can pass by accident.

   The NULL half is also the only coverage of the branch at 00039c54: nothing a
   shipped container can hold makes malloc fail. */
static void load_file_reads_the_size_field_not_its_duplicate(void)
{
    char *handle;
    char query[QUERY_MAX];

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    handle_set_entry_dword(handle, ATTR000_INDEX, ENTRY_SIZE_FIELD,
                           UNALLOCATABLE_SIZE);
    strcpy(query, "ATTR000.DAT");
    CHECK_EQ(fdps_vfs_load_file(query, handle) == NULL, 1);

    handle_set_entry_dword(handle, ATTR000_INDEX, ENTRY_SIZE_FIELD,
                           ATTR000_SIZE);
    handle_set_entry_dword(handle, ATTR000_INDEX, ENTRY_SIZE_DUP_FIELD,
                           UNALLOCATABLE_SIZE);
    check_member(handle, "ATTR000.DAT", ATTR000_START, ATTR000_SIZE);
    free(handle);
}

/* Where in the container the read begins.  Entry 130's start and size are
   rewritten to entry 0's, and the member that comes back under the name
   DSC64.DAT is ATTR000.DAT's 1,553 bytes -- so both numbers are taken from the
   entry the search matched, at 0x16 for the start and 0x0d for the size, and
   neither is recovered from anywhere else.

   The redirect is what separates the start field from the two dwords in front
   of it: reading 0x0d or 0x11 as the start would seek to 1,553 rather than to
   3,441, and the container's bytes there are header and directory rather than
   the member. */
static void load_file_seeks_to_the_entry_start_field(void)
{
    char *handle;

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    handle_set_entry_dword(handle, DSC64_INDEX, ENTRY_START_FIELD,
                           (unsigned long)ATTR000_START);
    handle_set_entry_dword(handle, DSC64_INDEX, ENTRY_SIZE_FIELD,
                           ATTR000_SIZE);
    check_member(handle, "DSC64.DAT", ATTR000_START, ATTR000_SIZE);
    free(handle);
}

/* Enough loads to run a DOS process out of stream slots if the fclose at
   00039ca7 were not reached.  Each buffer is freed, so what is being counted
   is open files and not memory. */
#define LOAD_REPEATS 40

/* The fclose is on the shared tail at 00039ca3, which the malloc failure
   rejoins rather than jumping, so a load that allocated nothing still closes
   what it opened.  Neither loop can see that on its own -- a leaked stream
   makes fopen fail and the function answers NULL through its middle branch
   instead, which looks the same from outside.  The assertion is the load
   afterwards: forty refusals followed by a member that still comes back whole
   is only possible if all forty closed. */
static void load_file_closes_the_container_on_both_paths(void)
{
    char *handle;
    char *member;
    char query[QUERY_MAX];
    int repeat;
    int loaded;
    int refused;

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }

    loaded = 0;
    for (repeat = 0; repeat < LOAD_REPEATS; repeat++) {
        strcpy(query, "DSC64.DAT");
        member = (char *)fdps_vfs_load_file(query, handle);
        if (member != NULL) {
            loaded++;
            free(member);
        }
    }
    CHECK_EQ(loaded, LOAD_REPEATS);

    handle_set_entry_dword(handle, DSC64_INDEX, ENTRY_SIZE_FIELD,
                           UNALLOCATABLE_SIZE);
    refused = 0;
    for (repeat = 0; repeat < LOAD_REPEATS; repeat++) {
        strcpy(query, "DSC64.DAT");
        if (fdps_vfs_load_file(query, handle) == NULL) {
            refused++;
        }
    }
    CHECK_EQ(refused, LOAD_REPEATS);

    handle_set_entry_dword(handle, DSC64_INDEX, ENTRY_SIZE_FIELD, DSC64_SIZE);
    check_member(handle, "DSC64.DAT", DSC64_START, DSC64_SIZE);
    free(handle);
}

/* fdps_vfs_image_get_entry is handed a whole container resident in memory, so
   the file cases below bring the container in whole rather than building a
   handle over its directory.  112,350 is the shipped FIELD2.VFS's own length
   (resource_info/vfs.md); it is asserted rather than assumed so a staged file
   that is not that container fails here instead of failing as arithmetic. */
#define VFS_IMAGE_BYTES 112350L

/* Reads a container into one block with plain library calls, exactly as the
   game holds its resident images.  Returns NULL rather than asserting, and
   every caller checks; the block is the caller's to free. */
static char *read_whole_container(char *archive_name, long *out_bytes)
{
    FILE *fp;
    char *image;
    long bytes;

    fp = fopen(archive_name, "rb");
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0L, SEEK_END);
    bytes = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    image = (char *)malloc((size_t)bytes);
    if (image == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(image, (size_t)bytes, 1, fp) != 1) {
        free(image);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_bytes = bytes;
    return image;
}

/* The real container, through the image reader: three members' starts and
   sizes as the shipped file records them.  The starts are the same numbers
   fdps_vfs_load_file seeks to, which is what makes this an assertion about one
   container read two ways rather than about this function's own arithmetic.

   The answer is checked as a distance from the image base, because that is
   what the function computes: ADD EAX,[EBP + -0x36] onto the image pointer at
   00039d83.  Entry 0 begins where the table begins so a wrong stride still
   finds it; entries 64 and 130 land on a different record under any stride but
   26, and every member of FIELD2.VFS has a different start and a different
   size.  Entry 130 is the last of the 131 and its 36 bytes end exactly at the
   container's 112,350, so a start read a field too far runs off the end. */
static void image_get_entry_returns_member_pointers(void)
{
    char *image;
    char *member;
    char query[QUERY_MAX];
    unsigned int size;
    long bytes;

    bytes = 0;
    image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(image != NULL, 1);
    if (image == NULL) {
        return;
    }
    CHECK_EQ(bytes, VFS_IMAGE_BYTES);

    size = 0;
    strcpy(query, "ATTR000.DAT");
    member = (char *)fdps_vfs_image_get_entry(
        (struct fdps_vfs_image_header *)image, query, &size);
    CHECK_EQ(member != NULL, 1);
    if (member != NULL) {
        CHECK_EQ(member - image, ATTR000_START);
    }
    CHECK_EQ(size, ATTR000_SIZE);

    size = 0;
    strcpy(query, "ATTR610.DAT");
    member = (char *)fdps_vfs_image_get_entry(
        (struct fdps_vfs_image_header *)image, query, &size);
    CHECK_EQ(member != NULL, 1);
    if (member != NULL) {
        CHECK_EQ(member - image, ATTR610_START);
    }
    CHECK_EQ(size, ATTR610_SIZE);

    size = 0;
    strcpy(query, "DSC64.DAT");
    member = (char *)fdps_vfs_image_get_entry(
        (struct fdps_vfs_image_header *)image, query, &size);
    CHECK_EQ(member != NULL, 1);
    if (member != NULL) {
        CHECK_EQ(member - image, DSC64_START);
    }
    CHECK_EQ(size, DSC64_SIZE);

    free(image);
}

/* A value no member size can be, so the slot says whether anything wrote to
   it.  The store at 00039d7e is inside the matched branch and there is no
   other store to out_size in the body, so a miss has to leave this standing --
   which is what makes the bare NULL the whole of the failure signal. */
#define SIZE_SENTINEL 0x5A5A5A5AL

static void image_get_entry_missing_member_leaves_the_size_alone(void)
{
    char *image;
    char query[QUERY_MAX];
    unsigned int size;
    long bytes;

    bytes = 0;
    image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(image != NULL, 1);
    if (image == NULL) {
        return;
    }
    size = (unsigned int)SIZE_SENTINEL;
    strcpy(query, "NOSUCH.DAT");
    CHECK_EQ(fdps_vfs_image_get_entry((struct fdps_vfs_image_header *)image,
                                      query, &size) == NULL,
             1);
    CHECK_EQ(size, SIZE_SENTINEL);
    free(image);
}

/* strupr is called on the caller's buffer inside the loop (CALL 0x000435f3 at
   00039d5f), so a mixed-case query finds the upper-case member and the
   caller's own string is upper-case afterwards.  Both halves are asserted: the
   second is not a side effect nobody depends on -- it is what
   fdps_baseani_get_entry_or_exit prints in its miss message. */
static void image_get_entry_uppercases_the_query_in_place(void)
{
    char *image;
    char *member;
    char query[QUERY_MAX];
    unsigned int size;
    long bytes;

    bytes = 0;
    image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(image != NULL, 1);
    if (image == NULL) {
        return;
    }
    size = 0;
    strcpy(query, "attr000.dat");
    member = (char *)fdps_vfs_image_get_entry(
        (struct fdps_vfs_image_header *)image, query, &size);
    CHECK_EQ(member != NULL, 1);
    if (member != NULL) {
        CHECK_EQ(member - image, ATTR000_START);
    }
    CHECK_EQ(size, ATTR000_SIZE);
    CHECK_EQ(strcmp(query, "ATTR000.DAT"), 0);
    free(image);
}

/* A container image the test writes, for the four things no shipped container
   can show: a table that does not start at 35, an entry count above 255, a
   lower-case member name, and a size field that disagrees with its own
   duplicate.  There is no file behind it and it is not pretending to be one.

   Its header fields go in as explicit little-endian bytes at offsets 5 and 7
   rather than through struct fdps_vfs_image_header, so that a wrong offset in
   that struct would show up here rather than cancelling out.  Entries are the
   same 26 bytes a container's are -- IMUL EDX,[EBP + -0xc],0x1a at 00039d2d --
   and the tail past the table is where the members would be, so a returned
   pointer stays inside the array. */
#define IMAGE_TABLE_OFFSET_FIELD 5
#define IMAGE_ENTRY_COUNT_FIELD 7
#define SYNTH_TABLE_OFFSET 0x40
#define SYNTH_ENTRIES 3
#define SYNTH_MEMBER_BYTES 64
#define SYNTH_MEMBER_START (SYNTH_TABLE_OFFSET + SYNTH_ENTRIES * HANDLE_ENTRY_SIZE)
static char synthetic_image[SYNTH_MEMBER_START + SYNTH_MEMBER_BYTES];

static void synthetic_image_reset(unsigned int table_offset,
                                  unsigned long entry_count)
{
    memset(synthetic_image, 0, sizeof(synthetic_image));
    synthetic_image[IMAGE_TABLE_OFFSET_FIELD] = (char)(table_offset & 0xffU);
    synthetic_image[IMAGE_TABLE_OFFSET_FIELD + 1] =
        (char)((table_offset >> 8) & 0xffU);
    synthetic_image[IMAGE_ENTRY_COUNT_FIELD] = (char)(entry_count & 0xffUL);
    synthetic_image[IMAGE_ENTRY_COUNT_FIELD + 1] =
        (char)((entry_count >> 8) & 0xffUL);
    synthetic_image[IMAGE_ENTRY_COUNT_FIELD + 2] =
        (char)((entry_count >> 16) & 0xffUL);
    synthetic_image[IMAGE_ENTRY_COUNT_FIELD + 3] =
        (char)((entry_count >> 24) & 0xffUL);
}

/* Both writers take the table offset as an argument rather than reading the
   header back, so a case that moves the table cannot silently write its
   entries where the old table was. */
static void synthetic_image_set_name(unsigned int table_offset, int index,
                                     char *entry_name)
{
    strcpy(synthetic_image + table_offset + index * HANDLE_ENTRY_SIZE,
           entry_name);
}

static void synthetic_image_set_dword(unsigned int table_offset, int index,
                                      int field_offset, unsigned long value)
{
    char *field;

    field = synthetic_image + table_offset + index * HANDLE_ENTRY_SIZE
            + field_offset;
    field[0] = (char)(value & 0xffUL);
    field[1] = (char)((value >> 8) & 0xffUL);
    field[2] = (char)((value >> 16) & 0xffUL);
    field[3] = (char)((value >> 24) & 0xffUL);
}

/* Fills the synthetic image with one findable member at index 2, leaving
   entries 0 and 1 zeroed -- their names are empty strings, which match
   nothing.  Index 2 rather than 0 is what makes the stride load-bearing: entry
   0 begins where the table begins whatever the stride is. */
#define SYNTH_MEMBER_INDEX 2
#define SYNTH_MEMBER_SIZE 111L
#define SYNTH_MEMBER_SIZE_DUP 222L
static void synthetic_image_build(unsigned int table_offset,
                                  unsigned long entry_count, char *entry_name)
{
    synthetic_image_reset(table_offset, entry_count);
    synthetic_image_set_name(table_offset, SYNTH_MEMBER_INDEX, entry_name);
    synthetic_image_set_dword(table_offset, SYNTH_MEMBER_INDEX,
                              ENTRY_SIZE_FIELD, SYNTH_MEMBER_SIZE);
    synthetic_image_set_dword(table_offset, SYNTH_MEMBER_INDEX,
                              ENTRY_SIZE_DUP_FIELD, SYNTH_MEMBER_SIZE_DUP);
    synthetic_image_set_dword(table_offset, SYNTH_MEMBER_INDEX,
                              ENTRY_START_FIELD, SYNTH_MEMBER_START);
}

/* Runs the function over the synthetic image and returns the distance from its
   base, or -1 for a miss, so the cases below read as one number each. */
static long synthetic_image_lookup(char *member_name, unsigned int *out_size)
{
    char query[QUERY_MAX];
    char *member;

    strcpy(query, member_name);
    member = (char *)fdps_vfs_image_get_entry(
        (struct fdps_vfs_image_header *)synthetic_image, query, out_size);
    if (member == NULL) {
        return -1L;
    }
    return member - synthetic_image;
}

/* Where the directory is comes out of the header's field at 5, zero-extended:
   XOR EAX,EAX / MOV AX,[EBP + -0x1b] at 00039d34.  Every shipped container has
   35 there, so a reader that had the 35 built into it would pass every case
   above; these two tables are at 0x40 and 0x20 and the same member is found
   through both. */
static void image_get_entry_reads_the_table_offset_from_the_header(void)
{
    unsigned int size;

    size = 0;
    synthetic_image_build(SYNTH_TABLE_OFFSET, SYNTH_ENTRIES, "MEMBER.DAT");
    CHECK_EQ(synthetic_image_lookup("MEMBER.DAT", &size), SYNTH_MEMBER_START);
    CHECK_EQ(size, SYNTH_MEMBER_SIZE);

    size = 0;
    synthetic_image_build(0x20, SYNTH_ENTRIES, "MEMBER.DAT");
    CHECK_EQ(synthetic_image_lookup("MEMBER.DAT", &size), SYNTH_MEMBER_START);
    CHECK_EQ(size, SYNTH_MEMBER_SIZE);
}

/* The size comes from the entry's field at 0x0d and not from the copy of it at
   0x11: MOV EAX,[EBP + -0x3f] at 00039d78, which is 0x0d into the entry copy
   at [EBP + -0x4c].  Every shipped container has the two fields agreeing, so
   only a directory the test wrote can tell them apart.  The start is read from
   0x16 the same way, MOV EAX,[EBP + -0x36] at 00039d83. */
static void image_get_entry_reads_the_size_field_not_its_duplicate(void)
{
    unsigned int size;

    size = 0;
    synthetic_image_build(SYNTH_TABLE_OFFSET, SYNTH_ENTRIES, "MEMBER.DAT");
    CHECK_EQ(synthetic_image_lookup("MEMBER.DAT", &size), SYNTH_MEMBER_START);
    CHECK_EQ(size, SYNTH_MEMBER_SIZE);
    CHECK_EQ(size != (unsigned int)SYNTH_MEMBER_SIZE_DUP, 1);
}

/* The loop bound is the header's whole 32-bit count, compared unsigned: CMP
   EAX,[EBP + -0x19] / JC at 00039d1f.  This is the one reader in the module
   that does either, and both halves are visible from outside.

   256 is the first count a byte-wide read gets wrong, and the contrast is
   asserted in the same case: fdps_vfs_image_entry_count reports 0 for this
   very image while the search still walks it.  0x80000000 is what separates
   JC from JL -- a signed compare makes the bound negative and the loop body
   never runs.  0 is the control: the count really is the bound, and an entry
   that is there is not found when the header does not admit to it. */
static void image_get_entry_reads_the_entry_count_as_a_whole_dword(void)
{
    unsigned int size;

    size = 0;
    synthetic_image_build(SYNTH_TABLE_OFFSET, 0x100UL, "MEMBER.DAT");
    CHECK_EQ(fdps_vfs_image_entry_count(
                 (struct fdps_vfs_image_header *)synthetic_image),
             0);
    CHECK_EQ(synthetic_image_lookup("MEMBER.DAT", &size), SYNTH_MEMBER_START);
    CHECK_EQ(size, SYNTH_MEMBER_SIZE);

    size = 0;
    synthetic_image_build(SYNTH_TABLE_OFFSET, 0x80000000UL, "MEMBER.DAT");
    CHECK_EQ(synthetic_image_lookup("MEMBER.DAT", &size), SYNTH_MEMBER_START);
    CHECK_EQ(size, SYNTH_MEMBER_SIZE);

    size = (unsigned int)SIZE_SENTINEL;
    synthetic_image_build(SYNTH_TABLE_OFFSET, 0UL, "MEMBER.DAT");
    CHECK_EQ(synthetic_image_lookup("MEMBER.DAT", &size), -1L);
    CHECK_EQ(size, SIZE_SENTINEL);
}

/* The fold is one-way.  strupr rewrites the query and the entry's own name is
   compared as it lies, so a lower-case member is unreachable however the query
   is spelled -- and the query is upper-case afterwards either way, which is
   how this case tells "not folded" from "not looked at".  No shipped container
   holds a lower-case name, so only a directory the test wrote can show it. */
static void image_get_entry_does_not_fold_the_entry_name(void)
{
    unsigned int size;
    char query[QUERY_MAX];

    size = (unsigned int)SIZE_SENTINEL;
    synthetic_image_build(SYNTH_TABLE_OFFSET, SYNTH_ENTRIES, "lower.dat");
    CHECK_EQ(synthetic_image_lookup("lower.dat", &size), -1L);
    CHECK_EQ(size, SIZE_SENTINEL);
    CHECK_EQ(synthetic_image_lookup("LOWER.DAT", &size), -1L);

    strcpy(query, "lower.dat");
    CHECK_EQ(fdps_vfs_image_get_entry(
                 (struct fdps_vfs_image_header *)synthetic_image, query,
                 &size) == NULL,
             1);
    CHECK_EQ(strcmp(query, "LOWER.DAT"), 0);

    size = 0;
    synthetic_image_build(SYNTH_TABLE_OFFSET, SYNTH_ENTRIES, "UPPER.DAT");
    CHECK_EQ(synthetic_image_lookup("upper.dat", &size), SYNTH_MEMBER_START);
    CHECK_EQ(size, SYNTH_MEMBER_SIZE);
}

/* The match breaks out of the loop (JMP 0x00039d8d at 00039d89), so a
   container holding one name twice always resolves to the earlier entry.  The
   duplicate goes in at index 0, ahead of the one the builder puts at index 2,
   and carries a different size and a different start -- so a walk that ran on
   to the last match would be caught by both numbers rather than by neither. */
#define SYNTH_FIRST_INDEX 0
#define SYNTH_FIRST_SIZE 333L
#define SYNTH_FIRST_START (SYNTH_MEMBER_START + 16)
static void image_get_entry_stops_at_the_first_match(void)
{
    unsigned int size;

    size = 0;
    synthetic_image_build(SYNTH_TABLE_OFFSET, SYNTH_ENTRIES, "MEMBER.DAT");
    synthetic_image_set_name(SYNTH_TABLE_OFFSET, SYNTH_FIRST_INDEX,
                             "MEMBER.DAT");
    synthetic_image_set_dword(SYNTH_TABLE_OFFSET, SYNTH_FIRST_INDEX,
                              ENTRY_SIZE_FIELD, SYNTH_FIRST_SIZE);
    synthetic_image_set_dword(SYNTH_TABLE_OFFSET, SYNTH_FIRST_INDEX,
                              ENTRY_START_FIELD, SYNTH_FIRST_START);
    CHECK_EQ(synthetic_image_lookup("MEMBER.DAT", &size), SYNTH_FIRST_START);
    CHECK_EQ(size, SYNTH_FIRST_SIZE);
}

/* fdps_vfs_load_file_or_exit is fdps_vfs_load_file plus the NULL check, and
   the check ends the process, so only the ordinary path can be exercised from
   a test at all: the miss arm runs fdps_wait_any_key and exit(1) (00029429 and
   00029430), which would hang the run on a machine with no key coming and then
   take the harness down with it.  What is left to assert is everything the
   wrapper does on the way through, and that is where its whole risk sits --
   the argument order, which is decided at the call sites and reversed again
   for the inner call, and the fact that the answer reaches the caller through
   a slot rather than as a return value.

   Every expected value below is one the fdps_vfs_load_file cases above already
   pin against the shipped FIELD2.VFS, which is the point: the wrapper is
   correct exactly when it produces what the loader produces. */

/* A non-null value to preset the out slot with, so "the slot was written" is
   distinguishable from "the slot happened to start as what we wanted".  Its
   address is all that is used; nothing reads the byte. */
static char out_slot_sentinel;
#define OUT_SENTINEL ((void *)&out_slot_sentinel)

/* The ordinary path end to end.  MOV EDX,[EBP + 0x1c] / MOV [EDX],EAX at
   0002941c is the only store the function makes, so the buffer arrives through
   the third argument and nowhere else -- the wrapper returns void, and a caller
   that took a return value would get the leftover slot pointer the epilogue
   happens to leave in EAX.

   ATTR000.DAT is entry 0 of FIELD2.VFS at 3,441 for 1,553 bytes, out of the
   container's own entry table (resource_info/vfs.md), and the bytes are read a
   second time here with plain library calls so the comparison is against the
   file rather than against anything src/vfs.c produced. */
static void load_or_exit_writes_the_member_through_out(void)
{
    char *handle;
    char query[QUERY_MAX];
    void *out;
    char *member;

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    out = OUT_SENTINEL;
    strcpy(query, "ATTR000.DAT");
    fdps_vfs_load_file_or_exit(handle, query, &out);
    CHECK_EQ(out != OUT_SENTINEL, 1);
    CHECK_EQ(out != NULL, 1);
    member = (char *)out;
    CHECK_EQ(read_member_bytes(VFS_NAME, ATTR000_START, ATTR000_SIZE), 1);
    if (member != NULL) {
        CHECK_EQ(memcmp(member, member_expected, ATTR000_SIZE), 0);
        free(member);
    }
    free(handle);
}

/* The argument order, which is the one thing about this function a reader
   cannot get from its body: the call sites push the out slot, then the query,
   then the handle (0001893c through 0001894b and the twelve like it), so the
   handle is first and the query second; the inner call then pushes them back
   the other way at 0002940f and 00029413, which makes the query
   fdps_vfs_load_file's first argument and the handle its second.

   Both halves show here at once.  The query goes in lower case and comes back
   folded, which only happens if it reached the strupr inside the search, and
   the member is found at all, which only happens if the handle reached the
   search as the container.  Swap the two and neither holds. */
static void load_or_exit_takes_the_handle_first_and_the_query_second(void)
{
    char *handle;
    char query[QUERY_MAX];
    void *out;

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    out = OUT_SENTINEL;
    strcpy(query, "attr000.dat");
    fdps_vfs_load_file_or_exit(handle, query, &out);
    CHECK_EQ(strcmp(query, "ATTR000.DAT"), 0);
    CHECK_EQ(out != NULL, 1);
    free(out);
    free(handle);
}

/* Nothing of the wrapper's own gets between the caller and the loader.  The
   same member comes out both ways and the two buffers agree byte for byte
   while being different blocks -- each load mallocs its own -- so the wrapper
   neither copies the member anywhere nor hands back something it allocated.

   DSC64.DAT rather than entry 0 on purpose: it is entry 130, the last of the
   131, and its 36 bytes end exactly at the container's 112,350, so a start or
   a size that came from a neighbouring field would run off the end of the file
   instead of landing on a plausible-looking member. */
static void load_or_exit_hands_back_what_the_loader_produced(void)
{
    char *handle;
    char query[QUERY_MAX];
    void *out;
    char *direct;

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL) {
        return;
    }
    out = OUT_SENTINEL;
    strcpy(query, "DSC64.DAT");
    fdps_vfs_load_file_or_exit(handle, query, &out);
    strcpy(query, "DSC64.DAT");
    direct = (char *)fdps_vfs_load_file(query, handle);
    CHECK_EQ(out != NULL, 1);
    CHECK_EQ(direct != NULL, 1);
    if (out != NULL && direct != NULL) {
        CHECK_EQ(out != (void *)direct, 1);
        CHECK_EQ(memcmp(out, direct, DSC64_SIZE), 0);
        CHECK_EQ(read_member_bytes(VFS_NAME, DSC64_START, DSC64_SIZE), 1);
        CHECK_EQ(memcmp(out, member_expected, DSC64_SIZE), 0);
    }
    free(out);
    free(direct);
    free(handle);
}

/* fdps_vfs_load_entry is fdps_vfs_open, fdps_vfs_load_file and free in one
   call, with the same fatal NULL check fdps_vfs_load_file_or_exit carries
   expanded inline (0002a140).  Both of its failure arms end the process -- the
   container that will not open at 0002a165 prints and exits, the member that is
   not there at 0002a1bc waits for a key and exits -- so, exactly as for the
   wrapper above, only the ordinary path can be exercised from a test at all.

   What is left is where all of this function's own risk sits: which argument is
   the container and which is the member, that the member's bytes are the
   container's own, and that the handle it built for itself does not survive the
   call.  Every expected value below is one the fdps_vfs_load_file cases already
   pin against the shipped FIELD2.VFS.

   One thing deliberately not asserted: that the free at 0002a1cf really
   released the handle.  A leaked handle is 3,423 bytes for this container and a
   DOS/4GW test process has megabytes, so no repeat count a test can afford
   would notice.  The repeat case below covers the file handle rather than the
   memory -- that one is exhaustible. */

/* Loads one member through fdps_vfs_load_entry and holds it against the same
   span of the container read independently, the way check_member does for
   fdps_vfs_load_file.  The query goes through a writable buffer because the
   search folds it to upper case in place. */
static void check_entry(char *archive_name, char *member_name, long start,
                        unsigned int length)
{
    char query[QUERY_MAX];
    char *member;

    strcpy(query, member_name);
    member = (char *)fdps_vfs_load_entry(archive_name, query);
    CHECK_EQ(member != NULL, 1);
    CHECK_EQ(read_member_bytes(VFS_NAME, start, length), 1);
    if (member == NULL) {
        return;
    }
    CHECK_EQ(memcmp(member, member_expected, length), 0);
    free(member);
}

/* The ordinary path end to end, three members deep.  The container is named by
   the first argument and opened here rather than being handed over as a handle,
   so this is the open, the search, the reopen through the path the handle
   stored, the size from the entry's field at 0x0d and the seek to its 0x16, all
   in one call.

   The three indices are what make it an assertion about the arithmetic rather
   than about entry 0: entry 0 begins where the table begins so a wrong stride
   still finds it, while entries 64 and 130 land on a different record under any
   stride but 26.  Entry 130 is the last of the 131 and its 36 bytes end exactly
   at the container's 112,350, so a start read a field too far runs off the end
   of the file instead of landing on a plausible member. */
static void load_entry_loads_whole_members(void)
{
    check_entry(VFS_NAME, "ATTR000.DAT", ATTR000_START, ATTR000_SIZE);
    check_entry(VFS_NAME, "ATTR610.DAT", ATTR610_START, ATTR610_SIZE);
    check_entry(VFS_NAME, "DSC64.DAT", DSC64_START, DSC64_SIZE);
}

/* The argument order, and the asymmetry between the two strings.

   The order is decided at the call sites and nowhere in the body: MOV
   EAX,0x61e24 / PUSH at 0002a3be then MOV EAX,0x60128 / PUSH at 0002a3c4 in
   fdps_title_screen put the container at [EBP + 0x14] and the member name at
   [EBP + 0x18].  Both halves show here at once -- the member is found, which
   only happens if the second argument reached the search, and the container
   opened, which only happens if the first reached fopen.  Swapped, the open
   would fail on "dsc64.dat" and the function would print and exit rather than
   fail an assertion.

   The container path is not folded and the member name is.  fdps_vfs_open
   copies a path raw, so a lower-case spelling opens the same file under DOS and
   comes back out of the call still lower-case; strupr inside the search
   rewrites the member name in the caller's own buffer.  That is why the member
   name a caller passes cannot be a read-only literal and the container path
   can. */
static void load_entry_folds_the_member_name_but_not_the_container_path(void)
{
    char archive[QUERY_MAX];
    char query[QUERY_MAX];
    char *member;

    strcpy(archive, VFS_NAME_LOWER);
    strcpy(query, "dsc64.dat");
    member = (char *)fdps_vfs_load_entry(archive, query);
    CHECK_EQ(member != NULL, 1);
    CHECK_EQ(strcmp(query, "DSC64.DAT"), 0);
    CHECK_EQ(strcmp(archive, VFS_NAME_LOWER), 0);
    free(member);
}

/* Nothing of this function's own gets between the caller and the loader.  The
   same member comes out both ways -- once through the one-call form and once
   through fdps_vfs_open followed by fdps_vfs_load_file -- and the two buffers
   agree byte for byte while being different blocks, so the answer is the
   loader's own malloc handed straight back rather than a copy this function
   made or the handle it built. */
static void load_entry_matches_open_plus_load_file(void)
{
    char *handle;
    char *direct;
    char *member;
    char query[QUERY_MAX];

    strcpy(query, "ATTR000.DAT");
    member = (char *)fdps_vfs_load_entry(VFS_NAME, query);
    CHECK_EQ(member != NULL, 1);

    handle = (char *)fdps_vfs_open(VFS_NAME);
    CHECK_EQ(handle != NULL, 1);
    if (handle == NULL || member == NULL) {
        free(member);
        free(handle);
        return;
    }
    strcpy(query, "ATTR000.DAT");
    direct = (char *)fdps_vfs_load_file(query, handle);
    CHECK_EQ(direct != NULL, 1);
    if (direct != NULL) {
        CHECK_EQ(member != direct, 1);
        CHECK_EQ(memcmp(member, direct, ATTR000_SIZE), 0);
        free(direct);
    }
    free(member);
    free(handle);
}

/* Every call opens the container twice -- once for the directory and once for
   the member -- and both fcloses are inside the two functions this one calls.
   Forty round trips is more streams than a DOS process has, so a path that left
   either file open would have fopen failing long before the fortieth; and a
   failed open here is not a NULL the assertion would catch, it is the printf
   and exit(1) at 0002a165, which takes the harness down.  Either way the run
   says so. */
static void load_entry_can_be_run_repeatedly(void)
{
    char query[QUERY_MAX];
    char *member;
    int repeat;
    int loaded;

    loaded = 0;
    for (repeat = 0; repeat < LOAD_REPEATS; repeat++) {
        strcpy(query, "DSC64.DAT");
        member = (char *)fdps_vfs_load_entry(VFS_NAME, query);
        if (member != NULL) {
            loaded++;
            free(member);
        }
    }
    CHECK_EQ(loaded, LOAD_REPEATS);
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
    RUN_TEST(find_entry_size_reports_real_member_sizes);
    RUN_TEST(find_entry_size_reads_the_size_field);
    RUN_TEST(find_entry_size_reads_a_whole_dword);
    RUN_TEST(find_entry_size_folds_only_the_query);
    RUN_TEST(find_entry_size_reads_the_entry_count_as_one_byte);
    RUN_TEST(find_entry_size_stops_at_the_first_match);
    RUN_TEST(open_real_container_matches_a_hand_built_handle);
    RUN_TEST(open_stores_the_archive_path);
    RUN_TEST(open_copies_the_path_without_folding_case);
    RUN_TEST(open_missing_file_returns_null);
    RUN_TEST(open_closes_the_container);
    RUN_TEST(open_handle_drives_the_searches);
    RUN_TEST(load_file_returns_whole_members);
    RUN_TEST(load_file_missing_member_returns_null);
    RUN_TEST(load_file_folds_the_query_like_the_search);
    RUN_TEST(load_file_reopens_the_path_out_of_the_handle);
    RUN_TEST(load_file_reads_the_size_field_not_its_duplicate);
    RUN_TEST(load_file_seeks_to_the_entry_start_field);
    RUN_TEST(load_file_closes_the_container_on_both_paths);
    RUN_TEST(image_get_entry_returns_member_pointers);
    RUN_TEST(image_get_entry_missing_member_leaves_the_size_alone);
    RUN_TEST(image_get_entry_uppercases_the_query_in_place);
    RUN_TEST(image_get_entry_reads_the_table_offset_from_the_header);
    RUN_TEST(image_get_entry_reads_the_size_field_not_its_duplicate);
    RUN_TEST(image_get_entry_reads_the_entry_count_as_a_whole_dword);
    RUN_TEST(image_get_entry_does_not_fold_the_entry_name);
    RUN_TEST(image_get_entry_stops_at_the_first_match);
    RUN_TEST(load_or_exit_writes_the_member_through_out);
    RUN_TEST(load_or_exit_takes_the_handle_first_and_the_query_second);
    RUN_TEST(load_or_exit_hands_back_what_the_loader_produced);
    RUN_TEST(load_entry_loads_whole_members);
    RUN_TEST(load_entry_folds_the_member_name_but_not_the_container_path);
    RUN_TEST(load_entry_matches_open_plus_load_file);
    RUN_TEST(load_entry_can_be_run_repeatedly);
}
