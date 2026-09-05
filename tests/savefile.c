/* tests/savefile.c -- cover for src/savefile.c.
 *
 * Expected values come from the assembly at 00056898 and 000568b7 -- SUB
 * ECX,0x4, XOR EAX,EAX before a LODSB that writes only AL, ADD EBX,EAX, and
 * for the cipher MOV DX,0xa5 / ADD DX,0x9014 / ROL DX,3 / XOR AL,DL / STOSB,
 * both loops closed by LOOP -- and from the shipped FDE.SAV itself.  None of
 * them is read off the emitted C.
 *
 * The checksum's buffers are staged here rather than read from a game file
 * because that function takes its entire input from its two arguments: it
 * reads no global and opens nothing.  The cipher is the routine that makes the
 * real file usable as a witness, and the last case does exactly that -- it
 * decrypts the shipped FDE.SAV and hands the plaintext to the checksum, which
 * has to agree with the dword the file itself stores at +0x59c7.  That single
 * assertion exercises the whole keystream over 22,987 bytes against a file the
 * game wrote, and neither routine can be wrong for it to hold.
 */
#include <stdio.h>
#include "testharn.h"
#include "savefile.h"

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

/* ------------------------------------------------------ fdps_xor_crypt_buffer

   The first eight keystream bytes, worked out by hand from the four
   instructions that make one and confirmed byte for byte against the shipped
   file: FDE.SAV opens with cc 01 b7 53 on disc and 00 01 16 ff once decrypted,
   so its first four keystream bytes are cc 00 a1 ac.

   The very first byte is the load-bearing one.  0xcc is rol16(0xa5 + 0x9014,
   3) & 0xff, and it pins two things at once that no later byte can separate:
   that the key is advanced before the XOR rather than after -- the 0xa5 seed
   is never applied to anything -- and that the rotate is sixteen bits wide.  A
   32-bit key rotating a preserved carry would give 0xc8 here. */
#define KEYSTREAM_LENGTH 8

static const unsigned char keystream[KEYSTREAM_LENGTH] = {
    0xcc, 0x00, 0xa1, 0xac, 0x06, 0xd1, 0x2c, 0x04
};

static unsigned char crypt_stage[16];

/* Crypting a run of zeroes copies the keystream out where it can be read. */
static void the_keystream_is_the_low_half_of_the_rotating_key(void)
{
    int i;

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        crypt_stage[i] = 0x00;
    }

    fdps_xor_crypt_buffer(crypt_stage, KEYSTREAM_LENGTH);

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        CHECK_EQ(crypt_stage[i], keystream[i]);
    }
}

/* The keystream is a function of the byte index alone -- nothing in the loop
   feeds a data byte back into DX.  A buffer of 0xff must therefore come out as
   the same keystream inverted, byte for byte, as the buffer of zeroes above.
   If the cipher were chained on its own output the two would diverge from the
   second byte on. */
static void the_keystream_does_not_depend_on_the_data(void)
{
    int i;

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        crypt_stage[i] = 0xff;
    }

    fdps_xor_crypt_buffer(crypt_stage, KEYSTREAM_LENGTH);

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        CHECK_EQ(crypt_stage[i], (unsigned char) (0xff ^ keystream[i]));
    }
}

/* The one property the game depends on: the same routine encrypts and
   decrypts, because the key is reseeded to 0xa5 on entry and the keystream
   restarts with it.  Both save call sites and both load call sites call this
   function and there is no second one. */
static void crypting_twice_restores_the_buffer(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        crypt_stage[i] = (unsigned char) (0x37 * i + 0x5a);
    }

    fdps_xor_crypt_buffer(crypt_stage, 16);
    fdps_xor_crypt_buffer(crypt_stage, 16);

    for (i = 0; i < 16; i++) {
        CHECK_EQ(crypt_stage[i], (unsigned char) (0x37 * i + 0x5a));
    }
}

/* ECX is the byte count, and STOSB writes one byte per pass: a length of 1
   touches the first byte and stops.  The neighbour is checked because the
   LOOP-after-body shape makes an off-by-one here write past the end rather
   than fall short. */
static void the_length_is_a_byte_count(void)
{
    crypt_stage[0] = 0x00;
    crypt_stage[1] = 0x00;
    crypt_stage[2] = 0x00;

    fdps_xor_crypt_buffer(crypt_stage, 1);

    CHECK_EQ(crypt_stage[0], keystream[0]);
    CHECK_EQ(crypt_stage[1], 0x00);
    CHECK_EQ(crypt_stage[2], 0x00);
}

/* The shipped save, and the only witness that covers the whole keystream.
   FDE.SAV is 0x59cb bytes, which is the length all eight call sites push. */
#define SAVE_NAME "FDE.SAV"
#define SAVE_IMAGE_SIZE 0x59cbL
#define SAVE_CHECKSUM_OFFSET 0x59c7L
#define SAVE_STORED_CHECKSUM 0x002dedc4L

static unsigned char save_image[0x59cb];

static int load_shipped_save(void)
{
    FILE *fp;
    size_t got;

    fp = fopen(SAVE_NAME, "rb");
    if (fp == NULL) {
        return 0;
    }
    got = fread(save_image, 1, (size_t) SAVE_IMAGE_SIZE, fp);
    fclose(fp);
    return got == (size_t) SAVE_IMAGE_SIZE;
}

/* Decrypt the file the game wrote and let it check itself.  The dword at
   +0x59c7 is the checksum the game stored when it saved, and
   fdps_compute_save_checksum over the plaintext has to reproduce it: 22,983
   keystream bytes all have to be right for that sum to land, and the stored
   dword is four more.  The opening bytes are asserted separately so that a
   failure says whether the keystream went wrong at the start or somewhere in
   the middle.

   The case skips itself when the file is not staged, the way the container
   cases in tests/rsrc.c do: there is nothing to assert against and a
   fabricated stand-in would only be asserting against bytes this test
   wrote. */
static void the_shipped_save_decrypts_to_its_own_checksum(void)
{
    unsigned long stored_checksum;

    if (!load_shipped_save()) {
        return;
    }

    fdps_xor_crypt_buffer(save_image, (unsigned int) SAVE_IMAGE_SIZE);

    stored_checksum =
        (unsigned long) save_image[SAVE_CHECKSUM_OFFSET]
        | ((unsigned long) save_image[SAVE_CHECKSUM_OFFSET + 1] << 8)
        | ((unsigned long) save_image[SAVE_CHECKSUM_OFFSET + 2] << 16)
        | ((unsigned long) save_image[SAVE_CHECKSUM_OFFSET + 3] << 24);

    CHECK_EQ(save_image[0], 0x00);
    CHECK_EQ(save_image[1], 0x01);
    CHECK_EQ(save_image[2], 0x16);
    CHECK_EQ(save_image[3], 0xff);
    CHECK_EQ(stored_checksum, SAVE_STORED_CHECKSUM);
    CHECK_EQ(fdps_compute_save_checksum(save_image,
                                        (unsigned int) SAVE_IMAGE_SIZE),
             stored_checksum);
}

void run_savefile_tests(void)
{
    RUN_TEST(trailing_four_bytes_are_not_summed);
    RUN_TEST(the_skip_starts_exactly_at_size_minus_four);
    RUN_TEST(bytes_are_summed_zero_extended);
    RUN_TEST(the_accumulator_is_thirty_two_bits_wide);
    RUN_TEST(size_five_sums_exactly_one_byte);
    RUN_TEST(the_image_is_not_written_to);
    RUN_TEST(the_keystream_is_the_low_half_of_the_rotating_key);
    RUN_TEST(the_keystream_does_not_depend_on_the_data);
    RUN_TEST(crypting_twice_restores_the_buffer);
    RUN_TEST(the_length_is_a_byte_count);
    RUN_TEST(the_shipped_save_decrypts_to_its_own_checksum);
}
