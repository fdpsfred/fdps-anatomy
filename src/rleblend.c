/* rleblend.c -- the RLE blit kernels that blend as they draw: the translucent
 * kernel, the two tinting kernels and the colour-range blend.
 *
 * See rleblend.h for what the four ops mean here, where the row width and the
 * row count come from, and what the record the dispatcher's sixth argument
 * points at carries.
 */
#include "gamedata.h"
#include "rleblend.h"

/* One row of the shade ramp is 256 dwords, and the two halves of the table are
   nine rows apart: SHL ECX,0xa at 00057648 scales a row index into 0x400 bytes
   and the 0x2400 of 0005762f is 9 * 0x100 entries, not 0x2400 of them.  Both
   are counted in entries here because the pointer arithmetic below is on
   dwords. */
#define SHADE_RAMP_ROW_ENTRIES 0x100
#define SHADE_RAMP_COMPLEMENT_ROWS (9 * SHADE_RAMP_ROW_ENTRIES)

/* The three slots of the blend record, MOV EBX,[EAX+0x8] / MOV EBX,[EAX] /
   MOV ECX,[EAX+0x4] at 00057624..0005762f. */
#define BLEND_DESC_SHADE_RAMP 0
#define BLEND_DESC_LEVEL 1
#define BLEND_DESC_CUBE 2

/* 0005761b.  Hand-written assembly, not compiler output: no prologue, ESI is
   the stream, EDI the destination, EDX the row advance, BX the width left in
   the row and CL the command byte, and the blend record arrives as [EBP+0x1c],
   read out of fdps_blit_dispatch's own live frame.  The C below is the same
   decode and the same arithmetic, not the same registers (ADR-0001), the way
   fdps_rle_blit_passthrough at 00056a0d is.

   The four inputs become four parameters.  The first three are set up by
   fdps_blit_dispatch immediately before the CALL (MOV ESI,[EBP+8], MOV
   EDI,[EBP+0xc], and EDX as [EBP+0x18] - [EBP+0x10] at 000568e7); the fourth is
   the record pointer, which the assembly dereferences inside this routine
   (00057621..0005762f), so it becomes one parameter and the three loads stay
   here, the way mode 6's geometry record does in rlerot.c.

   Three register facts of the original therefore do not carry over and must not
   be reproduced: the destruction of EBX, ESI and EDI, which the stack
   convention says a callee preserves; the scratch use of the caller's argument
   slots [EBP+8] through [EBP+0x1c], where the assembly parks the two row
   pointers, the cube base and the constants 0xf0f0f and 0xffff because it has
   no registers left -- the dispatcher never reads any of those slots again, its
   epilogue at 00056a08 being four POPs and a RET; and the XOR ECX,ECX at
   00057667, which exists because the body writes only CL and then reads the
   whole of CX in SUB BX,CX and the whole of ECX in LOOP.

   What DOES carry over is the row advance passing through the global.  MOV
   [0x00070030],EDX at 0005761b parks it there on entry and ADD EDI,[0x00070030]
   at 0005777f reads it back at every row end, so the advance is published
   whether or not anything downstream wants it -- fdps_rle_blit_scaled writes
   the same global, sixteen bits wide (see rle.c), and this is the kernel whose
   full 32-bit signed store is why gamedata.h types it as an int.

   The blend is the one fdps_blit_blend_rect performs at 00030230, run per pixel
   instead of per rectangle: the source pixel's weighted colour is held across
   the run in EDX and re-fetched from [EBP+0x1c] after every store, the
   destination pixel under the cursor is weighted through the other row, and the
   sum is folded by SHR EAX,0x4 / AND 0xf0f0f.  The fold cannot carry between
   channels because the two rows the level picks always carry coefficients
   summing to 16, so no channel byte of the sum exceeds 15 * 16.

   The cube index is built green-major, (v >> 12) | (v & 0xffff) at
   000576af..000576b5 over a value carrying red in bits 16..19, green in bits
   8..11 and blue in bits 0..3: green lands in bits 8..11, red in bits 4..7 and
   blue in bits 0..3, which is the order fdps_build_palette_tables writes the
   cube in and NOT the order the axis names suggest (rebuild_info/pitfalls.md).

   The level fold is a row choice and not a second blend rule.  CMP ECX,0x8 /
   JBE at 00057639 is UNSIGNED, and the arm it guards is SUB ECX,0x10 / NEG ECX
   / XCHG EAX,EDX -- 16 - level with the two 0x2400 offsets exchanged -- because
   rows 0..8 carry the coefficients 0..8 and rows 9..17 carry 16..8, so above 8
   the same pair of weights is only stored the other way round.  Picking any two
   rows whose coefficients do not sum to 16 breaks the masked add.

   Three things here look like defects and are not, and all three are the plain
   kernel's (see rle.c):

   The row ends on OR BX,BX / JNZ, an exact-zero test, so a run that overshoots
   the remaining width wraps the sixteen-bit counter instead of ending the row.
   The counter is an unsigned short here and the test stays an equality.

   Op 01 writes only the second byte of each destination pair -- INC EDI, then
   the blend, then STOSB at 000576e5..00057709 -- and blends against the pixel
   at that second byte, leaving the first byte of every pair as it was.

   The row counter is decremented in memory and the loop that reads it is a
   do-while, DEC word ptr [0x00070022] / JNZ, so a caller that asks for zero
   rows gets 0x10000 of them rather than none.

   The width is re-read from the global at the top of every row rather than
   cached, because that is where MOV BX,[0x00070024] sits, at the row-restart
   target. */
void fdps_rle_blit_translucent(unsigned char *rle_stream,
                               unsigned char *dest_pixel,
                               int dest_row_advance,
                               int *blend_descriptor)
{
    unsigned char *stream_cursor;
    unsigned char *dest_cursor;
    unsigned int *shade_ramp;
    unsigned int blend_level;
    unsigned int ramp_row;
    unsigned int *source_weight_row;
    unsigned int *dest_weight_row;
    unsigned char *inverse_palette_cube;
    unsigned short width_remaining;
    unsigned char command;
    unsigned int run_length;
    unsigned int weighted_source;
    unsigned int blended;
    unsigned int cube_index;

    data_fdps_graphics_rle_blit_dst_row_advance = dest_row_advance;

    shade_ramp = (unsigned int *) blend_descriptor[BLEND_DESC_SHADE_RAMP];
    blend_level = (unsigned int) blend_descriptor[BLEND_DESC_LEVEL];
    inverse_palette_cube =
        (unsigned char *) blend_descriptor[BLEND_DESC_CUBE];

    if (blend_level <= 8) {
        ramp_row = blend_level;
        source_weight_row = shade_ramp + ramp_row * SHADE_RAMP_ROW_ENTRIES
                            + SHADE_RAMP_COMPLEMENT_ROWS;
        dest_weight_row = shade_ramp + ramp_row * SHADE_RAMP_ROW_ENTRIES;
    } else {
        ramp_row = 16 - blend_level;
        source_weight_row = shade_ramp + ramp_row * SHADE_RAMP_ROW_ENTRIES;
        dest_weight_row = shade_ramp + ramp_row * SHADE_RAMP_ROW_ENTRIES
                          + SHADE_RAMP_COMPLEMENT_ROWS;
    }

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
                /* blended fill: one pixel byte weighted once, then blended into
                   run_length consecutive destination bytes */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                weighted_source = source_weight_row[*stream_cursor];
                stream_cursor++;
                while (run_length != 0) {
                    blended = (weighted_source
                               + dest_weight_row[*dest_cursor]) >> 4;
                    blended &= 0x000f0f0fu;
                    cube_index = (blended & 0xffffu) | (blended >> 12);
                    *dest_cursor = inverse_palette_cube[cube_index];
                    dest_cursor++;
                    run_length--;
                }
                break;

            case 1:
                /* stretched blended fill: the second byte of each of
                   run_length destination pairs, first halves untouched */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length - run_length);
                weighted_source = source_weight_row[*stream_cursor];
                stream_cursor++;
                while (run_length != 0) {
                    dest_cursor++;
                    blended = (weighted_source
                               + dest_weight_row[*dest_cursor]) >> 4;
                    blended &= 0x000f0f0fu;
                    cube_index = (blended & 0xffffu) | (blended >> 12);
                    *dest_cursor = inverse_palette_cube[cube_index];
                    dest_cursor++;
                    run_length--;
                }
                break;

            case 2:
                /* blended literal: run_length pixel bytes, each weighted and
                   blended with the destination byte under it */
                width_remaining = (unsigned short)
                                  (width_remaining - run_length);
                while (run_length != 0) {
                    weighted_source = source_weight_row[*stream_cursor];
                    stream_cursor++;
                    blended = (weighted_source
                               + dest_weight_row[*dest_cursor]) >> 4;
                    blended &= 0x000f0f0fu;
                    cube_index = (blended & 0xffffu) | (blended >> 12);
                    *dest_cursor = inverse_palette_cube[cube_index];
                    dest_cursor++;
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

        dest_cursor += data_fdps_graphics_rle_blit_dst_row_advance;
        data_fdps_graphics_rle_blit_remaining_rows--;
    } while (data_fdps_graphics_rle_blit_remaining_rows != 0);
}
