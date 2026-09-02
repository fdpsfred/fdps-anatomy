/* tests/save.c -- cover for src/save.c.
 *
 * Expected values come from the assembly at 00056898 -- SUB ECX,0x4, XOR
 * EAX,EAX before a LODSB that writes only AL, ADD EBX,EAX, LOOP -- and from
 * one measurement against the shipped FDE.SAV, recorded below.  None of them
 * is read off the emitted C.
 *
 * The buffers are staged here rather than read from a game file because the
 * function takes its entire input from its two arguments: it reads no global
 * and opens nothing.  The real file would still be the better witness -- the
 * sum of the first 0x59c7 bytes of the decrypted fdps_game_files/FDE.SAV is
 * 0x2dedc4, which is exactly the dword stored at its +0x59c7 -- but reaching
 * that plaintext needs fdps_xor_crypt_buffer, which is not emitted yet, and
 * decrypting it a second time inside the test would prove nothing about the
 * function under test.  The three cases below pin the same three properties
 * that measurement confirms: the four-byte skip, zero extension, and a 32-bit
 * accumulator.
 */
#include "testharn.h"
#include "save.h"

/* 300 summed bytes plus the four skipped ones: 300 * 0xff overflows 16 bits,
   which is what the width case needs. */
#define WIDE_BYTES 304

static unsigned char stage_small[12];
static unsigned char stage_wide[WIDE_BYTES];

/* SUB ECX,0x4 at 000568a5.  Eight bytes summing to 36 followed by four 0xff
   bytes: the trailing four are the stored checksum field and must not enter
   the sum that gets compared against them.  Summing the buffer whole would
   give 36 + 4 * 0xff = 1056. */
static void trailing_four_bytes_are_not_summed(void)
{
    int i;

    for (i = 0; i < 8; i++) {
        stage_small[i] = (unsigned char) (i + 1);
    }
    for (i = 8; i < 12; i++) {
        stage_small[i] = 0xff;
    }

    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 36);
}

/* The boundary itself: the byte at size - 4 is the first one left out, and the
   byte at size - 5 is the last one taken in.  One 0x7f planted on either side
   of the line separates a count of size - 4 from a count of size - 3 or
   size - 5, which the case above cannot. */
static void the_skip_starts_exactly_at_size_minus_four(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        stage_small[i] = 0x00;
    }
    stage_small[8] = 0x7f;
    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 0);

    stage_small[8] = 0x00;
    stage_small[7] = 0x7f;
    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 0x7f);
}

/* XOR EAX,EAX at 000568aa followed by a LODSB that writes AL alone: bytes
   enter the sum zero-extended.  0x80 + 0xff + 0x80 + 0xff is 766 unsigned and
   -258 if the image were read as signed char. */
static void bytes_are_summed_zero_extended(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        stage_small[i] = 0x00;
    }
    stage_small[0] = 0x80;
    stage_small[1] = 0xff;
    stage_small[2] = 0x80;
    stage_small[3] = 0xff;

    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 766);
}

/* ADD EBX,EAX is a 32-bit add into a 32-bit accumulator.  300 bytes of 0xff
   sum to 76500, which does not fit in 16 bits: a narrower accumulator would
   report 76500 - 65536 = 10964. */
static void the_accumulator_is_thirty_two_bits_wide(void)
{
    int i;

    for (i = 0; i < WIDE_BYTES; i++) {
        stage_wide[i] = 0xff;
    }
    for (i = WIDE_BYTES - 4; i < WIDE_BYTES; i++) {
        stage_wide[i] = 0x00;
    }

    CHECK_EQ(fdps_compute_save_checksum(stage_wide, WIDE_BYTES), 76500L);
}

/* The shortest walk the loop can take without wrapping its count: size 5 sums
   exactly one byte.  LOOP tests after the body, so a size of 4 would walk 2^32
   bytes instead of none -- that one is described in save.c and deliberately
   not exercised here. */
static void size_five_sums_exactly_one_byte(void)
{
    stage_small[0] = 0x2a;
    stage_small[1] = 0xff;
    stage_small[2] = 0xff;
    stage_small[3] = 0xff;
    stage_small[4] = 0xff;

    CHECK_EQ(fdps_compute_save_checksum(stage_small, 5), 0x2a);
}

/* LODSB alone: there is no STOSB in this routine, unlike fdps_xor_crypt_buffer
   next to it, so the image comes back untouched.  A caller that computed the
   checksum over a buffer it was about to write out would otherwise be shipping
   whatever the routine had scribbled. */
static void the_image_is_not_written_to(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        stage_small[i] = (unsigned char) (0x11 * (i + 1));
    }

    fdps_compute_save_checksum(stage_small, 12);

    CHECK_EQ(stage_small[0], 0x11);
    CHECK_EQ(stage_small[7], 0x88);
    CHECK_EQ(stage_small[11], (unsigned char) (0x11 * 12));
}

void run_save_tests(void)
{
    RUN_TEST(trailing_four_bytes_are_not_summed);
    RUN_TEST(the_skip_starts_exactly_at_size_minus_four);
    RUN_TEST(bytes_are_summed_zero_extended);
    RUN_TEST(the_accumulator_is_thirty_two_bits_wide);
    RUN_TEST(size_five_sums_exactly_one_byte);
    RUN_TEST(the_image_is_not_written_to);
}
