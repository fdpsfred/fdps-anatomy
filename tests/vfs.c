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

void run_vfs_tests(void)
{
    RUN_TEST(missing_file_returns_zero);
    RUN_TEST(real_container_entry_count);
    RUN_TEST(non_container_is_read_unchecked);
    RUN_TEST(image_entry_count_real_container);
    RUN_TEST(image_entry_count_truncates_to_low_byte);
    RUN_TEST(image_entry_count_reads_offset_seven);
}
