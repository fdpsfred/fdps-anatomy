/* tests/title.c -- cover for src/title.c.
 *
 * fdps_play_movie @ 00030f40.
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
#include <string.h>
#include "testharn.h"
#include "ailv3.h"
#include "gamedata.h"
#include "audio.h"
#include "cd.h"
#include "keybd.h"
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
}
