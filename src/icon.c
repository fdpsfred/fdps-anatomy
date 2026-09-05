/* icon.c -- the IconAni cut-scene script interpreter and its opcode handlers.
 *
 * See icon.h for what a caller has to know.  No handler here keeps state of
 * its own: the palette a fade runs from is the master palette gamedata.h
 * declares, and the view origin and map cursor the scroll handler moves are
 * the game's own globals, also declared there.
 *
 * inp comes from <conio.h> as an ordinary library call, which is what the
 * original has: 00022410 issues CALL 0003d4e4 rather than an IN instruction.
 * Watcom only turns it into an instruction when __INLINE_FUNCTIONS__ is
 * defined, and the flag that defines it, -oi, is not in this build's set
 * (rebuild_info/build_flags.md).  delay comes from <i86.h>, which is where
 * Watcom 10.0a declares it, and it is a real call in the original too:
 * CALL 0003d370.  abs comes from <stdlib.h> and is likewise a real call in
 * the original -- 00021e6e and 00021e83 both issue CALL 0003d364 rather than
 * the CDQ/XOR/SUB sequence Watcom emits when it expands abs inline.
 */
#include <conio.h>
#include <i86.h>
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "mapdraw.h"
#include "palette.h"
#include "icon.h"

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit anyone here looks at. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* A fade rewrites the whole DAC: entries 0 to 255, inclusive at both ends. */
#define FADE_FIRST_DAC_ENTRY 0
#define FADE_LAST_DAC_ENTRY 0xff

/* How much darker each step gets, and the bound the darkening is stepped up
   against.  The test is < 0x40 with a step of 4, so the amount takes the
   sixteen values 0, 4, ... 60 and the darkest the screen reaches is 60 below
   the master palette rather than the 64 that would extinguish it. */
#define FADE_DARKEN_STEP 4
#define FADE_DARKEN_LIMIT 0x40

/* Where the fade back in starts.  Its counter is loaded with 0x40 and the loop
   runs while the counter is still >= 0, so the darkening takes the seventeen
   values 64, 60, ... 4, 0 -- one step more than the fade out's sixteen, and the
   only place in the pair where a bias of 64 (which extinguishes every
   component) or a bias of 0 (which uploads the master palette untouched) is
   ever reached. */
#define FADE_IN_FIRST_DARKENING 0x40

/* 00022410.  The loop is CMP dword ptr [EBP-4],0x40 / JL with ADD 0x4 at the
   bottom, so the counter is signed and counts up while the bias handed to the
   upload is its negation -- NEG EAX, three times, once per channel.  A counter
   made unsigned turns that negation into a bias near four billion, which the
   upload's signed clamp reads as brighter than white.

   The retrace wait comes first in every step, before the upload and before
   the delay, and it is what makes the fade advance one displayed frame at a
   time; the delay then holds that frame for the operand's milliseconds.  The
   two together are the fade's speed, and dropping either changes it
   (rebuild_info/pitfalls.md).

   The upload's source is data_fdps_vga_main_palette_ptr on every step, not
   the DAC contents from the step before, so the ramp cannot accumulate error
   and cannot inherit a palette some other scene installed. */
void fdps_icon_script_fade_to_black(int step_delay_ms)
{
    int darken_amount;

    for (darken_amount = 0; darken_amount < FADE_DARKEN_LIMIT;
         darken_amount += FADE_DARKEN_STEP) {
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so each step of the ramp lands
               on its own displayed frame. */
        }

        fdps_set_palette_range(
            (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
            FADE_FIRST_DAC_ENTRY, FADE_LAST_DAC_ENTRY,
            -darken_amount, -darken_amount, -darken_amount);

        delay((unsigned int) step_delay_ms);
    }
}

/* 00022480.  The mirror image of the handler above in shape but not in extent.
   The loop is MOV dword ptr [EBP-4],0x40 / CMP against 0x0 with JGE and ADD
   -0x4 at the bottom, so the counter starts at 64, counts down, and the body
   still runs on the pass where the counter has reached 0.  The three NEG EAX
   hand its negation to the upload as all three channel biases, exactly as the
   fade out does, so the biases are -64, -60, ... -4, 0.

   The counter is signed -- JGE, not JAE -- and it has to be, because the loop's
   exit depends on the counter going below zero being recognised as below zero.

   Both ends of that range matter and neither is what a mirror of the sibling
   would produce.  The first step's -64 puts every component at 0 through the
   upload's clamp, so the ramp starts from true black whatever the DAC held
   when the opcode was reached; the last step's 0 uploads the master palette
   with nothing subtracted, so the handler leaves the DAC holding exactly the
   master palette.  Copying the sibling's bound -- running while the counter is
   > 0, or starting it at 60 -- drops that last step and leaves the screen
   permanently four short of full brightness (rebuild_info/pitfalls.md).

   The retrace wait, the upload and the delay are in the same order and the same
   one-of-each-per-step arrangement as the fade out, and pace the ramp the same
   way. */
void fdps_icon_script_fade_in(int step_delay_ms)
{
    int darken_amount;

    for (darken_amount = FADE_IN_FIRST_DARKENING; darken_amount >= 0;
         darken_amount -= FADE_DARKEN_STEP) {
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so each step of the ramp lands
               on its own displayed frame. */
        }

        fdps_set_palette_range(
            (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
            FADE_FIRST_DAC_ENTRY, FADE_LAST_DAC_ENTRY,
            -darken_amount, -darken_amount, -darken_amount);

        delay((unsigned int) step_delay_ms);
    }
}

/* The map is drawn on 24-pixel tiles, so a tile number becomes map pixels by
   multiplying by 0x18 -- the IMUL EAX,EAX,0x18 the operands go through -- and
   a pixel distance becomes a tile distance by dividing by the same. */
#define MAP_TILE_PIXELS 0x18

/* 00021e30.  Script opcode 5, the scripted view scroll.  See icon.h for what
   the operands are and what the handler leaves behind.

   THE STEP COUNT IS THE DOMINANT AXIS.  Both axis distances go through abs and
   the larger of the two is divided by the tile size, so the scroll takes as
   many frames as the longer axis takes tiles.  The comparison is a signed
   CMP/JLE over two values abs has already made non-negative, and the division
   is IDIV with the dividend sign-extended by SAR EDX,0x1f, so it is signed
   division throughout.

   THE INCREMENTS ARE COMPUTED ONCE.  The original divides each axis delta by
   the step count before the loop is entered, into [EBP-0xc] and [EBP-0x8], and
   the loop then adds those two constants.  The deltas are taken from the view
   origin as it stood on entry, so a per-iteration recomputation would not give
   the same numbers -- the origin it would divide from has already moved.

   THE MINOR AXIS IS TRUNCATED, AND THE END ABSORBS IT.  step_x and step_y are
   integer quotients, so over step_count frames the minor axis arrives short of
   the target by the remainder.  The two writes after the loop put the exact
   target pixels into the origin, which closes that gap in one jump on the last
   frame.  Rounding the increments instead -- the obvious repair -- changes what
   is on screen during the scroll (rebuild_info/pitfalls.md).

   A SUB-TILE MOVE IS NOT A SCROLL.  When the dominant distance is under one
   tile the quotient is zero, the loop body never runs, and the view is simply
   placed at the target: no frame is rendered and nothing is animated.

   The loop's only call is fdps_render_view_frame, which composes a frame and
   holds until the timer tick moves, so the step rate is the game's frame rate
   and not the CPU's speed. */
int fdps_icon_script_scroll_view_to_tile(unsigned char *script, int offset)
{
    /* The tile the script names, converted to map pixels. */
    int target_x;
    int target_y;
    /* How far the view has to travel on each axis, in pixels, unsigned. */
    int travel_x;
    int travel_y;
    /* Frames the scroll takes, and the per-frame movement on each axis. */
    int step_count;
    int step_x;
    int step_y;
    int step;

    target_x = (int) script[offset + 1] * MAP_TILE_PIXELS;
    target_y = (int) script[offset + 2] * MAP_TILE_PIXELS;

    travel_x = abs(target_x - data_fdps_battle_view_window_origin_x);
    travel_y = abs(target_y - data_fdps_battle_view_window_origin_y);

    if (travel_x > travel_y) {
        step_count = travel_x / MAP_TILE_PIXELS;
    } else {
        step_count = travel_y / MAP_TILE_PIXELS;
    }

    if (step_count != 0) {
        step_x = (target_x - data_fdps_battle_view_window_origin_x)
                 / step_count;
        step_y = (target_y - data_fdps_battle_view_window_origin_y)
                 / step_count;

        for (step = 0; step < step_count; step++) {
            data_fdps_battle_view_window_origin_x += step_x;
            data_fdps_battle_view_window_origin_y += step_y;
            fdps_render_view_frame();
        }
    }

    data_fdps_map_cursor_world_x = target_x + MAP_TILE_PIXELS;
    data_fdps_map_cursor_world_y = target_y + MAP_TILE_PIXELS;
    data_fdps_battle_view_window_origin_x = target_x;
    data_fdps_battle_view_window_origin_y = target_y;

    return offset + 3;
}
