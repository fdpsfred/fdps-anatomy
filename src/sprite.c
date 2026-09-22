/* sprite.c -- the .SAF sprite drawers: one tilemap cell, one tilemap layer,
 * one composite sprite; and the four .CEL drawers, the general one every UI
 * screen blits through and three that each know one sheet -- the Command.cel
 * UI sheet, a battle-map unit's walk sprite and a piece of the Cusor.cel
 * map-cursor outline kit.  Last comes the one .CEL consumer that draws into
 * memory rather than onto a screen surface, fdps_cel_expand_sheet_24x24,
 * which unpacks the loaded map tile sheet into a flat array of 24 by 24 pixel
 * blocks for the battlefield overview screen and is the only function here
 * that allocates.
 *
 * See sprite.h for the draw request every .SAF drawer here is handed and for
 * what each of its nine slots means, and resource_info/saf.md for the
 * container those three functions walk.  They own no state: the request, the
 * loaded image and the destination surface all belong to the caller.
 *
 * The four .CEL drawers at the bottom are the exception on the first count --
 * a .CEL sheet and not a .SAF (resource_info/cel.md) -- and three of them on
 * the second, being the only functions here that read globals: the loaded
 * Command.cel sheet for the first; the unit array, the view window origin, the
 * map animation counter and the sprite cache for the second; and the loaded
 * Cusor.cel sheet and the view window origin for the third.  The fourth,
 * fdps_cel_blit_sprite, is handed its sheet and its surface and owns nothing
 * of its own; it is the general drawer the game's menus, shops and windows all
 * go through.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "sprite.h"
#include "blit.h"
#include "audio.h"
#include "unit.h"
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
   The original's game units were compiled with -oe=25, which made that
   expansion (rebuild_info/build_flags.md).  Writing the body out is still
   correct: ADR-0001 is functional equivalence and the two spellings behave
   alike.  Spelling it as a call only reproduces the expansion under -oe=25
   with the callee in the same unit, which the rebuild's build lacks.  The
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

/* A map tile is 24 pixels square -- IMUL EAX,EAX,0x18 on each of the record's
   two tile bytes at 0001ee35 and 0001ee4f -- and the unit sprite is a tile-
   sized 24 by 24, which is what the two 0x18 immediates pushed at 0001ef16 and
   0001ef18 are.

   The two biases are the view window's one-tile border.  x takes the whole
   0x18 of it (ADD EAX,0x18 at 0001ee3e).  y takes 0x12 (ADD EAX,0x12 at
   0001ee58), which is that same border with fdps_draw_map_unit's fixed
   six-pixel sprite lift already folded into the constant by the compiler,
   because a unit sprite stands taller than the tile it occupies. */
#define UNIT_SPRITE_TILE_SIZE 0x18
#define UNIT_SPRITE_SIZE 0x18
#define UNIT_SPRITE_X_BIAS 0x18
#define UNIT_SPRITE_Y_BIAS 0x12

/* The scene buffer is 360 by 240 and the drawer is hardwired to it: 0x168 is
   pushed as the destination pitch at 0001ef11 and is the multiplier of the row
   in IMUL EAX,dword ptr [EBP + -0x1c],0x168 at 0001ef1a.  The two visibility
   bounds are that buffer less one sprite, 360 - 24 and 240 - 24, and are the
   immediates of CMP ...,0x150 at 0001eea2 and CMP ...,0xd8 at 0001ee8f. */
#define UNIT_SCENE_PITCH 0x168
#define UNIT_SCENE_VISIBLE_WIDTH 0x150
#define UNIT_SCENE_VISIBLE_HEIGHT 0xd8

/* A cache slot holds twelve streams as four facings of three walk frames --
   struct fdps_cel_cache_slot in src/fdpstype.h -- which is the 0xc of IMUL
   EDX,dword ptr [EBP + -0x18],0xc at 0001eed6 and the 3 of LEA EAX,[EAX +
   EAX*0x2] at 0001eedd.  The animation counter advances one per frame tick and
   a walk frame lasts four of them (SAR EAX,0x2 at 0001eec3). */
#define UNIT_SPRITES_PER_CACHE_SLOT 0xc
#define UNIT_WALK_FRAMES_PER_FACING 3
#define UNIT_WALK_PHASE_TICKS 4

/* The counter runs 0..15, so the division yields 0..3 and the fourth value is
   folded back onto the second (CMP dword ptr [EBP + -0x14],0x3 / JNZ / MOV
   dword ptr [EBP + -0x14],0x1 at 0001eec9).  That turns the three walk frames
   into the ping-pong 0, 1, 2, 1 over the counter's sixteen ticks. */
#define UNIT_WALK_PHASE_FOLDED 3
#define UNIT_WALK_PHASE_FOLD_TO 1

/* Four pixels of travel per sub-tile step, LEA EAX,[EAX*0x4 + 0x0] at
   0001ee7f.  See the note in the function about where that value goes. */
#define UNIT_SPRITE_STEP_PIXELS 4

/* 0001ee10.  Draws one battle-map unit's 24x24 walk sprite into a scene
   buffer, in a blit mode the caller picks, so a caller can paint the unit
   normally or as a flat silhouette.  Three callers: fdps_unit_rest,
   fdps_battle_advance_turn and fdps_flash_units_in_color.

   `scene_buffer` is the 360x240 8bpp scene buffer the caller allocated and
   filled, not the VGA page; the sprite is composited into it.  `unit_index`
   goes straight to fdps_get_unit_record, which bounds-checks nothing.
   `blit_param` and `blit_mode` are fdps_blit_dispatch's mode operand and
   kernel selector, forwarded untouched -- the shipped callers pass mode 3, the
   recolour kernel, with a palette colour shifted left by eight, and
   fdps_flash_units_in_color alternates that with mode 0 to make the flash.

   The tile position is converted the way fdps_draw_map_unit converts it: tile
   times 24 minus the view window origin plus the border bias.  Both record
   bytes are zero-extended (AND EAX,0xff at 0001ee30 and 0001ee4a), so they are
   the unsigned 0..255 the layout declares, while both origin globals are
   signed and are subtracted as such.

   THE VISIBILITY TEST IS STRICT ON ALL FOUR SIDES AND NOTHING IS CLIPPED.  The
   sprite is drawn only when 0 < y < 0xd8 and 0 < x < 0x150, and only the
   sprite's origin is looked at, so a unit near an edge is drawn whole or
   dropped whole.  The lower bounds are JLE and JG on zero at 0001ee8d and
   0001ee9e: writing them as >= 0 draws a row of units along the top edge and a
   column along the left that the original never shows
   (rebuild_info/pitfalls.md).

   THE SUB-TILE STEP OFFSET IS COMPUTED AND THEN NOT USED.  Record byte 4 is
   the unit's step counter and is scaled by four into the frame slot at
   [EBP-0x10] at 0001ee86, and nothing in the body ever reads that slot again.
   The facing switch that fdps_draw_map_unit uses to turn the counter into a
   displacement of (0,+4), (-4,0), (0,-4) or (+4,0) is absent here altogether,
   so a unit caught between tiles is drawn snapped to the tile it is leaving.
   Finishing the offset the way the sibling does -- the obvious C, given the
   rest of the geometry is that routine's block trimmed -- moves flashed and
   resting units up to 20 pixels off where the original puts them.  The line
   below therefore keeps the multiply and stops there.

   The walk phase is the shared map animation counter, which fdps_draw_map_unit
   steps modulo 16 once per frame tick at 0002cdf8 and which nothing else in
   the image writes.  The division is signed -- SAR EDX,0x1f / SHL EDX,0x2 /
   SBB EAX,EDX / SAR EAX,0x2 at 0001eebb is the compiler's signed divide by
   four -- so it truncates towards zero rather than flooring, which is only
   reachable if something ever leaves the counter negative.

   The stream address is the .CEL rule that a stored offset is measured from
   the start of the block: the flat sprite index is scaled by four and added to
   the cache base to reach the table entry, and the entry is added to that same
   base again, never to the address it was read from.  The base is read from
   the global twice in the original, at 0001eef4 and 0001eefc; one read carries
   both here, which is the same value either way (ADR-0001).  Nothing bounds
   the index and the cache pointer is not tested for null. */
void fdps_blit_unit_sprite(unsigned char *scene_buffer, int unit_index,
                           unsigned int blit_param, int blit_mode)
{
    struct fdps_unit_record *unit;
    int sprite_x;
    int sprite_y;
    int sprite_index;
    int facing;
    int step_offset;
    int walk_phase;
    unsigned char *sprite_stream;

    unit = fdps_get_unit_record(unit_index);

    sprite_x = unit->pos_x * UNIT_SPRITE_TILE_SIZE
        - data_fdps_battle_view_window_origin_x + UNIT_SPRITE_X_BIAS;
    sprite_y = unit->pos_y * UNIT_SPRITE_TILE_SIZE
        - data_fdps_battle_view_window_origin_y + UNIT_SPRITE_Y_BIAS;
    sprite_index = unit->sprite_cache_slot;
    facing = unit->facing;

    /* Dead on purpose: see the note above.  This must not reach sprite_x or
       sprite_y. */
    step_offset = unit->walk_step * UNIT_SPRITE_STEP_PIXELS;

    if (sprite_y > 0 && sprite_y < UNIT_SCENE_VISIBLE_HEIGHT &&
        sprite_x > 0 && sprite_x < UNIT_SCENE_VISIBLE_WIDTH) {
        walk_phase =
            data_fdps_map_unit_walk_anim_counter / UNIT_WALK_PHASE_TICKS;
        if (walk_phase == UNIT_WALK_PHASE_FOLDED) {
            walk_phase = UNIT_WALK_PHASE_FOLD_TO;
        }

        sprite_index = sprite_index * UNIT_SPRITES_PER_CACHE_SLOT
            + facing * UNIT_WALK_FRAMES_PER_FACING + walk_phase;
        sprite_stream = data_fdps_cel_sprite_cache_ptr
            + *(int *) (data_fdps_cel_sprite_cache_ptr + sprite_index * 4);

        fdps_blit_dispatch(sprite_stream,
                           scene_buffer + sprite_y * UNIT_SCENE_PITCH
                               + sprite_x,
                           UNIT_SPRITE_SIZE, UNIT_SPRITE_SIZE,
                           UNIT_SCENE_PITCH, blit_param,
                           (unsigned char) blit_mode);
    }
}

/* The visible map window is 312 by 192 map pixels: the immediates of ADD
   EAX,0x138 at 0002cc3c and ADD EAX,0xc0 at 0002cc5a, added to the view window
   origin to form each upper bound.

   The scene buffer the pieces land in is 360 bytes per row -- IMUL
   EAX,EAX,0x168 at 0002cc72, the same stride pushed as the destination pitch
   at 0002ccb0 -- and carries a 24 pixel border on all four sides, which is the
   ADD EAX,0x18 on each axis at 0002cc6f and 0002cc81.  The piece itself is a
   map tile square, the two 0x18 immediates pushed at 0002ccb5 and 0002ccb7. */
#define CURSOR_VIEW_WIDTH 0x138
#define CURSOR_VIEW_HEIGHT 0xc0
#define CURSOR_SCENE_PITCH 0x168
#define CURSOR_SCENE_BORDER 0x18
#define CURSOR_TILE_SIZE 0x18

/* 0002cc20.  Four compares and then one basic block.  The four branches all
   land on the same exit -- the chain JL 0002cc46 / JMP 0002cc53 / JMP 0002cc64
   / JMP 0002ccc9 collapses a rejected x straight past the y test -- so the
   whole body is the one guarded block below, and there is nothing after it.

   Every compare is signed: JL at 0002cc35 and JGE at 0002cc51 on the two lower
   bounds, JG at 0002cc44 and 0002cc62 on the two upper ones.  Writing the
   bounds test unsigned drops a piece whose position is left of or above the
   map origin instead of drawing it (rebuild_info/pitfalls.md).

   The bounds are asymmetric and that is not an accident of the encoding: the
   lower bound is taken on equality (JL rejects only a strictly smaller value)
   and the upper one is not (JG requires the bound to be strictly greater), so
   a piece exactly on the left or top edge is drawn and one exactly on the
   right or bottom edge is not.

   The stream address is the .CEL rule that a stored offset is measured from
   the start of the FILE: the index is scaled by four and added to the sheet
   base to reach the table entry, and the entry is added to that same base
   again, never to the address it was read from.  The original reads the sheet
   pointer out of its global twice, at 0002cc8c and again at 0002cc9e; one read
   carries both here, which is the same value either way (ADR-0001).

   Nothing is read after the call: the seven arguments are pushed right to left
   and discarded with ADD ESP,0x1c at 0002ccc6, and fdps_blit_dispatch returns
   nothing this function looks at. */
void fdps_blit_cursor_tile(int map_x, int map_y, int sprite_index,
                           unsigned char *dest)
{
    unsigned char *dest_pixel;
    unsigned char *sprite_stream;

    if (map_x >= data_fdps_battle_view_window_origin_x &&
        map_x < data_fdps_battle_view_window_origin_x + CURSOR_VIEW_WIDTH &&
        map_y >= data_fdps_battle_view_window_origin_y &&
        map_y < data_fdps_battle_view_window_origin_y + CURSOR_VIEW_HEIGHT) {
        dest_pixel = dest
            + (map_y - data_fdps_battle_view_window_origin_y
               + CURSOR_SCENE_BORDER) * CURSOR_SCENE_PITCH
            + (map_x - data_fdps_battle_view_window_origin_x)
            + CURSOR_SCENE_BORDER;
        sprite_stream = data_fdps_cursor_highlight_sprite_sheet_ptr
            + *(int *) (data_fdps_cursor_highlight_sprite_sheet_ptr
                        + sprite_index * 4 + CEL_OFFSET_TABLE_START);

        fdps_blit_dispatch(sprite_stream, dest_pixel, CURSOR_TILE_SIZE,
                           CURSOR_TILE_SIZE, CURSOR_SCENE_PITCH, 0,
                           BLIT_MODE_OPAQUE);
    }
}

/* 0002dba0.  One basic block: no branch, no test, no loop, and the four
   values it works out are the sprite's size, its stream address and the
   destination pixel.

   The size is a pair of MOVSX word reads off the sheet's header, at +0x07 and
   +0x09, and it is the sheet's size rather than the sprite's -- the index is
   nowhere in either address.  They are signed reads, so the two header fields
   are the i16 pair struct fdps_cel_header declares and not a u16 pair
   (fdpstype.h); every shipped sheet states a positive size, so the choice
   shows only on a malformed one.

   The three adds that produce the stream are the .CEL rule that a stored
   offset is measured from the start of the FILE.  LEA EAX,[EAX*0x4+0x0]
   scales the index, ADD EDX,EAX puts it on the sheet base to address the table
   entry, and ADD EAX,dword ptr [EDX+0xf] puts the entry itself on the sheet
   base again -- onto the base, never onto the address the entry was read from.
   The 0x0f is a displacement in that load and not a read of the header's own
   table-position field at +0x05 (rebuild_info/pitfalls.md).

   The destination is dest_base + dest_y * dest_pitch + dest_x, assembled as
   IMUL EAX,dword ptr [EBP+0x20] on the row, ADD EAX,dword ptr [EBP+0x1c] onto
   the base and ADD EDX,EAX with the column; the same pitch then goes to the
   dispatcher as the surface's row stride.  The original loads the sheet
   pointer from its argument slot twice, at 0002dbca and again at 0002dbcf,
   because it needs it for the table address and then for the rebase; one read
   of the parameter carries both here, which is the same value either way
   (ADR-0001).

   Nothing is read after the call: the seven arguments are pushed right to left
   and the caller discards all of them with ADD ESP,0x1c at 0002dc0b, and
   fdps_blit_dispatch returns nothing this function looks at.  The mode operand
   and the blit mode are the caller's last two arguments passed straight
   through, which is what makes this the one .CEL drawer that can reach any of
   the dispatcher's kernels. */
void fdps_cel_blit_sprite(unsigned char *cel_sheet, int sprite_index,
                          unsigned char *dest_base, int dest_pitch,
                          int dest_x, int dest_y, unsigned int mode_operand,
                          unsigned char blit_mode)
{
    int sprite_width;
    int sprite_height;
    unsigned char *sprite_stream;
    unsigned char *dest_pixel;

    sprite_width = ((struct fdps_cel_header *) cel_sheet)->sprite_width;
    sprite_height = ((struct fdps_cel_header *) cel_sheet)->sprite_height;
    sprite_stream = cel_sheet
        + *(int *) (cel_sheet + sprite_index * 4 + CEL_OFFSET_TABLE_START);
    dest_pixel = dest_base + dest_y * dest_pitch + dest_x;

    fdps_blit_dispatch(sprite_stream, dest_pixel, sprite_width, sprite_height,
                       dest_pitch, mode_operand, blit_mode);
}

/* The expanded block's geometry, every number of it an immediate in the
   assembly and none of them a read of the sheet's own header.  A map tile is
   24 pixels square -- MOV word ptr [EDX],0x18 at 0002e68f and MOV word ptr
   [EDX+0x2],0x18 at 0002e694 write it into the header, PUSH 0x18 at 0002e6e4
   hands it to the drawer as the destination pitch and IMUL EAX,...,0x18 at
   0002e6dd scales the row -- and 576 bytes is that square laid out with no
   padding, the 0x240 of the two IMULs at 0002e658 and 0002e6a7.

   The header is six bytes of three i16 at +0, +2 and +4, and EXPANDED_TILES_AT
   is spelled as its size because that is what the 6 pushed as the drawer's x
   at 0002e6e2 is: a column offset abused as a flat byte fudge, which lands
   tile n at block + 6 + n * 576 because the drawer's destination arithmetic is
   base + y * pitch + x and the pitch is the tile's own width. */
#define EXPANDED_TILE_SIZE 0x18
#define EXPANDED_TILE_BYTES 0x240
#define EXPANDED_WIDTH_AT 0
#define EXPANDED_HEIGHT_AT 2
#define EXPANDED_COUNT_AT 4
#define EXPANDED_TILES_AT 6

/* 0002e640.  One counted loop over the sheet's sprites and one branch, the
   allocation test, whose failure arm never rejoins: it prints and calls exit,
   so the C leaves the arm without a return and falls through exactly as the
   JNZ at 0002e672 does.  The ADD ESP,0x4 the compiler emitted after the exit
   call is dead cdecl cleanup behind a noreturn callee and has no source.

   The count is read once, sign-extended out of the sheet's i16 sprite_count by
   the MOVSX at 0002e651, and then serves three times over: as the multiplier
   of the allocation, as the value stored in the header and as the loop bound
   the signed JL at 0002e6cd tests.  The original re-loads the sheet pointer
   from the global for the drawer's first argument at 0002e6ee rather than
   holding the one it read at 0002e64c, which is the same pointer either way
   (ADR-0001).

   Only the block's tile area is cleared.  memset covers count * 576 bytes
   from +6 and the six header bytes are the three stores that follow, so every
   byte of the allocation is written before it is returned -- which matters,
   because the .CEL streams need not cover every pixel of a tile and a skip run
   leaves the destination alone.  Uncovered pixels are palette index 0.

   Nothing is read after any call but malloc's pointer, tested against zero at
   0002e672 and then used as the block: memset returns it again and the code
   ignores that, fdps_cel_blit_sprite returns nothing, and the value returned
   to the caller is the block, staged through [EBP-0x4] at 0002e701 the way
   Watcom stages every return value at -od. */
unsigned char *fdps_cel_expand_sheet_24x24(void)
{
    /* How many sprites the loaded tile sheet holds, and so how many tiles the
       block gets. */
    int tile_count;
    /* The block being filled, and the answer. */
    unsigned char *expanded;
    /* Which sprite of the sheet is being unpacked. */
    int tile_index;

    tile_count = ((struct fdps_cel_header *)
                  data_fdps_scene_layer_tile_sheet_ptrs[0])->sprite_count;
    expanded = (unsigned char *) malloc((size_t)
        (tile_count * EXPANDED_TILE_BYTES + EXPANDED_TILES_AT));
    if (expanded == NULL) {
        printf("Out of memory at rease shape !!!\n");
        exit(1);
    }
    *(short *) (expanded + EXPANDED_WIDTH_AT) = EXPANDED_TILE_SIZE;
    *(short *) (expanded + EXPANDED_HEIGHT_AT) = EXPANDED_TILE_SIZE;
    *(short *) (expanded + EXPANDED_COUNT_AT) = (short) tile_count;
    memset(expanded + EXPANDED_TILES_AT, 0,
           (size_t) (tile_count * EXPANDED_TILE_BYTES));
    for (tile_index = 0; tile_index < tile_count; tile_index++) {
        fdps_cel_blit_sprite(data_fdps_scene_layer_tile_sheet_ptrs[0],
                             tile_index, expanded, EXPANDED_TILE_SIZE,
                             EXPANDED_TILES_AT,
                             tile_index * EXPANDED_TILE_SIZE, 0,
                             BLIT_MODE_OPAQUE);
    }
    return expanded;
}
