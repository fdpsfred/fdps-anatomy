/* text.c -- drawing text and numbers, and the 1bpp glyph blit underneath them.
 *
 * See text.h for the glyph bitmap layout and the destination surface contract.
 * Nothing here owns state: the glyph cell size comes from the two font globals
 * gamedata.c owns, and everything else arrives as an argument.
 */
#include "gamedata.h"
#include "text.h"

/* 0001fed0.  Four stack arguments -- PUSH EAX x4 at the call sites in
   fdps_draw_glyph, then ADD ESP,0x10 after each CALL -- read at [EBP+0x14]
   through [EBP+0x20] behind the four pushed registers and the return address.
   Nothing reads EAX afterwards; the function returns nothing.

   Both loop bounds are re-read from their globals on every iteration (XOR
   EAX,EAX / MOV AL,[0x0006403d] inside the loop head at 0001fee3, and the same
   shape for the width at 0001ff00), so the C spells them in the condition
   rather than latching them into locals.  The compare is CMP EAX,[EBP-0x8] /
   JG, the signed one, on a byte that was zero-extended: 0..255 either way, so
   the signedness costs nothing here, but the read has to stay a widening of an
   unsigned char and not a sign-extension of a char, or a cell height of 200
   becomes -56 and nothing is drawn at all.

   The bit window is a full dword slot (SHL dword ptr [EBP-0x4],0x1 and TEST
   dword ptr [EBP-0x4],0x80), not the byte the decompiler infers, and it is
   deliberately not initialised: column 0 of every row satisfies (column & 7)
   == 0, so the fetch branch always runs before the shift branch can.

   The fetch is MOV EDX,[EBP+0x1c] / INC dword ptr [EBP+0x1c] / MOV AL,[EDX]:
   the byte comes from the pointer's value before the step, and the pointer
   that steps is the callee's own copy of the argument.  fdps_draw_glyph hands
   the same glyph_bits to five calls in a row and depends on that.

   The fetch condition being true again at column 0 makes source rows
   byte-aligned: a 12-pixel-wide glyph consumes two bytes per row and the last
   four bits of the second byte are never drawn.

   A clear bit writes nothing at all -- the JZ at 0001ff38 skips the store
   rather than storing a background colour -- so the destination shows through
   between the strokes.  Filling the cell first would look equivalent on a
   cleared screen and is not: text here is drawn over the window artwork. */
void fdps_blit_glyph_1bpp(unsigned char *dst, int pitch,
                          unsigned char *glyph_bits, int color)
{
    unsigned int glyph_bit_window;
    int row;
    int column;

    for (row = 0; row < (int) data_fdps_glyph_cell_height; row++) {
        for (column = 0; column < (int) data_fdps_font_glyph_width; column++) {
            if ((column & 7) == 0) {
                glyph_bit_window = (unsigned int) *glyph_bits;
                glyph_bits++;
            } else {
                glyph_bit_window <<= 1;
            }
            if ((glyph_bit_window & 0x80) != 0) {
                dst[column] = (unsigned char) color;
            }
        }
        dst += pitch;
    }
}
