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

void run_vfs_tests(void)
{
    RUN_TEST(missing_file_returns_zero);
    RUN_TEST(real_container_entry_count);
    RUN_TEST(non_container_is_read_unchecked);
}
