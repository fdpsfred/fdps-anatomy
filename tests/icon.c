/* tests/icon.c -- cover for src/icon.c.
 *
 * Expected values come from the assembly -- at 00022410, MOV dword ptr
 * [EBP-0x4],0x0 then CMP against 0x40 with JL and ADD 0x4 at the bottom of the
 * loop; at 00022480, MOV dword ptr [EBP-0x4],0x40 then CMP against 0x0 with
 * JGE and ADD -0x4, so that one's counter reaches 0 and its last bias is 0 --
 * plus, in both, the three NEG EAX that turn the counter into the three
 * channel biases and the PUSH 0xff / PUSH 0x0 / PUSH dword ptr [0x000643bc]
 * that fix the range and its source, and the palette record ticket 17 settled
 * (red +0, green +1, blue +2, stride 3).  None of them is read off the
 * emitted C.
 *
 * WHAT THE FADE IS OBSERVED THROUGH.  The whole visible output of this
 * function is DAC entries, and the VGA DAC is readable: entry number to 0x3c7,
 * then three reads of 0x3c9 give back red, green and blue.  So every assertion
 * here is made against the hardware the function actually wrote to rather than
 * against a mock.  The first test establishes that premise -- a DAC that did
 * not read back would make every later assertion meaningless rather than
 * false.
 *
 * WHY THE DAC IS SAVED AND PUT BACK.  Unlike the other palette routines
 * covered so far, this one rewrites all 256 entries, the sixteen a text-mode
 * console draws its report with included.  Each test that runs a fade
 * therefore saves the DAC first, reads what it needs to assert into locals
 * while the faded palette is still up, and restores the DAC before the first
 * CHECK_EQ -- so a failure is printed on a readable screen instead of a black
 * one.
 *
 * The master palette pointer is a stub until ticket 23, so every test points
 * it at a palette staged here and no expected value depends on what the real
 * Fde.pal holds.
 *
 * WHAT IS NOT ASSERTED.  Only a ramp's last step is observable: each upload
 * overwrites the one before it, and nothing outside either function sees the
 * intermediate biases -- including the fade in's first bias of -64, whose
 * whole job is to blank a DAC that the next sixteen steps then overwrite.  The retrace wait and the delay have
 * no observable at all beyond the time they take, and a test that timed them
 * would be measuring the emulator's cycle setting -- that they are there, one
 * of each per step, is a playtest contract (rebuild_info/pitfalls.md), not a
 * unit-test one.  step_delay_ms is passed straight to the CRT's delay() and
 * is therefore in the same position; every test here passes 0.
 */
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "mapdraw.h"
#include "icon.h"

#define VGA_DAC_READ_INDEX 0x3c7
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9

/* The DAC the fade covers: entries 0..255, three components each. */
#define DAC_ENTRY_COUNT 256

/* The darkening the sixteenth and last step applies.  0, 4, ... 60: the
   counter stops below 0x40, so the last one is 60 and not 64. */
#define LAST_STEP_DARKENING 60

static unsigned char stage_pal[DAC_ENTRY_COUNT * 3];
static unsigned char saved_dac[DAC_ENTRY_COUNT * 3];

static void write_dac_entry(int entry, int red, int green, int blue)
{
    outp(VGA_DAC_WRITE_INDEX, entry);
    outp(VGA_DAC_DATA, red);
    outp(VGA_DAC_DATA, green);
    outp(VGA_DAC_DATA, blue);
}

/* Reads one DAC entry into three elements of components[]. */
static void read_dac_entry(int entry, int *components)
{
    outp(VGA_DAC_READ_INDEX, entry);
    components[0] = (int) (inp(VGA_DAC_DATA) & 0x3f);
    components[1] = (int) (inp(VGA_DAC_DATA) & 0x3f);
    components[2] = (int) (inp(VGA_DAC_DATA) & 0x3f);
}

static void save_dac(void)
{
    int entry;
    int components[3];

    for (entry = 0; entry < DAC_ENTRY_COUNT; entry++) {
        read_dac_entry(entry, components);
        saved_dac[entry * 3] = (unsigned char) components[0];
        saved_dac[entry * 3 + 1] = (unsigned char) components[1];
        saved_dac[entry * 3 + 2] = (unsigned char) components[2];
    }
}

static void restore_dac(void)
{
    int entry;

    for (entry = 0; entry < DAC_ENTRY_COUNT; entry++) {
        write_dac_entry(entry, (int) saved_dac[entry * 3],
                        (int) saved_dac[entry * 3 + 1],
                        (int) saved_dac[entry * 3 + 2]);
    }
}

/* Loads one record of the staged master palette. */
static void stage_entry(int n, int red, int green, int blue)
{
    stage_pal[n * 3] = (unsigned char) red;
    stage_pal[n * 3 + 1] = (unsigned char) green;
    stage_pal[n * 3 + 2] = (unsigned char) blue;
}

/* Loads every record of the staged master palette with the same colour and
   points the game's master palette pointer at it. */
static void stage_uniform_palette(int red, int green, int blue)
{
    int n;

    for (n = 0; n < DAC_ENTRY_COUNT; n++) {
        stage_entry(n, red, green, blue);
    }
    data_fdps_vga_main_palette_ptr = stage_pal;
}

static void the_dac_reads_back_what_was_written(void)
{
    int components[3];

    save_dac();
    write_dac_entry(200, 13, 29, 47);
    read_dac_entry(200, components);
    restore_dac();

    CHECK_EQ(components[0], 13);
    CHECK_EQ(components[1], 29);
    CHECK_EQ(components[2], 47);
}

/* CMP dword ptr [EBP-0x4],0x40 / JL with ADD 0x4: the counter's last value is
   60, so the last bias uploaded is -60 and a component brighter than that
   survives the fade.  A loop written to reach -64 -- or the -63 the sibling
   opcode's plate rounds to -- would leave all three of these at 0, which is a
   different screen and a different starting point for the fade back in. */
static void the_last_step_stops_sixty_below_the_master_palette(void)
{
    int first[3];
    int last[3];

    save_dac();
    stage_uniform_palette(63, 62, 60);

    fdps_icon_script_fade_to_black(0);

    read_dac_entry(0, first);
    read_dac_entry(255, last);
    restore_dac();

    CHECK_EQ(first[0], 63 - LAST_STEP_DARKENING);
    CHECK_EQ(first[1], 62 - LAST_STEP_DARKENING);
    CHECK_EQ(first[2], 0);
    CHECK_EQ(last[0], 63 - LAST_STEP_DARKENING);
    CHECK_EQ(last[1], 62 - LAST_STEP_DARKENING);
    CHECK_EQ(last[2], 0);
}

/* PUSH 0xff / PUSH 0x0 as the last and first entry of the range, and the
   upload's own three-byte stride: entry n ends up holding record n of the
   staged palette darkened, not record 0 repeated and not some other record.
   The samples span both ends of the range and the middle of it. */
static void every_entry_takes_its_own_record_of_the_master_palette(void)
{
    static int sample[6] = {0, 1, 2, 127, 254, 255};
    int seen[6][3];
    int n;
    int s;

    save_dac();
    for (n = 0; n < DAC_ENTRY_COUNT; n++) {
        stage_entry(n, 61 + (n % 3), 63 - (n % 3), 60 + (n % 4));
    }
    data_fdps_vga_main_palette_ptr = stage_pal;

    fdps_icon_script_fade_to_black(0);

    for (s = 0; s < 6; s++) {
        read_dac_entry(sample[s], seen[s]);
    }
    restore_dac();

    for (s = 0; s < 6; s++) {
        n = sample[s];
        CHECK_EQ(seen[s][0], 61 + (n % 3) - LAST_STEP_DARKENING);
        CHECK_EQ(seen[s][1], 63 - (n % 3) - LAST_STEP_DARKENING);
        CHECK_EQ(seen[s][2], 60 + (n % 4) - LAST_STEP_DARKENING);
    }
}

/* PUSH dword ptr [0x000643bc] inside the loop, once per step: every step
   re-derives the DAC from the master palette rather than from what is on the
   DAC already.  Entry 40 is bright on the DAC and dim in the master palette,
   entry 41 the other way round; both come out of the fade following the
   master palette, so a ramp that darkened the live DAC instead would get both
   backwards. */
static void the_ramp_runs_from_the_master_palette_not_the_live_dac(void)
{
    int dim[3];
    int bright[3];

    save_dac();
    stage_uniform_palette(0, 0, 0);
    stage_entry(40, 20, 10, 5);
    stage_entry(41, 63, 63, 63);
    write_dac_entry(40, 63, 63, 63);
    write_dac_entry(41, 0, 0, 0);

    fdps_icon_script_fade_to_black(0);

    read_dac_entry(40, dim);
    read_dac_entry(41, bright);
    restore_dac();

    CHECK_EQ(dim[0], 0);
    CHECK_EQ(dim[1], 0);
    CHECK_EQ(dim[2], 0);
    CHECK_EQ(bright[0], 63 - LAST_STEP_DARKENING);
    CHECK_EQ(bright[1], 63 - LAST_STEP_DARKENING);
    CHECK_EQ(bright[2], 63 - LAST_STEP_DARKENING);
}

/* The three NEG EAX push the same value three times: one counter drives all
   three channels, so a master entry whose components differ keeps those
   differences exactly, shifted down together. */
static void the_same_bias_is_applied_to_all_three_channels(void)
{
    int components[3];

    save_dac();
    stage_uniform_palette(63, 62, 61);

    fdps_icon_script_fade_to_black(0);

    read_dac_entry(100, components);
    restore_dac();

    CHECK_EQ(components[0] - components[1], 1);
    CHECK_EQ(components[1] - components[2], 1);
    CHECK_EQ(components[0], 63 - LAST_STEP_DARKENING);
}

/* The fade back in, opcode 0x0f at 00022480.  Its counter is MOV
   [EBP-0x4],0x40 / CMP against 0x0 / JGE with ADD -0x4, so it reaches 0 and the
   body runs one last time with all three NEG EAX yielding a bias of 0.  A bias
   of 0 means the upload's sum is the master palette component itself, so the
   DAC finishes holding the master palette exactly -- which is what makes this
   opcode restore the picture rather than merely brighten it partway.  The
   components chosen are all inside the DAC's 0..63 range so the callee's clamp
   cannot be what produces the expected value. */
static void the_fade_in_ends_holding_the_master_palette_unmodified(void)
{
    int first[3];
    int last[3];

    save_dac();
    stage_uniform_palette(63, 62, 60);

    fdps_icon_script_fade_in(0);

    read_dac_entry(0, first);
    read_dac_entry(255, last);
    restore_dac();

    CHECK_EQ(first[0], 63);
    CHECK_EQ(first[1], 62);
    CHECK_EQ(first[2], 60);
    CHECK_EQ(last[0], 63);
    CHECK_EQ(last[1], 62);
    CHECK_EQ(last[2], 60);
}

/* PUSH 0xff / PUSH 0x0 / PUSH dword ptr [0x000643bc] in the fade in's loop are
   the same three arguments the fade out pushes, so the same range and the same
   three-byte stride: entry n comes back holding record n of the master palette
   and not record 0 repeated. */
static void the_fade_in_gives_every_entry_its_own_record(void)
{
    static int sample[6] = {0, 1, 2, 127, 254, 255};
    int seen[6][3];
    int n;
    int s;

    save_dac();
    for (n = 0; n < DAC_ENTRY_COUNT; n++) {
        stage_entry(n, 61 + (n % 3), 63 - (n % 3), 60 + (n % 4));
    }
    data_fdps_vga_main_palette_ptr = stage_pal;

    fdps_icon_script_fade_in(0);

    for (s = 0; s < 6; s++) {
        read_dac_entry(sample[s], seen[s]);
    }
    restore_dac();

    for (s = 0; s < 6; s++) {
        n = sample[s];
        CHECK_EQ(seen[s][0], 61 + (n % 3));
        CHECK_EQ(seen[s][1], 63 - (n % 3));
        CHECK_EQ(seen[s][2], 60 + (n % 4));
    }
}

/* The master palette pointer is re-read on every step here too, so what was on
   the DAC when the opcode was reached does not survive.  Entry 40 is bright on
   the DAC and dim in the master palette and entry 41 the other way round; both
   end up following the master palette.  This is the assertion that fails if the
   ramp were ever rewritten to brighten the live DAC instead. */
static void the_fade_in_derives_from_the_master_palette_not_the_live_dac(void)
{
    int dim[3];
    int bright[3];

    save_dac();
    stage_uniform_palette(0, 0, 0);
    stage_entry(40, 20, 10, 5);
    stage_entry(41, 63, 63, 63);
    write_dac_entry(40, 63, 63, 63);
    write_dac_entry(41, 0, 0, 0);

    fdps_icon_script_fade_in(0);

    read_dac_entry(40, dim);
    read_dac_entry(41, bright);
    restore_dac();

    CHECK_EQ(dim[0], 20);
    CHECK_EQ(dim[1], 10);
    CHECK_EQ(dim[2], 5);
    CHECK_EQ(bright[0], 63);
    CHECK_EQ(bright[1], 63);
    CHECK_EQ(bright[2], 63);
}

/* The pair as a script uses them: 0x0e blacks the scene out, 0x0f brings it
   back.  The two handlers are deliberately not mirror images -- the fade out's
   last bias is -60 and the fade in's is 0 -- and this pins that asymmetry from
   the outside.  A fade in that copied the sibling's bound would stop at a bias
   of -4 and leave every component four short, so the second reading here would
   be 59, 58, 57 instead of the master palette's own 63, 62, 61. */
static void the_fade_in_restores_what_the_fade_out_dimmed(void)
{
    int dimmed[3];
    int restored[3];

    save_dac();
    stage_uniform_palette(63, 62, 61);

    fdps_icon_script_fade_to_black(0);
    read_dac_entry(100, dimmed);

    fdps_icon_script_fade_in(0);
    read_dac_entry(100, restored);
    restore_dac();

    CHECK_EQ(dimmed[0], 63 - LAST_STEP_DARKENING);
    CHECK_EQ(dimmed[1], 62 - LAST_STEP_DARKENING);
    CHECK_EQ(dimmed[2], 61 - LAST_STEP_DARKENING);
    CHECK_EQ(restored[0], 63);
    CHECK_EQ(restored[1], 62);
    CHECK_EQ(restored[2], 61);
}


/* ---------------------------------------------------------------------------
 * 00021e30, script opcode 5: the scripted view scroll.
 *
 * WHAT CAN BE ASSERTED HERE AND WHAT CANNOT.  The scroll's animated path runs
 * fdps_render_view_frame once per step, and that function holds until the
 * timer's tick counter moves and blits to the VGA aperture -- neither of which
 * a unit test has.  So every case below drives the step_count == 0 path, where
 * the assembly's CMP dword ptr [EBP-0x10],0x0 / JZ 0x00021f1e skips the loop
 * outright and no call is made at all.  That path still carries all of the
 * function's arithmetic: the operand decode, the tile-to-pixel conversion, the
 * distance and the quotient that decides whether a scroll happens, the four
 * globals it leaves set, and the return value.
 *
 * The one thing it cannot pin is that the step count comes from the LARGER of
 * the two axis distances rather than the smaller: below one tile both choices
 * give zero.  That is settled in the assembly instead -- CMP EAX,[EBP-0x14] /
 * JLE at 00021e94 picks the x branch only when x is the greater -- and is a
 * playtest observation, not a unit-test one.
 *
 * Expected values come from the assembly: IMUL EAX,EAX,0x18 on each operand
 * after AND EAX,0xff, the byte loads at [EAX+0x1] and [EAX+0x2], the IDIV by
 * 0x18 that makes the step count, the ADD EAX,0x18 before each of the two
 * cursor stores at 00021f24 and 00021f2f, the plain stores of the target into
 * the origin at 00021f37 and 00021f3f, and the ADD EAX,0x3 on the returned
 * offset.  None is read off the emitted C.
 *
 * The view origin and cursor globals are ordinary ints that ticket 23 has not
 * filled in yet, so each case sets the origin itself and asserts only against
 * what it staged.
 */

/* The map's tile size in pixels, the 0x18 every operand is multiplied by. */
#define TILE_PIXELS 24

/* A script image big enough for an opcode at a non-zero offset plus its two
   operands and a decoy byte after them. */
static unsigned char scroll_script[16];

/* Puts the view where the scroll will find it, so the distance -- and with it
   the step count -- is whatever the case wants it to be. */
static void place_view(int origin_x, int origin_y)
{
    data_fdps_battle_view_window_origin_x = origin_x;
    data_fdps_battle_view_window_origin_y = origin_y;
}

/* A target the view already sits on: both distances are 0, the quotient is 0,
   and the loop is skipped.  Pins the tile-to-pixel conversion, both cursor
   stores at target + one tile, and the returned offset. */
static void a_target_the_view_already_sits_on_moves_nothing(void)
{
    int next_offset;

    scroll_script[0] = 5;
    scroll_script[1] = 10;
    scroll_script[2] = 4;
    place_view(10 * TILE_PIXELS, 4 * TILE_PIXELS);

    next_offset = fdps_icon_script_scroll_view_to_tile(scroll_script, 0);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 10 * TILE_PIXELS);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 4 * TILE_PIXELS);
    CHECK_EQ(data_fdps_map_cursor_world_x, 10 * TILE_PIXELS + TILE_PIXELS);
    CHECK_EQ(data_fdps_map_cursor_world_y, 4 * TILE_PIXELS + TILE_PIXELS);
    CHECK_EQ(next_offset, 3);
}

/* The operands are read at offset + 1 and offset + 2, not at offset + 0, and
   the return value is the offset the caller gave plus three.  The opcode byte
   and the byte after the operands are both set to tile numbers that would give
   very different pixels if either were picked up by mistake. */
static void the_operands_are_the_two_bytes_after_the_opcode(void)
{
    int next_offset;

    scroll_script[7] = 99;
    scroll_script[8] = 3;
    scroll_script[9] = 2;
    scroll_script[10] = 77;
    place_view(3 * TILE_PIXELS, 2 * TILE_PIXELS);

    next_offset = fdps_icon_script_scroll_view_to_tile(scroll_script, 7);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 3 * TILE_PIXELS);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 2 * TILE_PIXELS);
    CHECK_EQ(next_offset, 10);
}

/* AND EAX,0xff before the multiply: a tile number with its top bit set is 200
   and 255, not -56 and -1.  A signed decode would put the origin at -1344 and
   -24 instead of at 4800 and 6120. */
static void the_operand_bytes_are_unsigned(void)
{
    scroll_script[0] = 5;
    scroll_script[1] = 200;
    scroll_script[2] = 255;
    place_view(200 * TILE_PIXELS, 255 * TILE_PIXELS);

    fdps_icon_script_scroll_view_to_tile(scroll_script, 0);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 4800);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 6120);
    CHECK_EQ(data_fdps_map_cursor_world_x, 4800 + TILE_PIXELS);
    CHECK_EQ(data_fdps_map_cursor_world_y, 6120 + TILE_PIXELS);
}

/* A distance of 23 pixels on the x axis: the quotient by 0x18 truncates to
   zero, so no frame is rendered and the view is placed on the target in one
   go.  The origin has to land on the target exactly -- a step count rounded up
   to one would have divided a 23-pixel delta by 1 and got there too, but a
   division that rounded the quotient the other way would leave the view 23
   pixels short. */
static void a_sub_tile_distance_on_x_jumps_straight_to_the_target(void)
{
    scroll_script[0] = 5;
    scroll_script[1] = 6;
    scroll_script[2] = 6;
    place_view(6 * TILE_PIXELS - 23, 6 * TILE_PIXELS);

    fdps_icon_script_scroll_view_to_tile(scroll_script, 0);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 6 * TILE_PIXELS);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 6 * TILE_PIXELS);
}

/* The same on the y axis, and in the negative direction: abs is applied to
   each axis distance before the comparison, so a view 23 pixels BELOW the
   target is as much a sub-tile move as one 23 pixels above it. */
static void a_sub_tile_distance_on_y_jumps_straight_to_the_target(void)
{
    scroll_script[0] = 5;
    scroll_script[1] = 9;
    scroll_script[2] = 9;
    place_view(9 * TILE_PIXELS, 9 * TILE_PIXELS + 23);

    fdps_icon_script_scroll_view_to_tile(scroll_script, 0);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 9 * TILE_PIXELS);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 9 * TILE_PIXELS);
}

/* Tile 0, 0 is a real target and not a no-op: the cursor still ends one tile
   in from the view's corner, which is where the assembly's ADD EAX,0x18 puts
   it whatever the target is. */
static void the_cursor_sits_one_tile_inside_the_new_view(void)
{
    scroll_script[0] = 5;
    scroll_script[1] = 0;
    scroll_script[2] = 0;
    place_view(0, 0);

    fdps_icon_script_scroll_view_to_tile(scroll_script, 0);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 0);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 0);
    CHECK_EQ(data_fdps_map_cursor_world_x, TILE_PIXELS);
    CHECK_EQ(data_fdps_map_cursor_world_y, TILE_PIXELS);
}


/* ---------------------------------------------------------------------------
 * 00021f60, script opcode 1: the scripted group walk.
 *
 * WHAT THE RUN IS WATCHED THROUGH.  The handler's whole persistent output is
 * the unit records it edits, the two HUD globals it leaves set and the stream
 * offset it returns; the frames it renders in between are the compositor's
 * output and not this function's.  So every case below points the battle unit
 * array at records staged here, runs the handler and reads those records back.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED.  Every held frame goes through
 * fdps_render_view_frame, whose own wait ends only when
 * data_fdps_timer_tick_counter moves, and nothing advances that counter in a
 * test image -- the first frame would never end.  Each run that renders at all
 * hooks IRQ0 for the duration of the call with a handler that increments the
 * counter and chains to the one that was there, the same way tests/death.c and
 * tests/anim.c do.
 *
 * WHY NOTHING PAINTS.  The handler itself clears
 * data_fdps_map_cursor_draw_mode and data_fdps_ui_play_active_flag on entry,
 * which switches off the map cursor and the info panel; walk_stage zeroes the
 * scene layer count, the terrain HUD flag and the map unit count as well, so
 * the composite each frame builds is empty and the blit copies a blank page.
 * That keeps a run to the cost of one timer tick per frame and keeps the test
 * report on screen.
 *
 * Expected values come from the assembly: XOR EAX,EAX / MOV AL,byte ptr
 * [EDX+0x1], [EDX+0x2] and [EDX+0x3] for the three unsigned operands and ADD
 * dword ptr [EBP+0x18],0x4 for the header they sit in; MOV [EBP-0x18],0x1 with
 * CMP against 0x6 and JLE for the six sub-steps; the nesting order of the four
 * counters, with CALL 0x0002beb0 and CALL 0x0002eab0 at 000220b8 sitting at
 * the bottom of the hold loop and the unit loop inside it; MOV DL,byte ptr
 * [EAX+0x1] / MOV byte ptr [EAX+0x3],DL for the facing write; CMP dword ptr
 * [EBP-0x18],0x6 / JZ and CMP dword ptr [EBP-0xc],0x0 / JNZ for the commit
 * guard; INC byte ptr [EAX+0x1] / DEC byte ptr [EAX] / DEC byte ptr [EAX+0x1] /
 * INC byte ptr [EAX] for the four direction arms; MOV [0x00069cd0],0x1 and MOV
 * [0x00060159],0x1 at 000220d1 for the exit constants; and MOV EAX,[EBP-0x20] /
 * ADD EAX,EAX / ADD EDX,EAX for the returned offset.  None is read off the
 * emitted C.
 *
 * WHAT IS NOT ASSERTED.  The sub-tile step counter the handler writes for
 * sub-steps 1 to 5 is overwritten by the commit before the call returns, so its
 * intermediate values are visible only to fdps_draw_map_unit inside a frame;
 * the counter's value on return -- 0 -- is asserted instead, and the four-pixel
 * displacement it drives is covered in tests/mapdraw.c.  The unsigned decode of
 * the frames-per-sub-step and tile-count operands has no cheap observable
 * either: a top-bit-set value there is 128 tiles or 128 held frames, tens of
 * thousands of timer ticks, so only the unit count's decode is exercised from
 * the outside and the other two rest on the identical XOR EAX,EAX / MOV AL
 * sequence.  The one rendered frame per held pass, and the retrace each one
 * straddles, are playtest contracts (rebuild_info/pitfalls.md).
 */

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the frame wait spins on the counter. */
#define WALK_TIMER_VECTOR 8

/* Records staged for the battle unit array, and the script image the operands
   and the unit list are written into. */
#define WALK_UNITS 5
#define WALK_SCRIPT_BYTES 32

/* Where the staged units start out.  Far enough from either end of a byte that
   a step in any of the four directions stays in range. */
#define WALK_HOME_X 10
#define WALK_HOME_Y 10

/* Values staged into the two record fields the handler writes, chosen so that
   neither could be mistaken for something the handler produced: no facing arm
   yields 9 and no sub-step is 7. */
#define WALK_STAGED_FACING 9
#define WALK_STAGED_STEP 7

/* Values staged into the two HUD globals before a run, so that finding 1 in
   them afterwards says the handler wrote the exit constants rather than put
   back what it found. */
#define WALK_STAGED_CURSOR_MODE 4
#define WALK_STAGED_PLAY_FLAG 4

static struct fdps_unit_record walk_units[WALK_UNITS];
static unsigned char walk_script[WALK_SCRIPT_BYTES];

static void (__interrupt __far *walk_saved_timer)();

static void __interrupt __far walk_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(walk_saved_timer);
}

/* Points the battle unit array at the staged records, puts every one of them
   on the same tile with a facing and a step counter no arm of the handler
   produces, and switches off everything a rendered frame would otherwise
   paint. */
static void walk_stage(void)
{
    int unit;

    memset(walk_units, 0, sizeof(walk_units));
    for (unit = 0; unit < WALK_UNITS; unit++) {
        walk_units[unit].pos_x = WALK_HOME_X;
        walk_units[unit].pos_y = WALK_HOME_Y;
        walk_units[unit].facing = WALK_STAGED_FACING;
        walk_units[unit].walk_step = WALK_STAGED_STEP;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) walk_units;

    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;

    data_fdps_map_cursor_draw_mode = WALK_STAGED_CURSOR_MODE;
    data_fdps_ui_play_active_flag = WALK_STAGED_PLAY_FLAG;

    memset(walk_script, 0, sizeof(walk_script));
}

/* Writes the opcode and its three operands at offset, and returns the offset
   the first unit pair goes at. */
static int walk_write_header(int offset, int frames_per_sub_step,
                             int tile_count, int unit_count)
{
    walk_script[offset] = 1;
    walk_script[offset + 1] = (unsigned char) frames_per_sub_step;
    walk_script[offset + 2] = (unsigned char) tile_count;
    walk_script[offset + 3] = (unsigned char) unit_count;
    return offset + 4;
}

/* One listed unit: its index in the battle array and the direction it walks. */
static void walk_write_pair(int pair_at, int unit_index, int direction)
{
    walk_script[pair_at] = (unsigned char) unit_index;
    walk_script[pair_at + 1] = (unsigned char) direction;
}

/* One whole run with the frame wait serviced.  Used only by the cases that
   render; a run with a frames-per-sub-step of 0 renders nothing and calls the
   handler directly. */
static int walk_run(int offset)
{
    int next_offset;

    walk_saved_timer = _dos_getvect(WALK_TIMER_VECTOR);
    _dos_setvect(WALK_TIMER_VECTOR, walk_timer_isr);
    next_offset = fdps_icon_script_walk_units(walk_script, offset);
    _dos_setvect(WALK_TIMER_VECTOR, walk_saved_timer);

    return next_offset;
}

/* CMP dword ptr [EBP-0xc],0x0 at the top of the hold loop with the unit loop
   nested inside it: with no frames to hold, the list is never walked, so no
   record is touched however many tiles the script asks for.  Hoisting the unit
   update out to run once per sub-step -- the natural way to write "update the
   units, then render the held frames" -- would move both units three tiles
   here (rebuild_info/pitfalls.md).  The handler still returns the offset past
   the list and still writes the exit constants. */
static void walk_no_held_frames_moves_nothing(void)
{
    int pair_at;
    int next_offset;

    walk_stage();
    pair_at = walk_write_header(0, 0, 3, 2);
    walk_write_pair(pair_at, 0, 0);
    walk_write_pair(pair_at + 2, 1, 3);

    next_offset = fdps_icon_script_walk_units(walk_script, 0);

    CHECK_EQ(walk_units[0].pos_x, WALK_HOME_X);
    CHECK_EQ(walk_units[0].pos_y, WALK_HOME_Y);
    CHECK_EQ(walk_units[0].facing, WALK_STAGED_FACING);
    CHECK_EQ(walk_units[0].walk_step, WALK_STAGED_STEP);
    CHECK_EQ(walk_units[1].pos_x, WALK_HOME_X);
    CHECK_EQ(walk_units[1].pos_y, WALK_HOME_Y);
    CHECK_EQ(next_offset, 8);
}

/* The four direction arms, all in one run: INC byte ptr [EAX+0x1] for facing 0,
   DEC byte ptr [EAX] for 1, DEC byte ptr [EAX+0x1] for 2, and INC byte ptr
   [EAX] for everything else -- which is why the fifth unit here carries 7 and
   still steps right.  Each pair's second byte is written into the record's
   facing before the commit reads it back, so the facing on return is the
   script's code and not the 9 every unit was staged with, and the commit clears
   the sub-tile step counter to 0 rather than leaving the sixth sub-step in it.
   One tile and one held frame per sub-step: six rendered frames. */
static void walk_one_tile_steps_each_unit_in_its_own_direction(void)
{
    int pair_at;
    int next_offset;

    walk_stage();
    pair_at = walk_write_header(0, 1, 1, WALK_UNITS);
    walk_write_pair(pair_at, 0, 0);
    walk_write_pair(pair_at + 2, 1, 1);
    walk_write_pair(pair_at + 4, 2, 2);
    walk_write_pair(pair_at + 6, 3, 3);
    walk_write_pair(pair_at + 8, 4, 7);

    next_offset = walk_run(0);

    CHECK_EQ(walk_units[0].pos_x, WALK_HOME_X);
    CHECK_EQ(walk_units[0].pos_y, WALK_HOME_Y + 1);
    CHECK_EQ(walk_units[1].pos_x, WALK_HOME_X - 1);
    CHECK_EQ(walk_units[1].pos_y, WALK_HOME_Y);
    CHECK_EQ(walk_units[2].pos_x, WALK_HOME_X);
    CHECK_EQ(walk_units[2].pos_y, WALK_HOME_Y - 1);
    CHECK_EQ(walk_units[3].pos_x, WALK_HOME_X + 1);
    CHECK_EQ(walk_units[3].pos_y, WALK_HOME_Y);
    CHECK_EQ(walk_units[4].pos_x, WALK_HOME_X + 1);
    CHECK_EQ(walk_units[4].pos_y, WALK_HOME_Y);
    CHECK_EQ(walk_units[0].facing, 0);
    CHECK_EQ(walk_units[4].facing, 7);
    CHECK_EQ(walk_units[0].walk_step, 0);
    CHECK_EQ(walk_units[3].walk_step, 0);
    CHECK_EQ(next_offset, 4 + 2 * WALK_UNITS);
}

/* CMP dword ptr [EBP-0xc],0x0 / JNZ guards the commit: only the first held
   frame of sub-step 6 advances the tile, so three held frames per sub-step
   still move the unit exactly one tile.  A commit that ran on every held pass
   would put this unit three tiles down instead of one.  Eighteen rendered
   frames. */
static void walk_a_held_frame_does_not_commit_the_tile_again(void)
{
    int pair_at;

    walk_stage();
    pair_at = walk_write_header(0, 3, 1, 1);
    walk_write_pair(pair_at, 0, 0);

    walk_run(0);

    CHECK_EQ(walk_units[0].pos_y, WALK_HOME_Y + 1);
    CHECK_EQ(walk_units[0].pos_x, WALK_HOME_X);
    CHECK_EQ(walk_units[0].walk_step, 0);
}

/* The outermost counter is the tile count operand, so three tiles is three
   commits and the unit ends three tiles along.  Eighteen rendered frames. */
static void walk_the_tile_count_is_how_far_the_group_goes(void)
{
    int pair_at;

    walk_stage();
    pair_at = walk_write_header(0, 1, 3, 1);
    walk_write_pair(pair_at, 0, 3);

    walk_run(0);

    CHECK_EQ(walk_units[0].pos_x, WALK_HOME_X + 3);
    CHECK_EQ(walk_units[0].pos_y, WALK_HOME_Y);
}

/* The pair's first byte is the index fdps_get_unit_record is called with, not
   the position in the list: the two listed units here are 3 and 1, and 0, 2 and
   4 are left exactly where they were staged.  Six rendered frames. */
static void walk_the_pair_names_the_unit_by_index(void)
{
    int pair_at;

    walk_stage();
    pair_at = walk_write_header(0, 1, 1, 2);
    walk_write_pair(pair_at, 3, 0);
    walk_write_pair(pair_at + 2, 1, 2);

    walk_run(0);

    CHECK_EQ(walk_units[3].pos_y, WALK_HOME_Y + 1);
    CHECK_EQ(walk_units[1].pos_y, WALK_HOME_Y - 1);
    CHECK_EQ(walk_units[0].pos_y, WALK_HOME_Y);
    CHECK_EQ(walk_units[0].facing, WALK_STAGED_FACING);
    CHECK_EQ(walk_units[2].pos_y, WALK_HOME_Y);
    CHECK_EQ(walk_units[4].pos_y, WALK_HOME_Y);
}

/* The operands are at offset + 1, + 2 and + 3 and the list starts at
   offset + 4, none of which is offset + 0: the opcode byte and the byte just
   past the list are both set to values that would give a very different run if
   either were picked up by mistake.  Twelve rendered frames -- one held frame
   per sub-step across two tiles -- and the returned offset is the byte past the
   one pair. */
static void walk_the_operands_follow_the_opcode_byte(void)
{
    int pair_at;
    int next_offset;

    walk_stage();
    walk_script[13] = 99;
    pair_at = walk_write_header(7, 1, 2, 1);
    walk_write_pair(pair_at, 0, 3);

    next_offset = walk_run(7);

    CHECK_EQ(walk_units[0].pos_x, WALK_HOME_X + 2);
    CHECK_EQ(walk_units[0].pos_y, WALK_HOME_Y);
    CHECK_EQ(next_offset, 13);
}

/* XOR EAX,EAX / MOV AL,byte ptr [EDX+0x3]: a unit count with its top bit set is
   128, not -128, and the returned offset is offset + 4 + 2 * 128.  A signed
   decode would hand the interpreter back a position 260 bytes BEHIND this
   opcode.  The tile count is 0 so no record is read and the list is never
   walked -- what is being pinned here is the decode and the return arithmetic,
   not the walk. */
static void walk_the_unit_count_operand_is_unsigned(void)
{
    int next_offset;

    walk_stage();
    walk_write_header(0, 1, 0, 0x80);

    next_offset = fdps_icon_script_walk_units(walk_script, 0);

    CHECK_EQ(next_offset, 4 + 2 * 0x80);
}

/* MOV dword ptr [0x00069cd0],0x1 and MOV byte ptr [0x00060159],0x1 on the exit
   path are stores of literals, not restores: both globals were staged at 4 and
   both come back 1.  A save-and-restore pair -- the obvious spelling -- would
   leave 4 in each and carry an area-of-effect cursor mode straight through the
   opcode (rebuild_info/pitfalls.md). */
static void walk_the_hud_globals_end_at_one_not_where_they_started(void)
{
    walk_stage();
    walk_write_header(0, 0, 0, 0);

    fdps_icon_script_walk_units(walk_script, 0);

    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
    CHECK_EQ(data_fdps_ui_play_active_flag, 1);
}

/* ---------------------------------------------------------------------------
 * fdps_icon_script_set_unit_facing -- 00022100, script opcode 2.
 *
 * WHAT IS OBSERVABLE.  The handler's whole output is the facing byte of every
 * listed unit's record, the two HUD globals it writes on the way in and out,
 * and the offset it returns; the frames it holds the pose for are the
 * compositor's output and not this function's.  So each case below points the
 * battle unit array at records staged here, runs the handler and reads those
 * records back.
 *
 * WHY MOST CASES HOLD NO FRAMES.  The turning loop at 00022144 closes at
 * 0002218d and the hold loop at 00022196 is a sibling of it, not its parent,
 * so a hold operand of 0 turns every listed unit and renders nothing.  That is
 * exactly the property being pinned, and it also means the decode, the list
 * walk and the returned offset can all be exercised without a rendered frame
 * at all.  One case does render, to cover the hold loop, and it hooks IRQ0 the
 * same way the walk cases above do -- fdps_render_view_frame's wait ends only
 * when data_fdps_timer_tick_counter moves, and nothing advances that counter
 * in a test image.
 *
 * HOW A RENDERED FRAME IS SEEN FROM OUTSIDE.  fdps_render_view_frame latches
 * data_fdps_timer_tick_counter into data_fdps_view_frame_last_tick (mapdraw.h)
 * as the last thing it does, so a sentinel staged into that latch survives a
 * run that renders nothing and is gone after a run that renders.  That is an
 * exact observable; the NUMBER of frames a hold of n produces is not, because
 * a frame consumes one timer tick and the test cannot say how many ticks
 * elapsed inside the call without racing the interrupt.  One frame per held
 * pass is a playtest contract (rebuild_info/pitfalls.md), the same position
 * the walk cases above leave it in.
 *
 * Expected values come from the assembly: XOR EAX,EAX / MOV AL,byte ptr
 * [EDX+0x1] and [EDX+0x2] for the two unsigned operands and ADD dword ptr
 * [EBP+0x18],0x3 for the header they sit in; the two sibling loops, the first
 * counting to [EBP-0x14] with CALL 0x0002d210 and MOV byte ptr [EAX+0x3],DL in
 * it and the second counting to [EBP-0x18] with CALL 0x0002beb0 alone in it;
 * MOV DL,byte ptr [EAX+0x1] for the facing byte, stored with no mask and no
 * comparison; MOV dword ptr [0x00069cd0],0x1 and MOV byte ptr [0x00060159],0x1
 * at 000221af for the exit constants; and MOV EAX,[EBP-0x14] / ADD EAX,EAX /
 * ADD EDX,EAX at 000221c0 for the returned offset.  None is read off the
 * emitted C.
 */

/* Records staged for the battle unit array, and the script image the operands
   and the unit list are written into.  The image is large enough for the
   biggest list a one-byte count can name, 3 + 2 * 255 bytes. */
#define FACE_UNITS 5
#define FACE_SCRIPT_BYTES 520

/* Staged into every record's facing field before a run, so that finding it
   afterwards says the handler did not touch that unit.  No script below asks
   for a facing of 9. */
#define FACE_STAGED_FACING 9

/* Staged into the frame latch before a run.  A value the timer's tick counter
   will not be sitting on, so the latch having moved says a frame was
   presented. */
#define FACE_LATCH_SENTINEL 0x5a5a5a5aUL

/* Staged into the two HUD globals before a run, so that finding 1 in them
   afterwards says the handler wrote the exit constants rather than put back
   what it found. */
#define FACE_STAGED_CURSOR_MODE 4
#define FACE_STAGED_PLAY_FLAG 4

static struct fdps_unit_record face_units[FACE_UNITS];
static unsigned char face_script[FACE_SCRIPT_BYTES];

/* Points the battle unit array at the staged records, gives every one of them
   a facing no script below asks for, stages the frame latch and switches off
   everything a rendered frame would otherwise paint. */
static void face_stage(void)
{
    int unit;

    memset(face_units, 0, sizeof(face_units));
    for (unit = 0; unit < FACE_UNITS; unit++) {
        face_units[unit].facing = FACE_STAGED_FACING;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) face_units;

    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;

    data_fdps_view_frame_last_tick = (unsigned int) FACE_LATCH_SENTINEL;

    data_fdps_map_cursor_draw_mode = FACE_STAGED_CURSOR_MODE;
    data_fdps_ui_play_active_flag = FACE_STAGED_PLAY_FLAG;

    memset(face_script, 0, sizeof(face_script));
}

/* Writes the opcode and its two operands at offset, and returns the offset the
   first unit pair goes at. */
static int face_write_header(int offset, int hold_frames, int unit_count)
{
    face_script[offset] = 2;
    face_script[offset + 1] = (unsigned char) hold_frames;
    face_script[offset + 2] = (unsigned char) unit_count;
    return offset + 3;
}

/* One listed unit: its index in the battle array and the facing code written
   into its record. */
static void face_write_pair(int pair_at, int unit_index, int facing)
{
    face_script[pair_at] = (unsigned char) unit_index;
    face_script[pair_at + 1] = (unsigned char) facing;
}

/* One whole run with the frame wait serviced.  Used only by the case that
   holds frames; every other case holds none and calls the handler directly.
   The IRQ0 helper is the one the walk cases above install. */
static int face_run(int offset)
{
    int next_offset;

    walk_saved_timer = _dos_getvect(WALK_TIMER_VECTOR);
    _dos_setvect(WALK_TIMER_VECTOR, walk_timer_isr);
    next_offset = fdps_icon_script_set_unit_facing(face_script, offset);
    _dos_setvect(WALK_TIMER_VECTOR, walk_saved_timer);

    return next_offset;
}

/* The turning loop is a sibling of the hold loop, not nested inside it: with a
   hold operand of 0 every listed unit is still turned, no frame is presented,
   and the offset past the list still comes back.  Nesting the turn inside the
   hold the way the walk handler nests its unit loop -- the natural way to
   write "turn them and hold the pose" -- would leave both facings at 9 here
   (rebuild_info/pitfalls.md). */
static void face_a_zero_hold_still_turns_every_listed_unit(void)
{
    int next_offset;
    int pair_at;

    face_stage();
    pair_at = face_write_header(0, 0, 2);
    face_write_pair(pair_at, 0, 1);
    face_write_pair(pair_at + 2, 2, 3);

    next_offset = fdps_icon_script_set_unit_facing(face_script, 0);

    CHECK_EQ(face_units[0].facing, 1);
    CHECK_EQ(face_units[2].facing, 3);
    CHECK_EQ(data_fdps_view_frame_last_tick == FACE_LATCH_SENTINEL, 1);
    CHECK_EQ(next_offset, 7);
}

/* The pair's first byte is the index fdps_get_unit_record is called with, not
   the position in the list: the two listed units here are 3 and 1, and 0, 2
   and 4 keep the facing they were staged with. */
static void face_the_pair_names_the_unit_by_index(void)
{
    int pair_at;

    face_stage();
    pair_at = face_write_header(0, 0, 2);
    face_write_pair(pair_at, 3, 2);
    face_write_pair(pair_at + 2, 1, 0);

    fdps_icon_script_set_unit_facing(face_script, 0);

    CHECK_EQ(face_units[3].facing, 2);
    CHECK_EQ(face_units[1].facing, 0);
    CHECK_EQ(face_units[0].facing, FACE_STAGED_FACING);
    CHECK_EQ(face_units[2].facing, FACE_STAGED_FACING);
    CHECK_EQ(face_units[4].facing, FACE_STAGED_FACING);
}

/* MOV DL,byte ptr [EAX+0x1] / MOV byte ptr [EAX+0x3],DL and nothing between
   them: the pair's second byte reaches the record as it stands.  Nothing masks
   it to two bits, compares it against 3 or maps it through a table, so a code
   the drawing side has no arm for is stored too -- clamping it here, the
   obvious defensive spelling, would make this unit face down instead
   (rebuild_info/pitfalls.md). */
static void face_the_facing_byte_is_stored_as_it_stands(void)
{
    int pair_at;

    face_stage();
    pair_at = face_write_header(0, 0, 1);
    face_write_pair(pair_at, 0, 0xfe);

    fdps_icon_script_set_unit_facing(face_script, 0);

    CHECK_EQ(face_units[0].facing, 0xfe);
}

/* The operands are at offset + 1 and + 2 and the list starts at offset + 3,
   none of which is offset + 0: the opcode byte and the byte just past the list
   are both set to values that would give a very different run if either were
   picked up by mistake.  The returned offset is the byte past the one pair. */
static void face_the_operands_follow_the_opcode_byte(void)
{
    int next_offset;
    int pair_at;

    face_stage();
    face_script[12] = 99;
    pair_at = face_write_header(7, 0, 1);
    face_write_pair(pair_at, 4, 2);

    next_offset = fdps_icon_script_set_unit_facing(face_script, 7);

    CHECK_EQ(face_units[4].facing, 2);
    CHECK_EQ(next_offset, 12);
}

/* XOR EAX,EAX / MOV AL,byte ptr [EDX+0x2]: a unit count with its top bit set
   is 128, not -128.  A signed decode would walk no unit at all and hand the
   interpreter back a position 253 bytes BEHIND this opcode.  Every pair is
   left zero, so all 128 of them name unit 0 and ask for a facing of 0 -- what
   is being pinned is the decode and the return arithmetic, and unit 0 having
   been turned says the loop ran at all. */
static void face_the_unit_count_operand_is_unsigned(void)
{
    int next_offset;

    face_stage();
    face_write_header(0, 0, 0x80);

    next_offset = fdps_icon_script_set_unit_facing(face_script, 0);

    CHECK_EQ(next_offset, 3 + 2 * 0x80);
    CHECK_EQ(face_units[0].facing, 0);
}

/* The hold loop presents frames: with a hold operand of 2 the frame latch no
   longer holds the sentinel staged into it, and the facings the turning loop
   wrote before the first frame are still the ones in the records afterwards --
   nothing inside the hold loop touches a unit. */
static void face_a_nonzero_hold_presents_frames(void)
{
    int next_offset;
    int pair_at;

    face_stage();
    pair_at = face_write_header(0, 2, 1);
    face_write_pair(pair_at, 1, 3);

    next_offset = face_run(0);

    CHECK_EQ(data_fdps_view_frame_last_tick == FACE_LATCH_SENTINEL, 0);
    CHECK_EQ(face_units[1].facing, 3);
    CHECK_EQ(next_offset, 5);
}

/* MOV dword ptr [0x00069cd0],0x1 and MOV byte ptr [0x00060159],0x1 on the exit
   path are stores of literals, not restores: both globals were staged at 4 and
   both come back 1.  A save-and-restore pair -- the obvious spelling of "hide
   the HUD for the duration" -- would leave 4 in each and carry an
   area-of-effect cursor mode straight through the opcode
   (rebuild_info/pitfalls.md). */
static void face_the_hud_globals_end_at_one_not_where_they_started(void)
{
    face_stage();
    face_write_header(0, 0, 0);

    fdps_icon_script_set_unit_facing(face_script, 0);

    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
    CHECK_EQ(data_fdps_ui_play_active_flag, 1);
}

/* ---------------------------------------------------------------------------
 * fdps_icon_script_blink_units_out -- 000221e0, script opcode 9.
 *
 * WHAT IS OBSERVABLE.  The handler's whole output is the flags byte of every
 * listed unit's record, the two HUD globals it writes on the way in and out,
 * and the offset it returns.  The alternation itself is not observable from
 * outside: each phase overwrites the byte the phase before it wrote, and the
 * only thing that ever sees an intermediate value is fdps_draw_map_unit inside
 * a rendered frame.  What the cases below pin is the state the run ENDS in,
 * which is where the phase count's parity shows up -- an eighth phase, or a
 * counter started at 0, would leave 0 in the byte and the units on the map.
 *
 * EVERY CASE RENDERS.  Unlike the walk and turn handlers there is no operand
 * that can zero the frame loop: seven phases of three frames run on every
 * call, so every case here goes through blink_run and hooks IRQ0 the way the
 * walk cases above do.  fdps_render_view_frame's wait ends only when
 * data_fdps_timer_tick_counter moves and nothing advances that counter in a
 * test image.
 *
 * Expected values come from the assembly: XOR EAX,EAX / MOV AL,byte ptr
 * [EDX+0x1] for the unsigned count and ADD dword ptr [EBP+0x18],0x2 for the
 * two-byte header it sits in; MOV EAX,[EBP+0x18] / ADD EAX,[EBP-0x18] with no
 * scaling for the one-byte-per-unit list; MOV [EBP-0x10],0x1 with CMP against
 * 0x8 and JL for the seven phases; MOV AL,byte ptr [EBP-0x10] / AND AL,0x1 /
 * MOV byte ptr [EDX+0x5],AL at 0002226b for the whole-byte assignment of the
 * phase's low bit; CMP dword ptr [EBP-0x18],0x3 / JL around the bare CALL
 * 0x0002beb0 for the three frames a phase is held; MOV dword ptr
 * [0x00069cd0],0x1 and MOV byte ptr [0x00060159],0x1 at 00022290 for the exit
 * constants; and MOV EAX,[EBP+0x18] / ADD EAX,[EBP-0x14] at 000222a1 for the
 * returned offset.  None is read off the emitted C.
 *
 * WHAT IS NOT ASSERTED.  That the units are visible on the even phases and
 * hidden on the odd ones, and that each phase lasts three frames rather than
 * one or ten, are playtest contracts (rebuild_info/pitfalls.md): both are
 * properties of what is on screen partway through a call that returns only
 * once the flash is over.
 */

/* Records staged for the battle unit array, and the script image the operand
   and the unit list are written into.  The image is large enough for the
   biggest list a one-byte count can name, 2 + 255 bytes. */
#define BLINK_UNITS 5
#define BLINK_SCRIPT_BYTES 300

/* Staged into every record's flags byte before a run.  Every bit set, so a
   handler that only edited bit 0 would leave 0xff or 0xfe behind and a
   handler that assigns the whole byte leaves 1.  Bit 7 is the acted-this-turn
   flag, which is exactly the one the assignment destroys. */
#define BLINK_STAGED_FLAGS 0xff

/* Staged into the frame latch before a run.  A value the timer's tick counter
   will not be sitting on, so the latch having moved says frames were
   presented. */
#define BLINK_LATCH_SENTINEL 0x5a5a5a5aUL

/* Staged into the two HUD globals before a run, so that finding 1 in them
   afterwards says the handler wrote the exit constants rather than put back
   what it found. */
#define BLINK_STAGED_CURSOR_MODE 4
#define BLINK_STAGED_PLAY_FLAG 4

static struct fdps_unit_record blink_units[BLINK_UNITS];
static unsigned char blink_script[BLINK_SCRIPT_BYTES];

/* Points the battle unit array at the staged records, sets every flags byte to
   all ones, stages the frame latch and switches off everything a rendered
   frame would otherwise paint. */
static void blink_stage(void)
{
    int unit;

    memset(blink_units, 0, sizeof(blink_units));
    for (unit = 0; unit < BLINK_UNITS; unit++) {
        blink_units[unit].flags = BLINK_STAGED_FLAGS;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) blink_units;

    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;

    data_fdps_view_frame_last_tick = (unsigned int) BLINK_LATCH_SENTINEL;

    data_fdps_map_cursor_draw_mode = BLINK_STAGED_CURSOR_MODE;
    data_fdps_ui_play_active_flag = BLINK_STAGED_PLAY_FLAG;

    memset(blink_script, 0, sizeof(blink_script));
}

/* Writes the opcode and its one operand at offset, and returns the offset the
   first unit index goes at. */
static int blink_write_header(int offset, int unit_count)
{
    blink_script[offset] = 9;
    blink_script[offset + 1] = (unsigned char) unit_count;
    return offset + 2;
}

/* One whole run with the frame wait serviced.  Every case needs it: the frame
   loop is not operand-controlled, so twenty-one frames run whatever the script
   says. */
static int blink_run(int offset)
{
    int next_offset;

    walk_saved_timer = _dos_getvect(WALK_TIMER_VECTOR);
    _dos_setvect(WALK_TIMER_VECTOR, walk_timer_isr);
    next_offset = fdps_icon_script_blink_units_out(blink_script, offset);
    _dos_setvect(WALK_TIMER_VECTOR, walk_saved_timer);

    return next_offset;
}

/* The phase counter runs 1..7 and the last phase writes its low bit, so the
   run ends with the retired flag set on every listed unit.  A loop that ran an
   eighth phase, or started at 0, would end on an even phase and leave 0 here --
   the units back on the map, which is what a reading of this opcode as a
   decorative blink produces (rebuild_info/pitfalls.md).  Unit 3 is not listed
   and keeps the staged byte, which says the handler walks the list rather than
   the whole array. */
static void blink_the_listed_units_end_retired(void)
{
    int list_at;
    int next_offset;

    blink_stage();
    list_at = blink_write_header(0, 2);
    blink_script[list_at] = 1;
    blink_script[list_at + 1] = 4;

    next_offset = blink_run(0);

    CHECK_EQ(blink_units[1].flags, 1);
    CHECK_EQ(blink_units[4].flags, 1);
    CHECK_EQ(blink_units[3].flags, BLINK_STAGED_FLAGS);
    CHECK_EQ(next_offset, 4);
}

/* MOV byte ptr [EDX+0x5],AL assigns the whole byte: the record went in with
   every flag set and comes out holding 1, not 0xff and not 0x81.  Writing the
   obvious `unit->flags |= 1` -- which is what the single-unit retire opcode in
   fdps_icon_script_run does to the same byte -- would preserve bit 7, the
   acted-this-turn flag, and a unit flashed out here and later un-retired would
   come back with a different turn state than the original leaves it
   (rebuild_info/pitfalls.md). */
static void blink_the_whole_flags_byte_is_assigned(void)
{
    int list_at;

    blink_stage();
    list_at = blink_write_header(0, 1);
    blink_script[list_at] = 2;

    blink_run(0);

    CHECK_EQ(blink_units[2].flags, 1);
}

/* The list index is added to the offset unscaled -- ADD EAX,[EBP-0x18] with no
   ADD EAX,EAX in front of it -- so the indices are one byte apart, not two.
   Reading them as the two-byte pairs the walk and turn opcodes take would pick
   up unit 0 as the second entry here and hand back an offset four bytes too
   far. */
static void blink_the_list_is_one_byte_per_unit(void)
{
    int list_at;
    int next_offset;

    blink_stage();
    list_at = blink_write_header(0, 3);
    blink_script[list_at] = 1;
    blink_script[list_at + 1] = 2;
    blink_script[list_at + 2] = 3;

    next_offset = blink_run(0);

    CHECK_EQ(blink_units[1].flags, 1);
    CHECK_EQ(blink_units[2].flags, 1);
    CHECK_EQ(blink_units[3].flags, 1);
    CHECK_EQ(blink_units[0].flags, BLINK_STAGED_FLAGS);
    CHECK_EQ(next_offset, 5);
}

/* The count is at offset + 1 and the list starts at offset + 2, neither of
   which is offset + 0: the opcode byte and the byte just past the list are
   both set to unit indices that would give a very different run if either were
   picked up by mistake. */
static void blink_the_operand_follows_the_opcode_byte(void)
{
    int list_at;
    int next_offset;

    blink_stage();
    blink_script[10] = 3;
    list_at = blink_write_header(7, 1);
    blink_script[list_at] = 4;

    next_offset = blink_run(7);

    CHECK_EQ(blink_units[4].flags, 1);
    CHECK_EQ(blink_units[3].flags, BLINK_STAGED_FLAGS);
    CHECK_EQ(next_offset, 10);
}

/* XOR EAX,EAX / MOV AL,byte ptr [EDX+0x1]: a count with its top bit set is
   128, not -128.  A signed decode would list no unit at all and hand the
   interpreter back a position 126 bytes BEHIND this opcode.  Every list byte
   is left zero, so all 128 entries name unit 0 -- what is being pinned is the
   decode and the return arithmetic, and unit 0 having been retired says the
   loop ran at all. */
static void blink_the_unit_count_operand_is_unsigned(void)
{
    int next_offset;

    blink_stage();
    blink_write_header(0, 0x80);

    next_offset = blink_run(0);

    CHECK_EQ(next_offset, 2 + 0x80);
    CHECK_EQ(blink_units[0].flags, 1);
}

/* A count of 0 lists nobody, so no record is touched -- but the phase loop and
   its frame loop are not guarded by the count, so the flash still runs its
   twenty-one frames and the latch still moves off the sentinel.  A frame loop
   hoisted under the unit loop, or skipped on an empty list, would leave the
   sentinel in place and would take the opcode's on-screen time to nothing. */
static void blink_an_empty_list_still_renders(void)
{
    int next_offset;

    blink_stage();
    blink_write_header(0, 0);

    next_offset = blink_run(0);

    CHECK_EQ(blink_units[0].flags, BLINK_STAGED_FLAGS);
    CHECK_EQ(data_fdps_view_frame_last_tick == BLINK_LATCH_SENTINEL, 0);
    CHECK_EQ(next_offset, 2);
}

/* MOV dword ptr [0x00069cd0],0x1 and MOV byte ptr [0x00060159],0x1 at 00022290
   are stores of literals, not restores: both globals were staged at 4 and both
   come back 1.  A save-and-restore pair -- the obvious spelling of "hide the
   HUD for the duration" -- would leave 4 in each and carry an area-of-effect
   cursor mode straight through the opcode (rebuild_info/pitfalls.md). */
static void blink_the_hud_globals_end_at_one_not_where_they_started(void)
{
    blink_stage();
    blink_write_header(0, 0);

    blink_run(0);

    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
    CHECK_EQ(data_fdps_ui_play_active_flag, 1);
}

/* ---------------------------------------------------------------------------
 * 000224f0, script opcode 0x10: the scripted view shake.
 *
 * WHAT CAN BE ASSERTED FROM OUTSIDE, AND HOW.  The handler puts the view
 * origin back before it returns, so nothing a case reads after the call can
 * see what the origin held during a step.  The steps are observed from inside
 * instead: every rendered frame ends in fdps_render_view_frame's wait on
 * data_fdps_timer_tick_counter, and in a test image the only thing that moves
 * that counter is the IRQ0 handler installed here -- so the handler is
 * guaranteed to run at least once inside every frame, and it records the view
 * origin it finds.  Duplicate fires within one step record nothing new because
 * the recorder only stores a pair that differs from the one before it, which
 * makes the recorded list the distinct sequence of origins the effect passed
 * through.  data_fdps_view_frame_last_tick is staged equal to the tick counter
 * so that the very first frame waits too; left alone it could find the two
 * already unequal and pass straight through without a fire.
 *
 * Expected values come from the assembly: XOR EAX,EAX / MOV AL,byte ptr
 * [EDX+0x1] and [EDX+0x2] for the two unsigned header operands with ADD dword
 * ptr [EBP+0x18],0x3 for the header they sit in; CMP dword ptr [EBP-0x10],0x7f
 * / JLE round ADD dword ptr [EBP-0x10],0xffffff00 for each pair byte's sign
 * fold, and the same shape at 00022581 for the y; ADD dword ptr [EBP+0x18],0x2
 * for the pair stride; MOV EAX,[EBP-0x28] / ADD EAX,[EBP-0x10] / MOV
 * [0x00069ce4],EAX at 00022592 and the matching y store at 0002259d, both
 * reloading the origin saved at 000224fc and 00022504; the frame loop at
 * 000225af bounded by [EBP-0x20] with CALL 0x0002beb0 as its whole body; the
 * plain stores of the saved origin back at 000225ca and the byte restore MOV
 * AL,byte ptr [EBP-0x8] / MOV [0x00060159],AL at 000225da; and MOV EAX,[EBP
 * +0x18] as the return.  None is read off the emitted C.
 *
 * WHAT IS NOT ASSERTED.  The frames-per-step operand's unsigned decode has no
 * cheap observable -- a top-bit-set value there is at least 128 timer ticks a
 * step -- so only the step-count operand's decode is exercised from the
 * outside and the frame count rests on the identical XOR EAX,EAX / MOV AL
 * sequence.  That each held frame straddles a vertical retrace is a playtest
 * contract (rebuild_info/pitfalls.md), not a unit-test one.
 *
 * The view origin globals are ordinary ints that ticket 23 has not filled in
 * yet, so each case stages the origin itself and asserts only against what it
 * staged.
 */

/* Room for a step count with its top bit set: 0x82 steps is 260 pair bytes on
   top of the three header bytes. */
#define SHAKE_SCRIPT_BYTES 320

/* Where the shake cases put the view before they run, and the values staged
   into the two HUD globals.  Neither HUD value is one the handler produces, so
   finding them again says it left them alone rather than wrote them. */
#define SHAKE_HOME_X 100
#define SHAKE_HOME_Y 60
#define SHAKE_STAGED_CURSOR_MODE 4
#define SHAKE_STAGED_PLAY_FLAG 4

/* A frame-latch value no timer tick will be sitting on, so the latch still
   holding it says no frame was presented. */
#define SHAKE_LATCH_SENTINEL 0x5a5a5a5aUL

/* How many distinct origins the recorder below will hold. */
#define SHAKE_SEEN_MAX 8

static unsigned char shake_script[SHAKE_SCRIPT_BYTES];

static volatile int shake_seen_x[SHAKE_SEEN_MAX];
static volatile int shake_seen_y[SHAKE_SEEN_MAX];
static volatile int shake_seen_count;
static volatile int shake_last_x;
static volatile int shake_last_y;

/* IRQ0 for the shake cases: moves the tick counter the frame wait spins on,
   and on the way records the view origin the handler currently has set.  A
   pair equal to the home position is never recorded, so neither a fire before
   the first step nor one in the few instructions between the handler's restore
   and the vector being put back can add an entry; and a pair equal to the last
   one recorded is never recorded twice, so extra fires inside one step are
   ignored.  What is left is the distinct sequence of displaced origins. */
static void __interrupt __far shake_timer_isr(void)
{
    int live_x;
    int live_y;

    live_x = data_fdps_battle_view_window_origin_x;
    live_y = data_fdps_battle_view_window_origin_y;
    if ((live_x != SHAKE_HOME_X || live_y != SHAKE_HOME_Y)
        && (live_x != shake_last_x || live_y != shake_last_y)) {
        if (shake_seen_count < SHAKE_SEEN_MAX) {
            shake_seen_x[shake_seen_count] = live_x;
            shake_seen_y[shake_seen_count] = live_y;
            shake_seen_count++;
        }
        shake_last_x = live_x;
        shake_last_y = live_y;
    }

    ++data_fdps_timer_tick_counter;
    _chain_intr(walk_saved_timer);
}

/* Puts the view at its home position, arms the recorder against that position
   so a fire before the first step records nothing, stages the frame latch and
   the two HUD globals, and switches off everything a rendered frame would
   otherwise paint. */
static void shake_stage(void)
{
    data_fdps_battle_view_window_origin_x = SHAKE_HOME_X;
    data_fdps_battle_view_window_origin_y = SHAKE_HOME_Y;

    shake_seen_count = 0;
    shake_last_x = SHAKE_HOME_X;
    shake_last_y = SHAKE_HOME_Y;

    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;

    data_fdps_view_frame_last_tick = (unsigned int) SHAKE_LATCH_SENTINEL;

    data_fdps_map_cursor_draw_mode = SHAKE_STAGED_CURSOR_MODE;
    data_fdps_ui_play_active_flag = SHAKE_STAGED_PLAY_FLAG;

    memset(shake_script, 0, sizeof(shake_script));
}

/* Writes the opcode and its two operands at offset, and returns the offset the
   first displacement pair goes at. */
static int shake_write_header(int offset, int frames_per_step, int step_count)
{
    shake_script[offset] = 0x10;
    shake_script[offset + 1] = (unsigned char) frames_per_step;
    shake_script[offset + 2] = (unsigned char) step_count;
    return offset + 3;
}

/* One displacement pair, written as the raw bytes the script carries. */
static void shake_write_pair(int pair_at, int raw_x, int raw_y)
{
    shake_script[pair_at] = (unsigned char) raw_x;
    shake_script[pair_at + 1] = (unsigned char) raw_y;
}

/* One whole run with the frame wait serviced and the origin recorded.  Only
   the cases with a non-zero frame count need it; a run that holds no frames
   calls the handler directly. */
static int shake_run(int offset)
{
    int next_offset;

    data_fdps_view_frame_last_tick = data_fdps_timer_tick_counter;
    walk_saved_timer = _dos_getvect(WALK_TIMER_VECTOR);
    _dos_setvect(WALK_TIMER_VECTOR, shake_timer_isr);
    next_offset = fdps_icon_script_animate_view_offset(shake_script, offset);
    _dos_setvect(WALK_TIMER_VECTOR, walk_saved_timer);

    return next_offset;
}

/* The header is read at offset + 1 and offset + 2 and the pairs start at
   offset + 3, so the returned position is offset + 3 + 2 * step_count.  The
   opcode byte and the byte past the list are both set to values that would
   give a wildly different answer if either were picked up as an operand. */
static void shake_the_operands_follow_the_opcode_byte(void)
{
    int pair_at;
    int next_offset;

    shake_stage();
    shake_script[5] = 99;
    pair_at = shake_write_header(5, 0, 2);
    shake_write_pair(pair_at, 1, 2);
    shake_write_pair(pair_at + 2, 3, 4);
    shake_script[12] = 77;

    next_offset = fdps_icon_script_animate_view_offset(shake_script, 5);

    CHECK_EQ(pair_at, 8);
    CHECK_EQ(next_offset, 12);
}

/* XOR EAX,EAX / MOV AL,byte ptr [EDX+0x2]: a step count with its top bit set
   is 130 steps, so 260 pair bytes are consumed and the position advances by
   263.  A signed decode would make it -126, skip the loop and return 3,
   leaving the interpreter to resume in the middle of the pair list. */
static void shake_the_step_count_operand_is_unsigned(void)
{
    int next_offset;

    shake_stage();
    shake_write_header(0, 0, 0x82);

    next_offset = fdps_icon_script_animate_view_offset(shake_script, 0);

    CHECK_EQ(next_offset, 263);
}

/* The origin stores at 000225ca and 000225d2 are the values saved at 000224fc
   and 00022504, so however far the steps displaced the view it ends where it
   began.  The displacements here are large enough that a missing restore would
   be unmistakable. */
static void shake_the_origin_ends_where_it_started(void)
{
    int pair_at;

    shake_stage();
    pair_at = shake_write_header(0, 0, 2);
    shake_write_pair(pair_at, 40, 50);
    shake_write_pair(pair_at + 2, 0xc0, 0xd0);

    fdps_icon_script_animate_view_offset(shake_script, 0);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, SHAKE_HOME_X);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, SHAKE_HOME_Y);
}

/* MOV AL,byte ptr [EBP-0x8] / MOV [0x00060159],AL puts back the byte saved at
   0002252e, where the walk, turn and blink handlers all store the literal 1
   over it; and nothing in this function writes 0x00069cd0 at all.  Both
   globals were staged at 4 and both come back 4.  Writing the family uniformly
   would make the info panel reappear after a cut-scene that had hidden it, and
   would reset a cursor mode the opcode never touched
   (rebuild_info/pitfalls.md). */
static void shake_the_hud_globals_are_left_as_they_were_found(void)
{
    int pair_at;

    shake_stage();
    pair_at = shake_write_header(0, 0, 1);
    shake_write_pair(pair_at, 4, 4);

    fdps_icon_script_animate_view_offset(shake_script, 0);

    CHECK_EQ(data_fdps_ui_play_active_flag, SHAKE_STAGED_PLAY_FLAG);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, SHAKE_STAGED_CURSOR_MODE);
}

/* The frame loop at 000225af is bounded by the frames-per-step operand, so a
   count of 0 presents nothing: the latch keeps its sentinel through four steps
   of origin writes.  The position still advances past the whole pair list. */
static void shake_no_held_frames_presents_nothing(void)
{
    int pair_at;
    int next_offset;

    shake_stage();
    pair_at = shake_write_header(0, 0, 4);
    shake_write_pair(pair_at, 1, 1);
    shake_write_pair(pair_at + 2, 2, 2);
    shake_write_pair(pair_at + 4, 3, 3);
    shake_write_pair(pair_at + 6, 4, 4);

    next_offset = fdps_icon_script_animate_view_offset(shake_script, 0);

    CHECK_EQ(data_fdps_view_frame_last_tick == SHAKE_LATCH_SENTINEL, 1);
    CHECK_EQ(next_offset, 11);
}

/* The frame loop is nested inside the step loop -- CALL 0x0002beb0 sits at
   000225c1, inside the body the step counter at [EBP-0x1c] bounds -- so a step
   count of 0 renders nothing however many frames each step asks for, and the
   position advances by the three header bytes alone.  A frame loop hoisted out
   to run once per opcode would burn two hundred timer ticks here. */
static void shake_a_zero_step_count_renders_nothing(void)
{
    int next_offset;

    shake_stage();
    shake_write_header(0, 200, 0);

    next_offset = fdps_icon_script_animate_view_offset(shake_script, 0);

    CHECK_EQ(data_fdps_view_frame_last_tick == SHAKE_LATCH_SENTINEL, 1);
    CHECK_EQ(next_offset, 3);
}

/* The heart of the opcode, watched from inside the frames it presents.  Each
   step's pair is added to the origin saved on entry, so the ramp
   -6, -12, -18, -24 from x = 100 walks 94, 88, 82, 76; a loop that added each
   pair to the running origin -- which is what "shake the view by these
   amounts" reads like -- would walk 94, 82, 64, 40 instead
   (rebuild_info/pitfalls.md).  The same four frames pin the pair bytes' sign
   fold on both axes: 0xfa is -6 and not 250, and the y column mixes +5 with
   -10 so a decode that dropped the fold, or applied it to the wrong axis,
   moves every recorded value. */
static void shake_each_step_is_measured_from_the_origin_saved_on_entry(void)
{
    int pair_at;
    int next_offset;

    shake_stage();
    pair_at = shake_write_header(0, 1, 4);
    shake_write_pair(pair_at, 0xfa, 5);
    shake_write_pair(pair_at + 2, 0xf4, 0xf6);
    shake_write_pair(pair_at + 4, 0xee, 15);
    shake_write_pair(pair_at + 6, 0xe8, 0xec);

    next_offset = shake_run(0);

    CHECK_EQ(shake_seen_count, 4);
    CHECK_EQ(shake_seen_x[0], SHAKE_HOME_X - 6);
    CHECK_EQ(shake_seen_y[0], SHAKE_HOME_Y + 5);
    CHECK_EQ(shake_seen_x[1], SHAKE_HOME_X - 12);
    CHECK_EQ(shake_seen_y[1], SHAKE_HOME_Y - 10);
    CHECK_EQ(shake_seen_x[2], SHAKE_HOME_X - 18);
    CHECK_EQ(shake_seen_y[2], SHAKE_HOME_Y + 15);
    CHECK_EQ(shake_seen_x[3], SHAKE_HOME_X - 24);
    CHECK_EQ(shake_seen_y[3], SHAKE_HOME_Y - 20);
    CHECK_EQ(data_fdps_battle_view_window_origin_x, SHAKE_HOME_X);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, SHAKE_HOME_Y);
    CHECK_EQ(next_offset, 11);
}

/* One step held for two frames is still one displacement: the origin is
   written once per step, above the frame loop rather than inside it, so the
   recorder sees a single distinct pair however long the step is held.  A
   handler that displaced per frame would record two. */
static void shake_a_held_step_displaces_once(void)
{
    int pair_at;

    shake_stage();
    pair_at = shake_write_header(0, 2, 1);
    shake_write_pair(pair_at, 7, 0xf9);

    shake_run(0);

    CHECK_EQ(shake_seen_x[0], SHAKE_HOME_X + 7);
    CHECK_EQ(shake_seen_y[0], SHAKE_HOME_Y - 7);
    CHECK_EQ(shake_seen_count, 1);
}

/* ---- fdps_icon_script_prompt_three_way_choice, 00022600 -----------------
 *
 * Expected values come from the assembly: MOV dword ptr [EBP-0xc],0x3 at
 * 0002260c for the preloaded answer, the two CMP dword ptr [EBP-0x8],0x0 /
 * JNZ at 0002264d and 000226bc for the branch tests, MOV dword ptr
 * [EBP-0xc],0x1 at 00022676 and 0x2 at 000226e5 for the two answers that are
 * written, the PUSH 0x10, 0x11, 0x13, 0x14, 0x15 and 0x12 in front of the six
 * fdps_draw_text calls, and the PUSH 0xaa44a in front of the two that follow
 * a panel reveal against the PUSH 0xa0000 in front of the other four.  None
 * of them is read off the emitted C.
 *
 * WHAT THE FUNCTION IS OBSERVED THROUGH.  It takes no arguments, touches no
 * global of its own and returns one int, so the two observables are that
 * return value and what its six text draws leave on the visible page.  The
 * whole run is therefore made in mode 13h with a timer interrupt playing the
 * player's keys, the frame is captured out of the aperture afterwards, and
 * the adapter is back in text mode before the first CHECK_EQ so a failure
 * prints on a readable screen.
 *
 * HOW THE KEYS ARE PLAYED, and why this feeder is not the one tests/msgwin.c
 * uses.  A run here contains up to two prompts, and every prompt begins with
 * fdps_flush_keyboard_queue, which stores the read index into the WRITE index
 * (keybd.h): a code appended between the two prompts is thrown away, and the
 * whole window between them -- two panel slides, twelve retrace-paced 64000
 * byte presents -- is many timer ticks long, so a feeder that appended one
 * code per tick would lose the second prompt's key and hang.  The handler
 * installed below therefore appends a code only while the ring is EMPTY and
 * advances through the case's list only when the read index has moved, which
 * only fdps_read_keyboard_queue moves.  A code a flush discarded is simply
 * appended again on the next tick, and the count of read indexes seen is
 * exactly how many keys the run consumed -- which is how a case says how many
 * prompts ran.  Past the end of the list the last code is held, so a run that
 * asks for one more key than the case staged fails an assertion instead of
 * spinning forever.
 *
 * HOW A TEXT DRAW IS MADE VISIBLE.  The font staged here is one 16-pixel,
 * one-row cell per glyph whose glyph n paints exactly column n, and the
 * chapter text block staged here gives entry 0x10 + n a stream of the single
 * glyph n and nothing else.  So a text id the routine drew at the screen
 * origin shows up as one pixel at row 0, column id - 0x10, and the ids it did
 * not draw leave their columns at the palette index 0 that
 * fdps_message_window_close's clean page put there.  Row 0 is inside the four
 * black rows above that page's 312 x 192 viewport, so what is in the viewport
 * -- nothing is staged into the scene -- cannot reach the columns the cases
 * read.  The two questions, ids 0x10 and 0x13, go to the panel cursor 0xaa44a
 * instead and are wiped by the close that follows them, so their columns
 * staying at 0 is what says they were not drawn at the screen origin.
 *
 * WHAT IS NOT ASSERTED.  Where inside the panel a question lands, what the
 * portrait is and what the two slides look like belong to
 * fdps_message_window_open, fdps_message_window_close and
 * fdps_prompt_two_choice and are covered against those functions in
 * tests/msgwin.c.  The colours the six draws pass -- 0xd0, 0 and 0x6d -- are
 * pinned only as "the pixel is the foreground colour": with the fixture's
 * outline flag clear and both shadow offsets 0 the outline colour lands on
 * the same pixels the foreground then overwrites, so it cannot be seen from
 * here.
 *
 * FACE.CEL HAS TO BE THERE.  Both panel reveals pass face 0x7a, and
 * fdps_message_window_open sends a non-negative face straight to
 * fdps_load_and_draw_portrait, which ends the process at exit(1) on a sheet
 * it cannot open rather than failing an assertion.  Every case below skips
 * itself when the sheet is not next to the executable.
 * ------------------------------------------------------------------ */

#define CHOICE_VGA_BASE 0x000a0000
#define CHOICE_MODE_TEXT 0x03
#define CHOICE_MODE_320X200X256 0x13
#define CHOICE_SCREEN_PITCH 320
#define CHOICE_SCREEN_ROWS 200
#define CHOICE_SCREEN_BYTES (CHOICE_SCREEN_PITCH * CHOICE_SCREEN_ROWS)

/* The portrait sheet the two reveals load their speaker out of. */
#define CHOICE_FACE_SHEET "FACE.CEL"

/* The make codes the prompt answers to, its three answers, and the timer
   vector the feeder is installed on. */
#define CHOICE_KEY_ESC 0x01
#define CHOICE_KEY_ENTER 0x1c
#define CHOICE_KEY_RIGHT 0x4d
#define CHOICE_TIMER_VECTOR 8

/* The three branches the routine can answer with. */
#define CHOICE_BRANCH_FIRST 1
#define CHOICE_BRANCH_SECOND 2
#define CHOICE_BRANCH_THIRD 3

/* The text block: entries 0 to 0x15, a shared empty stream for every id the
   routine must not ask for, and one single-glyph stream per id it does. */
#define CHOICE_TEXT_IDS 0x16
#define CHOICE_TEXT_FIRST_ID 0x10
#define CHOICE_TEXT_DRAWN_IDS 6
#define CHOICE_TEXT_EMPTY_AT 0x30
#define CHOICE_TEXT_STREAMS_AT 0x34
#define CHOICE_TEXT_STREAM_BYTES 4
#define CHOICE_TEXT_BLOCK_BYTES (CHOICE_TEXT_STREAMS_AT \
                                 + CHOICE_TEXT_DRAWN_IDS \
                                   * CHOICE_TEXT_STREAM_BYTES)
#define CHOICE_TEXT_END (-1)

/* The columns the six ids paint at row 0, one per id. */
#define CHOICE_COL_FIRST_QUESTION 0
#define CHOICE_COL_FIRST_TAKEN 1
#define CHOICE_COL_CLOSING 2
#define CHOICE_COL_SECOND_QUESTION 3
#define CHOICE_COL_SECOND_TAKEN 4
#define CHOICE_COL_THIRD_TAKEN 5

/* The colour a drawn glyph carries, and the colour an undrawn column keeps:
   the foreground the routine pushes, and the palette index
   fdps_message_window_close's zeroed page leaves behind. */
#define CHOICE_GLYPH_COLOR 0xd0
#define CHOICE_CLEAR_COLOR 0x00

/* The font fixture: sixteen glyphs of one 16-pixel row, two bytes each, with
   glyph n setting the bit for column n. */
#define CHOICE_FONT_GLYPHS 16
#define CHOICE_FONT_STRIDE 2
#define CHOICE_FONT_CELL_WIDTH 16
#define CHOICE_FONT_CELL_ROWS 1
#define CHOICE_FONT_ADVANCE 24
#define CHOICE_FONT_LINE_HEIGHT 2

/* The Message.cel stand-in: one 302 x 73 sprite encoded as five fill runs per
   row, because a fill run cannot be longer than 64 pixels. */
#define CHOICE_PANEL_W 302
#define CHOICE_PANEL_H 73
#define CHOICE_PANEL_FILL_MAX 64
#define CHOICE_PANEL_SEGMENTS 5
#define CHOICE_PANEL_LAST_SEGMENT_W 46
#define CHOICE_PANEL_ROW_BYTES (CHOICE_PANEL_SEGMENTS * 2)
#define CHOICE_PANEL_STREAM_AT 0x40
#define CHOICE_PANEL_SHEET_BYTES (CHOICE_PANEL_STREAM_AT \
                                  + CHOICE_PANEL_H * CHOICE_PANEL_ROW_BYTES)
#define CHOICE_PANEL_COLOR 0x40

/* The Shadow.cel stand-in the prompt draws its two option cells out of:
   fourteen 24 x 24 sprites, one fill run per row, sprite i filled with i. */
#define CHOICE_SHADOW_SPRITES 14
#define CHOICE_SHADOW_SPRITE_W 24
#define CHOICE_SHADOW_SPRITE_H 24
#define CHOICE_SHADOW_STREAM_BYTES (CHOICE_SHADOW_SPRITE_H * 2)
#define CHOICE_SHADOW_STREAM_AT 0x50
#define CHOICE_SHADOW_SHEET_BYTES (CHOICE_SHADOW_STREAM_AT \
                                   + CHOICE_SHADOW_SPRITES \
                                     * CHOICE_SHADOW_STREAM_BYTES)

/* The .CEL header fields both fixtures carry, and a table position neither
   reader may consult: both hardwire the table at 0x0f. */
#define CHOICE_CEL_TABLE_AT 0x0f
#define CHOICE_CEL_DECOY_TABLE_AT 0x100
#define CHOICE_CEL_VERSION 1
#define CHOICE_CEL_PIXEL_FORMAT 2

/* More keys than any case here plays, and fewer than the ring holds. */
#define CHOICE_KEYS_MAX 4

static unsigned char choice_panel_sheet[CHOICE_PANEL_SHEET_BYTES];
static unsigned char choice_shadow_sheet[CHOICE_SHADOW_SHEET_BYTES];
static unsigned char choice_text_block[CHOICE_TEXT_BLOCK_BYTES];
static unsigned char choice_font[CHOICE_FONT_GLYPHS * CHOICE_FONT_STRIDE];
static unsigned char choice_capture[CHOICE_SCREEN_BYTES];
static unsigned char choice_keys[CHOICE_KEYS_MAX];
static volatile int choice_key_count;
static volatile int choice_keys_read;
static volatile int choice_last_head;
static int choice_fixtures_staged = 0;
static void (__interrupt __far *choice_saved_timer)();

static void choice_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void choice_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* A signed 16-bit word at a byte position: a table offset and a stream token
   are both written this way, which is the whole of the block format. */
static void choice_put_word(unsigned char *block, int at, int value)
{
    *(short *) (block + at) = (short) value;
}

static int choice_sheet_present(void)
{
    FILE *probe;

    probe = fopen(CHOICE_FACE_SHEET, "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);
    return 1;
}

/* The .CEL header both fixtures share. */
static void choice_cel_header(unsigned char *sheet, int width, int height,
                              int sprites)
{
    sheet[0] = 'C';
    sheet[1] = 'E';
    sheet[2] = 'L';
    choice_u16(sheet, 0x03, CHOICE_CEL_VERSION);
    choice_u16(sheet, 0x05, CHOICE_CEL_DECOY_TABLE_AT);
    choice_u16(sheet, 0x07, (unsigned int) width);
    choice_u16(sheet, 0x09, (unsigned int) height);
    choice_u16(sheet, 0x0b, (unsigned int) sprites);
    choice_u16(sheet, 0x0d, CHOICE_CEL_PIXEL_FORMAT);
}

/* The two sheets, the font and the text block.  None of them changes between
   cases, so they are built once. */
static void choice_stage_fixtures(void)
{
    int row;
    int segment;
    int cursor;
    int run;
    int sprite;
    int stream_at;
    int glyph;
    int text_id;

    if (choice_fixtures_staged) {
        return;
    }
    choice_fixtures_staged = 1;

    memset(choice_panel_sheet, 0, (size_t) CHOICE_PANEL_SHEET_BYTES);
    choice_cel_header(choice_panel_sheet, CHOICE_PANEL_W, CHOICE_PANEL_H, 1);
    choice_u32(choice_panel_sheet, CHOICE_CEL_TABLE_AT,
               (unsigned long) CHOICE_PANEL_STREAM_AT);
    choice_u32(choice_panel_sheet, CHOICE_CEL_TABLE_AT + 4,
               (unsigned long) CHOICE_PANEL_SHEET_BYTES);
    for (row = 0; row < CHOICE_PANEL_H; row++) {
        cursor = CHOICE_PANEL_STREAM_AT + row * CHOICE_PANEL_ROW_BYTES;
        for (segment = 0; segment < CHOICE_PANEL_SEGMENTS; segment++) {
            if (segment == CHOICE_PANEL_SEGMENTS - 1) {
                run = CHOICE_PANEL_LAST_SEGMENT_W;
            } else {
                run = CHOICE_PANEL_FILL_MAX;
            }
            choice_panel_sheet[cursor] = (unsigned char) (run - 1);
            choice_panel_sheet[cursor + 1] = CHOICE_PANEL_COLOR;
            cursor += 2;
        }
    }

    memset(choice_shadow_sheet, 0, (size_t) CHOICE_SHADOW_SHEET_BYTES);
    choice_cel_header(choice_shadow_sheet, CHOICE_SHADOW_SPRITE_W,
                      CHOICE_SHADOW_SPRITE_H, CHOICE_SHADOW_SPRITES);
    for (sprite = 0; sprite < CHOICE_SHADOW_SPRITES; sprite++) {
        stream_at = CHOICE_SHADOW_STREAM_AT
                    + sprite * CHOICE_SHADOW_STREAM_BYTES;
        choice_u32(choice_shadow_sheet, CHOICE_CEL_TABLE_AT + sprite * 4,
                   (unsigned long) stream_at);
        for (row = 0; row < CHOICE_SHADOW_SPRITE_H; row++) {
            choice_shadow_sheet[stream_at + row * 2] =
                (unsigned char) (CHOICE_SHADOW_SPRITE_W - 1);
            choice_shadow_sheet[stream_at + row * 2 + 1] =
                (unsigned char) sprite;
        }
    }
    choice_u32(choice_shadow_sheet,
               CHOICE_CEL_TABLE_AT + CHOICE_SHADOW_SPRITES * 4,
               (unsigned long) CHOICE_SHADOW_SHEET_BYTES);

    memset(choice_font, 0, (size_t) sizeof(choice_font));
    for (glyph = 0; glyph < 8; glyph++) {
        choice_font[glyph * CHOICE_FONT_STRIDE] =
            (unsigned char) (0x80 >> glyph);
    }
    for (glyph = 8; glyph < CHOICE_FONT_GLYPHS; glyph++) {
        choice_font[glyph * CHOICE_FONT_STRIDE + 1] =
            (unsigned char) (0x80 >> (glyph - 8));
    }

    memset(choice_text_block, 0, (size_t) CHOICE_TEXT_BLOCK_BYTES);
    choice_put_word(choice_text_block, CHOICE_TEXT_EMPTY_AT, CHOICE_TEXT_END);
    for (text_id = 0; text_id < CHOICE_TEXT_IDS; text_id++) {
        choice_put_word(choice_text_block, text_id * 2, CHOICE_TEXT_EMPTY_AT);
    }
    for (glyph = 0; glyph < CHOICE_TEXT_DRAWN_IDS; glyph++) {
        stream_at = CHOICE_TEXT_STREAMS_AT + glyph * CHOICE_TEXT_STREAM_BYTES;
        choice_put_word(choice_text_block,
                        (CHOICE_TEXT_FIRST_ID + glyph) * 2, stream_at);
        choice_put_word(choice_text_block, stream_at, glyph);
        choice_put_word(choice_text_block, stream_at + 2, CHOICE_TEXT_END);
    }
}

/* Everything the three callees read that this file has to make definite: the
   font, the text block, the two sheets, an empty scene so the close's
   recomposition draws nothing, and village mode so the prompt lifts its
   backdrop off the visible page instead of composing one. */
static void choice_stage_globals(void)
{
    choice_stage_fixtures();

    data_fdps_font_glyph_width = (unsigned char) CHOICE_FONT_CELL_WIDTH;
    data_fdps_glyph_cell_height = (unsigned char) CHOICE_FONT_CELL_ROWS;
    data_fdps_font_glyph_stride_bytes = CHOICE_FONT_STRIDE;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = CHOICE_FONT_ADVANCE;
    data_fdps_font_line_height = CHOICE_FONT_LINE_HEIGHT;
    data_fdps_font_sheet_ptr = choice_font;

    data_fdps_current_chapter_text_ptr = choice_text_block;
    data_fdps_message_window_sheet_ptr = choice_panel_sheet;
    data_fdps_shadow_sprite_sheet_ptr = choice_shadow_sheet;

    data_fdps_village_mode_flag = 1;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_timer_tick_counter = 0;
}

static void choice_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* The player.  It advances the game's clock like the real timer handler and
   appends the case's next make code the way fdps_keyboard_isr does, but only
   while the ring is empty, and it steps through the list on the read index
   moving rather than on ticks -- see the note at the top of this section. */
static void __interrupt __far choice_timer_isr(void)
{
    int slot;
    int next_key;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_scancode_queue_head != choice_last_head) {
        choice_last_head = data_fdps_input_scancode_queue_head;
        choice_keys_read++;
    }

    if (data_fdps_input_scancode_queue_head
            == data_fdps_input_scancode_queue_write_index) {
        next_key = choice_keys_read;
        if (next_key >= choice_key_count) {
            next_key = choice_key_count - 1;
        }
        slot = data_fdps_input_scancode_queue_write_index;
        data_fdps_input_scancode_queue[slot] = choice_keys[next_key];
        slot++;
        if (slot == SCANCODE_QUEUE_LEN) {
            slot = 0;
        }
        data_fdps_input_scancode_queue_write_index = slot;
    }

    _chain_intr(choice_saved_timer);
}

/* One whole run: the adapter in the mode the game runs it in, the feeder
   installed, the frame captured out of the aperture and text mode back before
   anything is asserted.  Answers with what the routine returned. */
static int choice_run(void)
{
    int answer;

    choice_stage_globals();

    choice_keys_read = 0;
    choice_last_head = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;

    choice_set_mode(CHOICE_MODE_320X200X256);
    choice_saved_timer = _dos_getvect(CHOICE_TIMER_VECTOR);
    _dos_setvect(CHOICE_TIMER_VECTOR, choice_timer_isr);
    answer = fdps_icon_script_prompt_three_way_choice();
    _dos_setvect(CHOICE_TIMER_VECTOR, choice_saved_timer);
    memmove(choice_capture, (void *) CHOICE_VGA_BASE,
            (size_t) CHOICE_SCREEN_BYTES);
    choice_set_mode(CHOICE_MODE_TEXT);

    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
    data_fdps_village_mode_flag = 0;
    return answer;
}

/* Loads the keys this case plays, in the order the prompts read them. */
static void choice_stage_keys(int count, int first, int second, int third,
                              int fourth)
{
    choice_keys[0] = (unsigned char) first;
    choice_keys[1] = (unsigned char) second;
    choice_keys[2] = (unsigned char) third;
    choice_keys[3] = (unsigned char) fourth;
    choice_key_count = count;
}

/* What row 0 of the captured frame holds at one text id's column. */
static int choice_column(int column)
{
    return (int) choice_capture[column];
}

/* ---------------------------------------------------------------------- */

/* The left option at the first question is the only value either test
   matches, and it ends the routine there: MOV dword ptr [EBP-0xc],0x1 at
   00022676 and JMP 0x00022711 past the whole else arm.  One key is read, so
   the second question was never asked, and the ids drawn at the screen origin
   are 0x11 and then 0x12 -- neither question's id is among them, because both
   go to the panel cursor 0xaa44a and the close wipes them. */
static void choice_the_left_option_answers_the_first_branch(void)
{
    int answer;

    if (!choice_sheet_present()) {
        return;
    }
    choice_stage_keys(1, CHOICE_KEY_ENTER, 0, 0, 0);
    answer = choice_run();

    CHECK_EQ(answer, CHOICE_BRANCH_FIRST);
    CHECK_EQ(choice_keys_read, 1);
    CHECK_EQ(choice_column(CHOICE_COL_FIRST_TAKEN), CHOICE_GLYPH_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_CLOSING), CHOICE_GLYPH_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_SECOND_TAKEN), CHOICE_CLEAR_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_THIRD_TAKEN), CHOICE_CLEAR_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_FIRST_QUESTION), CHOICE_CLEAR_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_SECOND_QUESTION), CHOICE_CLEAR_COLOR);
}

/* The right option at the first question and the left at the second is the
   inner test's taken arm: MOV dword ptr [EBP-0xc],0x2 at 000226e5.  Three
   keys are read -- the arrow, the confirm, and the second question's confirm
   -- and the ids drawn at the origin are 0x14 and 0x12, with 0x11 absent
   because the first branch was not taken. */
static void choice_the_right_option_then_the_left_answers_the_second(void)
{
    int answer;

    if (!choice_sheet_present()) {
        return;
    }
    choice_stage_keys(3, CHOICE_KEY_RIGHT, CHOICE_KEY_ENTER,
                      CHOICE_KEY_ENTER, 0);
    answer = choice_run();

    CHECK_EQ(answer, CHOICE_BRANCH_SECOND);
    CHECK_EQ(choice_keys_read, 3);
    CHECK_EQ(choice_column(CHOICE_COL_SECOND_TAKEN), CHOICE_GLYPH_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_CLOSING), CHOICE_GLYPH_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_FIRST_TAKEN), CHOICE_CLEAR_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_THIRD_TAKEN), CHOICE_CLEAR_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_SECOND_QUESTION), CHOICE_CLEAR_COLOR);
}

/* The right option twice writes nothing at all: the inner else arm at
   000226ee draws 0x15 and falls through, so what comes back is the 3 the
   prologue put in the slot at 0002260c.  A body that assigned the answer in
   that arm as the other two do would be indistinguishable here, which is why
   the case that matters for the preload is the cancel one below. */
static void choice_the_right_option_twice_answers_the_third(void)
{
    int answer;

    if (!choice_sheet_present()) {
        return;
    }
    choice_stage_keys(4, CHOICE_KEY_RIGHT, CHOICE_KEY_ENTER,
                      CHOICE_KEY_RIGHT, CHOICE_KEY_ENTER);
    answer = choice_run();

    CHECK_EQ(answer, CHOICE_BRANCH_THIRD);
    CHECK_EQ(choice_keys_read, 4);
    CHECK_EQ(choice_column(CHOICE_COL_THIRD_TAKEN), CHOICE_GLYPH_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_CLOSING), CHOICE_GLYPH_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_FIRST_TAKEN), CHOICE_CLEAR_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_SECOND_TAKEN), CHOICE_CLEAR_COLOR);
}

/* A cancel is not a way out.  Esc makes fdps_prompt_two_choice answer -1, and
   the test at 0002264d is JNZ against 0, so -1 takes the same else arm the
   right option takes and the second question is asked.  A body that tested
   for the right option with == 1, or that returned early on -1, would answer
   1 or leave the dialogue here instead of 2. */
static void choice_a_cancel_at_the_first_question_asks_the_second(void)
{
    int answer;

    if (!choice_sheet_present()) {
        return;
    }
    choice_stage_keys(2, CHOICE_KEY_ESC, CHOICE_KEY_ENTER, 0, 0);
    answer = choice_run();

    CHECK_EQ(answer, CHOICE_BRANCH_SECOND);
    CHECK_EQ(choice_keys_read, 2);
    CHECK_EQ(choice_column(CHOICE_COL_SECOND_TAKEN), CHOICE_GLYPH_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_CLOSING), CHOICE_GLYPH_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_FIRST_TAKEN), CHOICE_CLEAR_COLOR);
}

/* The same at the second question, where the arm writes no answer at all:
   two cancels leave the slot holding the 3 it was preloaded with, and 0x15 is
   the line that goes with it.  This is the case that would catch a preload of
   0 or an answer written in the wrong arm. */
static void choice_a_cancel_at_both_questions_answers_the_third(void)
{
    int answer;

    if (!choice_sheet_present()) {
        return;
    }
    choice_stage_keys(2, CHOICE_KEY_ESC, CHOICE_KEY_ESC, 0, 0);
    answer = choice_run();

    CHECK_EQ(answer, CHOICE_BRANCH_THIRD);
    CHECK_EQ(choice_keys_read, 2);
    CHECK_EQ(choice_column(CHOICE_COL_THIRD_TAKEN), CHOICE_GLYPH_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_CLOSING), CHOICE_GLYPH_COLOR);
    CHECK_EQ(choice_column(CHOICE_COL_SECOND_TAKEN), CHOICE_CLEAR_COLOR);
}

void run_icon_tests(void)
{
    RUN_TEST(the_dac_reads_back_what_was_written);
    RUN_TEST(the_last_step_stops_sixty_below_the_master_palette);
    RUN_TEST(every_entry_takes_its_own_record_of_the_master_palette);
    RUN_TEST(the_ramp_runs_from_the_master_palette_not_the_live_dac);
    RUN_TEST(the_same_bias_is_applied_to_all_three_channels);
    RUN_TEST(the_fade_in_ends_holding_the_master_palette_unmodified);
    RUN_TEST(the_fade_in_gives_every_entry_its_own_record);
    RUN_TEST(the_fade_in_derives_from_the_master_palette_not_the_live_dac);
    RUN_TEST(the_fade_in_restores_what_the_fade_out_dimmed);
    RUN_TEST(a_target_the_view_already_sits_on_moves_nothing);
    RUN_TEST(the_operands_are_the_two_bytes_after_the_opcode);
    RUN_TEST(the_operand_bytes_are_unsigned);
    RUN_TEST(a_sub_tile_distance_on_x_jumps_straight_to_the_target);
    RUN_TEST(a_sub_tile_distance_on_y_jumps_straight_to_the_target);
    RUN_TEST(the_cursor_sits_one_tile_inside_the_new_view);
    RUN_TEST(walk_no_held_frames_moves_nothing);
    RUN_TEST(walk_one_tile_steps_each_unit_in_its_own_direction);
    RUN_TEST(walk_a_held_frame_does_not_commit_the_tile_again);
    RUN_TEST(walk_the_tile_count_is_how_far_the_group_goes);
    RUN_TEST(walk_the_pair_names_the_unit_by_index);
    RUN_TEST(walk_the_operands_follow_the_opcode_byte);
    RUN_TEST(walk_the_unit_count_operand_is_unsigned);
    RUN_TEST(walk_the_hud_globals_end_at_one_not_where_they_started);
    RUN_TEST(face_a_zero_hold_still_turns_every_listed_unit);
    RUN_TEST(face_the_pair_names_the_unit_by_index);
    RUN_TEST(face_the_facing_byte_is_stored_as_it_stands);
    RUN_TEST(face_the_operands_follow_the_opcode_byte);
    RUN_TEST(face_the_unit_count_operand_is_unsigned);
    RUN_TEST(face_a_nonzero_hold_presents_frames);
    RUN_TEST(face_the_hud_globals_end_at_one_not_where_they_started);
    RUN_TEST(blink_the_listed_units_end_retired);
    RUN_TEST(blink_the_whole_flags_byte_is_assigned);
    RUN_TEST(blink_the_list_is_one_byte_per_unit);
    RUN_TEST(blink_the_operand_follows_the_opcode_byte);
    RUN_TEST(blink_the_unit_count_operand_is_unsigned);
    RUN_TEST(blink_an_empty_list_still_renders);
    RUN_TEST(blink_the_hud_globals_end_at_one_not_where_they_started);
    RUN_TEST(shake_the_operands_follow_the_opcode_byte);
    RUN_TEST(shake_the_step_count_operand_is_unsigned);
    RUN_TEST(shake_the_origin_ends_where_it_started);
    RUN_TEST(shake_the_hud_globals_are_left_as_they_were_found);
    RUN_TEST(shake_no_held_frames_presents_nothing);
    RUN_TEST(shake_a_zero_step_count_renders_nothing);
    RUN_TEST(shake_each_step_is_measured_from_the_origin_saved_on_entry);
    RUN_TEST(shake_a_held_step_displaces_once);
    RUN_TEST(choice_the_left_option_answers_the_first_branch);
    RUN_TEST(choice_the_right_option_then_the_left_answers_the_second);
    RUN_TEST(choice_the_right_option_twice_answers_the_third);
    RUN_TEST(choice_a_cancel_at_the_first_question_asks_the_second);
    RUN_TEST(choice_a_cancel_at_both_questions_answers_the_third);
}
