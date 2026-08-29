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

#endif
