/* tests/title.c -- cover for src/title.c.
 *
 * Four entry points, each with its own fixture and its own block of cases,
 * in the order the file holds them: fdps_play_movie @ 00030f40,
 * fdps_show_game_over @ 0002a960, fdps_play_ending_credit_roll @ 0001ba40 and
 * fdps_title_demo @ 0002ac10.  Every block but the movie's sets out what it
 * asserts against, and why its run is bounded the way it is, above itself;
 * everything from here to the game-over block is the movie.
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
#include <malloc.h>
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
#include "deploy.h"
#include "keybd.h"
#include "mapdraw.h"
#include "palette.h"
#include "sprite.h"
#include "text.h"
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

/* ---- fdps_play_ending_credit_roll @ 0001ba40 -----------------------------
 *
 * Run whole, twice, against the real game files, because there is nothing
 * smaller to run: the function takes no argument, returns nothing and reads
 * everything it needs out of globals, so the only way to reach the two
 * branches that decide the ending is to play one.  FMER1.TMP, FMER2.TMP,
 * MER1.TMP, MER2.TMP, FIGHT.VFS, FIGACT.VFS and BACKGRND.VFS are already
 * staged beside the executable for tests/combat.c (tests/gamefile.lst) and
 * this file needs no other real file: the seven member names are composed
 * inside the function out of the roster index and a record byte, so what is
 * asserted is that the composed names found the members the shipped
 * containers hold.
 *
 * WHAT THE CAPTION IS READ THROUGH.  The one thing the assembly decides that
 * nothing else in the run shows is WHICH chapter text entry each card is
 * captioned with -- base 0x20 or 0x19, plus the roster index, with slot 3
 * skipped on the chapter 27 ending.  So the chapter text global is pointed at
 * a table built here in which entry 0x19 + s is a run of s + 29 blank glyphs,
 * one solid glyph and the end marker: the caption's origin is screen row 2
 * column 0 (page + 0x2578 against the window at page + 0x2298, a difference of
 * two rows), the staged glyph advance is eight, and so entry 0x19 + s paints
 * exactly one eight-pixel block of colour 0xd0 at screen column 232 + 8s.  The
 * timer handler reads those rows back and records which slots it saw.
 *
 * A slot counts only when the eight pixels of the caption colour are there AND
 * the eight of the shadow colour that the font puts one row below them and one
 * column to the right are there too.  Requiring the pair is what makes the
 * reading safe over a screen that still has a backdrop or a character on it:
 * the caption is composed after both, so it is always on top, and a clip would
 * have to reproduce a two-colour signature to be counted as one.
 *
 * The font is staged to match: one byte per glyph, eight pixels wide, one row
 * high, glyph 0 blank and glyph 1 solid, with the outline flag clear so that
 * the single shadow at (1, 1) is drawn and not the four-way outline.
 *
 * THE TWO RUNS.  The chapter 30 ending is played with an empty roster, so the
 * loop's one pass is the pass past the end -- the card that takes the fixed
 * sprite id -- and the chapter 27 ending is played with four members, which is
 * the shortest roster that reaches the slot-3 skip: cards 0, 1 and 2, then the
 * counter jumps to 4 and the pass at 4 is the pass past the end.  Between them
 * every branch in the body is taken.
 *
 * WHY THE SECOND RUN GETS A FASTER CLOCK.  Every frame of every stage waits
 * for the tick counter to change, and a card is 9 + 50 + the action clip + 251
 * + 9 frames, so four cards are about 1,400 ticks -- eighty seconds at the
 * BIOS rate, which the host's stall detector would call a hang.  The second
 * run therefore reprograms channel 0 of the interval timer to about a
 * kilohertz and chains to the BIOS handler every 55th interrupt so the
 * time-of-day clock keeps its rate; the frames are then paced by the vertical
 * retrace alone.  The first run is left at the BIOS rate precisely so that one
 * assertion can count ticks and get frames.
 *
 * WHAT IS LEFT TO THE PLAYTEST.  Where each layer lands on the page, which
 * frame of the standing clip each stage draws, and that the two dissolves run
 * in the direction they should are all matters of pixels the real clips
 * produce; nothing below asserts a picture. */

/* The four staged blend tables, and the window compared out of each.  The
   fight file and the map file first disagree at 1028 and at 2, from the
   measurement in tests/combat.c, so a 128-byte window at 1024 and at 0
   separates them. */
#define CR_RAMP_BYTES 0x4800
#define CR_CUBE_BYTES 0x1000
#define CR_RAMP_WINDOW_AT 1024
#define CR_CUBE_WINDOW_AT 0
#define CR_WINDOW_BYTES 128
#define CR_ENDING_RAMP_FILE "FMER1.TMP"
#define CR_ENDING_CUBE_FILE "FMER2.TMP"
#define CR_MAP_RAMP_FILE "MER1.TMP"
#define CR_MAP_CUBE_FILE "MER2.TMP"

/* The page, the window taken out of it and the caption's origin inside it, all
   from src/title.c's own constants.  The caption is two page rows below the
   window's first row and in its first column. */
#define CR_PAGE_BYTES 0x16480
#define CR_SCREEN_PITCH 0x140
#define CR_CAPTION_SCREEN_ROW 2

/* The mark zone: ten eight-pixel slots ending at the right edge of the 320
   pixel row, far enough right that no card's sprite reaches them. */
#define CR_MARK_ZONE_FIRST 232
#define CR_MARK_SLOTS 10
#define CR_MARK_WIDTH 8
#define CR_CAPTION_COLOR 0xd0
#define CR_CAPTION_SHADOW_COLOR 0x6d

/* The staged font: one byte to a glyph, eight pixels by one row, glyph 0 blank
   and glyph 1 solid. */
#define CR_GLYPH_WIDTH 8
#define CR_GLYPH_ROWS 1
#define CR_GLYPH_STRIDE 1
#define CR_GLYPH_ADVANCE 8
#define CR_GLYPH_BLANK 0
#define CR_GLYPH_SOLID 1
#define CR_GLYPH_COUNT 256
#define CR_SHADOW_ROW 1
#define CR_SHADOW_COLUMN 1
#define CR_LINE_HEIGHT 8

/* The chapter text fixture: 48 entries, of which 0x19 through 0x22 carry a
   mark and every other one is empty.  -1 is fdps_draw_text's end marker
   (src/text.c). */
#define CR_TEXT_ENTRIES 0x30
#define CR_TEXT_SHORTS 512
#define CR_TEXT_END (-1)
#define CR_FIRST_MARK_TEXT_ID 0x19

/* The two chapter indices and the two roster sizes the runs use.  Four is the
   smallest member count that reaches the slot-3 skip and still ends on the
   pass past the roster. */
#define CR_CHAPTER_30 0x1d
#define CR_CHAPTER_27 0x1a
#define CR_CHAPTER_30_MEMBERS 0
#define CR_CHAPTER_27_MEMBERS 4
#define CR_ROSTER_SLOTS 8
#define CR_ROSTER_STRIDE 0x50
/* STAND000.SAF and ACT000.SAF exist in the shipped containers, so a card for a
   staged member finds its clips. */
#define CR_MEMBER_PORTRAIT 0

/* The caption entries each run should paint, as slot numbers off 0x19.  The
   chapter 30 run has one card, roster index 0, so entry 0x20 -- slot 7.  The
   chapter 27 run has cards 0, 1, 2 and 4, so entries 0x19, 0x1a, 0x1b and
   0x1d: slots 0, 1, 2 and 4, and NOT slot 3. */
#define CR_CHAPTER_30_MARKS (1 << 7)
#define CR_CHAPTER_27_MARKS ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 4))

/* One card is 9 slide frames, 50 hold frames, the action clip, 251 caption
   frames and 9 fade frames.  ACT012.SAF -- the clip the pass past the roster
   loads -- is 32 frames whose durations add up to 56 ticks, so the chapter 30
   run is 375 frames and, at one tick a frame with the first frame's wait
   skipped, at least 374 ticks.  ACT000.SAF adds up to 25, so a run that had
   loaded the member's clip instead of the fixed one could not reach this. */
#define CR_ONE_CARD_TICKS 374

/* The interval timer, and the divisor the second run drives it with: 2386
   ticks of the 1.193 MHz input is five hundred a second, and chaining to the
   BIOS handler every 27th of them keeps the time-of-day clock within a fifth
   of a percent.  Five hundred is enough and no more, because a frame also
   waits out a vertical retrace: at a fourteenth of a second that is seven
   times the tick period, so the tick wait has stopped being what paces the
   run.  Divisor 0 is 65536, the rate the BIOS programs. */
#define CR_TIMER_VECTOR 8
#define CR_PIT_CHANNEL0 0x40
#define CR_PIT_COMMAND 0x43
#define CR_PIT_MODE3 0x36
#define CR_PIT_FAST_DIVISOR 2386
#define CR_PIT_BIOS_DIVISOR 0
#define CR_SUBTICKS_PER_BIOS_TICK 27
#define CR_PIC_COMMAND 0x20
#define CR_PIC_EOI 0x20

/* Six DAC readings: the three channels of entry 0 and of entry 255. */
#define CR_DAC_READ_INDEX 0x3c7
#define CR_DAC_WRITE_INDEX 0x3c8
#define CR_DAC_DATA 0x3c9
#define CR_DAC_ENTRIES 256
#define CR_DAC_SENTINEL 63
#define CR_PALETTE_SPAN 61
#define CR_READING_SLOTS 6

/* A block the size of the composing page, so a leaked page can be counted
   without any other allocation being mistaken for one.  Watcom rounds a
   request up to a multiple of eight and 0x16480 is already one, but the window
   allows for a header the walker might report inside the size. */
#define CR_PAGE_BLOCK_SLACK 32

static struct fdps_unit_record cr_roster[CR_ROSTER_SLOTS];
static unsigned char cr_font[CR_GLYPH_COUNT * CR_GLYPH_STRIDE];
static short cr_text[CR_TEXT_SHORTS];
static struct fdps_palette_entry cr_ending_pal[CR_DAC_ENTRIES];
static struct fdps_palette_entry cr_map_pal[CR_DAC_ENTRIES];

static unsigned char cr_ending_ramp_file[CR_WINDOW_BYTES];
static unsigned char cr_ending_cube_file[CR_WINDOW_BYTES];
static unsigned char cr_map_ramp_file[CR_WINDOW_BYTES];
static unsigned char cr_map_cube_file[CR_WINDOW_BYTES];
static int cr_files_read;

/* What one run left behind.  Every case below reads one of these rather than
   running again. */
static int cr_marks;
static int cr_snapshot_taken;
static unsigned char cr_ramp_during[CR_WINDOW_BYTES];
static unsigned char cr_cube_during[CR_WINDOW_BYTES];
static unsigned char cr_ramp_after[CR_WINDOW_BYTES];
static unsigned char cr_cube_after[CR_WINDOW_BYTES];
static int cr_dac_during[CR_READING_SLOTS];
static int cr_dac_after[CR_READING_SLOTS];
static int cr_screen_non_zero;
static int cr_pages_leaked;
static unsigned int cr_ticks_used;
static int cr_chapter30_done;
static int cr_chapter27_done;

static void (__interrupt __far *cr_saved_timer)();
static int cr_fast_clock;
static int cr_subticks;

/* Which marks are standing in the zone right now.  A mark is not just eight
   pixels of the caption colour: it is those eight AND the eight of the shadow
   colour that the staged font puts one row below and one column right of them,
   which is a signature the clips cannot be mistaken for.  That pairing is what
   lets this run over a screen that still has a backdrop or a character on it,
   because the caption is composed last and is therefore on top of both. */
static void cr_scan_marks(void)
{
    unsigned char *glyph_row;
    unsigned char *shadow_row;
    int slot;
    int column;
    int matched;

    glyph_row = (unsigned char *) (MOVIE_BASE
                                   + CR_CAPTION_SCREEN_ROW * CR_SCREEN_PITCH
                                   + CR_MARK_ZONE_FIRST);
    shadow_row = glyph_row + CR_SHADOW_ROW * CR_SCREEN_PITCH + CR_SHADOW_COLUMN;
    for (slot = 0; slot < CR_MARK_SLOTS; slot++) {
        matched = 0;
        for (column = 0; column < CR_MARK_WIDTH; column++) {
            if (glyph_row[slot * CR_MARK_WIDTH + column] == CR_CAPTION_COLOR
                && shadow_row[slot * CR_MARK_WIDTH + column]
                   == CR_CAPTION_SHADOW_COLOR) {
                matched++;
            }
        }
        if (matched == CR_MARK_WIDTH) {
            cr_marks |= 1 << slot;
        }
    }
}

static void cr_read_dac(int *into)
{
    outp(CR_DAC_READ_INDEX, 0);
    into[0] = (int) inp(CR_DAC_DATA);
    into[1] = (int) inp(CR_DAC_DATA);
    into[2] = (int) inp(CR_DAC_DATA);
    outp(CR_DAC_READ_INDEX, CR_DAC_ENTRIES - 1);
    into[3] = (int) inp(CR_DAC_DATA);
    into[4] = (int) inp(CR_DAC_DATA);
    into[5] = (int) inp(CR_DAC_DATA);
}

/* The three things that only exist while the roll is running, taken the first
   time a caption reaches the screen.  That moment is past the blend-table
   reads and past the ending palette upload, and it is a long way before the
   restores, which are after the last card. */
static void cr_take_snapshot(void)
{
    memmove(cr_ramp_during,
            (unsigned char *) data_fdps_palette_shade_ramp_table
            + CR_RAMP_WINDOW_AT, (size_t) CR_WINDOW_BYTES);
    memmove(cr_cube_during,
            data_fdps_inverse_palette_cube + CR_CUBE_WINDOW_AT,
            (size_t) CR_WINDOW_BYTES);
    cr_read_dac(cr_dac_during);
}

static void __interrupt __far cr_timer_isr(void)
{
    int before;

    ++data_fdps_timer_tick_counter;

    before = cr_marks;
    cr_scan_marks();
    if (cr_marks != before && cr_snapshot_taken == 0) {
        cr_take_snapshot();
        cr_snapshot_taken = 1;
    }

    if (cr_fast_clock == 0) {
        _chain_intr(cr_saved_timer);
    }
    cr_subticks++;
    if (cr_subticks >= CR_SUBTICKS_PER_BIOS_TICK) {
        cr_subticks = 0;
        _chain_intr(cr_saved_timer);
    }
    outp(CR_PIC_COMMAND, CR_PIC_EOI);
}

static void cr_set_timer_divisor(int divisor)
{
    outp(CR_PIT_COMMAND, CR_PIT_MODE3);
    outp(CR_PIT_CHANNEL0, divisor & 0xff);
    outp(CR_PIT_CHANNEL0, (divisor >> 8) & 0xff);
}

/* Used heap entries the size of one composing page, which is how a leaked page
   is told from every other allocation the run makes and gives back. */
static int cr_page_blocks(void)
{
    struct _heapinfo entry;
    int pages;

    pages = 0;
    entry._pentry = NULL;
    while (_heapwalk(&entry) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY
            && entry._size >= (size_t) CR_PAGE_BYTES
            && entry._size <= (size_t) (CR_PAGE_BYTES + CR_PAGE_BLOCK_SLACK)) {
            pages++;
        }
    }
    return pages;
}

/* One window out of a staged blend table, read here so that what the run put
   in the global can be compared against the file rather than against a
   number. */
static int cr_read_file_window(char *name, long at, unsigned char *into)
{
    FILE *f;
    int got;

    memset(into, 0, (size_t) CR_WINDOW_BYTES);
    f = fopen(name, "rb");
    if (f == NULL) {
        return 0;
    }
    got = 0;
    if (fseek(f, at, SEEK_SET) == 0) {
        got = (int) fread(into, 1, (size_t) CR_WINDOW_BYTES, f);
    }
    fclose(f);
    return got == CR_WINDOW_BYTES;
}

static void cr_read_files(void)
{
    if (cr_files_read != 0) {
        return;
    }
    cr_files_read = 1;
    cr_files_read &= cr_read_file_window(CR_ENDING_RAMP_FILE,
                                         (long) CR_RAMP_WINDOW_AT,
                                         cr_ending_ramp_file);
    cr_files_read &= cr_read_file_window(CR_ENDING_CUBE_FILE,
                                         (long) CR_CUBE_WINDOW_AT,
                                         cr_ending_cube_file);
    cr_files_read &= cr_read_file_window(CR_MAP_RAMP_FILE,
                                         (long) CR_RAMP_WINDOW_AT,
                                         cr_map_ramp_file);
    cr_files_read &= cr_read_file_window(CR_MAP_CUBE_FILE,
                                         (long) CR_CUBE_WINDOW_AT,
                                         cr_map_cube_file);
}

/* Entry 0x19 + s is s + 29 blank glyphs, one solid glyph and the end marker;
   every other entry is the end marker alone. */
static void cr_build_text(void)
{
    int id;
    int slot;
    int blanks;
    int glyph;
    int cursor;

    memset(cr_text, 0, sizeof(cr_text));
    cursor = CR_TEXT_ENTRIES + 1;
    for (id = 0; id < CR_TEXT_ENTRIES; id++) {
        cr_text[id] = (short) (CR_TEXT_ENTRIES * 2);
    }
    cr_text[CR_TEXT_ENTRIES] = (short) CR_TEXT_END;

    for (slot = 0; slot < CR_MARK_SLOTS; slot++) {
        id = CR_FIRST_MARK_TEXT_ID + slot;
        cr_text[id] = (short) (cursor * 2);
        blanks = CR_MARK_ZONE_FIRST / CR_GLYPH_WIDTH + slot;
        for (glyph = 0; glyph < blanks; glyph++) {
            cr_text[cursor] = CR_GLYPH_BLANK;
            cursor++;
        }
        cr_text[cursor] = CR_GLYPH_SOLID;
        cursor++;
        cr_text[cursor] = (short) CR_TEXT_END;
        cursor++;
    }
}

/* Everything the run reads that is not the machine: the roster, the font, the
   chapter text and the two palettes.  No hardware is touched here, so the
   premise case can use it too. */
static void cr_stage_fixture(int chapter_id, int member_count)
{
    int index;

    memset(cr_roster, 0, sizeof(cr_roster));
    for (index = 0; index < CR_ROSTER_SLOTS; index++) {
        cr_roster[index].portrait_id = (unsigned char) CR_MEMBER_PORTRAIT;
    }
    data_fdps_roster_array_ptr = (unsigned char *) cr_roster;
    data_fdps_roster_member_count = member_count;
    data_fdps_chapter_current_chapter_id = chapter_id;

    memset(cr_font, 0, sizeof(cr_font));
    cr_font[CR_GLYPH_SOLID * CR_GLYPH_STRIDE] = 0xff;
    data_fdps_font_sheet_ptr = cr_font;
    data_fdps_font_glyph_stride_bytes = CR_GLYPH_STRIDE;
    data_fdps_font_glyph_width = (unsigned char) CR_GLYPH_WIDTH;
    data_fdps_glyph_cell_height = (unsigned char) CR_GLYPH_ROWS;
    data_fdps_glyph_advance_x = CR_GLYPH_ADVANCE;
    data_fdps_font_line_height = CR_LINE_HEIGHT;
    data_fdps_font_outline_enabled_flag = 0;
    data_fdps_glyph_shadow_row_offset = CR_SHADOW_ROW;
    data_fdps_font_shadow_offset_x = CR_SHADOW_COLUMN;

    cr_build_text();
    data_fdps_current_chapter_text_ptr = (unsigned char *) cr_text;

    for (index = 0; index < CR_DAC_ENTRIES; index++) {
        cr_ending_pal[index].red = (unsigned char) (index % CR_PALETTE_SPAN);
        cr_ending_pal[index].green =
            (unsigned char) ((index + 7) % CR_PALETTE_SPAN);
        cr_ending_pal[index].blue =
            (unsigned char) ((index + 14) % CR_PALETTE_SPAN);
        cr_map_pal[index].red =
            (unsigned char) ((index + 21) % CR_PALETTE_SPAN);
        cr_map_pal[index].green =
            (unsigned char) ((index + 28) % CR_PALETTE_SPAN);
        cr_map_pal[index].blue =
            (unsigned char) ((index + 35) % CR_PALETTE_SPAN);
    }
    data_fdps_vga_fight_palette_ptr = (unsigned char *) cr_ending_pal;
    data_fdps_vga_main_palette_ptr = (unsigned char *) cr_map_pal;

    /* Both audio gates closed, so the sound ids the shipped clips carry play
       nothing, and eight empty voices for the teardown. */
    data_fdps_audio_bgm_enabled_flag = 0;
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    for (index = 0; index < SFX_SAMPLE_SLOT_COUNT; index++) {
        data_fdps_audio_sample_handle_table[index] = NULL;
    }
}

/* One whole ending, fenced the way run_movie above fences its own call -- the
   run ends inside fdps_play_movie, so it leaves vector 09h on the build's stub
   keyboard handler and brings the audio stack up, and both have to be put back
   here. */
static void cr_run(int chapter_id, int member_count, int fast_clock)
{
    void (__interrupt __far *entry_timer)();
    unsigned char saved_irq_mask;
    unsigned int vector_offset;
    unsigned short vector_selector;
    unsigned int ticks_before;
    int blocks_before;
    int index;

    cr_read_files();
    cr_stage_fixture(chapter_id, member_count);

    cr_marks = 0;
    cr_snapshot_taken = 0;
    cr_subticks = 0;
    cr_fast_clock = fast_clock;
    memset(cr_ramp_during, 0, (size_t) CR_WINDOW_BYTES);
    memset(cr_cube_during, 0, (size_t) CR_WINDOW_BYTES);
    for (index = 0; index < CR_READING_SLOTS; index++) {
        cr_dac_during[index] = -1;
        cr_dac_after[index] = -1;
    }

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    /* An empty prefix, so the movie player the run ends on is looked for at
       "\fd.exe": certainly absent, on a drive that certainly exists. */
    data_fdps_cdrom_path[0] = '\0';

    /* What vector 08h held before the audio stack took it, which is what it
       has to be given back at the end: the hook below displaces AIL's own
       timer service, fdps_play_movie takes that service down and brings a
       fresh one up before it returns, and putting the displaced handler back
       would leave a shut-down library's dispatcher on the vector for the next
       AIL_startup to save as its own predecessor and chain to. */
    entry_timer = _dos_getvect(CR_TIMER_VECTOR);
    AIL_startup();

    saved_irq_mask = movie_mask_irq1();
    vector_offset = movie_read_int9_vector(&vector_selector);

    movie_set_mode(MOVIE_MODE_320X200X256);
    memset((void *) MOVIE_BASE, MOVIE_SENTINEL, (size_t) MOVIE_FRAME_BYTES);
    for (index = 0; index < CR_DAC_ENTRIES; index++) {
        outp(CR_DAC_WRITE_INDEX, index);
        outp(CR_DAC_DATA, CR_DAC_SENTINEL);
        outp(CR_DAC_DATA, CR_DAC_SENTINEL);
        outp(CR_DAC_DATA, CR_DAC_SENTINEL);
    }

    blocks_before = cr_page_blocks();
    data_fdps_timer_tick_counter = 0;
    ticks_before = data_fdps_timer_tick_counter;

    cr_saved_timer = _dos_getvect(CR_TIMER_VECTOR);
    _dos_setvect(CR_TIMER_VECTOR, cr_timer_isr);
    if (fast_clock != 0) {
        cr_set_timer_divisor(CR_PIT_FAST_DIVISOR);
    }

    fdps_play_ending_credit_roll();

    cr_ticks_used = data_fdps_timer_tick_counter - ticks_before;
    cr_pages_leaked = cr_page_blocks() - blocks_before;
    cr_read_dac(cr_dac_after);
    cr_screen_non_zero = 0;
    for (index = 0; index < MOVIE_FRAME_BYTES; index++) {
        if (((unsigned char *) MOVIE_BASE)[index] != 0) {
            cr_screen_non_zero++;
        }
    }
    movie_set_mode(MOVIE_MODE_TEXT);

    memmove(cr_ramp_after,
            (unsigned char *) data_fdps_palette_shade_ramp_table
            + CR_RAMP_WINDOW_AT, (size_t) CR_WINDOW_BYTES);
    memmove(cr_cube_after,
            data_fdps_inverse_palette_cube + CR_CUBE_WINDOW_AT,
            (size_t) CR_WINDOW_BYTES);

    /* The audio stack the run brought up goes down first, because putting it
       down is what gives the interrupt vector and the timer's rate back to
       whoever held them before it took them. */
    fdps_audio_shutdown();
    cr_set_timer_divisor(CR_PIT_BIOS_DIVISOR);
    _dos_setvect(CR_TIMER_VECTOR, entry_timer);
    cr_fast_clock = 0;

    movie_write_int9_vector(vector_selector, vector_offset);
    movie_restore_irq_mask(saved_irq_mask);
}

static void cr_chapter30_run(void)
{
    if (cr_chapter30_done != 0) {
        return;
    }
    cr_chapter30_done = 1;
    cr_run(CR_CHAPTER_30, CR_CHAPTER_30_MEMBERS, 0);
}

static void cr_chapter27_run(void)
{
    if (cr_chapter27_done != 0) {
        return;
    }
    cr_chapter27_done = 1;
    cr_run(CR_CHAPTER_27, CR_CHAPTER_27_MEMBERS, 1);
}

/* The premise every caption assertion rests on: each of the ten staged
   entries, drawn through fdps_draw_text with the colours and the pitch the
   credit roll hands it, paints its own slot and nothing else.  A fixture that
   did not do that would make the readings below meaningless rather than
   false.  The scratch page is the width of the screen so the slot number is
   the screen column divided by eight, which is what the timer handler
   assumes. */
static void credits_premise_each_caption_entry_marks_its_own_slot(void)
{
    unsigned char *page;
    int slot;
    int column;
    int painted;
    int shadowed;
    int elsewhere;

    cr_stage_fixture(CR_CHAPTER_30, CR_CHAPTER_30_MEMBERS);
    page = (unsigned char *) malloc((size_t) (CR_SCREEN_PITCH * 4));
    painted = 0;
    shadowed = 0;
    elsewhere = 0;

    for (slot = 0; slot < CR_MARK_SLOTS; slot++) {
        memset(page, 0, (size_t) (CR_SCREEN_PITCH * 4));
        fdps_draw_text((unsigned char *) cr_text,
                       CR_FIRST_MARK_TEXT_ID + slot, page, CR_SCREEN_PITCH,
                       CR_CAPTION_COLOR, 0, CR_CAPTION_SHADOW_COLOR);
        for (column = 0; column < CR_SCREEN_PITCH; column++) {
            if (column >= CR_MARK_ZONE_FIRST + slot * CR_MARK_WIDTH
                && column < CR_MARK_ZONE_FIRST + (slot + 1) * CR_MARK_WIDTH) {
                if (page[column] == CR_CAPTION_COLOR) {
                    painted++;
                }
                if (page[CR_SHADOW_ROW * CR_SCREEN_PITCH + CR_SHADOW_COLUMN
                         + column] == CR_CAPTION_SHADOW_COLOR) {
                    shadowed++;
                }
            } else if (page[column] != 0) {
                elsewhere++;
            }
        }
    }
    free(page);

    CHECK_EQ(painted, CR_MARK_SLOTS * CR_MARK_WIDTH);
    CHECK_EQ(shadowed, CR_MARK_SLOTS * CR_MARK_WIDTH);
    CHECK_EQ(elsewhere, 0);
}

/* And the premise the blend-table assertions rest on: the four staged files
   were read, and the ending pair really does differ from the map pair inside
   the window compared, so "the table holds the ending file" and "the table
   holds the map file" are different statements. */
static void credits_premise_the_two_blend_pairs_differ(void)
{
    cr_read_files();

    CHECK_EQ(cr_files_read, 1);
    CHECK_EQ(memcmp(cr_ending_ramp_file, cr_map_ramp_file,
                    (size_t) CR_WINDOW_BYTES) != 0, 1);
    CHECK_EQ(memcmp(cr_ending_cube_file, cr_map_cube_file,
                    (size_t) CR_WINDOW_BYTES) != 0, 1);
}

/* MOV dword ptr [EBP-0x30],0x20 at 0001ba66, taken because the chapter index
   is 0x1d, and ADD EAX,[EBP-0x2c] at 0001c040 for the card's own index.  The
   one card of this run is roster index 0, so the entry drawn is 0x20 and the
   slot seen is 7.  A run that had taken the other arm would have marked slot 0
   instead, and a run that added something other than the index would have
   marked a different one again. */
static void credits_the_chapter_30_ending_captions_from_entry_0x20(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_marks, CR_CHAPTER_30_MARKS);
}

/* The two fread calls at 0001bab2 and 0001baee land the ending pair over the
   globals every alpha blitter reads, and they are still there while the cards
   play: the snapshot is taken the first time a caption reaches the screen,
   which is inside the first card. */
static void credits_composites_through_the_ending_blend_tables(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_snapshot_taken, 1);
    CHECK_EQ(memcmp(cr_ramp_during, cr_ending_ramp_file,
                    (size_t) CR_WINDOW_BYTES), 0);
    CHECK_EQ(memcmp(cr_cube_during, cr_ending_cube_file,
                    (size_t) CR_WINDOW_BYTES), 0);
}

/* And the two at 0001c20d and 0001c249 put the map pair back before the movie,
   so the map is composited through its own tables again afterwards. */
static void credits_puts_the_map_blend_tables_back(void)
{
    cr_chapter30_run();

    CHECK_EQ(memcmp(cr_ramp_after, cr_map_ramp_file,
                    (size_t) CR_WINDOW_BYTES), 0);
    CHECK_EQ(memcmp(cr_cube_after, cr_map_cube_file,
                    (size_t) CR_WINDOW_BYTES), 0);
}

/* PUSH [0x000643e4] with 0, 0xff and three zero biases at 0001bb10 uploads the
   ending palette before the first card, and the same shape over [0x000643bc]
   at 0001c1ca puts the main one back.  Both fixtures are inside the DAC's six
   bits, so an unbiased upload is the identity and neither is clamped. */
static void credits_uploads_the_ending_palette_and_restores_the_main_one(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_dac_during[0], (int) cr_ending_pal[0].red);
    CHECK_EQ(cr_dac_during[1], (int) cr_ending_pal[0].green);
    CHECK_EQ(cr_dac_during[2], (int) cr_ending_pal[0].blue);
    CHECK_EQ(cr_dac_during[3],
             (int) cr_ending_pal[CR_DAC_ENTRIES - 1].red);
    CHECK_EQ(cr_dac_during[5],
             (int) cr_ending_pal[CR_DAC_ENTRIES - 1].blue);
    CHECK_EQ(cr_dac_after[0], (int) cr_map_pal[0].red);
    CHECK_EQ(cr_dac_after[2], (int) cr_map_pal[0].blue);
    CHECK_EQ(cr_dac_after[3], (int) cr_map_pal[CR_DAC_ENTRIES - 1].red);
}

/* CALL fdps_play_movie at 0001c263 is the last thing in the body, and the
   movie clears the whole mode 13h frame on its way back (00030ff2).  So a
   frame that comes back with a byte in it says the roll did not reach the
   movie -- which is also the only evidence a unit test has that the roster
   loop terminated at all. */
static void credits_hands_the_screen_to_the_ending_movie(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_screen_non_zero, 0);
}

/* Every frame of every stage ends waiting for data_fdps_timer_tick_counter to
   change, so a run costs at least one tick a frame less the first, whose latch
   is uninitialised and may not wait at all.  This run's single card is 9 + 50 +
   56 + 251 + 9 frames: the 56 is ACT012.SAF's own total duration, and the fixed
   sprite id of the pass past the roster is what loads it -- the staged member's
   ACT000.SAF would be 25 and could not reach this floor.  No ceiling is
   asserted: nothing stops the counter advancing more than once while a frame is
   composed. */
static void credits_paces_every_frame_on_the_tick(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_ticks_used >= (unsigned int) CR_ONE_CARD_TICKS, 1);
}

/* CALL free at 0001c1c2 is past the end of the roster loop and CALL malloc at
   0001bc60 is inside it, so a run of one card allocates one page and gives it
   back.  This is the case that says the leak below is a leak and not a page
   that was never released. */
static void credits_a_single_card_leaves_no_page_behind(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_pages_leaked, 0);
}

/* CMP [0x00069cf4],0x1a / CMP [EBP-0x2c],0x3 / MOV [EBP-0x2c],0x4 at 0001bb4a:
   on the chapter 27 ending the counter is moved from 3 to 4, so slot 3 plays
   no card.  With four members the cards are 0, 1, 2 and then the pass past the
   roster at 4, and the captions are entries 0x19, 0x1a, 0x1b and 0x1d.  Entry
   0x1c is the one that says the skip happened: a body that treated the skip as
   "leave slot 3 out of the roster" and closed the gap would have drawn it. */
static void credits_the_chapter_27_ending_skips_roster_slot_three(void)
{
    cr_chapter27_run();

    CHECK_EQ(cr_marks, CR_CHAPTER_27_MARKS);
}

/* The page is allocated once per card and released once for the whole run, so
   four cards leave three pages behind -- about 89 KB each.  That is the
   original's behaviour and moving the free into the loop, which is what a
   rewrite naturally does, would make this zero
   (rebuild_info/pitfalls.md). */
static void credits_leaks_one_page_for_every_card_but_the_last(void)
{
    cr_chapter27_run();

    CHECK_EQ(cr_pages_leaked, 3);
}

/* --- fdps_title_demo @ 0002ac10 ------------------------------------------
 *
 * The demo is run ONCE, whole, against the shipped containers, and every case
 * below reads its answer off the state that one run left.  The unit array it
 * builds is the map's own, so most of the expected values are numbers taken
 * out of the shipped MAP25.DAT and MAP25.COD with tools/vfs_dump, and none of
 * them is read off the emitted C:
 *
 *   MAP25.DAT opens TWELVE player slots behind EIGHTY scripted deployments,
 *     sixty-eight of which are tagged wave 0, so the chapter reset leaves an
 *     array of eighty records: the twelve party slots first, then the sixty-
 *     eight deployments in record order.  Unit 12 is therefore deployment
 *     record 0 -- side 0, character 64, AI class 2 -- and is the first record
 *     the demo's twelve-record stat loop must NOT have touched.
 *   MAP25.COD stands the twelve party slots on its records 80..91, which are
 *     (6,36) (7,36) (8,36) (18,37) (9,36) (10,36) (11,36) (12,36) (7,37)
 *     (8,37) (9,37) (10,37).  Slot 3 is the odd one out, eleven tiles to the
 *     right of the rest, and the store at 0002acce is what brings it back.
 *   THE NEAREST WAVE-0 DEPLOYMENT IS THIRTEEN TILES FROM THE NEAREST PARTY
 *     TILE.  That, with the zeroed movement and the zeroed weapon range the
 *     fixture tables give every unit, is what makes the one actor turn the run
 *     takes a rest rather than a fight: nothing can reach anything, so no
 *     blow, no death and no random draw enters the run.
 *
 * and the rest from the assembly at 0002ac10: MOV [0x00069cf4],0x19 at
 * 0002ac1c, the twelve PUSH/CALL fdps_roster_add_character pairs from 0002ac37
 * to 0002aca7, MOV byte ptr [EAX],0x4 at 0002acce on the record
 * fdps_get_unit_record(3) returned, PUSH 0x0 / PUSH 0xb / PUSH 0x0 at
 * 0002acd1, the two fdps_set_flag_bit calls at 0002acdf and 0002aceb, the
 * seven stores into record offsets 6, 0x40, 0x42, 0x44, 0x46, 0x48 and 0x4c
 * under CMP dword ptr [EBP-0xc],0xc at 0002acfe, and PUSH 0xfa00 / PUSH 0x0 /
 * PUSH 0xa0000 at 0002ae19 for the closing clear.
 *
 * HOW THE RUN IS BOUNDED, AND WHY IT HAS TO BE.  The inner loop hands every
 * one of the eighty actors to fdps_map_actor_behavior_step, whose tail spins
 * on the frame clock (tests/mapai.c), and the only thing that ends the demo
 * early is a make code in the scancode ring -- which the demo empties itself
 * just before the loop, and which nothing in a test process refills, because
 * the INT 09h handler that fills it is not installed.  So the fixture's timer
 * interrupt does two jobs: it moves data_fdps_timer_tick_counter, without
 * which the first frame never finishes, and it arms the ring with one make
 * code on every tick.  The demo therefore notices a key on the first actor and
 * leaves after one turn.  Two of the eighty deployments stand on a tile whose
 * event code is not zero, and a turn-end tile event on one of those would run
 * a chapter script rather than assert anything, so stopping at the first actor
 * is also what keeps the run inside the part that can be observed.
 *
 * WHAT THAT LEAVES TO THE PLAYTEST.  How the two passes read on screen, and
 * that the demo ends by itself when nobody presses anything, are not reachable
 * from here and are for a person in DOSBox-X (ADR-0003).
 * data_fdps_map_cursor_draw_mode is in the same position: the demo switches
 * the overlay off at 0002acb4, and then the one actor turn writes that same
 * global twice on its way through fdps_map_actor_move_and_attack, so what it
 * holds afterwards is not the demo's answer.
 *
 * WHAT THE ONE ACTOR TURN DOES.  Unit 0 is a party record with behaviour 0,
 * the plain fighter, so it runs the three action searches and then rests.  All
 * three decline and the rest is refused, and the fixture is what makes that
 * certain rather than likely: every item modifier is zero, so the weapon's
 * reach is zero tiles and the attack search has no target area to look in; the
 * unit knows no spell and carries no item with an effect; movement is zero, so
 * neither approach can take a step; and fdps_unit_rest returns at its first
 * test because the demo has just set current HP equal to maximum HP.  What is
 * left is the turn-end tile event -- the party tiles all carry event code 0 in
 * M25.DTL -- the acted-this-turn flag, and one drawn frame.
 *
 * WHAT THE FIXTURE TABLES ARE FOR.  The six data tables are the test file's
 * own, as tests/chapter.c, tests/chinit1.c and tests/aiact.c stage them,
 * because ticket 23 has not emitted the real ones.  Four of their fields are
 * chosen rather than left at zero: every character's base defense is 77 and
 * base dexterity 55, so that the two stat words the demo does NOT write --
 * defense at record 0x4a and evade at 0x4e -- come back holding something a
 * store of the demo's own constants could not have produced; every enemy
 * record carries HP so that no deployment arrives already dead; and every
 * terrain in the class table costs one movement point, WITHOUT WHICH THE RUN
 * DOES NOT FINISH -- the move grid's flood fill spends the allowance one tile
 * at a time and a table of zero costs never spends it.  Movement and every
 * item modifier stay zero on purpose; see above.
 *
 * The roster block is a real heap block and has to be: the demo FREES it on
 * the way out, and a static array handed to free() is not a failure a later
 * check would get to report (rebuild_info/emit_pipeline.md).  It is filled
 * with 0x5a first, which is how the behaviour byte is observed -- record byte
 * 0x34 is one of the fields fdps_roster_add_character leaves standing and
 * fdps_build_map_unit_array copies whole, so a party record reaches the demo
 * carrying 0x5a and the range call at 0002acd1 has to leave 0x50.
 */

#define DEMO_CHAPTER_ID 0x19
#define DEMO_STALE_CHAPTER_ID 3

#define DEMO_MAP_PLAYER_SLOTS 12
#define DEMO_MAP_CHAR_SPAWNS 80
#define DEMO_MAP_UNITS 80

/* Deployment record 0 of MAP25.DAT, which the reset lands at unit index 12.
   Its maximum HP is not the fixture's enemy row as it stands: a deployment's
   HP is the record's own level times that row's HP (src/deploy.c), and this
   record is level 30. */
#define DEMO_FIRST_DEPLOYED_UNIT 12
#define DEMO_FIRST_DEPLOYED_CHAR 64
#define DEMO_FIRST_DEPLOYED_SIDE 0
#define DEMO_FIRST_DEPLOYED_AI 2
#define DEMO_FIRST_DEPLOYED_LEVEL 30

/* The party's columns on MAP25.COD, and the one the demo overwrites. */
#define DEMO_UNIT0_POS_X 6
#define DEMO_UNIT2_POS_X 8
#define DEMO_UNIT4_POS_X 9
#define DEMO_MOVED_UNIT 3
#define DEMO_MOVED_POS_X 4
#define DEMO_MOVED_POS_Y 37

#define DEMO_PARTY_UNITS 12
#define DEMO_SIDE_NPC 1
#define DEMO_HP 2000
#define DEMO_MP 800
#define DEMO_ATTACK 800
#define DEMO_HIT 400

/* What the two untouched stat words have to read afterwards: the fixture's
   base defense and base dexterity, carried through
   fdps_unit_recompute_combat_stats with every item modifier zero. */
#define DEMO_FIXTURE_DP_BASE 77
#define DEMO_FIXTURE_DX_BASE 55

/* The spell grants.  Spell 0x27 is bit 7 of bitmap byte 4 and spell 0x0b is
   bit 3 of byte 1 (assets/spells.md, unit.h). */
#define DEMO_GRANT_UNIT_A 1
#define DEMO_GRANT_BYTE_A 4
#define DEMO_GRANT_BIT_A 0x80
#define DEMO_GRANT_UNIT_B 4
#define DEMO_GRANT_BYTE_B 1
#define DEMO_GRANT_BIT_B 0x08
#define DEMO_BYSTANDER_UNIT 2

/* The prefill, and what the behaviour byte has to read once the low nibble has
   been reset over slots 0..11. */
#define DEMO_ROSTER_PREFILL 0x5a
#define DEMO_BEHAVIOR_AFTER_RESET 0x50

#define DEMO_ROSTER_SLOTS 32
#define DEMO_UNIT_STRIDE 0x50
#define DEMO_TABLE_ROWS 128
#define DEMO_ITEM_TABLE_ROWS 256
#define DEMO_CELL_EVENT_FLAGS 32
#define DEMO_DAC_ENTRIES 256
#define DEMO_SCENE_LAYERS 6

#define DEMO_STALE_ROSTER_COUNT 7
#define DEMO_STALE_PLAYER_SLOTS 99

#define DEMO_FIXTURE_LEVEL 1
#define DEMO_FIXTURE_ENEMY_HP 100

/* Every terrain costs one movement point in the fixture class table.  Being
   non-zero is the whole of its importance: the movement grid's flood fill
   walks until the allowance runs out, so a table of zero costs -- which is
   what a null data_fdps_class_table_ptr gives, the pointer being a ticket 23
   global -- never runs out and the fill does not terminate. */
#define DEMO_TERRAIN_MOVE_COST 1
#define DEMO_TERRAIN_TYPES 8

/* The two .CEL sheets the unit draw reads: offset table at +0x0f, one two-byte
   run per row, 24 rows for a map sprite and 11 for a status icon. */
#define DEMO_SHEET_TABLE_AT 0x0f
#define DEMO_TILE_PX 24
#define DEMO_TILE_STREAM_BYTES (DEMO_TILE_PX * 2)
#define DEMO_CEL_RUN_OP 0x17
#define DEMO_SHADOW_SPRITES 4
#define DEMO_SHADOW_STREAMS_AT (DEMO_SHEET_TABLE_AT + DEMO_SHADOW_SPRITES * 4)
#define DEMO_ICON_SPRITES 5
#define DEMO_ICON_ROWS 11
#define DEMO_ICON_STREAM_BYTES (DEMO_ICON_ROWS * 2)
#define DEMO_ICON_STREAMS_AT (DEMO_SHEET_TABLE_AT + DEMO_ICON_SPRITES * 4)
#define DEMO_ICON_COLOR_BASE 0x80

#define DEMO_MODE_TEXT 0x03
#define DEMO_MODE_320X200X256 0x13
#define DEMO_TIMER_VECTOR 8

/* The make code the fixture's interrupt arms the ring with.  Anything below
   0x7f ends the demo; this one is a function key, which nothing the run passes
   through treats as a command. */
#define DEMO_ARMED_MAKE_CODE 0x3b

#define DEMO_CEL_NAME "ICON.CEL"
#define DEMO_FIELD_NAME "FIELD.VFS"
#define DEMO_FIELD1_NAME "FIELD1.VFS"
#define DEMO_FIELD2_NAME "FIELD2.VFS"
#define DEMO_MISC_NAME "MISC.VFS"
#define DEMO_CURSOR_SHEET_MEMBER "Cusor.cel"
#define DEMO_CEL_MIN_SIZE (15L + 0x2970L)

static void demo_zero_bytes(void *block, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        bytes[i] = 0;
    }
}

static struct fdps_character_base_record demo_char[DEMO_TABLE_ROWS];
static struct fdps_character_growth demo_growth[DEMO_TABLE_ROWS];
static struct fdps_enemy_data demo_enemy[DEMO_TABLE_ROWS];
static struct fdps_item_effect demo_items[DEMO_ITEM_TABLE_ROWS];
static struct fdps_class_record demo_classes[DEMO_TABLE_ROWS];
static struct fdps_spell_effect demo_spells[DEMO_TABLE_ROWS];
static unsigned char demo_palette[DEMO_DAC_ENTRIES * 3];
static struct fdps_vfs_image_header demo_sfx_pack;
static unsigned char demo_shadow_sheet[DEMO_SHADOW_STREAMS_AT
                                       + DEMO_SHADOW_SPRITES
                                             * DEMO_TILE_STREAM_BYTES];
static unsigned char demo_icon_sheet[DEMO_ICON_STREAMS_AT
                                     + DEMO_ICON_SPRITES
                                           * DEMO_ICON_STREAM_BYTES];

/* The two .CEL sheets fdps_draw_map_unit reads through, built to the offset
   table at +0x0f and the one full-width run per row resource_info/cel.md
   describes.  They are staged because both pointers are ticket 23 globals and
   so null, and the decoder takes its row length out of the stream it is
   handed: pointed at null it reads the interrupt vector table as run opcodes
   and writes past the page it was given (rebuild_info/pitfalls.md).  What they
   paint is not asserted -- whatever the render tail draws is overwritten by
   the demo's own frame clear before the call returns. */
static void demo_build_unit_sheets(void)
{
    int sprite;
    int row;
    int stream_at;

    demo_zero_bytes(demo_shadow_sheet, (int) sizeof(demo_shadow_sheet));
    demo_zero_bytes(demo_icon_sheet, (int) sizeof(demo_icon_sheet));

    for (sprite = 0; sprite < DEMO_SHADOW_SPRITES; sprite++) {
        stream_at = DEMO_SHADOW_STREAMS_AT + sprite * DEMO_TILE_STREAM_BYTES;
        *(int *) (demo_shadow_sheet + DEMO_SHEET_TABLE_AT + sprite * 4) =
            stream_at;
        for (row = 0; row < DEMO_TILE_PX; row++) {
            demo_shadow_sheet[stream_at + row * 2] = DEMO_CEL_RUN_OP;
            demo_shadow_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (sprite + 1);
        }
    }

    for (sprite = 0; sprite < DEMO_ICON_SPRITES; sprite++) {
        stream_at = DEMO_ICON_STREAMS_AT + sprite * DEMO_ICON_STREAM_BYTES;
        *(int *) (demo_icon_sheet + DEMO_SHEET_TABLE_AT + sprite * 4) =
            stream_at;
        for (row = 0; row < DEMO_ICON_ROWS; row++) {
            demo_icon_sheet[stream_at + row * 2] = DEMO_CEL_RUN_OP;
            demo_icon_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (DEMO_ICON_COLOR_BASE + sprite);
        }
    }
}

static void (__interrupt __far *demo_saved_timer)();

/* Moves the frame clock the render tail waits on, and arms the scancode ring
   with one make code so that the demo's own escape ends it on the first
   actor. */
static void __interrupt __far demo_tick_isr(void)
{
    ++data_fdps_timer_tick_counter;
    data_fdps_input_scancode_queue[0] = DEMO_ARMED_MAKE_CODE;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 1;
    _chain_intr(demo_saved_timer);
}

static void demo_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* 0 before the run, 1 once it has happened, 2 when it was skipped because a
   shipped container is not staged. */
static int demo_state = 0;

static int seen_chapter_id;
static int seen_player_slots;
static int seen_char_spawns;
static int seen_unit_count;
static int seen_roster_count;
static int seen_play_active;
static int seen_frame_non_zero;
static int seen_tail_first;
static int seen_tail_last;
static int seen_char_id[DEMO_PARTY_UNITS];
static int seen_side[DEMO_PARTY_UNITS];
static int seen_hp_current[DEMO_PARTY_UNITS];
static int seen_hp_max[DEMO_PARTY_UNITS];
static int seen_mp_current[DEMO_PARTY_UNITS];
static int seen_mp_max[DEMO_PARTY_UNITS];
static int seen_ap[DEMO_PARTY_UNITS];
static int seen_hit[DEMO_PARTY_UNITS];
static int seen_behavior[DEMO_PARTY_UNITS];
static int seen_unit1_dp;
static int seen_unit1_ev;
static int seen_unit0_pos_x;
static int seen_unit2_pos_x;
static int seen_unit4_pos_x;
static int seen_moved_pos_x;
static int seen_moved_pos_y;
static int seen_grant_a_byte;
static int seen_grant_a_other;
static int seen_grant_b_byte;
static int seen_grant_b_other;
static int seen_bystander_a_byte;
static int seen_bystander_b_byte;
static int seen_deployed_char;
static int seen_deployed_side;
static int seen_deployed_ai;
static int seen_deployed_hp_max;

static int demo_file_present(char *name)
{
    FILE *fp;

    fp = fopen(name, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int demo_containers_present(void)
{
    FILE *fp;
    long size;

    fp = fopen(DEMO_CEL_NAME, "rb");
    if (fp == NULL) {
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    if (size < DEMO_CEL_MIN_SIZE) {
        return 0;
    }

    return demo_file_present(DEMO_FIELD_NAME)
           && demo_file_present(DEMO_FIELD1_NAME)
           && demo_file_present(DEMO_FIELD2_NAME)
           && demo_file_present(DEMO_MISC_NAME);
}

/* Nulling, not freeing: with a layer count of zero and null everywhere else
   the chapter loader frees nothing, which is the state a freshly started
   process is in. */
static void demo_clear_chapter_globals(void)
{
    int layer;

    for (layer = 0; layer < DEMO_SCENE_LAYERS; layer++) {
        data_fdps_scene_layer_tile_map_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_sheet_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_attr_ptr[layer] = NULL;
    }
    data_fdps_scene_layer_count = 0;
    data_fdps_current_chapter_text_ptr = NULL;
    data_fdps_tile_event_data_table_ptr = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_spawn_pos_table_ptr = NULL;
}

/* Everything the run allocated, released, and every global it was pointed at
   put back the way a fresh process has it.  The roster pointer is nulled and
   NOT freed: the demo freed that block itself, and this is exactly the global
   the emit pipeline's rule about loader-freed pointers is about. */
static void demo_free_globals(void)
{
    int layer;

    for (layer = 0; layer < data_fdps_scene_layer_count; layer++) {
        free(data_fdps_scene_layer_tile_map_ptrs[layer]);
        free(data_fdps_scene_layer_tile_sheet_ptrs[layer]);
        free(data_fdps_scene_layer_tile_attr_ptr[layer]);
    }
    free(data_fdps_current_chapter_text_ptr);
    free(data_fdps_tile_event_data_table_ptr);
    free(data_fdps_map_cell_event_code_layer_ptr);
    free(data_fdps_battle_move_grid_ptr);
    demo_clear_chapter_globals();

    if (data_fdps_map_unit_count != 0) {
        free(data_fdps_map_unit_array_ptr);
    }
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;

    if (data_fdps_cel_sprite_cache_count != 0) {
        free(data_fdps_cel_sprite_cache_ptr);
    }
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;

    free(data_fdps_cursor_highlight_sprite_sheet_ptr);
    data_fdps_cursor_highlight_sprite_sheet_ptr = NULL;
    data_fdps_vga_main_palette_ptr = NULL;

    data_fdps_roster_array_ptr = NULL;
    data_fdps_roster_member_count = 0;

    data_fdps_battle_character_base_table_ptr = NULL;
    data_fdps_battle_character_growth_table_ptr = NULL;
    data_fdps_battle_enemy_data_table_ptr = NULL;
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_class_table_ptr = NULL;
    data_fdps_battle_spell_effect_table_ptr = NULL;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_shadow_sprite_sheet_ptr = NULL;
    data_fdps_unit_status_icon_sheet_ptr = NULL;

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

static struct fdps_unit_record *demo_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * DEMO_UNIT_STRIDE);
}

/* How many bytes of the frame are not zero, counted in the aperture itself
   while the adapter is still in the mode that decodes it. */
static int demo_frame_non_zero_count(void)
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

static void demo_capture(void)
{
    struct fdps_unit_record *unit;
    struct fdps_unit_record *deployed;
    int slot;

    seen_chapter_id = data_fdps_chapter_current_chapter_id;
    seen_player_slots = data_fdps_map_player_slot_count;
    seen_char_spawns = data_fdps_map_char_spawn_count;
    seen_unit_count = data_fdps_map_unit_count;
    seen_roster_count = data_fdps_roster_member_count;
    seen_play_active = (int) data_fdps_ui_play_active_flag;

    for (slot = 0; slot < DEMO_PARTY_UNITS; slot++) {
        unit = demo_unit(slot);
        seen_char_id[slot] = (int) unit->char_id;
        seen_side[slot] = (int) unit->side;
        seen_hp_current[slot] = (int) unit->hp_current;
        seen_hp_max[slot] = (int) unit->hp_max;
        seen_mp_current[slot] = (int) unit->mp_current;
        seen_mp_max[slot] = (int) unit->mp_max;
        seen_ap[slot] = (int) unit->ap;
        seen_hit[slot] = (int) unit->hit;
        seen_behavior[slot] = (int) unit->ai_behavior;
    }

    unit = demo_unit(DEMO_GRANT_UNIT_A);
    seen_unit1_dp = (int) unit->dp;
    seen_unit1_ev = (int) unit->ev;
    seen_grant_a_byte = (int) unit->spells_known_bitmap[DEMO_GRANT_BYTE_A];
    seen_grant_a_other = (int) unit->spells_known_bitmap[DEMO_GRANT_BYTE_B];

    unit = demo_unit(DEMO_GRANT_UNIT_B);
    seen_grant_b_byte = (int) unit->spells_known_bitmap[DEMO_GRANT_BYTE_B];
    seen_grant_b_other = (int) unit->spells_known_bitmap[DEMO_GRANT_BYTE_A];

    unit = demo_unit(DEMO_BYSTANDER_UNIT);
    seen_bystander_a_byte = (int) unit->spells_known_bitmap[DEMO_GRANT_BYTE_A];
    seen_bystander_b_byte = (int) unit->spells_known_bitmap[DEMO_GRANT_BYTE_B];

    seen_unit0_pos_x = (int) demo_unit(0)->pos_x;
    seen_unit2_pos_x = (int) demo_unit(2)->pos_x;
    seen_unit4_pos_x = (int) demo_unit(4)->pos_x;
    seen_moved_pos_x = (int) demo_unit(DEMO_MOVED_UNIT)->pos_x;
    seen_moved_pos_y = (int) demo_unit(DEMO_MOVED_UNIT)->pos_y;

    deployed = demo_unit(DEMO_FIRST_DEPLOYED_UNIT);
    seen_deployed_char = (int) deployed->char_id;
    seen_deployed_side = (int) deployed->side;
    seen_deployed_ai = (int) deployed->ai_behavior;
    seen_deployed_hp_max = (int) deployed->hp_max;
}

/* Stages the whole battle-time environment, runs the demo once and records
   what it left behind. */
static void demo_run(void)
{
    unsigned char *roster_block;
    int index;
    int entry;

    if (demo_state != 0) {
        return;
    }
    demo_state = 2;

    if (!demo_containers_present()) {
        return;
    }

    demo_clear_chapter_globals();

    demo_zero_bytes(demo_char, (int) sizeof(demo_char));
    demo_zero_bytes(demo_growth, (int) sizeof(demo_growth));
    demo_zero_bytes(demo_enemy, (int) sizeof(demo_enemy));
    demo_zero_bytes(demo_items, (int) sizeof(demo_items));
    demo_zero_bytes(demo_classes, (int) sizeof(demo_classes));
    demo_zero_bytes(demo_spells, (int) sizeof(demo_spells));
    demo_zero_bytes(&demo_sfx_pack, (int) sizeof(demo_sfx_pack));
    demo_build_unit_sheets();

    for (index = 0; index < DEMO_TABLE_ROWS; index++) {
        demo_char[index].level = (unsigned char) DEMO_FIXTURE_LEVEL;
        demo_char[index].dp_base = (short) DEMO_FIXTURE_DP_BASE;
        demo_char[index].dx_base = (short) DEMO_FIXTURE_DX_BASE;
        demo_enemy[index].hp = (unsigned short) DEMO_FIXTURE_ENEMY_HP;
        for (entry = 0; entry < DEMO_TERRAIN_TYPES; entry++) {
            demo_classes[index].move_cost[entry] =
                (unsigned char) DEMO_TERRAIN_MOVE_COST;
        }
    }

    data_fdps_battle_character_base_table_ptr = (unsigned char *) demo_char;
    data_fdps_battle_character_growth_table_ptr =
        (unsigned char *) demo_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) demo_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) demo_items;
    data_fdps_class_table_ptr = (unsigned char *) demo_classes;
    data_fdps_battle_spell_effect_table_ptr = (unsigned char *) demo_spells;
    data_fdps_audio_basewav_sfx_bank_buf_ptr =
        (unsigned char *) &demo_sfx_pack;
    data_fdps_shadow_sprite_sheet_ptr = demo_shadow_sheet;
    data_fdps_unit_status_icon_sheet_ptr = demo_icon_sheet;
    data_fdps_map_unit_shadow_pass_flag = 0;
    data_fdps_map_unit_walk_anim_counter = 0;
    data_fdps_map_unit_status_icon_tick_counter = 0;
    data_fdps_map_unit_status_icon_cycle = 0;
    data_fdps_map_unit_anim_last_tick = 0;

    roster_block = (unsigned char *)
                   malloc((size_t) (DEMO_ROSTER_SLOTS * DEMO_UNIT_STRIDE));
    if (roster_block == NULL) {
        return;
    }
    memset(roster_block, DEMO_ROSTER_PREFILL,
           (size_t) (DEMO_ROSTER_SLOTS * DEMO_UNIT_STRIDE));
    data_fdps_roster_array_ptr = roster_block;
    data_fdps_roster_member_count = DEMO_STALE_ROSTER_COUNT;

    for (entry = 0; entry < DEMO_DAC_ENTRIES; entry++) {
        demo_palette[entry * 3] = (unsigned char) (entry % 64);
        demo_palette[entry * 3 + 1] = (unsigned char) ((entry * 3) % 64);
        demo_palette[entry * 3 + 2] = (unsigned char) ((entry * 5) % 64);
    }
    data_fdps_vga_main_palette_ptr = demo_palette;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(DEMO_MISC_NAME,
                                              DEMO_CURSOR_SHEET_MEMBER);

    data_fdps_chapter_current_chapter_id = DEMO_STALE_CHAPTER_ID;
    data_fdps_map_player_slot_count = DEMO_STALE_PLAYER_SLOTS;
    data_fdps_map_char_spawn_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_ptr = NULL;

    for (index = 0; index < DEMO_CELL_EVENT_FLAGS; index++) {
        data_fdps_map_cell_event_triggered_flags[index] = 0;
    }

    /* The terrain panel is left switched off, which is the state a fresh
       process is in: it draws through two sheet pointers ticket 23 has not
       filled and fdps_draw_cursor_info_panel tests this global first. */
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = DEMO_ROSTER_PREFILL;

    data_fdps_chapter_event_or_battle_end_code = 0;
    data_fdps_battle_turn_counter = 1;
    data_fdps_map_cursor_draw_mode = 1;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_scene_layer_scroll_last_tick = 0;
    data_fdps_view_frame_last_tick = 0;

    demo_set_mode(DEMO_MODE_320X200X256);
    memset((void *) MOVIE_BASE, MOVIE_SENTINEL, (size_t) MOVIE_WINDOW_BYTES);

    demo_saved_timer = _dos_getvect(DEMO_TIMER_VECTOR);
    _dos_setvect(DEMO_TIMER_VECTOR, demo_tick_isr);

    fdps_title_demo();

    _dos_setvect(DEMO_TIMER_VECTOR, demo_saved_timer);

    seen_frame_non_zero = demo_frame_non_zero_count();
    seen_tail_first = (int) ((unsigned char *) MOVIE_BASE)[MOVIE_TAIL_FIRST];
    seen_tail_last = (int) ((unsigned char *) MOVIE_BASE)[MOVIE_TAIL_LAST];

    demo_set_mode(DEMO_MODE_TEXT);

    demo_capture();
    demo_free_globals();
    demo_state = 1;
}

/* The demo rebuilds the chapter the STORE at 0002ac1c names, not the one that
   was already loaded.  Both map counts are staged at numbers MAP25.DAT does
   not carry -- 99 player slots and 0 deployments -- and the chapter global at
   3, so the twelve, the eighty and the 0x19 coming back are the resource
   load's own answer and say which map it loaded. */
static void demo_loads_chapter_25_and_leaves_the_id_standing(void)
{
    demo_run();
    CHECK_EQ(demo_state, 1);
    if (demo_state != 1) {
        return;
    }

    CHECK_EQ(seen_chapter_id, DEMO_CHAPTER_ID);
    CHECK_EQ(seen_player_slots, DEMO_MAP_PLAYER_SLOTS);
    CHECK_EQ(seen_char_spawns, DEMO_MAP_CHAR_SPAWNS);
    CHECK_EQ(seen_unit_count, DEMO_MAP_UNITS);
}

/* The twelve characters, and the ORDER they were enrolled in.  Player slot i
   is filled from roster slot i, so the char_id sequence 0, 1, 8, 9, 2, 10, 3,
   11, 4, 12, 5, 6 is the order of the twelve calls, and any pair swapped in
   the body swaps a pair here.  The roster count is staged at 7 rather than 0,
   so the store at 0002ac2d is what makes slot 0 the first member. */
static void demo_enrols_its_twelve_characters_in_order(void)
{
    demo_run();
    CHECK_EQ(demo_state, 1);
    if (demo_state != 1) {
        return;
    }

    CHECK_EQ(seen_char_id[0], 0);
    CHECK_EQ(seen_char_id[1], 1);
    CHECK_EQ(seen_char_id[2], 8);
    CHECK_EQ(seen_char_id[3], 9);
    CHECK_EQ(seen_char_id[4], 2);
    CHECK_EQ(seen_char_id[5], 10);
    CHECK_EQ(seen_char_id[6], 3);
    CHECK_EQ(seen_char_id[7], 11);
    CHECK_EQ(seen_char_id[8], 4);
    CHECK_EQ(seen_char_id[9], 12);
    CHECK_EQ(seen_char_id[10], 5);
    CHECK_EQ(seen_char_id[11], 6);
}

/* One byte of one record is written before the battle: unit 3's column, and
   nothing else.  The tile row it keeps is MAP25.COD's 37 for that slot, and
   the three columns read back beside it are the map's own -- a store that had
   landed on the wrong record, or that had written the pair, would move one of
   the five. */
static void demo_moves_only_unit_threes_column(void)
{
    demo_run();
    CHECK_EQ(demo_state, 1);
    if (demo_state != 1) {
        return;
    }

    CHECK_EQ(seen_moved_pos_x, DEMO_MOVED_POS_X);
    CHECK_EQ(seen_moved_pos_y, DEMO_MOVED_POS_Y);
    CHECK_EQ(seen_unit0_pos_x, DEMO_UNIT0_POS_X);
    CHECK_EQ(seen_unit2_pos_x, DEMO_UNIT2_POS_X);
    CHECK_EQ(seen_unit4_pos_x, DEMO_UNIT4_POS_X);
}

/* The canned stat line, on all twelve.  The side is the one value that was not
   already there: fdps_build_map_unit_array puts a party record on side 2, so
   side 1 can only be the store at 0002ad20.  Current and maximum are asserted
   separately for both HP and MP because the body writes each of the four with
   its own instruction. */
static void demo_gives_every_party_unit_the_same_canned_stats(void)
{
    int slot;

    demo_run();
    CHECK_EQ(demo_state, 1);
    if (demo_state != 1) {
        return;
    }

    for (slot = 0; slot < DEMO_PARTY_UNITS; slot++) {
        CHECK_EQ(seen_side[slot], DEMO_SIDE_NPC);
        CHECK_EQ(seen_hp_current[slot], DEMO_HP);
        CHECK_EQ(seen_hp_max[slot], DEMO_HP);
        CHECK_EQ(seen_mp_current[slot], DEMO_MP);
        CHECK_EQ(seen_mp_max[slot], DEMO_MP);
        CHECK_EQ(seen_ap[slot], DEMO_ATTACK);
        CHECK_EQ(seen_hit[slot], DEMO_HIT);
    }
}

/* The two stat words the loop steps over stay as the recompute left them.
   Defense sits between maximum MP and hit and evade right after hit, so a body
   that had walked the stat block instead of naming six offsets, or that had
   put the 400 at 0x4e instead of 0x4c, would overwrite one of these two. */
static void demo_leaves_defense_and_evade_alone(void)
{
    demo_run();
    CHECK_EQ(demo_state, 1);
    if (demo_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit1_dp, DEMO_FIXTURE_DP_BASE);
    CHECK_EQ(seen_unit1_ev, DEMO_FIXTURE_DX_BASE);
}

/* The stat loop stops at twelve and the behaviour reset at eleven, and unit 12
   is what says so: it is MAP25.DAT's deployment record 0, and it still carries
   that record's side, character and AI class and the fixture's enemy HP rather
   than the demo's 2000 -- thirty levels of the fixture's hundred.  A bound of
   data_fdps_map_unit_count instead of 0xc would have given it the canned
   line. */
static void demo_touches_the_twelve_party_slots_and_no_deployment(void)
{
    demo_run();
    CHECK_EQ(demo_state, 1);
    if (demo_state != 1) {
        return;
    }

    CHECK_EQ(seen_deployed_char, DEMO_FIRST_DEPLOYED_CHAR);
    CHECK_EQ(seen_deployed_side, DEMO_FIRST_DEPLOYED_SIDE);
    CHECK_EQ(seen_deployed_ai, DEMO_FIRST_DEPLOYED_AI);
    CHECK_EQ(seen_deployed_hp_max,
             DEMO_FIRST_DEPLOYED_LEVEL * DEMO_FIXTURE_ENEMY_HP);
}

/* The behaviour byte's low nibble is reset over slots 0..11 and its high
   nibble survives.  The roster block reached the reset carrying 0x5a in that
   field, so 0x50 is the whole of the claim: 0x00 would mean the byte was
   assigned rather than masked, and 0x5a that the range never reached the
   record. */
static void demo_resets_the_low_nibble_of_every_party_behaviour_byte(void)
{
    int slot;

    demo_run();
    CHECK_EQ(demo_state, 1);
    if (demo_state != 1) {
        return;
    }

    for (slot = 0; slot < DEMO_PARTY_UNITS; slot++) {
        CHECK_EQ(seen_behavior[slot], DEMO_BEHAVIOR_AFTER_RESET);
    }
}

/* One spell to one unit, and a different one to another.  Each grant is read
   as the bit it sets AND as the byte the other grant's bit lives in, so a body
   that had passed a pair in the other order, or given both to one unit, moves
   an answer; unit 2 is the bystander that must have neither. */
static void demo_grants_one_spell_each_to_two_of_the_party(void)
{
    demo_run();
    CHECK_EQ(demo_state, 1);
    if (demo_state != 1) {
        return;
    }

    CHECK_EQ(seen_grant_a_byte, DEMO_GRANT_BIT_A);
    CHECK_EQ(seen_grant_a_other, 0);
    CHECK_EQ(seen_grant_b_byte, DEMO_GRANT_BIT_B);
    CHECK_EQ(seen_grant_b_other, 0);
    CHECK_EQ(seen_bystander_a_byte, 0);
    CHECK_EQ(seen_bystander_b_byte, 0);
}

/* The way out: the party is disbanded, the terrain panel is switched back on,
   and the whole mode 13h frame is blanked -- and only the frame, because the
   two bytes above it still carry the sentinel the run painted the window with.
   The member count reaches the store at 0002ae08 holding the demo's own twelve
   and the play flag reaches 0002ae12 holding 0x5a, so neither answer is what
   was there before. */
static void demo_hands_the_screen_back_blanked_and_the_party_disbanded(void)
{
    demo_run();
    CHECK_EQ(demo_state, 1);
    if (demo_state != 1) {
        return;
    }

    CHECK_EQ(seen_roster_count, 0);
    CHECK_EQ(seen_play_active, 1);
    CHECK_EQ(seen_frame_non_zero, 0);
    CHECK_EQ(seen_tail_first, MOVIE_SENTINEL);
    CHECK_EQ(seen_tail_last, MOVIE_SENTINEL);
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
    RUN_TEST(credits_premise_each_caption_entry_marks_its_own_slot);
    RUN_TEST(credits_premise_the_two_blend_pairs_differ);
    RUN_TEST(credits_the_chapter_30_ending_captions_from_entry_0x20);
    RUN_TEST(credits_composites_through_the_ending_blend_tables);
    RUN_TEST(credits_puts_the_map_blend_tables_back);
    RUN_TEST(credits_uploads_the_ending_palette_and_restores_the_main_one);
    RUN_TEST(credits_hands_the_screen_to_the_ending_movie);
    RUN_TEST(credits_paces_every_frame_on_the_tick);
    RUN_TEST(credits_a_single_card_leaves_no_page_behind);
    RUN_TEST(credits_the_chapter_27_ending_skips_roster_slot_three);
    RUN_TEST(credits_leaks_one_page_for_every_card_but_the_last);
    RUN_TEST(demo_loads_chapter_25_and_leaves_the_id_standing);
    RUN_TEST(demo_enrols_its_twelve_characters_in_order);
    RUN_TEST(demo_moves_only_unit_threes_column);
    RUN_TEST(demo_gives_every_party_unit_the_same_canned_stats);
    RUN_TEST(demo_leaves_defense_and_evade_alone);
    RUN_TEST(demo_touches_the_twelve_party_slots_and_no_deployment);
    RUN_TEST(demo_resets_the_low_nibble_of_every_party_behaviour_byte);
    RUN_TEST(demo_grants_one_spell_each_to_two_of_the_party);
    RUN_TEST(demo_hands_the_screen_back_blanked_and_the_party_disbanded);
}
