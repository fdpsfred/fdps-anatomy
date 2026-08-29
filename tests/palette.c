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
}
