/* rlerot.c -- the rotating RLE sprite blitters.
 *
 * See rlerot.h for the vector pair the rotation is built out of, for the six
 * step globals the kernels publish on entry, and for where the source rectangle
 * and the destination pitch come from.
 */
#include "gamedata.h"
#include "rle.h"
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

/* 00057114.  Blit mode 6.  Hand-written assembly like mode 5 above, and with
   the same kind of register contract -- ESI is the stream, EDI the destination
   -- but its third input arrives one indirection further out.  MOV
   EBP,[EBP+0x1c] at 00057114 does not read the dispatcher's sixth argument as
   a pair of packed words the way modes 4 and 5 do; it reads it as a POINTER,
   replaces the frame pointer with it, and then pulls four words out of the
   record it addresses (MOV AX,[EBP], [EBP+4], MOV DX,[EBP+8], MOV AX,[EBP+0xc]
   at 00057117 to 00057133).  So the argument becomes one parameter here and
   the four loads stay inside the routine, which is where the assembly does
   them; only the record's slot stride is visible, four bytes with the top half
   of each never read.  Destroying EBP and reaching into the caller's frame are
   properties of the register contract and not of what the routine draws
   (ADR-0001), as are the destruction of EBX, ESI and EDI and the requirement
   that the caller have cleared the top of ECX.

   What mode 6 adds to mode 5 is a Bresenham counter in each axis, the same
   pair fdps_rle_blit_scaled runs, so the rectangle being walked is the
   destination rectangle rather than the source one:

   Horizontally, BX counts destination pixels down from the destination width
   and BP is the accumulator, seeded with the destination width by MOV BP,BX at
   00057236.  While BP is below the source width the run gives up one source
   column (BP += destination width, CL down by one, and the run ends into the
   next command byte when CL reaches zero); once BP reaches it, BP -= source
   width and one destination pixel is emitted, BX down by one.  Either exit can
   end the op, and BX reaching zero ends the whole row with the run
   half-decoded.

   Vertically, the accumulator at 00070026 is seeded with the destination
   height and compared against the SOURCE height at 00070022 after every
   destination row: every source row it is still at or below is walked by
   fdps_rle_skip_row and costs it one destination height.  So a destination
   taller than the source skips nothing and re-decodes the same source row, and
   a shorter one drops rows.  Unlike mode 5 this kernel never touches
   data_fdps_graphics_rle_blit_remaining_rows -- it reads it as the source
   height and counts its own destination rows down in
   data_fdps_graphics_rle_blit_dest_rows_remaining instead.

   Which is what makes the push and pop at 0005724b and 000574bb load-bearing
   in both registers at once.  ESI is restored so the next destination row
   starts from this row's first command byte and only fdps_rle_skip_row ever
   advances it to another source row -- carrying the cursor on from wherever
   the decode stopped would desynchronise the stream on the first row that
   fills early, and would break the vertical upscale as well.  EDI is restored
   because the row's origin, not the cursor the rotation has walked away to, is
   what the perpendicular row vector steps.

   Everything the rotation itself does is mode 5's, with one difference: the
   two within-row fractional accumulators live in memory at 00070044 and
   00070046 rather than in DX and BP, because both registers are needed for the
   resampling counters.  They are zeroed at the top of every row (00057239),
   they subtract a single 0x1000 on carry rather than taking a modulo, and the
   vertical carry still stores the pixel a second time at the cursor it has not
   yet stepped (000572dc, 00057381, 0005741c) as the diagonal gap filler.  The
   row-to-row pair at 00070048 and 0007004a is still cleared once, at 00057133,
   and still carries across rows.

   Two inherited details that look like defects and are not.  The destination
   row ends on DEC BX / JNZ and the blit on DEC word ptr [0x0007002c] / JNZ,
   both exact-zero tests on sixteen-bit counters, so a destination width or
   height of zero asks for 0x10000 rather than none.  And a destination height
   of zero also never leaves the vertical loop: the accumulator is seeded with
   it and grows by it, so it stays at zero, which is never above the source
   height, and fdps_rle_skip_row is called for ever.  Neither is guarded, here
   or in mode 4.

   Ghidra's decompiler claims a value comes back from the fdps_rle_skip_row
   call in DX (extraout_DX) and there is no such thing.  The assembly holds the
   vertical accumulator in DX across the call and adds to it afterwards (CALL
   0x00056dc9 / ADD DX,[0x0007002a] at 000574d1); that routine touches only BX,
   CX, ESI and AL, and what it returns is the advanced stream cursor, which is
   ESI.  Its decompiled setup is wrong in one more place worth naming: it
   assigns the record's second word to the vertical accumulator global, where
   the assembly stores it to the destination height at 0007002a and only later
   copies that into the accumulator.

   Nothing in the shipped executable selects mode 6, so none of this can be
   confirmed by playing the game: it is transcribed from the assembly and rests
   on the assembly's authority. */
void fdps_rle_blit_rotated_scaled(unsigned char *rle_stream,
                                  unsigned char *dest_pixel,
                                  int *blit_geometry)
{
    unsigned char *stream_cursor;
    unsigned char *row_stream_start;
    unsigned char *dest_cursor;
    unsigned char *row_origin;
    int dest_pitch;
    short rotate_dx;
    short rotate_dy;
    unsigned short dx_magnitude;
    unsigned short dy_magnitude;
    unsigned short dest_pixels_remaining;
    unsigned short hscale_accumulator;
    unsigned short vscale_accumulator;
    unsigned short pixel_step_accumulator;
    unsigned short row_step_accumulator;
    unsigned char command;
    unsigned char run_pixel;
    unsigned char run_length;
    unsigned char halftone_phase;
    int row_finished;
    int run_finished;

    /* The record's four slots, only the low word of each ever read. */
    data_fdps_graphics_rle_blit_dest_width =
        (unsigned short) blit_geometry[0];
    data_fdps_graphics_rle_blit_dest_height =
        (unsigned short) blit_geometry[1];
    rotate_dx = (short) blit_geometry[2];
    rotate_dy = (short) blit_geometry[3];

    data_fdps_graphics_rle_blit_rot_row_step_x_accumulator = 0;
    data_fdps_graphics_rle_blit_rot_row_step_y_accumulator = 0;

    /* Zero-extended: XOR EBP,EBP then MOV BP,[0x0007002e] at 00057145. */
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
            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y = dest_pitch;
            data_fdps_graphics_rle_blit_rot_row_dest_step_x = -1;
            data_fdps_graphics_rle_rotate_dst_y_step_per_src_y = dest_pitch;
        }
    } else {
        if (rotate_dy > 0) {
            dx_magnitude = (unsigned short) -rotate_dx;
            dy_magnitude = (unsigned short) rotate_dy;
            data_fdps_graphics_rle_rotate_dst_x_step_per_src_x = -1;
            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y = -dest_pitch;
            data_fdps_graphics_rle_rotate_dst_y_step_per_src_y = -dest_pitch;
            data_fdps_graphics_rle_blit_rot_row_dest_step_x = 1;
        } else {
            dx_magnitude = (unsigned short) -rotate_dx;
            dy_magnitude = (unsigned short) -rotate_dy;
            data_fdps_graphics_rle_rotate_dst_x_step_per_src_x = -1;
            data_fdps_graphics_rle_blit_rotated_src_pixel_step_y = dest_pitch;
            data_fdps_graphics_rle_blit_rot_row_dest_step_x = -1;
            data_fdps_graphics_rle_rotate_dst_y_step_per_src_y = -dest_pitch;
        }
    }

    data_fdps_graphics_rle_rotate_cos_magnitude = dx_magnitude;
    data_fdps_graphics_rle_rotate_sin_magnitude = dy_magnitude;
    data_fdps_graphics_rle_blit_dest_rows_remaining =
        data_fdps_graphics_rle_blit_dest_height;
    data_fdps_graphics_rle_blit_vscale_accumulator =
        data_fdps_graphics_rle_blit_dest_height;

    stream_cursor = rle_stream;
    row_origin = dest_pixel;

    do {
        dest_pixels_remaining = data_fdps_graphics_rle_blit_dest_width;
        /* MOV BP,BX at 00057236: the horizontal accumulator starts at the
           destination width, the same seed mode 4 uses. */
        hscale_accumulator = data_fdps_graphics_rle_blit_dest_width;
        data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator = 0;
        data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator = 0;
        /* PUSH ESI / PUSH EDI at 0005724b: the row's stream position and its
           origin are both kept and both restored at the end of the row. */
        row_stream_start = stream_cursor;
        dest_cursor = row_origin;
        row_finished = 0;

        do {
            command = *stream_cursor;
            stream_cursor++;
            run_length = (unsigned char) ((command & 0x3f) + 1);
            run_finished = 0;

            switch (command >> 6) {
            case 0:
                /* fill: one pixel byte follows and is the value of every
                   source column the run covers */
                run_pixel = *stream_cursor;
                stream_cursor++;
                while (run_finished == 0) {
                    if (hscale_accumulator
                        < data_fdps_graphics_rle_blit_src_width) {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator
                             + data_fdps_graphics_rle_blit_dest_width);
                        run_length--;
                        if (run_length == 0) {
                            run_finished = 1;
                        }
                    } else {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator
                             - data_fdps_graphics_rle_blit_src_width);
                        *dest_cursor = run_pixel;
                        pixel_step_accumulator = (unsigned short)
                            (data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator
                             + data_fdps_graphics_rle_rotate_cos_magnitude);
                        if (pixel_step_accumulator >= 0x1000) {
                            pixel_step_accumulator = (unsigned short)
                                (pixel_step_accumulator - 0x1000);
                            dest_cursor +=
                                data_fdps_graphics_rle_rotate_dst_x_step_per_src_x;
                        }
                        data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator =
                            pixel_step_accumulator;
                        pixel_step_accumulator = (unsigned short)
                            (data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator
                             + data_fdps_graphics_rle_rotate_sin_magnitude);
                        if (pixel_step_accumulator >= 0x1000) {
                            pixel_step_accumulator = (unsigned short)
                                (pixel_step_accumulator - 0x1000);
                            *dest_cursor = run_pixel;
                            dest_cursor +=
                                data_fdps_graphics_rle_blit_rotated_src_pixel_step_y;
                        }
                        data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator =
                            pixel_step_accumulator;
                        dest_pixels_remaining--;
                        if (dest_pixels_remaining == 0) {
                            run_finished = 1;
                            row_finished = 1;
                        }
                    }
                }
                break;

            case 1:
                /* half-tone: the run covers two source columns per unit of
                   length and the phase, flipped as each column is consumed,
                   suppresses the store on the first of every pair */
                run_length = (unsigned char) (run_length * 2);
                run_pixel = *stream_cursor;
                stream_cursor++;
                halftone_phase = 0;
                while (run_finished == 0) {
                    if (hscale_accumulator
                        < data_fdps_graphics_rle_blit_src_width) {
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
                            (hscale_accumulator
                             - data_fdps_graphics_rle_blit_src_width);
                        if (halftone_phase != 0) {
                            *dest_cursor = run_pixel;
                        }
                        pixel_step_accumulator = (unsigned short)
                            (data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator
                             + data_fdps_graphics_rle_rotate_cos_magnitude);
                        if (pixel_step_accumulator >= 0x1000) {
                            pixel_step_accumulator = (unsigned short)
                                (pixel_step_accumulator - 0x1000);
                            dest_cursor +=
                                data_fdps_graphics_rle_rotate_dst_x_step_per_src_x;
                        }
                        data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator =
                            pixel_step_accumulator;
                        pixel_step_accumulator = (unsigned short)
                            (data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator
                             + data_fdps_graphics_rle_rotate_sin_magnitude);
                        if (pixel_step_accumulator >= 0x1000) {
                            pixel_step_accumulator = (unsigned short)
                                (pixel_step_accumulator - 0x1000);
                            if (halftone_phase != 0) {
                                *dest_cursor = run_pixel;
                            }
                            dest_cursor +=
                                data_fdps_graphics_rle_blit_rotated_src_pixel_step_y;
                        }
                        data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator =
                            pixel_step_accumulator;
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
                   cursor steps over one only when a source column is consumed
                   (INC ESI at 000573be), never when a pixel is emitted -- so
                   MOV AL,[ESI] at 000573cf can hand the same source byte to
                   several destination pixels */
                while (run_finished == 0) {
                    if (hscale_accumulator
                        < data_fdps_graphics_rle_blit_src_width) {
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
                            (hscale_accumulator
                             - data_fdps_graphics_rle_blit_src_width);
                        run_pixel = *stream_cursor;
                        *dest_cursor = run_pixel;
                        pixel_step_accumulator = (unsigned short)
                            (data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator
                             + data_fdps_graphics_rle_rotate_cos_magnitude);
                        if (pixel_step_accumulator >= 0x1000) {
                            pixel_step_accumulator = (unsigned short)
                                (pixel_step_accumulator - 0x1000);
                            dest_cursor +=
                                data_fdps_graphics_rle_rotate_dst_x_step_per_src_x;
                        }
                        data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator =
                            pixel_step_accumulator;
                        pixel_step_accumulator = (unsigned short)
                            (data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator
                             + data_fdps_graphics_rle_rotate_sin_magnitude);
                        if (pixel_step_accumulator >= 0x1000) {
                            pixel_step_accumulator = (unsigned short)
                                (pixel_step_accumulator - 0x1000);
                            *dest_cursor = run_pixel;
                            dest_cursor +=
                                data_fdps_graphics_rle_blit_rotated_src_pixel_step_y;
                        }
                        data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator =
                            pixel_step_accumulator;
                        dest_pixels_remaining--;
                        if (dest_pixels_remaining == 0) {
                            run_finished = 1;
                            row_finished = 1;
                        }
                    }
                }
                break;

            default:
                /* skip: a transparent run, the destination walked over
                   unwritten -- the only op with no gap-filling store either */
                while (run_finished == 0) {
                    if (hscale_accumulator
                        < data_fdps_graphics_rle_blit_src_width) {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator
                             + data_fdps_graphics_rle_blit_dest_width);
                        run_length--;
                        if (run_length == 0) {
                            run_finished = 1;
                        }
                    } else {
                        hscale_accumulator = (unsigned short)
                            (hscale_accumulator
                             - data_fdps_graphics_rle_blit_src_width);
                        pixel_step_accumulator = (unsigned short)
                            (data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator
                             + data_fdps_graphics_rle_rotate_cos_magnitude);
                        if (pixel_step_accumulator >= 0x1000) {
                            pixel_step_accumulator = (unsigned short)
                                (pixel_step_accumulator - 0x1000);
                            dest_cursor +=
                                data_fdps_graphics_rle_rotate_dst_x_step_per_src_x;
                        }
                        data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator =
                            pixel_step_accumulator;
                        pixel_step_accumulator = (unsigned short)
                            (data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator
                             + data_fdps_graphics_rle_rotate_sin_magnitude);
                        if (pixel_step_accumulator >= 0x1000) {
                            pixel_step_accumulator = (unsigned short)
                                (pixel_step_accumulator - 0x1000);
                            dest_cursor +=
                                data_fdps_graphics_rle_blit_rotated_src_pixel_step_y;
                        }
                        data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator =
                            pixel_step_accumulator;
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

        /* POP EDI / POP ESI at 000574bb, then the vertical step.  The
           accumulator is compared against the SOURCE height, which this kernel
           reads and never decrements, and every source row it is still at or
           below is walked by decoding it. */
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

        /* Then the perpendicular vector, applied to the row's origin rather
           than to the cursor the row walked away with. */
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

        data_fdps_graphics_rle_blit_dest_rows_remaining--;
    } while (data_fdps_graphics_rle_blit_dest_rows_remaining != 0);
}
