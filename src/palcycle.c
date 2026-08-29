/* palcycle.c -- palette-cycling animations for scenes and for the UI.
 *
 * See palcycle.h for what a caller has to know.  This file owns the UI
 * cycle's phase and its serviced-tick latch; it holds no other state, and it
 * reads only the timer tick counter that gamedata.h declares.
 *
 * inp and outp come from <conio.h> as ordinary library calls, which is what
 * the original has: 00015be0 issues CALL 0003d4e4 and CALL 00042cb8 rather
 * than IN/OUT instructions.  Watcom only turns them into instructions when
 * __INLINE_FUNCTIONS__ is defined, and the flag that defines it, -oi, is not
 * in this build's set (rebuild_info/build_flags.md).
 */
#include <conio.h>
#include "gamedata.h"
#include "palcycle.h"

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit anyone here looks at. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The DAC write port pair: the entry number goes to 0x3c8 and its red, green
   and blue components follow on 0x3c9, six bits each. */
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9

/* The animated block is DAC entries 8..15 -- eight entries starting at 8. */
#define UI_PALETTE_FIRST_DAC_ENTRY 8
#define UI_PALETTE_ENTRY_COUNT 8

/* The colour wave is a 16-step cycle, so the phase runs 0..15, but each ramp
   is stored with 24 entries: the first eight are repeated at the end so that
   ramp[phase + entry] with phase up to 15 and entry up to 7 reaches 22 without
   ever having to wrap.  Declaring the ramps with the cycle's 16 entries and
   indexing them the same way reads past the end of the array instead of doing
   the modulo the repetition stands in for (rebuild_info/pitfalls.md). */
#define UI_PALETTE_RAMP_LENGTH 24
#define UI_PALETTE_LAST_PHASE 15

/* 00015be0.  The retrace wait is unconditional and comes first; see the
   header for why it must not be moved behind the tick test.

   The three ramps are function-local arrays with initialisers, which is what
   the read-only copies at 00014740, 00014758 and 00014770 are: the prologue
   REP MOVSDs 24 bytes from each of them onto the frame before anything else
   happens, in the declaration order below.  Their contents are those three
   blocks read back byte for byte.  In 6-bit DAC units the wave runs between a
   light green-cyan (14,42,26) and a near-black green (0,14,0).

   The once-per-tick guard is an equality test, not an ordering one -- CMP EAX,
   dword ptr [00069d64] / JZ -- so a tick counter that has wrapped past the
   latch still services the frame.  On an equal tick the routine returns having
   changed nothing at all: not the DAC, not the phase. */
void fdps_cycle_ui_palette(void)
{
    unsigned char red_ramp[UI_PALETTE_RAMP_LENGTH] = {
        14, 11,  8,  5,  3,  2,  0,  0,
         0,  0,  3,  3,  5,  8, 11, 14,
        14, 11,  8,  5,  3,  2,  0,  0
    };
    unsigned char green_ramp[UI_PALETTE_RAMP_LENGTH] = {
        42, 37, 33, 30, 26, 21, 18, 14,
        14, 18, 21, 26, 30, 33, 37, 42,
        42, 37, 33, 30, 26, 21, 18, 14
    };
    unsigned char blue_ramp[UI_PALETTE_RAMP_LENGTH] = {
        26, 21, 16, 12,  8,  4,  2,  0,
         0,  2,  4,  8, 12, 16, 21, 26,
        26, 21, 16, 12,  8,  4,  2,  0
    };
    int entry;

    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        /* Spin until the retrace begins.  This is the frame pace of every
           modal UI loop in the game. */
    }

    if (data_fdps_ui_palette_last_cycle_tick != data_fdps_timer_tick_counter) {
        data_fdps_ui_palette_last_cycle_tick = data_fdps_timer_tick_counter;

        for (entry = 0; entry < UI_PALETTE_ENTRY_COUNT; entry++) {
            outp(VGA_DAC_WRITE_INDEX, UI_PALETTE_FIRST_DAC_ENTRY + entry);
            outp(VGA_DAC_DATA,
                 red_ramp[data_fdps_ui_palette_cycle_phase + entry]);
            outp(VGA_DAC_DATA,
                 green_ramp[data_fdps_ui_palette_cycle_phase + entry]);
            outp(VGA_DAC_DATA,
                 blue_ramp[data_fdps_ui_palette_cycle_phase + entry]);
        }

        data_fdps_ui_palette_cycle_phase = data_fdps_ui_palette_cycle_phase - 1;
        if (data_fdps_ui_palette_cycle_phase == -1) {
            data_fdps_ui_palette_cycle_phase = UI_PALETTE_LAST_PHASE;
        }
    }
}
