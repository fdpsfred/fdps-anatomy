/* sprite.c -- the .SAF sprite drawers: one tilemap cell, one tilemap layer,
 * one composite sprite.
 *
 * See sprite.h for the draw request every drawer here is handed and for what
 * each of its nine slots means, and resource_info/saf.md for the container
 * these functions walk.  Nothing in this file owns state: the request, the
 * loaded image and the destination surface all belong to the caller.
 */
#include "sprite.h"
#include "blit.h"

/* The .SAF header fields this file reads, as byte offsets from the image base.
   The cell size is a u16 pair near the front of the header; the other two are
   the item count and the section start of section descriptor 2, the tile
   section, which is the third of the four 10-byte descriptors that begin at
   +0x0c.  They are addressed as byte offsets rather than through a header
   struct because the descriptors are byte-packed onto odd boundaries -- the
   u32 at +0x22 is only 2-byte aligned -- and a struct would pad them apart
   (rebuild_info/pitfalls.md). */
#define SAF_CELL_WIDTH_OFFSET 0x07
#define SAF_CELL_HEIGHT_OFFSET 0x09
#define SAF_TILE_COUNT_OFFSET 0x20
#define SAF_TILE_SECTION_START_OFFSET 0x22

/* 00013ed0.  The cell size and the request's x and y are read out before any
   test is made -- XOR EAX,EAX / MOV AX,word ptr [EDX+0x7] at 00013eeb and its
   three neighbours all sit above the first compare -- and the reads of the
   size are zero-extending, so the two u16 fields are unsigned however wide the
   int they land in.  The count at +0x20 is read the same way, XOR EDX,EDX /
   MOV DX,word ptr [EAX+0x20], and then compared with CMP EDX,dword ptr
   [EAX+0x18] / JLE, the signed compare an unsigned short promoted to int
   gives.  The lower bound is the separate CMP dword ptr [EAX+0x18],0x0 / JGE
   after it, in that order.

   All six tests are one conjunction: every failing arm reaches the same
   epilogue without having called anything, and the four placement tests are
   strict compares -- CMP dword ptr [EBP+-0x18],0x0 / JLE for x, then SUB
   EAX,cell width / CMP EAX,x / JG, and the same pair for y.  Nothing is
   clipped and nothing is adjusted; sprite.h says what writing the natural
   bound instead would draw.

   The stream address is three separate adds onto the image base: the section
   start read at +0x22 gives the offset table, LEA EAX,[EAX*0x4] steps it, and
   the entry read out of the table is added to the image base again -- MOV
   EAX,dword ptr [EBP+-0x8] / ADD EAX,dword ptr [EDX] -- not to the table
   address, because a stored offset is measured from the start of the file.

   The original keeps two copies of the image base in its frame, at -0x8 and
   -0x10, and reads the header through one and rebases through the other; they
   hold the same value from the first instruction onwards, so one local carries
   both (ADR-0001).

   The seven arguments are pushed right to left and discarded by the caller,
   ADD ESP,0x1c at 00013fc0, and the mode is pushed as the whole dword at
   +0x20 even though fdps_blit_dispatch reads only its low byte; the cast here
   is that read, and it is why a mode above 0xff behaves as its low byte does
   rather than as an out-of-range mode. */
void fdps_draw_tilemap_cell(int *request)
{
    unsigned char *saf_base;
    int cell_width;
    int cell_height;
    int x;
    int y;
    unsigned int tile_section_start;
    unsigned char *tile_stream;
    unsigned char *dest_pixel;

    saf_base = (unsigned char *) request[DRAW_REQUEST_IMAGE];
    cell_width = *(unsigned short *) (saf_base + SAF_CELL_WIDTH_OFFSET);
    cell_height = *(unsigned short *) (saf_base + SAF_CELL_HEIGHT_OFFSET);
    x = request[DRAW_REQUEST_X];
    y = request[DRAW_REQUEST_Y];

    if (request[DRAW_REQUEST_ITEM_INDEX]
            < *(unsigned short *) (saf_base + SAF_TILE_COUNT_OFFSET)
        && request[DRAW_REQUEST_ITEM_INDEX] >= 0
        && x > 0
        && request[DRAW_REQUEST_DEST_PITCH] - cell_width > x
        && y > 0
        && request[DRAW_REQUEST_DEST_ROWS] - cell_height > y) {
        tile_section_start =
            *(unsigned int *) (saf_base + SAF_TILE_SECTION_START_OFFSET);
        tile_stream = saf_base + *(unsigned int *)
            (saf_base + tile_section_start
             + request[DRAW_REQUEST_ITEM_INDEX] * 4);
        dest_pixel = (unsigned char *) request[DRAW_REQUEST_DEST_BASE]
            + x + y * request[DRAW_REQUEST_DEST_PITCH];
        fdps_blit_dispatch(tile_stream, dest_pixel, cell_width, cell_height,
                           request[DRAW_REQUEST_DEST_PITCH],
                           (unsigned int) request[DRAW_REQUEST_BLIT_OPERAND],
                           (unsigned char) request[DRAW_REQUEST_BLIT_MODE]);
    }
}
