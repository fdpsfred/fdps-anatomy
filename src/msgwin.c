/* msgwin.c -- the dialogue message window: opening and closing it, waiting for
 * the key that dismisses it, the two-choice prompt, and the character portrait
 * that sits beside the text.
 *
 * The portrait is the one piece of state this file owns the lifetime of.  It
 * lives in the global data_fdps_portrait_sprite_buf_ptr (gamedata.h), holds at
 * most one record of FACE.CEL at a time, and is deliberately left allocated
 * across calls so that a window repaint can redraw the same image without
 * going back to the file.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "msgwin.h"

/* The portrait sheet, PUSH 0x615b0 at 0001780d against the "rb" at 0x6157c. */
#define PORTRAIT_SHEET_NAME "FACE.CEL"

/* One fread of eight bytes brings back two adjacent directory entries at once
   -- PUSH 0x8 / PUSH 0x1 at 0001785c -- so the record's own start and the
   start of the record after it arrive together and their difference is the
   record's length.  FACE.CEL's directory holds 161 entries for its 160
   records, the last of them the end of the file, so the pair is in range for
   every record including the last. */
#define PORTRAIT_OFFSET_PAIR_BYTES 8

/* PUSH 0x7d and PUSH 0x64 at 000178c6 and 000178c4: the sprite size the sheet
   declares in its own header and that every record of it decodes to.  Written
   as the literals the assembly holds and not read back out of the header --
   nothing here opens the header at all. */
#define PORTRAIT_WIDTH 0x7d
#define PORTRAIT_HEIGHT 0x64

/* PUSH 0x0 twice at 000178bc: mode 0, the plain RLE decode, with the mode
   operand that mode never reads. */
#define PORTRAIT_BLIT_MODE 0
#define PORTRAIT_BLIT_MODE_OPERAND 0

/* CMP dword ptr [EBP + 0x1c],-0x1 at 000177fd: the index that means "no
   portrait", tested for equality and so not a sign test. */
#define PORTRAIT_NONE (-1)

/* 000177d0.  Two branches carry the body.  CMP [0x00060120],0x0 / JZ at
   000177dc skips the free, and CMP [EBP + 0x1c],-0x1 / JZ at 000177fd jumps
   straight to the epilogue at 000178da -- so the release at the top runs
   whatever the index is, and everything from the fopen down is the "an index
   was asked for" arm.  Nothing else branches; the not-found block ends in CALL
   exit, which is why the ADD ESP,0x4 at 00017839 behind it is unreachable.

   The store MOV [0x00060120],0x0 at 000177f3 is unconditional and sits outside
   the free's JZ, so the global is nulled even on the path that had nothing to
   free.

   The eight bytes of the directory read land in two ADJACENT stack slots,
   [EBP-0x10] and [EBP-0xc], addressed by one LEA EAX,[EBP + -0x10] at
   00017860, and only the second is subtracted from at 0001786c.  They are
   emitted as one two-element array because that adjacency is what the read
   depends on; two separate locals leave the compiler free to order them
   apart.

   No result is checked but the fopen: neither malloc, nor either fread's
   count, nor the fseeks.  A short or truncated sheet is not detected here.

   The blit reads the buffer back out of the global rather than out of the
   malloc's EAX -- PUSH dword ptr [0x00060120] at 000178cc -- and the buffer is
   still allocated at the RET. */
void fdps_load_and_draw_portrait(unsigned char *dest, int dest_pitch,
                                 int portrait_index)
{
    int record_bytes;
    FILE *sheet;
    int record_offsets[2];

    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
    }
    data_fdps_portrait_sprite_buf_ptr = NULL;
    if (portrait_index == PORTRAIT_NONE) {
        return;
    }

    sheet = fopen(PORTRAIT_SHEET_NAME, "rb");
    if (sheet == NULL) {
        printf("File not found: 'FACE.CEL'\n");
        exit(1);
    }
    fseek(sheet, portrait_index * 4 + (long) sizeof(struct fdps_cel_header),
          SEEK_SET);
    fread(record_offsets, 1, PORTRAIT_OFFSET_PAIR_BYTES, sheet);
    record_bytes = record_offsets[1] - record_offsets[0];
    data_fdps_portrait_sprite_buf_ptr = malloc(record_bytes);
    fseek(sheet, record_offsets[0], SEEK_SET);
    fread(data_fdps_portrait_sprite_buf_ptr, 1, record_bytes, sheet);
    fclose(sheet);

    fdps_blit_dispatch(data_fdps_portrait_sprite_buf_ptr, dest,
                       PORTRAIT_WIDTH, PORTRAIT_HEIGHT, dest_pitch,
                       PORTRAIT_BLIT_MODE_OPERAND, PORTRAIT_BLIT_MODE);
}
