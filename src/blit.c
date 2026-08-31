/* blit.c -- rectangle blit primitives.
 *
 * See blit.h for the destination convention.  Nothing here owns state: every
 * routine works entirely on the surface and the coordinates it is handed.
 */
#include <string.h>
#include "blit.h"

/* The VGA graphics aperture as a flat linear address, and the mode 13h
   scanline pitch in bytes.  Both are hard-coded in the original (ADD
   EAX,0xa0000 and IMUL EAX,[EBP+0x18],0x140) and both stay literals here:
   0xa0000 is where the display adapter answers, not the address of anything
   the linker places, so there is no symbol to reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* 0002e5e0.  The square's side is cell_pitch - 1 in both directions: the loop
   bound at 0002e60a and the memset length at 0002e61d are the same DEC EAX on
   the same argument.  That is deliberate -- it is what spaces the battle map
   overview's markers one pixel apart -- and blit.h says what closing the gap
   would look like.

   CMP EAX,dword ptr [EBP + -0x4] / JG is the signed compare, and both operands
   are signed ints: cell_pitch of 0 gives the bound -1 and the loop never runs.
   Reading cell_pitch as unsigned turns that into a walk of four billion
   scanlines across the whole address space.

   The row address is held in one local that is advanced by a whole scanline
   per iteration (ADD dword ptr [EBP + -0x8],0x140); memset's return value is
   discarded, so the walk does not depend on what the CRT hands back.

   Neither coordinate is clipped and no bound is checked.  Both call sites in
   the original are in fdps_battle_map_overview, which centres the map on the
   screen and can hand this routine a corner outside it for a map big enough;
   the original writes there anyway and so does this. */
void fdps_fill_screen_square(int x, int y, int color, int cell_pitch)
{
    unsigned char *row_dst;
    int row;

    row_dst = (unsigned char *) (VGA_SCREEN_BASE + y * VGA_SCREEN_PITCH + x);

    for (row = 0; row < cell_pitch - 1; row++) {
        memset(row_dst, color, (size_t) (cell_pitch - 1));
        row_dst += VGA_SCREEN_PITCH;
    }
}

/* 0002f2f0.  Two loops that never both run: CMP dword ptr [EBP + 0x18],0x0 /
   JNZ at 0002f308 picks the copy loop when src_stride is non-zero and falls
   through into the fill loop when it is zero.  Both loops are counted the same
   way -- MOV [EBP-0x10],0x0 then CMP EAX,[EBP+0x28] / JL -- so the compare
   against rows is signed and rows <= 0 leaves the destination alone.

   Both cursors are read out of the arguments into locals before the branch is
   taken (0002f2fc and 0002f302), which is why the caller's own pointers are
   never advanced.  The copy loop advances the source by src_stride and the
   destination by dst_stride independently (0002f378 and 0002f37e); the fill
   loop advances only the destination (0002f341), because it has no source.

   src_or_fill carries the source pointer in copy mode and the fill byte in
   fill mode; the fill branch takes its own copy of it into [EBP-0xc] at
   0002f30e and hands that to memset, which uses only the low 8 bits.

   The copy is memmove (CALL 0x0003d514), not memcpy: a row whose source and
   destination overlap comes out shifted rather than smeared, and the
   transition routines that slide a page across itself depend on that.  Each
   row is one call -- a run of rows with matching strides is NOT one long
   transfer, and folding them into one would change both the timing and the
   overlap behaviour.

   Nothing is clipped, no length is checked and there is no transparency test:
   bytes_per_row bytes are written on each of rows rows wherever the arithmetic
   lands. */
void fdps_blit_rect(unsigned int src_or_fill, int src_stride, void *dst,
                    int dst_stride, int bytes_per_row, int rows)
{
    /* Declared in this order because that is the order the original's frame
       is laid out in: [EBP-4] is the destination cursor, [EBP-8] the source
       cursor, [EBP-0xc] the fill value and [EBP-0x10] the row counter, and
       wcc386 hands out the slots in declaration order.  Nothing depends on
       it -- the arrangement is codegen, not behaviour -- but written this way
       the object file this compiles to differs from the original's bytes only
       in the two idioms the toolchain settings decide. */
    unsigned char *dst_cursor;
    unsigned char *src_cursor;
    unsigned int fill_value;
    int row;

    src_cursor = (unsigned char *) src_or_fill;
    dst_cursor = (unsigned char *) dst;

    if (src_stride == 0) {
        fill_value = src_or_fill;

        for (row = 0; row < rows; row++) {
            memset(dst_cursor, (int) fill_value, (size_t) bytes_per_row);
            dst_cursor += dst_stride;
        }
    } else {
        for (row = 0; row < rows; row++) {
            memmove(dst_cursor, src_cursor, (size_t) bytes_per_row);
            src_cursor += src_stride;
            dst_cursor += dst_stride;
        }
    }
}

/* 0002f390.  One shape, no mode branch: the arguments are copied into the two
   cursors at 0002f39c and 0002f3a2 and then the row loop runs unconditionally.
   Both loop tests are signed -- CMP EAX,[EBP+0x28] / JL at 0002f3b2 for the
   rows and CMP EAX,[EBP+0x24] / JL at 0002f3cb for the columns -- so a width
   or a height of 0 or less transfers nothing at all.

   The inner body is the whole point of the routine.  XOR EAX,EAX / MOV AL,
   byte ptr [EDX] at 0002f3e0 loads one source byte zero-extended into the
   pixel slot, CMP dword ptr [EBP-0x10],0x0 / JZ at 0002f3e7 skips the store
   when it is zero, and only the surviving byte is written through
   MOV byte ptr [EDX],AL at 0002f3f6.  There is no per-row transfer and no
   call of any kind: the destination is read-modify-write, and the pixels a
   zero source byte passes over keep whatever the previous blits left there.

   Both cursors are indexed by the same column counter and are advanced by
   their own stride once per row (0002f3fd and 0002f403), which is what lets
   the source sheet and the destination page have different pitches -- 0x75
   and 0x7d for the two gauge sheets, 0x138 for the offscreen pages.  A
   src_stride of 0 is not a fill mode here as it is in fdps_blit_rect above;
   it just points every row at the same source row.

   Nothing is clipped and no extent is checked. */
void fdps_blit_transparent_rect(unsigned char *src, int src_stride,
                                unsigned char *dst, int dst_stride,
                                int width, int height)
{
    /* Declared in the order the original's frame is laid out, as in
       fdps_blit_rect above: [EBP-4] the destination cursor, [EBP-8] the
       source cursor, [EBP-0xc] the column counter, [EBP-0x10] the pixel and
       [EBP-0x14] the row counter.  Codegen, not behaviour. */
    unsigned char *dst_cursor;
    unsigned char *src_cursor;
    int column;
    unsigned int pixel;
    int row;

    src_cursor = src;
    dst_cursor = dst;

    for (row = 0; row < height; row++) {
        for (column = 0; column < width; column++) {
            pixel = (unsigned int) src_cursor[column];

            if (pixel != 0) {
                dst_cursor[column] = (unsigned char) pixel;
            }
        }

        src_cursor += src_stride;
        dst_cursor += dst_stride;
    }
}
