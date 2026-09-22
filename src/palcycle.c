/* palcycle.c -- palette-cycling animations for scenes and for the UI.
 *
 * See palcycle.h for what a caller has to know.  This file owns two pairs of
 * globals and nothing else: the UI cycle's phase and serviced-tick latch, and
 * the scene cycle's.  Everything else it touches it reads -- the timer tick
 * counter and the current chapter index, both declared in gamedata.h.
 *
 * The two cycles are not variations of one routine.  The UI cycle writes the
 * DAC itself and waits for the retrace before every call whether or not a
 * colour changes; the scene cycle hands its three channel arrays to
 * fdps_set_palette_range_on_retrace and does the waiting only inside it.
 *
 * inp and outp come from <conio.h> as ordinary library calls, which is what
 * the original has: 00015be0 issues CALL 0003d4e4 and CALL 00042cb8 rather
 * than IN/OUT instructions.  Watcom only turns them into instructions when
 * __INLINE_FUNCTIONS__ is defined, and the flag that defines it, -oi, is not
 * in this build's set (rebuild_info/build_flags.md).
 */
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "cdaudio.h"
#include "palette.h"
#include "palcycle.h"

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* 00060014. Starts at 15, the top of the cycle, exactly as the image ships it.
   Nothing ever resets the phase except the wrap from -1 back to 15, so a zero
   start would leave the UI colour wave permanently offset from the original.
   */
int data_fdps_ui_palette_cycle_phase = 15;

/* 00063fbc. Starts at zero (BSS); the first fdps_cycle_ui_palette call cycles
   unless the tick counter also still reads zero, which matches the original's
   zeroed storage. */
unsigned int data_fdps_ui_palette_last_cycle_tick;

/* 00069d20. Starts at zero (bss); the first call of fdps_cycle_scene_palette
   therefore cycles unless the timer tick counter is also still zero. */
unsigned int data_fdps_scene_palette_last_cycle_tick;

/* 00069d24. Starts at zero in the image (BSS), so the first scene palette
   cycle begins at phase 0 and advances to 1 before its first colour write. */
int data_fdps_scene_palette_cycle_phase;

/* End of global data. */

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit anyone here looks at. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The DAC write port pair: the entry number goes to 0x3c8 and its red, green
   and blue components follow on 0x3c9, six bits each. */
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9

/* The animated block is DAC entries 8..15 -- eight entries starting at 8. */
#define UI_PALETTE_FIRST_DAC_ENTRY 8
#define UI_PALETTE_ENTRY_COUNT 8

/* The colour wave is a 16-step cycle, so the phase runs 0..15, but each ramp
   is stored with 24 entries: the first eight are repeated at the end so that
   ramp[phase + entry] with phase up to 15 and entry up to 7 reaches 22 without
   ever having to wrap.  Declaring the ramps with the cycle's 16 entries and
   indexing them the same way reads past the end of the array instead of doing
   the modulo the repetition stands in for (rebuild_info/pitfalls.md). */
#define UI_PALETTE_RAMP_LENGTH 24
#define UI_PALETTE_LAST_PHASE 15

/* 00015be0.  The retrace wait is unconditional and comes first; see the
   header for why it must not be moved behind the tick test.

   The three ramps are function-local arrays with initialisers, which is what
   the read-only copies at 00014740, 00014758 and 00014770 are: the prologue
   REP MOVSDs 24 bytes from each of them onto the frame before anything else
   happens, in the declaration order below.  Their contents are those three
   blocks read back byte for byte.  In 6-bit DAC units the wave runs between a
   light green-cyan (14,42,26) and a near-black green (0,14,0).

   The once-per-tick guard is an equality test, not an ordering one -- CMP EAX,
   dword ptr [00069d64] / JZ -- so a tick counter that has wrapped past the
   latch still services the frame.  On an equal tick the routine returns having
   changed nothing at all: not the DAC, not the phase. */
void fdps_cycle_ui_palette(void)
{
    unsigned char red_ramp[UI_PALETTE_RAMP_LENGTH] = {
        14, 11,  8,  5,  3,  2,  0,  0,
         0,  0,  3,  3,  5,  8, 11, 14,
        14, 11,  8,  5,  3,  2,  0,  0
    };
    unsigned char green_ramp[UI_PALETTE_RAMP_LENGTH] = {
        42, 37, 33, 30, 26, 21, 18, 14,
        14, 18, 21, 26, 30, 33, 37, 42,
        42, 37, 33, 30, 26, 21, 18, 14
    };
    unsigned char blue_ramp[UI_PALETTE_RAMP_LENGTH] = {
        26, 21, 16, 12,  8,  4,  2,  0,
         0,  2,  4,  8, 12, 16, 21, 26,
        26, 21, 16, 12,  8,  4,  2,  0
    };
    int entry;

    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        /* Spin until the retrace begins.  This is the frame pace of every
           modal UI loop in the game. */
    }

    if (data_fdps_ui_palette_last_cycle_tick != data_fdps_timer_tick_counter) {
        data_fdps_ui_palette_last_cycle_tick = data_fdps_timer_tick_counter;

        for (entry = 0; entry < UI_PALETTE_ENTRY_COUNT; entry++) {
            outp(VGA_DAC_WRITE_INDEX, UI_PALETTE_FIRST_DAC_ENTRY + entry);
            outp(VGA_DAC_DATA,
                 red_ramp[data_fdps_ui_palette_cycle_phase + entry]);
            outp(VGA_DAC_DATA,
                 green_ramp[data_fdps_ui_palette_cycle_phase + entry]);
            outp(VGA_DAC_DATA,
                 blue_ramp[data_fdps_ui_palette_cycle_phase + entry]);
        }

        data_fdps_ui_palette_cycle_phase = data_fdps_ui_palette_cycle_phase - 1;
        if (data_fdps_ui_palette_cycle_phase == -1) {
            data_fdps_ui_palette_cycle_phase = UI_PALETTE_LAST_PHASE;
        }
    }
}

/* How many timer ticks the window sits on one colour before it slides by one.
   It is why the phase runs to count * 4 rather than to count: the modulus in
   every arm below is the colour count times this, and the window start is the
   phase divided by it. */
#define SCENE_TICKS_PER_COLOR 4

/* Where the sliding window over a chapter's three ramps currently starts.
   Spelled as a macro rather than lifted into a local because that is what the
   original computes: each of the three ramp arguments below recomputes the
   division from the phase global, and nothing between them can change it. */
#define SCENE_RAMP_START \
    (data_fdps_scene_palette_cycle_phase / SCENE_TICKS_PER_COLOR)

/* The nine arms of the chapter switch.  A chapter's cycle is a run of
   consecutive DAC entries at the top of the palette starting at
   <chapter>_FIRST_DAC_ENTRY, <chapter>_COLOR_COUNT of them.  Chapter numbers
   in the comments are the 1-based ones the player sees; the switch itself
   compares the 0-based chapter index, which is one lower
   (chapters/_index.md). */

/* chapter 1 and chapter 27 -- DAC entries 240..244, a 5-colour cycle. */
#define CHAPTER00_26_FIRST_DAC_ENTRY 0xf0
#define CHAPTER00_26_COLOR_COUNT 5

/* chapter 3 -- DAC entries 240..254, a 15-colour cycle. */
#define CHAPTER02_FIRST_DAC_ENTRY 0xf0
#define CHAPTER02_COLOR_COUNT 15

/* chapter 9 -- DAC entries 246..254, a 9-colour cycle. */
#define CHAPTER08_FIRST_DAC_ENTRY 0xf6
#define CHAPTER08_COLOR_COUNT 9

/* chapter 21 -- DAC entries 240..254, a 15-colour cycle. */
#define CHAPTER20_FIRST_DAC_ENTRY 0xf0
#define CHAPTER20_COLOR_COUNT 15

/* chapter 22 -- DAC entries 248..254, a 7-colour cycle. */
#define CHAPTER21_FIRST_DAC_ENTRY 0xf8
#define CHAPTER21_COLOR_COUNT 7

/* chapter 23 -- DAC entries 240..254, a 15-colour cycle. */
#define CHAPTER22_FIRST_DAC_ENTRY 0xf0
#define CHAPTER22_COLOR_COUNT 15

/* chapter 24 -- DAC entries 240..250, an 11-colour cycle. */
#define CHAPTER23_FIRST_DAC_ENTRY 0xf0
#define CHAPTER23_COLOR_COUNT 11

/* chapters 28 and 29 -- DAC entries 245..254, a 10-colour cycle. */
#define CHAPTER27_28_FIRST_DAC_ENTRY 0xf5
#define CHAPTER27_28_COLOR_COUNT 10

/* chapter 30 -- DAC entries 240..254, a 15-colour cycle. */
#define CHAPTER29_FIRST_DAC_ENTRY 0xf0
#define CHAPTER29_COLOR_COUNT 15

/* 0002eab0.  One frame's worth of the scene backdrop's colour cycling.  Every
   modal loop that shows the map or a scene calls it once per pass.

   The twenty-seven ramps are function-local arrays with initialisers, which is
   what the read-only block at 0002b2a0-0002b4e9 is: the prologue copies all
   586 bytes of it onto the frame with MOVSD runs before anything else happens,
   in the declaration order below, and it does so on every call -- including
   the calls that return at the tick guard without touching a colour.  Making
   them static const would delete that copying, and with it the per-call cost
   that this function contributes to a frame in which the callee also waits for
   the vertical retrace.

   Each ramp is stored at twice its colour count less one, so the window
   ramp[start .. start + count - 1] with start up to count - 1 reaches
   2 * count - 2 without ever having to wrap.  The last byte of every ramp is
   therefore never read, and chapter 21's green ramp is a byte longer still --
   the original declares it at 2 * count, and its final two bytes are dead.

   The guard is an equality test, not an ordering one -- CMP EAX,dword ptr
   [00069d64] / JZ 0002f22b -- so a tick counter that has wrapped past the
   latch still services the frame.  On an already-serviced tick the routine
   returns having changed nothing: not the DAC, not the phase, and not the
   latch.

   A chapter index the switch does not list changes no colour and does not
   move the phase, but does still latch the tick: the default arm jumps to the
   same tail every case falls into.  The phase is one global shared by all nine
   arms rather than one per chapter, so a chapter change carries the previous
   chapter's phase in, and the modulus of the new arm is what brings it back
   into range on the first serviced tick.

   Takes nothing and returns nothing. */
void fdps_cycle_scene_palette(void)
{
    /* 02b2a0 */
    unsigned char chapter00_26_red_ramp[2 * CHAPTER00_26_COLOR_COUNT - 1] = {
        59, 55, 49, 51, 55, 59, 55, 49, 51
    };
    /* 02b2a9 */
    unsigned char chapter00_26_green_ramp[2 * CHAPTER00_26_COLOR_COUNT - 1] = {
        60, 57, 51, 54, 57, 60, 57, 51, 54
    };
    /* 02b2b2 */
    unsigned char chapter00_26_blue_ramp[2 * CHAPTER00_26_COLOR_COUNT - 1] = {
        62, 60, 58, 59, 60, 62, 60, 58, 59
    };
    /* 02b2bb */
    unsigned char chapter02_red_ramp[2 * CHAPTER02_COLOR_COUNT - 1] = {
        24, 30, 35, 42, 47, 53, 59, 54, 49, 44,
        38, 33, 28, 24, 19, 24, 30, 35, 42, 47,
        53, 59, 54, 49, 44, 38, 33, 28, 24
    };
    /* 02b2d8 */
    unsigned char chapter02_green_ramp[2 * CHAPTER02_COLOR_COUNT - 1] = {
         0,  0,  2,  4,  6,  9, 12, 10,  7,  5,
         4,  2,  2,  0,  0,  0,  0,  2,  4,  6,
         9, 12, 10,  7,  5,  4,  2,  2,  0
    };
    /* 02b2f5 */
    unsigned char chapter02_blue_ramp[2 * CHAPTER02_COLOR_COUNT - 1] = {
         2,  4,  7, 11, 15, 19, 25, 18, 16, 12,
         8,  6,  3,  2,  0,  2,  4,  7, 11, 15,
        19, 25, 18, 16, 12,  8,  6,  3,  2
    };
    /* 02b312 */
    unsigned char chapter08_red_ramp[2 * CHAPTER08_COLOR_COUNT - 1] = {
        57, 51, 45, 40, 42, 44, 46, 49, 51, 57,
        51, 45, 40, 42, 44, 46, 49
    };
    /* 02b323 */
    unsigned char chapter08_green_ramp[2 * CHAPTER08_COLOR_COUNT - 1] = {
        53, 38, 24, 11, 16, 21, 27, 33, 38, 53,
        38, 24, 11, 16, 21, 27, 33
    };
    /* 02b334 */
    unsigned char chapter08_blue_ramp[2 * CHAPTER08_COLOR_COUNT - 1] = {
        18, 12,  7,  3,  4,  6,  9, 10, 12, 18,
        12,  7,  3,  4,  6,  9, 10
    };
    /* 02b345 */
    unsigned char chapter20_red_ramp[2 * CHAPTER20_COLOR_COUNT - 1] = {
        14, 15, 16, 18, 19, 21, 21, 28, 35, 42,
        33, 25, 17, 15, 14, 14, 15, 16, 18, 19,
        21, 21, 28, 35, 42, 33, 25, 17, 15
    };
    /* 02b362 */
    unsigned char chapter20_green_ramp[2 * CHAPTER20_COLOR_COUNT] = {
         2,  2,  2,  3,  3,  4,  4,  6,  8, 12,
         8,  5,  3,  2,  2,  2,  2,  2,  3,  3,
         4,  4,  6,  8, 12,  8,  5,  3,  2,  2
    };
    /* 02b380 */
    unsigned char chapter20_blue_ramp[2 * CHAPTER20_COLOR_COUNT - 1] = {
         0,  0,  0,  0,  2,  2,  2,  3,  6,  9,
         5,  2,  0,  0,  0,  0,  0,  0,  0,  2,
         2,  2,  3,  6,  9,  5,  2,  0,  0
    };
    /* 02b39d */
    unsigned char chapter21_red_ramp[2 * CHAPTER21_COLOR_COUNT - 1] = {
        37, 38, 40, 42, 40, 38, 37, 37, 38, 40,
        42, 40, 38
    };
    /* 02b3aa */
    unsigned char chapter21_green_ramp[2 * CHAPTER21_COLOR_COUNT - 1] = {
         8,  6,  4,  2,  4,  6,  8,  8,  6,  4,
         2,  4,  6
    };
    /* 02b3b7 */
    unsigned char chapter21_blue_ramp[2 * CHAPTER21_COLOR_COUNT - 1] = {
         8,  6,  4,  2,  4,  6,  8,  8,  6,  4,
         2,  4,  6
    };
    /* 02b3c4 */
    unsigned char chapter22_red_ramp[2 * CHAPTER22_COLOR_COUNT - 1] = {
        24, 28, 32, 35, 40, 44, 47, 51, 49, 44,
        38, 33, 28, 24, 20, 24, 28, 32, 35, 40,
        44, 47, 51, 49, 44, 38, 33, 28, 24
    };
    /* 02b3e1 */
    unsigned char chapter22_green_ramp[2 * CHAPTER22_COLOR_COUNT - 1] = {
         0,  0,  2,  4,  6,  8, 11, 14,  3,  2,
         2,  0,  0,  0,  0,  0,  0,  2,  4,  6,
         8, 11, 14,  3,  2,  2,  0,  0,  0
    };
    /* 02b3fe */
    unsigned char chapter22_blue_ramp[2 * CHAPTER22_COLOR_COUNT - 1] = {
         0,  0,  0,  0,  0,  0,  2,  2,  3,  2,
         2,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  2,  2,  3,  2,  2,  0,  0,  0
    };
    /* 02b41b */
    unsigned char chapter23_red_ramp[2 * CHAPTER23_COLOR_COUNT - 1] = {
        28, 32, 35, 40, 43, 40, 38, 37, 33, 30,
        28, 28, 32, 35, 40, 43, 40, 38, 37, 33,
        30
    };
    /* 02b430 */
    unsigned char chapter23_green_ramp[2 * CHAPTER23_COLOR_COUNT - 1] = {
        10,  8,  5,  9, 11,  8,  7,  4,  7,  8,
        10, 10,  8,  5,  9, 11,  8,  7,  4,  7,
         8
    };
    /* 02b445 */
    unsigned char chapter23_blue_ramp[2 * CHAPTER23_COLOR_COUNT - 1] = {
         0,  0,  0,  2,  4,  3,  2,  2,  0,  0,
         0,  0,  0,  0,  2,  4,  3,  2,  2,  0,
         0
    };
    /* 02b45a */
    unsigned char chapter27_28_red_ramp[2 * CHAPTER27_28_COLOR_COUNT - 1] = {
         2,  4, 12, 19, 20, 20, 15,  8,  4,  2,
         2,  4, 12, 19, 20, 20, 15,  8,  4
    };
    /* 02b46d */
    unsigned char chapter27_28_green_ramp[2 * CHAPTER27_28_COLOR_COUNT - 1] = {
         9,  5,  7, 11, 15, 12,  9,  6,  6,  9,
         9,  5,  7, 11, 15, 12,  9,  6,  6
    };
    /* 02b480 */
    unsigned char chapter27_28_blue_ramp[2 * CHAPTER27_28_COLOR_COUNT - 1] = {
        19, 18, 21, 23, 25, 23, 21, 21, 18, 19,
        19, 18, 21, 23, 25, 23, 21, 21, 18
    };
    /* 02b493 */
    unsigned char chapter29_red_ramp[2 * CHAPTER29_COLOR_COUNT - 1] = {
         0,  0,  2,  4,  6,  4,  3,  3,  2,  4,
         6,  9,  5,  2,  0,  0,  0,  2,  4,  6,
         4,  3,  3,  2,  4,  6,  9,  5,  2
    };
    /* 02b4b0 */
    unsigned char chapter29_green_ramp[2 * CHAPTER29_COLOR_COUNT - 1] = {
         5,  5,  6,  7,  8,  7,  6,  6,  6,  7,
         8, 10,  7,  6,  5,  5,  5,  6,  7,  8,
         7,  6,  6,  6,  7,  8, 10,  7,  6
    };
    /* 02b4cd */
    unsigned char chapter29_blue_ramp[2 * CHAPTER29_COLOR_COUNT - 1] = {
        13, 15, 17, 19, 21, 18, 19, 18, 17, 19,
        21, 25, 21, 17, 13, 13, 15, 17, 19, 21,
        18, 19, 18, 17, 19, 21, 25, 21, 17
    };
    int chapter_index;

    fdps_cd_music_repeat_poll();

    if (data_fdps_scene_palette_last_cycle_tick ==
        data_fdps_timer_tick_counter) {
        return;
    }

    chapter_index = data_fdps_chapter_current_chapter_id;

    switch (chapter_index) {
    case 0:
    case 26:
        /* 0002ed64.  Chapters 1 and 27 share this arm: the dispatch has one
           body for both indices, not two identical ones. */
        data_fdps_scene_palette_cycle_phase =
            (data_fdps_scene_palette_cycle_phase + 1) %
            (CHAPTER00_26_COLOR_COUNT * SCENE_TICKS_PER_COLOR);
        fdps_set_palette_range_on_retrace(
            CHAPTER00_26_FIRST_DAC_ENTRY,
            chapter00_26_red_ramp + SCENE_RAMP_START,
            chapter00_26_green_ramp + SCENE_RAMP_START,
            chapter00_26_blue_ramp + SCENE_RAMP_START,
            CHAPTER00_26_COLOR_COUNT);
        break;

    case 2:
        /* 0002ede5.  chapter 3. */
        data_fdps_scene_palette_cycle_phase =
            (data_fdps_scene_palette_cycle_phase + 1) %
            (CHAPTER02_COLOR_COUNT * SCENE_TICKS_PER_COLOR);
        fdps_set_palette_range_on_retrace(
            CHAPTER02_FIRST_DAC_ENTRY,
            chapter02_red_ramp + SCENE_RAMP_START,
            chapter02_green_ramp + SCENE_RAMP_START,
            chapter02_blue_ramp + SCENE_RAMP_START,
            CHAPTER02_COLOR_COUNT);
        break;

    case 8:
        /* 0002ee6f.  chapter 9. */
        data_fdps_scene_palette_cycle_phase =
            (data_fdps_scene_palette_cycle_phase + 1) %
            (CHAPTER08_COLOR_COUNT * SCENE_TICKS_PER_COLOR);
        fdps_set_palette_range_on_retrace(
            CHAPTER08_FIRST_DAC_ENTRY,
            chapter08_red_ramp + SCENE_RAMP_START,
            chapter08_green_ramp + SCENE_RAMP_START,
            chapter08_blue_ramp + SCENE_RAMP_START,
            CHAPTER08_COLOR_COUNT);
        break;

    case 20:
        /* 0002eef3.  chapter 21. */
        data_fdps_scene_palette_cycle_phase =
            (data_fdps_scene_palette_cycle_phase + 1) %
            (CHAPTER20_COLOR_COUNT * SCENE_TICKS_PER_COLOR);
        fdps_set_palette_range_on_retrace(
            CHAPTER20_FIRST_DAC_ENTRY,
            chapter20_red_ramp + SCENE_RAMP_START,
            chapter20_green_ramp + SCENE_RAMP_START,
            chapter20_blue_ramp + SCENE_RAMP_START,
            CHAPTER20_COLOR_COUNT);
        break;

    case 21:
        /* 0002ef7d.  chapter 22. */
        data_fdps_scene_palette_cycle_phase =
            (data_fdps_scene_palette_cycle_phase + 1) %
            (CHAPTER21_COLOR_COUNT * SCENE_TICKS_PER_COLOR);
        fdps_set_palette_range_on_retrace(
            CHAPTER21_FIRST_DAC_ENTRY,
            chapter21_red_ramp + SCENE_RAMP_START,
            chapter21_green_ramp + SCENE_RAMP_START,
            chapter21_blue_ramp + SCENE_RAMP_START,
            CHAPTER21_COLOR_COUNT);
        break;

    case 22:
        /* 0002effe.  chapter 23. */
        data_fdps_scene_palette_cycle_phase =
            (data_fdps_scene_palette_cycle_phase + 1) %
            (CHAPTER22_COLOR_COUNT * SCENE_TICKS_PER_COLOR);
        fdps_set_palette_range_on_retrace(
            CHAPTER22_FIRST_DAC_ENTRY,
            chapter22_red_ramp + SCENE_RAMP_START,
            chapter22_green_ramp + SCENE_RAMP_START,
            chapter22_blue_ramp + SCENE_RAMP_START,
            CHAPTER22_COLOR_COUNT);
        break;

    case 23:
        /* 0002f088.  chapter 24. */
        data_fdps_scene_palette_cycle_phase =
            (data_fdps_scene_palette_cycle_phase + 1) %
            (CHAPTER23_COLOR_COUNT * SCENE_TICKS_PER_COLOR);
        fdps_set_palette_range_on_retrace(
            CHAPTER23_FIRST_DAC_ENTRY,
            chapter23_red_ramp + SCENE_RAMP_START,
            chapter23_green_ramp + SCENE_RAMP_START,
            chapter23_blue_ramp + SCENE_RAMP_START,
            CHAPTER23_COLOR_COUNT);
        break;

    case 27:
    case 28:
        /* 0002f112.  Chapters 28 and 29 share this arm.  The test that
           selects it is CMP dword ptr [EBP-0x298],0x1c / JBE 0002f112, and it
           is reached only after CMP 0x1a / JBE has already sent everything up
           to 0x1a elsewhere, so the JBE admits 0x1b as well as 0x1c. */
        data_fdps_scene_palette_cycle_phase =
            (data_fdps_scene_palette_cycle_phase + 1) %
            (CHAPTER27_28_COLOR_COUNT * SCENE_TICKS_PER_COLOR);
        fdps_set_palette_range_on_retrace(
            CHAPTER27_28_FIRST_DAC_ENTRY,
            chapter27_28_red_ramp + SCENE_RAMP_START,
            chapter27_28_green_ramp + SCENE_RAMP_START,
            chapter27_28_blue_ramp + SCENE_RAMP_START,
            CHAPTER27_28_COLOR_COUNT);
        break;

    case 29:
        /* 0002f19c.  chapter 30. */
        data_fdps_scene_palette_cycle_phase =
            (data_fdps_scene_palette_cycle_phase + 1) %
            (CHAPTER29_COLOR_COUNT * SCENE_TICKS_PER_COLOR);
        fdps_set_palette_range_on_retrace(
            CHAPTER29_FIRST_DAC_ENTRY,
            chapter29_red_ramp + SCENE_RAMP_START,
            chapter29_green_ramp + SCENE_RAMP_START,
            chapter29_blue_ramp + SCENE_RAMP_START,
            CHAPTER29_COLOR_COUNT);
        break;

    default:
        /* 0002f221 by way of the dispatch's fall-through jumps: no chapter
           matched, so no colour changes.  The tick is still latched below. */
        break;
    }

    data_fdps_scene_palette_last_cycle_tick = data_fdps_timer_tick_counter;
}
