/* tests/palcycle.c -- cover for src/palcycle.c.
 *
 * Expected values come from the assembly at 00015be0 -- the three REP MOVSD
 * copies of 24 bytes from 00014740, 00014758 and 00014770, the CMP EAX,dword
 * ptr [00069d64] / JZ guard, the CMP dword ptr [EBP-0x4],0x8 / JL loop bound,
 * the PUSH 0x3c8 then three PUSH 0x3c9 per iteration, and the DEC / CMP -1 /
 * MOV 0xf wrap -- and from those three blocks read out of the image byte for
 * byte.  None of them is read off the emitted C.
 *
 * WHAT THE COLOUR WRITES ARE OBSERVED THROUGH.  This function's visible output
 * is eight DAC entries, and the VGA DAC is readable: entry number to 0x3c7,
 * then three reads of 0x3c9 give back red, green and blue.  So the window
 * arithmetic is checked against the hardware the function actually wrote to
 * rather than against a mock.  The first test establishes that premise -- a
 * DAC that did not read back would make every later assertion here
 * meaningless rather than false -- on an entry the console does not use.
 *
 * The two globals the function owns are stubs until ticket 23, so no test
 * here reads one without having set it first; every expected value is a
 * transition from a value this file wrote, never a starting value.
 *
 * The retrace wait is not asserted.  It has no observable other than the time
 * it takes, and a test that timed it would be measuring the emulator's
 * cycle setting.  That it is there at all, and ahead of the tick guard, is a
 * playtest contract (rebuild_info/pitfalls.md), not a unit-test one.
 */
#include <conio.h>
#include "testharn.h"
#include "gamedata.h"
#include "palcycle.h"

#define VGA_DAC_READ_INDEX 0x3c7
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9

#define UI_PALETTE_FIRST_DAC_ENTRY 8
#define UI_PALETTE_ENTRY_COUNT 8

/* A DAC entry well above anything a text-mode console displays, used only to
   prove that a write to the DAC comes back out of it. */
#define SCRATCH_DAC_ENTRY 250

/* The three ramps as they sit in the image at 00014740, 00014758 and 00014770.
   They are duplicated here on purpose: the point of the window tests is that
   the emitted arrays hold these bytes, so taking them from the emitted C would
   only prove it equals itself. */
static unsigned char image_red_ramp[24] = {
    14, 11,  8,  5,  3,  2,  0,  0,
     0,  0,  3,  3,  5,  8, 11, 14,
    14, 11,  8,  5,  3,  2,  0,  0
};
static unsigned char image_green_ramp[24] = {
    42, 37, 33, 30, 26, 21, 18, 14,
    14, 18, 21, 26, 30, 33, 37, 42,
    42, 37, 33, 30, 26, 21, 18, 14
};
static unsigned char image_blue_ramp[24] = {
    26, 21, 16, 12,  8,  4,  2,  0,
     0,  2,  4,  8, 12, 16, 21, 26,
    26, 21, 16, 12,  8,  4,  2,  0
};

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
    components[0] = (int)(inp(VGA_DAC_DATA) & 0x3f);
    components[1] = (int)(inp(VGA_DAC_DATA) & 0x3f);
    components[2] = (int)(inp(VGA_DAC_DATA) & 0x3f);
}

/* Puts the animation in a state where the next call is guaranteed to service a
   frame: the latch differs from the counter, and the phase is the caller's. */
static void arm_a_new_tick(int phase, unsigned int tick)
{
    data_fdps_ui_palette_cycle_phase = phase;
    data_fdps_timer_tick_counter = tick;
    data_fdps_ui_palette_last_cycle_tick = tick + 1;
}

static void the_dac_reads_back_what_was_written(void)
{
    int components[3];

    write_dac_entry(SCRATCH_DAC_ENTRY, 13, 29, 47);
    read_dac_entry(SCRATCH_DAC_ENTRY, components);
    CHECK_EQ(components[0], 13);
    CHECK_EQ(components[1], 29);
    CHECK_EQ(components[2], 47);
}

/* CMP EAX,dword ptr [00069d64] / JZ 00015ceb jumps straight to the epilogue,
   so an already-serviced tick changes neither global nor any DAC entry. */
static void an_already_serviced_tick_changes_nothing(void)
{
    int components[3];

    write_dac_entry(UI_PALETTE_FIRST_DAC_ENTRY, 1, 2, 3);
    data_fdps_ui_palette_cycle_phase = 7;
    data_fdps_timer_tick_counter = 1234;
    data_fdps_ui_palette_last_cycle_tick = 1234;

    fdps_cycle_ui_palette();

    CHECK_EQ(data_fdps_ui_palette_cycle_phase, 7);
    CHECK_EQ(data_fdps_ui_palette_last_cycle_tick, 1234);
    read_dac_entry(UI_PALETTE_FIRST_DAC_ENTRY, components);
    CHECK_EQ(components[0], 1);
    CHECK_EQ(components[1], 2);
    CHECK_EQ(components[2], 3);
}

/* MOV EAX,[00069d64] / MOV [00063fbc],EAX latches the counter, and the DEC at
   00015cd2 steps the phase down by one. */
static void a_new_tick_latches_the_counter_and_steps_the_phase(void)
{
    arm_a_new_tick(9, 5);

    fdps_cycle_ui_palette();

    CHECK_EQ(data_fdps_ui_palette_last_cycle_tick, 5);
    CHECK_EQ(data_fdps_ui_palette_cycle_phase, 8);
}

/* The guard is equality, not order: a counter below the latch still services
   the frame, which is what makes a wrapped tick counter harmless. */
static void a_counter_below_the_latch_still_services_the_frame(void)
{
    data_fdps_ui_palette_cycle_phase = 4;
    data_fdps_timer_tick_counter = 10;
    data_fdps_ui_palette_last_cycle_tick = 4000000000u;

    fdps_cycle_ui_palette();

    CHECK_EQ(data_fdps_ui_palette_last_cycle_tick, 10);
    CHECK_EQ(data_fdps_ui_palette_cycle_phase, 3);
}

/* CMP dword ptr [00060014],-0x1 / JNZ: phase 1 goes to 0 and stays there, so
   0 is a phase the animation really visits and not a wrap boundary. */
static void phase_one_steps_to_zero_without_wrapping(void)
{
    arm_a_new_tick(1, 77);

    fdps_cycle_ui_palette();

    CHECK_EQ(data_fdps_ui_palette_cycle_phase, 0);
}

/* MOV dword ptr [00060014],0xf: the step off 0 goes to 15, not to 0 or 16. */
static void phase_zero_wraps_to_fifteen(void)
{
    arm_a_new_tick(0, 78);

    fdps_cycle_ui_palette();

    CHECK_EQ(data_fdps_ui_palette_cycle_phase, 15);
}

/* The eight entries are 8..15 in order -- PUSH EAX after ADD EAX,0x8 with the
   loop counter -- and entry n gets ramp[phase + n] from each of the three
   ramps.  Phase 3 is an ordinary phase inside the first 16 bytes. */
static void the_window_starts_at_the_phase(void)
{
    int components[3];
    int entry;

    arm_a_new_tick(3, 91);

    fdps_cycle_ui_palette();

    for (entry = 0; entry < UI_PALETTE_ENTRY_COUNT; entry++) {
        read_dac_entry(UI_PALETTE_FIRST_DAC_ENTRY + entry, components);
        CHECK_EQ(components[0], image_red_ramp[3 + entry]);
        CHECK_EQ(components[1], image_green_ramp[3 + entry]);
        CHECK_EQ(components[2], image_blue_ramp[3 + entry]);
    }
}

/* Phase 15 is the highest the phase reaches, and it reads indices 15..22 --
   past the 16 steps of the cycle and into the repeated tail.  This is the
   assertion that a 16-byte ramp would fail. */
static void the_top_phase_reads_into_the_repeated_tail(void)
{
    int components[3];
    int entry;

    arm_a_new_tick(15, 92);

    fdps_cycle_ui_palette();

    for (entry = 0; entry < UI_PALETTE_ENTRY_COUNT; entry++) {
        read_dac_entry(UI_PALETTE_FIRST_DAC_ENTRY + entry, components);
        CHECK_EQ(components[0], image_red_ramp[15 + entry]);
        CHECK_EQ(components[1], image_green_ramp[15 + entry]);
        CHECK_EQ(components[2], image_blue_ramp[15 + entry]);
    }
}

/* The loop bound is JL 8, so entry 15 is written and entry 16 is not.  A
   marker below the block and one above it both have to survive. */
static void only_entries_eight_to_fifteen_are_written(void)
{
    int components[3];

    write_dac_entry(UI_PALETTE_FIRST_DAC_ENTRY - 1, 5, 6, 7);
    write_dac_entry(UI_PALETTE_FIRST_DAC_ENTRY + UI_PALETTE_ENTRY_COUNT,
                    9, 10, 11);
    arm_a_new_tick(6, 93);

    fdps_cycle_ui_palette();

    read_dac_entry(UI_PALETTE_FIRST_DAC_ENTRY - 1, components);
    CHECK_EQ(components[0], 5);
    CHECK_EQ(components[1], 6);
    CHECK_EQ(components[2], 7);
    read_dac_entry(UI_PALETTE_FIRST_DAC_ENTRY + UI_PALETTE_ENTRY_COUNT,
                   components);
    CHECK_EQ(components[0], 9);
    CHECK_EQ(components[1], 10);
    CHECK_EQ(components[2], 11);

    /* And the last entry of the block did get the window's last step. */
    read_dac_entry(UI_PALETTE_FIRST_DAC_ENTRY + UI_PALETTE_ENTRY_COUNT - 1,
                   components);
    CHECK_EQ(components[0], image_red_ramp[6 + 7]);
    CHECK_EQ(components[1], image_green_ramp[6 + 7]);
    CHECK_EQ(components[2], image_blue_ramp[6 + 7]);
}

void run_palcycle_tests(void)
{
    RUN_TEST(the_dac_reads_back_what_was_written);
    RUN_TEST(an_already_serviced_tick_changes_nothing);
    RUN_TEST(a_new_tick_latches_the_counter_and_steps_the_phase);
    RUN_TEST(a_counter_below_the_latch_still_services_the_frame);
    RUN_TEST(phase_one_steps_to_zero_without_wrapping);
    RUN_TEST(phase_zero_wraps_to_fifteen);
    RUN_TEST(the_window_starts_at_the_phase);
    RUN_TEST(the_top_phase_reads_into_the_repeated_tail);
    RUN_TEST(only_entries_eight_to_fifteen_are_written);
}
