/* palette.c -- the VGA DAC registers and the colour lookup tables built on
 * top of them.
 *
 * See palette.h for what a caller has to know.  This file holds no state: the
 * palette and the biases arrive as arguments and the DAC is the only thing
 * written.
 *
 * outp comes from <conio.h> as an ordinary library call, which is what the
 * original has: 00022f40 issues CALL 00042cb8 rather than an OUT instruction.
 * Watcom only turns it into an instruction when __INLINE_FUNCTIONS__ is
 * defined, and the flag that defines it, -oi, is not in this build's set
 * (rebuild_info/build_flags.md).
 */
#include <conio.h>
#include "fdpstype.h"
#include "palette.h"

/* The DAC write port pair: the entry number goes to 0x3c8 and its red, green
   and blue components follow on 0x3c9, six bits each. */
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9

/* A DAC component is six bits, so 63 is the brightest value the hardware
   takes and the value the bias is clamped up against. */
#define VGA_DAC_MAX_COMPONENT 0x3f

/* 00022f40.  One entry per iteration, three components per entry, and the
   index register rewritten every time.

   The loop bound is CMP EAX,[EBP+0x1c] / JLE, so last_entry is inclusive and
   a last_entry below first_entry falls straight through to the epilogue
   without touching the DAC at all.

   Each component is read with MOV AL,byte ptr [EAX] followed by AND EAX,0xff:
   the source byte is zero-extended, never sign-extended, so a source value of
   200 enters the sum as 200 and not as -56.  The clamp that follows is signed
   in both directions -- JLE against 0x3f, then JGE against 0 -- which is what
   makes a negative bias fade towards black instead of wrapping round to a
   bright colour.

   The clamped value lives in one stack slot that all three channels reuse, and
   the three blocks are written out in full rather than folded into a helper:
   the original has no call here beyond outp, and the three uploads have to
   reach the data port in R, G, B order with nothing between them. */
void fdps_set_palette_range(struct fdps_palette_entry *rgb,
                            int first_entry, int last_entry,
                            int red_bias, int green_bias, int blue_bias)
{
    int dac_entry;
    int component;

    for (dac_entry = first_entry; dac_entry <= last_entry; dac_entry++) {
        outp(VGA_DAC_WRITE_INDEX, dac_entry);

        component = red_bias + (int) rgb->red;
        if (component > VGA_DAC_MAX_COMPONENT) {
            component = VGA_DAC_MAX_COMPONENT;
        } else if (component < 0) {
            component = 0;
        }
        outp(VGA_DAC_DATA, component);

        component = green_bias + (int) rgb->green;
        if (component > VGA_DAC_MAX_COMPONENT) {
            component = VGA_DAC_MAX_COMPONENT;
        } else if (component < 0) {
            component = 0;
        }
        outp(VGA_DAC_DATA, component);

        component = blue_bias + (int) rgb->blue;
        if (component > VGA_DAC_MAX_COMPONENT) {
            component = VGA_DAC_MAX_COMPONENT;
        } else if (component < 0) {
            component = 0;
        }
        outp(VGA_DAC_DATA, component);

        rgb++;
    }
}
