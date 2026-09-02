/* rlerot.c -- the rotating RLE sprite blitters.
 *
 * See rlerot.h for the vector pair the rotation is built out of, for the six
 * step globals the kernels publish on entry, and for where the source rectangle
 * and the destination pitch come from.
 */
#include "gamedata.h"
#include "rlerot.h"

/* 00056e2a.  Blit mode 5.  Hand-written assembly, not compiler output: no
   prologue, ESI is the stream, EDI the destination, BX the width left in the
   row, CL the command byte and DX and BP the two within-row fractional
   accumulators.  The C below is the same decode and the same arithmetic, not
   the same registers (ADR-0001), the way fdps_rle_blit_passthrough at 00056a0d
   is.

   Four inputs become four parameters.  Two of them are the registers
   fdps_blit_dispatch loads immediately before the CALL, MOV ESI,[EBP+8] and MOV
   EDI,[EBP+0xc] at 000568e1; the other two are read out of the dispatcher's
   live frame, MOV DX,[EBP+0x1c] and MOV AX,[EBP+0x1e] at 00056e3c, which are
   the low and high halves of the dispatcher's sixth argument.  The routine then
   zeroes EBP at 00056e44 and uses it first for the destination pitch and then
   as its vertical accumulator without ever restoring it; fdps_blit_dispatch
   survives that only because its epilogue pops EBP off ESP rather than relying
   on it.  Reaching into the caller's frame and destroying its frame pointer are
   properties of the register contract and not of what the routine draws, and so
   are the caller's XOR ECX,ECX at 0005690d -- which exists because the body
   writes only CL but then reads CX in SUB BX,CX and the whole of ECX in LOOP --
   and the destruction of EBX, ESI, EDI and EBP, which the stack convention says
   a callee preserves.

   The sign folding on entry is four arms on the two signed sixteen-bit tests
   CMP DX,0 / JG and CMP AX,0 / JG, and every arm writes the same four step
   globals with that quadrant's signs already in them.  The arms are the four
   quadrants of one vector, not four mirroring modes: dx > 0 chooses the sign of
   the two x steps and dy > 0 the sign of the two y steps, the pixel step in y
   taking the opposite sign to the row step in x so that the two vectors come
   out perpendicular.  Whichever arm runs, |dx| and |dy| are what is stored at
   0007004c and 0007004e (MOV [0x0007004c],DX / MOV [0x0007004e],AX at 00056f0c
   after the arms have joined), so the loops below work in magnitudes only.
   NEG is a sixteen-bit negate here, so a dx of -32768 stays 0x8000 and is
   carried as a magnitude of 32768; the C keeps that by truncating to unsigned
   short.

   Three things a faithful rewrite has to keep, all of them in the per-pixel
   step that every one of the four ops repeats:

   Each accumulator subtracts a single 0x1000 when it reaches 0x1000, SUB
   DX,0x1000 rather than a modulo, so an increment larger than 0x1000 still
   advances the destination by at most one pixel per source pixel.  The obvious
   `accumulator += dx; cursor += accumulator >> 12; accumulator &= 0xfff`
   magnifies where the original clamps and turns the mode into a different
   transform: with dx at 0x3000 the original walks 1, 1, 1 and that spelling
   walks 3, 3, 3 (rebuild_info/pitfalls.md).

   The vertical carry stores the pixel a second time at the cursor it has not
   yet stepped (MOV [EDI],AL at 00056f79, 00056fe1 and 00057043) and only then
   moves the cursor by the y step.  It looks like a redundant write and is the
   gap filler: on a step that crosses both a column and a row it paints the
   horizontal neighbour as well as the vertical one, so a rotated run stays
   connected instead of dotting.  Op 11 is the one op that does not do it,
   because it stores nothing at all.

   The two row-to-row accumulators are cleared once, at 00056e2a and 00056e33
   before the first row, and carry from one source row to the next; the two
   within-row accumulators are zeroed at the top of every row instead (XOR DX,DX
   / XOR BP,BP at 00056f20).  Clearing the row pair per row as well would drop
   the fractional part of the perpendicular vector and turn the rotation into a
   shear.

   Two more inherited details that look like defects and are not.  The row ends
   on OR BX,BX / JNZ, an exact-zero test, so a run that overshoots the remaining
   width wraps the sixteen-bit counter instead of ending the row and the decoder
   goes on consuming command bytes until some later run lands it on zero; the
   counter is an unsigned short here and the test stays an equality.  And the
   row counter is decremented in memory below the row advance, DEC word ptr
   [0x00070022] / JNZ, so a caller that asks for zero rows gets 0x10000 of them
   and the row origin is stepped once even on the last row.

   Nothing in the shipped executable selects mode 5, so none of the above can be
   confirmed by playing the game: it is transcribed from the assembly and rests
   on the assembly's authority. */
void fdps_rle_blit_rotated(unsigned char *rle_stream,
                           unsigned char *dest_pixel,
                           short rotate_dx,
                           short rotate_dy)
{
    unsigned char *stream_cursor;
    unsigned char *row_origin;
    unsigned char *dest_cursor;
    int dest_pitch;
    unsigned short dx_magnitude;
    unsigned short dy_magnitude;
    unsigned short width_remaining;
    unsigned short pixel_step_x_accumulator;
    unsigned short pixel_step_y_accumulator;
    unsigned short row_step_accumulator;
    unsigned char command;
    unsigned char run_pixel;
    unsigned char halftone_phase;
    unsigned int run_length;

    data_fdps_graphics_rle_blit_rot_row_step_x_accumulator = 0;
    data_fdps_graphics_rle_blit_rot_row_step_y_accumulator = 0;

    /* Zero-extended: XOR EBP,EBP then MOV BP,[0x0007002e] at 00056e44. */
    dest_pitch = (int) data_fdps_graphics_rle_blit_dst_pitch;

    if (rotate_dx > 0) {
        if (rotate_dy > 0) {
            dx_magnitude = (unsigned short) rotate_dx;
            dy_magnitude = (unsigned short) rotate_dy;
            data_fdps_graphics_rle_rotate_dst_x_step_per_src_x = 1;
            data_fdps_graphics_rle_rotate_dst_y_step_per_src_y = dest_pitch;
            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y = -dest_pitch;
            data_fdps_graphics_rle_blit_rot_row_dest_step_x = 1;
        } else {
            dx_magnitude = (unsigned short) rotate_dx;
            dy_magnitude = (unsigned short) -rotate_dy;
            data_fdps_graphics_rle_rotate_dst_x_step_per_src_x = 1;
            data_fdps_graphics_rle_rotate_dst_y_step_per_src_y = dest_pitch;
            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y = dest_pitch;
            data_fdps_graphics_rle_blit_rot_row_dest_step_x = -1;
        }
    } else {
        if (rotate_dy > 0) {
            dx_magnitude = (unsigned short) -rotate_dx;
            dy_magnitude = (unsigned short) rotate_dy;
            data_fdps_graphics_rle_rotate_dst_x_step_per_src_x = -1;
            data_fdps_graphics_rle_rotate_dst_y_step_per_src_y = -dest_pitch;
            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y = -dest_pitch;
            data_fdps_graphics_rle_blit_rot_row_dest_step_x = 1;
        } else {
            dx_magnitude = (unsigned short) -rotate_dx;
            dy_magnitude = (unsigned short) -rotate_dy;
            data_fdps_graphics_rle_rotate_dst_x_step_per_src_x = -1;
            data_fdps_graphics_rle_rotate_dst_y_step_per_src_y = -dest_pitch;
            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y = dest_pitch;
            data_fdps_graphics_rle_blit_rot_row_dest_step_x = -1;
        }
    }

    data_fdps_graphics_rle_rotate_cos_magnitude = dx_magnitude;
    data_fdps_graphics_rle_rotate_sin_magnitude = dy_magnitude;

    stream_cursor = rle_stream;
    row_origin = dest_pixel;

    do {
        width_remaining = data_fdps_graphics_rle_blit_src_width;
        pixel_step_x_accumulator = 0;
        pixel_step_y_accumulator = 0;
        /* PUSH EDI at 00056f26: the row's origin is kept and the row draws
           from a copy of it. */
        dest_cursor = row_origin;

        do {
            command = *stream_cursor;
            stream_cursor++;
            run_length = (unsigned int) (command & 0x3f) + 1;

            switch (command >> 6) {
            case 0:
                /* fill: one pixel byte follows and is stored at every step of
                   the run */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                run_pixel = *stream_cursor;
                stream_cursor++;
                while (run_length != 0) {
                    *dest_cursor = run_pixel;
                    pixel_step_x_accumulator = (unsigned short)
                        (pixel_step_x_accumulator
                         + data_fdps_graphics_rle_rotate_cos_magnitude);
                    if (pixel_step_x_accumulator >= 0x1000) {
                        pixel_step_x_accumulator = (unsigned short)
                            (pixel_step_x_accumulator - 0x1000);
                        dest_cursor +=
                            data_fdps_graphics_rle_rotate_dst_x_step_per_src_x;
                    }
                    pixel_step_y_accumulator = (unsigned short)
                        (pixel_step_y_accumulator
                         + data_fdps_graphics_rle_rotate_sin_magnitude);
                    if (pixel_step_y_accumulator >= 0x1000) {
                        pixel_step_y_accumulator = (unsigned short)
                            (pixel_step_y_accumulator - 0x1000);
                        *dest_cursor = run_pixel;
                        dest_cursor +=
                            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y;
                    }
                    run_length--;
                }
                break;

            case 1:
                /* half-tone: the run covers two columns per unit of length and
                   the phase suppresses the store on the first of every pair,
                   so only the second is painted */
                run_length = run_length * 2;
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                run_pixel = *stream_cursor;
                stream_cursor++;
                halftone_phase = 0;
                while (run_length != 0) {
                    if (halftone_phase != 0) {
                        *dest_cursor = run_pixel;
                    }
                    pixel_step_x_accumulator = (unsigned short)
                        (pixel_step_x_accumulator
                         + data_fdps_graphics_rle_rotate_cos_magnitude);
                    if (pixel_step_x_accumulator >= 0x1000) {
                        pixel_step_x_accumulator = (unsigned short)
                            (pixel_step_x_accumulator - 0x1000);
                        dest_cursor +=
                            data_fdps_graphics_rle_rotate_dst_x_step_per_src_x;
                    }
                    pixel_step_y_accumulator = (unsigned short)
                        (pixel_step_y_accumulator
                         + data_fdps_graphics_rle_rotate_sin_magnitude);
                    if (pixel_step_y_accumulator >= 0x1000) {
                        pixel_step_y_accumulator = (unsigned short)
                            (pixel_step_y_accumulator - 0x1000);
                        if (halftone_phase != 0) {
                            *dest_cursor = run_pixel;
                        }
                        dest_cursor +=
                            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y;
                    }
                    halftone_phase = (unsigned char) (halftone_phase ^ 1);
                    run_length--;
                }
                break;

            case 2:
                /* literal: one pixel byte out of the stream per step */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                while (run_length != 0) {
                    run_pixel = *stream_cursor;
                    stream_cursor++;
                    *dest_cursor = run_pixel;
                    pixel_step_x_accumulator = (unsigned short)
                        (pixel_step_x_accumulator
                         + data_fdps_graphics_rle_rotate_cos_magnitude);
                    if (pixel_step_x_accumulator >= 0x1000) {
                        pixel_step_x_accumulator = (unsigned short)
                            (pixel_step_x_accumulator - 0x1000);
                        dest_cursor +=
                            data_fdps_graphics_rle_rotate_dst_x_step_per_src_x;
                    }
                    pixel_step_y_accumulator = (unsigned short)
                        (pixel_step_y_accumulator
                         + data_fdps_graphics_rle_rotate_sin_magnitude);
                    if (pixel_step_y_accumulator >= 0x1000) {
                        pixel_step_y_accumulator = (unsigned short)
                            (pixel_step_y_accumulator - 0x1000);
                        *dest_cursor = run_pixel;
                        dest_cursor +=
                            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y;
                    }
                    run_length--;
                }
                break;

            default:
                /* skip: a transparent run, the destination walked over
                   unwritten -- the only op with no gap-filling store either */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                while (run_length != 0) {
                    pixel_step_x_accumulator = (unsigned short)
                        (pixel_step_x_accumulator
                         + data_fdps_graphics_rle_rotate_cos_magnitude);
                    if (pixel_step_x_accumulator >= 0x1000) {
                        pixel_step_x_accumulator = (unsigned short)
                            (pixel_step_x_accumulator - 0x1000);
                        dest_cursor +=
                            data_fdps_graphics_rle_rotate_dst_x_step_per_src_x;
                    }
                    pixel_step_y_accumulator = (unsigned short)
                        (pixel_step_y_accumulator
                         + data_fdps_graphics_rle_rotate_sin_magnitude);
                    if (pixel_step_y_accumulator >= 0x1000) {
                        pixel_step_y_accumulator = (unsigned short)
                            (pixel_step_y_accumulator - 0x1000);
                        dest_cursor +=
                            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y;
                    }
                    run_length--;
                }
                break;
            }
        } while (width_remaining != 0);

        /* POP EDI at 000570a8, then the perpendicular vector: the row origin
           gathers |dy| towards a step in x and |dx| towards a step in y, both
           accumulators living in memory across the whole blit.  The MOV
           DX,[0x00070026] at 000570a9 is dead -- DX is reloaded from
           0x00070048 before any use -- so it has no counterpart here. */
        row_step_accumulator = (unsigned short)
            (data_fdps_graphics_rle_blit_rot_row_step_x_accumulator
             + data_fdps_graphics_rle_rotate_sin_magnitude);
        if (row_step_accumulator >= 0x1000) {
            row_step_accumulator = (unsigned short)
                (row_step_accumulator - 0x1000);
            row_origin += data_fdps_graphics_rle_blit_rot_row_dest_step_x;
        }
        data_fdps_graphics_rle_blit_rot_row_step_x_accumulator =
            row_step_accumulator;

        row_step_accumulator = (unsigned short)
            (data_fdps_graphics_rle_blit_rot_row_step_y_accumulator
             + data_fdps_graphics_rle_rotate_cos_magnitude);
        if (row_step_accumulator >= 0x1000) {
            row_step_accumulator = (unsigned short)
                (row_step_accumulator - 0x1000);
            row_origin += data_fdps_graphics_rle_rotate_dst_y_step_per_src_y;
        }
        data_fdps_graphics_rle_blit_rot_row_step_y_accumulator =
            row_step_accumulator;

        data_fdps_graphics_rle_blit_remaining_rows--;
    } while (data_fdps_graphics_rle_blit_remaining_rows != 0);
}
