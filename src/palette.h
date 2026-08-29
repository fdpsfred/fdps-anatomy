/* palette.h -- the VGA DAC registers and the colour lookup tables built on
 * top of them.
 *
 * Every colour the game shows is a DAC entry: an index 0..255 whose red,
 * green and blue are six bits each, written through the port pair 0x3c8 (the
 * entry number) and 0x3c9 (three data bytes in R, G, B order).  A palette in
 * memory is an array of struct fdps_palette_entry -- the same three bytes in
 * the same order -- which is what a .PAL resource holds and what the loaders
 * hand to this file.
 *
 * Fades and tints in this game are done here rather than in the frame buffer:
 * the picture is drawn once and the DAC is rewritten underneath it, so a fade
 * costs one upload of the palette instead of one redraw of the screen.  That
 * is why the upload takes a per-channel bias -- the caller sweeps the bias
 * from 0 down to -63 and the same source palette darkens to black.
 *
 * This file holds no state.  Both the palette it uploads and the biases come
 * from the caller.
 */
#ifndef PALETTE_H
#define PALETTE_H

#include "fdpstype.h"

/* Uploads DAC entries first_entry..last_entry inclusive from rgb, biasing each
   channel as it goes.  Entry first_entry takes rgb[0], the next takes rgb[1]
   and so on, so rgb must hold at least last_entry - first_entry + 1 records;
   nothing here bounds-checks it and nothing tests it for null.

   Each component is uploaded as (bias + source), clamped into 0..63 at both
   ends: the bias is signed and may take a component off either end of the
   6-bit range, and the clamp is what makes a whole-screen fade a matter of
   walking the bias from 0 to -63 (to black) or from 0 to +63 (to white).  The
   source byte itself is taken unsigned, so a source palette holding values
   above 63 -- which nothing in the game does, but nothing here rejects --
   simply clamps to 63.

   last_entry below first_entry uploads nothing at all: the bound is tested
   before the first entry, not after it.  The DAC's own auto-increment is not
   relied on; every entry rewrites the index register.

   Reads no global, writes no global, and its only callee is the CRT's outp. */
extern void fdps_set_palette_range(struct fdps_palette_entry *rgb,
                                   int first_entry, int last_entry,
                                   int red_bias, int green_bias,
                                   int blue_bias);
#pragma aux fdps_set_palette_range "*" parm caller [];

/* A colour is also carried around this file as one packed word in the form
   0x00RRGGBB -- red in bits 16..23, green in bits 8..15, blue in bits 0..7 --
   which is how a three-byte palette record is handed to the lookup-table
   builder as a single value.  The three components in a packed word are the
   DAC's six-bit values widened to eight by multiplying by four, so each one
   spans 0..252 rather than 0..255.

   Returns the red component of a packed word, 0..255.  Bits 24 and above are
   masked off rather than assumed clear, so a word carrying anything in its top
   byte still yields the red channel alone.  Reads no global and calls nothing.

   The argument is unsigned: the extraction is SHR, not SAR.  The mask that
   follows makes that unobservable through this function's own result, but the
   packed word is a bit pattern rather than a quantity and is signed nowhere
   that handles it. */
extern unsigned int fdps_get_rgb_red(unsigned int rgb);
#pragma aux fdps_get_rgb_red "*" parm caller [];

/* Returns the green component of a packed word, 0..255 -- bits 8..15 brought
   down to bits 0..7.  Everything above the channel is masked off, so both the
   red byte the shift leaves in place and anything a word carries in its top
   byte are excluded rather than assumed absent.  Reads no global and calls
   nothing.

   The argument is unsigned for the same reason as the red extractor's: the
   shift is SHR, not SAR. */
extern unsigned int fdps_get_rgb_green(unsigned int rgb);
#pragma aux fdps_get_rgb_green "*" parm caller [];

/* Returns the blue component of a packed word, 0..255 -- bits 0..7, which are
   already in place, so the whole body is the mask that removes green, red and
   anything the word carries above bit 23.  Reads no global and calls nothing.

   The argument is unsigned to match the other two extractors; this one has no
   shift, so its own body cannot tell a signed argument from an unsigned one. */
extern unsigned int fdps_get_rgb_blue(unsigned int rgb);
#pragma aux fdps_get_rgb_blue "*" parm caller [];

/* Builds the packed word the three extractors above take apart: red into bits
   16..23, green into bits 8..15, blue into bits 0..7.  Reads no global and
   calls nothing.

   Each channel contributes its low eight bits and nothing else -- the original
   loads one byte per argument slot and zero-extends it -- so a value above 255
   loses everything above bit 7 rather than spilling into the channel above it,
   and the result never has a bit set above 23.  Channels are the eight-bit
   widening of the six-bit DAC values, so the ones this game builds run 0..252
   in steps of four.

   Nothing in the image calls this: fdps_build_palette_tables open-codes the
   same packing inline for each of the 256 entries it reads, and this is the
   named form of it that the linker kept because the object defining it is
   linked whole. */
extern unsigned int fdps_pack_rgb(unsigned char red, unsigned char green,
                                  unsigned char blue);
#pragma aux fdps_pack_rgb "*" parm caller [];

/* Returns the index of the palette entry closest to the target colour, by
   smallest squared RGB distance: (r - red)^2 + (g - green)^2 + (b - blue)^2.
   This is what turns an arbitrary 24-bit colour into a DAC index, and
   fdps_build_palette_tables runs it once per cell of the 16x16x16 lookup
   table.  Reads no global and calls nothing.

   palette is the raw 768 bytes of a 256-entry palette -- red, green, blue per
   entry, the byte order of struct fdps_palette_entry -- and all 256 entries
   are always scanned.  There is no entry-count parameter and no null check;
   a buffer shorter than 768 bytes is read past its end.

   Palette bytes are taken unsigned; the target components are taken as they
   come and are neither masked nor clamped, so a caller is responsible for
   keeping them in range.

   Ties go to the lowest index: the comparison against the running best is a
   strict less-than, so a later entry at the same distance does not displace
   an earlier one.

   The result is 0..255 for any target within range.  256 is the seed value
   and means no entry came within a squared distance of 10000000 -- reachable
   only with a target component thousands away from anything a DAC byte can
   hold, which no caller in the image produces. */
extern int fdps_palette_find_nearest_color(int target_red, int target_green,
                                           int target_blue,
                                           unsigned char *palette);
#pragma aux fdps_palette_find_nearest_color "*" parm caller [];

/* Rebuilds both blending tables -- data_fdps_palette_shade_ramp_table and
   data_fdps_inverse_palette_cube, declared in gamedata.h -- from one 256-entry
   DAC palette, and writes nothing else.  fdps_load_global_resources is the
   only caller; every other file only ever reads the two tables.

   palette must hold 256 struct fdps_palette_entry.  There is no entry-count
   parameter and no null check, and all 256 are read; the components are the
   DAC's six-bit values, which this widens to eight by multiplying by four.

   The shade ramp comes out as 18 rows of 256 words.  Row 0 is all zero, row 1
   is the palette in the nibble-per-byte form 0x000R0G0B -- the top nibble of
   each widened channel in the low nibble of its own byte -- and rows 2..17 are
   row 1 multiplied by 2..8 and then by 16 down to 8.  A row is therefore a
   weight, not a brightness, and the two halves of the table are two separate
   ramps; a blitter picks a row and reads one word per source pixel.

   The cube comes out as 16x16x16 palette indices in green-major order: the
   cell for a quantised (r, g, b), each 0..15, is at green * 256 + red * 16 +
   blue.  Each cell is the nearest entry by squared distance to (red * 4,
   green * 4, blue * 4), so the whole cube is 4096 calls to
   fdps_palette_find_nearest_color and this is by far the slowest thing the
   loader does.

   Nothing here reads either table before writing it, so calling it again with
   a different palette replaces both outright. */
extern void fdps_build_palette_tables(struct fdps_palette_entry *palette);
#pragma aux fdps_build_palette_tables "*" parm caller [];

#endif
