/* sprite.c -- the .SAF sprite drawers: one tilemap cell, one tilemap layer,
 * one composite sprite.
 *
 * See sprite.h for the draw request every drawer here is handed and for what
 * each of its nine slots means, and resource_info/saf.md for the container
 * these functions walk.  Nothing in this file owns state: the request, the
 * loaded image and the destination surface all belong to the caller.
 */
#include <string.h>
#include "sprite.h"
#include "blit.h"

/* The .SAF header fields this file reads, as byte offsets from the image base.
   The cell size is a u16 pair near the front of the header; the other four are
   the item count and the section start of two of the four 10-byte section
   descriptors that begin at +0x0c -- descriptor 1, the tilemap section, at
   +0x16 and +0x18, and descriptor 2, the tile section, at +0x20 and +0x22.
   They are addressed as byte offsets rather than through a header struct
   because the descriptors are byte-packed onto odd boundaries -- the u32 at
   +0x22 is only 2-byte aligned -- and a struct would pad them apart
   (rebuild_info/pitfalls.md). */
#define SAF_CELL_WIDTH_OFFSET 0x07
#define SAF_CELL_HEIGHT_OFFSET 0x09
#define SAF_TILEMAP_COUNT_OFFSET 0x16
#define SAF_TILEMAP_SECTION_START_OFFSET 0x18
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

/* 00013fd0.  The range test is the same shape as the cell drawer's and reads
   the same way: XOR EDX,EDX / MOV DX,word ptr [EAX+0x16] zero-extends the
   count, CMP EDX,dword ptr [EAX+0x18] / JLE is the signed compare that an
   unsigned short promoted to int gives, and CMP dword ptr [EAX+0x18],0x0 /
   JGE after it is the lower bound.  Both arms of the failure reach the
   epilogue at 000140cf with nothing called.

   The grid's dimensions and its cells are read SIGNED where the header fields
   are read unsigned -- MOVSX EAX,word ptr [EAX] at 00014045 and 0001404e for
   the columns and rows and again at 000140a9 for each cell, against the XOR /
   MOV AX pairs used for the cell size.  The loop bounds are then the signed
   CMP EAX,dword ptr [EBP+-0x18] / JL, so a negative count in the record leaves
   the loop unentered rather than running it as a very large unsigned one.

   The record address is three adds onto the image base, exactly as the cell
   drawer resolves a tile: the section start read at +0x18 gives the offset
   table, LEA EAX,[EAX*0x4] steps it by the index, and the entry read out of
   the table is added to the image base again and not to the table address,
   because a stored offset is measured from the start of the file.  The
   original keeps two copies of the image base in its frame, at -0x8 and -0xc,
   and reads the header through one and rebases through the other; they hold
   the same value from 00013fe8 onwards, so one local carries both (ADR-0001).

   The request handed to each cell is a 0x24-byte copy made once, before the
   walk, by CALL 0x0003d514 -- memmove -- with the destination LEA'd from
   EBP-0x48.  Three of the copy's slots are then written in the loops and are
   visible in the assembly only as frame offsets inside that buffer: -0x3c is
   the copy's x at +0x0c, -0x38 its y at +0x10 and -0x30 its item index at
   +0x18.  The x written at the top of each row is read from the CALLER's block
   at [EBP+0x14]+0xc, so it is the request's own x every time and not the
   copy's running value wound back. */
void fdps_draw_tilemap_layer(int *request)
{
    unsigned char *saf_base;
    unsigned int tilemap_section_start;
    short *cell;
    int columns;
    int rows;
    int cell_width;
    int cell_height;
    int row;
    int column;
    int cell_request[DRAW_REQUEST_DWORDS];

    saf_base = (unsigned char *) request[DRAW_REQUEST_IMAGE];
    if (request[DRAW_REQUEST_ITEM_INDEX]
            < *(unsigned short *) (saf_base + SAF_TILEMAP_COUNT_OFFSET)
        && request[DRAW_REQUEST_ITEM_INDEX] >= 0) {
        cell_width = *(unsigned short *) (saf_base + SAF_CELL_WIDTH_OFFSET);
        cell_height = *(unsigned short *) (saf_base + SAF_CELL_HEIGHT_OFFSET);
        tilemap_section_start =
            *(unsigned int *) (saf_base + SAF_TILEMAP_SECTION_START_OFFSET);
        cell = (short *) (saf_base + *(unsigned int *)
            (saf_base + tilemap_section_start
             + request[DRAW_REQUEST_ITEM_INDEX] * 4));
        columns = cell[0];
        rows = cell[1];
        cell += 2;
        memmove(cell_request, request, DRAW_REQUEST_DWORDS * sizeof(int));
        for (row = 0; row < rows; row++) {
            cell_request[DRAW_REQUEST_X] = request[DRAW_REQUEST_X];
            for (column = 0; column < columns; column++) {
                cell_request[DRAW_REQUEST_ITEM_INDEX] = *cell;
                fdps_draw_tilemap_cell(cell_request);
                cell++;
                cell_request[DRAW_REQUEST_X] += cell_width;
            }
            cell_request[DRAW_REQUEST_Y] += cell_height;
        }
    }
}
