/* sprite.c -- the .SAF sprite drawers: one tilemap cell, one tilemap layer,
 * one composite sprite; and the one-line drawer for the Command.cel UI sheet.
 *
 * See sprite.h for the draw request every .SAF drawer here is handed and for
 * what each of its nine slots means, and resource_info/saf.md for the
 * container those three functions walk.  They own no state: the request, the
 * loaded image and the destination surface all belong to the caller.
 *
 * The Command.cel drawer at the bottom is the exception on both counts -- a
 * .CEL sheet and not a .SAF (resource_info/cel.md), and the one function here
 * that reads a global, the loaded sheet.
 */
#include <stddef.h>
#include <string.h>
#include "fdpstype.h"
#include "sprite.h"
#include "blit.h"
#include "audio.h"
#include "gamedata.h"

/* The .SAF header fields this file reads, as byte offsets from the image base.
   The cell size is a u16 pair near the front of the header; the other six are
   the item count and the section start of three of the four 10-byte section
   descriptors that begin at +0x0c -- descriptor 0, the frame section, at +0x0c
   and +0x0e, descriptor 1, the tilemap section, at +0x16 and +0x18, and
   descriptor 2, the tile section, at +0x20 and +0x22.  They are addressed as
   byte offsets rather than through a header struct because the descriptors are
   byte-packed onto odd boundaries -- the u32 at +0x22 is only 2-byte aligned
   -- and a struct would pad them apart (rebuild_info/pitfalls.md). */
#define SAF_CELL_WIDTH_OFFSET 0x07
#define SAF_CELL_HEIGHT_OFFSET 0x09
#define SAF_FRAME_COUNT_OFFSET 0x0c
#define SAF_FRAME_SECTION_START_OFFSET 0x0e
#define SAF_TILEMAP_COUNT_OFFSET 0x16
#define SAF_TILEMAP_SECTION_START_OFFSET 0x18
#define SAF_TILE_COUNT_OFFSET 0x20
#define SAF_TILE_SECTION_START_OFFSET 0x22

/* A frame record and the 13-byte layer records that follow it, as byte offsets
   from the record (resource_info/saf.md).  The blend level at +0x07 is read as
   a 16-bit quantity, so it takes the reserved byte at +0x08 with it; that byte
   is zero in every shipped image, which is why the wider read and a byte read
   come to the same thing.  Nothing here is a struct for the same reason the
   header is not: the layer's i16 fields sit on odd boundaries. */
#define SAF_FRAME_SOUND_OFFSET 0x00
#define SAF_FRAME_LAYER_COUNT_OFFSET 0x08
#define SAF_FRAME_LAYERS_OFFSET 0x0a
#define SAF_LAYER_RECORD_BYTES 13
#define SAF_LAYER_TILEMAP_OFFSET 0x00
#define SAF_LAYER_X_OFFSET 0x02
#define SAF_LAYER_Y_OFFSET 0x04
#define SAF_LAYER_BLEND_FLAG_OFFSET 0x06
#define SAF_LAYER_BLEND_LEVEL_OFFSET 0x07

/* The blit mode a translucent layer asks for and the three-dword descriptor it
   is handed, whose slots rleblend.h is the canon for: [0] the shade ramp base,
   [1] the blend level and [2] the inverse colour cube base.  The level the
   descriptor wants runs 0 = the source opaque through 16 = invisible, the
   complement of the opacity a layer record stores. */
#define BLIT_MODE_OPAQUE 0
#define BLIT_MODE_TRANSLUCENT 9
#define BLEND_DESC_SHADE_RAMP 0
#define BLEND_DESC_LEVEL 1
#define BLEND_DESC_CUBE 2
#define BLEND_DESC_DWORDS 3
#define BLEND_LEVEL_OPAQUE 16

/* A layer's blend flag: the only two values the assembly tests for, and the
   only two any shipped .SAF stores. */
#define SAF_LAYER_BLEND_OFF 0
#define SAF_LAYER_BLEND_ON 1

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

/* 00014140.  The frame lookup is written out here rather than called, because
   the assembly has no CALL for it: 0001414c through 000141bf is a full inline
   expansion of fdps_saf_get_frame @ 000140e0, and the fingerprint is the
   frame it leaves behind -- the two argument temps at -0x50 and -0x54 copied
   into the consecutive parameter-shaped slots at -0x58 and -0x5c, the callee's
   body replayed instruction for instruction under that substitution, and the
   result slot at -0x64 copied out into this function's own local at -0xc.
   -oe is not in the flag set, so the expansion was asked for in the source
   with _inline (rebuild_info/build_flags.md).  Writing the body out is still
   correct: ADR-0001 is functional equivalence and the two spellings behave
   alike.  Spelling it as a call is the riskier of the two, because it only
   stays equivalent while an _inline declaration is there to expand it.  The
   count word at +0x0c is read zero-extended and the frame offset is rebased on
   the image and not on the table, both exactly as fdps_saf_get_frame does
   them, so saf.c's notes apply here unchanged.

   Everything after the lookup sits under CMP dword ptr [EBP+-0xc],0x0 / JZ to
   the epilogue at 000141c9, so an out-of-range frame index plays no sound
   either.

   The widths are not interchangeable and the assembly says which is which.
   The layer count at +0x08 of the frame, both of the layer's offsets and the
   sound number are MOVSX -- 00014208, 00014268, 0001427a and 000142b5 -- so a
   negative layer count leaves the walk unentered and a negative offset moves
   the layer up and to the left.  The tilemap number at +0x00 of the layer and
   the frame count in the header are XOR EAX,EAX / MOV AX, zero-extending, so
   neither can come back negative.  The blend flag is XOR EAX,EAX / MOV AL,byte
   ptr [EDX+0x6] into a dword local that is then compared with 0 and with 1.

   The blend level is MOVSX EAX,word ptr [EAX+0x7] at 0001424a, a 16-bit read
   of a field the format documents as one byte; the byte after it is reserved
   and zero in all 525 images, so the two readings agree on every real input.

   The loop re-reads the layer count from the frame at the top of every
   iteration, which is what the assembly does -- MOV EAX,dword ptr [EBP+-0x10]
   / MOVSX EAX,word ptr [EAX+0x8] at 00014205 is the loop's own head, not a
   value hoisted before it.  Nothing in the body can change it.

   The two blend tables are the only absolute addresses in the function, MOV
   dword ptr [EBP+-0x28],0x653f0 and MOV dword ptr [EBP+-0x20],0x643f0, and
   they are the bases of data_fdps_palette_shade_ramp_table and
   data_fdps_inverse_palette_cube with no displacement folded into either.
   Both stores sit above the loop; only the level in the middle slot is
   rewritten inside it.

   The last three pushes are the layer request, then the sound number and the
   image: ADD ESP,0x4 after the layer call and ADD ESP,0x8 after the sound
   call, both caller cleanup. */
void fdps_draw_composite_sprite(int *request, char play_sound)
{
    unsigned char *saf_base;
    int frame_index;
    unsigned int frame_section_start;
    unsigned char *frame;
    unsigned char *layer;
    int layer_index;
    int blend_flag;
    int blend_descriptor[BLEND_DESC_DWORDS];
    int layer_request[DRAW_REQUEST_DWORDS];

    saf_base = (unsigned char *) request[DRAW_REQUEST_IMAGE];
    frame_index = request[DRAW_REQUEST_ITEM_INDEX];
    if (frame_index < *(unsigned short *) (saf_base + SAF_FRAME_COUNT_OFFSET)
        && frame_index >= 0) {
        frame_section_start =
            *(unsigned int *) (saf_base + SAF_FRAME_SECTION_START_OFFSET);
        frame = saf_base + *(unsigned int *)
            (saf_base + frame_section_start + frame_index * 4);
    } else {
        frame = NULL;
    }

    if (frame != NULL) {
        memmove(layer_request, request, DRAW_REQUEST_DWORDS * sizeof(int));
        layer = frame + SAF_FRAME_LAYERS_OFFSET;
        blend_descriptor[BLEND_DESC_SHADE_RAMP] =
            (int) data_fdps_palette_shade_ramp_table;
        blend_descriptor[BLEND_DESC_CUBE] =
            (int) data_fdps_inverse_palette_cube;
        for (layer_index = 0;
             layer_index < *(short *) (frame + SAF_FRAME_LAYER_COUNT_OFFSET);
             layer_index++) {
            if (request[DRAW_REQUEST_BLIT_MODE] == 0) {
                blend_flag = layer[SAF_LAYER_BLEND_FLAG_OFFSET];
                if (blend_flag == SAF_LAYER_BLEND_OFF) {
                    layer_request[DRAW_REQUEST_BLIT_MODE] = BLIT_MODE_OPAQUE;
                } else if (blend_flag == SAF_LAYER_BLEND_ON) {
                    blend_descriptor[BLEND_DESC_LEVEL] = BLEND_LEVEL_OPAQUE
                        - *(short *) (layer + SAF_LAYER_BLEND_LEVEL_OFFSET);
                    layer_request[DRAW_REQUEST_BLIT_MODE] =
                        BLIT_MODE_TRANSLUCENT;
                    layer_request[DRAW_REQUEST_BLIT_OPERAND] =
                        (int) blend_descriptor;
                }
            }
            layer_request[DRAW_REQUEST_X] = request[DRAW_REQUEST_X]
                + *(short *) (layer + SAF_LAYER_X_OFFSET);
            layer_request[DRAW_REQUEST_Y] = request[DRAW_REQUEST_Y]
                + *(short *) (layer + SAF_LAYER_Y_OFFSET);
            layer_request[DRAW_REQUEST_ITEM_INDEX] =
                *(unsigned short *) (layer + SAF_LAYER_TILEMAP_OFFSET);
            fdps_draw_tilemap_layer(layer_request);
            layer += SAF_LAYER_RECORD_BYTES;
        }
        if (play_sound != 0) {
            fdps_sfx_play(saf_base,
                          *(short *) (frame + SAF_FRAME_SOUND_OFFSET));
        }
    }
}

/* Command.cel's sheet-wide sprite size, and where its offset table starts.
   All three are constants in the assembly and never reads of the sheet's
   header: PUSH 0x19 at 00015bc6 and PUSH 0x16 at 00015bc4 are the width and
   height the header's i16 pair at +0x07 and +0x09 declares, and MOV EAX,dword
   ptr [EAX+0xf] at 00015bb4 is the table position the header's u16 at +0x05
   declares.  The table start is spelled as the header's size because that is
   what the 15 is -- a .CEL opens with struct fdps_cel_header and the table
   begins in the next byte (resource_info/cel.md). */
#define COMMAND_SPRITE_WIDTH 0x19
#define COMMAND_SPRITE_HEIGHT 0x16
#define CEL_OFFSET_TABLE_START ((int) sizeof(struct fdps_cel_header))

/* 00015b90.  One basic block: no branch, no test, no loop, and the only value
   worked out before the call is the sprite's stream address.

   The three adds that produce it are the .CEL rule that a stored offset is
   measured from the start of the FILE.  LEA EAX,[EAX*0x4+0x0] scales the
   index, ADD EAX,EDX puts it on the sheet base to address the table entry, and
   ADD EAX,EDX after the load puts the entry itself on the sheet base again --
   onto the base, never onto the address the entry was read from.

   The original loads the sheet pointer from its global twice, at 00015ba6 and
   again at 00015bae, because it needs it for the table address and then for
   the rebase; one read of the global carries both here, which is the same
   value either way (ADR-0001).

   Nothing is read after the call: the seven arguments are pushed right to left
   and the caller discards all of them with ADD ESP,0x1c at 00015bd5, and
   fdps_blit_dispatch returns nothing this function looks at.  The width, the
   height, the mode operand and the mode are all immediates in the push
   sequence, so the only two values that come from outside are dst and pitch,
   passed straight through. */
void fdps_blit_command_sprite(unsigned char *dst, int pitch, int sprite_index)
{
    unsigned char *sprite_stream;

    sprite_stream = data_fdps_command_sprite_sheet_ptr
        + *(int *) (data_fdps_command_sprite_sheet_ptr
                    + sprite_index * 4 + CEL_OFFSET_TABLE_START);
    fdps_blit_dispatch(sprite_stream, dst, COMMAND_SPRITE_WIDTH,
                       COMMAND_SPRITE_HEIGHT, pitch, 0, BLIT_MODE_OPAQUE);
}
