/* tests/palette.c -- cover for src/palette.c.
 *
 * Expected values come from the assembly at 00022f40 -- MOV [EBP-8] from the
 * start argument then CMP against [EBP+0x1c] with JLE, PUSH 0x3c8 once per
 * entry and PUSH 0x3c9 three times, MOV AL,byte ptr [EAX] / [EAX+1] / [EAX+2]
 * followed by AND EAX,0xff, ADD EDX,EAX with the bias, CMP [EBP-4],0x3f / JLE
 * then CMP [EBP-4],0x0 / JGE, and ADD dword ptr [EBP+0x14],0x3 at the bottom
 * of the loop -- and from the palette record ticket 17 settled (red +0, green
 * +1, blue +2, stride 3).  None of them is read off the emitted C.
 *
 * The packed-word cases at the bottom come from the channel extractors --
 * 0002ae90 shifts right 16 for red, 0002aec0 shifts right 8 for green, 0002aef0
 * does not shift at all for blue, and all three mask with 0xff -- and from the
 * caller that builds the words, 0002afac onwards, where a six-bit DAC byte is
 * widened by LEA EAX,[EAX*4+0] and shifted into place by SHL EAX,0x10 for red
 * and SHL EDX,0x8 for green, blue being OR'd in where it already sits.
 *
 * WHAT THE COLOUR WRITES ARE OBSERVED THROUGH.  This function's entire output
 * is DAC entries, and the VGA DAC is readable: entry number to 0x3c7, then
 * three reads of 0x3c9 give back red, green and blue.  So every assertion here
 * is made against the hardware the function actually wrote to rather than
 * against a mock.  The first test establishes that premise -- a DAC that did
 * not read back would make every later assertion meaningless rather than
 * false.
 *
 * The entries used are 200 and up, well clear of the sixteen a text-mode
 * console displays, so a test that leaves one black does not take the report
 * with it.
 *
 * The source palette is staged here as a plain byte array and passed in
 * through a cast, not built as an array of the record type: the point of the
 * stride assertions is that the function steps three bytes per entry, and
 * staging it as records would make that step whatever the record happens to
 * be rather than what the assembly does.
 */
#include <conio.h>
#include "testharn.h"
#include "fdpstype.h"
#include "palette.h"

#define VGA_DAC_READ_INDEX 0x3c7
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9

/* The block of DAC entries these tests upload into, and one entry either side
   of it that is loaded with a marker and must survive. */
#define SCRATCH_FIRST 200
#define SCRATCH_COUNT 4

/* Room for one source record per scratch entry, plus one past the end so a
   walk that runs long has somewhere to be caught doing it. */
#define STAGE_ENTRIES 6

static unsigned char stage_rgb[STAGE_ENTRIES * 3];

static struct fdps_palette_entry *staged(void)
{
    return (struct fdps_palette_entry *) stage_rgb;
}

/* Loads entry n of the staged palette with three distinct byte values. */
static void stage_entry(int n, int red, int green, int blue)
{
    stage_rgb[n * 3] = (unsigned char) red;
    stage_rgb[n * 3 + 1] = (unsigned char) green;
    stage_rgb[n * 3 + 2] = (unsigned char) blue;
}

static void write_dac_entry(int entry, int red, int green, int blue)
{
    outp(VGA_DAC_WRITE_INDEX, entry);
    outp(VGA_DAC_DATA, red);
    outp(VGA_DAC_DATA, green);
    outp(VGA_DAC_DATA, blue);
}

/* Reads one DAC entry into three elements of components[]. */
static void read_dac_entry(int entry, int *components)
{
    outp(VGA_DAC_READ_INDEX, entry);
    components[0] = (int) (inp(VGA_DAC_DATA) & 0x3f);
    components[1] = (int) (inp(VGA_DAC_DATA) & 0x3f);
    components[2] = (int) (inp(VGA_DAC_DATA) & 0x3f);
}

/* Puts a known marker into the scratch block and the entry either side of it,
   so any entry the function did not write says so on its own. */
static void mark_scratch_block(void)
{
    int entry;

    for (entry = -1; entry <= SCRATCH_COUNT; entry++) {
        write_dac_entry(SCRATCH_FIRST + entry, 1, 2, 3);
    }
}

static void the_dac_reads_back_what_was_written(void)
{
    int components[3];

    write_dac_entry(SCRATCH_FIRST, 13, 29, 47);
    read_dac_entry(SCRATCH_FIRST, components);
    CHECK_EQ(components[0], 13);
    CHECK_EQ(components[1], 29);
    CHECK_EQ(components[2], 47);
}

/* MOV AL,[EAX] / [EAX+1] / [EAX+2] with the three PUSH 0x3c9 in that order:
   byte 0 of the record is the red the DAC gets first, byte 1 the green and
   byte 2 the blue.  With every bias zero nothing else happens to them. */
static void zero_bias_uploads_the_source_bytes_unchanged(void)
{
    int components[3];

    mark_scratch_block();
    stage_entry(0, 5, 40, 63);

    fdps_set_palette_range(staged(), SCRATCH_FIRST, SCRATCH_FIRST, 0, 0, 0);

    read_dac_entry(SCRATCH_FIRST, components);
    CHECK_EQ(components[0], 5);
    CHECK_EQ(components[1], 40);
    CHECK_EQ(components[2], 63);
}

/* JLE against [EBP+0x1c]: the last entry is uploaded, and the one after it is
   not.  The entry below the first is not touched either. */
static void the_range_is_inclusive_at_both_ends(void)
{
    int components[3];

    mark_scratch_block();
    stage_entry(0, 10, 11, 12);
    stage_entry(1, 20, 21, 22);

    fdps_set_palette_range(staged(), SCRATCH_FIRST, SCRATCH_FIRST + 1,
                           0, 0, 0);

    read_dac_entry(SCRATCH_FIRST - 1, components);
    CHECK_EQ(components[0], 1);
    CHECK_EQ(components[1], 2);
    CHECK_EQ(components[2], 3);
    read_dac_entry(SCRATCH_FIRST, components);
    CHECK_EQ(components[0], 10);
    CHECK_EQ(components[1], 11);
    CHECK_EQ(components[2], 12);
    read_dac_entry(SCRATCH_FIRST + 1, components);
    CHECK_EQ(components[0], 20);
    CHECK_EQ(components[1], 21);
    CHECK_EQ(components[2], 22);
    read_dac_entry(SCRATCH_FIRST + 2, components);
    CHECK_EQ(components[0], 1);
    CHECK_EQ(components[1], 2);
    CHECK_EQ(components[2], 3);
}

/* The bound is tested before the first upload -- MOV EAX,[EBP-8] / CMP /
   JLE with the JMP to the epilogue on the fall-through -- so a last entry
   below the first uploads nothing at all rather than one entry. */
static void a_backwards_range_uploads_nothing(void)
{
    int components[3];

    mark_scratch_block();
    stage_entry(0, 33, 34, 35);

    fdps_set_palette_range(staged(), SCRATCH_FIRST + 1, SCRATCH_FIRST,
                           0, 0, 0);

    read_dac_entry(SCRATCH_FIRST, components);
    CHECK_EQ(components[0], 1);
    CHECK_EQ(components[1], 2);
    CHECK_EQ(components[2], 3);
    read_dac_entry(SCRATCH_FIRST + 1, components);
    CHECK_EQ(components[0], 1);
    CHECK_EQ(components[1], 2);
    CHECK_EQ(components[2], 3);
}

/* first == last is one entry, not none: the compare is <=, and the increment
   happens after the body. */
static void a_single_entry_range_uploads_exactly_one(void)
{
    int components[3];

    mark_scratch_block();
    stage_entry(0, 44, 45, 46);
    stage_entry(1, 55, 56, 57);

    fdps_set_palette_range(staged(), SCRATCH_FIRST, SCRATCH_FIRST, 0, 0, 0);

    read_dac_entry(SCRATCH_FIRST, components);
    CHECK_EQ(components[0], 44);
    CHECK_EQ(components[1], 45);
    CHECK_EQ(components[2], 46);
    read_dac_entry(SCRATCH_FIRST + 1, components);
    CHECK_EQ(components[0], 1);
    CHECK_EQ(components[1], 2);
    CHECK_EQ(components[2], 3);
}

/* ADD dword ptr [EBP+0x14],0x3 at the bottom of the loop: the source advances
   three bytes per entry, so entry n of the range takes bytes 3n, 3n+1 and 3n+2
   of the staged block, and the entries come out in ascending order. */
static void each_entry_advances_the_source_by_three_bytes(void)
{
    int components[3];
    int n;

    mark_scratch_block();
    for (n = 0; n < SCRATCH_COUNT; n++) {
        stage_entry(n, n * 3, n * 3 + 1, n * 3 + 2);
    }

    fdps_set_palette_range(staged(), SCRATCH_FIRST,
                           SCRATCH_FIRST + SCRATCH_COUNT - 1, 0, 0, 0);

    for (n = 0; n < SCRATCH_COUNT; n++) {
        read_dac_entry(SCRATCH_FIRST + n, components);
        CHECK_EQ(components[0], n * 3);
        CHECK_EQ(components[1], n * 3 + 1);
        CHECK_EQ(components[2], n * 3 + 2);
    }
}

/* The source pointer is the caller's, not a base the range indexes: starting
   the range at the second scratch entry still reads the staged block from its
   first record. */
static void the_source_starts_at_the_pointer_not_at_the_entry(void)
{
    int components[3];

    mark_scratch_block();
    stage_entry(0, 7, 8, 9);
    stage_entry(1, 17, 18, 19);

    fdps_set_palette_range(staged(), SCRATCH_FIRST + 1, SCRATCH_FIRST + 2,
                           0, 0, 0);

    read_dac_entry(SCRATCH_FIRST + 1, components);
    CHECK_EQ(components[0], 7);
    CHECK_EQ(components[1], 8);
    CHECK_EQ(components[2], 9);
    read_dac_entry(SCRATCH_FIRST + 2, components);
    CHECK_EQ(components[0], 17);
    CHECK_EQ(components[1], 18);
    CHECK_EQ(components[2], 19);
}

/* CMP dword ptr [EBP-4],0x3f / JLE: 63 is kept, 64 becomes 63.  A source byte
   already above the 6-bit range clamps the same way. */
static void the_upper_clamp_holds_at_sixty_three(void)
{
    int components[3];

    mark_scratch_block();
    stage_entry(0, 63, 60, 255);

    fdps_set_palette_range(staged(), SCRATCH_FIRST, SCRATCH_FIRST,
                           0, 4, 0);

    read_dac_entry(SCRATCH_FIRST, components);
    CHECK_EQ(components[0], 63);
    CHECK_EQ(components[1], 63);
    CHECK_EQ(components[2], 63);
}

/* CMP dword ptr [EBP-4],0x0 / JGE: 0 is kept and -1 becomes 0, so a bias that
   takes a component below black lands on black rather than wrapping into a
   bright colour. */
static void the_lower_clamp_holds_at_zero(void)
{
    int components[3];

    mark_scratch_block();
    stage_entry(0, 0, 1, 10);

    fdps_set_palette_range(staged(), SCRATCH_FIRST, SCRATCH_FIRST,
                           0, -2, -40);

    read_dac_entry(SCRATCH_FIRST, components);
    CHECK_EQ(components[0], 0);
    CHECK_EQ(components[1], 0);
    CHECK_EQ(components[2], 0);
}

/* AND EAX,0xff zero-extends the source byte, so 200 enters the sum as 200.
   Sign extension would make it -56, and -56 + -160 would clamp to 0 instead
   of arriving at 40. */
static void the_source_byte_is_taken_unsigned(void)
{
    int components[3];

    mark_scratch_block();
    stage_entry(0, 200, 200, 200);

    fdps_set_palette_range(staged(), SCRATCH_FIRST, SCRATCH_FIRST,
                           -160, -170, -180);

    read_dac_entry(SCRATCH_FIRST, components);
    CHECK_EQ(components[0], 40);
    CHECK_EQ(components[1], 30);
    CHECK_EQ(components[2], 20);
}

/* Three separate stack arguments at [EBP+0x20], [EBP+0x24] and [EBP+0x28],
   each added to its own channel: a bias on one channel moves that channel and
   leaves the other two where they were. */
static void each_bias_moves_only_its_own_channel(void)
{
    int components[3];

    mark_scratch_block();
    stage_entry(0, 20, 20, 20);

    fdps_set_palette_range(staged(), SCRATCH_FIRST, SCRATCH_FIRST,
                           5, -5, 0);

    read_dac_entry(SCRATCH_FIRST, components);
    CHECK_EQ(components[0], 25);
    CHECK_EQ(components[1], 15);
    CHECK_EQ(components[2], 20);
}

/* The bias is re-added to every entry of the range from its own source bytes;
   it is not accumulated as the walk goes. */
static void the_bias_applies_to_every_entry_of_the_range(void)
{
    int components[3];
    int n;

    mark_scratch_block();
    for (n = 0; n < SCRATCH_COUNT; n++) {
        stage_entry(n, 30, 30, 30);
    }

    fdps_set_palette_range(staged(), SCRATCH_FIRST,
                           SCRATCH_FIRST + SCRATCH_COUNT - 1, -10, -10, -10);

    for (n = 0; n < SCRATCH_COUNT; n++) {
        read_dac_entry(SCRATCH_FIRST + n, components);
        CHECK_EQ(components[0], 20);
        CHECK_EQ(components[1], 20);
        CHECK_EQ(components[2], 20);
    }
}

/* A fade to black is the whole reason the bias is signed and clamped: bias
   -63 puts every entry of the range at 0 whatever the source held. */
static void a_full_negative_bias_blacks_the_whole_range(void)
{
    int components[3];
    int n;

    mark_scratch_block();
    for (n = 0; n < SCRATCH_COUNT; n++) {
        stage_entry(n, 63, 32, 1);
    }

    fdps_set_palette_range(staged(), SCRATCH_FIRST,
                           SCRATCH_FIRST + SCRATCH_COUNT - 1, -63, -63, -63);

    for (n = 0; n < SCRATCH_COUNT; n++) {
        read_dac_entry(SCRATCH_FIRST + n, components);
        CHECK_EQ(components[0], 0);
        CHECK_EQ(components[1], 0);
        CHECK_EQ(components[2], 0);
    }
}

/* SHR EAX,0x10 then AND EAX,0xff at 0002ae90: the result is bits 16..23 of the
   argument, moved down to bits 0..7.  0x00123456 puts 0x12 there. */
static void the_red_channel_is_bits_sixteen_to_twenty_three(void)
{
    CHECK_EQ(fdps_get_rgb_red(0x00123456u), 0x12);
}

/* The shift moves the other two channels out below bit 0, so neither reaches
   the result however they are set. */
static void green_and_blue_do_not_reach_the_result(void)
{
    CHECK_EQ(fdps_get_rgb_red(0x0000ffffu), 0);
    CHECK_EQ(fdps_get_rgb_red(0x00ff0000u), 0xff);
}

/* AND EAX,0xff after the shift: whatever a word carries above bit 23 is masked
   off rather than added to the channel.  Without the mask 0xff345678 would come
   back as 0xff34. */
static void bits_above_twenty_three_are_masked_off(void)
{
    CHECK_EQ(fdps_get_rgb_red(0xff000000u), 0);
    CHECK_EQ(fdps_get_rgb_red(0xff345678u), 0x34);
}

/* The ends of the channel's range: an all-zero word gives 0 and a word with
   every bit of a 24-bit colour set gives 0xff. */
static void the_channel_spans_zero_to_two_hundred_and_fifty_five(void)
{
    CHECK_EQ(fdps_get_rgb_red(0x00000000u), 0);
    CHECK_EQ(fdps_get_rgb_red(0x00ffffffu), 0xff);
}

/* Each of the eight bits of the channel arrives on its own, at the position the
   shift distance puts it: bit 16 + n of the word becomes bit n of the result.
   A wrong shift distance would move the whole set. */
static void every_bit_of_the_channel_lands_where_the_shift_puts_it(void)
{
    int bit;

    for (bit = 0; bit < 8; bit++) {
        CHECK_EQ(fdps_get_rgb_red(1u << (16 + bit)), 1L << bit);
    }
}

/* The words this actually sees are built by fdps_build_palette_tables at
   0002afac: a six-bit DAC byte is widened by LEA EAX,[EAX*4+0] and packed with
   SHL EAX,0x10, so a DAC red of 63 arrives as 0x00fc0000 and comes back as 252,
   and a DAC red of 32 arrives as 0x00800000 and comes back as 128.  The low
   nibble survives -- the AND 0xf0 that keeps only the top nibble is the
   caller's, at 0002b03c, not this function's. */
static void a_dac_derived_word_yields_the_widened_component(void)
{
    CHECK_EQ(fdps_get_rgb_red((unsigned int) (63 * 4) << 16), 252);
    CHECK_EQ(fdps_get_rgb_red((unsigned int) (32 * 4) << 16), 128);
}

/* SHR EAX,0x8 then AND EAX,0xff at 0002aec0: the result is bits 8..15 of the
   argument, moved down to bits 0..7.  0x00123456 puts 0x34 there, the middle
   byte of the same word whose red is 0x12. */
static void the_green_channel_is_bits_eight_to_fifteen(void)
{
    CHECK_EQ(fdps_get_rgb_green(0x00123456u), 0x34);
    CHECK_EQ(fdps_get_rgb_red(0x00123456u), 0x12);
}

/* The mask is what separates green from red here, not the shift: SHR by 8
   leaves the red byte sitting in bits 8..15 and only AND 0xff removes it.  A
   word that is all red must give 0, and one that is all red and all green must
   give the green byte alone rather than 0xffff. */
static void the_red_byte_does_not_reach_the_green_result(void)
{
    CHECK_EQ(fdps_get_rgb_green(0x00ff0000u), 0);
    CHECK_EQ(fdps_get_rgb_green(0x00ff3400u), 0x34);
}

/* The shift moves the blue byte out below bit 0, so however blue is set it
   does not reach the result. */
static void blue_does_not_reach_the_green_result(void)
{
    CHECK_EQ(fdps_get_rgb_green(0x000000ffu), 0);
    CHECK_EQ(fdps_get_rgb_green(0x000012ffu), 0x12);
}

/* AND EAX,0xff after the shift: bits 24 and above land in bits 16..23 of the
   shifted value and are masked off.  Without the mask 0xff345678 would come
   back as 0xff3456. */
static void bits_above_twenty_three_do_not_reach_the_green_result(void)
{
    CHECK_EQ(fdps_get_rgb_green(0xff000000u), 0);
    CHECK_EQ(fdps_get_rgb_green(0xff345678u), 0x56);
}

/* The ends of the channel's range: an all-zero word gives 0 and a word with
   every bit of a 24-bit colour set gives 0xff. */
static void the_green_channel_spans_zero_to_two_hundred_and_fifty_five(void)
{
    CHECK_EQ(fdps_get_rgb_green(0x00000000u), 0);
    CHECK_EQ(fdps_get_rgb_green(0x00ffffffu), 0xff);
}

/* Bit 8 + n of the word becomes bit n of the result: a shift distance of 16
   or 0 would move the whole set. */
static void every_bit_of_the_green_channel_lands_where_the_shift_puts_it(void)
{
    int bit;

    for (bit = 0; bit < 8; bit++) {
        CHECK_EQ(fdps_get_rgb_green(1u << (8 + bit)), 1L << bit);
    }
}

/* The words this actually sees are built by fdps_build_palette_tables at
   0002afc6: the green DAC byte is widened by LEA EAX,[EAX*4+0] and packed with
   SHL EDX,0x8, so a DAC green of 63 arrives as 0x0000fc00 and comes back as
   252, and a DAC green of 32 arrives as 0x00008000 and comes back as 128.  The
   low nibble survives -- the AND 0xf0 that keeps only the top nibble is the
   caller's, at 0002b052, not this function's. */
static void a_dac_derived_word_yields_the_widened_green_component(void)
{
    CHECK_EQ(fdps_get_rgb_green((unsigned int) (63 * 4) << 8), 252);
    CHECK_EQ(fdps_get_rgb_green((unsigned int) (32 * 4) << 8), 128);
}

/* AND EAX,0xff with no shift before it at 0002aef0: the result is bits 0..7 of
   the argument, left where they already were.  0x00123456 puts 0x56 there, the
   low byte of the same word whose red is 0x12 and whose green is 0x34. */
static void the_blue_channel_is_bits_zero_to_seven(void)
{
    CHECK_EQ(fdps_get_rgb_blue(0x00123456u), 0x56);
    CHECK_EQ(fdps_get_rgb_green(0x00123456u), 0x34);
    CHECK_EQ(fdps_get_rgb_red(0x00123456u), 0x12);
}

/* There is no shift in this one, so the mask is the only thing separating blue
   from the two channels above it.  A word that is all red and all green must
   give 0, and one that also carries a blue byte must give that byte alone
   rather than 0x00ffff78. */
static void red_and_green_do_not_reach_the_blue_result(void)
{
    CHECK_EQ(fdps_get_rgb_blue(0x00ffff00u), 0);
    CHECK_EQ(fdps_get_rgb_blue(0x00ffff78u), 0x78);
}

/* Bits 24 and above are masked off by the same AND rather than assumed clear:
   without it 0xff345678 would come back whole. */
static void bits_above_twenty_three_do_not_reach_the_blue_result(void)
{
    CHECK_EQ(fdps_get_rgb_blue(0xff000000u), 0);
    CHECK_EQ(fdps_get_rgb_blue(0xff345678u), 0x78);
}

/* The ends of the channel's range: an all-zero word gives 0 and a word with
   every bit of a 24-bit colour set gives 0xff. */
static void the_blue_channel_spans_zero_to_two_hundred_and_fifty_five(void)
{
    CHECK_EQ(fdps_get_rgb_blue(0x00000000u), 0);
    CHECK_EQ(fdps_get_rgb_blue(0x00ffffffu), 0xff);
}

/* Bit n of the word becomes bit n of the result: the shift distance here is
   zero, and any shift at all would move the whole set off the channel. */
static void every_bit_of_the_blue_channel_stays_where_it_is(void)
{
    int bit;

    for (bit = 0; bit < 8; bit++) {
        CHECK_EQ(fdps_get_rgb_blue(1u << bit), 1L << bit);
    }
}

/* The words this actually sees are built by fdps_build_palette_tables at
   0002afe1: the blue DAC byte is widened by LEA EAX,[EAX*4+0] and folded in
   with OR EDX,EAX at 0002b025 -- no shift, because blue is already at the
   bottom -- so a DAC blue of 63 arrives as 0x000000fc and comes back as 252,
   and a DAC blue of 32 arrives as 0x00000080 and comes back as 128.  The low
   nibble survives; the AND 0xf0 that keeps only the top nibble is the caller's,
   at 0002b068, not this function's. */
static void a_dac_derived_word_yields_the_widened_blue_component(void)
{
    CHECK_EQ(fdps_get_rgb_blue((unsigned int) (63 * 4)), 252);
    CHECK_EQ(fdps_get_rgb_blue((unsigned int) (32 * 4)), 128);
}

/* One word packed the way 0002b00e..0002b027 packs it -- SHL EAX,0x10 for red,
   SHL EDX,0x8 for green, blue OR'd in unshifted -- must come apart into the
   three DAC bytes it was built from, each widened by four.  A word this game
   builds is the only kind any of the three extractors ever sees. */
static void a_packed_word_comes_apart_into_the_three_dac_bytes(void)
{
    unsigned int packed;

    packed = ((unsigned int) (10 * 4) << 16)
           | ((unsigned int) (20 * 4) << 8)
           | (unsigned int) (30 * 4);

    CHECK_EQ(fdps_get_rgb_red(packed), 40);
    CHECK_EQ(fdps_get_rgb_green(packed), 80);
    CHECK_EQ(fdps_get_rgb_blue(packed), 120);
}

void run_palette_tests(void)
{
    RUN_TEST(the_dac_reads_back_what_was_written);
    RUN_TEST(zero_bias_uploads_the_source_bytes_unchanged);
    RUN_TEST(the_range_is_inclusive_at_both_ends);
    RUN_TEST(a_backwards_range_uploads_nothing);
    RUN_TEST(a_single_entry_range_uploads_exactly_one);
    RUN_TEST(each_entry_advances_the_source_by_three_bytes);
    RUN_TEST(the_source_starts_at_the_pointer_not_at_the_entry);
    RUN_TEST(the_upper_clamp_holds_at_sixty_three);
    RUN_TEST(the_lower_clamp_holds_at_zero);
    RUN_TEST(the_source_byte_is_taken_unsigned);
    RUN_TEST(each_bias_moves_only_its_own_channel);
    RUN_TEST(the_bias_applies_to_every_entry_of_the_range);
    RUN_TEST(a_full_negative_bias_blacks_the_whole_range);
    RUN_TEST(the_red_channel_is_bits_sixteen_to_twenty_three);
    RUN_TEST(green_and_blue_do_not_reach_the_result);
    RUN_TEST(bits_above_twenty_three_are_masked_off);
    RUN_TEST(the_channel_spans_zero_to_two_hundred_and_fifty_five);
    RUN_TEST(every_bit_of_the_channel_lands_where_the_shift_puts_it);
    RUN_TEST(a_dac_derived_word_yields_the_widened_component);
    RUN_TEST(the_green_channel_is_bits_eight_to_fifteen);
    RUN_TEST(the_red_byte_does_not_reach_the_green_result);
    RUN_TEST(blue_does_not_reach_the_green_result);
    RUN_TEST(bits_above_twenty_three_do_not_reach_the_green_result);
    RUN_TEST(the_green_channel_spans_zero_to_two_hundred_and_fifty_five);
    RUN_TEST(every_bit_of_the_green_channel_lands_where_the_shift_puts_it);
    RUN_TEST(a_dac_derived_word_yields_the_widened_green_component);
    RUN_TEST(the_blue_channel_is_bits_zero_to_seven);
    RUN_TEST(red_and_green_do_not_reach_the_blue_result);
    RUN_TEST(bits_above_twenty_three_do_not_reach_the_blue_result);
    RUN_TEST(the_blue_channel_spans_zero_to_two_hundred_and_fifty_five);
    RUN_TEST(every_bit_of_the_blue_channel_stays_where_it_is);
    RUN_TEST(a_dac_derived_word_yields_the_widened_blue_component);
    RUN_TEST(a_packed_word_comes_apart_into_the_three_dac_bytes);
}
