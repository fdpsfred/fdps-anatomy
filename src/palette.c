/* palette.c -- the VGA DAC registers and the colour lookup tables built on
 * top of them.
 *
 * See palette.h for what a caller has to know.  The palettes and the biases
 * arrive as arguments; this file owns no state of its own beyond the two
 * lookup tables fdps_build_palette_tables fills, and those live in
 * gamedata.h because eighteen other files read them.
 *
 * inp and outp come from <conio.h> as ordinary library calls, which is what
 * the original has: 00022f40 issues CALL 00042cb8 and 0002f240 issues CALL
 * 0003d4e4 rather than OUT and IN instructions.  Watcom only turns them into
 * instructions when __INLINE_FUNCTIONS__ is defined, and the flag that
 * defines it, -oi, is not in this build's set (rebuild_info/build_flags.md).
 */
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "palette.h"

/* The DAC write port pair: the entry number goes to 0x3c8 and its red, green
   and blue components follow on 0x3c9, six bits each. */
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9

/* A DAC component is six bits, so 63 is the brightest value the hardware
   takes and the value the bias is clamped up against. */
#define VGA_DAC_MAX_COMPONENT 0x3f

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit anyone here looks at. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

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

/* The source palette always has 256 entries here: the 256-iteration loops are
   CMP against 0x100 with the counter never compared against anything the
   caller supplies, so the entry count is not a parameter. */
#define PALETTE_ENTRY_COUNT 0x100

/* The shade ramp is 18 rows of PALETTE_ENTRY_COUNT words: row 0 forced to
   zero, row 1 the palette itself, rows 2..17 the scaled copies.  The row
   count is the bound of the level loop, CMP [EBP-0x14],0x12 / JL, and it is
   also the length of the multiplier table the prologue copies in. */
#define SHADE_LEVEL_COUNT 0x12

/* The inverse lookup is a cube of 16 steps per axis: three nested loops each
   bounded by CMP ...,0x10 / JL. */
#define COLOUR_CUBE_AXIS 0x10

/* 0002af60.  Fills both of the lookup tables the blitters blend through, from
   one 256-entry DAC palette.  The only caller is fdps_load_global_resources,
   which runs it once at startup and again whenever the main palette changes.

   Three passes, in this order:

   1.  The shade ramp's row 1 (data_fdps_palette_shade_ramp_table + 256) gets
       one packed word per palette entry, and row 0 is forced to zero.  A
       packed word here is not the 0x00RRGGBB form the extractors take apart;
       it is the nibble-per-byte form 0x000R0G0B, the top nibble of each
       eight-bit channel sitting in the low nibble of its own byte.  That
       layout is the whole trick of the table: because every channel has four
       clear bits above it inside its own byte, one multiply scales all three
       at once and no channel can carry into the next.

   2.  Rows 2..17 are row 1 multiplied by that row's entry in the multiplier
       table.  The multipliers are 2..8 for rows 2..8 and then 16 down to 8
       for rows 9..17, so the two halves of the table are two different
       weight ramps and the row index is not a brightness in itself.

   3.  data_fdps_inverse_palette_cube gets the nearest palette index for every
       one of the 16x16x16 quantised colours.

   THE TWO ABSOLUTE BASES ARE ONE ARRAY (pitfalls, contract B/H).  The original
   writes row 0 through 0x653f0 and row 1 through 0x657f0, which is 0x653f0 +
   0x400 -- the same array, with the row-1 base folded into the displacement.
   Ghidra attributes nothing separate to 0x657f0 and neither does routing.json:
   there is one 4608-word global here, and emitting the second base as a global
   of its own would put row 1 wherever the linker felt like putting it while
   pass 2 kept reading the row the multiply needs.

   THE PACKING IS OPEN-CODED, and that is not an oversight.  fdps_pack_rgb
   above has exactly this body, but the original has no CALL here: the shape at
   0002b00e is an inline expansion of it, right down to reading only the low
   byte of each of the three argument slots the way the out-of-line copy reads
   only the low byte of each of its parameters.  Writing the call instead would
   be functionally identical; writing it out is what the assembly does.

   THE MULTIPLIER TABLE IS A LOCAL, not a global.  The prologue copies 18
   dwords from 0002ae40 with REP MOVSD into the frame, which is what wcc386
   emits for an auto array with an initialiser; routing.json lists 0002ae40 as
   skipped for exactly that reason, so the contents belong here and nowhere
   else.

   Signedness, all of it deliberate.  The palette bytes are zero-extended, AND
   EAX,0xff, so a component above 127 enters as itself.  Every loop bound is
   JL, signed.  The row scaling is IMUL, a signed multiply, on values that
   cannot exceed 0x0f0f0f * 16 -- so the low 32 bits are the same either way
   and nothing here can overflow.  The blue nibble comes down with SAR and not
   SHR, but the mask above it leaves at most 0xf0, so the sign bit is clear and
   the two shifts agree.

   The cube is filled in one sequential sweep with green outermost, red in the
   middle and blue innermost -- the call is
   fdps_palette_find_nearest_color(red, green, blue, palette) with the middle
   counter as red and the outer one as green -- so the cell for a quantised
   (r, g, b) is at green * 256 + red * 16 + blue.  The index is a running
   counter the innermost loop increments, never recomputed from the three
   loops, which is what fixes that order.

   The nearest-colour search can return 0x100 for a target nothing comes close
   to, and the store is MOV byte ptr, so such a cell would hold 0.  No target
   this function builds is out of range -- every component is 0..60 -- so the
   case does not arise here. */
void fdps_build_palette_tables(struct fdps_palette_entry *palette)
{
    /* 0002ae40, copied into the frame by the prologue's REP MOVSD.  Rows 0
       and 1 never consult it: the level loop starts at 2. */
    int shade_multipliers[SHADE_LEVEL_COUNT] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8,
        16, 15, 14, 13, 12, 11, 10, 9, 8
    };
    int entry_index;
    int shade_level;
    int level_multiplier;
    int level_row_base;
    int red_scaled;
    int green_scaled;
    int blue_scaled;
    unsigned int packed_color;
    int cube_green;
    int cube_red;
    int cube_blue;
    int cube_index;

    for (entry_index = 0; entry_index < PALETTE_ENTRY_COUNT; entry_index++) {
        data_fdps_palette_shade_ramp_table[entry_index] = 0;

        /* The DAC's six bits widened to eight, LEA EAX,[EAX*4+0] on each of
           the three bytes, so a component of 63 becomes 252. */
        red_scaled = palette[entry_index].red * 4;
        green_scaled = palette[entry_index].green * 4;
        blue_scaled = palette[entry_index].blue * 4;

        packed_color = ((unsigned int) (unsigned char) red_scaled << 16)
                     | ((unsigned int) (unsigned char) green_scaled << 8)
                     | (unsigned int) (unsigned char) blue_scaled;

        data_fdps_palette_shade_ramp_table[PALETTE_ENTRY_COUNT + entry_index] =
            ((fdps_get_rgb_red(packed_color) & 0xf0u) << 12)
          | ((fdps_get_rgb_green(packed_color) & 0xf0u) << 4)
          | ((fdps_get_rgb_blue(packed_color) & 0xf0u) >> 4);
    }

    for (shade_level = 2; shade_level < SHADE_LEVEL_COUNT; shade_level++) {
        level_multiplier = shade_multipliers[shade_level];
        level_row_base = shade_level * PALETTE_ENTRY_COUNT;

        for (entry_index = 0; entry_index < PALETTE_ENTRY_COUNT;
             entry_index++) {
            data_fdps_palette_shade_ramp_table[level_row_base + entry_index] =
                data_fdps_palette_shade_ramp_table[PALETTE_ENTRY_COUNT
                                                   + entry_index]
                * (unsigned int) level_multiplier;
        }
    }

    cube_index = 0;
    for (cube_green = 0; cube_green < COLOUR_CUBE_AXIS; cube_green++) {
        for (cube_red = 0; cube_red < COLOUR_CUBE_AXIS; cube_red++) {
            for (cube_blue = 0; cube_blue < COLOUR_CUBE_AXIS; cube_blue++) {
                data_fdps_inverse_palette_cube[cube_index] =
                    (unsigned char) fdps_palette_find_nearest_color(
                        cube_red * 4, cube_green * 4, cube_blue * 4,
                        (unsigned char *) palette);
                cube_index++;
            }
        }
    }
}

/* 0002b1b0.  The nearest-colour search fdps_build_palette_tables runs 4096
   times to fill the 16x16x16 lookup table: a linear scan of all 256 palette
   entries keeping the one whose squared RGB distance to the target is
   smallest.

   The palette arrives as a byte cursor, not as a record pointer, because that
   is what the original walks: MOV AL,byte ptr [EAX] with no displacement,
   followed by INC of the cursor slot, three times per iteration.  The three
   bytes it steps through are a struct fdps_palette_entry's red, green and
   blue in that order, so the caller may hand over an array of those records;
   what this function needs is 768 readable bytes.

   The loop is a fixed 256 iterations -- CMP dword ptr [EBP-0x24],0x100 / JL,
   with the counter never compared against anything the caller supplies -- so
   the entry count is not a parameter and a shorter palette is read past its
   end.

   Three things about the arithmetic are load-bearing:

   AND EAX,0xff after the byte load zero-extends the palette byte, so a
   component of 200 enters the subtraction as 200 and not as -56.  The target
   components are not masked at all, which is why they are plain ints here and
   why a caller passing something outside 0..255 gets a real, and possibly
   enormous, distance rather than a wrapped one.

   The distance is the squared distance, not the sum of absolute differences.
   Which entry wins differs between the two metrics -- a target 10 away in all
   three channels is closer than one 18 away in a single channel by squares and
   farther by sums -- so the IMULs are not an optimisation of a simpler
   comparison.

   JGE against the running best makes the test a strict less-than: the first of
   several equally close entries wins and no later one displaces it.

   The seed is 10000000 for the distance and 0x100 for the index, and 0x100 is
   not a valid entry.  Three in-range components can be at most 3 * 255 * 255 =
   195075 apart, so with a target the game itself builds the seed is always
   beaten and the result is always 0..255; the seed survives only for a target
   far outside the DAC's range, and then this returns 256. */
int fdps_palette_find_nearest_color(int target_red, int target_green,
                                    int target_blue, unsigned char *palette)
{
    unsigned char *palette_cursor;
    int best_distance;
    int best_index;
    int entry_index;
    int red_delta;
    int green_delta;
    int blue_delta;
    int distance;

    best_distance = 10000000;
    best_index = 0x100;
    palette_cursor = palette;

    for (entry_index = 0; entry_index < 0x100; entry_index++) {
        red_delta = target_red - (int) *palette_cursor++;
        green_delta = target_green - (int) *palette_cursor++;
        blue_delta = target_blue - (int) *palette_cursor++;

        distance = red_delta * red_delta
                 + green_delta * green_delta
                 + blue_delta * blue_delta;

        if (distance < best_distance) {
            best_distance = distance;
            best_index = entry_index;
        }
    }

    return best_index;
}

/* 0002f240.  The other DAC upload in this file, and the differences from
   fdps_set_palette_range above are all deliberate.

   THE SOURCE IS THREE ARRAYS, NOT ONE ARRAY OF RECORDS.  The three loads are
   MOV EAX,[EBP+0x18] / ADD EAX,[EBP-0x4] / MOV AL,byte ptr [EAX] and the same
   again off [EBP+0x1c] and [EBP+0x20], so each channel has its own base and
   each is indexed by the loop counter with a stride of one byte.  Nothing here
   walks a three-byte record.  fdps_cycle_scene_palette, the only caller, holds
   its colours as three per-channel ramps and slides a rotating phase offset
   along them, which is why they arrive split.

   THERE IS NO BIAS AND NO CLAMP.  AND EAX,0xff and then straight to the PUSH:
   the byte is zero-extended and uploaded whole, with none of the sibling's
   compare-and-clamp pair between the load and the port write.  A byte above 63
   is not clamped to 63; it reaches the DAC, which drops its top two bits, so
   200 shows as 8.  Adding a clamp here to match the sibling would change what
   such a byte displays (rebuild_info/pitfalls.md).

   THE COUNT IS A COUNT, NOT AN INCLUSIVE LAST ENTRY.  The bound is
   CMP EAX,[EBP+0x24] / JL, tested before the first upload, so count entries are
   written and a count of zero or less writes none.  JL is signed: a negative
   count falls straight through rather than running away as an unsigned bound
   would.

   THE RETRACE WAIT HAPPENS ONCE, BEFORE THE LOOP, and it is not a frame
   counter the caller could hoist or drop.  PUSH 0x3da / CALL inp /
   TEST AL,0x8 / JZ back to the PUSH sits above the loop's initialiser, so the
   whole run is written inside one blanking interval; per entry it would tear
   the run across frames, and omitted it would tear the picture.  It is also
   the only place this function blocks, and it blocks for at most one frame per
   call: fdps_cycle_scene_palette has nine call sites, but they are the arms of
   one scene-id dispatch -- each arm calls once and then JMPs to the shared
   tail at 0002f221, and the caller contains no loop at all -- so one
   invocation of the caller is one call here and one wait.  The animation's
   speed is set elsewhere in the caller, by its timer-tick gate and by the
   arm's own phase modulus (0x14, 0x1c, 0x24, 0x28, 0x2c or 0x3c invocations
   to a full cycle).

   The index register is rewritten for every entry -- PUSH 0x3c8 inside the
   loop, not before it -- so the DAC's auto-increment is not relied on, exactly
   as in the sibling.  The entry number is the loop counter added to
   first_index, ADD EAX,[EBP+0x14], so the run is ascending and contiguous. */
void fdps_set_palette_range_on_retrace(int first_index, unsigned char *red,
                                       unsigned char *green,
                                       unsigned char *blue, int count)
{
    int entry_offset;

    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        /* Spin until the retrace begins, so the whole run lands during
           blanking. */
    }

    for (entry_offset = 0; entry_offset < count; entry_offset++) {
        outp(VGA_DAC_WRITE_INDEX, entry_offset + first_index);
        outp(VGA_DAC_DATA, red[entry_offset]);
        outp(VGA_DAC_DATA, green[entry_offset]);
        outp(VGA_DAC_DATA, blue[entry_offset]);
    }
}
