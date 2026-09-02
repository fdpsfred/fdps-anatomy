/* rle.c -- the RLE sprite blitters: pass-through, scaled, row-skipping and the
 * two mirrors.
 *
 * See rle.h for the stream format the whole family decodes and for where the
 * row width and the row count come from.
 */
#include "gamedata.h"
#include "rle.h"

/* 00056a0d.  Hand-written assembly, not compiler output: no prologue, ESI is
   the stream, EDI the destination, EDX the row advance, BX the width left in
   the row and CL the command byte.  The C below is the same decode and the
   same arithmetic, not the same registers (ADR-0001), the way
   fdps_xor_crypt_buffer at 000568b7 is.

   The three registers become three parameters.  In the original they are set
   up by fdps_blit_dispatch immediately before the CALL (MOV ESI,[EBP+8], MOV
   EDI,[EBP+0xc], and EDX as [EBP+0x18] - [EBP+0x10] at 000568e7), and nothing
   is passed on the stack; the kernel touches no stack slot of its own and
   never writes into its caller's frame, so making that handoff explicit is the
   whole of the difference.  Two register facts of the original therefore do
   not carry over and must not be reproduced: the caller's XOR ECX,ECX at
   0005690d, which exists because the body writes only CL but then reads the
   whole of CX in SUB BX,CX and the whole of ECX in REP STOSB / REP MOVSB /
   LOOP, and the destruction of EBX, ESI and EDI, which the stack convention
   says a callee preserves.  Both are properties of the register contract, not
   of what the routine draws.

   The four ops are selected by the top two bits, SHL CL,1 / JC twice, and the
   length is the low six bits plus one -- SHR CL,2 / INC CL -- so a run is 1 to
   64 pixels and a length of zero cannot be encoded.

   Three things here look like defects and are not:

   The row ends on OR BX,BX / JNZ, an exact-zero test.  A run that overshoots
   the remaining width wraps the sixteen-bit counter to 0xff.. instead of
   ending the row, and the decoder goes on consuming command bytes until some
   later run happens to land the counter on zero.  Hardening this to
   `width <= 0` changes what such a stream draws, so the counter is an
   unsigned short here and the test stays an equality
   (rebuild_info/pitfalls.md).

   Op 01 writes only the second byte of each destination pair -- INC EDI then
   STOSB at 00056a48 -- and leaves the first byte of every pair holding what
   was already on the surface.  It is not a stretch or a half-tone: over the 99
   .CEL sheets the game ships these runs are almost always one pixel long and
   the untouched column is a deliberate one-pixel hole inside painted content,
   so filling it changes what the game draws.

   The row counter is decremented in memory, and the loop that reads it is a
   do-while: DEC word ptr [0x00070022] / JNZ tests after the decrement, so a
   caller that asks for zero rows gets 0x10000 of them rather than none.  That
   is reproduced rather than guarded, and fdps_blit_dispatch is the only thing
   that sets it.

   The width is re-read from the global at the top of every row rather than
   cached in a local, because that is where MOV BX,[0x00070024] sits -- at the
   loop's entry, which is also the row-restart target. */
void fdps_rle_blit_passthrough(unsigned char *rle_stream,
                               unsigned char *dest_pixel,
                               int dest_row_advance)
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
                /* fill: one pixel byte over run_length destination bytes */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                run_pixel = *stream_cursor;
                stream_cursor++;
                while (run_length != 0) {
                    *dest_cursor = run_pixel;
                    dest_cursor++;
                    run_length--;
                }
                break;

            case 1:
                /* stretched: one pixel byte into the second half of each of
                   run_length destination pairs, first halves untouched */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length - run_length);
                run_pixel = *stream_cursor;
                stream_cursor++;
                while (run_length != 0) {
                    dest_cursor++;
                    *dest_cursor = run_pixel;
                    dest_cursor++;
                    run_length--;
                }
                break;

            case 2:
                /* literal: run_length bytes copied straight out of the stream */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                while (run_length != 0) {
                    *dest_cursor = *stream_cursor;
                    dest_cursor++;
                    stream_cursor++;
                    run_length--;
                }
                break;

            default:
                /* skip: a transparent run, destination stepped over unwritten */
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

/* 00056dc9.  Hand-written assembly like the kernel above, and with the same
   kind of register contract: ESI is the stream on the way in and the advanced
   stream on the way out, BX is the width left in the row and CL the command
   byte.  Here the one register that carries information both ways becomes the
   parameter and the return value; the destruction of BX and CL is a property
   of the register contract and not of what the routine does, so it does not
   carry over (ADR-0001).  Both call sites already treat it that way: each
   restores the row start with POP ESI (00056d84, 000574bc) and then calls
   this once per row it means to drop, reloading its own width and accumulator
   afterwards (MOV BX,[0x00070028] at 00056ca6, MOV DX,[0x00070026] at
   000574bd).

   Why it exists: the scaled blitters step a vertical Bresenham accumulator
   over the source rows, and a row the accumulator says not to draw still has
   to be walked, because the stream gives no way to find row n + 1 except by
   decoding row n.  So this decodes the ops for their byte cost alone and
   never looks at a pixel byte.

   The op selector and the length are the same three instructions as the
   kernel above -- SHL CL,1 / JC twice, then SHR CL,2 / INC CL -- so a run is
   1 to 64 pixels.  What differs per op is only how far the stream moves and
   how much width the run accounts for:

     00 fill       command byte plus one pixel byte, 2 in all; width -= len
     01 stretched  command byte plus one pixel byte, 2 in all; width -= len
                   twice, because the run covers two columns per unit
                   (SUB BX,CX at 00056df8 and 00056dfb)
     10 literal    command byte plus len pixel bytes (ADD ESI,ECX at 00056e14)
     11 skip       the command byte alone; width -= len

   The width is read from the global once, at 00056dc9, above the loop's entry
   at 00056dd0, so this walks exactly one row rather than restarting like the
   drawing kernels.

   Two things about the terminator, both deliberate.  It is OR BX,BX / JNZ, an
   exact-zero test on a sixteen-bit counter, so a run that overshoots the row
   wraps to 0xff.. and the decoder keeps consuming command bytes until some
   later run lands the counter on zero; the counter is an unsigned short here
   and the test stays an equality for that reason (rebuild_info/pitfalls.md).
   And the test is at the bottom: the first op of a row is decoded before the
   width is looked at, so a width of zero on entry walks 0x10000 pixels rather
   than none. */
unsigned char *fdps_rle_skip_row(unsigned char *rle_stream)
{
    unsigned char *stream_cursor;
    unsigned short width_remaining;
    unsigned char command;
    unsigned int run_length;

    stream_cursor = rle_stream;
    width_remaining = data_fdps_graphics_rle_blit_src_width;

    do {
        command = *stream_cursor;
        stream_cursor++;
        run_length = (unsigned int) (command & 0x3f) + 1;

        switch (command >> 6) {
        case 0:
            /* fill: the pixel byte that follows is stepped over unread */
            width_remaining = (unsigned short)
                              (width_remaining - run_length);
            stream_cursor++;
            break;

        case 1:
            /* stretched: one pixel byte, but two columns per unit of length */
            stream_cursor++;
            width_remaining = (unsigned short)
                              (width_remaining - run_length - run_length);
            break;

        case 2:
            /* literal: the run's pixel bytes are in the stream */
            width_remaining = (unsigned short)
                              (width_remaining - run_length);
            stream_cursor += run_length;
            break;

        default:
            /* skip: nothing follows the command byte */
            width_remaining = (unsigned short)
                              (width_remaining - run_length);
            break;
        }
    } while (width_remaining != 0);

    return stream_cursor;
}
