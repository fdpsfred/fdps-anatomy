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

/* 00056c5e.  Blit mode 4, the scaled sprite blit.  Hand-written assembly like
   the kernel above and with the same kind of register contract -- ESI is the
   stream, EDI the destination -- but with a third input that arrives in a way
   the pass-through kernel has no equivalent of: the two scale words are read
   straight out of fdps_blit_dispatch's own frame, MOV DX,[EBP+0x1c] at
   00056c5e and MOV DX,[EBP+0x1e] at 00056c69, which are the low and high
   halves of the dispatcher's sixth argument.  The kernel then zeroes EBP at
   00056c74 and spends the whole routine using it as its Bresenham accumulator
   without ever restoring it; fdps_blit_dispatch survives that only because its
   epilogue pops EBP off ESP rather than relying on it.  Reaching into the
   caller's frame and destroying its frame pointer are properties of the
   register contract and not of what the routine draws, so the two words become
   two parameters here the same way ESI and EDI do (ADR-0001).

   After entry the parameters are not read again.  Everything below reads the
   globals the routine has just published, because that is what the assembly
   reads -- MOV BX,[0x00070028] at the top of every row, ADD BP,[0x00070028] in
   all four ops, ADD BP,[0x0007002a] in the vertical step -- and the block is
   shared with the caller and with fdps_rle_skip_row.

   The row advance is the one place where the obvious C differs from what the
   original stores.  It is computed sixteen bits wide, MOV BP,[0x0007002e] /
   SUB BP,[0x00070028], and then written to the whole dword at 00056c99 with
   EBP's top half still holding the zero from 00056c74.  So a destination wider
   than the pitch does not give a negative advance here: 2 - 4 is stored as
   65534, and the destination cursor walks forward by that.  The global itself
   is signed and does hold negative advances -- fdps_rle_blit_translucent and
   its neighbours store the dispatcher's full 32-bit pitch - width into it at
   0005761b -- which is why the truncation has to be written out rather than
   left to the compiler.

   Both Bresenham steps are the same shape.  Horizontally the accumulator
   starts at the destination width and each op runs one loop: while the
   accumulator is below the source width it adds the destination width and
   consumes one pixel of the run, and otherwise it subtracts the source width
   and emits one destination pixel.  So a source pixel is emitted more than
   once when scaling up and dropped when scaling down, and either exit can end
   the op -- the run running out goes on to the next command byte, the
   destination row filling ends the row with the run half-decoded.

   Which is what makes the source cursor's push and pop load-bearing.  ESI is
   pushed at the top of every destination row (00056cb0) and popped back at
   00056d84, and the stream is only ever advanced to the next source row by
   fdps_rle_skip_row.  Carrying the cursor on from wherever the decode stopped
   would desynchronise the stream on the first row that fills early, and would
   also break the vertical upscale, which re-decodes the same source row from
   its start for as many destination rows as the accumulator calls for.

   Three details that look like defects and are not, all of them inherited from
   the same sixteen-bit exact-zero tests the pass-through kernel uses:

   The destination row ends on DEC BX / JNZ, so a destination width of zero
   gives a row of 0x10000 pixels rather than an empty one, and the row count
   ends on DEC word ptr [0x0007002c] / JNZ, so a destination height of zero
   asks for 0x10000 rows.  Neither is guarded here.

   A destination height of zero also never leaves the vertical loop: the
   accumulator is seeded with it and grows by it, so it stays at zero, which is
   never above the source height, and the routine calls fdps_rle_skip_row for
   ever.  That is the original's behaviour and fdps_blit_dispatch is the only
   thing that reaches it.

   The half-tone op's phase starts at zero (XOR AH,AH at 00056cf6) and flips
   only when a source pixel is consumed, so the first destination pixel of the
   run is stepped over unwritten -- the same one-pixel hole the pass-through
   kernel's op 01 leaves, here spread by the scale.  Its length is doubled
   before the loop (SHL CX,1 at 00056cf2), which is safe in eight bits because
   the length is at most 64 and CH is provably zero: fdps_blit_dispatch clears
   ECX at 0005690d and nothing in either kernel writes above CL.

   The source width is read once, into DX at 00056c9f above the row loop's
   entry at 00056ca6, and has to survive the calls to fdps_rle_skip_row inside
   the loop; it does, because that routine touches only BX, CX, ESI and AL.
   Ghidra's decompiler claims a value comes back from that call in DX
   (extraout_DX) and there is no such thing -- it returns only the advanced
   stream cursor. */
void fdps_rle_blit_scaled(unsigned char *rle_stream,
                          unsigned char *dest_pixel,
                          unsigned short dest_width,
                          unsigned short dest_height)
{
    unsigned char *stream_cursor;
    unsigned char *row_stream_start;
    unsigned char *dest_cursor;
    unsigned short src_width;
    unsigned short dest_pixels_remaining;
    unsigned short hscale_accumulator;
    unsigned short vscale_accumulator;
    unsigned char command;
    unsigned char run_pixel;
    unsigned char run_length;
    unsigned char halftone_phase;
    int row_finished;
    int run_finished;

    data_fdps_graphics_rle_blit_dest_width = dest_width;
    data_fdps_graphics_rle_blit_dest_height = dest_height;
    data_fdps_graphics_rle_blit_dest_rows_remaining =
        data_fdps_graphics_rle_blit_dest_height;
    data_fdps_graphics_rle_blit_vscale_accumulator =
        data_fdps_graphics_rle_blit_dest_height;
    data_fdps_graphics_rle_blit_dst_row_advance =
        (int) (unsigned short) (data_fdps_graphics_rle_blit_dst_pitch
                                - data_fdps_graphics_rle_blit_dest_width);

    stream_cursor = rle_stream;
    dest_cursor = dest_pixel;
    src_width = data_fdps_graphics_rle_blit_src_width;

    do {
        dest_pixels_remaining = data_fdps_graphics_rle_blit_dest_width;
        hscale_accumulator = data_fdps_graphics_rle_blit_dest_width;
        row_stream_start = stream_cursor;
        row_finished = 0;

        do {
            command = *stream_cursor;
            stream_cursor++;
            run_length = (unsigned char) ((command & 0x3f) + 1);
            run_finished = 0;

            switch (command >> 6) {
            case 0:
                /* fill: one pixel byte follows and is the whole run */
                run_pixel = *stream_cursor;
                stream_cursor++;
                while (run_finished == 0) {
                    if (hscale_accumulator < src_width) {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator
                             + data_fdps_graphics_rle_blit_dest_width);
                        run_length--;
                        if (run_length == 0) {
                            run_finished = 1;
                        }
                    } else {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator - src_width);
                        *dest_cursor = run_pixel;
                        dest_cursor++;
                        dest_pixels_remaining--;
                        if (dest_pixels_remaining == 0) {
                            run_finished = 1;
                            row_finished = 1;
                        }
                    }
                }
                break;

            case 1:
                /* half-tone: the run covers twice its length, and only the
                   pixels the phase marks are written */
                run_length = (unsigned char) (run_length * 2);
                run_pixel = *stream_cursor;
                stream_cursor++;
                halftone_phase = 0;
                while (run_finished == 0) {
                    if (hscale_accumulator < src_width) {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator
                             + data_fdps_graphics_rle_blit_dest_width);
                        halftone_phase = (unsigned char) (halftone_phase ^ 1);
                        run_length--;
                        if (run_length == 0) {
                            run_finished = 1;
                        }
                    } else {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator - src_width);
                        if (halftone_phase != 0) {
                            *dest_cursor = run_pixel;
                        }
                        dest_cursor++;
                        dest_pixels_remaining--;
                        if (dest_pixels_remaining == 0) {
                            run_finished = 1;
                            row_finished = 1;
                        }
                    }
                }
                break;

            case 2:
                /* literal: the run's pixel bytes are in the stream, and the
                   cursor steps over one only when a source pixel is consumed
                   (INC ESI at 00056d45), never when one is emitted */
                while (run_finished == 0) {
                    if (hscale_accumulator < src_width) {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator
                             + data_fdps_graphics_rle_blit_dest_width);
                        stream_cursor++;
                        run_length--;
                        if (run_length == 0) {
                            run_finished = 1;
                        }
                    } else {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator - src_width);
                        *dest_cursor = *stream_cursor;
                        dest_cursor++;
                        dest_pixels_remaining--;
                        if (dest_pixels_remaining == 0) {
                            run_finished = 1;
                            row_finished = 1;
                        }
                    }
                }
                break;

            default:
                /* skip: a transparent run, the destination stepped over */
                while (run_finished == 0) {
                    if (hscale_accumulator < src_width) {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator
                             + data_fdps_graphics_rle_blit_dest_width);
                        run_length--;
                        if (run_length == 0) {
                            run_finished = 1;
                        }
                    } else {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator - src_width);
                        dest_cursor++;
                        dest_pixels_remaining--;
                        if (dest_pixels_remaining == 0) {
                            run_finished = 1;
                            row_finished = 1;
                        }
                    }
                }
                break;
            }
        } while (row_finished == 0);

        /* Back to the start of the source row, then the vertical step: every
           source row the accumulator passes over is walked by decoding it, and
           the accumulator lives in the global between rows. */
        stream_cursor = row_stream_start;
        vscale_accumulator = data_fdps_graphics_rle_blit_vscale_accumulator;
        while (vscale_accumulator
               <= data_fdps_graphics_rle_blit_remaining_rows) {
            stream_cursor = fdps_rle_skip_row(stream_cursor);
            vscale_accumulator = (unsigned short)
                (vscale_accumulator
                 + data_fdps_graphics_rle_blit_dest_height);
        }
        data_fdps_graphics_rle_blit_vscale_accumulator = (unsigned short)
            (vscale_accumulator
             - data_fdps_graphics_rle_blit_remaining_rows);

        dest_cursor += data_fdps_graphics_rle_blit_dst_row_advance;
        data_fdps_graphics_rle_blit_dest_rows_remaining--;
    } while (data_fdps_graphics_rle_blit_dest_rows_remaining != 0);
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

/* 00057551.  Blit mode 7, the left-right mirror: the same four ops and the same
   stream consumption as the pass-through kernel at 00056a0d, decoded from the
   right-hand column of the row leftwards, so the drawn rectangle is that
   kernel's output reflected inside the same box.

   Hand-written assembly like its three neighbours above, and with the same kind
   of register contract: no prologue, no epilogue, no stack slot of its own
   beyond the PUSH EDI / POP EDI that saves the row origin, and nothing read out
   of the caller's frame at all.  fdps_blit_dispatch sets ESI and EDI
   immediately before the CALL at 0005699f (MOV ESI,[EBP+8] and MOV EDI,[EBP+0xc]
   at 000568e1 and 000568e4), so those two become the two parameters here.  What
   does not carry over is the rest of the register contract -- the destruction of
   EBX, ECX, EDX, ESI and EDI, and the caller's XOR ECX,ECX at 0005690d, which
   exists only because the body writes CL and then reads the whole of ECX in REP
   STOSB, LOOP and SUB EDI,ECX.  Those are properties of the handoff, not of what
   the routine draws (ADR-0001), the way they are for fdps_rle_blit_passthrough.

   Three things separate this from the mode-0 kernel, and all three are places
   where the obvious rewrite is wrong:

   The destination is not walked backwards from EDI.  EDI arrives at the
   rectangle's top-left corner exactly as it does for mode 0, and this routine
   adds the width and steps back one (ADD EDI,EBX / DEC EDI at 00057564) to reach
   the row's right-hand column itself.  Starting at the incoming pointer and
   walking left would draw the sprite a whole width too far left.

   It ignores the EDX = pitch - width end-of-row advance the dispatcher leaves in
   EDX and reloads the full pitch from the global instead (XOR EDX,EDX / MOV DX,
   word ptr [0x0007002e] at 00057553), because it restores the row origin with
   POP EDI rather than letting the cursor run off the row end.  Reusing the
   siblings' advance here would double-count the width.  The pitch is read once,
   above the row loop's entry at 0005755c, and it is read as a word into a zeroed
   EDX, so it is a zero-extended sixteen-bit value and never a negative advance.

   The stretched op is the one place where the reflection is not a plain index
   flip.  Mode 0 does INC EDI and then STOSB, painting the second column of each
   destination pair; this does DEC EDI, then the store, then DEC EDI again
   (0005759d), painting the columns that are the reflections of those.  Mirroring
   mode 0's loop body without moving the decrement ahead of the store paints the
   complementary set of columns and the sprite comes out one pixel off.

   Everything else is the mode-0 kernel's behaviour and is reproduced rather than
   guarded: the row terminator is OR BX,BX / JNZ, an exact-zero test on a
   sixteen-bit counter, so a run that overshoots the row width wraps it instead
   of ending the row; the width is re-read from the global at the top of every
   row because that is where MOV BX,[0x00070024] sits, at the row-restart target;
   and the row count is decremented in memory by a do-while (DEC word ptr
   [0x00070022] / JNZ at 000575df), so a caller asking for zero rows gets 0x10000
   of them. */
void fdps_rle_blit_mirrored_horizontal(unsigned char *rle_stream,
                                       unsigned char *dest_pixel)
{
    unsigned char *stream_cursor;
    unsigned char *row_origin;
    unsigned char *dest_cursor;
    unsigned int dest_row_pitch;
    unsigned short width_remaining;
    unsigned char command;
    unsigned char run_pixel;
    unsigned int run_length;

    stream_cursor = rle_stream;
    row_origin = dest_pixel;
    dest_row_pitch = (unsigned int) data_fdps_graphics_rle_blit_dst_pitch;

    do {
        width_remaining = data_fdps_graphics_rle_blit_src_width;
        dest_cursor = row_origin + width_remaining;
        dest_cursor--;

        do {
            command = *stream_cursor;
            stream_cursor++;
            run_length = (unsigned int) (command & 0x3f) + 1;

            switch (command >> 6) {
            case 0:
                /* fill: STD / REP STOSB / CLD, one pixel byte over run_length
                   columns walking left */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                run_pixel = *stream_cursor;
                stream_cursor++;
                while (run_length != 0) {
                    *dest_cursor = run_pixel;
                    dest_cursor--;
                    run_length--;
                }
                break;

            case 1:
                /* stretched: one pixel byte into the FIRST byte of each of
                   run_length destination pairs counted leftwards, which is the
                   reflection of the second byte of each pair mode 0 writes */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length - run_length);
                run_pixel = *stream_cursor;
                stream_cursor++;
                while (run_length != 0) {
                    dest_cursor--;
                    *dest_cursor = run_pixel;
                    dest_cursor--;
                    run_length--;
                }
                break;

            case 2:
                /* literal: run_length bytes out of the stream, the stream read
                   forwards and the destination written leftwards */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                while (run_length != 0) {
                    *dest_cursor = *stream_cursor;
                    dest_cursor--;
                    stream_cursor++;
                    run_length--;
                }
                break;

            default:
                /* skip: SUB EDI,ECX, a transparent run stepped over unwritten */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                dest_cursor -= run_length;
                break;
            }
        } while (width_remaining != 0);

        row_origin += dest_row_pitch;
        data_fdps_graphics_rle_blit_remaining_rows--;
    } while (data_fdps_graphics_rle_blit_remaining_rows != 0);
}

/* 000575ed.  Blit mode 8, the top-bottom mirror.  Hand-written assembly like
   its neighbours above, and the shortest of the family: twelve instructions
   that draw nothing at all.  It sets the destination cursor and the row advance
   and then tail-jumps into fdps_rle_blit_passthrough (JMP 0x00056a0d at
   00057616), so the whole reflection is mode 0's own decode run backwards up
   the rectangle.  The JMP is a tail call and becomes an ordinary call here:
   which of the two the compiler emits is codegen and not behaviour (ADR-0001),
   and the plain RET that ends the pass-through kernel returns to
   fdps_blit_dispatch either way.

   The register contract is the family's: ESI is the stream and EDI the
   rectangle's top-left corner, both set up by fdps_blit_dispatch before the
   CALL at 000569b2, so those two become the parameters.  It discards the
   pitch - width advance the dispatcher leaves in EDX for the sibling kernels
   and builds its own.

   Both of its numbers are arrived at in a way the obvious rewrite gets wrong:

   The starting displacement is pitch * (rows - 1), not pitch * rows.  DEC DX at
   000575fe takes one off the row count before MUL EDX, because the mirrored
   image has to fill the very same rectangle an unmirrored blit would; stepping
   down by a full pitch per row puts the whole sprite one row too low.

   The upward advance is -(pitch + width), not -pitch.  The pass-through kernel
   adds the advance at 00056a81, after the cursor has already walked one whole
   width across the row, so the width has to be paid back as well.  An advance
   of -pitch staggers the image one width further left on every row.

   Both counts are sixteen bits and both wrap rather than saturating, which is
   reproduced rather than guarded.  DEC DX on the row count is a sixteen-bit
   decrement, so a row count of zero displaces the destination by 65535 pitches
   and the kernel then draws 65536 rows from there; ADD DX at 0005760d is a
   sixteen-bit add into a zeroed EDX, so a pitch plus width that exceeds 65535
   is negated after truncation and the advance is not the arithmetic sum.  The
   multiply itself cannot overflow: 65535 * 65535 fits the low half MUL leaves
   in EAX.

   The row count global is only read here.  DEC DX steps the register copy, so
   the value the pass-through kernel finds when it starts is the full count. */
void fdps_rle_blit_mirrored_vertical(unsigned char *rle_stream,
                                     unsigned char *dest_pixel)
{
    unsigned int dest_row_pitch;
    unsigned int rows_above_last;
    unsigned char *bottom_row_pixel;
    int upward_row_advance;

    dest_row_pitch = (unsigned int) data_fdps_graphics_rle_blit_dst_pitch;
    rows_above_last = (unsigned int) (unsigned short)
                      (data_fdps_graphics_rle_blit_remaining_rows - 1);
    bottom_row_pixel = dest_pixel + dest_row_pitch * rows_above_last;

    upward_row_advance = -(int) (unsigned short)
                         (data_fdps_graphics_rle_blit_dst_pitch +
                          data_fdps_graphics_rle_blit_src_width);

    fdps_rle_blit_passthrough(rle_stream, bottom_row_pixel,
                              upward_row_advance);
}
