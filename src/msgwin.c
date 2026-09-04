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
#include <string.h>
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

/* Where the two option pictures go on the composition page.  PUSH 0xf9 at
   00017af4 and 00017b42 for the left cell, PUSH 0x113 at 00017ad4 and
   00017b68 for the right, and the row is the local the prologue seeds with
   MOV dword ptr [EBP + -0x8],0x7b at 000179b1.  At a 360 pitch and the scene
   layers' 20-row, 20-column apron those are screen (229, 103) and (255, 103),
   two 24 x 24 cells with two columns of gap between them. */
#define OPTION_LEFT_X 0xf9
#define OPTION_RIGHT_X 0x113
#define OPTION_ROW_Y 0x7b

/* The option pictures themselves, out of Shadow.cel (gamedata.h).  That sheet
   holds fourteen 24 x 24 sprites: 0 to 3 are the unit shadows mapdraw.c and
   statwin.c draw, and 4 to 13 are this prompt's, five per side.  The side the
   player is not on shows its single still picture; the side he is on cycles
   the four frames above it, so the highlighted option is the one that moves.

   ADD EDX,0x4 at 00017b26 and PUSH 0x8 at 00017b50 for the left cell,
   ADD EDX,0x9 at 00017b94 and PUSH 0xd at 00017ae2 for the right. */
#define OPTION_LEFT_ANIM_FIRST_SPRITE 4
#define OPTION_LEFT_STILL_SPRITE 8
#define OPTION_RIGHT_ANIM_FIRST_SPRITE 9
#define OPTION_RIGHT_STILL_SPRITE 0xd

/* One frame every three ticks, the frame taken modulo four.  Both divisions
   are IDIV against a sign-extended dividend -- MOV EDX,[EBP-0x18] / SAR
   EDX,0x1f in front of each, at 00017b10 and 00017b21 -- so the tick they
   divide is read as a signed int, and the second one is a real remainder and
   not the AND a power of two would allow.  That is what the cast and the %
   at the calls below reproduce. */
#define OPTION_ANIM_TICKS_PER_PHASE 3
#define OPTION_ANIM_PHASES 4

/* PUSH 0x0 twice in front of every cel blit here: mode 0, the opaque
   pass-through, with the operand that mode never reads. */
#define OPTION_BLIT_MODE 0
#define OPTION_BLIT_MODE_OPERAND 0

/* The lowest scancode this loop throws away.  CMP dword ptr [EBP + -0xc],0x7f
   / JGE at 00017c1e, which is NOT the JLE 0x7f that ends the wait above: the
   test here is >= and the one there is <=, so 0x7f itself is a code this loop
   ignores and one that loop acts on.  Both readings are of the same widened
   byte -- AND EAX,0xff at 00017c16 -- so the queue-empty marker 0xff is above
   the threshold either way. */
#define PROMPT_SCANCODE_IGNORED_FROM 0x7f

/* The scancodes the prompt answers to.  These are the ring's make codes
   (keybd.h), not ASCII, and they are the same six menu.c reads. */
#define PROMPT_KEY_ESC 0x01
#define PROMPT_KEY_ENTER 0x1c
#define PROMPT_KEY_SPACE 0x39
#define PROMPT_KEY_KEYPAD_DEL 0x53
#define PROMPT_KEY_LEFT 0x4b
#define PROMPT_KEY_RIGHT 0x4d

/* What the routine answers with.  The left cell is option 0 and the selection
   the loop starts on -- MOV dword ptr [EBP + -0x14],0x0 at 000179a3 -- and
   every one of the twenty-two call sites tests the result against 0, so the
   left cell is the affirmative one. */
#define CHOICE_LEFT 0
#define CHOICE_RIGHT 1
#define CHOICE_CANCELLED (-1)

/* 00017990.  The modal two-option prompt: it takes over the screen, animates
   the two option cells over whatever the caller left there, and does not
   return until the player commits.  0 is the left cell, 1 the right, -1 a
   cancel.

   IT IS A while LOOP AND NOT A do-while, though the flag it tests is zero on
   entry so the first pass always runs.  The test at 00017a13 is both the
   entry and the target of the JMP at 00017c71, and the JNZ out of it goes
   straight to the free at 00017c76.

   THE KEY IS READ AT THE BOTTOM OF THE PASS, after the frame has been
   presented -- which is the opposite of fdps_message_window_wait_key above,
   where the read is at the top and a key already down dismisses the window
   with nothing drawn.  Here a full frame is always drawn and shown before any
   key is looked at, and the queue is thrown away at the entry
   (fdps_flush_keyboard_queue at 000179b8) so the key that opened the prompt
   cannot answer it.

   THE PANEL IT ANIMATES OVER IS LIFTED OFF THE VISIBLE SCREEN, not off a
   page: the source is the adapter at 0xa9609, the same 302 x 73 rectangle at
   screen (9, 120) the message window occupies, so what is captured is the
   panel and the question the caller has already drawn into it.  It is
   captured once, before the loop, and put back on every pass; the four corner
   pixels are zeroed in the copy so the transparent put-back leaves the
   corners rounded over a live battle map.

   THE SELECTION SURVIVES A CANCEL ONLY AS -1.  Esc and keypad Del set the
   flag and overwrite the selection in the same arm at 00017c45, so a cancel
   after an arrow key still answers -1 and never the option that was
   highlighted.

   Confirm keeps whatever is highlighted and writes nothing: the arm at
   00017c30 sets the loop flag alone.

   THE PACING SPIN NEEDS data_fdps_timer_tick_counter TO BE volatile, for the
   same reason the wait above does; it is qualified at the declaration in
   gamedata.h.  Neither malloc is tested, and the only values used after a
   CALL are inp's status byte and fdps_read_keyboard_queue's scancode. */
int fdps_prompt_two_choice(void)
{
    /* The prompt panel lifted off the visible screen on entry, at its own
       302-byte pitch, and put back over the background on every pass. */
    unsigned char *saved_panel;
    /* The 360x240 page this pass composes its frame on, thrown away at the
       end of the pass. */
    unsigned char *frame;
    /* The timer tick as it stood at the end of the previous pass.  It paces
       the loop and it is what the highlighted cell's animation phase is taken
       from, so the first pass always draws phase 0. */
    unsigned int last_tick;
    /* Which cell is highlighted: 0 the left, 1 the right, -1 once a cancel
       has been taken.  It is also the answer. */
    int selection;
    /* Set by a confirm or a cancel, and the only way out of the loop. */
    int confirmed;
    /* The make code this pass took out of the ring, widened from the byte
       fdps_read_keyboard_queue answers with. */
    int scancode;
    /* The page row both option cells are drawn at.  The original keeps it in
       a frame slot rather than pushing the literal twice. */
    int option_row_y;

    last_tick = 0;
    selection = CHOICE_LEFT;
    confirmed = 0;
    option_row_y = OPTION_ROW_Y;

    fdps_flush_keyboard_queue();
    saved_panel = (unsigned char *) malloc((size_t) MESSAGE_WINDOW_BYTES);
    fdps_blit_rect(VGA_SCREEN_BASE + MESSAGE_WINDOW_AT, VGA_SCREEN_PITCH,
                   saved_panel, MESSAGE_WINDOW_W, MESSAGE_WINDOW_W,
                   MESSAGE_WINDOW_H);
    saved_panel[0] = 0;
    saved_panel[MESSAGE_WINDOW_TOP_RIGHT] = 0;
    saved_panel[MESSAGE_WINDOW_BOTTOM_LEFT] = 0;
    saved_panel[MESSAGE_WINDOW_BOTTOM_RIGHT] = 0;

    while (confirmed == 0) {
        fdps_cycle_scene_palette();
        frame = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);
        if (data_fdps_village_mode_flag == 0) {
            fdps_draw_scene_layers(frame);
        } else {
            fdps_blit_rect(VGA_SCREEN_BASE + SCREEN_WINDOW_AT,
                           VGA_SCREEN_PITCH, frame + SCENE_PAGE_WINDOW_AT,
                           SCENE_PAGE_PITCH, SCREEN_WINDOW_W,
                           SCREEN_WINDOW_H);
        }
        fdps_blit_transparent_rect(saved_panel, MESSAGE_WINDOW_W,
                                   frame + SCENE_PAGE_MESSAGE_WINDOW_AT,
                                   SCENE_PAGE_PITCH, MESSAGE_WINDOW_W,
                                   MESSAGE_WINDOW_H);
        if (data_fdps_portrait_sprite_buf_ptr != NULL) {
            fdps_blit_dispatch(data_fdps_portrait_sprite_buf_ptr,
                               frame + SCENE_PAGE_PORTRAIT_AT,
                               PORTRAIT_WIDTH, PORTRAIT_HEIGHT,
                               SCENE_PAGE_PITCH, PORTRAIT_BLIT_MODE_OPERAND,
                               PORTRAIT_BLIT_MODE);
        }

        /* The still cell goes down first and the animated one after it, in
           both arms, so the highlighted side is always the last thing drawn
           over the panel. */
        if (selection == CHOICE_LEFT) {
            fdps_cel_blit_sprite(data_fdps_shadow_sprite_sheet_ptr,
                                 OPTION_RIGHT_STILL_SPRITE, frame,
                                 SCENE_PAGE_PITCH, OPTION_RIGHT_X,
                                 option_row_y, OPTION_BLIT_MODE_OPERAND,
                                 OPTION_BLIT_MODE);
            fdps_cel_blit_sprite(data_fdps_shadow_sprite_sheet_ptr,
                                 OPTION_LEFT_ANIM_FIRST_SPRITE
                                     + ((int) last_tick
                                        / OPTION_ANIM_TICKS_PER_PHASE)
                                       % OPTION_ANIM_PHASES,
                                 frame, SCENE_PAGE_PITCH, OPTION_LEFT_X,
                                 option_row_y, OPTION_BLIT_MODE_OPERAND,
                                 OPTION_BLIT_MODE);
        } else {
            fdps_cel_blit_sprite(data_fdps_shadow_sprite_sheet_ptr,
                                 OPTION_LEFT_STILL_SPRITE, frame,
                                 SCENE_PAGE_PITCH, OPTION_LEFT_X,
                                 option_row_y, OPTION_BLIT_MODE_OPERAND,
                                 OPTION_BLIT_MODE);
            fdps_cel_blit_sprite(data_fdps_shadow_sprite_sheet_ptr,
                                 OPTION_RIGHT_ANIM_FIRST_SPRITE
                                     + ((int) last_tick
                                        / OPTION_ANIM_TICKS_PER_PHASE)
                                       % OPTION_ANIM_PHASES,
                                 frame, SCENE_PAGE_PITCH, OPTION_RIGHT_X,
                                 option_row_y, OPTION_BLIT_MODE_OPERAND,
                                 OPTION_BLIT_MODE);
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

        scancode = fdps_read_keyboard_queue();
        if (scancode < PROMPT_SCANCODE_IGNORED_FROM) {
            if (scancode == PROMPT_KEY_ENTER || scancode == PROMPT_KEY_SPACE) {
                confirmed = 1;
            } else if (scancode == PROMPT_KEY_ESC
                       || scancode == PROMPT_KEY_KEYPAD_DEL) {
                confirmed = 1;
                selection = CHOICE_CANCELLED;
            } else if (scancode == PROMPT_KEY_LEFT) {
                selection = CHOICE_LEFT;
            } else if (scancode == PROMPT_KEY_RIGHT) {
                selection = CHOICE_RIGHT;
            }
        }
    }

    free(saved_panel);
    return selection;
}

/* The whole mode 13h page, which is what this routine saves and puts back:
   200 rows of VGA_SCREEN_PITCH, PUSH 0xfa00 in front of all three mallocs and
   all six memmoves, and the 0xc8 the band height is subtracted from at
   0002067e. */
#define VGA_SCREEN_ROWS 0xc8
#define VGA_SCREEN_BYTES 0xfa00

/* Where the panel stands when the slide is over: screen (9, 120), pushed as
   PUSH 0x9 / PUSH 0x78 at 0002075a and 00020758.  The same 9 is the column
   every intermediate frame blends at, and 120 is the last entry of the row
   table below, which is why the slide ends exactly where the still panel is
   drawn. */
#define MESSAGE_WINDOW_COL 0x9
#define MESSAGE_WINDOW_ROW 0x78

/* Message.cel holds the panel as its sprite 0 -- PUSH 0x0 for the index in
   both blits, at 00020635 and 00020765 -- and both blits are mode 0, the
   plain opaque RLE decode, with the mode operand 0. */
#define MESSAGE_WINDOW_SPRITE 0
#define MESSAGE_WINDOW_BLIT_MODE 0
#define MESSAGE_WINDOW_BLIT_MODE_OPERAND 0

/* The slide: six frames, and the blend weight of frame i is i * 2 + 5.  ADD
   EAX,EAX / ADD EAX,0x5 at 0002069a, against the CMP 0x6 at 0002064c.
   fdps_blit_blend_rect divides by sixteen, so the panel arrives at 5/16, 7/16,
   9/16, 11/16, 13/16 and 15/16 -- it is never blended at 16/16, because the
   still frame below is an opaque blit and not a blend. */
#define MESSAGE_WINDOW_SLIDE_STEPS 6
#define MESSAGE_WINDOW_FIRST_ALPHA 5
#define MESSAGE_WINDOW_ALPHA_STEP 2

/* Where the portrait goes on the visible page: PUSH 0xa708c at 00020803, which
   at a 320 pitch is screen (12, 90).  The base is the adapter's own linear
   address, so only the offset is a placement (contract E). */
#define SCREEN_PORTRAIT_AT 0x708c

/* 000205b0.  One counted loop over a six-entry table and one two-armed tail;
   everything else is straight line.  The loop is Watcom's usual shape -- the
   test at 0002064c with JL into the body and JMP past it, the increment at
   0002065a, the JMP back at 0002073a -- and the tail is CMP [EBP+0x14],0x0 /
   JGE at 000207d1, so the negative arm is the fall-through and the portrait is
   the branch taken.

   THE SEVEN FRAMES ARE ALL COMPOSED FROM THE SAME SAVED SCREEN.  The visible
   page is copied into screen_backup once, before anything is drawn, and every
   frame starts by copying that backup over the work page.  So the slide never
   accumulates: frame i is the original screen with the panel blended at row
   table[i] and nothing else, and the still frame at the end is the original
   screen with the panel blitted opaque.  A body that dropped either restore
   would leave every earlier frame's band underneath the next one.

   THE BAND IS CLIPPED AT THE BOTTOM OF THE SCREEN AND NOWHERE ELSE.
   band_height is 200 - row clipped down to the panel's own 73, which for the
   first four rows of the table (190, 170, 150, 135) is the part of the panel
   that fits above row 200 and for the last two (127, 120) is the whole panel.
   Nothing here clips the width and nothing checks the left edge: the blend is
   handed a raw work_page + row * 320 + 9 and would run off the end of the
   64000-byte page if the clip were dropped, because 190 + 73 rows at a 320
   pitch is 20,000 bytes past it.

   THE RETRACE WAIT IS TWO-PHASE AND HAS TO STAY THAT WAY.  Each present spins
   until bit 3 of 0x3da is set and then until it is clear, so the 64000-byte
   copy begins at the top of active display rather than inside the blanking
   interval.  The single-phase wait-for-set that the same idiom is usually
   written with starts every copy one blanking period earlier and changes what
   the reveal looks like (rebuild_info/pitfalls.md).

   THE PANEL BUFFER IS ZEROED BEFORE THE CEL IS DECODED INTO IT.  Message.cel's
   sprite 0 has transparent pixels, and mode 0 leaves them as it found them, so
   without the memset the blend would read whatever malloc handed back through
   them.  Zero is also the palette index the blend folds to nothing much, which
   is what makes the panel's rounded corners come out dark rather than random.

   ONLY THE STILL PANEL AND THE PORTRAIT SURVIVE THE CALL.  The six blended
   frames are transient by construction -- each is wiped by the next frame's
   restore and the last of them by the restore in front of the still blit -- so
   what a caller sees afterwards is the original screen, the opaque panel at
   (9, 120) and, if there is a speaker, the portrait over it at (12, 90). */
void fdps_message_window_open(int face_index)
{
    /* The row the panel's top edge sits at in each of the six frames, copied
       onto the stack from the read-only template at 0001fd50.  It ends at 120,
       the row the still panel is drawn at. */
    int slide_in_row[MESSAGE_WINDOW_SLIDE_STEPS] =
        { 190, 170, 150, 135, 127, 120 };
    /* The off-screen page every frame is composed on and presented from. */
    unsigned char *work_page;
    /* Message.cel's sprite 0 decoded once, packed at its own 302 pitch: the
       foreground of all six blends. */
    unsigned char *panel;
    /* The visible page as it stood on entry.  Every frame starts from it. */
    unsigned char *screen_backup;
    /* Which of the six slide frames is being composed. */
    int step;
    /* How many of the panel's 73 rows fit above the bottom of the screen at
       this frame's row. */
    int band_height;

    work_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    screen_backup = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    panel = (unsigned char *) malloc((size_t) MESSAGE_WINDOW_BYTES);

    memmove(screen_backup, (void *) VGA_SCREEN_BASE,
            (size_t) VGA_SCREEN_BYTES);
    memset(panel, 0, (size_t) MESSAGE_WINDOW_BYTES);
    fdps_cel_blit_sprite(data_fdps_message_window_sheet_ptr,
                         MESSAGE_WINDOW_SPRITE, panel, MESSAGE_WINDOW_W, 0, 0,
                         MESSAGE_WINDOW_BLIT_MODE_OPERAND,
                         MESSAGE_WINDOW_BLIT_MODE);

    for (step = 0; step < MESSAGE_WINDOW_SLIDE_STEPS; step++) {
        memmove(work_page, screen_backup, (size_t) VGA_SCREEN_BYTES);

        band_height = VGA_SCREEN_ROWS - slide_in_row[step];
        if (band_height > MESSAGE_WINDOW_H) {
            band_height = MESSAGE_WINDOW_H;
        }

        fdps_blit_blend_rect(panel, MESSAGE_WINDOW_W,
                             work_page
                                 + slide_in_row[step] * VGA_SCREEN_PITCH
                                 + MESSAGE_WINDOW_COL,
                             VGA_SCREEN_PITCH,
                             work_page
                                 + slide_in_row[step] * VGA_SCREEN_PITCH
                                 + MESSAGE_WINDOW_COL,
                             VGA_SCREEN_PITCH, MESSAGE_WINDOW_W, band_height,
                             data_fdps_palette_shade_ramp_table,
                             data_fdps_inverse_palette_cube,
                             step * MESSAGE_WINDOW_ALPHA_STEP
                                 + MESSAGE_WINDOW_FIRST_ALPHA);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the copy starts at the top of the frame. */
        }
        memmove((void *) VGA_SCREEN_BASE, work_page,
                (size_t) VGA_SCREEN_BYTES);
    }

    memmove(work_page, screen_backup, (size_t) VGA_SCREEN_BYTES);
    fdps_cel_blit_sprite(data_fdps_message_window_sheet_ptr,
                         MESSAGE_WINDOW_SPRITE, work_page, VGA_SCREEN_PITCH,
                         MESSAGE_WINDOW_COL, MESSAGE_WINDOW_ROW,
                         MESSAGE_WINDOW_BLIT_MODE_OPERAND,
                         MESSAGE_WINDOW_BLIT_MODE);

    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        /* Spin until the retrace begins. */
    }
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        /* And until it ends, so the copy starts at the top of the frame. */
    }
    memmove((void *) VGA_SCREEN_BASE, work_page, (size_t) VGA_SCREEN_BYTES);

    free(work_page);
    free(screen_backup);
    free(panel);

    if (face_index < 0) {
        if (data_fdps_portrait_sprite_buf_ptr != NULL) {
            free(data_fdps_portrait_sprite_buf_ptr);
        }
        data_fdps_portrait_sprite_buf_ptr = NULL;
    } else {
        fdps_load_and_draw_portrait(
            (unsigned char *) (VGA_SCREEN_BASE + SCREEN_PORTRAIT_AT),
            VGA_SCREEN_PITCH, face_index);
    }
}

/* 00020820.  One counted loop over a six-entry table, and nothing else
   branches: the test at 0002090f is CMP [EBP-0xc],0x0 / JGE into the body with
   a JMP past it, the step is the DEC at 0002091d, and the only other test in
   the body is the band clip's CMP 0x49 / JLE at 0002094d.  The loop runs
   DOWNWARDS, from 5 to 0, which is what turns the same row table the open
   above walks forwards into a slide off the bottom of the screen.

   IT DOES NOT TAKE THE WINDOW THAT IS ON SCREEN BACK DOWN.  The visible page
   is never read.  The backdrop is built from scratch -- fdps_draw_scene_layers
   recomposes the scrolling scene into a 360 x 240 page and its 312 x 192
   viewport is copied into an otherwise zeroed screen page -- and the panel
   that slides is a bare Message.cel sprite 0 with no text and no portrait
   drawn into it.  So the message and the speaker are gone from the first
   animation frame, and whatever else was on the visible page is gone with
   them (rebuild_info/pitfalls.md).

   THE FOUR-PIXEL BORDER IS BLACK BECAUSE THE PAGE IS ZEROED AND THE VIEWPORT
   IS NARROWER THAN THE SCREEN.  memset over the whole 64000 bytes at 00020872
   and then a 312 x 192 copy to byte 0x504 leaves rows 0..3, rows 196..199 and
   four columns at each edge holding index 0, and every frame carries that
   border because every frame is a copy of this page.

   EVERY FRAME IS COMPOSED FROM THAT CLEAN PAGE AND NOT FROM THE LAST ONE.  The
   memmove at 0002092f restores the work page before each blend, so frame i is
   the bare scene with the panel blended at one row -- the bands do not
   accumulate -- and the frame left standing at the end is the clean page
   itself, presented at 00020a32, with no panel on it at all.

   THE BAND IS 200 - row, AND THE CLAMP ONLY BITES ON THE FIRST FRAME.  The
   subtraction at 00020946 is what shortens the band as the panel walks down,
   and the clamp at 0002094d holds it to the panel's own 73 rows.  This loop
   runs the table backwards, so the rows come out 120, 127, 135, 150, 170, 190
   and the bands 80, 73, 65, 50, 30, 10: only the first exceeds 73, and the JLE
   at 00020951 is taken on the other five.  What the clamp bounds is the READ,
   not the write.  An unclamped 80 rows at row 120 would still finish at work
   page byte 120 * 320 + 9 + 79 * 320 + 301 = 63990, inside the page, while
   reading 302 * 80 = 24160 bytes out of a panel buffer that holds only
   302 * 73 = 22046.

   THE RETRACE WAIT IS TWO-PHASE AND HAS TO STAY THAT WAY: spin until bit 3 of
   0x3da is set, then until it is clear, so each 64000-byte present begins at
   the top of active display.  The single-phase wait the idiom is usually
   written with starts every copy one blanking period earlier and changes what
   the six steps look like (rebuild_info/pitfalls.md).

   NOTHING BUT ITS OWN FOUR BUFFERS IS RELEASED.  The scene page goes as soon
   as its viewport has been copied out, and the work page, the clean page and
   the panel go at the end; the portrait buffer
   data_fdps_portrait_sprite_buf_ptr that fdps_message_window_open left loaded
   is deliberately still allocated at the RET, because the next window reuses
   it.  No malloc is checked. */
void fdps_message_window_close(void)
{
    /* The row the panel's top edge sits at in each of the six frames, copied
       onto the stack by the MOVSD.REP at 00020839 from the template at
       0001fd68 -- a second copy of the table the open above takes from
       0001fd50, holding the same six rows in the same order.  This loop reads
       it from the far end, so the panel starts at 120, where the open left it,
       and ends at 190. */
    int slide_out_row[MESSAGE_WINDOW_SLIDE_STEPS] =
        { 190, 170, 150, 135, 127, 120 };
    /* The 360 x 240 page the scrolling scene is recomposed on.  Only its
       312 x 192 viewport is wanted, and it is freed as soon as that has been
       copied out. */
    unsigned char *scene_page;
    /* The bare scene on a screen-sized page: every frame is restored from it,
       and it is the frame left on the visible page at the end. */
    unsigned char *clean_page;
    /* The off-screen page a frame is composed on and presented from. */
    unsigned char *work_page;
    /* Message.cel's sprite 0 decoded once, packed at its own 302 pitch, with
       nothing written over it: the foreground of all six blends. */
    unsigned char *panel;
    /* Which of the six slide frames is being composed, counting down. */
    int step;
    /* How many of the panel's 73 rows fit above the bottom of the screen at
       this frame's row. */
    int band_height;

    scene_page = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);
    fdps_draw_scene_layers(scene_page);

    clean_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    memset(clean_page, 0, (size_t) VGA_SCREEN_BYTES);
    fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                   SCENE_PAGE_PITCH, clean_page + SCREEN_WINDOW_AT,
                   VGA_SCREEN_PITCH, SCREEN_WINDOW_W, SCREEN_WINDOW_H);

    work_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    free(scene_page);

    panel = (unsigned char *) malloc((size_t) MESSAGE_WINDOW_BYTES);
    memset(panel, 0, (size_t) MESSAGE_WINDOW_BYTES);
    fdps_cel_blit_sprite(data_fdps_message_window_sheet_ptr,
                         MESSAGE_WINDOW_SPRITE, panel, MESSAGE_WINDOW_W, 0, 0,
                         MESSAGE_WINDOW_BLIT_MODE_OPERAND,
                         MESSAGE_WINDOW_BLIT_MODE);

    for (step = MESSAGE_WINDOW_SLIDE_STEPS - 1; step >= 0; step--) {
        memmove(work_page, clean_page, (size_t) VGA_SCREEN_BYTES);

        band_height = VGA_SCREEN_ROWS - slide_out_row[step];
        if (band_height > MESSAGE_WINDOW_H) {
            band_height = MESSAGE_WINDOW_H;
        }

        fdps_blit_blend_rect(panel, MESSAGE_WINDOW_W,
                             work_page
                                 + slide_out_row[step] * VGA_SCREEN_PITCH
                                 + MESSAGE_WINDOW_COL,
                             VGA_SCREEN_PITCH,
                             work_page
                                 + slide_out_row[step] * VGA_SCREEN_PITCH
                                 + MESSAGE_WINDOW_COL,
                             VGA_SCREEN_PITCH, MESSAGE_WINDOW_W, band_height,
                             data_fdps_palette_shade_ramp_table,
                             data_fdps_inverse_palette_cube,
                             step * MESSAGE_WINDOW_ALPHA_STEP
                                 + MESSAGE_WINDOW_FIRST_ALPHA);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the copy starts at the top of the frame. */
        }
        memmove((void *) VGA_SCREEN_BASE, work_page,
                (size_t) VGA_SCREEN_BYTES);
    }

    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        /* Spin until the retrace begins. */
    }
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        /* And until it ends, so the copy starts at the top of the frame. */
    }
    memmove((void *) VGA_SCREEN_BASE, clean_page, (size_t) VGA_SCREEN_BYTES);

    free(work_page);
    free(clean_page);
    free(panel);
}
