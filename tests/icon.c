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
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
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
}
