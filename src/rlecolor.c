/* rlecolor.c -- the RLE blit kernels that recolour as they draw: palette remap
 * with and without the backdrop, and the additive recolour kernel.
 *
 * See rlecolor.h for what the four ops mean here, where the row width and the
 * row count come from, and how the dispatcher's sixth argument is read.
 */
#include "gamedata.h"
#include "rlecolor.h"

/* ------------------------------------------------------------------------
   REFERENCE ONLY, NOT COMPILED.  The routines below are the C translation of
   the original's hand-written assembly, kept for reading.  What is linked is
   src/rlepal.asm, transcribed from FDPS.LE instruction for instruction
   (ticket 22.3).  rebuild_info/code_layout.md says how to switch the
   rebuild back to this C, and tools/rle_asm/switch_impl.py does it.
   ------------------------------------------------------------------------ */
#if 0 /* RLE_C_REFERENCE -- rebuild_info/code_layout.md */

/* 00056a8d.  Hand-written assembly, not compiler output: no prologue, ESI is
   the stream, EDI the destination, EDX the row advance, BX the width left in
   the row and CL the command byte, and the table base arrives as [EBP+0x1c] --
   the dispatcher's own frame, which the first instruction overwrites EBP with.
   The C below is the same decode and the same arithmetic, not the same
   registers (ADR-0001), the way fdps_rle_blit_passthrough at 00056a0d is.

   The four inputs become four parameters.  In the original the first three are
   set up by fdps_blit_dispatch immediately before the CALL (MOV ESI,[EBP+8],
   MOV EDI,[EBP+0xc], and EDX as [EBP+0x18] - [EBP+0x10] at 000568e7) and the
   fourth is reached by reading the dispatcher's frame through the frame pointer
   it was called with; nothing is passed on the stack.  Three register facts of
   the original therefore do not carry over and must not be reproduced: the
   caller's XOR ECX,ECX at 0005690d, which exists because the body writes only
   CL but reads the whole of CX in SUB BX,CX and the whole of ECX in REP STOSB /
   LOOP; the caller's XOR EAX,EAX at 0005690f, which is what keeps the table
   index inside 0..255 when only AL is written before MOV AL,[EAX+EBP*1]; and
   the destruction of EBX, ESI, EDI and the caller's EBP, which the stack
   convention says a callee preserves.  All three are properties of the register
   contract, not of what the routine draws.

   The op selector and the length are the family's: SHL CL,1 / JC twice for the
   top two bits, SHR CL,2 / INC CL for the low six plus one, so a run is 1 to 64
   pixels and a length of zero cannot be encoded.

   What makes this kernel the one it is, is op 11.  In the plain blitter it is a
   transparent skip; here it consumes no stream byte and instead reads the pixel
   already on the surface, writes palette_remap[that pixel] back and moves on
   (MOV AL,[EDI] / MOV AL,[EAX+EBP*1] / STOSB at 00056b08).  Building this file
   by copying rle.c and only threading the table through the writes leaves the
   backdrop inside the sprite's bounding box unremapped, which is the whole
   difference between this kernel and the mode-2 one.

   Three things here look like defects and are not, all three shared with
   fdps_rle_blit_passthrough:

   The row ends on OR BX,BX / JNZ, an exact-zero test.  A run that overshoots the
   remaining width wraps the sixteen-bit counter instead of ending the row, and
   the decoder goes on consuming command bytes until some later run happens to
   land the counter on zero.  Hardening this to `width <= 0` changes what such a
   stream draws, so the counter is an unsigned short here and the test stays an
   equality (rebuild_info/pitfalls.md).

   Op 01 writes only the second byte of each destination pair -- INC EDI then
   STOSB at 00056ad1 -- so the first byte of every pair keeps what was already on
   the surface, unremapped.  That untouched column is a deliberate one-pixel hole
   inside painted content, so filling it, or remapping it the way op 11 remaps
   its own run, changes what the game draws.

   The row counter is decremented in memory and the loop that reads it is a
   do-while: DEC word ptr [0x00070022] / JNZ tests after the decrement, so a
   caller that asks for zero rows gets 0x10000 of them rather than none.  That is
   reproduced rather than guarded, and fdps_blit_dispatch is the only thing that
   sets it.

   The width is re-read from the global at the top of every row rather than
   cached in a local, because that is where MOV BX,[0x00070024] sits -- at the
   loop's entry, which is also the row-restart target. */
void fdps_rle_blit_remap_sprite_and_backdrop(unsigned char *rle_stream,
                                             unsigned char *dest_pixel,
                                             int dest_row_advance,
                                             unsigned char *palette_remap)
{
    unsigned char *stream_cursor;
    unsigned char *dest_cursor;
    unsigned short width_remaining;
    unsigned char command;
    unsigned char run_pixel;
    unsigned int run_length;

    stream_cursor = rle_stream;
    dest_cursor = dest_pixel;

    do {
        width_remaining = data_fdps_graphics_rle_blit_src_width;

        do {
            command = *stream_cursor;
            stream_cursor++;
            run_length = (unsigned int) (command & 0x3f) + 1;

            switch (command >> 6) {
            case 0:
                /* fill: one pixel byte, remapped once, over run_length bytes */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                run_pixel = palette_remap[*stream_cursor];
                stream_cursor++;
                while (run_length != 0) {
                    *dest_cursor = run_pixel;
                    dest_cursor++;
                    run_length--;
                }
                break;

            case 1:
                /* stretched: the remapped byte into the second half of each of
                   run_length destination pairs, first halves untouched */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length - run_length);
                run_pixel = palette_remap[*stream_cursor];
                stream_cursor++;
                while (run_length != 0) {
                    dest_cursor++;
                    *dest_cursor = run_pixel;
                    dest_cursor++;
                    run_length--;
                }
                break;

            case 2:
                /* literal: run_length stream bytes, each remapped on its way */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                while (run_length != 0) {
                    *dest_cursor = palette_remap[*stream_cursor];
                    dest_cursor++;
                    stream_cursor++;
                    run_length--;
                }
                break;

            default:
                /* backdrop: no stream byte at all -- run_length destination
                   pixels are read back and remapped in place */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                while (run_length != 0) {
                    *dest_cursor = palette_remap[*dest_cursor];
                    dest_cursor++;
                    run_length--;
                }
                break;
            }
        } while (width_remaining != 0);

        dest_cursor += dest_row_advance;
        data_fdps_graphics_rle_blit_remaining_rows--;
    } while (data_fdps_graphics_rle_blit_remaining_rows != 0);
}

/* 00056b25.  The same hand-written shape as the kernel above and the same four
   inputs: ESI the stream, EDI the destination, EDX the row advance, and the
   table base taken out of the dispatcher's own frame by MOV EBP,[EBP+0x1c] at
   00056b25, which overwrites EBP for the rest of the call.  Nothing is passed on
   the stack, so the four become four parameters here (ADR-0001), and the
   caller's XOR ECX,ECX / XOR EAX,EAX at 0005690d and the destruction of EBX,
   ESI, EDI and EBP are register-contract facts of the original that do not
   carry over.

   Op selector and length are the family's: SHL CL,1 / JC twice at 00056b32 and
   00056b3a (or 00056b77), then SHR CL,2 / INC CL, so a run is 1 to 64 pixels.

   What makes this kernel the one it is, is op 11: here it is a plain
   transparent skip -- ADD EDI,ECX / SUB BX,CX at 00056b98, no stream byte read
   and no destination byte touched -- where the sprite-and-backdrop kernel above
   spends the same op remapping the pixels already on the surface.  Everything
   else the two do is identical.  So the destination bytes under a mode-2 skip
   run come out holding exactly what they held, not palette_remap[] of it, and
   that is the whole difference between the two modes.

   The same three things that look like defects in the kernel above are here
   too, and are not defects:

   The row ends on OR BX,BX / JNZ at 00056ba2, an exact-zero test on a sixteen-
   bit counter, and op 01 subtracts the length twice (SUB BX,CX at 00056b5f and
   00056b62).  A run that overshoots the remaining width wraps through 65535 and
   the decoder keeps consuming command bytes; `width_remaining > 0` on an int
   would end the row instead (rebuild_info/pitfalls.md).

   Op 01 writes only the second byte of each destination pair -- INC EDI then
   STOSB at 00056b69 -- so the first byte of every pair keeps the surface's own
   byte, untouched and unremapped.

   The row counter is decremented in memory and tested after the decrement (DEC
   word ptr [0x00070022] / JNZ at 00056ba9), so a request for zero rows draws
   0x10000 of them.  That is reproduced rather than guarded.

   The width is re-read from the global at the top of every row because MOV
   BX,[0x00070024] at 00056b28 is where the JNZ at 00056bb0 lands.

   Nothing in the shipped program selects mode 2, so nothing here can be found by
   playing: it has to be right by reading. */
void fdps_rle_blit_with_palette_remap(unsigned char *rle_stream,
                                      unsigned char *dest_pixel,
                                      int dest_row_advance,
                                      unsigned char *palette_remap)
{
    unsigned char *stream_cursor;
    unsigned char *dest_cursor;
    unsigned short width_remaining;
    unsigned char command;
    unsigned char run_pixel;
    unsigned int run_length;

    stream_cursor = rle_stream;
    dest_cursor = dest_pixel;

    do {
        width_remaining = data_fdps_graphics_rle_blit_src_width;

        do {
            command = *stream_cursor;
            stream_cursor++;
            run_length = (unsigned int) (command & 0x3f) + 1;

            switch (command >> 6) {
            case 0:
                /* fill: one pixel byte, remapped once, over run_length bytes */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                run_pixel = palette_remap[*stream_cursor];
                stream_cursor++;
                while (run_length != 0) {
                    *dest_cursor = run_pixel;
                    dest_cursor++;
                    run_length--;
                }
                break;

            case 1:
                /* stretched: the remapped byte into the second half of each of
                   run_length destination pairs, first halves untouched */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length - run_length);
                run_pixel = palette_remap[*stream_cursor];
                stream_cursor++;
                while (run_length != 0) {
                    dest_cursor++;
                    *dest_cursor = run_pixel;
                    dest_cursor++;
                    run_length--;
                }
                break;

            case 2:
                /* literal: run_length stream bytes, each remapped on its way */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                while (run_length != 0) {
                    *dest_cursor = palette_remap[*stream_cursor];
                    dest_cursor++;
                    stream_cursor++;
                    run_length--;
                }
                break;

            default:
                /* skip: a transparent run, destination stepped over unwritten
                   and unremapped, no stream byte consumed */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                dest_cursor += run_length;
                break;
            }
        } while (width_remaining != 0);

        dest_cursor += dest_row_advance;
        data_fdps_graphics_rle_blit_remaining_rows--;
    } while (data_fdps_graphics_rle_blit_remaining_rows != 0);
}

/* 00056bb7.  Blit mode 3, the same hand-written shape as the two kernels above
   and the same handoff: ESI the stream, EDI the destination, EDX the row
   advance, and the dispatcher's sixth argument reached through the dispatcher's
   own live frame.  Here it is not a pointer.  The first three instructions read
   three separate bytes out of that argument's slot -- MOV DH,[EBP+0x1c], MOV
   DL,[EBP+0x1d], MOV AH,[EBP+0x1e] -- and only then does PUSH EDX / POP EBP
   overwrite EBP with the row advance, which is why the reads come first.  The
   slot is a 32-bit argument and the three bytes are its low three, so it comes
   across as one unsigned parameter that this kernel unpacks itself, the way the
   original does (rlecolor.h).  Nothing is passed on the stack, and the caller's
   XOR ECX,ECX / XOR EAX,EAX at 0005690d and the destruction of EBX, ESI, EDI
   and EBP are register-contract facts of the original that do not carry over.

   Op selector and length are the family's: SHL CL,1 / JC at 00056bce and
   00056bd6 (or 00056c19), then SHR CL,2 / INC CL, so a run is 1 to 64 pixels
   and a length of zero cannot be encoded.

   What makes this kernel the one it is, is that it has no table at all.  Every
   pixel it writes is passed through three 8-bit steps before the store, at
   00056be5 / 00056be7 / 00056be9 and again on each of the other two writing
   paths:

       pixel = ((source + tint_offset) & band_mask) + color_base

   each step wrapping in eight bits, and the destination byte is never read, so
   nothing here blends.  The band mask is what generalises the predecessor's
   fixed ((src + offset) & 7) + base: with mask 7 the sprite is folded into the
   eight-colour palette band anchored at color_base while tint_offset rotates
   which colour of the band each pixel lands on, and with mask 0 the whole
   sprite collapses to the single index color_base.  That collapsing case is the
   one the shipped program uses -- fdps_blit_unit_sprite reaches mode 3 with
   0x0000ff00, so tint_offset 0, color_base 0xff and mask 0, painting the unit
   as a flat silhouette in index 0xff for the rest flash (MOV dword ptr
   [EBP+-0x8],0xff00 in fdps_unit_rest at 000120dc and in
   fdps_battle_advance_turn at 0001e400).

   The packing is not the predecessor's and must not be carried over from it:
   FD2 packs base in the low byte and offset in byte 1 with the mask hardcoded
   to 7, FDPS packs tint_offset in the low byte (added before the mask),
   color_base in byte 1 (added after it) and the band mask in byte 2.  Swapping
   the two operands is not a crash: with the shipped constant it turns a flat
   white silhouette into a scrambled sprite.

   Op 11 here is the plain transparent skip -- ADD EDI,ECX / SUB BX,CX at
   00056c40, no stream byte read and no destination byte touched -- so the
   backdrop under those runs keeps exactly what it held, unrecoloured.

   The three things that look like defects in the kernels above are here too,
   and are not defects.  The row ends on OR BX,BX / JNZ at 00056bed, an exact-
   zero test on a sixteen-bit counter, and op 01 subtracts the length twice (SUB
   BX,CX at 00056bfc and 00056bff), so a run that overshoots the remaining width
   wraps through 65535 and the decoder keeps consuming command bytes.  Op 01
   writes only the second byte of each destination pair -- INC EDI then STOSB at
   00056c09 -- so the first byte of every pair keeps the surface's own byte,
   untouched and unrecoloured; the obvious dst[2*i] rewrite puts every pixel one
   column to the left of the original.  And the row counter is decremented in
   memory and tested after the decrement (DEC word ptr [0x00070022] / JNZ at
   00056c50), so a request for zero rows draws 0x10000 of them.  The width is
   re-read from the global at the top of every row because MOV BX,[0x00070024]
   at 00056bc2 is where the JNZ at 00056c57 lands. */
void fdps_rle_blit_recolor(unsigned char *rle_stream,
                           unsigned char *dest_pixel,
                           int dest_row_advance,
                           unsigned int recolor_operands)
{
    unsigned char *stream_cursor;
    unsigned char *dest_cursor;
    unsigned char tint_offset;
    unsigned char color_base;
    unsigned char band_mask;
    unsigned short width_remaining;
    unsigned char command;
    unsigned char run_pixel;
    unsigned int run_length;

    stream_cursor = rle_stream;
    dest_cursor = dest_pixel;
    tint_offset = (unsigned char) recolor_operands;
    color_base = (unsigned char) (recolor_operands >> 8);
    band_mask = (unsigned char) (recolor_operands >> 16);

    do {
        width_remaining = data_fdps_graphics_rle_blit_src_width;

        do {
            command = *stream_cursor;
            stream_cursor++;
            run_length = (unsigned int) (command & 0x3f) + 1;

            switch (command >> 6) {
            case 0:
                /* fill: one pixel byte, recoloured once, over run_length bytes */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                run_pixel = (unsigned char)
                            ((((unsigned int) *stream_cursor + tint_offset)
                              & band_mask) + color_base);
                stream_cursor++;
                while (run_length != 0) {
                    *dest_cursor = run_pixel;
                    dest_cursor++;
                    run_length--;
                }
                break;

            case 1:
                /* stretched: the recoloured byte into the second half of each
                   of run_length destination pairs, first halves untouched */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length - run_length);
                run_pixel = (unsigned char)
                            ((((unsigned int) *stream_cursor + tint_offset)
                              & band_mask) + color_base);
                stream_cursor++;
                while (run_length != 0) {
                    dest_cursor++;
                    *dest_cursor = run_pixel;
                    dest_cursor++;
                    run_length--;
                }
                break;

            case 2:
                /* literal: run_length stream bytes, each recoloured on its way */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                while (run_length != 0) {
                    *dest_cursor = (unsigned char)
                                   ((((unsigned int) *stream_cursor
                                      + tint_offset) & band_mask) + color_base);
                    dest_cursor++;
                    stream_cursor++;
                    run_length--;
                }
                break;

            default:
                /* skip: a transparent run, destination stepped over unwritten
                   and unrecoloured, no stream byte consumed */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                dest_cursor += run_length;
                break;
            }
        } while (width_remaining != 0);

        dest_cursor += dest_row_advance;
        data_fdps_graphics_rle_blit_remaining_rows--;
    } while (data_fdps_graphics_rle_blit_remaining_rows != 0);
}

#endif /* RLE_C_REFERENCE */
