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
 * The packer's cases come from 0002af20 itself -- XOR EAX,EAX / MOV AL,byte ptr
 * [EBP+0x14] / SHL EAX,0x10, the same pair with SHL EAX,0x8 for green and with
 * no shift for blue, the three OR'd together -- so what they pin down is that
 * one byte per argument reaches exactly one field, zero-extended, and that the
 * word it builds is the one the extractors above take apart.
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
#include "gamedata.h"
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

/* 0002af20 puts each channel in its own field and nowhere else: SHL EAX,0x10
   after the red byte, SHL EAX,0x8 after the green one, and no shift at all for
   blue.  One channel set and the other two clear must therefore give exactly
   the one field. */
static void each_channel_lands_in_its_own_field(void)
{
    CHECK_EQ(fdps_pack_rgb(0x12, 0, 0), 0x00120000L);
    CHECK_EQ(fdps_pack_rgb(0, 0x34, 0), 0x00003400L);
    CHECK_EQ(fdps_pack_rgb(0, 0, 0x56), 0x00000056L);
}

/* The three fields are OR'd together, so three distinct bytes must appear side
   by side in the 0x00RRGGBB order the extractors read them back out of, and
   neither of the two ORs may disturb a field already in place. */
static void the_three_channels_sit_side_by_side(void)
{
    CHECK_EQ(fdps_pack_rgb(0x12, 0x34, 0x56), 0x00123456L);
}

/* The ends of the range.  All three channels at 0xff give exactly 0x00ffffff:
   the top byte stays clear because only eight bits of each argument are ever
   loaded, and there is nothing in the body that could set a bit above 23. */
static void the_word_never_carries_a_bit_above_twenty_three(void)
{
    CHECK_EQ(fdps_pack_rgb(0, 0, 0), 0L);
    CHECK_EQ(fdps_pack_rgb(0xff, 0xff, 0xff), 0x00ffffffL);
}

/* Each argument is fetched as MOV AL out of a zeroed EAX -- one byte out of the
   four-byte slot -- so a value carrying bit 8 loses it rather than pushing it
   into the channel above.  0x1ff as red packs as 0x00ff0000 and not 0x01ff0000;
   0x100 as green contributes nothing at all. */
static void only_the_low_byte_of_each_argument_contributes(void)
{
    int oversized_red;
    int oversized_green;

    oversized_red = 0x1ff;
    oversized_green = 0x100;

    CHECK_EQ(fdps_pack_rgb((unsigned char) oversized_red, 0, 0), 0x00ff0000L);
    CHECK_EQ(fdps_pack_rgb(0, (unsigned char) oversized_green, 0), 0L);
}

/* The byte load is a zero extension and not MOVSX: a channel of 0x80 belongs in
   its own field as 0x80, where a sign extension would smear 0xff across the
   channels above it.  Red 0x80 alone is 0x00800000, not 0xff800000, and green
   0x80 alone is 0x00008000, not 0x00ffff80. */
static void a_channel_of_eighty_hex_is_not_sign_extended(void)
{
    CHECK_EQ(fdps_pack_rgb(0x80, 0, 0), 0x00800000L);
    CHECK_EQ(fdps_pack_rgb(0, 0x80, 0), 0x00008000L);
    CHECK_EQ(fdps_pack_rgb(0, 0, 0x80), 0x00000080L);
}

/* The packing this performs is the one fdps_build_palette_tables open-codes at
   0002b00e..0002b027, so a word built here out of DAC bytes widened by four
   must be the same word that code builds: DAC 10, 20, 30 widen to 40, 80, 120
   and land as 0x00285078. */
static void a_dac_derived_word_matches_the_one_the_builder_packs(void)
{
    CHECK_EQ(fdps_pack_rgb((unsigned char) (10 * 4),
                           (unsigned char) (20 * 4),
                           (unsigned char) (30 * 4)),
             0x00285078L);
    CHECK_EQ(fdps_pack_rgb((unsigned char) (63 * 4),
                           (unsigned char) (63 * 4),
                           (unsigned char) (63 * 4)),
             0x00fcfcfcL);
}

/* The three extractors in this same file take apart what this builds, so a word
   packed here must come back out channel for channel.  This is the property the
   two halves of the pair exist for; the shift distances are read off both sides
   of the assembly independently. */
static void the_extractors_undo_the_packing(void)
{
    unsigned int packed;

    packed = fdps_pack_rgb(0x12, 0x34, 0x56);

    CHECK_EQ(fdps_get_rgb_red(packed), 0x12);
    CHECK_EQ(fdps_get_rgb_green(packed), 0x34);
    CHECK_EQ(fdps_get_rgb_blue(packed), 0x56);
}

/* Every bit of every channel reaches the field the shift puts it in, and none
   reaches any other: bit n of red becomes bit n+16, bit n of green becomes bit
   n+8, and bit n of blue stays where it is. */
static void every_bit_of_a_channel_lands_where_its_shift_puts_it(void)
{
    int bit;

    for (bit = 0; bit < 8; bit++) {
        CHECK_EQ(fdps_pack_rgb((unsigned char) (1u << bit), 0, 0),
                 1L << (bit + 16));
        CHECK_EQ(fdps_pack_rgb(0, (unsigned char) (1u << bit), 0),
                 1L << (bit + 8));
        CHECK_EQ(fdps_pack_rgb(0, 0, (unsigned char) (1u << bit)),
                 1L << bit);
    }
}

/* WHAT THE NEAREST-COLOUR CASES BELOW ARE READ OFF.  0002b1b0 seeds
   [EBP-0x20] with 0x989680 (10000000) and [EBP-0x1c] with 0x100, walks a byte
   cursor with three MOV AL,byte ptr [EAX] / AND EAX,0xff / SUB pairs and three
   INCs per iteration, forms IMUL of each difference by itself and adds them,
   and takes the entry only on JGE falling through -- a strict less-than.  The
   loop bound is the literal 0x100 in CMP dword ptr [EBP-0x24],0x100 / JL, not
   anything the caller passes.  Every expected value below is computed from
   that arithmetic by hand, never read off the emitted C.

   The palette is staged as 257 entries so the entry one past the end of the
   scan can be loaded with the only exact match in the block: a walk that ran
   257 times would return 256 and a walk that stops at 256 returns 255. */
#define NEAR_ENTRIES 257

static unsigned char near_stage[NEAR_ENTRIES * 3];

static void near_entry(int n, int red, int green, int blue)
{
    near_stage[n * 3] = (unsigned char) red;
    near_stage[n * 3 + 1] = (unsigned char) green;
    near_stage[n * 3 + 2] = (unsigned char) blue;
}

/* Loads every staged entry, the one past the end included, with one colour, so
   a test only has to say which entries differ from the background. */
static void near_fill(int red, int green, int blue)
{
    int n;

    for (n = 0; n < NEAR_ENTRIES; n++) {
        near_entry(n, red, green, blue);
    }
}

/* Loads entry n with (n, n, n): 256 distinct colours along the grey axis, so
   the distance to a target (t, t, t) is 3 * (t - n)^2 and the answer for an
   exact hit is the index itself. */
static void near_stage_grey_ramp(void)
{
    int n;

    for (n = 0; n < NEAR_ENTRIES; n++) {
        near_entry(n, n & 0xff, n & 0xff, n & 0xff);
    }
}

/* Distance zero cannot be beaten and the seed cannot survive it, so a target
   that is exactly one entry's colour comes back as that entry's index.  Index
   0 as well as a middling one: the seeded best index is 0x100 and the seeded
   distance is huge, so entry 0 has to be able to win on the first iteration. */
static void an_exact_match_returns_its_own_index(void)
{
    near_stage_grey_ramp();

    CHECK_EQ(fdps_palette_find_nearest_color(7, 7, 7, near_stage), 7);
    CHECK_EQ(fdps_palette_find_nearest_color(0, 0, 0, near_stage), 0);
    CHECK_EQ(fdps_palette_find_nearest_color(255, 255, 255, near_stage), 255);
}

/* On the grey ramp a target of (100, 101, 102) is (100-n)^2 + (101-n)^2 +
   (102-n)^2 away from entry n: 5 at n = 100, 2 at n = 101 and 5 at n = 102.
   The middle one wins, so the result is not simply the first channel's own
   value. */
static void the_closest_entry_wins_when_nothing_matches_exactly(void)
{
    near_stage_grey_ramp();

    CHECK_EQ(fdps_palette_find_nearest_color(100, 101, 102, near_stage), 101);
}

/* AND EAX,0xff after MOV AL: a palette byte of 200 is 200, not -56.  On the
   grey ramp a target of (200, 200, 200) therefore hits entry 200 exactly.
   Under sign extension every entry from 128 up would go negative, entry 200
   would sit 256 away in each channel, and the nearest entry would be 127. */
static void the_palette_byte_is_taken_unsigned(void)
{
    near_stage_grey_ramp();

    CHECK_EQ(fdps_palette_find_nearest_color(200, 200, 200, near_stage), 200);
    CHECK_EQ(fdps_palette_find_nearest_color(255, 200, 200, near_stage), 218);
}

/* JGE skips the update, so the comparison is < and not <=: with two entries at
   distance 0 the earlier one is kept and the later one does not displace it. */
static void ties_go_to_the_lowest_index(void)
{
    near_fill(0, 0, 0);
    near_entry(5, 10, 10, 10);
    near_entry(9, 10, 10, 10);

    CHECK_EQ(fdps_palette_find_nearest_color(10, 10, 10, near_stage), 5);
}

/* The seed is the literal 0x989680 and the test against it is strict.  With
   every entry black, a target of (3000, 1000, 0) is 3000^2 + 1000^2 =
   10000000 away from all 256 of them -- equal to the seed, so no entry is ever
   taken and the seeded index 0x100 comes back.  One less in the green channel
   makes it 9998001, which is under the seed, and then the first entry wins and
   the 255 equal ones behind it do not displace it. */
static void a_distance_equal_to_the_seed_is_not_taken(void)
{
    near_fill(0, 0, 0);

    CHECK_EQ(fdps_palette_find_nearest_color(3000, 1000, 0, near_stage), 256);
    CHECK_EQ(fdps_palette_find_nearest_color(3000, 999, 0, near_stage), 0);
}

/* CMP dword ptr [EBP-0x24],0x100 / JL: entry 255 is scanned and entry 256 is
   not.  The background is 67700 away from the target, entry 255 is 1 away and
   entry 256 -- one past the end of the scan -- is an exact match.  Reading one
   entry too many would return 256; stopping where the original stops returns
   255.  With entry 255 back at the background colour the only exact match in
   the buffer is the one at 256, and the answer becomes the first background
   entry rather than 256. */
static void the_scan_covers_the_last_entry_and_no_further(void)
{
    near_fill(200, 200, 200);
    near_entry(255, 40, 50, 61);
    near_entry(256, 40, 50, 60);

    CHECK_EQ(fdps_palette_find_nearest_color(40, 50, 60, near_stage), 255);

    near_entry(255, 200, 200, 200);

    CHECK_EQ(fdps_palette_find_nearest_color(40, 50, 60, near_stage), 0);
}

/* The three IMULs are the metric, not a scaling of one: by squares an entry
   10 off in every channel is 300 away and one 18 off in a single channel is
   324 away, so the first wins; by sums of absolute differences it would be 30
   against 18 and the second would win.  The two metrics disagree here, and the
   answer says which one the function computes. */
static void the_metric_is_squared_distance_not_absolute_difference(void)
{
    near_fill(200, 200, 200);
    near_entry(3, 10, 10, 10);
    near_entry(7, 18, 0, 0);

    CHECK_EQ(fdps_palette_find_nearest_color(0, 0, 0, near_stage), 3);
}

/* A palette entry brighter than the target gives a negative difference, and
   IMUL of it by itself is positive: entry 4 is 4 below the target and entry 8
   is 4 above it, both 16 away, so the tie goes to 4.  Entry 12 is 3 above the
   target, 9 away, and beats both -- which it could not do if a negative
   difference were compared as a signed quantity instead of being squared. */
static void a_negative_difference_counts_as_its_square(void)
{
    near_fill(200, 200, 200);
    near_entry(4, 6, 0, 0);
    near_entry(8, 14, 0, 0);

    CHECK_EQ(fdps_palette_find_nearest_color(10, 0, 0, near_stage), 4);

    near_entry(12, 13, 0, 0);

    CHECK_EQ(fdps_palette_find_nearest_color(10, 0, 0, near_stage), 12);
}

/* The target components are plain signed ints -- nothing masks them, and the
   SUB is signed -- so a negative target stays negative and lands nearest to
   black.  Taken as unsigned it would be an enormous quantity and the answer
   would be the brightest entry on the ramp instead of the darkest. */
static void the_target_components_are_signed(void)
{
    near_stage_grey_ramp();

    CHECK_EQ(fdps_palette_find_nearest_color(-10, -10, -10, near_stage), 0);
}

/* fdps_build_palette_tables, 0002af60.
 *
 * Expected values come from the assembly of the builder and of the two
 * functions it leans on, never from running the emitted C:
 *
 *   The widening is LEA EAX,[EAX*4+0] on each of the three palette bytes, so a
 *   six-bit 63 becomes 252 and a 17 becomes 68.  The packing keeps only the low
 *   byte of each widened value -- MOV AL,byte ptr -- and the three masks are
 *   AND EAX,0xf0, so what survives is the TOP nibble of each widened byte: 252
 *   gives 0xf, 68 gives 0x4, 136 gives 0x8, 204 gives 0xc.
 *
 *   Those nibbles go to bits 16..19 (SHL 0xc after the mask), 8..11 (SHL 0x4)
 *   and 0..3 (SAR 0x4), so a whole entry lands as 0x000R0G0B.  Row 0 of the
 *   ramp is stored 0 outright, at 0002afa2, before any of that runs.
 *
 *   The multipliers are the 18 dwords at 0002ae40 the prologue copies into the
 *   frame: 0, 1, 2, 3, 4, 5, 6, 7, 8, 16, 15, 14, 13, 12, 11, 10, 9, 8.  Rows
 *   2..17 are row 1 times that row's entry, IMUL at 0002b0eb, so row 8 and row
 *   17 are the same table.
 *
 *   The cube is filled by three nested loops whose counters reach
 *   fdps_palette_find_nearest_color as (middle, outer, inner) = (red, green,
 *   blue), against a write index that only the innermost loop increments.  The
 *   cell for a quantised (r, g, b) is therefore at green * 256 + red * 16 +
 *   blue, and the assertions below are what would fail if any pair of the three
 *   axes were swapped.
 *
 * Both tables are poisoned before every build, so "written" is an assertion
 * here and not an accident of the zero-filled stub the link supplies until
 * ticket 23 defines them.
 */
#define BUILD_ENTRIES 256
#define RAMP_ROWS 18
#define RAMP_WORDS (RAMP_ROWS * BUILD_ENTRIES)
#define CUBE_CELLS 4096

/* Neither poison can be a legitimate result.  A ramp word never has a bit
   above 23 set, and 0xff is never a winning palette index for the staged
   palette below: its entry 255 is one of many identical whites and ties go to
   the lowest index. */
#define RAMP_POISON 0xdeadbeefu
#define CUBE_POISON 0xff

#define RAMP_AT(row, entry) ((row) * BUILD_ENTRIES + (entry))

/* Staged as plain bytes and handed over through a cast, for the reason the
   preamble gives: the builder reaching entry n at 3n is the thing under test,
   and an array of records would make the step whatever the record is. */
static unsigned char build_stage[BUILD_ENTRIES * 3];
static int build_stage_ready = 0;

static struct fdps_palette_entry *build_staged(void)
{
    return (struct fdps_palette_entry *) build_stage;
}

static void build_entry(int n, int red, int green, int blue)
{
    build_stage[n * 3] = (unsigned char) red;
    build_stage[n * 3 + 1] = (unsigned char) green;
    build_stage[n * 3 + 2] = (unsigned char) blue;
}

static void poison_tables(void)
{
    int i;

    for (i = 0; i < RAMP_WORDS; i++) {
        data_fdps_palette_shade_ramp_table[i] = RAMP_POISON;
    }
    for (i = 0; i < CUBE_CELLS; i++) {
        data_fdps_inverse_palette_cube[i] = CUBE_POISON;
    }
}

/* Entries 0..6 are markers chosen so that every field of a ramp word and every
   axis of the cube can be told apart; 7..255 are the same white as entry 4, so
   the brightest corner of the cube has a tie that entry 4 wins. */
static void stage_marker_palette(void)
{
    int i;

    build_entry(0, 0, 0, 0);
    build_entry(1, 63, 0, 0);
    build_entry(2, 0, 63, 0);
    build_entry(3, 0, 0, 63);
    build_entry(4, 63, 63, 63);
    build_entry(5, 17, 34, 51);
    build_entry(6, 100, 0, 0);
    for (i = 7; i < BUILD_ENTRIES; i++) {
        build_entry(i, 63, 63, 63);
    }
}

/* One build serves every case below it, because a build is 4096 nearest-colour
   searches over 256 entries each and there is nothing to learn from repeating
   it.  The one case that needs a second build says so and clears the flag. */
static void built_from_marker_palette(void)
{
    if (build_stage_ready) {
        return;
    }
    stage_marker_palette();
    poison_tables();
    fdps_build_palette_tables(build_staged());
    build_stage_ready = 1;
}

/* Row 1 is the palette itself.  Entry 0 is black, entries 1, 2 and 3 put a
   full channel in exactly one field each -- which is what says red reaches bits
   16..19, green bits 8..11 and blue bits 0..3 -- and entry 4 is all three at
   once. */
static void ramp_row_one_holds_the_packed_palette(void)
{
    built_from_marker_palette();

    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(1, 0)], 0x00000000L);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(1, 1)], 0x000f0000L);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(1, 2)], 0x00000f00L);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(1, 3)], 0x0000000fL);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(1, 4)], 0x000f0f0fL);
}

/* Entry 5 is (17, 34, 51), which widens to (68, 136, 204) = (0x44, 0x88,
   0xcc).  The mask keeps the top nibble of each, so the word is 0x0004080c:
   three different nibbles in three different fields, which no permutation of
   the channels would reproduce. */
static void each_channel_keeps_the_top_nibble_of_its_widened_byte(void)
{
    built_from_marker_palette();

    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(1, 5)], 0x0004080cL);
}

/* Entry 6 has a red of 100, past the six bits a DAC component really has.
   Nothing rejects or clamps it: 100 * 4 is 400, whose low byte is 0x90, whose
   top nibble is 9. */
static void a_component_above_the_dac_range_still_widens_by_four(void)
{
    built_from_marker_palette();

    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(1, 6)], 0x00090000L);
}

/* Row 0 is stored zero regardless of the palette -- it is the weight-nothing
   row -- and the poison is what makes that an assertion rather than a reading
   of the zero-filled stub. */
static void ramp_row_zero_is_forced_to_zero(void)
{
    built_from_marker_palette();

    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(0, 0)], 0L);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(0, 4)], 0L);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(0, 255)], 0L);
}

/* Rows 2..8 are row 1 times the row number.  0x000f0f0f * 2 is 0x001e1e1e and
   * 8 is 0x00787878; entry 5's 0x0004080c * 3 is 0x000c1824, where each
   channel has grown past its nibble but stayed inside its own byte. */
static void the_scaled_rows_are_row_one_times_the_level(void)
{
    built_from_marker_palette();

    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(2, 4)], 0x001e1e1eL);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(3, 5)], 0x000c1824L);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(8, 4)], 0x00787878L);
}

/* The multiplier table is not a ramp: it climbs 2..8 and then jumps to 16 and
   walks back down to 8, so row 9 is the heaviest row and row 17 repeats row 8.
   A table read as a straight 0..17 would give row 9 nine times row 1 and row
   17 seventeen times it, and both of these would fail. */
static void the_multiplier_table_turns_round_after_row_eight(void)
{
    built_from_marker_palette();

    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(9, 4)], 0x00f0f0f0L);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(10, 4)], 0x00e1e1e1L);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(17, 4)], 0x00787878L);
}

/* The heaviest row against the brightest entry is the worst case for the whole
   nibble layout: 0xf * 16 is 0xf0, which is the largest a channel can reach
   and still be inside its own byte.  Entry 5 at the same row shows the same
   thing for three unequal channels. */
static void no_channel_carries_into_the_one_above_it(void)
{
    built_from_marker_palette();

    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(9, 4)], 0x00f0f0f0L);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(9, 5)], 0x004080c0L);
}

/* Eighteen rows of 256, and every one of them written.  This is also what says
   the two absolute bases in the original are one array: row 1 is reached
   through 0x657f0 and every other row through 0x653f0, and a build that left
   them apart would leave 256 words of poison behind. */
static void every_word_of_all_eighteen_rows_is_written(void)
{
    int i;
    int left = 0;

    built_from_marker_palette();

    for (i = 0; i < RAMP_WORDS; i++) {
        if (data_fdps_palette_shade_ramp_table[i] == RAMP_POISON) {
            left++;
        }
    }
    CHECK_EQ(left, 0);
}

/* The three axes, one at a time.  Entry 1 is the only strong red, entry 2 the
   only strong green and entry 3 the only strong blue, so the cell that holds
   each of those indices says which loop carries which channel: red is the
   middle loop (stride 16), green the outer one (stride 256) and blue the
   innermost (stride 1). */
static void the_cube_axis_order_is_green_red_blue(void)
{
    built_from_marker_palette();

    CHECK_EQ(data_fdps_inverse_palette_cube[0], 0);
    CHECK_EQ(data_fdps_inverse_palette_cube[15 * 16], 1);
    CHECK_EQ(data_fdps_inverse_palette_cube[15 * 256], 2);
    CHECK_EQ(data_fdps_inverse_palette_cube[15], 3);
}

/* A cell away from the axes, checked against the search itself so that the
   argument order and the index formula are pinned together: the target for
   cell (r, g, b) is (r * 4, g * 4, b * 4) and nothing else.  The bright corner
   is an absolute value -- every entry from 4 up is the same white, 27 away,
   and the strict less-than gives it to the first of them. */
static void a_cube_cell_is_the_nearest_entry_to_its_quantised_colour(void)
{
    built_from_marker_palette();

    CHECK_EQ(data_fdps_inverse_palette_cube[9 * 256 + 5 * 16 + 2],
             fdps_palette_find_nearest_color(5 * 4, 9 * 4, 2 * 4,
                                             build_stage));
    CHECK_EQ(data_fdps_inverse_palette_cube[CUBE_CELLS - 1], 4);
}

/* All 4096 cells, and no more of the array than that. */
static void every_cell_of_the_cube_is_written(void)
{
    int i;
    int left = 0;

    built_from_marker_palette();

    for (i = 0; i < CUBE_CELLS; i++) {
        if (data_fdps_inverse_palette_cube[i] == CUBE_POISON) {
            left++;
        }
    }
    CHECK_EQ(left, 0);
}

/* Nothing is accumulated or merged: a second build with a different palette
   replaces both tables outright.  The second palette is black everywhere but
   entry 10, so the word that was 0x000f0000 becomes 0, the white corner of the
   cube moves from entry 4 to entry 10, and the black corner stays at 0. */
static void a_second_build_replaces_both_tables(void)
{
    int i;

    built_from_marker_palette();

    for (i = 0; i < BUILD_ENTRIES; i++) {
        build_entry(i, 0, 0, 0);
    }
    build_entry(10, 63, 63, 63);
    fdps_build_palette_tables(build_staged());

    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(1, 1)], 0L);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(1, 10)], 0x000f0f0fL);
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_AT(9, 10)], 0x00f0f0f0L);
    CHECK_EQ(data_fdps_inverse_palette_cube[CUBE_CELLS - 1], 10);
    CHECK_EQ(data_fdps_inverse_palette_cube[0], 0);

    build_stage_ready = 0;
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
    RUN_TEST(each_channel_lands_in_its_own_field);
    RUN_TEST(the_three_channels_sit_side_by_side);
    RUN_TEST(the_word_never_carries_a_bit_above_twenty_three);
    RUN_TEST(only_the_low_byte_of_each_argument_contributes);
    RUN_TEST(a_channel_of_eighty_hex_is_not_sign_extended);
    RUN_TEST(a_dac_derived_word_matches_the_one_the_builder_packs);
    RUN_TEST(the_extractors_undo_the_packing);
    RUN_TEST(every_bit_of_a_channel_lands_where_its_shift_puts_it);
    RUN_TEST(an_exact_match_returns_its_own_index);
    RUN_TEST(the_closest_entry_wins_when_nothing_matches_exactly);
    RUN_TEST(the_palette_byte_is_taken_unsigned);
    RUN_TEST(ties_go_to_the_lowest_index);
    RUN_TEST(a_distance_equal_to_the_seed_is_not_taken);
    RUN_TEST(the_scan_covers_the_last_entry_and_no_further);
    RUN_TEST(the_metric_is_squared_distance_not_absolute_difference);
    RUN_TEST(a_negative_difference_counts_as_its_square);
    RUN_TEST(the_target_components_are_signed);
    RUN_TEST(ramp_row_one_holds_the_packed_palette);
    RUN_TEST(each_channel_keeps_the_top_nibble_of_its_widened_byte);
    RUN_TEST(a_component_above_the_dac_range_still_widens_by_four);
    RUN_TEST(ramp_row_zero_is_forced_to_zero);
    RUN_TEST(the_scaled_rows_are_row_one_times_the_level);
    RUN_TEST(the_multiplier_table_turns_round_after_row_eight);
    RUN_TEST(no_channel_carries_into_the_one_above_it);
    RUN_TEST(every_word_of_all_eighteen_rows_is_written);
    RUN_TEST(the_cube_axis_order_is_green_red_blue);
    RUN_TEST(a_cube_cell_is_the_nearest_entry_to_its_quantised_colour);
    RUN_TEST(every_cell_of_the_cube_is_written);
    RUN_TEST(a_second_build_replaces_both_tables);
}
