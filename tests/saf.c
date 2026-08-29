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
 *
 * The playback cases at the end read the same image through a cursor, and
 * their expected values come from 00014550 -- XOR EAX,EAX / MOV AL,byte ptr
 * [EBP+0x18] / CMP EAX,0x1 for the reset, INC dword ptr [EAX+0x4] placed after
 * the frame-count test, MOVSX EDX,word ptr [EAX+0x2] / CMP EDX,dword ptr
 * [EAX+0x4] / JG for the hold, and CMP byte ptr [EBP+0x18],0x0 / JNZ for the
 * choice between restarting and stopping at the end.
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

/* Writes the three header bytes fdps_saf_frame_count tests, so a case can put
   any combination of matching and non-matching bytes in front of it. */
static void stage_magic_bytes(int first, int second, int third)
{
    stage_image[0] = (unsigned char) first;
    stage_image[1] = (unsigned char) second;
    stage_image[2] = (unsigned char) third;
}

/* The real header passes and the answer is the count word: MOV AX,word ptr
   [EDX+0xc] with the accepting branch taken. */
static void real_magic_returns_the_header_count(void)
{
    stage_magic();
    stage_header(7, 0x100);
    CHECK_EQ(fdps_saf_frame_count(stage_image), 7);
}

/* CMP EAX,0x53 / JZ accept, CMP EAX,0x41 / JNZ third-test (fall-through
   accepts), CMP EAX,0x46 / JNZ reject: the three tests are ORed, so each byte
   on its own admits an image whose other two bytes are wrong.  An && would
   turn all three of these into 0. */
static void any_single_magic_byte_admits_the_image(void)
{
    stage_header(5, 0x100);
    stage_magic_bytes('S', 'x', 'y');
    CHECK_EQ(fdps_saf_frame_count(stage_image), 5);
    stage_magic_bytes('x', 'A', 'y');
    CHECK_EQ(fdps_saf_frame_count(stage_image), 5);
    stage_magic_bytes('x', 'y', 'F');
    CHECK_EQ(fdps_saf_frame_count(stage_image), 5);
}

/* Only the last JNZ's target stores 0, and reaching it needs all three
   compares to have failed. */
static void all_three_magic_bytes_wrong_returns_zero(void)
{
    stage_header(5, 0x100);
    stage_magic_bytes('x', 'y', 'z');
    CHECK_EQ(fdps_saf_frame_count(stage_image), 0);
}

/* The bytes are read at +0, +1 and +2 in that order and nowhere else: 'A' in
   the first position or 'S' in the second is not the byte its compare is
   looking at, so a transposed magic is refused. */
static void each_magic_byte_is_tested_at_its_own_offset(void)
{
    stage_header(5, 0x100);
    stage_magic_bytes('A', 'S', 'x');
    CHECK_EQ(fdps_saf_frame_count(stage_image), 0);
    stage_magic_bytes('x', 'F', 'A');
    CHECK_EQ(fdps_saf_frame_count(stage_image), 0);
}

/* MOV AX,word ptr [EDX+0xc] takes 16 bits from 0x0c.  The section start
   written right behind it starts with 0x01,0x02, so a 32-bit read of the same
   address would see 0x02010007 instead of 7, and a read displaced two bytes
   either way would see the junk below or the start's low word. */
static void count_is_the_16_bit_field_at_0x0c(void)
{
    stage_magic();
    stage_header(7, 0x201);
    stage_image[0x0a] = 0x77;
    stage_image[0x0b] = 0x88;
    CHECK_EQ(fdps_saf_frame_count(stage_image), 7);
}

/* XOR EAX,EAX before the word load zero-extends it, so the largest count a
   header can state comes back as 65535.  A sign-extending read would return
   -1 and every caller's loop would run zero times. */
static void count_0xffff_is_zero_extended(void)
{
    stage_magic();
    stage_header(0xffff, 0x100);
    CHECK_EQ(fdps_saf_frame_count(stage_image), 65535L);
}

/* An image that passes the magic test but states no frames returns 0, the
   same answer a refused image gives -- the two are deliberately not
   distinguishable. */
static void passing_image_with_no_frames_returns_zero(void)
{
    stage_magic();
    stage_header(0, 0x100);
    CHECK_EQ(fdps_saf_frame_count(stage_image), 0);
}

/* Where the staged clip puts its parts: the frame section's offset table at
   0x100 and the frame records themselves well past it, far enough apart that a
   reader landing on the wrong one reads a duration written for another frame
   rather than a byte of its neighbour. */
#define CLIP_TABLE 0x100
#define CLIP_FRAME_0 0x300
#define CLIP_FRAME_STRIDE 0x40

/* Writes the head of one frame record: i16 sound id then i16 duration in
   ticks.  The duration goes in byte at a time, so a case can state the bit
   pattern 0xffff and let the code decide whether that is -1 or 65535 -- which
   is the whole question MOVSX asks. */
static void stage_duration(int frame_index, unsigned long duration)
{
    unsigned char *frame;

    frame = stage_image + CLIP_FRAME_0 + frame_index * CLIP_FRAME_STRIDE;
    frame[0] = 0xff; /* sound id -1: this frame starts no sound effect */
    frame[1] = 0xff;
    frame[2] = (unsigned char) (duration & 0xff);
    frame[3] = (unsigned char) ((duration >> 8) & 0xff);
}

/* A whole playable clip: real magic, a frame count, an offset table and that
   many frame records, each holding for one tick until a case says otherwise. */
static void stage_clip(int frame_count)
{
    int i;

    stage_magic();
    stage_header(frame_count, CLIP_TABLE);
    for (i = 0; i < frame_count; i++) {
        stage_entry(CLIP_TABLE, i,
                    CLIP_FRAME_0 + i * CLIP_FRAME_STRIDE);
        stage_duration(i, 1);
    }
}

/* The caller's three-dword playback block, pointed at the staged image. */
static int play_cursor[3];

static void stage_cursor(int frame_index, int ticks_held)
{
    play_cursor[SAF_CURSOR_FRAME_INDEX] = frame_index;
    play_cursor[SAF_CURSOR_TICKS_HELD] = ticks_held;
    play_cursor[SAF_CURSOR_IMAGE] = (int) stage_image;
}

/* XOR EAX,EAX / MOV AL,byte ptr [EBP+0x18] / CMP EAX,0x1 / JNZ is the first
   thing the function does, and the branch it guards writes both counters and
   nothing else.  The image is left with all three magic bytes wrong, which
   every other mode answers -1 to, so an implementation that looked at the
   image before the mode would be caught here. */
static void reset_mode_clears_the_cursor_without_reading_the_image(void)
{
    stage_clip(2);
    stage_magic_bytes('x', 'y', 'z');
    stage_cursor(1, 5);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 1), 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 0);
}

/* The magic test feeds the same count-or-zero the standalone reader does, and
   a zero count stores -1 and leaves.  INC dword ptr [EAX+0x4] sits after that
   test, so the tick counter must come back untouched. */
static void all_three_magic_bytes_wrong_returns_minus_one(void)
{
    stage_clip(2);
    stage_magic_bytes('x', 'y', 'z');
    stage_cursor(0, 4);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), -1);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 4);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 0);
}

/* The three magic compares are ORed here as well: 'A' in the second position
   admits the image whatever the other two hold, and playback proceeds. */
static void one_matching_magic_byte_lets_the_clip_run(void)
{
    stage_clip(2);
    stage_duration(0, 5);
    stage_magic_bytes('x', 'A', 'y');
    stage_cursor(0, 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 1);
}

/* CMP dword ptr [EBP+-0xc],0x0 / JNZ: a header that states no frames is the
   -1 answer, and again without the tick counter moving. */
static void an_image_with_no_frames_returns_minus_one(void)
{
    stage_magic();
    stage_header(0, CLIP_TABLE);
    stage_cursor(0, 2);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), -1);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 2);
}

/* INC dword ptr [EAX+0x4] happens before MOVSX EDX,word ptr [EAX+0x2] / CMP
   EDX,dword ptr [EAX+0x4] / JG, so the count reaches the duration on the
   duration-th call: a frame stating 3 is shown for ticks 1, 2 and 3 and steps
   on at the third.  JG rather than JGE is what puts the step on that call and
   not the one after it. */
static void a_frame_is_held_for_exactly_its_duration(void)
{
    stage_clip(2);
    stage_duration(0, 3);
    stage_duration(1, 3);
    stage_cursor(0, 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 1);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 2);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 1);
}

/* A duration of 0 is already <= the freshly incremented 1, so the frame is
   never actually held: the step happens on the first call. */
static void a_zero_duration_frame_steps_on_at_the_first_tick(void)
{
    stage_clip(3);
    stage_duration(0, 0);
    stage_cursor(0, 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 1);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 0);
}

/* MOVSX sign-extends the duration word, so the pattern 0xffff is -1 and steps
   the frame on immediately.  Read as an unsigned 65535 it would hold the frame
   for about eighteen minutes at the game's tick rate, which is the visible
   difference this pins. */
static void a_negative_duration_steps_on_at_the_first_tick(void)
{
    stage_clip(3);
    stage_duration(0, 0xffff);
    stage_cursor(0, 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 1);
}

/* The frame whose duration is read is the one the cursor's index names, found
   through the offset table: with frame 0 stating 1 tick and frame 1 stating 4,
   a cursor sitting on frame 1 must last four calls.  A lookup that ignored the
   index, or rebased the stored offset on the table instead of the image base,
   would read frame 0's duration or a zero and step on at the first call. */
static void the_duration_comes_from_the_frame_the_index_names(void)
{
    stage_clip(2);
    stage_duration(0, 1);
    stage_duration(1, 4);
    stage_cursor(1, 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 1);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 1);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 3);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 1);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 0);
}

/* CMP EAX,dword ptr [EBP+-0xc] / JL, then CMP byte ptr [EBP+0x18],0x0 / JNZ
   with the fall-through storing 0: stepping off the end of the clip in mode 0
   restarts at frame 0, zeroes the tick counter and reports 1 once. */
static void passing_the_last_frame_reports_one_and_wraps_in_mode_zero(void)
{
    stage_clip(3);
    stage_duration(2, 1);
    stage_cursor(2, 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 0), 1);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 0);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 0);
}

/* The other side of that JNZ: MOV EAX,dword ptr [EBP+-0xc] / DEC EAX leaves
   the cursor on the last frame instead.  A caller that keeps ticking a clamped
   cursor gets 1 again every time its last frame's duration elapses, and the
   index never moves. */
static void any_other_mode_stops_on_the_last_frame(void)
{
    stage_clip(3);
    stage_duration(2, 1);
    stage_cursor(2, 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 2), 1);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 2);
    CHECK_EQ(play_cursor[SAF_CURSOR_TICKS_HELD], 0);
    CHECK_EQ(fdps_saf_advance_tick(play_cursor, 2), 1);
    CHECK_EQ(play_cursor[SAF_CURSOR_FRAME_INDEX], 2);
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
    RUN_TEST(real_magic_returns_the_header_count);
    RUN_TEST(any_single_magic_byte_admits_the_image);
    RUN_TEST(all_three_magic_bytes_wrong_returns_zero);
    RUN_TEST(each_magic_byte_is_tested_at_its_own_offset);
    RUN_TEST(count_is_the_16_bit_field_at_0x0c);
    RUN_TEST(count_0xffff_is_zero_extended);
    RUN_TEST(passing_image_with_no_frames_returns_zero);
    RUN_TEST(reset_mode_clears_the_cursor_without_reading_the_image);
    RUN_TEST(all_three_magic_bytes_wrong_returns_minus_one);
    RUN_TEST(one_matching_magic_byte_lets_the_clip_run);
    RUN_TEST(an_image_with_no_frames_returns_minus_one);
    RUN_TEST(a_frame_is_held_for_exactly_its_duration);
    RUN_TEST(a_zero_duration_frame_steps_on_at_the_first_tick);
    RUN_TEST(a_negative_duration_steps_on_at_the_first_tick);
    RUN_TEST(the_duration_comes_from_the_frame_the_index_names);
    RUN_TEST(passing_the_last_frame_reports_one_and_wraps_in_mode_zero);
    RUN_TEST(any_other_mode_stops_on_the_last_frame);
}
