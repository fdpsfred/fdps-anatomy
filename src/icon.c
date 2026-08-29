/* icon.c -- the IconAni cut-scene script interpreter and its opcode handlers.
 *
 * See icon.h for what a caller has to know.  Nothing in this file holds
 * state: the palette a fade runs from is the master palette gamedata.h
 * declares, and the DAC is the only thing written.
 *
 * inp comes from <conio.h> as an ordinary library call, which is what the
 * original has: 00022410 issues CALL 0003d4e4 rather than an IN instruction.
 * Watcom only turns it into an instruction when __INLINE_FUNCTIONS__ is
 * defined, and the flag that defines it, -oi, is not in this build's set
 * (rebuild_info/build_flags.md).  delay comes from <i86.h>, which is where
 * Watcom 10.0a declares it, and it is a real call in the original too:
 * CALL 0003d370.
 */
#include <conio.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "palette.h"
#include "icon.h"

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit anyone here looks at. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* A fade rewrites the whole DAC: entries 0 to 255, inclusive at both ends. */
#define FADE_FIRST_DAC_ENTRY 0
#define FADE_LAST_DAC_ENTRY 0xff

/* How much darker each step gets, and the bound the darkening is stepped up
   against.  The test is < 0x40 with a step of 4, so the amount takes the
   sixteen values 0, 4, ... 60 and the darkest the screen reaches is 60 below
   the master palette rather than the 64 that would extinguish it. */
#define FADE_DARKEN_STEP 4
#define FADE_DARKEN_LIMIT 0x40

/* 00022410.  The loop is CMP dword ptr [EBP-4],0x40 / JL with ADD 0x4 at the
   bottom, so the counter is signed and counts up while the bias handed to the
   upload is its negation -- NEG EAX, three times, once per channel.  A counter
   made unsigned turns that negation into a bias near four billion, which the
   upload's signed clamp reads as brighter than white.

   The retrace wait comes first in every step, before the upload and before
   the delay, and it is what makes the fade advance one displayed frame at a
   time; the delay then holds that frame for the operand's milliseconds.  The
   two together are the fade's speed, and dropping either changes it
   (rebuild_info/pitfalls.md).

   The upload's source is data_fdps_vga_main_palette_ptr on every step, not
   the DAC contents from the step before, so the ramp cannot accumulate error
   and cannot inherit a palette some other scene installed. */
void fdps_icon_script_fade_to_black(int step_delay_ms)
{
    int darken_amount;

    for (darken_amount = 0; darken_amount < FADE_DARKEN_LIMIT;
         darken_amount += FADE_DARKEN_STEP) {
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so each step of the ramp lands
               on its own displayed frame. */
        }

        fdps_set_palette_range(
            (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
            FADE_FIRST_DAC_ENTRY, FADE_LAST_DAC_ENTRY,
            -darken_amount, -darken_amount, -darken_amount);

        delay((unsigned int) step_delay_ms);
    }
}
