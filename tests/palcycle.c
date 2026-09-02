/* tests/palcycle.c -- cover for src/palcycle.c.
 *
 * Two functions live here, and so do two sets of cases: the UI cycle at
 * 00015be0 first, then the scene cycle at 0002eab0 below the divider.  What
 * the two have in common is how their colours are observed, described next;
 * the scene half's own sources are listed at that divider.
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
 * The four globals this file's source owns are stubs until ticket 23, so no
 * test here reads one without having set it first; every expected value is a
 * transition from a value this file wrote, never a starting value.  The same
 * goes for data_fdps_chapter_current_chapter_id, which gamedata.c will own.
 *
 * The retrace wait is not asserted.  It has no observable other than the time
 * it takes, and a test that timed it would be measuring the emulator's
 * cycle setting.  That it is there at all, and ahead of the tick guard, is a
 * playtest contract (rebuild_info/pitfalls.md), not a unit-test one.
 */
#include <conio.h>
#include "testharn.h"
#include "gamedata.h"
#include "cdaudio.h"
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

/* ---------------------------------------------------------------------------
   fdps_cycle_scene_palette at 0002eab0.

   Expected values come from the assembly: the moduli that go into EBX before
   each IDIV (0x14, 0x3c, 0x24, 0x1c, 0x2c, 0x28), the DAC start indices that
   are PUSHed as the first argument (0xf0, 0xf5, 0xf6, 0xf8), the colour counts
   PUSHed as the fifth (5, 7, 9, 10, 11, 15), the SAR EAX,0x2 that turns the
   phase into the window start, the CMP EAX,dword ptr [00069d64] / JZ 0002f22b
   guard, and the four ramp groups read out of 0002b2a0-0002b4e9 byte for byte.
   The chapter indices are the switch's own case values, 0-based; the chapter
   numbers in the test names are those plus one (chapters/_index.md).

   The colours are observed through the DAC, as the UI cycle's tests are: the
   callee fdps_set_palette_range_on_retrace is real emitted code in
   src/palette.c, so a call really does reach the hardware and can be read
   back.  Nothing here mocks it.

   fdps_cd_music_repeat_poll is called before the tick guard on every call, and
   every setup below leaves its own latch equal to the tick counter so that it
   returns immediately.  Without that a run of these tests would eventually
   walk its 0x4b-tick throttle up to a real MSCDEX device request, which is a
   different module's behaviour and has no business in this file's result.

   The ramps below are duplicated out of the image on purpose, for the same
   reason the UI cycle's are: taking them from the emitted C would only prove
   it equals itself.  Only the four groups these tests read colours back from
   are here -- chapter indices 0/26, 8, 21 and 28. */

static unsigned char image_ch00_26_red_ramp[9] = {
    59, 55, 49, 51, 55, 59, 55, 49, 51
};
static unsigned char image_ch00_26_green_ramp[9] = {
    60, 57, 51, 54, 57, 60, 57, 51, 54
};
static unsigned char image_ch00_26_blue_ramp[9] = {
    62, 60, 58, 59, 60, 62, 60, 58, 59
};
static unsigned char image_ch08_red_ramp[17] = {
    57, 51, 45, 40, 42, 44, 46, 49, 51, 57,
    51, 45, 40, 42, 44, 46, 49
};
static unsigned char image_ch08_green_ramp[17] = {
    53, 38, 24, 11, 16, 21, 27, 33, 38, 53,
    38, 24, 11, 16, 21, 27, 33
};
static unsigned char image_ch08_blue_ramp[17] = {
    18, 12,  7,  3,  4,  6,  9, 10, 12, 18,
    12,  7,  3,  4,  6,  9, 10
};
static unsigned char image_ch21_red_ramp[13] = {
    37, 38, 40, 42, 40, 38, 37, 37, 38, 40,
    42, 40, 38
};
static unsigned char image_ch21_green_ramp[13] = {
     8,  6,  4,  2,  4,  6,  8,  8,  6,  4,
     2,  4,  6
};
static unsigned char image_ch21_blue_ramp[13] = {
     8,  6,  4,  2,  4,  6,  8,  8,  6,  4,
     2,  4,  6
};
static unsigned char image_ch27_28_red_ramp[19] = {
     2,  4, 12, 19, 20, 20, 15,  8,  4,  2,
     2,  4, 12, 19, 20, 20, 15,  8,  4
};
static unsigned char image_ch27_28_green_ramp[19] = {
     9,  5,  7, 11, 15, 12,  9,  6,  6,  9,
     9,  5,  7, 11, 15, 12,  9,  6,  6
};
static unsigned char image_ch27_28_blue_ramp[19] = {
    19, 18, 21, 23, 25, 23, 21, 21, 18, 19,
    19, 18, 21, 23, 25, 23, 21, 21, 18
};

/* The DAC entries just outside each animated run are used as markers: a byte
   written here before the call has to still be there afterwards. */
static void mark_dac_entry(int entry)
{
    write_dac_entry(entry, 1, 2, 3);
}

static void check_dac_entry_untouched(int entry)
{
    int components[3];

    read_dac_entry(entry, components);
    CHECK_EQ(components[0], 1);
    CHECK_EQ(components[1], 2);
    CHECK_EQ(components[2], 3);
}

/* Puts the scene cycle in a state where the next call is guaranteed to service
   a frame, on the given chapter index and from the given phase. */
static void arm_a_new_scene_tick(int chapter_index, int phase,
                                 unsigned int tick)
{
    data_fdps_chapter_current_chapter_id = chapter_index;
    data_fdps_scene_palette_cycle_phase = phase;
    data_fdps_timer_tick_counter = tick;
    data_fdps_scene_palette_last_cycle_tick = tick + 1;
    data_fdps_audio_cd_repeat_last_tick = tick;
}

/* Checks one animated run entry by entry against the three ramps it came from,
   with the window starting at ramp_start. */
static void check_dac_run(int first_entry, int count, int ramp_start,
                          unsigned char *red, unsigned char *green,
                          unsigned char *blue)
{
    int components[3];
    int n;

    for (n = 0; n < count; n++) {
        read_dac_entry(first_entry + n, components);
        CHECK_EQ(components[0], red[ramp_start + n]);
        CHECK_EQ(components[1], green[ramp_start + n]);
        CHECK_EQ(components[2], blue[ramp_start + n]);
    }
}

/* CMP EAX,dword ptr [00069d64] / JZ 0002f22b goes straight to the epilogue, so
   a tick already serviced leaves the phase, the latch and the DAC alone. */
static void an_already_serviced_scene_tick_changes_nothing(void)
{
    mark_dac_entry(0xf0);
    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_scene_palette_cycle_phase = 7;
    data_fdps_timer_tick_counter = 2000;
    data_fdps_scene_palette_last_cycle_tick = 2000;
    data_fdps_audio_cd_repeat_last_tick = 2000;

    fdps_cycle_scene_palette();

    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 7);
    CHECK_EQ(data_fdps_scene_palette_last_cycle_tick, 2000);
    check_dac_entry_untouched(0xf0);
}

/* INC EDX then IDIV steps the phase, and MOV [00069d20],EAX at 0002f226
   latches the tick the pass ran on. */
static void a_new_scene_tick_steps_the_phase_and_latches_the_tick(void)
{
    arm_a_new_scene_tick(0, 0, 5);

    fdps_cycle_scene_palette();

    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 1);
    CHECK_EQ(data_fdps_scene_palette_last_cycle_tick, 5);
}

/* MOV EBX,0x14 for chapter 1: the period is the colour count times four, so
   the phase runs 0..19 and 19 steps to 0 rather than to 20. */
static void the_phase_wraps_at_four_times_the_colour_count(void)
{
    arm_a_new_scene_tick(0, 18, 6);
    fdps_cycle_scene_palette();
    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 19);

    arm_a_new_scene_tick(0, 19, 7);
    fdps_cycle_scene_palette();
    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 0);
}

/* A chapter index no case matches reaches 0002f221 through the dispatch's
   fall-through jumps: no IDIV runs, so the phase stands, and no colour is
   written -- but the tail still latches the tick. */
static void an_unlisted_chapter_moves_no_colour_but_still_latches(void)
{
    mark_dac_entry(0xf0);
    arm_a_new_scene_tick(5, 13, 8);

    fdps_cycle_scene_palette();

    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 13);
    CHECK_EQ(data_fdps_scene_palette_last_cycle_tick, 8);
    check_dac_entry_untouched(0xf0);
}

/* PUSH 0xf0 and PUSH 0x5 at 0002ed7d and 0002edd3: five entries starting at
   240, each taking ramp[phase / 4 + n].  Phase 3 becomes 4, so the window
   starts at 1 and entries 240..244 get elements 1..5. */
static void chapter_one_writes_five_entries_from_the_window(void)
{
    mark_dac_entry(0xef);
    mark_dac_entry(0xf5);
    arm_a_new_scene_tick(0, 3, 9);

    fdps_cycle_scene_palette();

    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 4);
    check_dac_run(0xf0, 5, 1, image_ch00_26_red_ramp,
                  image_ch00_26_green_ramp, image_ch00_26_blue_ramp);
    check_dac_entry_untouched(0xef);
    check_dac_entry_untouched(0xf5);
}

/* SAR EAX,0x2: the window start is the phase divided by four, so three of
   every four serviced ticks leave the colours exactly where they were. */
static void the_window_only_moves_every_fourth_tick(void)
{
    int components[3];

    arm_a_new_scene_tick(0, 4, 10);
    fdps_cycle_scene_palette();
    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 5);
    read_dac_entry(0xf0, components);
    CHECK_EQ(components[0], image_ch00_26_red_ramp[1]);

    arm_a_new_scene_tick(0, 6, 11);
    fdps_cycle_scene_palette();
    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 7);
    read_dac_entry(0xf0, components);
    CHECK_EQ(components[0], image_ch00_26_red_ramp[1]);

    arm_a_new_scene_tick(0, 7, 12);
    fdps_cycle_scene_palette();
    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 8);
    read_dac_entry(0xf0, components);
    CHECK_EQ(components[0], image_ch00_26_red_ramp[2]);
}

/* Chapter index 26 falls into the same arm as index 0 -- the dispatch's
   CMP 0x1a / JBE 0002ed64 -- so it animates the same five entries from the
   same three ramps with the same period. */
static void chapter_twenty_seven_shares_chapter_one_arm(void)
{
    arm_a_new_scene_tick(26, 3, 13);

    fdps_cycle_scene_palette();

    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 4);
    check_dac_run(0xf0, 5, 1, image_ch00_26_red_ramp,
                  image_ch00_26_green_ramp, image_ch00_26_blue_ramp);
}

/* One MOV EBX per arm: 0x3c, 0x24, 0x3c, 0x1c, 0x3c, 0x2c, 0x28 and 0x3c for
   chapter indices 2, 8, 20, 21, 22, 23, 28 and 29.  The step below the wrap
   reaches the period less one, and the step after it reaches 0. */
static void each_animated_chapter_has_its_own_period(void)
{
    static int chapter_index[8] = { 2, 8, 20, 21, 22, 23, 28, 29 };
    static int period[8] = { 60, 36, 60, 28, 60, 44, 40, 60 };
    int arm;

    for (arm = 0; arm < 8; arm++) {
        arm_a_new_scene_tick(chapter_index[arm], period[arm] - 2,
                             (unsigned int)(20 + arm));
        fdps_cycle_scene_palette();
        CHECK_EQ(data_fdps_scene_palette_cycle_phase, period[arm] - 1);

        arm_a_new_scene_tick(chapter_index[arm], period[arm] - 1,
                             (unsigned int)(40 + arm));
        fdps_cycle_scene_palette();
        CHECK_EQ(data_fdps_scene_palette_cycle_phase, 0);
    }
}

/* PUSH 0xf6 and PUSH 0x9 at 0002ee88 and 0002eee1: chapter 9 animates nine
   entries starting at 246, so 245 below the run and 255 above it are both
   left alone.  Phase 3 becomes 4 and the window starts at 1. */
static void chapter_nine_runs_from_entry_246(void)
{
    mark_dac_entry(0xf5);
    mark_dac_entry(0xff);
    arm_a_new_scene_tick(8, 3, 30);

    fdps_cycle_scene_palette();

    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 4);
    check_dac_run(0xf6, 9, 1, image_ch08_red_ramp, image_ch08_green_ramp,
                  image_ch08_blue_ramp);
    check_dac_entry_untouched(0xf5);
    check_dac_entry_untouched(0xff);
}

/* The highest window a seven-colour arm reaches: phase 27 is the top of the
   0x1c period, so the window starts at 6 and the last entry reads element 12
   -- the final byte of a ramp declared at twice the count less one.  A ramp
   one byte shorter would be read past its end here. */
static void chapter_twenty_two_reaches_the_top_of_its_ramp(void)
{
    mark_dac_entry(0xf7);
    mark_dac_entry(0xff);
    arm_a_new_scene_tick(21, 26, 31);

    fdps_cycle_scene_palette();

    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 27);
    check_dac_run(0xf8, 7, 6, image_ch21_red_ramp, image_ch21_green_ramp,
                  image_ch21_blue_ramp);
    check_dac_entry_untouched(0xf7);
    check_dac_entry_untouched(0xff);
}

/* PUSH 0xf5 and PUSH 0xa at 0002f12b and 0002f18a: chapter 29 is the one arm
   whose run starts at 245, ten entries from 245 to 254.  Phase 7 becomes 8
   and the window starts at 2. */
static void chapter_twenty_nine_runs_from_entry_245(void)
{
    mark_dac_entry(0xf4);
    mark_dac_entry(0xff);
    arm_a_new_scene_tick(28, 7, 32);

    fdps_cycle_scene_palette();

    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 8);
    check_dac_run(0xf5, 10, 2, image_ch27_28_red_ramp, image_ch27_28_green_ramp,
                  image_ch27_28_blue_ramp);
    check_dac_entry_untouched(0xf4);
    check_dac_entry_untouched(0xff);
}

/* Chapter index 27 falls into the same arm as index 28.  The test that picks
   that arm is CMP dword ptr [EBP-0x298],0x1c / JBE 0002f112, and it is only
   reached once CMP 0x1a / JBE has sent everything up to 0x1a elsewhere, so
   the JBE covers 0x1b as well as 0x1c.  Same ten entries from 245, the same
   three ramps and the same period of 40 as index 28: a switch that listed
   only 28 would leave the phase at 7 and the DAC untouched here. */
static void chapter_twenty_eight_shares_chapter_twenty_nine_arm(void)
{
    mark_dac_entry(0xf4);
    mark_dac_entry(0xff);
    arm_a_new_scene_tick(27, 7, 33);

    fdps_cycle_scene_palette();

    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 8);
    check_dac_run(0xf5, 10, 2, image_ch27_28_red_ramp,
                  image_ch27_28_green_ramp, image_ch27_28_blue_ramp);
    check_dac_entry_untouched(0xf4);
    check_dac_entry_untouched(0xff);

    /* The 0x28 period from the same arm's MOV EBX,0x28, driven from index 27
       rather than index 28: 39 steps to 0, not to 40. */
    arm_a_new_scene_tick(27, 39, 34);
    fdps_cycle_scene_palette();
    CHECK_EQ(data_fdps_scene_palette_cycle_phase, 0);
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
    RUN_TEST(an_already_serviced_scene_tick_changes_nothing);
    RUN_TEST(a_new_scene_tick_steps_the_phase_and_latches_the_tick);
    RUN_TEST(the_phase_wraps_at_four_times_the_colour_count);
    RUN_TEST(an_unlisted_chapter_moves_no_colour_but_still_latches);
    RUN_TEST(chapter_one_writes_five_entries_from_the_window);
    RUN_TEST(the_window_only_moves_every_fourth_tick);
    RUN_TEST(chapter_twenty_seven_shares_chapter_one_arm);
    RUN_TEST(each_animated_chapter_has_its_own_period);
    RUN_TEST(chapter_nine_runs_from_entry_246);
    RUN_TEST(chapter_twenty_two_reaches_the_top_of_its_ramp);
    RUN_TEST(chapter_twenty_nine_runs_from_entry_245);
    RUN_TEST(chapter_twenty_eight_shares_chapter_twenty_nine_arm);
}
