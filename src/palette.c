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

/* 0002ae90.  MOV EAX,[EBP+0x14] / SHR EAX,0x10 / AND EAX,0xff, and the result
   is the whole function: the red component of a packed 0x00RRGGBB word.

   The shift is SHR and not SAR, which is why the argument is unsigned; the AND
   that follows would hide the difference in this function's own result, but the
   packed word is a bit pattern and nothing that builds or consumes one treats
   it as a quantity.

   The mask is not redundant.  fdps_build_palette_tables only ever hands over a
   word it built itself, whose top byte is clear, but the mask is in the
   original and a word with bits set above 23 must still come back with the red
   channel alone.

   The original stores the result into a stack slot and reloads it before
   returning, which is the unoptimised code wcc386 emits for a one-line return;
   there is no second value there to name. */
unsigned int fdps_get_rgb_red(unsigned int rgb)
{
    return (rgb >> 16) & 0xffu;
}

/* 0002aec0.  MOV EAX,[EBP+0x14] / SHR EAX,0x8 / AND EAX,0xff: the green
   component of the same packed 0x00RRGGBB word, one byte down from the red
   one.

   The shift distance is the only thing that differs from fdps_get_rgb_red, so
   everything said there holds here too.  SHR and not SAR is why the argument
   is unsigned, and the mask is what stops the red byte, which the shift leaves
   sitting in bits 8..15, reaching the result.  Here the mask is load-bearing
   for a word the game itself builds, not only for a malformed one: red is
   always present in a packed word, so without the AND every colour with any
   red in it would come back wrong.

   The original stores the result into a stack slot and reloads it before
   returning, which is the unoptimised code wcc386 emits for a one-line return;
   there is no second value there to name. */
unsigned int fdps_get_rgb_green(unsigned int rgb)
{
    return (rgb >> 8) & 0xffu;
}

/* 0002aef0.  MOV EAX,[EBP+0x14] / AND EAX,0xff: the blue component of the same
   packed 0x00RRGGBB word.  Blue is already in bits 0..7, so this is the one
   extractor of the three with no shift in it at all -- the whole body is the
   mask.

   The mask is load-bearing here for every word the game builds, not only for a
   malformed one: red and green sit above blue in the word and nothing else
   removes them.

   Nothing in this body distinguishes a signed argument from an unsigned one,
   since AND 0xff yields the same eight bits either way.  The type is unsigned
   because it is the same packed word the red and green extractors take, and
   there SHR rather than SAR settles it.

   The original stores the result into a stack slot and reloads it before
   returning, which is the unoptimised code wcc386 emits for a one-line return;
   there is no second value there to name. */
unsigned int fdps_get_rgb_blue(unsigned int rgb)
{
    return rgb & 0xffu;
}

/* 0002af20.  The constructor the three extractors above are the inverse of:
   XOR EAX,EAX / MOV AL,byte ptr [EBP+0x14] / SHL EAX,0x10 for red, the same
   pair with SHL EAX,0x8 OR'd in for green, and the same pair unshifted OR'd in
   for blue.

   The arguments are bytes, not masked words.  Each one is fetched with MOV AL
   out of a zeroed EAX -- a byte-wide parameter load -- where the extractors in
   this same object fetch a whole dword and AND it with 0xff.  Two different
   code shapes for what would otherwise be the same operation is what settles
   the parameter width here: only the low byte of each argument slot is ever
   read, so a channel of 256 packs as 0 and the result carries nothing above
   bit 23.

   The load is a zero extension and not MOVSX, which is why the channels are
   unsigned: a channel is a bit pattern going into a fixed field, and 0x80
   belongs in that field as 0x80 rather than sign-extending across the two
   channels above it.

   The original stores the assembled word into a stack slot and reloads it
   before returning, the same unoptimised one-line-return shape the three
   extractors have; there is no second value there to name. */
unsigned int fdps_pack_rgb(unsigned char red, unsigned char green,
                           unsigned char blue)
{
    return ((unsigned int) red << 16)
         | ((unsigned int) green << 8)
         | (unsigned int) blue;
}
