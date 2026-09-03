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
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "gamedata.h"
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

/* --- fdps_saf_play_over_background @ 0001ecf0 --------------------------- */

/* WHAT THE PLAYBACK CASES ASSERT AGAINST.  This routine draws on the real
   adapter -- it copies the caller's background into a private page, composes
   the frame on top of it and puts that page's 320x200 window at 0xa0000 -- so
   the cases below put the machine in mode 13h, run one whole clip with a timer
   interrupt going, and read the aperture back afterwards.  The interrupt is
   not optional scaffolding: nothing else moves data_fdps_timer_tick_counter,
   and the wait at 0001edd0 spins until it changes.  The premise that the
   aperture reads back at all is asserted first, so a machine where it does not
   fails there instead of failing every geometry case for the wrong reason.

   WHERE THE EXPECTED POSITIONS COME FROM.  The request's x and y are both
   0x18 -- MOV dword ptr [EBP-0x2c],0x18 and MOV dword ptr [EBP-0x28],0x18 --
   and the window the two fdps_blit_rect calls move is at page + 0x2298, which
   is row 0x18, column 0x18 of a 0x170-byte pitch.  The two 0x18s cancel, so a
   frame layer carrying offset 0,0 lands on screen pixel 0,0 and one carrying
   x,y lands on screen column x, row y.  A layer carrying a small negative
   offset lands in the page's margin instead, which is the whole reason the
   page is 48 pixels wider and taller than the screen.

   WHAT IS NOT COVERED.  The sound flag is PUSH 0x1 at 0001ed94, so every frame
   fires its own effect as it is drawn; the fixture frames all carry sound -1
   and fdps_sfx_play does nothing at all while the audio flags are clear, so no
   assertion here can see that argument.  It takes a machine with a driver. */

/* The visible screen and the two BIOS modes the cases move between. */
#define PLAY_VGA_BASE 0x000a0000
#define PLAY_SCREEN_W 0x140
#define PLAY_SCREEN_H 0xc8
#define PLAY_SCREEN_BYTES (PLAY_SCREEN_W * PLAY_SCREEN_H)
#define PLAY_MODE_TEXT 0x03
#define PLAY_MODE_320X200X256 0x13

/* The BIOS timer, the one interrupt that has to be running for the wait loop
   to end. */
#define PLAY_TIMER_VECTOR 8

/* Painted over the whole aperture before every run.  The background pattern
   never produces it and neither does any tile, so a byte still holding it
   afterwards is a byte the routine did not reach. */
#define PLAY_SENTINEL 0x5a

/* The fixture .SAF: three 4x2 tiles of one pixel value each, a single-cell
   tilemap naming each of them, and three frames each naming one tilemap at an
   offset of its own.  Header offsets first -- the magic, the cell size, and
   the count and start of each of the three sections the drawers read. */
#define PSAF_BYTES 0x100
#define PSAF_CELL_W_AT 0x07
#define PSAF_CELL_H_AT 0x09
#define PSAF_FRAME_COUNT_AT 0x0c
#define PSAF_FRAME_TABLE_PTR_AT 0x0e
#define PSAF_TILEMAP_COUNT_AT 0x16
#define PSAF_TILEMAP_TABLE_PTR_AT 0x18
#define PSAF_TILE_COUNT_AT 0x20
#define PSAF_TILE_TABLE_PTR_AT 0x22

/* Where the fixture puts each section.  The header ends at 0x34; then a
   three-entry tile table and the streams, a three-entry tilemap table and the
   records, and a three-entry frame table and the records. */
#define PSAF_TILE_TABLE_AT 0x34
#define PSAF_TILE0_AT 0x40
#define PSAF_TILE_STRIDE 4
#define PSAF_TILEMAP_TABLE_AT 0x4c
#define PSAF_TILEMAP0_AT 0x58
#define PSAF_TILEMAP_STRIDE 8
#define PSAF_FRAME_TABLE_AT 0x70
#define PSAF_FRAME0_AT 0x80
#define PSAF_FRAME_STRIDE 0x20

/* A frame record: i16 sound, i16 duration, i16 layer count at +8, then
   13-byte layers of tilemap number, i16 x, i16 y and a blend flag.  Sound
   0xffff is the -1 every real frame with no effect carries. */
#define PSAF_FRAME_SOUND_AT 0x00
#define PSAF_FRAME_DURATION_AT 0x02
#define PSAF_FRAME_LAYER_COUNT_AT 0x08
#define PSAF_LAYER_AT 0x0a
#define PSAF_LAYER_TILEMAP_AT 0x00
#define PSAF_LAYER_X_AT 0x02
#define PSAF_LAYER_Y_AT 0x04
#define PSAF_LAYER_BLEND_AT 0x06
#define PSAF_NO_SOUND 0xffff

/* The fixture's cell: four across and two down, small enough that no two
   frames' marks can meet and tall enough that a draw one row out shows. */
#define PLAY_CELL_W 4
#define PLAY_CELL_H 2

/* The three frames' layer offsets and pixel values.  Every offset is well
   inside the screen and no two boxes touch; the pixel values are non-zero,
   because zero is the drawer's transparency key, and all three are below 0x80,
   which the background never is. */
#define PLAY_F0_X 0
#define PLAY_F0_Y 0
#define PLAY_F0_PIXEL 0x11
#define PLAY_F1_X 100
#define PLAY_F1_Y 40
#define PLAY_F1_PIXEL 0x22
#define PLAY_F2_X 200
#define PLAY_F2_Y 80
#define PLAY_F2_PIXEL 0x33

/* How far a one-frame clip is pushed off the left edge in the margin case:
   two of the cell's four columns end up outside the window and so unseen. */
#define PLAY_OVERHANG 2

static unsigned char play_saf[PSAF_BYTES];
static unsigned char play_background[PLAY_SCREEN_BYTES];
static unsigned char play_screen[PLAY_SCREEN_BYTES];

static void (__interrupt __far *play_saved_timer)();

static void __interrupt __far play_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(play_saved_timer);
}

static void play_u16(int at, unsigned int value)
{
    play_saf[at] = (unsigned char) (value & 0xff);
    play_saf[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void play_u32(int at, unsigned long value)
{
    play_saf[at] = (unsigned char) (value & 0xff);
    play_saf[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    play_saf[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    play_saf[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* The background the clip plays over.  Every byte has bit 7 set, so no byte of
   it can be mistaken for the sentinel or for any of the three marks. */
static int play_pattern(int row, int col)
{
    return ((row * 23 + col * 13) & 0x7f) | 0x80;
}

static void play_stage_background(void)
{
    int row;
    int col;

    for (row = 0; row < PLAY_SCREEN_H; row++) {
        for (col = 0; col < PLAY_SCREEN_W; col++) {
            play_background[row * PLAY_SCREEN_W + col] =
                (unsigned char) play_pattern(row, col);
        }
    }
}

/* One frame record: one layer, naming one tilemap and carrying the offset the
   drawer adds to the request's own origin.  Every frame is held for one tick,
   so a three-frame clip is three passes round the loop. */
static void play_stage_frame(int index, int tilemap, int layer_x, int layer_y)
{
    int at;

    at = PSAF_FRAME0_AT + index * PSAF_FRAME_STRIDE;
    play_u16(at + PSAF_FRAME_SOUND_AT, PSAF_NO_SOUND);
    play_u16(at + PSAF_FRAME_DURATION_AT, 1);
    play_u16(at + PSAF_FRAME_LAYER_COUNT_AT, 1);
    play_u16(at + PSAF_LAYER_AT + PSAF_LAYER_TILEMAP_AT,
             (unsigned int) tilemap);
    play_u16(at + PSAF_LAYER_AT + PSAF_LAYER_X_AT,
             (unsigned int) (layer_x & 0xffff));
    play_u16(at + PSAF_LAYER_AT + PSAF_LAYER_Y_AT,
             (unsigned int) (layer_y & 0xffff));
    play_saf[at + PSAF_LAYER_AT + PSAF_LAYER_BLEND_AT] = 0;
}

/* The fixture image, with `frame_count` of its three frames declared in the
   header.  Command 0x03 is a fill run of four pixels (resource_info/cel.md),
   so two of them make the two rows of one 4x2 cell. */
static void play_stage_clip(int frame_count)
{
    int index;
    int pixel[3];

    pixel[0] = PLAY_F0_PIXEL;
    pixel[1] = PLAY_F1_PIXEL;
    pixel[2] = PLAY_F2_PIXEL;

    memset(play_saf, 0, (size_t) PSAF_BYTES);
    play_saf[0] = 'S';
    play_saf[1] = 'A';
    play_saf[2] = 'F';
    play_u16(PSAF_CELL_W_AT, PLAY_CELL_W);
    play_u16(PSAF_CELL_H_AT, PLAY_CELL_H);

    play_u16(PSAF_TILE_COUNT_AT, 3);
    play_u32(PSAF_TILE_TABLE_PTR_AT, (unsigned long) PSAF_TILE_TABLE_AT);
    play_u16(PSAF_TILEMAP_COUNT_AT, 3);
    play_u32(PSAF_TILEMAP_TABLE_PTR_AT, (unsigned long) PSAF_TILEMAP_TABLE_AT);
    for (index = 0; index < 3; index++) {
        play_u32(PSAF_TILE_TABLE_AT + index * 4,
                 (unsigned long) (PSAF_TILE0_AT + index * PSAF_TILE_STRIDE));
        play_saf[PSAF_TILE0_AT + index * PSAF_TILE_STRIDE] = 0x03;
        play_saf[PSAF_TILE0_AT + index * PSAF_TILE_STRIDE + 1] =
            (unsigned char) pixel[index];
        play_saf[PSAF_TILE0_AT + index * PSAF_TILE_STRIDE + 2] = 0x03;
        play_saf[PSAF_TILE0_AT + index * PSAF_TILE_STRIDE + 3] =
            (unsigned char) pixel[index];
        play_u32(PSAF_TILEMAP_TABLE_AT + index * 4,
                 (unsigned long) (PSAF_TILEMAP0_AT
                                  + index * PSAF_TILEMAP_STRIDE));
        play_u16(PSAF_TILEMAP0_AT + index * PSAF_TILEMAP_STRIDE, 1);
        play_u16(PSAF_TILEMAP0_AT + index * PSAF_TILEMAP_STRIDE + 2, 1);
        play_u16(PSAF_TILEMAP0_AT + index * PSAF_TILEMAP_STRIDE + 4,
                 (unsigned int) index);
    }

    play_u16(PSAF_FRAME_COUNT_AT, (unsigned int) frame_count);
    play_u32(PSAF_FRAME_TABLE_PTR_AT, (unsigned long) PSAF_FRAME_TABLE_AT);
    for (index = 0; index < 3; index++) {
        play_u32(PSAF_FRAME_TABLE_AT + index * 4,
                 (unsigned long) (PSAF_FRAME0_AT + index * PSAF_FRAME_STRIDE));
    }
    play_stage_frame(0, 0, PLAY_F0_X, PLAY_F0_Y);
    play_stage_frame(1, 1, PLAY_F1_X, PLAY_F1_Y);
    play_stage_frame(2, 2, PLAY_F2_X, PLAY_F2_Y);
}

static void play_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* One whole clip, with the adapter in the mode the game plays it in and a
   timer interrupt running, leaving the finished screen in play_screen[]. */
static void play_run(void)
{
    play_stage_background();

    play_set_mode(PLAY_MODE_320X200X256);
    memset((void *) PLAY_VGA_BASE, PLAY_SENTINEL, (size_t) PLAY_SCREEN_BYTES);

    play_saved_timer = _dos_getvect(PLAY_TIMER_VECTOR);
    _dos_setvect(PLAY_TIMER_VECTOR, play_timer_isr);
    fdps_saf_play_over_background(play_saf, play_background);
    _dos_setvect(PLAY_TIMER_VECTOR, play_saved_timer);

    memmove(play_screen, (void *) PLAY_VGA_BASE, (size_t) PLAY_SCREEN_BYTES);
    play_set_mode(PLAY_MODE_TEXT);
}

static int play_pixel(int row, int col)
{
    return (int) play_screen[row * PLAY_SCREEN_W + col];
}

/* How many bytes of the captured screen differ from the background they were
   painted from while lying outside the given box. */
static int play_mismatches_outside(int first_row, int last_row, int first_col,
                                   int last_col)
{
    int row;
    int col;
    int bad;

    bad = 0;
    for (row = 0; row < PLAY_SCREEN_H; row++) {
        for (col = 0; col < PLAY_SCREEN_W; col++) {
            if (row >= first_row && row <= last_row && col >= first_col
                && col <= last_col) {
                continue;
            }
            if (play_screen[row * PLAY_SCREEN_W + col]
                != play_background[row * PLAY_SCREEN_W + col]) {
                bad++;
            }
        }
    }
    return bad;
}

/* How many bytes of the captured screen still hold the sentinel. */
static int play_sentinel_left(void)
{
    long at;
    int kept;

    kept = 0;
    for (at = 0; at < (long) PLAY_SCREEN_BYTES; at++) {
        if (play_screen[at] == PLAY_SENTINEL) {
            kept++;
        }
    }
    return kept;
}

/* The premise every case below rests on: in mode 13h the aperture is a plain
   linear window that reads back what was written to it, at the first byte of
   the frame and at its last. */
static void play_the_aperture_reads_back_in_mode_13h(void)
{
    unsigned char *aperture;

    aperture = (unsigned char *) PLAY_VGA_BASE;
    play_set_mode(PLAY_MODE_320X200X256);
    memset(aperture, PLAY_SENTINEL, (size_t) PLAY_SCREEN_BYTES);
    play_screen[0] = aperture[0];
    play_screen[1] = aperture[PLAY_SCREEN_BYTES - 1];
    play_set_mode(PLAY_MODE_TEXT);

    CHECK_EQ(play_screen[0], PLAY_SENTINEL);
    CHECK_EQ(play_screen[1], PLAY_SENTINEL);
}

/* The loop draws the frame the cursor names and only then advances it, and it
   leaves on the advance that answers 1 -- CMP dword ptr [EBP-0x4],0x0 / JNZ at
   the top with the answer stored at 0001edf2.  A three-frame clip therefore
   ends with its LAST frame on the screen: frame 2's mark is there and frames 0
   and 1 are not, because the background goes back down under every pass.  A
   routine that advanced before drawing would end on frame 0's mark, one that
   kept going after the wrap would too, and one that painted the background
   only once would show all three at the same time. */
static void play_ends_on_the_clips_last_frame(void)
{
    play_stage_clip(3);
    play_run();

    CHECK_EQ(play_pixel(PLAY_F2_Y, PLAY_F2_X), PLAY_F2_PIXEL);
    CHECK_EQ(play_pixel(PLAY_F0_Y, PLAY_F0_X),
             play_pattern(PLAY_F0_Y, PLAY_F0_X));
    CHECK_EQ(play_pixel(PLAY_F1_Y, PLAY_F1_X),
             play_pattern(PLAY_F1_Y, PLAY_F1_X));
}

/* PUSH 0xc8 / PUSH 0x140 / PUSH 0x140 for the second fdps_blit_rect: the whole
   320x200 window travels to the adapter, so no byte of the screen is left
   holding the sentinel the run started from.  A short row count or a narrow
   row would leave some. */
static void play_covers_the_whole_screen(void)
{
    play_stage_clip(3);
    play_run();

    CHECK_EQ(play_sentinel_left(), 0);
}

/* The first fdps_blit_rect copies the caller's own 320x200 background into the
   page's window before every frame, so once the last frame is drawn the only
   bytes on the screen that differ from that background are the last frame's
   own 4x2 mark.  This is the assertion that the background is the caller's and
   not the page's leftovers. */
static void play_repaints_the_background_under_every_frame(void)
{
    play_stage_clip(3);
    play_run();

    CHECK_EQ(play_mismatches_outside(PLAY_F2_Y, PLAY_F2_Y + PLAY_CELL_H - 1,
                                     PLAY_F2_X, PLAY_F2_X + PLAY_CELL_W - 1),
             0);
    CHECK_EQ(play_pixel(PLAY_F2_Y, PLAY_F2_X + PLAY_CELL_W - 1),
             PLAY_F2_PIXEL);
    CHECK_EQ(play_pixel(PLAY_F2_Y + PLAY_CELL_H - 1, PLAY_F2_X),
             PLAY_F2_PIXEL);
}

/* The request's origin is 0x18,0x18 and the window it publishes starts at
   0x2298 -- row 0x18, column 0x18 of the 0x170-pitch page -- so the two cancel
   and a layer at offset 0,0 lands on screen pixel 0,0.  Dropping either 0x18
   moves the frame 24 pixels; dropping the request's would also put the cell at
   x = 0, which fdps_draw_tilemap_cell's strict x > 0 test refuses outright and
   nothing would be drawn at all. */
static void play_puts_a_zero_offset_layer_at_the_screen_origin(void)
{
    play_stage_clip(1);
    play_run();

    CHECK_EQ(play_pixel(0, 0), PLAY_F0_PIXEL);
    CHECK_EQ(play_pixel(PLAY_CELL_H - 1, PLAY_CELL_W - 1), PLAY_F0_PIXEL);
    CHECK_EQ(play_mismatches_outside(0, PLAY_CELL_H - 1, 0, PLAY_CELL_W - 1),
             0);
}

/* THE 24-PIXEL MARGIN IS WHAT THE OVERSIZED PAGE IS FOR.  The page is 0x170 by
   0xf8 -- 48 wider and taller than the screen -- and the frame is composed at
   0x18,0x18 inside it, so a layer carrying a small negative offset is drawn
   into the margin instead of being refused or wrapping onto the row above.
   With the layer two pixels left of the origin the cell still starts at page
   column 0x16, which clears the strict x > 0 test, and its first two columns
   fall outside the published window: the screen shows the other two at columns
   0 and 1 and nothing at all appears at the right-hand end of any row.  A page
   the size of the screen would either drop the cell or smear it round. */
static void play_draws_an_overhanging_layer_into_the_margin(void)
{
    play_stage_clip(1);
    play_stage_frame(0, 0, PLAY_F0_X - PLAY_OVERHANG, PLAY_F0_Y);
    play_run();

    CHECK_EQ(play_pixel(0, 0), PLAY_F0_PIXEL);
    CHECK_EQ(play_pixel(0, 1), PLAY_F0_PIXEL);
    CHECK_EQ(play_pixel(PLAY_CELL_H - 1, 1), PLAY_F0_PIXEL);
    CHECK_EQ(play_mismatches_outside(0, PLAY_CELL_H - 1, 0,
                                     PLAY_CELL_W - PLAY_OVERHANG - 1),
             0);
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
    RUN_TEST(play_the_aperture_reads_back_in_mode_13h);
    RUN_TEST(play_ends_on_the_clips_last_frame);
    RUN_TEST(play_covers_the_whole_screen);
    RUN_TEST(play_repaints_the_background_under_every_frame);
    RUN_TEST(play_puts_a_zero_offset_layer_at_the_screen_origin);
    RUN_TEST(play_draws_an_overhanging_layer_into_the_margin);
}
