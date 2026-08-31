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
