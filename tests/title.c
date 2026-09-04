/* tests/title.c -- cover for src/title.c.
 *
 * fdps_play_movie @ 00030f40 and fdps_show_game_over @ 0002a960.  The
 * game-over cases are at the end of the file and set out what they assert
 * against above themselves; everything down to run_title_tests is the movie.
 *
 * Expected values come from the assembly at 00030f40: the CALL kbhit / TEST
 * EAX,EAX / JZ / CALL getch / JMP at 00030f56..00030f64 that makes the drain a
 * top-tested loop over kbhit's return; the six pushes of the first palette
 * upload at 00030f6b..00030f78 -- -0x40, -0x40, -0x40, 0xff, 0, [0x000643bc] --
 * and of the second at 00031006..00031013, which differ only in the three
 * biases; PUSH 0xfa00 / PUSH 0x0 / PUSH 0xa0000 at 00030ff2..00030ff9 for the
 * frame clear; the CALL 0x00056818 at 00030f51 and the CALL 0x000567f0 at
 * 00031021 that bracket the whole body with the keyboard hook's uninstall and
 * install; and PUSH 0x19 / CALL fdps_audio_init at 00031026.  None of them is
 * read off the emitted C.
 *
 * WHAT THE RUN IS OBSERVED THROUGH.  This function returns nothing and writes
 * no global of its own: everything it does is to the machine and to two other
 * subsystems.  Four of those effects can be read back from inside the test
 * process, and between them they cover every step of the body that is not the
 * spawn:
 *
 *   the mode 13h aperture, for the frame clear.  A VGA in text mode does not
 *     decode 0xa0000 at all, so the case below puts the adapter into mode 13h
 *     -- the mode the game is in when a movie starts -- fills the whole 64K
 *     window with a sentinel, and reads back both the frame and the two bytes
 *     past its end.
 *   the DAC, for the closing palette upload.  The master palette global is
 *     pointed at a fixture whose three channels disagree with each other and
 *     with the entry number, all inside the DAC's six bits so that an unbiased
 *     upload is the identity and nothing is clamped.
 *   interrupt vector 09h and the two globals the keyboard hook saves it in,
 *     for the uninstall at the top and the install at the bottom.  The hook is
 *     put on before the call so that the two are distinguishable: the vector
 *     the install files away is the one the uninstall put back, and it is not
 *     the hook's own handler.
 *   AIL's timer table, for the shutdown at the top and the bring-up at the
 *     bottom.  A timer is registered before the call and the next free slot is
 *     read after it: 4 can only mean that the registration was released and
 *     exactly one new one taken.
 *
 * WHY THE CALL IS FENCED.  Two of the steps reach outside the program's memory
 * in ways a test has to bound.  Vector 09h spends the tail of the call pointing
 * at the build's fdps_keyboard_isr, which is the stub -- `return 0`, a RET
 * where the CPU will have pushed an interrupt frame -- so IRQ1 is masked at the
 * 8259 for the duration and the vector is put back byte for byte afterwards,
 * the same fence tests/keybd.c uses for the same reason.  And the CD stop
 * command is staged through the DOS conventional-memory block cd.c allocates:
 * without that allocation the stage would memcpy thirteen bytes to linear
 * address 0, so the block is allocated first, exactly as main does at startup.
 *
 * WHAT IS LEFT TO THE PLAYTEST.  The spawn is the one step with no unit
 * observable.  What it does here is fail -- the composed player path names a
 * file that is not staged -- and that failure is itself part of what is
 * asserted: every assertion below is about a step that runs after the spawn,
 * so together they say that a spawn which did not happen does not stop the
 * function.  Which arguments fd.exe was handed, and that P_WAIT holds the game
 * until the movie ends, are visible only with the real player on the real CD.
 * The fade-out upload is in the same position: it and the closing upload write
 * the same DAC, the closing one runs unconditionally and last, so the black
 * frame in between has no state left behind it (rebuild_info/pitfalls.md).
 */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testharn.h"
#include "ailv3.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "blit.h"
#include "cd.h"
#include "keybd.h"
#include "palette.h"
#include "sprite.h"
#include "vfs.h"
#include "title.h"

/* The aperture, the frame inside it, and the window it sits in.  The frame
   stops at 64000 and the window is 64K, so the bytes above the frame are where
   a clear that ran long would land. */
#define MOVIE_BASE 0x000a0000
#define MOVIE_FRAME_BYTES 0xfa00
#define MOVIE_WINDOW_BYTES 0x10000
#define MOVIE_TAIL_FIRST MOVIE_FRAME_BYTES
#define MOVIE_TAIL_LAST (MOVIE_WINDOW_BYTES - 1)

#define MOVIE_MODE_TEXT 0x03
#define MOVIE_MODE_320X200X256 0x13

#define MOVIE_DAC_READ_INDEX 0x3c7
#define MOVIE_DAC_DATA 0x3c9
#define MOVIE_DAC_ENTRIES 256
#define MOVIE_DAC_COMPONENT_MASK 0x3f

/* What the window is filled with before the run.  The clear writes zero, so
   the sentinel is a value the clear cannot produce. */
#define MOVIE_SENTINEL 0x5a

/* The BIOS keyboard queue, in the BIOS data area that DOS/4GW identity maps
   along with the rest of the first megabyte.  The head and tail are offsets
   into segment 0x40, and the ring runs from 0x1e to 0x3d inclusive. */
#define BIOS_DATA_BASE 0x400
#define BIOS_KBD_HEAD_ADDR (BIOS_DATA_BASE + 0x1a)
#define BIOS_KBD_TAIL_ADDR (BIOS_DATA_BASE + 0x1c)
#define BIOS_KBD_RING_START 0x1e
#define BIOS_KBD_RING_END 0x3e
#define BIOS_KBD_RING_ADDR (BIOS_DATA_BASE + BIOS_KBD_RING_START)
#define BIOS_KBD_RING_BYTES (BIOS_KBD_RING_END - BIOS_KBD_RING_START)

/* Scancode 0x1c with ASCII 0x0d: Return.  An ordinary printable key, so DOS
   delivers it as one character and not as the two reads an extended key takes,
   and it is not the Ctrl-C that INT 21h AH=08h treats specially. */
#define MOVIE_QUEUED_KEY 0x1c0d
#define MOVIE_QUEUED_KEY_COUNT 2

/* AIL hands out timer handles as byte offsets into its fifteen-slot table, 0,
   4, 8 and so on (the allocator at 00044f4e), so the next free slot after
   exactly one registration is 4. */
#define AIL_TIMER_SLOT_STRIDE 4
#define NO_TIMER_CALLBACK 0

/* Parked in the latched scancode before the run so that the 0xff the
   uninstaller writes is visible as a write rather than as a value that was
   already there.  Not 0xff and not a scancode any key produces. */
#define MOVIE_SCANCODE_MARKER 0x42

/* The stem fdps_play_ending_credit_roll passes, the literal at 0x61768. */
#define MOVIE_NAME "End"

/* INT 21h AH=35h for vector 09h: offset as the result, selector through the
   pointer.  Separate in-line assembly from src/keybd.c's own probes on
   purpose -- this file reads the vector back independently and compares it
   against what the function under test filed away. */
extern unsigned int movie_read_int9_vector(unsigned short *selector_out);
#pragma aux movie_read_int9_vector =    \
    "push es"                           \
    "push esi"                          \
    "mov  eax,3509h"                    \
    "int  21h"                          \
    "mov  ax,es"                        \
    "pop  esi"                          \
    "mov  [esi],ax"                     \
    "pop  es"                           \
    parm [esi]                          \
    value [ebx]                         \
    modify [eax ebx ecx edx];

/* INT 21h AH=25h for vector 09h with an arbitrary selector:offset, which is
   what putting the original handler back needs. */
extern void movie_write_int9_vector(unsigned short handler_selector,
                                    unsigned int handler_offset);
#pragma aux movie_write_int9_vector =   \
    "push ds"                           \
    "mov  eax,2509h"                    \
    "mov  ds,cx"                        \
    "int  21h"                          \
    "pop  ds"                           \
    parm [cx] [edx]                     \
    modify [eax ebx ecx edx];

extern unsigned short movie_current_cs(void);
#pragma aux movie_current_cs = "mov ax,cs" value [ax] modify [eax];

/* Set bit 1 of the master 8259's interrupt mask register so IRQ1 cannot be
   delivered, and hand the mask back as it was so it can be put back byte for
   byte.  Masking stops the interrupt rather than deferring it the way CLI
   would, and deferral would not hold: DOS re-enables interrupts inside the
   very INT 21h calls the function under test makes. */
extern unsigned char movie_mask_irq1(void);
#pragma aux movie_mask_irq1 =           \
    "in   al,21h"                       \
    "mov  ah,al"                        \
    "or   al,2"                         \
    "out  21h,al"                       \
    "mov  al,ah"                        \
    value [al]                          \
    modify [eax];

extern void movie_restore_irq_mask(unsigned char mask);
#pragma aux movie_restore_irq_mask = "out 21h,al" parm [al] modify [eax];

/* The master palette the run uploads from.  Three channels that disagree with
   each other and with the entry number, all inside 0..63 so that the unbiased
   upload is the identity and the clamp never fires. */
static unsigned char movie_pal[MOVIE_DAC_ENTRIES * 3];

/* What the one run saw.  File scope because each case asserts about a
   different part of the same single observation. */
static int movie_ran;
static int movie_frame_non_zero;
static int movie_tail_first;
static int movie_tail_last;
static int movie_dac_mismatches;
static int movie_dac_last_red;
static int movie_dac_last_green;
static int movie_dac_last_blue;
static int movie_queue_pending_after;
static int movie_kbhit_after;
/* Read the moment the stuffing is done, before fdps_play_movie is entered.
   movie_drains_the_keyboard_queue fails intermittently on a binary that does
   not otherwise change (measured: five runs of one EMITTEST.EXE gave fail,
   pass, fail, pass, fail, always the same single check), and the two readings
   below are what separate the two candidate explanations: either the setup
   never held the keys the drain assertion assumes, or the drain really did not
   happen.  Recorded rather than asserted inside run_movie, because run_movie
   is shared by every case in this group and must not gain a failure of its
   own. */
static int movie_queue_pending_at_stuff;
static int movie_kbhit_at_stuff;
static int movie_scancode_after;
static int movie_sfx_flag_after;
static int movie_timer_slot_before;
static int movie_timer_slot_after;
static unsigned int movie_vector_before_offset;
static unsigned short movie_vector_before_selector;
static unsigned int movie_vector_after_offset;
static unsigned short movie_vector_after_selector;
static unsigned int movie_saved_offset;
static unsigned short movie_saved_selector;

static void movie_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* How many keys the BIOS queue holds, from its own head and tail rather than
   from anything DOS reports, so that "the queue was drained" is read out of the
   same place a drain has to change. */
static int movie_queue_pending(void)
{
    int head;
    int tail;
    int span;

    head = (int) *(unsigned short *) BIOS_KBD_HEAD_ADDR;
    tail = (int) *(unsigned short *) BIOS_KBD_TAIL_ADDR;
    span = tail - head;
    if (span < 0) {
        span += BIOS_KBD_RING_BYTES;
    }
    return span / 2;
}

static void movie_queue_stuff(void)
{
    unsigned short *ring;

    ring = (unsigned short *) BIOS_KBD_RING_ADDR;
    ring[0] = MOVIE_QUEUED_KEY;
    ring[1] = MOVIE_QUEUED_KEY;
    *(unsigned short *) BIOS_KBD_HEAD_ADDR = BIOS_KBD_RING_START;
    *(unsigned short *) BIOS_KBD_TAIL_ADDR =
        BIOS_KBD_RING_START + 2 * MOVIE_QUEUED_KEY_COUNT;
}

/* Empty the queue by moving its tail onto its head, rather than by reading the
   keys out.  Nothing here may call getch: a getch made when DOS does not in
   fact see the stuffed queue would block, and this is the case that finds out
   whether it sees it. */
static void movie_queue_clear(void)
{
    *(unsigned short *) BIOS_KBD_TAIL_ADDR =
        *(unsigned short *) BIOS_KBD_HEAD_ADDR;
}

static void movie_read_dac(void)
{
    int entry;
    int red;
    int green;
    int blue;

    movie_dac_mismatches = 0;
    for (entry = 0; entry < MOVIE_DAC_ENTRIES; entry++) {
        outp(MOVIE_DAC_READ_INDEX, entry);
        red = (int) (inp(MOVIE_DAC_DATA) & MOVIE_DAC_COMPONENT_MASK);
        green = (int) (inp(MOVIE_DAC_DATA) & MOVIE_DAC_COMPONENT_MASK);
        blue = (int) (inp(MOVIE_DAC_DATA) & MOVIE_DAC_COMPONENT_MASK);
        if (red != (int) movie_pal[entry * 3]
            || green != (int) movie_pal[entry * 3 + 1]
            || blue != (int) movie_pal[entry * 3 + 2]) {
            movie_dac_mismatches++;
        }
        if (entry == MOVIE_DAC_ENTRIES - 1) {
            movie_dac_last_red = red;
            movie_dac_last_green = green;
            movie_dac_last_blue = blue;
        }
    }
}

/* How many bytes of the frame are not zero, counted in the aperture itself
   while the adapter is still in the mode that decodes it. */
static int movie_frame_non_zero_count(void)
{
    unsigned char *frame;
    int offset;
    int bad;

    frame = (unsigned char *) MOVIE_BASE;
    bad = 0;
    for (offset = 0; offset < MOVIE_FRAME_BYTES; offset++) {
        if (frame[offset] != 0) {
            bad++;
        }
    }
    return bad;
}

/* One whole call, fenced, with everything it can be observed through staged
   first and read back afterwards.  Run once: the call spawns a process and
   brings the audio stack up and down, and every case below asks about the same
   single observation. */
static void run_movie(void)
{
    unsigned char saved_irq_mask;
    int entry;

    if (movie_ran != 0) {
        return;
    }
    movie_ran = 1;

    for (entry = 0; entry < MOVIE_DAC_ENTRIES; entry++) {
        movie_pal[entry * 3] = (unsigned char) (entry & MOVIE_DAC_COMPONENT_MASK);
        movie_pal[entry * 3 + 1] =
            (unsigned char) ((entry + 21) & MOVIE_DAC_COMPONENT_MASK);
        movie_pal[entry * 3 + 2] =
            (unsigned char) ((entry + 42) & MOVIE_DAC_COMPONENT_MASK);
    }
    data_fdps_vga_main_palette_ptr = movie_pal;

    /* The CD stop command stages its request header through this block; with
       the block still NULL the stage would write to linear address 0. */
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }

    /* An empty prefix, so the composed player path is "\fd.exe": a file that
       is certainly not staged, on a drive that certainly exists.  A prefix
       naming a drive that does not exist would put a DOS critical error in
       front of the spawn instead of a plain file-not-found. */
    data_fdps_cdrom_path[0] = '\0';

    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_input_last_scancode = MOVIE_SCANCODE_MARKER;

    AIL_startup();
    movie_timer_slot_before = (int) AIL_register_timer(NO_TIMER_CALLBACK);

    saved_irq_mask = movie_mask_irq1();
    movie_vector_before_offset =
        movie_read_int9_vector(&movie_vector_before_selector);
    fdps_install_keyboard_isr();

    movie_set_mode(MOVIE_MODE_320X200X256);
    memset((void *) MOVIE_BASE, MOVIE_SENTINEL, (size_t) MOVIE_WINDOW_BYTES);
    movie_queue_stuff();
    movie_queue_pending_at_stuff = movie_queue_pending();
    movie_kbhit_at_stuff = kbhit();

    fdps_play_movie(MOVIE_NAME);

    movie_queue_pending_after = movie_queue_pending();
    movie_kbhit_after = kbhit();
    movie_frame_non_zero = movie_frame_non_zero_count();
    movie_tail_first = (int) ((unsigned char *) MOVIE_BASE)[MOVIE_TAIL_FIRST];
    movie_tail_last = (int) ((unsigned char *) MOVIE_BASE)[MOVIE_TAIL_LAST];
    movie_read_dac();
    movie_set_mode(MOVIE_MODE_TEXT);

    movie_scancode_after = (int) data_fdps_input_last_scancode;
    movie_sfx_flag_after = (int) data_fdps_audio_sfx_enabled_flag;
    movie_saved_offset = data_fdps_prev_int9_handler_offset;
    movie_saved_selector = data_fdps_input_prev_int9_handler_selector;
    movie_vector_after_offset =
        movie_read_int9_vector(&movie_vector_after_selector);

    movie_write_int9_vector(movie_vector_before_selector,
                            movie_vector_before_offset);
    movie_restore_irq_mask(saved_irq_mask);

    movie_timer_slot_after = (int) AIL_register_timer(NO_TIMER_CALLBACK);
    fdps_audio_shutdown();
}

/* The premise the frame assertions rest on: in mode 13h the aperture is a
   plain linear window that reads back what was written to it, at the first
   byte of the frame, at its last, and at the top of the 64K window.  An
   aperture that did not read back would make those assertions meaningless
   rather than false. */
static void movie_premise_the_aperture_reads_back(void)
{
    unsigned char *window;
    int first;
    int frame_last;
    int window_last;

    window = (unsigned char *) MOVIE_BASE;
    movie_set_mode(MOVIE_MODE_320X200X256);
    memset(window, MOVIE_SENTINEL, (size_t) MOVIE_WINDOW_BYTES);
    first = (int) window[0];
    frame_last = (int) window[MOVIE_FRAME_BYTES - 1];
    window_last = (int) window[MOVIE_WINDOW_BYTES - 1];
    movie_set_mode(MOVIE_MODE_TEXT);

    CHECK_EQ(first, MOVIE_SENTINEL);
    CHECK_EQ(frame_last, MOVIE_SENTINEL);
    CHECK_EQ(window_last, MOVIE_SENTINEL);
}

/* The premise the drain assertion rests on: keys written into the BIOS queue
   are keys the CRT's kbhit reports, so an empty queue afterwards is a drain
   and not an arrangement that never held any keys in the first place.  The
   queue is emptied here by moving its tail and not by reading it, so nothing
   in this case can block. */
static void movie_premise_the_queue_can_be_stuffed(void)
{
    unsigned char saved_irq_mask;
    int pending;
    int seen;
    int seen_empty;

    saved_irq_mask = movie_mask_irq1();
    movie_queue_stuff();
    pending = movie_queue_pending();
    seen = kbhit();
    movie_queue_clear();
    seen_empty = kbhit();
    movie_restore_irq_mask(saved_irq_mask);

    CHECK_EQ(pending, MOVIE_QUEUED_KEY_COUNT);
    CHECK_EQ(seen != 0, 1);
    CHECK_EQ(seen_empty, 0);
}

/* PUSH 0xfa00 / PUSH 0x0 / PUSH 0xa0000: every byte of the mode 13h frame is
   colour 0 when the call returns.  This is also what says the body carried on
   past a spawn that failed -- the clear is the first step after it. */
static void movie_clears_the_whole_mode13h_frame(void)
{
    run_movie();

    CHECK_EQ(movie_frame_non_zero, 0);
}

/* The length is 0xfa00 and not the 0x10000 of the whole aperture window: the
   first byte past the frame and the last byte of the window both still hold
   the sentinel.  A clear written against the window rather than the frame
   would take out both. */
static void movie_clears_no_more_than_the_frame(void)
{
    run_movie();

    CHECK_EQ(movie_tail_first, MOVIE_SENTINEL);
    CHECK_EQ(movie_tail_last, MOVIE_SENTINEL);
}

/* The six pushes of the closing upload -- 0, 0, 0, 0xff, 0, [0x000643bc] --
   put the master palette back over the whole DAC with no bias, so every one of
   the 256 entries matches the fixture the run staged.  The last entry is
   checked by name as well: the bound is inclusive, so entry 255 is written and
   an exclusive reading would leave it holding whatever mode 13h set up. */
static void movie_leaves_the_master_palette_unbiased_over_the_whole_dac(void)
{
    run_movie();

    CHECK_EQ(movie_dac_mismatches, 0);
    CHECK_EQ(movie_dac_last_red, 255 & MOVIE_DAC_COMPONENT_MASK);
    CHECK_EQ(movie_dac_last_green, (255 + 21) & MOVIE_DAC_COMPONENT_MASK);
    CHECK_EQ(movie_dac_last_blue, (255 + 42) & MOVIE_DAC_COMPONENT_MASK);
}

/* CALL kbhit / TEST EAX,EAX / JZ / CALL getch / JMP: the loop runs while kbhit
   answers non-zero and throws each character away.  The loop is the only branch
   in the body and kbhit's is the only returned value the body uses -- which is
   exactly why this case has to branch on kbhit too.

   Writing keys into the BIOS ring does not reliably make kbhit report them once
   run_movie has installed the game's INT 9 handler: measured over six runs of
   one unchanged EMITTEST.EXE, three had the ring holding both keys with kbhit
   already answering 0 at the moment of stuffing, before fdps_play_movie was
   entered at all.  On those runs the loop was right to do nothing, and the old
   unconditional "the queue is empty afterwards" was not measuring a drain; it
   was measuring whether the setup had happened to work, and it reddened the
   emit gate for functions that have nothing to do with any of this.

   So both arms assert.  When kbhit could see the keys the ring must come back
   empty, which is the drain.  When it could not, the ring must come back
   holding exactly what was put in it, which says the loop did nothing rather
   than something arbitrary.  Either way the case measures the body. */
static void movie_drains_the_keyboard_queue(void)
{
    run_movie();

    if (movie_kbhit_at_stuff == 0) {
        CHECK_EQ(movie_queue_pending_after, MOVIE_QUEUED_KEY_COUNT);
        CHECK_EQ(movie_kbhit_after, 0);
        return;
    }

    CHECK_EQ(movie_queue_pending_after, 0);
    CHECK_EQ(movie_kbhit_after, 0);
}

/* The half of the setup that IS deterministic, asserted on its own so the case
   above can branch on the half that is not.  The write into the BIOS ring lands
   every time -- the ring held both keys on all six runs of the measurement,
   including the three where kbhit could not see them -- so a failure here would
   mean the stuffing itself broke, which is a different fault from the one the
   drain case handles.

   kbhit's answer at the same instant is deliberately NOT asserted here.  What
   makes it vary is somewhere in fdps_install_keyboard_isr, the mode change and
   AIL being up, none of which this file owns; movie_premise_the_queue_can_be_-
   stuffed does the same stuffing with none of them in place and has never
   failed.  Tracked as an open issue against src/title.c rather than pinned to
   whatever the environment happens to do today. */
static void movie_queue_really_held_two_keys_at_entry(void)
{
    run_movie();

    CHECK_EQ(movie_queue_pending_at_stuff, MOVIE_QUEUED_KEY_COUNT);
}

/* CALL 0x00056818 at 00030f51, before anything else touches the keyboard: the
   hook comes down at the top and only then goes back up at the bottom.  The
   vector the closing install files away is therefore the one that was on 09h
   before the hook was ever put on, and not the hook's own handler -- which is
   what it would be if the uninstall had been dropped, because the install
   files whatever the vector holds when it runs.  The latched scancode is the
   uninstaller's other move, MOV byte ptr [0x00070006],0xff at 0005682f, and it
   goes into the run holding a marker instead. */
static void movie_takes_the_keyboard_hook_down_before_putting_it_back(void)
{
    run_movie();

    CHECK_EQ(movie_saved_offset == movie_vector_before_offset, 1);
    CHECK_EQ(movie_saved_selector == movie_vector_before_selector, 1);
    CHECK_EQ(movie_saved_offset == (unsigned int) fdps_keyboard_isr, 0);
    CHECK_EQ(movie_scancode_after, 0xff);
}

/* CALL 0x000567f0 at 00031021: the hook is back on vector 09h when the call
   returns, pointing at fdps_keyboard_isr through the code selector.  The game
   is left able to read the keyboard again, which is the half of the contract a
   caller depends on. */
static void movie_puts_the_keyboard_hook_back(void)
{
    run_movie();

    CHECK_EQ(movie_vector_after_offset == (unsigned int) fdps_keyboard_isr, 1);
    CHECK_EQ(movie_vector_after_selector == movie_current_cs(), 1);
    CHECK_EQ(movie_vector_after_offset != movie_vector_before_offset, 1);
}

/* CALL AIL_shutdown at 00030f4c and PUSH 0x19 / CALL fdps_audio_init at
   00031026, read through AIL's timer table.  A timer is registered before the
   call and takes slot 0; the next free slot after the call is 4, which needs
   both halves to have happened -- the shutdown to release that registration
   and the bring-up to take exactly one new one.  Without the shutdown the
   bring-up's timer would be slot 4 and this would answer 8; without the
   bring-up it would answer 0.  The sound-effects flag is the bring-up's own
   unconditional store, and it goes into the run clear. */
static void movie_takes_the_audio_stack_down_and_brings_it_back(void)
{
    run_movie();

    CHECK_EQ(movie_timer_slot_before, 0);
    CHECK_EQ(movie_timer_slot_after, AIL_TIMER_SLOT_STRIDE);
    CHECK_EQ(movie_sfx_flag_after, 1);
}

/* --- fdps_show_game_over @ 0002a960 ------------------------------------- */

/* WHAT THESE CASES ASSERT AGAINST.  This routine draws on the real adapter and
   leaves what it drew standing, so the cases below put the machine in mode
   13h, paint a known picture into the aperture, hook the timer, run the whole
   thing once, and have the interrupt compare the live aperture against two
   reference images on every tick.  Everything all five cases look at comes
   from that one run.

   MISC.VFS has to be there.  The routine loads GameOver.saf through
   fdps_vfs_load_entry, which ends the process rather than returning when the
   container will not open, so the container is staged through
   tests/gamefile.lst and every case skips itself when it is missing.

   THE TWO REFERENCES ARE BUILT, NOT GUESSED, and every number in them is read
   off the assembly at 0002a960 rather than off the emitted C:

     PUSH 0x16480 at 0002a9b7, MOV [EBP-0x4c],0x170 and MOV [EBP-0x48],0xf8 at
       0002a9c7 and 0002a9ce, MOV [EBP-0x44],0x18 and MOV [EBP-0x40],0x18 at
       0002a9d5 and 0002a9dc, ADD EAX,0x2298 at 0002aa0f -- the 368x248 page,
       the (24, 24) origin and the 320x200 window taken out of it
     MOV [EBP-0x2c],0x1 at 0002a9e9 -- the playback cursor opens on frame 1,
       which is what the first reference is a picture of
     MOV [EBP-0x4],0x0 / CMP [EBP-0x4],0x10 / JL at 0002aac7 and 0002aace --
       sixteen fade steps, so the last one runs at step 15
     PUSH 0x6f at 0002aae5 with the step itself pushed as the alpha at
       0002aae4 -- the backdrop's tint colour and its strength
     MOV EAX,0x10 / SUB EAX,[EBP-0x4] at 0002ab1c -- the translucency level,
       so the last step draws the picture at level 1
     MOV [EBP-0x38],0x0 at 0002ab34 and MOV [EBP-0x38],0x1f at 0002ab5b --
       entry 0 drawn translucent and entry 0x1f drawn opaque over it, with
       MOV [EBP-0x30],0x9 and MOV [EBP-0x30],0x0 for the two blit modes

   A build that opened the clip on a different frame, sized the page
   differently, took a different window out of it, ran a different number of
   fade steps, tinted toward a different colour, inverted either the alpha or
   the level, or composed the two entries in the other order produces a
   different 320x200 image and no tick matches.

   WHAT THE FIXTURE SUPPLIES.  The two palette blending tables are built here
   from a staged palette before anything is drawn, because both references and
   the run itself resolve every blended pixel through them: with the tables
   left zero-filled every tint and every translucency would come out as palette
   index 0 and the alpha and the level would be unobservable.  Which palette it
   is does not matter -- the references and the run read the same tables -- only
   that it is not degenerate, so it is a spread of all three channels.

   HOW THE RUN IS ALLOWED TO FINISH.  The routine ends with
   fdps_flush_keyboard_queue followed by fdps_wait_any_key, and that wait spins
   until the scancode ring's two indices differ (keybd.h).  Nothing in a test
   process presses a key, so the timer interrupt makes them differ -- but only
   OVER_KEY_DELAY_TICKS after the finished picture has been seen on the
   adapter, which is what lets the fourth case below tell a body that waited
   from one that did not.  A run that never puts the expected picture up is
   released anyway once OVER_BUDGET_TICKS have passed, so a wrong build fails
   its assertions instead of hanging the harness. */

#define OVER_MISC_NAME "MISC.VFS"
#define OVER_MEMBER_NAME "GameOver.saf"

/* The off-screen page and the window taken out of it, from the assembly cited
   in the note above. */
#define OVER_PAGE_PITCH 0x170
#define OVER_PAGE_ROWS 0xf8
#define OVER_PAGE_BYTES 0x16480
#define OVER_PAGE_MARGIN 0x18
#define OVER_PAGE_WINDOW_AT 0x2298

/* The visible screen and the two BIOS modes the run moves between. */
#define OVER_VGA_BASE 0x000a0000
#define OVER_SCREEN_W 0x140
#define OVER_SCREEN_H 0xc8
#define OVER_SCREEN_BYTES (OVER_SCREEN_W * OVER_SCREEN_H)
#define OVER_MODE_TEXT 0x03
#define OVER_MODE_320X200X256 0x13

/* The BIOS timer, the one interrupt that has to be running: nothing else moves
   data_fdps_timer_tick_counter, and both of the routine's loops spin until it
   changes. */
#define OVER_TIMER_VECTOR 8

/* The frame the clip opens on, and the two entries the fade composes. */
#define OVER_FIRST_FRAME 1
#define OVER_PICTURE_FRAME 0
#define OVER_SIGN_FRAME 0x1f

/* What the last fade step hands the two drawers: alpha 15, because the step
   counter is the alpha and the bound `< 0x10` makes 15 the last one, and
   level 1, because the level is 16 minus the step. */
#define OVER_LAST_TINT_ALPHA 15
#define OVER_LAST_BLEND_LEVEL 1
#define OVER_TINT_COLOR 0x6f

/* The blit modes and the three-dword descriptor slots, as rleblend.h has
   them. */
#define OVER_BLIT_MODE_OPAQUE 0
#define OVER_BLIT_MODE_TRANSLUCENT 9
#define OVER_BLEND_DESC_SHADE_RAMP 0
#define OVER_BLEND_DESC_LEVEL 1
#define OVER_BLEND_DESC_CUBE 2
#define OVER_BLEND_DESC_DWORDS 3

/* How long after the finished picture appears the scancode ring is made
   non-empty, and how long the whole run is given before it is released
   regardless.  Five ticks is a quarter of a second, far longer than the three
   frees and the ring rewind between the last present and the wait, so a body
   that did not wait cannot reach the release by accident. */
#define OVER_KEY_DELAY_TICKS 5
#define OVER_BUDGET_TICKS 400

/* The clip is GameOver.saf's frames 1..31 at two ticks each and then sixteen
   fade steps at one tick each, so a whole run is around 77 ticks.  The bound
   is set well below that and well above what any partial playback could reach:
   a body that drew one frame and fell through is 17, and one that skipped the
   clip is 16. */
#define OVER_MIN_RUN_TICKS 45

/* The DAC range every component of the staged palette stays inside. */
#define OVER_DAC_ENTRIES 256
#define OVER_DAC_RANGE 64
#define OVER_PAL_RED_STEP 7
#define OVER_PAL_GREEN_STEP 11
#define OVER_PAL_BLUE_STEP 13

/* The picture painted into the aperture before the run, which is what the
   routine snapshots and then tints away underneath the game-over picture.  Two
   different multipliers so no row and no column repeats, and the whole 0..255
   range so the tint has something to move. */
#define OVER_BG_ROW_STEP 13
#define OVER_BG_COL_STEP 7

static unsigned char over_background[OVER_SCREEN_BYTES];
static unsigned char over_first_ref[OVER_SCREEN_BYTES];
static unsigned char over_final_ref[OVER_SCREEN_BYTES];
static unsigned char over_palette[OVER_DAC_ENTRIES * 3];

static void (__interrupt __far *over_saved_timer)();

/* Written by the interrupt while the routine runs. */
static volatile long over_ticks;
static volatile long over_final_at_tick;
static volatile int over_first_seen;
static volatile int over_final_seen;

static int over_ran;
static int over_ready;
static long over_return_ticks;
static long over_left_mismatches;
static long over_first_vs_background;
static long over_first_vs_final;

/* Sampled once per timer tick while the routine runs.  memcmp gives up at the
   first differing byte, so a tick that matches neither reference costs almost
   nothing and only a genuine match pays for the whole 64,000.

   The release of the closing wait lives here as well: the ring's write index
   is pushed off its read index once the finished picture has been up for
   OVER_KEY_DELAY_TICKS, or unconditionally once the whole run has overrun its
   budget, so that a build which never produces the picture fails a check
   rather than spinning forever. */
static void __interrupt __far over_timer_isr(void)
{
    unsigned char *aperture;

    ++data_fdps_timer_tick_counter;
    over_ticks++;

    aperture = (unsigned char *) OVER_VGA_BASE;
    if (over_first_seen == 0
        && memcmp(aperture, over_first_ref, (size_t) OVER_SCREEN_BYTES) == 0) {
        over_first_seen = 1;
    }
    if (over_final_seen == 0
        && memcmp(aperture, over_final_ref, (size_t) OVER_SCREEN_BYTES) == 0) {
        over_final_seen = 1;
        over_final_at_tick = over_ticks;
    }

    if ((over_final_seen != 0
         && over_ticks >= over_final_at_tick + OVER_KEY_DELAY_TICKS)
        || over_ticks >= OVER_BUDGET_TICKS) {
        data_fdps_input_scancode_queue_write_index =
            data_fdps_input_scancode_queue_head + 1;
    }

    _chain_intr(over_saved_timer);
}

static void over_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Copies the 320x200 window the routine presents out of a composition page. */
static void over_take_window(unsigned char *page, unsigned char *out)
{
    int row;

    for (row = 0; row < OVER_SCREEN_H; row++) {
        memmove(out + row * OVER_SCREEN_W,
                page + OVER_PAGE_WINDOW_AT + row * OVER_PAGE_PITCH,
                (size_t) OVER_SCREEN_W);
    }
}

/* The picture the playback loop puts up on its first pass: the caller's screen
   copied opaquely into the page's window, then GameOver.saf entry
   OVER_FIRST_FRAME drawn over it at the page's own origin in blit mode 0. */
static void over_build_first(void *bank, unsigned char *out)
{
    int request[DRAW_REQUEST_DWORDS];
    unsigned char *page;

    page = (unsigned char *) malloc((size_t) OVER_PAGE_BYTES);
    if (page == NULL) {
        return;
    }

    fdps_blit_rect((unsigned int) over_background, OVER_SCREEN_W,
                   page + OVER_PAGE_WINDOW_AT, OVER_PAGE_PITCH,
                   OVER_SCREEN_W, OVER_SCREEN_H);

    request[DRAW_REQUEST_DEST_BASE] = (int) page;
    request[DRAW_REQUEST_DEST_PITCH] = OVER_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = OVER_PAGE_ROWS;
    request[DRAW_REQUEST_X] = OVER_PAGE_MARGIN;
    request[DRAW_REQUEST_Y] = OVER_PAGE_MARGIN;
    request[DRAW_REQUEST_IMAGE] = (int) bank;
    request[DRAW_REQUEST_ITEM_INDEX] = OVER_FIRST_FRAME;
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = OVER_BLIT_MODE_OPAQUE;
    fdps_draw_composite_sprite(request, 1);

    over_take_window(page, out);
    free(page);
}

/* The picture the last fade step composes: the caller's screen tinted toward
   colour 0x6f at alpha 15, entry 0 over it at translucency level 1, and entry
   0x1f opaque on top of both.  The page is not cleared first, for the same
   reason the routine does not clear it -- the tint covers every byte of the
   window with no key and no clip. */
static void over_build_final(void *bank, unsigned char *out)
{
    int request[DRAW_REQUEST_DWORDS];
    int blend_descriptor[OVER_BLEND_DESC_DWORDS];
    unsigned char *page;

    page = (unsigned char *) malloc((size_t) OVER_PAGE_BYTES);
    if (page == NULL) {
        return;
    }

    fdps_blit_tint_rect(over_background, OVER_SCREEN_W,
                        page + OVER_PAGE_WINDOW_AT, OVER_PAGE_PITCH,
                        OVER_SCREEN_W, OVER_SCREEN_H,
                        data_fdps_palette_shade_ramp_table,
                        data_fdps_inverse_palette_cube,
                        OVER_TINT_COLOR, OVER_LAST_TINT_ALPHA);

    blend_descriptor[OVER_BLEND_DESC_SHADE_RAMP] =
        (int) data_fdps_palette_shade_ramp_table;
    blend_descriptor[OVER_BLEND_DESC_LEVEL] = OVER_LAST_BLEND_LEVEL;
    blend_descriptor[OVER_BLEND_DESC_CUBE] =
        (int) data_fdps_inverse_palette_cube;

    request[DRAW_REQUEST_DEST_BASE] = (int) page;
    request[DRAW_REQUEST_DEST_PITCH] = OVER_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = OVER_PAGE_ROWS;
    request[DRAW_REQUEST_X] = OVER_PAGE_MARGIN;
    request[DRAW_REQUEST_Y] = OVER_PAGE_MARGIN;
    request[DRAW_REQUEST_IMAGE] = (int) bank;
    request[DRAW_REQUEST_ITEM_INDEX] = OVER_PICTURE_FRAME;
    request[DRAW_REQUEST_BLIT_OPERAND] = (int) blend_descriptor;
    request[DRAW_REQUEST_BLIT_MODE] = OVER_BLIT_MODE_TRANSLUCENT;
    fdps_draw_composite_sprite(request, 1);

    request[DRAW_REQUEST_ITEM_INDEX] = OVER_SIGN_FRAME;
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = OVER_BLIT_MODE_OPAQUE;
    fdps_draw_composite_sprite(request, 1);

    over_take_window(page, out);
    free(page);
}

static long over_differences(unsigned char *left, unsigned char *right)
{
    long count;
    long at;

    count = 0;
    for (at = 0; at < OVER_SCREEN_BYTES; at++) {
        if (left[at] != right[at]) {
            count++;
        }
    }
    return count;
}

/* The one run all the cases read. */
static void over_run(void)
{
    FILE *fp;
    void *bank;
    int saved_head;
    int saved_write;
    int entry;
    int row;
    int col;

    if (over_ran) {
        return;
    }
    over_ran = 1;

    fp = fopen(OVER_MISC_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);

    for (entry = 0; entry < OVER_DAC_ENTRIES; entry++) {
        over_palette[entry * 3] =
            (unsigned char) ((entry * OVER_PAL_RED_STEP) % OVER_DAC_RANGE);
        over_palette[entry * 3 + 1] =
            (unsigned char) ((entry * OVER_PAL_GREEN_STEP) % OVER_DAC_RANGE);
        over_palette[entry * 3 + 2] =
            (unsigned char) ((entry * OVER_PAL_BLUE_STEP) % OVER_DAC_RANGE);
    }
    fdps_build_palette_tables((struct fdps_palette_entry *) over_palette);

    for (row = 0; row < OVER_SCREEN_H; row++) {
        for (col = 0; col < OVER_SCREEN_W; col++) {
            over_background[row * OVER_SCREEN_W + col] =
                (unsigned char) (row * OVER_BG_ROW_STEP
                                 + col * OVER_BG_COL_STEP);
        }
    }

    bank = fdps_vfs_load_entry(OVER_MISC_NAME, OVER_MEMBER_NAME);
    over_build_first(bank, over_first_ref);
    over_build_final(bank, over_final_ref);
    free(bank);

    over_first_vs_background = over_differences(over_first_ref,
                                                over_background);
    over_first_vs_final = over_differences(over_first_ref, over_final_ref);

    /* The ring goes in empty, so the closing wait really waits: only the
       interrupt below can end it. */
    saved_head = data_fdps_input_scancode_queue_head;
    saved_write = data_fdps_input_scancode_queue_write_index;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;

    over_ticks = 0;
    over_final_at_tick = 0;
    over_first_seen = 0;
    over_final_seen = 0;

    over_set_mode(OVER_MODE_320X200X256);
    memmove((void *) OVER_VGA_BASE, over_background,
            (size_t) OVER_SCREEN_BYTES);

    over_saved_timer = _dos_getvect(OVER_TIMER_VECTOR);
    _dos_setvect(OVER_TIMER_VECTOR, over_timer_isr);
    fdps_show_game_over();
    over_return_ticks = over_ticks;
    _dos_setvect(OVER_TIMER_VECTOR, over_saved_timer);

    over_left_mismatches = over_differences((unsigned char *) OVER_VGA_BASE,
                                            over_final_ref);
    over_set_mode(OVER_MODE_TEXT);

    data_fdps_input_scancode_queue_head = saved_head;
    data_fdps_input_scancode_queue_write_index = saved_write;
    over_ready = 1;
}

/* The premise the two match cases rest on: the run happened at all, and the
   two references are pictures of different things, neither of them simply the
   backdrop that was already on the adapter.  Without the second and third a
   match would be no evidence; without the first the whole group could go green
   by never running, which is why the missing-container path is asserted here
   rather than skipped silently -- build_emit.py's preflight refuses to build
   at all unless every name in tests/gamefile.lst is present, so a container
   that is not there is a broken harness and not a machine without game
   files. */
static void game_over_premise_the_references_are_distinct(void)
{
    over_run();

    CHECK_EQ(over_ready, 1);
    if (!over_ready) {
        return;
    }

    CHECK_EQ(over_first_vs_background > 0, 1);
    CHECK_EQ(over_first_vs_final > 0, 1);
}

/* The clip opens on entry 1 and composes it over the caller's own screen:
   MOV [EBP-0x2c],0x1 seeds the cursor, and the opaque fdps_blit_rect at
   0002aa1e repaints the page's window from the snapshot before the entry is
   drawn.  A body that reset the cursor through fdps_saf_advance_tick instead,
   the way fdps_saf_play_over_background does, would open on entry 0 and this
   picture would never appear. */
static void game_over_opens_the_clip_on_frame_one_over_the_snapshot(void)
{
    over_run();
    if (!over_ready) {
        return;
    }

    CHECK_EQ(over_first_seen, 1);
}

/* The last fade step reaches the adapter byte for byte: the backdrop tinted
   toward 0x6f at alpha 15, entry 0 over it at level 1, entry 0x1f opaque on
   top.  This is the one assertion that pins the whole fade at once -- the
   sixteen-step bound, the tint colour, the alpha running up while the level
   runs down, and the order the two entries are composed in. */
static void game_over_ends_the_fade_on_the_fifteenth_step(void)
{
    over_run();
    if (!over_ready) {
        return;
    }

    CHECK_EQ(over_final_seen, 1);
}

/* Nothing clears the frame on the way out: the picture the fade ended on is
   still on the adapter when the call returns, which is what the caller draws
   over next. */
static void game_over_leaves_the_finished_picture_on_the_adapter(void)
{
    over_run();
    if (!over_ready) {
        return;
    }

    CHECK_EQ(over_left_mismatches, 0L);
}

/* CALL 0x000567b3 / CALL 0x000567a0 at 0002abfc and 0002ac01: the ring is
   emptied and then waited on, so the call does not come back until a scancode
   arrives.  The interrupt makes one available OVER_KEY_DELAY_TICKS after the
   finished picture goes up, and the call is still inside itself at that point.
   A body that dropped the wait would return within a tick of the last present
   and this gap would be 0 or 1. */
static void game_over_waits_for_a_key_before_it_returns(void)
{
    over_run();
    if (!over_ready) {
        return;
    }

    CHECK_EQ(over_return_ticks - over_final_at_tick >= OVER_KEY_DELAY_TICKS, 1);
}

/* The playback loop runs the clip out rather than stopping on the first frame:
   it exits only when fdps_saf_advance_tick answers non-zero, and GameOver.saf
   is 31 frames of two ticks each.  The whole run therefore cannot be short,
   and the bound is set below what a correct run takes and above what any
   truncated playback could reach. */
static void game_over_plays_the_whole_clip_before_it_fades(void)
{
    over_run();
    if (!over_ready) {
        return;
    }

    CHECK_EQ(over_final_at_tick >= OVER_MIN_RUN_TICKS, 1);
}

void run_title_tests(void)
{
    RUN_TEST(movie_premise_the_aperture_reads_back);
    RUN_TEST(movie_premise_the_queue_can_be_stuffed);
    RUN_TEST(movie_clears_the_whole_mode13h_frame);
    RUN_TEST(movie_clears_no_more_than_the_frame);
    RUN_TEST(movie_leaves_the_master_palette_unbiased_over_the_whole_dac);
    RUN_TEST(movie_queue_really_held_two_keys_at_entry);
    RUN_TEST(movie_drains_the_keyboard_queue);
    RUN_TEST(movie_takes_the_keyboard_hook_down_before_putting_it_back);
    RUN_TEST(movie_puts_the_keyboard_hook_back);
    RUN_TEST(movie_takes_the_audio_stack_down_and_brings_it_back);
    RUN_TEST(game_over_premise_the_references_are_distinct);
    RUN_TEST(game_over_opens_the_clip_on_frame_one_over_the_snapshot);
    RUN_TEST(game_over_ends_the_fade_on_the_fifteenth_step);
    RUN_TEST(game_over_leaves_the_finished_picture_on_the_adapter);
    RUN_TEST(game_over_waits_for_a_key_before_it_returns);
    RUN_TEST(game_over_plays_the_whole_clip_before_it_fades);
}
