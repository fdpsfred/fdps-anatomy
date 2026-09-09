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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "cdaudio.h"
#include "chapter.h"
#include "deploy.h"
#include "keybd.h"
#include "mapai.h"
#include "mapdraw.h"
#include "maptile.h"
#include "msgwin.h"
#include "palcycle.h"
#include "palette.h"
#include "roster.h"
#include "saf.h"
#include "text.h"
#include "unit.h"
#include "unitstat.h"
#include "vfs.h"
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

/* How many walk sub-steps carry a unit across one tile.  The loop counter runs
   1..6 -- MOV [EBP-0x18],0x1 with CMP against 0x6 and JLE -- and
   fdps_draw_map_unit offsets a unit by four pixels per sub-step, so five
   drawn offsets of 4, 8, 12, 16, 20 and then the tile itself at 24 make one
   0x18-pixel tile of travel. */
#define WALK_SUB_STEPS 6

/* The facing codes the commit branches on, the same four fdps_draw_map_unit
   reads: the assembly tests the facing byte against 0, then 1, then 2, and
   every other value falls into the last arm. */
#define WALK_FACING_DOWN 0
#define WALK_FACING_LEFT 1
#define WALK_FACING_UP 2

/* 00021f60.  Script opcode 1, the scripted group walk.  See icon.h for the
   operand layout and what the handler leaves behind.

   THE CURSOR AND THE PANEL ARE SWITCHED OFF AND THEN SWITCHED ON, NOT PUT
   BACK.  The entry pair writes 0 into both globals and the exit pair writes
   the literal 1 into both -- MOV dword ptr [0x00069cd0],0x1 and MOV byte ptr
   [0x00060159],0x1 at 000220d1 -- so whatever cursor mode was in force before
   the opcode does not survive it.  Saving the two on entry and restoring them
   on exit, the obvious spelling, is a different function
   (rebuild_info/pitfalls.md).

   THE UNIT UPDATE LIVES INSIDE THE PER-FRAME HOLD LOOP.  The list is walked
   once per held frame, not once per sub-step: the hold counter [EBP-0xc] is
   the loop the CALL 0x0002beb0 sits at the bottom of, and the unit loop
   [EBP-0x10] is nested inside it.  With a frames-per-sub-step operand of 0
   the hold loop never runs, so nothing is drawn AND nothing moves.  Hoisting
   the unit work out to run once per sub-step would move the units on a zero
   operand, which the original does not.

   THE TILE IS COMMITTED ONCE PER TILE, ON THE FIRST HELD FRAME OF SUB-STEP 6.
   CMP dword ptr [EBP-0x18],0x6 / JZ picks the commit arm and CMP dword ptr
   [EBP-0xc],0x0 / JNZ lets only the first held frame through it, so a
   frames-per-sub-step of 3 still advances the unit by exactly one tile.  The
   sub-step counter written to the record is the loop counter itself for
   sub-steps 1 to 5 and is cleared to 0 by the commit, which is what puts the
   unit on its new tile with no sub-tile offset left over.

   THE FACING IS TAKEN FROM THE SCRIPT ON EVERY PASS AND THE COMMIT READS IT
   BACK.  MOV DL,byte ptr [EAX+0x1] / MOV byte ptr [EAX+0x3],DL runs before
   the commit's own MOV AL,byte ptr [EAX+0x3], so the direction the tile is
   committed in is the script's operand and never the facing the unit carried
   in.

   The three operand bytes are zero-extended -- XOR EAX,EAX / MOV AL,byte ptr
   [EDX+n] for each -- so all three counts are 0..255.

   Nothing here scrolls the view, consults the terrain or reports an arrival
   tile: the listed units walk straight through whatever is on the map for
   exactly the scripted number of tiles. */
int fdps_icon_script_walk_units(unsigned char *script, int offset)
{
    /* The three operand bytes: how many rendered frames each walk sub-step is
       held for, how many tiles the group walks, and how many units are
       listed. */
    int frames_per_sub_step;
    int tile_count;
    int unit_count;
    /* The three loop counters, outermost first, plus the position in the
       unit list. */
    int tile;
    int sub_step;
    int hold;
    int listed;
    /* The listed unit's index in the battle unit array, and its record. */
    int unit_index;
    struct fdps_unit_record *unit;

    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_play_active_flag = 0;

    frames_per_sub_step = (int) script[offset + 1];
    tile_count = (int) script[offset + 2];
    unit_count = (int) script[offset + 3];
    offset += 4;

    for (tile = 0; tile < tile_count; tile++) {
        for (sub_step = 1; sub_step <= WALK_SUB_STEPS; sub_step++) {
            for (hold = 0; hold < frames_per_sub_step; hold++) {
                for (listed = 0; listed < unit_count; listed++) {
                    unit_index = (int) script[offset + listed * 2];
                    unit = fdps_get_unit_record(unit_index);
                    unit->facing = script[offset + listed * 2 + 1];

                    if (sub_step == WALK_SUB_STEPS) {
                        if (hold == 0) {
                            unit->walk_step = 0;
                            if (unit->facing == WALK_FACING_DOWN) {
                                unit->pos_y++;
                            } else if (unit->facing == WALK_FACING_LEFT) {
                                unit->pos_x--;
                            } else if (unit->facing == WALK_FACING_UP) {
                                unit->pos_y--;
                            } else {
                                unit->pos_x++;
                            }
                        }
                    } else {
                        unit->walk_step = (unsigned char) sub_step;
                    }
                }
                fdps_render_view_frame();
                fdps_cycle_scene_palette();
            }
        }
    }

    data_fdps_map_cursor_draw_mode = 1;
    data_fdps_ui_play_active_flag = 1;

    return offset + unit_count * 2;
}

/* 00022100.  Opcode 2: turn a list of units, then hold the view on them.
   Two operand bytes, both zero-extended -- XOR EAX,EAX / MOV AL,byte ptr
   [EDX+0x1] and the same at [EDX+0x2] -- so the hold length and the unit count
   are 0..255 and never negative.  ADD dword ptr [EBP+0x18],0x3 skips the
   opcode byte and those two operands, and every list access is taken from the
   advanced offset.

   THE TURNING LOOP AND THE HOLD LOOP ARE SIBLINGS, NOT NESTED.  The unit loop
   at 00022144 closes at 0002218d and the hold loop at 00022196 starts after
   it, each with its own CMP against its own operand.  So every listed unit is
   turned before the first frame is drawn -- the group turns together on one
   displayed frame -- and a hold count of 0 still turns them all.  Nesting the
   turn inside the hold, the way the sibling walk handler nests its unit loop,
   would make a hold of 0 a no-op and would rewrite the facings once per frame.

   THE EXIT WRITES CONSTANTS, IT DOES NOT PUT ANYTHING BACK.  MOV dword ptr
   [0x00069cd0],0x1 and MOV byte ptr [0x00060159],0x1 at 000221af are stores of
   literals; the entry pair stored 0 into the same two and nothing saved what
   was there.  Saving on entry and restoring on exit, the obvious spelling of
   "hide the HUD for the duration", is a different function whenever the cursor
   mode was not 1 going in (rebuild_info/pitfalls.md).

   MOV DL,byte ptr [EAX+0x1] / MOV byte ptr [EAX+0x3],DL: the pair's second
   byte goes into the record's facing field as it stands.  Nothing masks it,
   compares it or maps it, so a facing code outside 0..3 is stored too, and it
   is fdps_draw_map_unit that later decides what such a value looks like.

   The hold loop's only call is fdps_render_view_frame, which composes a frame
   and holds until the timer tick moves, so the pose's on-screen length is a
   count of the game's frames and not of CPU time. */
int fdps_icon_script_set_unit_facing(unsigned char *script, int offset)
{
    /* The two operand bytes: how many rendered frames the pose is held for,
       and how many units are listed. */
    int hold_frames;
    int unit_count;
    /* Position in the unit list, and the held-frame counter. */
    int listed;
    int held;
    /* The listed unit's index in the battle unit array, and its record. */
    int unit_index;
    struct fdps_unit_record *unit;

    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_play_active_flag = 0;

    hold_frames = (int) script[offset + 1];
    unit_count = (int) script[offset + 2];
    offset += 3;

    for (listed = 0; listed < unit_count; listed++) {
        unit_index = (int) script[offset + listed * 2];
        unit = fdps_get_unit_record(unit_index);
        unit->facing = script[offset + listed * 2 + 1];
    }

    for (held = 0; held < hold_frames; held++) {
        fdps_render_view_frame();
    }

    data_fdps_map_cursor_draw_mode = 1;
    data_fdps_ui_play_active_flag = 1;

    return offset + unit_count * 2;
}

/* The phase counter runs 1, 2, ... 7 -- MOV dword ptr [EBP-0x10],0x1 with CMP
   against 0x8 and JL -- so the flash has seven phases and the last of them is
   odd.  Both ends of that range are behaviour: starting at 0 or stopping at 8
   would leave the units visible, because it is the phase's low bit that goes
   into the record. */
#define BLINK_FIRST_PHASE 1
#define BLINK_PHASE_LIMIT 8

/* How long one phase is held for: CMP dword ptr [EBP-0x18],0x3 / JL around the
   bare CALL 0x0002beb0, so three rendered frames per phase and twenty-one for
   the whole effect.  Nothing in the script can change it. */
#define BLINK_FRAMES_PER_PHASE 3

/* 000221e0.  Script opcode 9, the scripted retire-with-a-flash.  See icon.h
   for the operand layout and what the handler leaves behind.

   THE LIST IS ONE BYTE PER UNIT.  MOV EAX,[EBP+0x18] / ADD EAX,[EBP-0x18] /
   MOV EDX,[EBP+0x14] / ADD EDX,EAX / MOV AL,byte ptr [EDX]: the list index is
   added to the offset unscaled, where the walk and turn handlers above scale
   theirs by two.  So the operand header is two bytes and the list that follows
   is unit_count bytes, and the returned offset is offset + 2 + unit_count.

   THE FLAGS BYTE IS ASSIGNED WHOLE.  MOV AL,byte ptr [EBP-0x10] / AND AL,0x1 /
   MOV byte ptr [EDX+0x5],AL is a store of the phase's low bit over the entire
   byte, not a read-modify-write of bit 0.  Every other flag the record carried
   is cleared with it, including the acted-this-turn bit; spelling this as
   `unit->flags |= 1` and `unit->flags &= ~1`, which is what the single-unit
   retire and un-retire opcodes in fdps_icon_script_run do, preserves those
   flags where the original destroys them, so a unit flashed out here and later
   brought back comes back able to act (rebuild_info/pitfalls.md).

   THE LAST PHASE IS ODD, AND THAT IS THE POINT.  Phase 7 writes 1, which is
   the retired flag set, so the effect deliberately ends with the listed units
   off the map rather than back where it found them.  Treating it as a
   symmetric blink and restoring the units afterwards leaves scripted units
   standing that the original removed (rebuild_info/pitfalls.md).

   THE COUNT AND THE INDICES ARE UNSIGNED.  XOR EAX,EAX / MOV AL,byte ptr
   [EDX+0x1] for the count and XOR EAX,EAX / MOV AL,byte ptr [EDX] for each
   index, so both are 0..255 and a top-bit-set count is 128 units rather than a
   list walked backwards.

   The unit loop and the frame loop are siblings inside the phase loop, so
   every listed unit changes state on the same displayed frame; and the frame
   loop's only call is fdps_render_view_frame, which holds until the timer tick
   moves, so the flash's speed is the game's frame rate and not the CPU's. */
int fdps_icon_script_blink_units_out(unsigned char *script, int offset)
{
    /* The one operand byte: how many unit indices follow it. */
    int unit_count;
    /* The flash's phase counter, whose low bit is what reaches the record. */
    int phase;
    /* Position in the unit list, and the held-frame counter. */
    int listed;
    int held;
    /* The listed unit's index in the battle unit array, and its record. */
    int unit_index;
    struct fdps_unit_record *unit;

    unit_count = (int) script[offset + 1];
    offset += 2;

    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_play_active_flag = 0;

    for (phase = BLINK_FIRST_PHASE; phase < BLINK_PHASE_LIMIT; phase++) {
        for (listed = 0; listed < unit_count; listed++) {
            unit_index = (int) script[offset + listed];
            unit = fdps_get_unit_record(unit_index);
            unit->flags = (unsigned char) (phase & 1);
        }

        for (held = 0; held < BLINK_FRAMES_PER_PHASE; held++) {
            fdps_render_view_frame();
        }
    }

    data_fdps_map_cursor_draw_mode = 1;
    data_fdps_ui_play_active_flag = 1;

    return offset + unit_count;
}

/* The value an operand byte has to exceed to be a negative displacement, and
   what is taken off it when it does.  CMP dword ptr [EBP-0x10],0x7f / JLE
   round ADD dword ptr [EBP-0x10],0xffffff00, so the byte is loaded unsigned
   and sign-extended by hand rather than being loaded as a signed char. */
#define VIEW_OFFSET_SIGN_THRESHOLD 0x7f
#define VIEW_OFFSET_SIGN_BIAS 0x100

/* 000224f0.  Script opcode 0x10, the scripted view shake.  See icon.h for the
   operand layout and what the handler leaves behind.

   EACH STEP'S PAIR IS MEASURED FROM THE ORIGIN SAVED ON ENTRY, NOT FROM THE
   PREVIOUS STEP.  MOV EAX,[EBP-0x28] / ADD EAX,[EBP-0x10] / MOV [0x00069ce4],
   EAX reloads the saved x every time round the loop and adds this step's
   displacement to it; the same shape at 0002259d does the y.  Writing it as
   `data_fdps_battle_view_window_origin_x += offset_x`, which is what "shake
   the view by these amounts" reads like, turns ICON06.DAT's ramp of
   -6, -12, -18, -24 into -6, -18, -36, -60 (rebuild_info/pitfalls.md).

   THE DISPLAY FLAG IS PUT BACK, NOT FORCED TO 1.  MOV AL,byte ptr [EBP-0x8] /
   MOV [0x00060159],AL restores whatever the flag held on entry, where the
   walk, turn and blink handlers above all end with MOV [0x00060159],0x1.  This
   handler also never touches data_fdps_map_cursor_draw_mode at all.  Writing
   the family uniformly makes the info panel reappear after a cut-scene that
   had deliberately hidden it (rebuild_info/pitfalls.md).

   THE HEADER OPERANDS ARE UNSIGNED AND THE PAIRS ARE SIGNED.  XOR EAX,EAX /
   MOV AL,byte ptr [EDX+0x1] and [EDX+0x2] load the frame count and the step
   count as 0..255, while each displacement byte is loaded the same way and
   then folded down by 0x100 when it exceeds 0x7f, so a pair byte spans
   -128..127.

   A frame count of 0 holds nothing: the inner loop's CMP dword ptr
   [EBP-0x14],[EBP-0x20] / JL runs zero times, so the origin is written and
   overwritten with nothing presented in between. */
int fdps_icon_script_animate_view_offset(unsigned char *script, int offset)
{
    /* The view origin the effect displaces from and puts back. */
    int saved_origin_x;
    int saved_origin_y;
    /* The info-panel flag on entry, held in a dword and put back as a byte. */
    int saved_play_flag;
    /* The two header operands: how many rendered frames each step is held for
       and how many displacement pairs follow. */
    int frames_per_step;
    int step_count;
    /* Position in the pair list, and the held-frame counter. */
    int step;
    int held;
    /* This step's displacement from the saved origin, in pixels. */
    int offset_x;
    int offset_y;

    saved_origin_x = data_fdps_battle_view_window_origin_x;
    saved_origin_y = data_fdps_battle_view_window_origin_y;

    frames_per_step = (int) script[offset + 1];
    step_count = (int) script[offset + 2];
    offset += 3;

    saved_play_flag = (int) data_fdps_ui_play_active_flag;
    data_fdps_ui_play_active_flag = 0;

    for (step = 0; step < step_count; step++) {
        offset_x = (int) script[offset];
        if (offset_x > VIEW_OFFSET_SIGN_THRESHOLD) {
            offset_x -= VIEW_OFFSET_SIGN_BIAS;
        }
        offset_y = (int) script[offset + 1];
        if (offset_y > VIEW_OFFSET_SIGN_THRESHOLD) {
            offset_y -= VIEW_OFFSET_SIGN_BIAS;
        }
        offset += 2;

        data_fdps_battle_view_window_origin_x = saved_origin_x + offset_x;
        data_fdps_battle_view_window_origin_y = saved_origin_y + offset_y;

        for (held = 0; held < frames_per_step; held++) {
            fdps_render_view_frame();
        }
    }

    data_fdps_battle_view_window_origin_x = saved_origin_x;
    data_fdps_battle_view_window_origin_y = saved_origin_y;
    data_fdps_ui_play_active_flag = (unsigned char) saved_play_flag;

    return offset;
}

/* The message panel's speaker: FACE.CEL record 122, pushed at 00022613 and
   again at 00022682, so both questions are asked under the same portrait. */
#define CHOICE_SPEAKER_FACE 0x7a

/* The chapter text block's entries, in the order the handler draws them:
   PUSH 0x10, 0x11, 0x13, 0x14, 0x15 and 0x12 at 00022630, 00022666, 0002269f,
   000226d5, 00022701 and 00022724. */
#define CHOICE_FIRST_QUESTION_TEXT_ID 0x10
#define CHOICE_FIRST_TAKEN_TEXT_ID 0x11
#define CHOICE_CLOSING_TEXT_ID 0x12
#define CHOICE_SECOND_QUESTION_TEXT_ID 0x13
#define CHOICE_SECOND_TAKEN_TEXT_ID 0x14
#define CHOICE_THIRD_TAKEN_TEXT_ID 0x15

/* The two places the text goes.  0xa0000 is the mode 13h aperture and 0x140
   its row stride, and 0xaa44a is the pen the message panel's own text sits at,
   screen (138, 131) -- the same origin fdps_draw_text puts the pen back to on
   a page break (text.c).  All three are hard-coded in the original, PUSH
   0xa0000 at 00022661, PUSH 0xaa44a at 0002262b and PUSH 0x140 at 00022626,
   and stay literals here: 0xa0000 is where the display adapter answers and not
   the address of anything the linker places (rebuild_info/pitfalls.md). */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define PANEL_TEXT_ORIGIN 0x000aa44a

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d in front of
   every one of the six draws: foreground, no background fill, and the shadow
   the outline colour becomes while the font's outline flag is clear. */
#define CHOICE_TEXT_FG_COLOR 0xd0
#define CHOICE_TEXT_BG_COLOR 0
#define CHOICE_TEXT_OUTLINE_COLOR 0x6d

/* What the player is answering with.  fdps_prompt_two_choice's 0 is the left
   option and the only value either test here matches; its 1 and its -1 both
   fall into the else arm. */
#define CHOICE_ANSWER_LEFT 0

/* The three branches, and the order they are settled in: the third is the
   value the frame slot is preloaded with at 0002260c and the one that survives
   when neither test matches. */
#define CHOICE_FIRST_BRANCH 1
#define CHOICE_SECOND_BRANCH 2
#define CHOICE_THIRD_BRANCH 3

/* 00022600.  Straight-line code with one two-armed test nested inside the else
   arm of another: CMP dword ptr [EBP + -0x8],0x0 / JNZ at 0002264d and again
   at 000226bc, both against the value the CALL to fdps_prompt_two_choice left
   in EAX and the prologue's frame slot took at 00022645 and 000226b4.  Both
   arms of both tests join at 00022711, where the closing line is drawn, so
   that draw is on every path and there is no early return.

   THE ANSWER IS PRELOADED WITH THE THIRD BRANCH AND ONLY EVER OVERWRITTEN.
   MOV dword ptr [EBP + -0xc],0x3 at 0002260c runs before the first question is
   asked, and the innermost else arm at 000226ee writes nothing, so the third
   branch is what a player who takes the right option twice gets by default.

   THE SECOND QUESTION IS ASKED UNDER A PANEL OF ITS OWN.  The first one's
   panel is retracted before the answer is looked at -- CALL 00020820 at
   00022648 sits between the prompt and the test -- and the else arm reveals a
   fresh one at 00022684 rather than drawing into the panel that is already up.

   Only the prompt's answer is used after a CALL.  fdps_draw_text's return, the
   pen one glyph past the last one drawn, is dropped at all six call sites: the
   ADD ESP,0x1c that follows each one is the argument cleanup and EAX is dead
   from there. */
int fdps_icon_script_prompt_three_way_choice(void)
{
    /* What the player picked, 1, 2 or 3, and the value handed back.  The
       interpreter keeps it and uses it as the deployment record's match key
       (icon.h). */
    int chosen_branch;
    /* The answer to the question just asked: 0 the left option, 1 the right,
       -1 a cancel.  The same frame slot carries both questions' answers. */
    int answer;

    chosen_branch = CHOICE_THIRD_BRANCH;

    fdps_message_window_open(CHOICE_SPEAKER_FACE);
    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                   CHOICE_FIRST_QUESTION_TEXT_ID,
                   (unsigned char *) PANEL_TEXT_ORIGIN, VGA_SCREEN_PITCH,
                   CHOICE_TEXT_FG_COLOR, CHOICE_TEXT_BG_COLOR,
                   CHOICE_TEXT_OUTLINE_COLOR);
    answer = fdps_prompt_two_choice();
    fdps_message_window_close();

    if (answer == CHOICE_ANSWER_LEFT) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CHOICE_FIRST_TAKEN_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       CHOICE_TEXT_FG_COLOR, CHOICE_TEXT_BG_COLOR,
                       CHOICE_TEXT_OUTLINE_COLOR);
        chosen_branch = CHOICE_FIRST_BRANCH;
    } else {
        fdps_message_window_open(CHOICE_SPEAKER_FACE);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CHOICE_SECOND_QUESTION_TEXT_ID,
                       (unsigned char *) PANEL_TEXT_ORIGIN, VGA_SCREEN_PITCH,
                       CHOICE_TEXT_FG_COLOR, CHOICE_TEXT_BG_COLOR,
                       CHOICE_TEXT_OUTLINE_COLOR);
        answer = fdps_prompt_two_choice();
        fdps_message_window_close();

        if (answer == CHOICE_ANSWER_LEFT) {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHOICE_SECOND_TAKEN_TEXT_ID,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, CHOICE_TEXT_FG_COLOR,
                           CHOICE_TEXT_BG_COLOR, CHOICE_TEXT_OUTLINE_COLOR);
            chosen_branch = CHOICE_SECOND_BRANCH;
        } else {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHOICE_THIRD_TAKEN_TEXT_ID,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, CHOICE_TEXT_FG_COLOR,
                           CHOICE_TEXT_BG_COLOR, CHOICE_TEXT_OUTLINE_COLOR);
        }
    }

    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                   CHOICE_CLOSING_TEXT_ID, (unsigned char *) VGA_SCREEN_BASE,
                   VGA_SCREEN_PITCH, CHOICE_TEXT_FG_COLOR,
                   CHOICE_TEXT_BG_COLOR, CHOICE_TEXT_OUTLINE_COLOR);

    return chosen_branch;
}

/* The container every cut-scene script lives in.  The interpreter holds the
   name itself -- all 67 call sites push nothing but the member name -- so a
   caller cannot point it at another archive. */
#define SCRIPT_ARCHIVE_NAME "IconAni.vfs"

/* The diagnostic the archive's own failure prints.  It is followed by a wait
   for a key and then by the load attempt on the null handle, so it reports the
   failure without avoiding it. */
#define SCRIPT_ARCHIVE_MISSING_MESSAGE "file not found: '%s'\n"

/* The two member names an opcode composes out of its operand byte, and the
   frame buffer they are composed into.  Forty bytes is the slot the original
   reserves for it (EBP-0x58 up to EBP-0x30); the longest name either format
   can produce is twelve characters and a terminator. */
#define SCRIPT_SAF_MEMBER_FORMAT "Icon%04d.saf"
#define SCRIPT_WAV_MEMBER_FORMAT "Icon%04d.wav"
#define SCRIPT_MEMBER_NAME_BYTES 40

/* How many .wav buffers one script can hold open at once.  The table is a
   frame array of twenty pointers and the counter is compared against 20 with
   JGE, so the twenty-first sound of a script is not loaded and does not play
   (rebuild_info/pitfalls.md). */
#define SCRIPT_WAV_SLOTS 20

/* What opcode 8 asks fdps_audio_start_wav for: play the sample once, and take
   the rate and the volume from the .wav's own header rather than overriding
   them.  Both -1s are PUSH -0x1 in the original. */
#define SCRIPT_WAV_LOOP_ONCE 1
#define SCRIPT_WAV_RATE_FROM_HEADER (-1)
#define SCRIPT_WAV_VOLUME_FROM_HEADER (-1)

/* Opcode 7's operand and what it turns into.  0xff is the script's way of
   asking for silence and the only operand the handler rewrites; every other
   value is handed to the music layer as it stands. */
#define SCRIPT_MUSIC_SILENCE_OPERAND 0xff
#define SCRIPT_MUSIC_SILENCE (-1)

/* Opcode 4's first operand when the wave number is not a literal but the
   branch the player picked at opcode 0x63. */
#define SCRIPT_WAVE_FROM_CHOICE 0xff

/* Opcode 3's colours: the standard message colours, the same three the choice
   prompt above draws its lines in.  They are constants in the opcode, not
   operands -- a script cannot ask for another colour. */
#define SCRIPT_TEXT_FG_COLOR 0xd0
#define SCRIPT_TEXT_BG_COLOR 0
#define SCRIPT_TEXT_OUTLINE_COLOR 0x6d

/* Opcode 0x15 rewrites the whole DAC, entries 0 to 255 inclusive, exactly as
   the two fade handlers do; only the three biases come out of the script. */
#define SCRIPT_PALETTE_FIRST_ENTRY 0
#define SCRIPT_PALETTE_LAST_ENTRY 0xff

/* Bit 0 of a unit's flags byte: the unit has left the battle.  Opcode 0x0b
   sets it and opcode 0x0c clears it, both leaving the other seven bits alone,
   which is what separates them from the blink handler's whole-byte assign. */
#define SCRIPT_UNIT_RETIRED_FLAG 1

/* Opcode 0x0c zeroes the six status timer bytes, the whole of
   struct fdps_unit_record's status_timers. */
#define SCRIPT_UNIT_STATUS_TIMER_BYTES 6

/* Opcode 0x12 writes record byte 0x25 + operand, and 0x25 is status_timers[3]
   -- the array starts at 0x22.  The operand is not ranged against the array,
   so a large one writes past it; that is the original's behaviour and the same
   opcode also skips the unit-index bounds check its neighbours make
   (rebuild_info/pitfalls.md). */
#define SCRIPT_UNIT_TIMER_FIRST_INDEX 3

/* The tile layer opcode 0x14 writes into: an 0xb-byte header followed by the
   cells, two bytes each, with the row stride in the signed word at +7.  The
   same layout fdps_map_apply_triggered_cell_changes walks (src/maptile.h). */
#define SCRIPT_TILE_LAYER_HEADER_BYTES 0xb
#define SCRIPT_TILE_LAYER_STRIDE_OFFSET 7

/* Opcode 0x61's debug award: ten rounds of 99 experience handed to unit 3.
   The award global is re-armed inside the loop, once per round, because the
   award routine consumes it. */
#define SCRIPT_XP_AWARD_ROUNDS 10
#define SCRIPT_XP_AWARD_POINTS 99
#define SCRIPT_XP_AWARD_UNIT 3

/* The opcodes, in the order the compare ladder settles them.  Opcode 0 ends
   the script and so does every value not listed here. */
#define SCRIPT_OP_END 0x00
#define SCRIPT_OP_WALK_UNITS 0x01
#define SCRIPT_OP_FACE_UNITS 0x02
#define SCRIPT_OP_DRAW_TEXT 0x03
#define SCRIPT_OP_DEPLOY_WAVE 0x04
#define SCRIPT_OP_SCROLL_VIEW 0x05
#define SCRIPT_OP_PLAY_SAF 0x06
#define SCRIPT_OP_SET_MUSIC 0x07
#define SCRIPT_OP_PLAY_WAV 0x08
#define SCRIPT_OP_BLINK_UNITS_OUT 0x09
#define SCRIPT_OP_PLACE_UNIT 0x0a
#define SCRIPT_OP_RETIRE_UNIT 0x0b
#define SCRIPT_OP_REVIVE_UNIT 0x0c
#define SCRIPT_OP_SET_VIEW_TILE 0x0d
#define SCRIPT_OP_FADE_OUT 0x0e
#define SCRIPT_OP_FADE_IN 0x0f
#define SCRIPT_OP_SHAKE_VIEW 0x10
#define SCRIPT_OP_SWITCH_MAP 0x11
#define SCRIPT_OP_SET_UNIT_TIMER 0x12
#define SCRIPT_OP_TRIGGER_CELL_EVENT 0x13
#define SCRIPT_OP_SET_MAP_CELL 0x14
#define SCRIPT_OP_BIAS_PALETTE 0x15
#define SCRIPT_OP_DEBUG_LEVEL_UNIT_3 0x61
#define SCRIPT_OP_ACTOR_BEHAVIOR_STEP 0x62
#define SCRIPT_OP_ASK_THREE_WAY 0x63

/* 00021650.  Loads one script member out of IconAni.vfs and runs its byte
   opcodes until one of them stops it.  This is what every scripted cut-scene
   in the game is: a chapter's opening scene is Icon%02d.dat and its victory
   scene Win%02d.dat, and both arrive here as the one string the caller pushes.

   THE ARCHIVE FAILING TO OPEN IS REPORTED AND THEN IGNORED.  CMP dword ptr
   [EBP-0x2c],0x0 / JNZ at 00021690 guards nothing but the printf and the wait
   for a key: control falls straight into the load at 000216ab with the null
   handle in hand.  Adding a return there is the obvious repair and it is not
   equivalent -- the original goes on to dereference that handle inside
   fdps_vfs_find_entry (rebuild_info/pitfalls.md).

   THE SCRIPT FAILING TO LOAD IS A DIFFERENT MATTER.  Its own test at 000216c2
   jumps to 00021dd2, past the free of the script buffer and past the whole
   interpreter loop, and the function then unwinds normally.

   The loop is written round a running flag rather than a break: the ladder's
   unknown-opcode arm and its opcode 0 arm both fall on MOV byte ptr [EBP-4],0
   at 00021dbd and then re-enter the loop, which sees the flag and leaves.  The
   flag is a byte, not an int -- MOV byte ptr, CMP byte ptr.

   EVERY PASS FLUSHES THE KEYBOARD AND CLEARS THE CURSOR DRAW MODE, whatever
   the opcode turns out to be, so a key pressed during one opcode cannot be
   read by the next one and no map cursor is drawn over any of them.  The mode
   is put back to 1 on the way out, not to what it held on entry.

   Only three kinds of call here have their results used, and all three are
   read off the assembly rather than the decompiler: fdps_vfs_open's handle and
   fdps_vfs_load_file's buffer land in frame slots at 00021689 and 000216bb,
   the five sub-handlers that own their own operands have their returns stored
   back as the script position (MOV dword ptr [EBP-0x28],EAX at 00021882,
   0002189a, 00021958, 00021a71 and 00021c10), and
   fdps_icon_script_prompt_three_way_choice's answer is kept at 00021db2 for
   whatever opcode 4 comes next.  Everything else discards EAX.

   The epilogue reloads data_fdps_chapter_current_chapter_id and compares it
   against the copy the prologue took at 00021678.  Nothing branches on the
   result, the function returns void, and all 67 call sites destroy the flags
   with ADD ESP,0x4 before they do anything else, so the comparison has no
   observable and no local is kept here to feed it. */
void fdps_icon_script_run(char *script_name)
{
    /* The loaded script image and how far into it the interpreter has got.
       Every operand is read as script[offset + n], so the offset is what each
       opcode advances rather than the pointer. */
    unsigned char *script;
    int offset;
    /* The opened IconAni.vfs, and the buffers the two media opcodes load out
       of it.  The .saf plays synchronously and is freed on the spot; the .wav
       buffers have to outlive the call that starts them, so they are parked
       and freed only when the script ends. */
    void *archive;
    void *saf_image;
    void *wav_buffers[SCRIPT_WAV_SLOTS];
    int wav_slots_used;
    /* Cleared by the arm that ends the script; the loop runs while it holds. */
    char script_running;
    /* The opcode byte this pass is dispatching. */
    unsigned char opcode;
    /* The unit record an opcode edits, re-resolved for every opcode because
       fdps_get_unit_record's answer is only good until the array moves. */
    struct fdps_unit_record *unit;
    /* Opcode 0x14's target: the addressed cell of the tile layer, and the row
       stride the layer header declares. */
    unsigned char *map_cell;
    int map_row_stride;
    /* Opcode 7's track, after 0xff has been turned into silence. */
    int music_track;
    /* The branch opcode 0x63 asked the player for, kept for the opcode 4 that
       follows it.  It is not initialised: a script whose opcode 4 asks for the
       chosen branch without having asked the question first deploys on
       whatever the frame slot held, which is what the original does. */
    int chosen_branch;
    /* Where a composed member name is built. */
    char member_name[SCRIPT_MEMBER_NAME_BYTES];
    int xp_round;
    int wav_slot;

    script_running = 1;
    offset = 0;
    wav_slots_used = 0;

    fdps_units_clear_status_bit7();

    archive = fdps_vfs_open(SCRIPT_ARCHIVE_NAME);
    if (archive == NULL) {
        printf(SCRIPT_ARCHIVE_MISSING_MESSAGE, SCRIPT_ARCHIVE_NAME);
        fdps_wait_any_key();
    }

    script = (unsigned char *) fdps_vfs_load_file(script_name, archive);
    if (script == NULL) {
        fdps_wait_any_key();
    } else {
        while (script_running) {
            fdps_flush_keyboard_queue();
            data_fdps_map_cursor_draw_mode = 0;
            opcode = script[offset];

            switch (opcode) {
            case SCRIPT_OP_WALK_UNITS:
                offset = fdps_icon_script_walk_units(script, offset);
                break;

            case SCRIPT_OP_FACE_UNITS:
                offset = fdps_icon_script_set_unit_facing(script, offset);
                break;

            case SCRIPT_OP_DRAW_TEXT:
                fdps_draw_text(data_fdps_current_chapter_text_ptr,
                               (int) script[offset + 1],
                               (unsigned char *) VGA_SCREEN_BASE,
                               VGA_SCREEN_PITCH, SCRIPT_TEXT_FG_COLOR,
                               SCRIPT_TEXT_BG_COLOR,
                               SCRIPT_TEXT_OUTLINE_COLOR);
                offset += 2;
                break;

            case SCRIPT_OP_DEPLOY_WAVE:
                if (script[offset + 1] == SCRIPT_WAVE_FROM_CHOICE) {
                    fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                                     chosen_branch, script[offset + 2]);
                } else {
                    fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                                     (int) script[offset + 1],
                                     script[offset + 2]);
                }
                offset += 3;
                break;

            case SCRIPT_OP_SCROLL_VIEW:
                offset = fdps_icon_script_scroll_view_to_tile(script, offset);
                break;

            case SCRIPT_OP_PLAY_SAF:
                sprintf(member_name, SCRIPT_SAF_MEMBER_FORMAT,
                        (int) script[offset + 1]);
                fdps_vfs_load_file_or_exit(archive, member_name, &saf_image);
                fdps_saf_play_over_scene(saf_image);
                free(saf_image);
                offset += 2;
                break;

            case SCRIPT_OP_SET_MUSIC:
                music_track = (int) script[offset + 1];
                if (music_track == SCRIPT_MUSIC_SILENCE_OPERAND) {
                    music_track = SCRIPT_MUSIC_SILENCE;
                }
                fdps_cd_set_music_track(music_track);
                offset += 2;
                break;

            case SCRIPT_OP_PLAY_WAV:
                /* The name is composed before the table is checked, so a
                   twenty-first sound costs the sprintf and nothing else. */
                sprintf(member_name, SCRIPT_WAV_MEMBER_FORMAT,
                        (int) script[offset + 1]);
                if (wav_slots_used < SCRIPT_WAV_SLOTS) {
                    fdps_vfs_load_file_or_exit(archive, member_name,
                                               &wav_buffers[wav_slots_used]);
                    fdps_audio_start_wav(wav_buffers[wav_slots_used],
                                         SCRIPT_WAV_LOOP_ONCE,
                                         SCRIPT_WAV_RATE_FROM_HEADER,
                                         SCRIPT_WAV_VOLUME_FROM_HEADER);
                    wav_slots_used++;
                }
                offset += 2;
                break;

            case SCRIPT_OP_BLINK_UNITS_OUT:
                offset = fdps_icon_script_blink_units_out(script, offset);
                break;

            case SCRIPT_OP_PLACE_UNIT:
                if ((int) script[offset + 1] < data_fdps_map_unit_count) {
                    unit = fdps_get_unit_record((int) script[offset + 1]);
                    unit->pos_x = script[offset + 2];
                    unit->pos_y = script[offset + 3];
                    unit->facing = script[offset + 4];
                }
                offset += 5;
                break;

            case SCRIPT_OP_RETIRE_UNIT:
                if ((int) script[offset + 1] < data_fdps_map_unit_count) {
                    unit = fdps_get_unit_record((int) script[offset + 1]);
                    unit->flags = (unsigned char)
                                  (unit->flags | SCRIPT_UNIT_RETIRED_FLAG);
                }
                offset += 2;
                break;

            case SCRIPT_OP_REVIVE_UNIT:
                if ((int) script[offset + 1] < data_fdps_map_unit_count) {
                    unit = fdps_get_unit_record((int) script[offset + 1]);
                    unit->flags = (unsigned char)
                                  (unit->flags & ~SCRIPT_UNIT_RETIRED_FLAG);
                    memset(unit->status_timers, 0,
                           SCRIPT_UNIT_STATUS_TIMER_BYTES);
                }
                offset += 2;
                break;

            case SCRIPT_OP_SET_VIEW_TILE:
                data_fdps_battle_view_window_origin_x =
                    (int) script[offset + 1] * MAP_TILE_PIXELS;
                data_fdps_battle_view_window_origin_y =
                    (int) script[offset + 2] * MAP_TILE_PIXELS;
                data_fdps_map_cursor_world_x =
                    data_fdps_battle_view_window_origin_x + MAP_TILE_PIXELS;
                data_fdps_map_cursor_world_y =
                    data_fdps_battle_view_window_origin_y + MAP_TILE_PIXELS;
                offset += 3;
                break;

            case SCRIPT_OP_FADE_OUT:
                fdps_icon_script_fade_to_black((int) script[offset + 1]);
                offset += 2;
                break;

            case SCRIPT_OP_FADE_IN:
                fdps_icon_script_fade_in((int) script[offset + 1]);
                offset += 2;
                break;

            case SCRIPT_OP_SHAKE_VIEW:
                offset = fdps_icon_script_animate_view_offset(script, offset);
                break;

            case SCRIPT_OP_SWITCH_MAP:
                data_fdps_chapter_current_chapter_id =
                    (int) script[offset + 1];
                fdps_chapter_state_reset();
                offset += 2;
                break;

            case SCRIPT_OP_SET_UNIT_TIMER:
                /* No bounds check on the unit index and none on the byte
                   index either, both deliberate: this is the one unit opcode
                   the original leaves unguarded. */
                unit = fdps_get_unit_record((int) script[offset + 1]);
                unit->status_timers[(int) script[offset + 2]
                                    + SCRIPT_UNIT_TIMER_FIRST_INDEX] =
                    script[offset + 3];
                offset += 4;
                break;

            case SCRIPT_OP_TRIGGER_CELL_EVENT:
                data_fdps_map_cell_event_triggered_flags[script[offset + 1]] =
                    script[offset + 2];
                offset += 3;
                fdps_map_apply_triggered_cell_changes();
                break;

            case SCRIPT_OP_SET_MAP_CELL:
                map_row_stride = (int) *(short *)
                    (data_fdps_scene_layer_tile_map_ptrs[0]
                     + SCRIPT_TILE_LAYER_STRIDE_OFFSET);
                map_cell = data_fdps_scene_layer_tile_map_ptrs[0]
                           + ((int) script[offset + 1]
                              + map_row_stride * (int) script[offset + 2]) * 2
                           + SCRIPT_TILE_LAYER_HEADER_BYTES;
                /* One 16-bit move in and one out -- MOV DX,word ptr [EDX+3]
                   then MOV word ptr [EAX],DX -- so the cell takes the two
                   operand bytes as a word and not as two byte stores. */
                *(short *) map_cell = *(short *) (script + offset + 3);
                offset += 5;
                break;

            case SCRIPT_OP_BIAS_PALETTE:
                fdps_set_palette_range(
                    (struct fdps_palette_entry *)
                        data_fdps_vga_main_palette_ptr,
                    SCRIPT_PALETTE_FIRST_ENTRY, SCRIPT_PALETTE_LAST_ENTRY,
                    (int) script[offset + 1], (int) script[offset + 2],
                    (int) script[offset + 3]);
                offset += 4;
                break;

            case SCRIPT_OP_DEBUG_LEVEL_UNIT_3:
                for (xp_round = 0; xp_round < SCRIPT_XP_AWARD_ROUNDS;
                     xp_round++) {
                    data_fdps_battle_pending_xp_credit =
                        SCRIPT_XP_AWARD_POINTS;
                    fdps_unit_award_exp_and_level_up(SCRIPT_XP_AWARD_UNIT);
                }
                fdps_roster_write_back_battle_units();
                offset++;
                break;

            case SCRIPT_OP_ACTOR_BEHAVIOR_STEP:
                fdps_map_actor_behavior_step((int) script[offset + 1],
                                             (int) script[offset + 2]);
                offset += 3;
                break;

            case SCRIPT_OP_ASK_THREE_WAY:
                chosen_branch = fdps_icon_script_prompt_three_way_choice();
                offset++;
                break;

            default:
                /* SCRIPT_OP_END lands here too: the ladder gives opcode 0 no
                   arm of its own and every value it does not recognise ends
                   the script the same way. */
                script_running = 0;
                break;
            }
        }

        free(script);
    }

    free(archive);

    for (wav_slot = 0; wav_slot < wav_slots_used; wav_slot++) {
        free(wav_buffers[wav_slot]);
    }

    data_fdps_map_cursor_draw_mode = 1;
}
