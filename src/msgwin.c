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
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "keybd.h"
#include "sprite.h"
#include "mapdraw.h"
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

/* Where the display adapter answers and how wide a mode 13h row is.  Both are
   hard-coded in the original -- PUSH 0xa9609 at 00020408, PUSH 0xa0504 at
   00020490 and 00020554, PUSH 0x140 beside each of them -- and stay literals
   here: 0xa0000 is the adapter's own linear address under DOS/4GW and not the
   address of anything the linker places (rebuild_info/pitfalls.md,
   contract E). */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, which is what the present straddles: PUSH 0x3da / CALL inp at
   00020523 and 00020534, TEST AL,0x8 after each. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The message window as fdps_message_window_open leaves it on the visible
   page: 302 x 73 pixels at screen (9, 120), which is byte 0x9609 of a
   320-pitch frame, and 0x561e bytes when its rows are packed at its own width.
   The saved copy is stored at pitch 302, so its rows are contiguous. */
#define MESSAGE_WINDOW_AT 0x9609
#define MESSAGE_WINDOW_W 0x12e
#define MESSAGE_WINDOW_H 0x49
#define MESSAGE_WINDOW_BYTES 0x561e

/* The three corners of the saved copy that are not byte 0.  Written as the
   offsets the assembly holds -- MOV byte ptr [EAX + 0x12d],0x0 and its two
   companions at 0002041e, 00020428 and 00020432 -- and they are the top right,
   bottom left and bottom right of a 302 x 73 rectangle packed at pitch 302:
   0x12d is 301, 0x54f0 is 72 * 302 and 0x561d is 72 * 302 + 301. */
#define MESSAGE_WINDOW_TOP_RIGHT 0x12d
#define MESSAGE_WINDOW_BOTTOM_LEFT 0x54f0
#define MESSAGE_WINDOW_BOTTOM_RIGHT 0x561d

/* The page a pass composes its frame on: 360 x 240 8bpp, PUSH 0x15180 / CALL
   malloc at 0002044c.  It carries the scene layers' 24-pixel apron, so a frame
   pixel is a screen pixel plus 20 rows and 20 columns. */
#define SCENE_PAGE_PITCH 0x168
#define SCENE_PAGE_BYTES 0x15180

/* The part of the screen a pass is allowed to disturb: 312 x 192 at screen
   (4, 4), byte 0x504 of a 320-pitch frame, which is byte 0x21d8 -- pixel
   (24, 24) -- of the composition page. */
#define SCREEN_WINDOW_AT 0x504
#define SCREEN_WINDOW_W 0x138
#define SCREEN_WINDOW_H 0xc0
#define SCENE_PAGE_WINDOW_AT 0x21d8

/* Where the three things drawn over the background land on the composition
   page.  0xc4fd is page pixel (29, 140), the message window's own screen
   (9, 120); 0x106bc is (300, 186), screen (280, 166), the window's bottom
   right corner; 0x9ad0 is (32, 110), screen (12, 90), the portrait's place
   beside the text. */
#define SCENE_PAGE_MESSAGE_WINDOW_AT 0xc4fd
#define SCENE_PAGE_INDICATOR_AT 0x106bc
#define SCENE_PAGE_PORTRAIT_AT 0x9ad0

/* The prompt indicator: Command.cel sprites 0x48 through 0x4b, one phase every
   three ticks, the phase taken modulo four.  ADD EAX,0x48 at 000204dc, the
   IDIV EBX against a 3 at 000204d7 and the AND EAX,0x3 at 000204d9.  The
   division is SIGNED -- MOV EDX,[EBP-0x4] / SAR EDX,0x1f / IDIV rather than
   XOR EDX,EDX / DIV -- so the tick it divides is read as a signed int, which
   is what the cast at the call below reproduces. */
#define WAIT_INDICATOR_FIRST_SPRITE 0x48
#define WAIT_INDICATOR_TICKS_PER_PHASE 3
#define WAIT_INDICATOR_PHASE_MASK 3

/* The highest scancode that ends the wait.  AND EAX,0xff / CMP EAX,0x7f / JLE
   at 0002043e: a make code, 0x01..0x7f, ends it, while the queue-empty marker
   0xff and any code with bit 7 set do not.  The widening is unsigned, which is
   why fdps_read_keyboard_queue's byte return type is load-bearing (keybd.h).

   THE CODE ITSELF IS NOT KEPT.  Nothing between the CALL and the CMP stores
   it, and nothing below reads it, so a caller that needs to know which key was
   pressed has to go to the ring itself.  Written with a local it would still
   behave the same; written without one the frame stays the 0xc bytes the
   original's SUB ESP takes. */
#define SCANCODE_LAST_MAKE_CODE 0x7f

/* 000203d0.  One do-while whose test is at the bottom, with a break at the
   top: 0x00020439 is both the entry into the first pass and the target of the
   JNZ at 00020596, and the JLE at 00020446 leaves straight for the epilogue's
   free at 0002059c.  So the keyboard is read once per pass BEFORE any of the
   drawing, and a pass that finds a make code draws nothing at all -- not even
   the first one, which is what makes a window dismissed by a key already down
   leave the screen exactly as fdps_message_window_open left it.

   THE SAVED WINDOW IS TAKEN OFF THE VISIBLE SCREEN AND NOT OFF A PAGE.  The
   source is the adapter itself at 0xa9609, so what is captured is whatever
   fdps_message_window_open and fdps_draw_text have already put there,
   including the text.  It is captured once, before the loop, and every pass
   blits the same copy back: nothing re-reads the screen for it.

   THE FOUR CORNER PIXELS ARE ZEROED IN THE COPY, NOT ON THE SCREEN, and the
   put-back is fdps_blit_transparent_rect, which skips a source byte of 0.
   That is how the window's corners come out rounded over a live battle map.
   In village mode the background under them is the same screen the copy came
   from, so there they make no difference at all.

   The composition page is allocated and freed inside the loop, once per pass;
   the saved window is allocated once and freed at the return.  Neither malloc
   is tested, and the only values that come back from a CALL and are used are
   fdps_read_keyboard_queue's byte and inp's.

   THE PACING SPIN NEEDS data_fdps_timer_tick_counter TO BE volatile.  Nothing
   inside `while (last_tick == data_fdps_timer_tick_counter)` writes it --
   fdps_timer_tick_handler does, from the timer interrupt -- so over a plain
   int wcc386 loads it once and the game stops dead here.  It is qualified at
   the declaration in gamedata.h (rebuild_info/pitfalls.md). */
void fdps_message_window_wait_key(int show_wait_indicator, int timeout_ticks)
{
    /* The message window lifted off the visible screen on entry, at its own
       302-byte pitch, and blitted back over the background on every pass. */
    unsigned char *saved_window;
    /* The 360x240 page this pass composes its frame on, thrown away at the end
       of the pass. */
    unsigned char *frame;
    /* The timer tick as it stood at the end of the previous pass.  It paces
       the loop -- a pass ends when the counter has moved past it -- and it is
       also what the indicator's phase is derived from, so the first pass
       always draws phase 0. */
    unsigned int last_tick;

    last_tick = 0;
    saved_window = (unsigned char *) malloc((size_t) MESSAGE_WINDOW_BYTES);
    fdps_blit_rect(VGA_SCREEN_BASE + MESSAGE_WINDOW_AT, VGA_SCREEN_PITCH,
                   saved_window, MESSAGE_WINDOW_W, MESSAGE_WINDOW_W,
                   MESSAGE_WINDOW_H);
    saved_window[0] = 0;
    saved_window[MESSAGE_WINDOW_TOP_RIGHT] = 0;
    saved_window[MESSAGE_WINDOW_BOTTOM_LEFT] = 0;
    saved_window[MESSAGE_WINDOW_BOTTOM_RIGHT] = 0;

    do {
        if (fdps_read_keyboard_queue() <= SCANCODE_LAST_MAKE_CODE) {
            break;
        }

        frame = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);
        if (data_fdps_village_mode_flag == 0) {
            fdps_draw_scene_layers(frame);
        } else {
            fdps_blit_rect(VGA_SCREEN_BASE + SCREEN_WINDOW_AT,
                           VGA_SCREEN_PITCH, frame + SCENE_PAGE_WINDOW_AT,
                           SCENE_PAGE_PITCH, SCREEN_WINDOW_W,
                           SCREEN_WINDOW_H);
        }
        fdps_blit_transparent_rect(saved_window, MESSAGE_WINDOW_W,
                                   frame + SCENE_PAGE_MESSAGE_WINDOW_AT,
                                   SCENE_PAGE_PITCH, MESSAGE_WINDOW_W,
                                   MESSAGE_WINDOW_H);
        if (show_wait_indicator != 0) {
            fdps_blit_command_sprite(frame + SCENE_PAGE_INDICATOR_AT,
                                     SCENE_PAGE_PITCH,
                                     WAIT_INDICATOR_FIRST_SPRITE
                                         + (((int) last_tick
                                             / WAIT_INDICATOR_TICKS_PER_PHASE)
                                            & WAIT_INDICATOR_PHASE_MASK));
        }
        if (data_fdps_portrait_sprite_buf_ptr != NULL) {
            fdps_blit_dispatch(data_fdps_portrait_sprite_buf_ptr,
                               frame + SCENE_PAGE_PORTRAIT_AT,
                               PORTRAIT_WIDTH, PORTRAIT_HEIGHT,
                               SCENE_PAGE_PITCH, PORTRAIT_BLIT_MODE_OPERAND,
                               PORTRAIT_BLIT_MODE);
        }

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that goes out is
               shown whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the copy below starts clear of it. */
        }
        fdps_blit_rect((unsigned int) (frame + SCENE_PAGE_WINDOW_AT),
                       SCENE_PAGE_PITCH,
                       (void *) (VGA_SCREEN_BASE + SCREEN_WINDOW_AT),
                       VGA_SCREEN_PITCH, SCREEN_WINDOW_W, SCREEN_WINDOW_H);

        while (last_tick == data_fdps_timer_tick_counter) {
            /* One pass per game tick.  See the note above: the counter has to
               be volatile or this never ends. */
        }
        last_tick = data_fdps_timer_tick_counter;
        free(frame);
        timeout_ticks--;
    } while (timeout_ticks != 0);

    free(saved_window);
}
