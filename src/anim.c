/* anim.c -- VFS/SAF animation playback and the turn banner.
 *
 * See anim.h for what a caller has to know and resource_info/vfs.md for the
 * containers the animations come out of.  This file owns one piece of state,
 * the pointer to the member the last BaseAni.vfs lookup found; the archive
 * image itself belongs to the startup loader and is declared in gamedata.h.
 *
 * printf and sprintf come from <stdio.h>, exit from <stdlib.h> and strlen and
 * strcmp from <string.h>, and all five are real calls in the original -- CALL
 * 0x00042deb and CALL 0x00042e0f at 0002a284 and 0002a28e, CALL 0x00042d41 at
 * 0001ea9c, CALL 0x00042dd2 at 0001eaaf and CALL 0x00042fe0 at 0001ec15 --
 * because the flag set carries no -oi (rebuild_info/build_flags.md), so the
 * plain declarations are what reproduce them.  malloc and free come from
 * <stdlib.h>, memmove from <string.h>, delay from <i86.h> and inp from
 * <conio.h>, which is where Watcom 10.0a declares each of them; all four are
 * calls in the original too -- CALL 0x0003d375 at 0001e860, CALL 0x0003d478 at
 * 0001ea68, CALL 0x0003d514 at 0001eb4f, CALL 0x0003d370 at 0001e983 and CALL
 * 0x0003d4e4 at 0001ebc3.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "vfs.h"
#include "blit.h"
#include "sprite.h"
#include "saf.h"
#include "gauge.h"
#include "unit.h"
#include "aitarget.h"
#include "mapdraw.h"
#include "anim.h"

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* 000643ec. Starts NULL in the image; it is always overwritten by
   fdps_baseani_get_entry_or_exit before being read, so the initial value only
   has to be zero. */
unsigned char *data_fdps_animation_baseani_entry_ptr;

/* End of global data. */

/* 0002a240.  One branch, CMP dword ptr [0x000643ec],0x0 / JZ at 0002a267, and
   the not-found arm ends in the exit call, so the ADD ESP,0x4 and the store of
   0 into the return slot that follow it at 0002a293 are never executed.

   The global is written from EAX at 0002a262 and then re-read twice, once for
   the test at 0002a267 and once for the value returned at 0002a270: the
   returned pointer is the global's contents and not a register held across the
   test.  That is visible to a caller only in that both say the same thing here,
   and it is why the answer is published before it is known to be good.

   The size the lookup writes is discarded.  It is asked for because
   fdps_vfs_image_get_entry insists on somewhere to put it -- LEA EAX,[EBP +
   -0x8] / PUSH EAX at 0002a24c -- and the slot is never read back. */
void *fdps_baseani_get_entry_or_exit(char *name)
{
    unsigned int entry_bytes;

    data_fdps_animation_baseani_entry_ptr = fdps_vfs_image_get_entry(
        (struct fdps_vfs_image_header *) data_fdps_animation_baseani_archive_ptr,
        name, &entry_bytes);
    if (data_fdps_animation_baseani_entry_ptr != NULL) {
        return data_fdps_animation_baseani_entry_ptr;
    }
    printf("File not found: %s\n", name);
    exit(1);
    return NULL;
}

/* What the formatted turn number is written into: SUB ESP,0xc at 0001ea86
   buys twelve bytes, eight of them the buffer at [EBP-0xc] and the last four
   the loop counter at [EBP-0x4].  Eight bytes hold seven digits and the
   terminator, and nothing bounds what sprintf writes here. */
#define TURN_NUMBER_BUFFER_BYTES 8

/* What a formatted digit is turned into a sprite-bank entry index with.  It is
   0x2f and NOT '0': entry 0 of the banner bank is the word graphic the caller
   draws itself and the numerals start at entry 1, so digit d is entry d + 1
   (rebuild_info/pitfalls.md). */
#define TURN_DIGIT_ENTRY_BIAS 0x2f

/* How far the request's x moves between one digit and the next, in pixels. */
#define TURN_DIGIT_PITCH 0x1c

/* 0001ea80.  One loop and no branch inside it, cyclomatic complexity 2.

   THE BYTE IS ZERO-EXTENDED AND THE COMPARE IS UNSIGNED, and both are
   behaviour rather than spelling.  XOR EDX,EDX / MOV DL,byte ptr [EAX+EBP-0xc]
   at 0001eac9 widens the formatted character without sign, which is why the
   buffer is unsigned char here; and CMP EAX,dword ptr [EBP-0x4] / JA at
   0001eab7 is the unsigned above, strlen's own size against an unsigned
   counter, so a signed counter would compare the wrong way the moment either
   side went past 0x7fffffff.

   strlen is called afresh on every pass -- the CALL at 0001eaaf is the loop's
   own condition, reached again from the increment block at 0001eac4 -- and not
   hoisted above it.

   The request is the caller's and is modified in place.  Only two of its nine
   slots are written, the entry index before each digit is painted and x after,
   so on return x sits one pitch past the last digit and the entry index holds
   the last digit's.  The order matters to what lands on screen: x is advanced
   AFTER the call, so the first digit is painted at the x the caller set.  The
   one caller, fdps_animate_turn_banner, rewrites both slots before it uses the
   block again.

   sprintf's return value is discarded -- ADD ESP,0xc at 0001eaa1 and no read
   of EAX -- and so is everything the composite drawer might have to say, which
   is nothing: it returns void. */
void fdps_draw_turn_number(int *request)
{
    unsigned char turn_digits[TURN_NUMBER_BUFFER_BYTES];
    unsigned int digit_index;

    sprintf((char *) turn_digits, "%d", data_fdps_battle_turn_counter);

    for (digit_index = 0; digit_index < strlen((char *) turn_digits);
         digit_index++) {
        request[DRAW_REQUEST_ITEM_INDEX] =
            turn_digits[digit_index] - TURN_DIGIT_ENTRY_BIAS;
        fdps_draw_composite_sprite(request, 0);
        request[DRAW_REQUEST_X] += TURN_DIGIT_PITCH;
    }
}

/* The scratch surface the banner is composed on: 360 x 240 8bpp, PUSH 0x15180
   / CALL malloc at 0001e85b. */
#define BANNER_SURFACE_PITCH 0x168
#define BANNER_SURFACE_ROWS 0xf0
#define BANNER_SURFACE_BYTES 0x15180

/* The mode 13h frame, and where the adapter answers.  Both are hard-coded in
   the original -- PUSH 0x140 at 0001e8d7 and PUSH 0xa0504 at 0001e94b -- and
   stay literals here: 0xa0000 is where the display adapter answers, not the
   address of anything the linker places. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The window of the screen the banner is allowed to disturb: 312 x 192 at
   pixel (4,4), which is byte 0x504 of a 320-pitch frame.  The same rectangle
   lands at pixel (24,24) of the scratch surface, byte 0x21d8 of a 360-pitch
   one, so the surface keeps a 24-pixel apron the sliding pieces can hang over
   without touching the screen. */
#define BANNER_WINDOW_AT 0x504
#define BANNER_WINDOW_W 0x138
#define BANNER_WINDOW_H 0xc0
#define BANNER_SURFACE_WINDOW_AT 0x21d8

/* The row of the scratch surface both pieces are drawn on, and the two ways a
   step's table entry becomes a column: the sign is placed at entry + 0x14 and
   the number at 0x12c - entry, which is why the two always sum to 0x140. */
#define BANNER_ROW 0x5c
#define BANNER_SIGN_X_BIAS 0x14
#define BANNER_NUMBER_X_BASE 0x12c

/* Entry 0 of the banner's sprite bank is the word graphic; the numerals are
   entries 1 to 10 and fdps_draw_turn_number selects those itself. */
#define BANNER_SIGN_ENTRY 0

/* Thirteen steps in, eleven out.  THE SLIDE-OUT STARTS AT ENTRY 10 AND NOT AT
   12 -- MOV dword ptr [EBP-0x4],0xa at 0001e98b -- so the banner snaps back
   two pixels the instant it starts leaving (rebuild_info/pitfalls.md). */
#define BANNER_SLIDE_STEPS 13
#define BANNER_SLIDE_OUT_FIRST 10

/* How long the assembled banner is held before it leaves, in milliseconds. */
#define BANNER_HOLD_MS 500

/* 0001e840.  Two loops, no branch inside either, and the two bodies are the
   same eight statements written out twice -- the original has them inline in
   both loops and not behind a call, so they stay written out here.

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED.  last_tick is
   read at 0001e966 before anything has written it, so the first step of the
   slide-in ends its wait at once unless the garbage on the stack happens to
   equal the counter.  The slide-out inherits the value the slide-in left, and
   delay() has moved the counter past it, so its first step is unpaced as well.
   Latching the counter before each loop -- which is what writing this cleanly
   leads to -- adds a tick to each phase.

   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of these two loops: nothing inside them writes the counter, so a
   build allowed to hoist the load would spin here forever.

   The request's entry index is put back to 0 before each sign draw -- MOV
   dword ptr [EBP-0x18],0x0 at 0001e901 -- because fdps_draw_turn_number leaves
   it holding the last digit's, and its x is rewritten for the same reason.
   Nothing else in the block moves after it is built.

   malloc's answer is used without a test, and there is no CALL whose value is
   read other than malloc's and the sheet lookup's: fdps_blit_rect,
   fdps_draw_composite_sprite, fdps_draw_turn_number and delay all return
   void. */
void fdps_animate_turn_banner(unsigned char *saved_screen)
{
    /* Where the sign's left edge sits at each step, before the 0x14 bias.  In
       the original this is a 13-dword template at 0x0001c280 copied onto the
       frame with REP MOVSD at 0001e859, which is what an initialised automatic
       array compiles to; it is not a global and has no other reader.  The last
       three entries are the overshoot: 92 is two pixels past the resting 90,
       which is where the banner settles. */
    int slide_x[BANNER_SLIDE_STEPS] = {-60, -20, 10, 35, 50, 65, 75, 80, 85,
                                       90, 92, 91, 90};
    /* The block sprite.h describes, built once and rewritten in two slots per
       step. */
    int request[DRAW_REQUEST_DWORDS];
    /* The 360x240 scratch surface the step is composed on, so that the sign
       and the number are never seen half-drawn on the adapter. */
    unsigned char *work_surface;
    /* Which entry of slide_x this step places the two pieces from. */
    int step;
    /* The tick the previous step ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    work_surface = (unsigned char *) malloc((size_t) BANNER_SURFACE_BYTES);
    request[DRAW_REQUEST_DEST_PITCH] = BANNER_SURFACE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = BANNER_SURFACE_ROWS;
    request[DRAW_REQUEST_Y] = BANNER_ROW;
    request[DRAW_REQUEST_IMAGE] =
        (int) fdps_baseani_get_entry_or_exit("Turn.saf");
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;
    request[DRAW_REQUEST_DEST_BASE] = (int) work_surface;

    for (step = 0; step < BANNER_SLIDE_STEPS; step++) {
        fdps_blit_rect((unsigned int) (saved_screen + BANNER_WINDOW_AT),
                       VGA_SCREEN_PITCH,
                       work_surface + BANNER_SURFACE_WINDOW_AT,
                       BANNER_SURFACE_PITCH, BANNER_WINDOW_W,
                       BANNER_WINDOW_H);
        request[DRAW_REQUEST_X] = slide_x[step] + BANNER_SIGN_X_BIAS;
        request[DRAW_REQUEST_ITEM_INDEX] = BANNER_SIGN_ENTRY;
        fdps_draw_composite_sprite(request, 1);
        request[DRAW_REQUEST_X] = BANNER_NUMBER_X_BASE - slide_x[step];
        fdps_draw_turn_number(request);
        fdps_blit_rect((unsigned int) (work_surface
                                       + BANNER_SURFACE_WINDOW_AT),
                       BANNER_SURFACE_PITCH,
                       (void *) (VGA_SCREEN_BASE + BANNER_WINDOW_AT),
                       VGA_SCREEN_PITCH, BANNER_WINDOW_W, BANNER_WINDOW_H);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    delay((unsigned int) BANNER_HOLD_MS);

    for (step = BANNER_SLIDE_OUT_FIRST; step >= 0; step--) {
        fdps_blit_rect((unsigned int) (saved_screen + BANNER_WINDOW_AT),
                       VGA_SCREEN_PITCH,
                       work_surface + BANNER_SURFACE_WINDOW_AT,
                       BANNER_SURFACE_PITCH, BANNER_WINDOW_W,
                       BANNER_WINDOW_H);
        request[DRAW_REQUEST_X] = slide_x[step] + BANNER_SIGN_X_BIAS;
        request[DRAW_REQUEST_ITEM_INDEX] = BANNER_SIGN_ENTRY;
        fdps_draw_composite_sprite(request, 1);
        request[DRAW_REQUEST_X] = BANNER_NUMBER_X_BASE - slide_x[step];
        fdps_draw_turn_number(request);
        fdps_blit_rect((unsigned int) (work_surface
                                       + BANNER_SURFACE_WINDOW_AT),
                       BANNER_SURFACE_PITCH,
                       (void *) (VGA_SCREEN_BASE + BANNER_WINDOW_AT),
                       VGA_SCREEN_PITCH, BANNER_WINDOW_W, BANNER_WINDOW_H);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    free(work_surface);
}

/* The container every full-screen animation is a member of, and the one member
   the turn banner is announced in front of.  Both are literals in the original
   -- MOV EAX,0x60128 at 0001eb10 and MOV EAX,0x617c0 at 0001ec0b -- and both
   have to live in writable storage: the first because fdps_vfs_open's caller
   chain folds nothing but the member name, which is the caller's buffer here,
   and the second because it is only ever compared.  It is the CALLER'S buffer
   that gets upper-cased in place, so a caller of this function is the one that
   needs writable storage (rebuild_info/pitfalls.md). */
#define ANIMATION_ARCHIVE "MISC.VFS"
#define PLAYER_PHASE_ANIMATION "PLYPHASE.SAF"

/* The mode 13h frame the fades cover, whole: 320 bytes to the row, 200 rows and
   the 64000 bytes that comes to.  VGA_SCREEN_BASE and VGA_SCREEN_PITCH are the
   banner's above and mean the same here. */
#define VGA_SCREEN_ROWS 0xc8
#define VGA_SCREEN_BYTES 0xfa00

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, which is what each fade step straddles. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* How dark the dimming pass gets and how light the restoring pass ends: both
   walk levels 1 to 5 of the shade ramp, out and back.  CMP dword ptr
   [EBP-0x8],0x6 / JL at 0001eb74 and CMP dword ptr [EBP-0x8],0x0 / JG at
   0001ec50 are both SIGNED, so the counter is a signed int. */
#define ANIMATION_FADE_LEVELS 5

/* The colour both fades weigh the screen against, PUSH 0x0 at 0001eb8b: entry 0
   of the shade ramp's tint row, which is why the screen goes to black. */
#define ANIMATION_FADE_TINT 0

/* 0001eb00.  Two counted loops with no branch inside either and one test
   between them, cyclomatic complexity 4 counting the four wait loops.

   The two fade bodies are the same six statements written out twice.  The
   original has them inline in both loops and not behind a call, and the only
   thing that differs between them is which saved copy is read and which way the
   level counter walks, so they stay written out here.

   THE TWO SAVED COPIES ARE FILLED FROM THE ADAPTER SEPARATELY, not copied from
   one another: the memmove at 0001eb4f and the one at 0001eb65 both read
   0xa0000.  The dimming pass then overwrites the first one with the dimmed
   screen at 0001ebf5, which is what makes it the clip's backdrop, and never
   touches the second, which is what leaves the restoring pass an untouched
   picture to work from.  Emitting one copy and reusing it makes the restoring
   pass fade the dimmed screen back in and the picture never returns.

   THE PLAYER-PHASE TEST IS MADE AGAINST THE FOLDED NAME, AND THAT IS THE ONLY
   REASON IT EVER MATCHES.  strcmp at 0001ec15 compares [EBP+0x14] -- the
   caller's own buffer -- with the upper-case literal at 0x617c0, while every
   one of the three call sites hands in a mixed-case one: "EnyPhase.saf" at
   0x61790 (0001e5ad), "PlyPhase.saf" at 0x617a0 (0001e609) and the same
   spelling at 0x61acc (000241c0).  What closes the gap is fdps_vfs_load_entry,
   which upper-cases the caller's buffer in place before it returns (vfs.h), so
   by the time the comparison runs the caller's own literal has been rewritten
   to "PLYPHASE.SAF".  The banner it runs draws on the adapter and leaves the
   backdrop copy alone, so the clip that follows paints straight over it and
   nothing of the banner is carried into the backdrop.

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED, the same contract
   fdps_animate_turn_banner above and fdps_saf_play_over_background (saf.c)
   carry.  last_tick is read at 0001ebe0 before anything has written it, so the
   dimming pass's first step ends its wait at once unless the stack garbage
   happens to equal the counter; the restoring pass then inherits whatever the
   clip left in it, which is a value the counter has already gone past, so its
   first step is unpaced as well.  Latching the counter before each loop adds a
   tick to each pass.

   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of these two loops: nothing inside them writes the counter, so a
   build allowed to hoist the load would spin here forever.

   No CALL's answer is read except fdps_vfs_load_entry's and the two malloc's,
   and neither malloc is tested.  fdps_blit_tint_rect, fdps_animate_turn_banner,
   fdps_saf_play_over_background and memmove all return values the original
   discards; strcmp's is the one that is tested.

   0xa0000 is written as a literal because it is the adapter's real linear
   address under DOS/4GW and not a symbol the rebuild places anywhere
   (rebuild_info/pitfalls.md, contract E). */
void fdps_play_vfs_animation(char *name)
{
    /* The member loaded out of MISC.VFS, which this function owns and frees. */
    void *animation;
    /* The screen as it was on entry, read by the dimming pass and then
       replaced by the dimmed screen the clip is played over. */
    unsigned char *backdrop;
    /* The screen as it was on entry, kept untouched for the restoring pass. */
    unsigned char *saved_screen;
    /* Which of the five shade-ramp levels this step weighs the screen at. */
    int level;
    /* The tick the previous step ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    animation = fdps_vfs_load_entry(ANIMATION_ARCHIVE, name);
    backdrop = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    saved_screen = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    memmove(backdrop, (void *) VGA_SCREEN_BASE, (size_t) VGA_SCREEN_BYTES);
    memmove(saved_screen, (void *) VGA_SCREEN_BASE, (size_t) VGA_SCREEN_BYTES);

    for (level = 1; level <= ANIMATION_FADE_LEVELS; level++) {
        fdps_blit_tint_rect(backdrop, VGA_SCREEN_PITCH,
                            (unsigned char *) VGA_SCREEN_BASE,
                            VGA_SCREEN_PITCH, VGA_SCREEN_PITCH,
                            VGA_SCREEN_ROWS,
                            data_fdps_palette_shade_ramp_table,
                            data_fdps_inverse_palette_cube,
                            ANIMATION_FADE_TINT, level);
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the step that has just been
               written is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the next step's write starts clear of
               it. */
        }
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    memmove(backdrop, (void *) VGA_SCREEN_BASE, (size_t) VGA_SCREEN_BYTES);
    if (strcmp(name, PLAYER_PHASE_ANIMATION) == 0) {
        fdps_animate_turn_banner(backdrop);
    }
    fdps_saf_play_over_background(animation, backdrop);
    free(backdrop);

    for (level = ANIMATION_FADE_LEVELS; level > 0; level--) {
        fdps_blit_tint_rect(saved_screen, VGA_SCREEN_PITCH,
                            (unsigned char *) VGA_SCREEN_BASE,
                            VGA_SCREEN_PITCH, VGA_SCREEN_PITCH,
                            VGA_SCREEN_ROWS,
                            data_fdps_palette_shade_ramp_table,
                            data_fdps_inverse_palette_cube,
                            ANIMATION_FADE_TINT, level);
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends. */
        }
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    free(animation);
    free(saved_screen);
}

/* The composite page every frame of the blow is built on: 360 x 240 with a
   24-pixel apron on all four sides, the same page fdps_battle_show_combat_gauges
   composes its seven frames on (gauge.c).  PUSH 0x15180 at 0001f032 buys it,
   and the two stores at 0001f042 and 0001f049 put the pitch and the row count
   into the draw request.  A gauge position is turned into a page pixel by
   adding the apron on both axes -- ADD EAX,0x18 / IMUL EAX,EAX,0x168 / ADD the
   page / ADD the x / ADD EAX,0x18 at 0001f088. */
#define ATTACK_PAGE_PITCH 0x168
#define ATTACK_PAGE_ROWS 0xf0
#define ATTACK_PAGE_BYTES 0x15180
#define ATTACK_PAGE_BORDER 0x18

/* The window handed to the adapter after every frame: 312 x 192 taken from
   page byte 0x21d8, which is page pixel (24,24), and put down at screen byte
   0x504, which is screen pixel (4,4).  Both come from the six pushes at
   0001f2e6.  VGA_SCREEN_BASE and VGA_SCREEN_PITCH are the banner's above and
   mean the same here, and 0xa0000 stays a literal because it is where the
   adapter answers and not the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define ATTACK_PAGE_WINDOW_AT 0x21d8
#define ATTACK_WINDOW_AT 0x504
#define ATTACK_WINDOW_W 0x138
#define ATTACK_WINDOW_H 0xc0

/* A map tile is 24 pixels square, and the sprite is drawn six pixels above the
   tile row -- IMUL EAX,EAX,0x18 at 0001f00c and 0001f023 and the SUB EAX,0x6
   at 0001f02c, the same lift fdps_draw_map_unit gives a unit's own sprite. */
#define MAP_TILE_SIZE 0x18
#define ATTACK_SPRITE_LIFT 6

/* CMP EAX,0x1 / JNZ at 0001ef6c: fdps_check_can_counter_attack answers -1 on
   every refusal and never 0, so the test is against 1 and is not a bare
   predicate (aitarget.h).  The refusal arm writes -1 into the attacker pair's
   x half and that is what suppresses its bar for the whole run -- MOV dword
   ptr [EBP-0x38],0xffffffff at 0001ef83 and the CMP ...,-0x1 at 0001f0e6 that
   reads it back. */
#define COUNTER_ATTACK_CONFIRMED 1
#define ATTACK_GAUGE_NO_BAR (-1)

/* CMP byte ptr [EAX+0x6],0x0 / JNZ at 0001efb0 and 0001efec: a side byte of 0
   picks graphic 2 of the unit gauge sheet and every other side picks graphic 1,
   the same reading fdps_battle_show_combat_gauges makes. */
#define ATTACK_GAUGE_SIDE_ZERO_GRAPHIC 2
#define ATTACK_GAUGE_OTHER_SIDE_GRAPHIC 1

/* The bar's 41-column interior, and the two painting arguments both bars are
   drawn with: PUSH 0x0 and PUSH 0xf at 0001f190 and 0001f286.  Mode 0 is
   fdps_draw_unit_gauge's plain painter, which never reads the strength, so the
   0xf is carried and not used (gauge.h). */
#define UNIT_GAUGE_INTERIOR_WIDTH 0x29
#define ATTACK_GAUGE_MODE_PLAIN 0
#define ATTACK_GAUGE_ALPHA 0xf

/* The member of the resident BaseAni.vfs image that every attack is played
   from, MOV EAX,0x617d0 at 0001f054.  It is looked up straight through
   fdps_vfs_image_get_entry rather than through fdps_baseani_get_entry_or_exit,
   so a miss comes back as a NULL image and not as an exit, and the literal is
   upper-cased IN PLACE by that lookup: it has to live in writable storage
   (rebuild_info/pitfalls.md). */
#define ATTACK_ANIMATION_MEMBER "EasyAni.Saf"

/* 0001ef40.  Four branches before the loop -- the counter test and the two
   side tests -- and three inside it, the attacker's bar guard and the two
   empty-bar guards, so cyclomatic complexity 9 counting the two retrace spins
   and the tick wait.

   THERE IS NO SEPARATE PAGE VARIABLE, and that is what the assembly says: the
   page pointer is slot 0 of the draw request, malloc's answer is stored there
   at 0001f03f and every later use -- the compositor's argument at 0001f0da,
   the two bar pixels at 0001f094 and 0001f0ac, the blit source at 0001f2ff and
   free's argument at 0001f328 -- reads that same slot back.  A local of its
   own would be a second slot and a copy into it.

   THE ATTACKER'S DESTINATION IS FORMED EVEN WHEN THERE IS NO ATTACKER BAR.
   The arithmetic at 0001f088 runs before the loop and reads the pair's y half
   whatever it holds, and on the refusal path that half was never written --
   only the x half is stored.  Nothing dereferences the pointer on that path,
   every use being inside the CMP ...,-0x1 guard, so it is arithmetic on a
   pointer that is never read.  Guarding it changes nothing on screen.

   THE SIZE THE LOOKUP WRITES OUT IS THROWN AWAY.  frame_count is handed to
   fdps_vfs_image_get_entry as the place to put the member's size -- LEA
   EAX,[EBP-0x4] / PUSH EAX at 0001f050 -- and is overwritten at 0001f085 with
   fdps_saf_frame_count of the same image before it is ever read.  It is one
   local in the original and stays one here.

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED, the same contract
   fdps_animate_turn_banner and fdps_play_vfs_animation above and
   fdps_battle_show_combat_gauges (gauge.c) carry.  last_tick is read at
   0001f310 before anything has written it, so the first frame does not wait
   for a tick.  Declaring it initialised from the counter, which is what
   writing the loop cleanly in C invites, costs one extra tick of animation
   (rebuild_info/pitfalls.md).

   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of that wait: nothing inside it writes the counter, so a build
   allowed to hoist the load would spin here forever.  The retrace spins read a
   port and cannot be hoisted for the same reason.

   THE TWO FILLS ARE INLINE EXPANSIONS OF fdps_draw_unit_gauge_proportional and
   are open-coded here for that reason (rebuild_info/build_flags.md): the
   shipped image has no CALL to that function at either of them, so writing
   them as calls would put two CALLs in the rebuild that the original does not
   have.

   FOUR CALLS' ANSWERS ARE READ.  fdps_check_can_counter_attack's is compared
   against 1 at 0001ef6c; the two fdps_get_unit_record pointers at 0001ef96 and
   0001efd2 are the records every field below comes out of, and the second is
   still live at 0001f002 where the request origin is taken off it;
   fdps_vfs_image_get_entry's is the sheet, stored into the request at 0001f068
   and read straight back as fdps_saf_frame_count's argument; malloc's is the
   page.  inp's is tested for bit 3 at both spins.
   fdps_battle_compute_unit_gauge_position, fdps_draw_scene_layers,
   fdps_draw_unit_gauge, fdps_draw_composite_sprite, fdps_blit_rect and free
   all return nothing the original reads.

   The frames are paced by the retrace and by the timer tick, so how many
   instructions stand between them is not observable (contract D). */
void fdps_play_attack_animation(int attacker_unit, int defender_unit)
{
    /* The nine-slot block sprite.h describes.  Slot 0 is the composite page --
       see the note above -- and slot 6 is the frame this pass draws. */
    int request[DRAW_REQUEST_DWORDS];
    /* Where each bar's top-left pixel sits, in view pixels and before the
       page's apron is added.  Both halves of the defender's are always
       written; the attacker's x holds -1 when no counter-attack is coming and
       its y is then never written at all. */
    int defender_gauge_pos[2];
    int attacker_gauge_pos[2];
    /* The record being read: the attacker's first and then the defender's,
       which is the one the request origin is taken off.  One local in the
       original -- both stores are to [EBP-0x30]. */
    struct fdps_unit_record *record;
    /* Those two positions turned into page pixels. */
    unsigned char *attacker_bar_pixel;
    unsigned char *defender_bar_pixel;
    /* Which of the sheet's three graphics fills each bar. */
    int attacker_gfx_index;
    int defender_gfx_index;
    /* The HP pair each bar is filled from, widened from the record's signed
       words by the four MOVSX at 0001ef9c, 0001efa6, 0001efd8 and 0001efe2. */
    int attacker_hp_current;
    int attacker_hp_max;
    int defender_hp_current;
    int defender_hp_max;
    /* How many of the bar's 41 interior columns this frame fills. */
    int attacker_fill_width;
    int defender_fill_width;
    /* How many frames the sheet holds, which is how many passes the loop
       makes.  The lookup's discarded size slot first -- see the note above. */
    unsigned int frame_count;
    /* Which frame of the sheet this pass draws. */
    int frame;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    fdps_battle_compute_unit_gauge_position(defender_gauge_pos, defender_unit);
    if (fdps_check_can_counter_attack(attacker_unit, defender_unit)
            == COUNTER_ATTACK_CONFIRMED) {
        fdps_battle_compute_unit_gauge_position(attacker_gauge_pos,
                                                attacker_unit);
    } else {
        attacker_gauge_pos[0] = ATTACK_GAUGE_NO_BAR;
    }

    record = fdps_get_unit_record(attacker_unit);
    attacker_hp_current = (int) record->hp_current;
    attacker_hp_max = (int) record->hp_max;
    if (record->side == 0) {
        attacker_gfx_index = ATTACK_GAUGE_SIDE_ZERO_GRAPHIC;
    } else {
        attacker_gfx_index = ATTACK_GAUGE_OTHER_SIDE_GRAPHIC;
    }

    record = fdps_get_unit_record(defender_unit);
    defender_hp_current = (int) record->hp_current;
    defender_hp_max = (int) record->hp_max;
    if (record->side == 0) {
        defender_gfx_index = ATTACK_GAUGE_SIDE_ZERO_GRAPHIC;
    } else {
        defender_gfx_index = ATTACK_GAUGE_OTHER_SIDE_GRAPHIC;
    }

    /* Both tile bytes are widened UNSIGNED -- MOV AL,byte ptr [EAX] then AND
       EAX,0xff at 0001f002 and 0001f018 -- so a tile column past 127 is far to
       the right and not far to the left. */
    request[DRAW_REQUEST_X] = (int) record->pos_x * MAP_TILE_SIZE
                              - data_fdps_battle_view_window_origin_x;
    request[DRAW_REQUEST_Y] = (int) record->pos_y * MAP_TILE_SIZE
                              - data_fdps_battle_view_window_origin_y
                              - ATTACK_SPRITE_LIFT;
    request[DRAW_REQUEST_DEST_BASE] = (int) malloc((size_t) ATTACK_PAGE_BYTES);
    request[DRAW_REQUEST_DEST_PITCH] = ATTACK_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = ATTACK_PAGE_ROWS;
    request[DRAW_REQUEST_IMAGE] = (int) fdps_vfs_image_get_entry(
        (struct fdps_vfs_image_header *)
            data_fdps_animation_baseani_archive_ptr,
        ATTACK_ANIMATION_MEMBER, &frame_count);
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;
    frame_count = (unsigned int)
        fdps_saf_frame_count((void *) request[DRAW_REQUEST_IMAGE]);

    attacker_bar_pixel = (unsigned char *) request[DRAW_REQUEST_DEST_BASE]
        + (attacker_gauge_pos[1] + ATTACK_PAGE_BORDER) * ATTACK_PAGE_PITCH
        + attacker_gauge_pos[0] + ATTACK_PAGE_BORDER;
    defender_bar_pixel = (unsigned char *) request[DRAW_REQUEST_DEST_BASE]
        + (defender_gauge_pos[1] + ATTACK_PAGE_BORDER) * ATTACK_PAGE_PITCH
        + defender_gauge_pos[0] + ATTACK_PAGE_BORDER;

    for (frame = 0; frame < (int) frame_count; frame++) {
        request[DRAW_REQUEST_ITEM_INDEX] = frame;
        fdps_draw_scene_layers(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE]);
        if (attacker_gauge_pos[0] != ATTACK_GAUGE_NO_BAR) {
            if (attacker_hp_max <= 0) {
                attacker_fill_width = 0;
            } else {
                attacker_fill_width =
                    (attacker_hp_current * UNIT_GAUGE_INTERIOR_WIDTH
                     + attacker_hp_max - 1) / attacker_hp_max;
            }
            fdps_draw_unit_gauge(attacker_bar_pixel, ATTACK_PAGE_PITCH,
                                 attacker_gfx_index, attacker_fill_width,
                                 ATTACK_GAUGE_MODE_PLAIN, ATTACK_GAUGE_ALPHA);
        }
        if (defender_hp_max <= 0) {
            defender_fill_width = 0;
        } else {
            defender_fill_width =
                (defender_hp_current * UNIT_GAUGE_INTERIOR_WIDTH
                 + defender_hp_max - 1) / defender_hp_max;
        }
        fdps_draw_unit_gauge(defender_bar_pixel, ATTACK_PAGE_PITCH,
                             defender_gfx_index, defender_fill_width,
                             ATTACK_GAUGE_MODE_PLAIN, ATTACK_GAUGE_ALPHA);
        fdps_draw_composite_sprite(request, 1);
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the blit starts clear of it. */
        }
        fdps_blit_rect((unsigned int)
                           ((unsigned char *) request[DRAW_REQUEST_DEST_BASE]
                            + ATTACK_PAGE_WINDOW_AT),
                       ATTACK_PAGE_PITCH,
                       (void *) (VGA_SCREEN_BASE + ATTACK_WINDOW_AT),
                       VGA_SCREEN_PITCH, ATTACK_WINDOW_W, ATTACK_WINDOW_H);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    free((void *) request[DRAW_REQUEST_DEST_BASE]);
}

/* The page every tick of the effect is composed on: the same 360 x 240 with a
   24-pixel apron the blow above composes on, PUSH 0x15180 at 00026e0c with the
   pitch and the row count stored into the draw request at 00026e1c and
   00026e23.  ATTACK_PAGE_* say the same numbers for the blow and are left
   alone; these are this routine's own so that a change to one is not silently
   a change to the other. */
#define EFFECT_PAGE_PITCH 0x168
#define EFFECT_PAGE_ROWS 0xf0
#define EFFECT_PAGE_BYTES 0x15180

/* The window handed to the adapter after every tick: 312 x 192 taken from page
   byte 0x21d8, which is page pixel (24,24), and put down at screen byte 0x504,
   which is screen pixel (4,4).  All six come from the pushes at 00026f63.
   VGA_SCREEN_BASE and VGA_SCREEN_PITCH are the banner's above and mean the same
   here, and 0xa0000 stays a literal because it is where the adapter answers and
   not the address of anything the linker places (rebuild_info/pitfalls.md,
   contract E). */
#define EFFECT_PAGE_WINDOW_AT 0x21d8
#define EFFECT_WINDOW_AT 0x504
#define EFFECT_WINDOW_W 0x138
#define EFFECT_WINDOW_H 0xc0

/* How long one frame is held: CMP dword ptr [EBP-0x8],0x2 / JL at 00026e7f, so
   two timer ticks each, whatever dwell the frame record itself carries. */
#define EFFECT_TICKS_PER_FRAME 2

/* What the request origin carries off the unit's own tile pixel, SUB EAX,0x18
   at 00026eec and SUB EAX,0x1e at 00026f06: one whole tile to the left and
   thirty pixels up.  They are NOT the six-pixel lift a unit sprite gets --
   these clips are drawn from a corner a tile out from the cell they land on. */
#define EFFECT_ORIGIN_LEFT 0x18
#define EFFECT_ORIGIN_UP 0x1e

/* 00026e00.  Three counted loops nested three deep and one branch inside the
   innermost, the sound-flag test, so cyclomatic complexity 8 counting the two
   retrace spins and the tick wait.

   THERE IS NO PAGE VARIABLE AND NO CLIP VARIABLE.  Both pointers live in the
   draw request and nowhere else: malloc's answer is stored into slot 0 at
   00026e19 and read back at 00026e92, 00026f7c and 00026faa, and the loaded
   member is stored into slot 5 at 00026e3c and read back at 00026e4d and
   00026fb6.  A local of its own would be a second slot and a copy into it.

   THE FRAME'S OWN DWELL IS NOT CONSULTED.  Every frame is held for exactly two
   ticks -- the inner counter runs 0 to 1 and nothing reads the duration field
   the .SAF frame record carries -- which is what makes this routine different
   from fdps_saf_play_over_background, and it is the loop bound that says so.

   THE SOUND FLAG IS SET ONCE PER FRAME AND NOT ONCE PER DRAW.  CMP
   [EBP-0x10],0x0 / JNZ and CMP [EBP-0x8],0x0 / JZ at 00026f0c ask for the first
   unit of the first of the frame's two ticks and for nothing else, so a frame
   carrying a sound effect fires it once however many units the clip is drawn
   over.  Handing 1 to every draw plays the effect twice per unit per frame.

   THE TILE BYTES ARE WIDENED UNSIGNED -- MOV AL,byte ptr [EAX] then AND
   EAX,0xff at 00026ed6 and 00026ef2 -- and so is every unit id, at 00026ec6.
   A tile column past 127 is therefore far to the right and not far to the
   left, and a unit id past 127 is a high index and not a negative one.

   NOTHING CULLS A UNIT THAT IS OFF THE VIEW.  The origin is formed from the
   tile and the camera whatever they say, and what keeps an off-screen copy off
   the page is fdps_draw_tilemap_cell's own placement test, one cell at a time
   (sprite.h).

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED, the same contract
   the three routines above carry.  last_tick is read at 00026f8d before
   anything has written it, so the first tick of the animation does not wait.
   Giving it an initialiser -- 0, or the counter -- adds a tick of delay to the
   start of every effect animation (rebuild_info/pitfalls.md).

   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of that wait: nothing inside it writes the counter, so a build
   allowed to hoist the load would spin here forever.  The retrace spins read a
   port and cannot be hoisted for the same reason.

   THREE CALLS' ANSWERS ARE READ.  malloc's is the page and is not tested;
   fdps_vfs_load_entry's is the clip, stored into the request at 00026e3c and
   read straight back as fdps_saf_frame_count's argument at 00026e4d; that
   count is the outer loop's bound; and fdps_get_unit_record's is the record
   both tile bytes come out of.  inp's is tested for bit 3 at both spins.
   fdps_draw_scene_layers, fdps_draw_composite_sprite, fdps_blit_rect and free
   all return nothing the original reads.

   The frames are paced by the retrace and by the timer tick, so how many
   instructions stand between them is not observable (contract D). */
void fdps_play_vfs_animation_over_units(int unit_count, unsigned char *unit_ids,
                                        char *anim_name)
{
    /* The nine-slot block sprite.h describes.  Slot 0 is the composite page and
       slot 5 the loaded clip -- see the note above -- and slot 6 is the frame
       this tick draws. */
    int request[DRAW_REQUEST_DWORDS];
    /* The record the tile position of the copy being drawn comes out of,
       re-resolved for every unit of every tick. */
    struct fdps_unit_record *record;
    /* How many frames the clip holds, which is how many the outer loop
       plays. */
    int frame_count;
    /* Which frame of the clip is on the page. */
    int frame;
    /* Which of that frame's two ticks this pass is. */
    int tick;
    /* Which entry of unit_ids this copy is being drawn over. */
    int unit;
    /* Whether this draw is the one allowed to fire the frame's sound. */
    int play_sound;
    /* The tick the previous pass ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    request[DRAW_REQUEST_DEST_BASE] = (int) malloc((size_t) EFFECT_PAGE_BYTES);
    request[DRAW_REQUEST_DEST_PITCH] = EFFECT_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = EFFECT_PAGE_ROWS;
    request[DRAW_REQUEST_IMAGE] =
        (int) fdps_vfs_load_entry(ANIMATION_ARCHIVE, anim_name);
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;
    frame_count = fdps_saf_frame_count((void *) request[DRAW_REQUEST_IMAGE]);

    for (frame = 0; frame < frame_count; frame++) {
        for (tick = 0; tick < EFFECT_TICKS_PER_FRAME; tick++) {
            fdps_draw_scene_layers(
                (unsigned char *) request[DRAW_REQUEST_DEST_BASE]);
            request[DRAW_REQUEST_ITEM_INDEX] = frame;
            for (unit = 0; unit < unit_count; unit++) {
                record = fdps_get_unit_record((int) unit_ids[unit]);
                request[DRAW_REQUEST_X] = (int) record->pos_x * MAP_TILE_SIZE
                                          - data_fdps_battle_view_window_origin_x
                                          - EFFECT_ORIGIN_LEFT;
                request[DRAW_REQUEST_Y] = (int) record->pos_y * MAP_TILE_SIZE
                                          - data_fdps_battle_view_window_origin_y
                                          - EFFECT_ORIGIN_UP;
                if (unit == 0 && tick == 0) {
                    play_sound = 1;
                } else {
                    play_sound = 0;
                }
                fdps_draw_composite_sprite(request, (char) play_sound);
            }
            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   == 0) {
                /* Spin until the retrace begins, so the tick that has just been
                   composed is the one the monitor shows whole. */
            }
            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   != 0) {
                /* And until it ends, so the blit starts clear of it. */
            }
            fdps_blit_rect((unsigned int)
                               ((unsigned char *) request[DRAW_REQUEST_DEST_BASE]
                                + EFFECT_PAGE_WINDOW_AT),
                           EFFECT_PAGE_PITCH,
                           (void *) (VGA_SCREEN_BASE + EFFECT_WINDOW_AT),
                           VGA_SCREEN_PITCH, EFFECT_WINDOW_W, EFFECT_WINDOW_H);
            while (last_tick == data_fdps_timer_tick_counter) {
            }
            last_tick = data_fdps_timer_tick_counter;
        }
    }

    free((void *) request[DRAW_REQUEST_DEST_BASE]);
    free((void *) request[DRAW_REQUEST_IMAGE]);
}
