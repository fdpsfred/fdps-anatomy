/* tests/saf.c -- cover for src/saf.c.
 *
 * Expected values come from the assembly at 000140e0 -- MOV AX,word ptr
 * [EAX+0xc] / AND EAX,0xffff / CMP EAX,dword ptr [EBP+0x18] / JLE, then CMP
 * dword ptr [EBP+0x18],0x0 / JGE, then ADD EDX,dword ptr [EAX+0xe] / LEA
 * EAX,[EAX*0x4] / ADD EDX,EAX / MOV EAX,dword ptr [EBP+0x14] / ADD EAX,dword
 * ptr [EDX] -- and from the .SAF header layout in resource_info/saf.md (four
 * 10-byte section descriptors from +0x0c: u16 count, u32 start, u32 size; a
 * section opens with `count` u32 offsets, all measured from the start of the
 * file).  None of them is read off the emitted C.
 *
 * The image is staged here rather than read from a game file because a .SAF is
 * not a loose file: all 525 of them live inside .VFS containers and reach this
 * function only as a block fdps_vfs_load_entry has already unpacked into
 * memory, so a byte buffer is exactly the shape the function is handed.  Every
 * header and table field is written a byte at a time, so the test makes no
 * alignment assumption of its own about the fields the code reads unaligned.
 */
#include <stddef.h>
#include "testharn.h"
#include "saf.h"

/* Big enough that a frame-section start above 0xffff -- the only way to prove
   the u32 at +0x0e is read as 32 bits and not 16 -- still lands inside the
   buffer, with room past it for the offset table and for a rebased frame
   pointer to stay in bounds. */
#define IMAGE_SIZE 0x10020

static unsigned char stage_image[IMAGE_SIZE];

/* Writes the two header fields this reader looks at.  Byte at a time: the u32
   start field begins at 0x0e and is only 2-byte aligned in the real file. */
static void stage_header(unsigned long frame_count, unsigned long section_start)
{
    stage_image[0x0c] = (unsigned char) (frame_count & 0xff);
    stage_image[0x0d] = (unsigned char) ((frame_count >> 8) & 0xff);
    stage_image[0x0e] = (unsigned char) (section_start & 0xff);
    stage_image[0x0f] = (unsigned char) ((section_start >> 8) & 0xff);
    stage_image[0x10] = (unsigned char) ((section_start >> 16) & 0xff);
    stage_image[0x11] = (unsigned char) ((section_start >> 24) & 0xff);
}

/* Writes one u32 of a section's offset table: entry `index` of the table that
   begins at `section_start`, holding a file-relative offset. */
static void stage_entry(unsigned long section_start, int index,
                        unsigned long file_offset)
{
    unsigned char *entry;

    entry = stage_image + section_start + index * 4;
    entry[0] = (unsigned char) (file_offset & 0xff);
    entry[1] = (unsigned char) ((file_offset >> 8) & 0xff);
    entry[2] = (unsigned char) ((file_offset >> 16) & 0xff);
    entry[3] = (unsigned char) ((file_offset >> 24) & 0xff);
}

/* Puts the file's opening bytes back to the real magic and version.  They are
   non-zero on purpose: a reader that got the table base wrong and landed on
   offset 0 would otherwise read four zeroes and look like a plausible hit. */
static void stage_magic(void)
{
    stage_image[0] = 'S';
    stage_image[1] = 'A';
    stage_image[2] = 'F';
    stage_image[3] = 6;
}

/* How far into the image the returned frame pointer landed, or -1 for NULL.
   Every stored offset used below is positive, so the sentinel cannot collide
   with a real answer. */
static long frame_offset(int frame_index)
{
    void *frame;

    frame = fdps_saf_get_frame(stage_image, frame_index);
    if (frame == NULL) {
        return -1;
    }
    return (long) ((unsigned char *) frame - stage_image);
}

/* CMP dword ptr [EBP+0x18],0x0 / JGE 0x00014112: a negative index falls into
   the branch that stores 0, however many frames the header claims. */
static void negative_index_is_rejected(void)
{
    stage_magic();
    stage_header(4, 0x100);
    stage_entry(0x100, 0, 0x200);
    CHECK_EQ(frame_offset(-1), -1);
    CHECK_EQ(frame_offset(-1000), -1);
}

/* CMP EAX,dword ptr [EBP+0x18] / JLE 0x00014109 jumps to the NULL store when
   count <= index, so the last accepted index is count-1 and index == count is
   already out. */
static void index_at_and_past_the_count_is_rejected(void)
{
    stage_magic();
    stage_header(3, 0x100);
    stage_entry(0x100, 2, 0x2c0);
    CHECK_EQ(frame_offset(2), 0x2c0);
    CHECK_EQ(frame_offset(3), -1);
    CHECK_EQ(frame_offset(4), -1);
}

/* A count of zero leaves no accepted index at all: 0 <= 0 takes the same JLE. */
static void zero_count_accepts_nothing(void)
{
    stage_magic();
    stage_header(0, 0x100);
    stage_entry(0x100, 0, 0x200);
    CHECK_EQ(frame_offset(0), -1);
}

/* MOV EAX,dword ptr [EBP+0x14] / ADD EAX,dword ptr [EDX]: the stored offset is
   added to the image base, not to the address of the table entry it came out
   of.  With the table at 0x100 the two differ by 0x100, so the answer says
   which one the code used. */
static void offset_is_rebased_on_the_image_base(void)
{
    stage_magic();
    stage_header(1, 0x100);
    stage_entry(0x100, 0, 0x40);
    CHECK_EQ(frame_offset(0), 0x40);
}

/* LEA EAX,[EAX*0x4 + 0x0]: the table is stepped four bytes per index, so index
   2 must read the third u32 and not the third byte or the third short. */
static void table_entries_are_four_bytes_apart(void)
{
    stage_magic();
    stage_header(3, 0x100);
    stage_entry(0x100, 0, 0x400);
    stage_entry(0x100, 1, 0x500);
    stage_entry(0x100, 2, 0x600);
    CHECK_EQ(frame_offset(0), 0x400);
    CHECK_EQ(frame_offset(1), 0x500);
    CHECK_EQ(frame_offset(2), 0x600);
}

/* ADD EAX,dword ptr [EDX] reads a whole 32-bit entry, so an offset past 0xffff
   survives intact instead of being truncated to its low word. */
static void stored_offset_is_a_full_32_bit_value(void)
{
    stage_magic();
    stage_header(1, 0x100);
    stage_entry(0x100, 0, 0x10004);
    CHECK_EQ(frame_offset(0), 0x10004L);
}

/* ADD EDX,dword ptr [EAX+0xe] reads the section start as 32 bits from an
   offset that is only 2-byte aligned.  A start above 0xffff needs the bytes at
   0x10 and 0x11, so a 16-bit read would put the table at the image base and
   return the magic bytes as an offset. */
static void section_start_is_a_full_32_bit_value(void)
{
    stage_magic();
    stage_header(1, 0x10000);
    stage_entry(0x10000, 0, 0x38);
    CHECK_EQ(frame_offset(0), 0x38);
}

/* MOV AX,word ptr [EAX+0xc] takes 16 bits: with the section start written just
   past it, a 32-bit read of the same address would see 0x01000002 and accept
   every index in sight.  Index 1 is checked too, so the fixture is known to be
   live when index 2 comes back NULL. */
static void count_is_a_16_bit_field(void)
{
    stage_magic();
    stage_header(2, 0x100);
    stage_entry(0x100, 1, 0x240);
    stage_entry(0x100, 2, 0x280);
    CHECK_EQ(frame_offset(1), 0x240);
    CHECK_EQ(frame_offset(2), -1);
}

/* AND EAX,0xffff zero-extends the count word.  Sign extension would make
   0xffff into -1, and the signed CMP/JLE would then reject index 0 as well. */
static void count_is_zero_extended_not_sign_extended(void)
{
    stage_magic();
    stage_header(0xffff, 0x100);
    stage_entry(0x100, 0, 0x44);
    CHECK_EQ(frame_offset(0), 0x44);
}

void run_saf_tests(void)
{
    RUN_TEST(negative_index_is_rejected);
    RUN_TEST(index_at_and_past_the_count_is_rejected);
    RUN_TEST(zero_count_accepts_nothing);
    RUN_TEST(offset_is_rebased_on_the_image_base);
    RUN_TEST(table_entries_are_four_bytes_apart);
    RUN_TEST(stored_offset_is_a_full_32_bit_value);
    RUN_TEST(section_start_is_a_full_32_bit_value);
    RUN_TEST(count_is_a_16_bit_field);
    RUN_TEST(count_is_zero_extended_not_sign_extended);
}
