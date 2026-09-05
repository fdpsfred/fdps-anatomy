/* statwin.c -- the battle unit status window.
 *
 * See statwin.h for the window's geometry and what a caller has to know.
 * The three drawing routines own no state: each works on the images it is
 * handed or loads for itself.  The window's event loop does own state -- the
 * tick it last drew a frame on, and the unit a village phase substitutes for
 * the one it was asked for -- and statwin.h declares both.  The panel drawer
 * is what writes the second of those.
 *
 * inp comes from <conio.h> and delay from <i86.h>, which is where Watcom
 * 10.0a declares them, and both are ordinary calls in the original rather
 * than an inline IN instruction: 00016e80 issues CALL 0003d4e4 and CALL
 * 0003d370.  Watcom only turns inp into an instruction when -oi is given, and
 * it is not in this build's flag set (rebuild_info/build_flags.md).
 */
#include <conio.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "audio.h"
#include "blit.h"
#include "gamedata.h"
#include "gauge.h"
#include "keybd.h"
#include "mapdraw.h"
#include "msgwin.h"
#include "palcycle.h"
#include "sprite.h"
#include "statwin.h"
#include "table.h"
#include "text.h"
#include "unit.h"
#include "vfs.h"

/* The VGA graphics aperture as a flat linear address, the mode 13h scanline
   pitch, the row count and the size of one whole frame.  All four are
   hard-coded in the original (PUSH 0xa0000, PUSH 0x140, 0xc8, PUSH 0xfa00)
   and stay literals here: 0xa0000 is where the display adapter answers, not
   the address of anything the linker places, so there is no symbol to
   reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8
#define VGA_SCREEN_BYTES 0xfa00

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit this file looks at. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* Every destination the panel drawer writes, as the byte offset from the
   surface's origin that the assembly adds (ADD EAX,0x98d8 and its eighteen
   companions between 0001643b and 00016811).  The pitch is the literal 0x140
   pushed beside each of them, so an offset is row * 320 + column and the two
   halves are worth writing down: the portrait cell is at row 10 column 161,
   the HP figures at row 122 columns 88 and 117, the MP figures at row 141 in
   the same two columns, the two gauges at rows 118 and 137 column 22, level
   and move at row 155 columns 62 and 118, the six-figure stat block at rows
   164, 173 and 182 in columns 56 and 112, the face at row 4 column 19, and
   the three names at row 5 column 201 and row 23 columns 192 and 263. */
#define PANEL_PITCH 0x140
#define PANEL_PORTRAIT_AT 0xd21
#define PANEL_HP_CURRENT_AT 0x98d8
#define PANEL_HP_MAX_AT 0x98f5
#define PANEL_MP_CURRENT_AT 0xb098
#define PANEL_MP_MAX_AT 0xb0b5
#define PANEL_LEVEL_AT 0xc1fe
#define PANEL_EXP_AT 0xcd38
#define PANEL_MOVE_AT 0xc236
#define PANEL_HP_GAUGE_AT 0x9396
#define PANEL_MP_GAUGE_AT 0xab56
#define PANEL_HIT_AT 0xe3b8
#define PANEL_EV_AT 0xd878
#define PANEL_DX_AT 0xcd70
#define PANEL_AP_AT 0xd8b0
#define PANEL_DP_AT 0xe3f0
#define PANEL_FACE_AT 0x513
#define PANEL_NAME_AT 0x709
#define PANEL_CLASS_AT 0x1d80
#define PANEL_RACE_AT 0x1dc7

/* The 24x24 sprite-cache cell the panel stamps at PANEL_PORTRAIT_AT: PUSH
   0x18 twice at 00016434, and mode 0 with operand 0 in the two zeroes pushed
   before them. */
#define PANEL_UNIT_CELL_SIZE 0x18

/* The field width each figure is given, as the fourth argument pushed to
   fdps_draw_number.  These are fixed widths and not maxima: a figure that
   does not fit is replaced by that many '?' glyphs (text.h). */
#define PANEL_HP_MP_DIGITS 4
#define PANEL_LEVEL_DIGITS 2
#define PANEL_MOVE_DIGITS 2
#define PANEL_STAT_DIGITS 3

/* The three values written into data_fdps_number_glyph_color_row, which is
   which of Number.cel's five colour rows the digits come out of: 0 the plain
   one, 1 for a stat a buff timer is raising, 3 for a current HP or MP that is
   below its maximum. */
#define NUMBER_COLOR_NORMAL 0
#define NUMBER_COLOR_BUFFED 1
#define NUMBER_COLOR_REDUCED 3

/* struct fdps_unit_record's status_timers[] slots the three buffs use, the
   same three fdps_unit_recompute_combat_stats applies (src/unit.c): slot 0
   multiplies ap, slot 1 multiplies dp, and slot 2 adds to dx and so to both
   hit and ev.  Which figures light up here is exactly which figures each of
   them changes. */
#define ATTACK_BUFF_TIMER_SLOT 0
#define DEFENSE_BUFF_TIMER_SLOT 1
#define DEXTERITY_BUFF_TIMER_SLOT 2

/* What record exp_carry holds for a unit that earns the player nothing, and
   the figure drawn in its place: CMP dword ptr [EBP+-0x2c],0xff / MOV
   ...,0x3e8 at 000163de.  A thousand does not fit PANEL_STAT_DIGITS, and that
   is the point -- see statwin.h. */
#define EXP_CARRY_NOT_PLAYER 0xff
#define EXP_NOT_PLAYER_FIGURE 1000

/* Which of the gauge sheet's three graphics each bar is filled from, and how
   many columns a full bar is: PUSH 0x1 and PUSH 0x2 into fdps_draw_gauge_bar,
   and the IMUL by 0x75 that scales the fill. */
#define PANEL_HP_GAUGE_BAR 1
#define PANEL_MP_GAUGE_BAR 2
#define PANEL_GAUGE_BAR_WIDTH 0x75

/* Where the three name blocks start in data_fdps_all_game_text_ptr: the
   character names at entry 1, the race names at 0x97 and the class names at
   0xa1.  Each base is the constant added to a record byte before the id is
   passed, so the class block covers 0xa1..0xc8 for the forty class codes
   0x00..0x27 that assets/classes.md lists, and the race block starts far
   enough before it to hold the race codes those records carry. */
#define TEXT_ID_FIRST_CHARACTER_NAME 1
#define TEXT_ID_FIRST_RACE_NAME 0x97
#define TEXT_ID_FIRST_CLASS_NAME 0xa1

/* The colours all three names are drawn in, pushed as literals at 000167a8
   and its two companions: the standard message colours. */
#define PANEL_TEXT_FG_COLOR 0xd0
#define PANEL_TEXT_BG_COLOR 0
#define PANEL_TEXT_OUTLINE_COLOR 0x6d

/* 00016300.  See statwin.h for what the caller has to supply and what the
   colour rules are.

   THE TWO GAUGE FILLS ARE WRITTEN OUT AND NOT CALLED, BECAUSE THE ASSEMBLY
   HAS NO CALL TO THE PROPORTIONAL FRONT END IN IT.  Both stretches carry a
   full copy of fdps_draw_gauge_bar_proportional's frame -- five argument
   temps copied into consecutive parameter-shaped slots at 00016586, the
   callee's body replayed under a uniform slot substitution, and the fill width
   copied back out of a result slot -- which is the inline-expansion
   fingerprint, and that function has no caller anywhere in the image because
   both of its uses were expanded (rebuild_info/build_flags.md).  Writing the
   arithmetic out is correct under ADR-0001: the two spellings behave
   identically, and spelling it as a call without an _inline declaration to
   expand it would put two CALLs here that the original does not have.

   The division is the signed one -- MOV EAX,EDX / SAR EDX,0x1f / IDIV at
   000165b8 -- and it rounds up, so one hit point left still shows a filled
   pixel.  A maximum of zero or less skips the divide entirely.

   THE FIELDS ARE READ ONCE, UP FRONT, AND THE RECORD IS READ AGAIN LATER.
   Seventeen values are copied into the frame before anything is drawn, but the
   three buff timers are read straight off the record between the draws
   (00016708 and its two companions reload the record pointer).  Nothing here
   can move the unit array, so the two are the same record either way.

   The widths are the record's: the four HP/MP words and the four combat stats
   and dx come in through MOVSX, sign extended, and every byte field comes in
   through XOR EAX,EAX / MOV AL, zero extended.  A negative HP therefore
   reaches both the figure and the gauge as a negative number, and a race or
   class byte of 0xff indexes 0x96 or 0xa0 entries past its block. */
void fdps_draw_unit_status_panel(int unit_index, unsigned char *dest)
{
    struct fdps_unit_record *unit;
    unsigned char *unit_cell_stream;
    int sprite_cache_slot;
    int hp_current;
    int hp_max;
    int mp_current;
    int mp_max;
    int level;
    int attack;
    int defense;
    int hit;
    int evade;
    int dexterity;
    int move;
    int char_id;
    int portrait_id;
    int clazz;
    int race;
    int exp_figure;
    int hp_fill_width;
    int mp_fill_width;

    if (data_fdps_village_mode_flag != 0) {
        data_fdps_village_status_window_unit_idx = unit_index;
    }

    unit = fdps_get_unit_record(unit_index);
    sprite_cache_slot = (int) unit->sprite_cache_slot;
    hp_current = (int) unit->hp_current;
    hp_max = (int) unit->hp_max;
    mp_current = (int) unit->mp_current;
    mp_max = (int) unit->mp_max;
    level = (int) unit->level;
    attack = (int) unit->ap;
    defense = (int) unit->dp;
    hit = (int) unit->hit;
    evade = (int) unit->ev;
    dexterity = (int) unit->dx_base;
    move = (int) unit->move;
    char_id = (int) unit->char_id;
    portrait_id = (int) unit->portrait_id;
    clazz = (int) unit->clazz;
    race = (int) unit->race;

    exp_figure = (int) unit->exp_carry;
    if (exp_figure == EXP_CARRY_NOT_PLAYER) {
        exp_figure = EXP_NOT_PLAYER_FIGURE;
    }

    /* Sprite 0 of a cache slot, reached the way every other reader reaches
       one: the slot table sits at the base of the block and each slot's entry
       is an offset from that same base. */
    if (data_fdps_village_mode_flag == 0) {
        unit_cell_stream = data_fdps_cel_sprite_cache_ptr
            + ((struct fdps_cel_cache_slot *) data_fdps_cel_sprite_cache_ptr)
                  [sprite_cache_slot].sprite_offset[0];
    } else {
        unit_cell_stream = data_fdps_cel_sprite_cache_ptr
            + ((struct fdps_cel_cache_slot *) data_fdps_cel_sprite_cache_ptr)
                  [unit_index].sprite_offset[0];
    }
    fdps_blit_dispatch(unit_cell_stream, dest + PANEL_PORTRAIT_AT,
                       PANEL_UNIT_CELL_SIZE, PANEL_UNIT_CELL_SIZE, PANEL_PITCH,
                       0, 0);

    /* No else on either compare: a current equal to its maximum leaves the
       colour row as the caller left it, and it is the store after the figure
       that puts it back to 0. */
    if (hp_current != hp_max) {
        data_fdps_number_glyph_color_row = NUMBER_COLOR_REDUCED;
    }
    fdps_draw_number(dest + PANEL_HP_CURRENT_AT, PANEL_PITCH, hp_current,
                     PANEL_HP_MP_DIGITS, 0);
    data_fdps_number_glyph_color_row = NUMBER_COLOR_NORMAL;
    fdps_draw_number(dest + PANEL_HP_MAX_AT, PANEL_PITCH, hp_max,
                     PANEL_HP_MP_DIGITS, 0);

    if (mp_current != mp_max) {
        data_fdps_number_glyph_color_row = NUMBER_COLOR_REDUCED;
    }
    fdps_draw_number(dest + PANEL_MP_CURRENT_AT, PANEL_PITCH, mp_current,
                     PANEL_HP_MP_DIGITS, 0);
    data_fdps_number_glyph_color_row = NUMBER_COLOR_NORMAL;
    fdps_draw_number(dest + PANEL_MP_MAX_AT, PANEL_PITCH, mp_max,
                     PANEL_HP_MP_DIGITS, 0);

    fdps_draw_number(dest + PANEL_LEVEL_AT, PANEL_PITCH, level,
                     PANEL_LEVEL_DIGITS, 0);
    fdps_draw_number(dest + PANEL_EXP_AT, PANEL_PITCH, exp_figure,
                     PANEL_STAT_DIGITS, 0);
    fdps_draw_number(dest + PANEL_MOVE_AT, PANEL_PITCH, move,
                     PANEL_MOVE_DIGITS, 0);

    if (hp_max <= 0) {
        hp_fill_width = 0;
    } else {
        hp_fill_width = (hp_current * PANEL_GAUGE_BAR_WIDTH + hp_max - 1)
                        / hp_max;
    }
    fdps_draw_gauge_bar(dest + PANEL_HP_GAUGE_AT, PANEL_PITCH,
                        PANEL_HP_GAUGE_BAR, hp_fill_width);

    if (mp_max <= 0) {
        mp_fill_width = 0;
    } else {
        mp_fill_width = (mp_current * PANEL_GAUGE_BAR_WIDTH + mp_max - 1)
                        / mp_max;
    }
    fdps_draw_gauge_bar(dest + PANEL_MP_GAUGE_AT, PANEL_PITCH,
                        PANEL_MP_GAUGE_BAR, mp_fill_width);

    /* One timer covers three figures here, because one buff moves all three:
       dx is what the buff raises and hit and ev are both derived from it. */
    if (unit->status_timers[DEXTERITY_BUFF_TIMER_SLOT] != 0) {
        data_fdps_number_glyph_color_row = NUMBER_COLOR_BUFFED;
    } else {
        data_fdps_number_glyph_color_row = NUMBER_COLOR_NORMAL;
    }
    fdps_draw_number(dest + PANEL_HIT_AT, PANEL_PITCH, hit,
                     PANEL_STAT_DIGITS, 0);
    fdps_draw_number(dest + PANEL_EV_AT, PANEL_PITCH, evade,
                     PANEL_STAT_DIGITS, 0);
    fdps_draw_number(dest + PANEL_DX_AT, PANEL_PITCH, dexterity,
                     PANEL_STAT_DIGITS, 0);

    if (unit->status_timers[ATTACK_BUFF_TIMER_SLOT] != 0) {
        data_fdps_number_glyph_color_row = NUMBER_COLOR_BUFFED;
    } else {
        data_fdps_number_glyph_color_row = NUMBER_COLOR_NORMAL;
    }
    fdps_draw_number(dest + PANEL_AP_AT, PANEL_PITCH, attack,
                     PANEL_STAT_DIGITS, 0);

    if (unit->status_timers[DEFENSE_BUFF_TIMER_SLOT] != 0) {
        data_fdps_number_glyph_color_row = NUMBER_COLOR_BUFFED;
    } else {
        data_fdps_number_glyph_color_row = NUMBER_COLOR_NORMAL;
    }
    fdps_draw_number(dest + PANEL_DP_AT, PANEL_PITCH, defense,
                     PANEL_STAT_DIGITS, 0);

    data_fdps_number_glyph_color_row = NUMBER_COLOR_NORMAL;

    fdps_load_and_draw_portrait(dest + PANEL_FACE_AT, PANEL_PITCH,
                                portrait_id);

    fdps_draw_text(data_fdps_all_game_text_ptr,
                   TEXT_ID_FIRST_CHARACTER_NAME + char_id,
                   dest + PANEL_NAME_AT, PANEL_PITCH, PANEL_TEXT_FG_COLOR,
                   PANEL_TEXT_BG_COLOR, PANEL_TEXT_OUTLINE_COLOR);
    fdps_draw_text(data_fdps_all_game_text_ptr,
                   TEXT_ID_FIRST_CLASS_NAME + clazz,
                   dest + PANEL_CLASS_AT, PANEL_PITCH, PANEL_TEXT_FG_COLOR,
                   PANEL_TEXT_BG_COLOR, PANEL_TEXT_OUTLINE_COLOR);
    fdps_draw_text(data_fdps_all_game_text_ptr,
                   TEXT_ID_FIRST_RACE_NAME + race,
                   dest + PANEL_RACE_AT, PANEL_PITCH, PANEL_TEXT_FG_COLOR,
                   PANEL_TEXT_BG_COLOR, PANEL_TEXT_OUTLINE_COLOR);
}

/* How many steps the slide-in has, and so how long each of the four offset
   tables is.  Both callers that open the window walk 0..8 (CMP ...,0x9 / JL
   at 00016bc3 and 00025a8e); the one that closes it walks 5 down to 0. */
#define ANIM_STEP_COUNT 9

/* PUSH 0xf / CALL delay at 00017067: every step holds its frame for fifteen
   milliseconds on top of the retrace wait. */
#define ANIM_FRAME_DELAY_MS 0xf

/* Where the four panels sit once the window has finished opening, in pixels.
   The two left panels share a column span and the two right ones share
   another, and the two spans meet exactly: LEFT_COLUMN_X + LEFT_COLUMN_WIDTH
   is RIGHT_COLUMN_X.  The horizontal split between the panels is at a
   different row on each side -- row 108 on the left, row 44 on the right --
   which is why the four heights are not two pairs. */
#define LEFT_COLUMN_X 0x0f
#define LEFT_COLUMN_WIDTH 0x85
#define UPPER_LEFT_HEIGHT 0x6c
#define LOWER_LEFT_Y 0x6c
#define RIGHT_COLUMN_X 0x94
#define RIGHT_COLUMN_WIDTH 0x9e
#define UPPER_RIGHT_HEIGHT 0x2c
#define LOWER_RIGHT_Y 0x2c
#define LOWER_RIGHT_HEIGHT 0x9c

/* 00016e80.  The four tables are the four panels' positions, one entry per
   step, and each panel travels along one axis only: the two left panels are
   fixed in the other axis by LEFT_COLUMN_X, the two right ones by
   RIGHT_COLUMN_X and LOWER_RIGHT_Y.  A position outside the screen means the
   panel is still partly off that edge, and each blit below is cut down to
   whatever part of it is on screen -- there is no clipping anywhere else, so
   these four tests are the whole of it.

   The tables live in the original as four nine-int local arrays initialised
   from templates at 0x14788, 0x147ac, 0x147d0 and 0x147f4, copied in with
   four REP MOVSD of ECX=9 at the top of the frame.  They are locals and not
   globals: nothing else in the image reads those addresses, and emitting them
   as data symbols would put four arrays in the rebuild that the original does
   not have as separate objects.

   THE ENTRIES ARE SIGNED AND THREE TABLES REALLY DO GO NEGATIVE.  CMP dword
   ptr [EAX+EBP-0x30],0x0 / JGE at 00016eff is the signed test, and the first
   five entries of upper_left_x and of upper_right_y are below zero.  Read as
   unsigned, upper_left_x[0] is about four billion, the JGE branch is taken,
   and the first frame blits 133 columns from four billion bytes past the
   window image.

   Steps 6 and 7 step back one pixel from step 5 before step 8 returns to it:
   the window overshoots its resting place by a pixel and settles.  That is
   not an off-by-one in the tables, it is the animation.

   The last two steps of lower_right_x are inside the screen by more than the
   panel is wide, which is what the clamp below is for. */
void fdps_draw_status_window_anim_frame(void *background, void *window_image,
                                        int step)
{
    /* Declared in this order because it is the order the original's frame is
       laid out in -- [EBP-4] is the off-screen buffer, [EBP-8] the upper-left
       panel's destination column and the four arrays run down from [EBP-0x30]
       -- and wcc386 hands out the slots in declaration order.  Nothing
       depends on it; the arrangement is codegen, not behaviour.

       The original holds all three panel extents below in the single slot at
       [EBP-0xc], one after another, so its frame is eight bytes smaller than
       this one's.  They are three different measurements of three different
       panels and are named as such here (ADR-0001: the standard is observable
       behaviour, and a stack slot is not). */
    unsigned char *frame;
    int upper_left_dest_x;
    int upper_left_width;
    int lower_right_width;
    int upper_right_height;
    int upper_left_x[ANIM_STEP_COUNT] =
        { -120, -100, -70, -40, -15, 15, 14, 14, 15 };
    int lower_left_y[ANIM_STEP_COUNT] =
        { 190, 175, 160, 140, 125, 108, 109, 109, 108 };
    int lower_right_x[ANIM_STEP_COUNT] =
        { 300, 270, 240, 215, 175, 148, 149, 149, 148 };
    int upper_right_y[ANIM_STEP_COUNT] =
        { -36, -32, -24, -16, -8, 0, -1, -1, 0 };

    /* One whole frame's worth of off-screen buffer, allocated and freed per
       call.  The original does not check the result and neither does this:
       PUSH 0xfa00 / CALL malloc / MOV [EBP-4],EAX at 00016ecb goes straight
       into the memmove that follows. */
    frame = (unsigned char *) malloc(VGA_SCREEN_BYTES);
    memmove(frame, background, (size_t) VGA_SCREEN_BYTES);

    /* Upper-left panel, sliding in from the left edge.  While its x is
       negative only its rightmost LEFT_COLUMN_WIDTH + x columns are on
       screen, and they go at column 0; once x reaches zero the whole panel is
       drawn at column x.  Either way the source column is
       RIGHT_COLUMN_X - width, which is LEFT_COLUMN_X shifted right by however
       much of the panel is still off the edge. */
    if (upper_left_x[step] < 0) {
        upper_left_width = upper_left_x[step] + LEFT_COLUMN_WIDTH;
        upper_left_dest_x = 0;
    } else {
        upper_left_width = LEFT_COLUMN_WIDTH;
        upper_left_dest_x = upper_left_x[step];
    }
    fdps_blit_rect((unsigned int) window_image
                       + (RIGHT_COLUMN_X - upper_left_width),
                   VGA_SCREEN_PITCH,
                   frame + upper_left_dest_x,
                   VGA_SCREEN_PITCH,
                   upper_left_width, UPPER_LEFT_HEIGHT);

    /* Lower-left panel, sliding up from below the screen.  Its top row is the
       table entry and its height is simply what is left above the bottom
       scanline, so the part still below the screen is never blitted rather
       than being blitted and clipped. */
    fdps_blit_rect((unsigned int) window_image
                       + (LOWER_LEFT_Y * VGA_SCREEN_PITCH + LEFT_COLUMN_X),
                   VGA_SCREEN_PITCH,
                   frame + lower_left_y[step] * VGA_SCREEN_PITCH
                       + LEFT_COLUMN_X,
                   VGA_SCREEN_PITCH,
                   LEFT_COLUMN_WIDTH, VGA_SCREEN_ROWS - lower_left_y[step]);

    /* Lower-right panel, sliding in from the right edge.  Its width is the
       room left between its x and the right edge of the screen, and it stops
       growing once the whole panel fits: CMP dword ptr [EBP-0xc],0x9e / JLE
       at 00016fc8, the signed test.  The clamp is load bearing and not
       defensive -- the last four steps sit at x 148 or 149, which leave 172
       and 171 columns of room, and without it those frames would blit past
       the panel's own right edge into whatever the window image holds
       there. */
    lower_right_width = VGA_SCREEN_PITCH - lower_right_x[step];
    if (lower_right_width > RIGHT_COLUMN_WIDTH) {
        lower_right_width = RIGHT_COLUMN_WIDTH;
    }
    fdps_blit_rect((unsigned int) window_image
                       + (LOWER_RIGHT_Y * VGA_SCREEN_PITCH + RIGHT_COLUMN_X),
                   VGA_SCREEN_PITCH,
                   frame + LOWER_RIGHT_Y * VGA_SCREEN_PITCH
                       + lower_right_x[step],
                   VGA_SCREEN_PITCH,
                   lower_right_width, LOWER_RIGHT_HEIGHT);

    /* Upper-right panel, sliding down from above the screen.  Its y is zero
       or negative, so the height on screen is UPPER_RIGHT_HEIGHT + y and the
       source starts that far down the panel; the destination is always row 0.
       No clamp is needed on this one because y never goes positive. */
    upper_right_height = upper_right_y[step] + UPPER_RIGHT_HEIGHT;
    fdps_blit_rect((unsigned int) window_image
                       + (UPPER_RIGHT_HEIGHT - upper_right_height)
                           * VGA_SCREEN_PITCH
                       + RIGHT_COLUMN_X,
                   VGA_SCREEN_PITCH,
                   frame + RIGHT_COLUMN_X,
                   VGA_SCREEN_PITCH,
                   RIGHT_COLUMN_WIDTH, upper_right_height);

    /* The frame's pacing, in the order the original has it: hold for fifteen
       milliseconds, then wait for a vertical retrace to start and then for it
       to finish, and only then put the buffer up.  Presenting on the far side
       of the retrace rather than at its start is what keeps the copy off the
       displayed scanlines; the delay on top of it is what makes the window
       take about a third of a second to open rather than nine frames. */
    delay((unsigned int) ANIM_FRAME_DELAY_MS);
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
    }
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
    }

    memmove((void *) VGA_SCREEN_BASE, frame, (size_t) VGA_SCREEN_BYTES);
    free(frame);
}

/* Where a .CEL sheet's offset table begins, spelled as the size of the header
   it follows because that is what the 15 is (resource_info/cel.md).  MOV EAX,
   dword ptr [EDX + 0xf] at 00016894 is the read, and 0xf is an immediate in
   the instruction: the header's own u16 at +0x05 that declares the table
   position is never looked at, here or anywhere else in the image. */
#define CEL_OFFSET_TABLE_START ((int) sizeof(struct fdps_cel_header))

/* The source rectangle the sheet is decoded as, PUSH 0x140 at 000168a8 and
   PUSH 0xc8 at 000168a3.  Both are immediates and neither is a read of the
   sheet's header: Status.cel's own i16 pair at +0x07 and +0x09 declares
   exactly this 320x200, but the code does not consult it. */
#define STATUS_CEL_WIDTH 0x140
#define STATUS_CEL_HEIGHT 0xc8

/* Mode 0, the plain opaque RLE kernel, PUSH 0x0 at 0001689a; the mode operand
   pushed with it at 0001689c is the dword mode 0 does not read. */
#define BLIT_MODE_OPAQUE 0

/* 00016840.  One basic block: no branch, no test and no loop, and nothing is
   checked -- not the container handle, not the loaded member and not the
   allocation.  A missing MISC.VFS therefore does not fail gracefully, it hands
   fdps_vfs_load_file a null handle to search.  That is the original's
   behaviour and is reproduced rather than guarded (rebuild_info/pitfalls.md).

   THE STRING LITERALS ARE WRITTEN THROUGH.  fdps_vfs_load_file upper-cases its
   query in place (vfs.h), so the "Status.cel" below is permanently
   "STATUS.CEL" after the first call, exactly as the original's copy at 0x61590
   is.  Both spellings find the member -- every entry name in every shipped
   container is upper case (resource_info/vfs.md) -- so the rewrite is
   invisible, but the literal has to be in writable storage for the call to
   work at all.  Under -mf it is: wcc386 puts string literals in CONST
   (rebuild_info/build_flags.md) and CONST is a member of DGROUP alongside
   _DATA and _BSS, which is where the original's own copy lives too.

   THE ORDER OF THE TWO FREES IS NOT INTERCHANGEABLE WITH THE BLIT.  The
   container handle goes back before the sheet is even addressed (CALL free at
   00016886, ahead of the offset-table read at 00016894) and the sheet goes
   back after the decode (CALL free at 000168c1).  So the handle is dead for
   the whole of the drawing and only the 64000-byte result outlives the call.

   Of the seven arguments the blit takes, three are the geometry and the
   distinction between two of them matters: STATUS_CEL_WIDTH is the source
   rectangle's width, the third argument, pushed last at 000168a8 and landing
   at [EBP + 0x10] in fdps_blit_dispatch, and VGA_SCREEN_PITCH is the
   destination surface's pitch, the fifth, pushed at 0001689e and landing at
   [EBP + 0x18].  They are the same 0x140 here because the destination is a
   whole mode 13h frame and the sheet fills it, which is why nothing in the
   emitted call would look wrong if the two were swapped -- and why they are
   named apart rather than sharing one constant.

   The caller owns what comes back and must free it; all four call sites store
   EAX into a local of their own and pass it on as the window image. */
void *fdps_load_status_cel_image(void)
{
    void *misc_vfs;
    unsigned char *cel_sheet;
    unsigned char *cel_stream;
    unsigned char *status_image;

    misc_vfs = fdps_vfs_open("MISC.VFS");
    cel_sheet = (unsigned char *) fdps_vfs_load_file("Status.cel", misc_vfs);
    status_image = (unsigned char *) malloc(VGA_SCREEN_BYTES);
    free(misc_vfs);

    /* Sprite 0's stream, addressed from the start of the FILE: the table
       entry is loaded off the sheet base and then added to the sheet base
       again, never to the address it was read from. */
    cel_stream = cel_sheet
        + *(int *) (cel_sheet + CEL_OFFSET_TABLE_START);
    fdps_blit_dispatch(cel_stream, status_image, STATUS_CEL_WIDTH,
                       STATUS_CEL_HEIGHT, VGA_SCREEN_PITCH, 0,
                       BLIT_MODE_OPAQUE);

    free(cel_sheet);

    /* The original copies the buffer into a fifth slot and returns that --
       MOV [EBP-4],EAX / MOV EAX,[EBP-4] at 000168cc -- which is what a plain
       return of a local compiles to at -od.  This one does the same: the
       object file wcc386 produces from the line below carries both moves and
       the same SUB ESP,0x14.  Only which slot holds which local differs, and
       a stack slot is not observable behaviour (ADR-0001). */
    return status_image;
}

/* The largest input code the wait loop below will hand back.  Everything
   above it -- the 0xff the scancode reader answers with when nothing has been
   pressed, and every break code -- keeps the loop running.  CMP dword ptr
   [EBP-0x8],0x7f / JLE at 00017117, a SIGNED compare, which is why the code
   is held in an int here rather than in the unsigned the reader returns. */
#define SCANCODE_LAST_MAKE_CODE 0x7f

/* The offscreen page the map is composed on before the window goes over it:
   360 x 240 8bpp at pitch 0x168, PUSH 0x15180 at 00017137, with the 24-pixel
   apron on all four sides that fdps_draw_scene_layers needs.  Its visible
   window starts at page byte 0x21d8, page coordinate (24,24), and lands at
   screen byte 0x504, screen coordinate (4,4). */
#define SCENE_PAGE_PITCH 0x168
#define SCENE_PAGE_BYTES 0x15180
#define SCENE_PAGE_WINDOW_AT 0x21d8
#define SCREEN_WINDOW_AT 0x504
#define SCREEN_WINDOW_W 0x138
#define SCREEN_WINDOW_H 0xc0

/* Where the composed window sits in a whole 320x200 frame and how big it is:
   columns 15..305 of every one of the 200 rows, PUSH 0x123 / PUSH 0xc8 with
   the two 0xf offsets at 000172ca.  It is the same 291x200 block
   fdps_draw_status_window_anim_frame slides into place. */
#define STATUS_WINDOW_AT 0x0f
#define STATUS_WINDOW_W 0x123
#define STATUS_WINDOW_H 0xc8

/* The unit sprite: one 24x24 cell, 0x240 bytes at pitch 0x18 (PUSH 0x240 at
   00017219, PUSH 0x18 three times at 00017294).  A cell is composed shadow
   first and unit sprite over it, then copied whole into the window image. */
#define UNIT_SPRITE_W 0x18
#define UNIT_SPRITE_H 0x18
#define UNIT_SPRITE_BYTES 0x240

/* Sprite 3 of Shadow.cel, the shadow every unit in this window stands on.
   PUSH 0x3 at 00017237; the sheet is the global loaded once at startup. */
#define SHADOW_SPRITE_INDEX 3

/* Where the unit's cell is stamped into the window image: row 10, column 161,
   which is the 0xd21 of ADD EAX,0xd21 at 000172b6. */
#define WINDOW_SPRITE_ROW 0x0a
#define WINDOW_SPRITE_COL 0xa1
#define WINDOW_SPRITE_AT \
    (WINDOW_SPRITE_ROW * VGA_SCREEN_PITCH + WINDOW_SPRITE_COL)

/* The walk cycle.  The tick is taken modulo 16 and divided by 4, and a
   quotient of 3 is folded back to 1, so the three drawn frames run 0, 1, 2, 1
   and the cycle reads as a ping-pong rather than a snap back to the start.
   MOV EBX,0x10 / IDIV at 000171fa and the SAR EAX,0x2 at 00017206.

   BOTH DIVISIONS ARE SIGNED, and the tick they are applied to is
   data_fdps_unit_status_window_last_tick rather than the live counter -- the
   frame is drawn for the tick the window last settled on, not the one that
   has just arrived.  That is the read at 000171ec, and it is why the last
   tick is declared as a signed int. */
#define WALK_CYCLE_TICKS 0x10
#define WALK_CYCLE_TICKS_PER_FRAME 4

/* A cache slot's twelve stream offsets are four facings of three walk frames
   (struct fdps_cel_cache_slot, src/fdpstype.h), and the facing numbering is
   the one the whole game uses: 0 down, 1 left, 2 up, 3 right (src/gauge.h).
   The four byte offsets the assembly folds into its displacements -- 0x00,
   0x0c, 0x18 and 0x24 -- are those four facings times three entries times
   four bytes. */
#define WALK_FRAMES_PER_FACING 3
#define FACING_DOWN 0
#define FACING_LEFT 1
#define FACING_UP 2
#define FACING_RIGHT 3

/* Blit mode 8, the top-bottom mirror (src/rle.h).  The falling unit and the
   unit lying at the bottom of its drop are drawn upside down with it; the two
   turning phases and the walk up out of the window are mode 0. */
#define BLIT_MODE_MIRRORED_VERTICAL 8

/* The idle animation's frame counter is -1 while nothing is playing, so the
   counter is both the phase and the armed flag.  The four counted phases end
   at frames 15, 25, 35 and 45 (the CMP 0xf, 0x19, 0x23 and 0x2d at 000172fa,
   00017347, 00017382 and 000173e3, all with a signed JL), and the fifth phase
   is not counted in frames at all -- it runs off idle_rise_row instead. */
#define IDLE_INACTIVE (-1)
#define IDLE_DROP_END 0x0f
#define IDLE_UPSIDE_DOWN_END 0x19
#define IDLE_FACE_RIGHT_END 0x23
#define IDLE_FACE_LEFT_END 0x2d

/* One call in two hundred arms the idle animation: IDIV by 0xc8 and
   TEST EDX,EDX at 00017100.  The division is signed, which costs nothing
   because Watcom's rand returns 0..0x7fff, and the test is on the remainder,
   so it is one draw of rand and not a running counter -- a window opened
   twice can arm it twice in a row or never. */
#define IDLE_TRIGGER_ODDS 0xc8

/* How many entries the drop table has, and where the animated sprite is drawn
   while the animation runs: always column 161 (ADD EAX,0xa1 at 00017324), and
   at row 170 for everything after the drop (the folded 0xd521 at 0001735f).
   Column 161 is the same column the cell occupies inside the window, so the
   unit appears to leave its own portrait slot and come back to it. */
#define IDLE_DROP_FRAMES 15
#define IDLE_SPRITE_COLUMN 0xa1
#define IDLE_STAND_ROW 0xaa
#define IDLE_STAND_AT (IDLE_STAND_ROW * VGA_SCREEN_PITCH + IDLE_SPRITE_COLUMN)

/* The walk back up: from row 170 in steps of four until the row is 10 or
   less, at which point the counter goes back to IDLE_INACTIVE and the plain
   walk cycle takes over again.  ADD dword ptr [EBP-0x10],-0x4 at 00017496 and
   CMP dword ptr [EBP-0x10],0xa / JLE at 00017441.

   THE RISE ROW IS NOT RESET WHEN THE ANIMATION ENDS.  It is initialised once
   on the way into the call and never again, so a second idle animation within
   the same call would play its four counted phases and then end immediately
   -- the row is already at or below 10.  Resetting it alongside the frame
   counter is the obvious tidy-up and it would let the rise play twice. */
#define IDLE_RISE_STEP 4
#define IDLE_RISE_LAST_ROW 0x0a

/* 000170c0.  Holds the assembled status window on the screen and comes back
   with the first input code the caller can act on.  The caller composes the
   window into window_image and slides it in with
   fdps_draw_status_window_anim_frame; this is the loop that keeps it there.

   window_image is a whole 320x200 frame holding the window at its resting
   position, and it is READ AND WRITTEN: the unit's 24x24 cell is stamped into
   it at row 10, column 161 on every frame drawn, so the image the caller
   handed over comes back with the last walk frame in it.

   unit_index picks the sprite cache slot the walk cycle is taken from, and is
   IGNORED DURING A VILLAGE PHASE: with data_fdps_village_mode_flag set the
   loop substitutes data_fdps_village_status_window_unit_idx, which
   fdps_draw_unit_status_panel published when it drew the panel.

   allow_idle_animation only offers the animation.  A non-zero value draws one
   rand and arms the sequence when it is a multiple of 200; a zero value does
   not call rand at all, so the two settings differ in the CRT's random state
   as well as in what is drawn.

   The result is the scancode, and everything above 0x7f keeps the loop
   running -- see SCANCODE_LAST_MAKE_CODE above.

   ONE PASS IS NOT ONE FRAME.  The loop polls the keyboard as fast as it can
   and draws only when the timer tick has moved since the last frame it drew,
   so the frame rate is the timer's and the poll rate is the machine's.  The
   pacing state is a global rather than a local
   (data_fdps_unit_status_window_last_tick), which is what makes a window
   opened again immediately after one was closed skip its first frame.

   NOTHING HERE CHECKS A malloc.  Three blocks are taken per frame and all
   three are given back before the pass ends; none of the three results is
   compared against NULL, and the sprite cache pointer is not tested either
   (gamedata.h). */
int fdps_unit_status_window_wait_input(unsigned char *window_image,
                                       int unit_index,
                                       char allow_idle_animation)
{
    /* The code the last poll returned.  Held signed because the test that
       ends the loop is signed, and returned as it stands. */
    int input_code;
    /* The 360x240 page the map behind the window is composed on, taken and
       given back inside one pass. */
    unsigned char *scene_page;
    /* The row the rising unit is drawn on in the animation's last phase.
       Initialised once for the whole call -- see IDLE_RISE_STEP above. */
    int idle_rise_row;
    /* Which of the three walk frames of the unit's facing is drawn this
       pass. */
    int walk_frame;
    /* The RLE stream of the unit sprite being drawn, addressed from the base
       of the sprite cache block. */
    unsigned char *sprite_stream;
    /* The 24x24 cell the shadow and the unit are composed in before the pair
       is copied into the window image. */
    unsigned char *sprite_cell;
    /* The whole 320x200 frame this pass builds and presents. */
    unsigned char *frame;
    /* Where the idle animation has got to, and IDLE_INACTIVE when it is not
       playing. */
    int idle_frame;
    /* The rows the falling unit is drawn on, one per frame of the drop: down
       to 170, a bounce up to 130, back down, a smaller bounce and a rest.
       The original copies these fifteen ints out of a template at 0x14818
       with a REP MOVSD of ECX=0xf at 000170e7, which is what a local array
       with an initialiser compiles to at -od.  It is a local and not a data
       symbol: nothing else in the image reads that address. */
    int idle_drop_row[IDLE_DROP_FRAMES] =
        { 15, 30, 70, 105, 140, 170, 150, 130, 150, 170, 160, 165, 165, 160,
          170 };

    idle_rise_row = IDLE_STAND_ROW;
    idle_frame = IDLE_INACTIVE;

    if (allow_idle_animation != 0 && rand() % IDLE_TRIGGER_ODDS == 0) {
        idle_frame = 0;
    }

    for (;;) {
        input_code = (int) fdps_read_scancode_auto_repeat();
        if (input_code <= SCANCODE_LAST_MAKE_CODE) {
            break;
        }
        if ((int) data_fdps_timer_tick_counter
                == data_fdps_unit_status_window_last_tick) {
            continue;
        }

        fdps_cycle_ui_palette();
        scene_page = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);
        frame = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
        memset(frame, 0, (size_t) VGA_SCREEN_BYTES);

        /* Where the picture behind the window comes from.  In the field the
           map is composed from scratch; in a village the visible screen
           already holds a finished picture and is simply lifted off it.  The
           two paths agree on where the result lands, which is why the copy
           into the frame below is shared. */
        if (data_fdps_village_mode_flag == 0) {
            fdps_draw_scene_layers(scene_page);
        } else {
            fdps_blit_rect(VGA_SCREEN_BASE + SCREEN_WINDOW_AT,
                           VGA_SCREEN_PITCH,
                           scene_page + SCENE_PAGE_WINDOW_AT,
                           SCENE_PAGE_PITCH, SCREEN_WINDOW_W,
                           SCREEN_WINDOW_H);
        }
        fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                       SCENE_PAGE_PITCH, frame + SCREEN_WINDOW_AT,
                       VGA_SCREEN_PITCH, SCREEN_WINDOW_W, SCREEN_WINDOW_H);
        free(scene_page);

        walk_frame = (data_fdps_unit_status_window_last_tick % WALK_CYCLE_TICKS)
            / WALK_CYCLE_TICKS_PER_FRAME;
        if (walk_frame == 3) {
            walk_frame = 1;
        }

        /* The shadow goes down first and the unit sprite over it, so the cell
           the window image receives is the pair composited.  The shadow is
           drawn whether or not the unit is: while the idle animation plays,
           the unit has left the cell and only its shadow stays behind in the
           portrait slot. */
        sprite_cell = (unsigned char *) malloc((size_t) UNIT_SPRITE_BYTES);
        fdps_cel_blit_sprite(data_fdps_shadow_sprite_sheet_ptr,
                             SHADOW_SPRITE_INDEX, sprite_cell, UNIT_SPRITE_W,
                             0, 0, 0, BLIT_MODE_OPAQUE);

        /* The clamp is AFTER the shadow, not folded into the test above it,
           and it cannot fire from a non-negative tick: it is what a negative
           last tick -- the counter having run past 0x7fffffff -- would land
           on.  Folding it into the same test as the 3 changes nothing
           observable and loses that. */
        if (walk_frame < 0) {
            walk_frame = 0;
        }
        if (data_fdps_village_mode_flag != 0) {
            unit_index = data_fdps_village_status_window_unit_idx;
        }

        /* A stored offset is measured from the base of the cache block, not
           from the slot it was read out of. */
        sprite_stream = data_fdps_cel_sprite_cache_ptr
            + ((struct fdps_cel_cache_slot *) data_fdps_cel_sprite_cache_ptr)
                  [unit_index].sprite_offset[
                      FACING_DOWN * WALK_FRAMES_PER_FACING + walk_frame];
        if (idle_frame == IDLE_INACTIVE) {
            fdps_blit_dispatch(sprite_stream, sprite_cell, UNIT_SPRITE_W,
                               UNIT_SPRITE_H, UNIT_SPRITE_W, 0,
                               BLIT_MODE_OPAQUE);
        }
        fdps_blit_rect((unsigned int) sprite_cell, UNIT_SPRITE_W,
                       window_image + WINDOW_SPRITE_AT, VGA_SCREEN_PITCH,
                       UNIT_SPRITE_W, UNIT_SPRITE_H);
        fdps_blit_rect((unsigned int) (window_image + STATUS_WINDOW_AT),
                       VGA_SCREEN_PITCH, frame + STATUS_WINDOW_AT,
                       VGA_SCREEN_PITCH, STATUS_WINDOW_W, STATUS_WINDOW_H);

        /* The five phases of the idle animation.  Each one re-tests the armed
           flag before its range, exactly as the assembly does, and at most
           one of the five runs per pass. */
        if (idle_frame != IDLE_INACTIVE && idle_frame < IDLE_DROP_END) {
            /* The drop and its two bounces, drawn upside down. */
            fdps_blit_dispatch(sprite_stream,
                               frame + idle_drop_row[idle_frame]
                                   * VGA_SCREEN_PITCH + IDLE_SPRITE_COLUMN,
                               UNIT_SPRITE_W, UNIT_SPRITE_H, VGA_SCREEN_PITCH,
                               0, BLIT_MODE_MIRRORED_VERTICAL);
            idle_frame = idle_frame + 1;
        } else if (idle_frame != IDLE_INACTIVE
                   && idle_frame < IDLE_UPSIDE_DOWN_END) {
            /* Ten frames lying where it landed, still upside down and still
               the facing the walk cycle picked. */
            fdps_blit_dispatch(sprite_stream, frame + IDLE_STAND_AT,
                               UNIT_SPRITE_W, UNIT_SPRITE_H, VGA_SCREEN_PITCH,
                               0, BLIT_MODE_MIRRORED_VERTICAL);
            idle_frame = idle_frame + 1;
        } else if (idle_frame != IDLE_INACTIVE
                   && idle_frame < IDLE_FACE_RIGHT_END) {
            /* Back the right way up, facing right for ten frames. */
            sprite_stream = data_fdps_cel_sprite_cache_ptr
                + ((struct fdps_cel_cache_slot *)
                       data_fdps_cel_sprite_cache_ptr)
                      [unit_index].sprite_offset[
                          FACING_RIGHT * WALK_FRAMES_PER_FACING + walk_frame];
            fdps_blit_dispatch(sprite_stream, frame + IDLE_STAND_AT,
                               UNIT_SPRITE_W, UNIT_SPRITE_H, VGA_SCREEN_PITCH,
                               0, BLIT_MODE_OPAQUE);
            idle_frame = idle_frame + 1;
        } else if (idle_frame != IDLE_INACTIVE
                   && idle_frame < IDLE_FACE_LEFT_END) {
            /* And facing left for ten more. */
            sprite_stream = data_fdps_cel_sprite_cache_ptr
                + ((struct fdps_cel_cache_slot *)
                       data_fdps_cel_sprite_cache_ptr)
                      [unit_index].sprite_offset[
                          FACING_LEFT * WALK_FRAMES_PER_FACING + walk_frame];
            fdps_blit_dispatch(sprite_stream, frame + IDLE_STAND_AT,
                               UNIT_SPRITE_W, UNIT_SPRITE_H, VGA_SCREEN_PITCH,
                               0, BLIT_MODE_OPAQUE);
            idle_frame = idle_frame + 1;
        } else if (idle_frame != IDLE_INACTIVE) {
            /* The walk back up out of the window, four pixels a frame, facing
               away.  This phase is paced by the row and not by the frame
               counter, so it is the row reaching the top that disarms the
               animation. */
            if (idle_rise_row <= IDLE_RISE_LAST_ROW) {
                idle_frame = IDLE_INACTIVE;
            } else {
                sprite_stream = data_fdps_cel_sprite_cache_ptr
                    + ((struct fdps_cel_cache_slot *)
                           data_fdps_cel_sprite_cache_ptr)
                          [unit_index].sprite_offset[
                              FACING_UP * WALK_FRAMES_PER_FACING + walk_frame];
                fdps_blit_dispatch(sprite_stream,
                                   frame + idle_rise_row * VGA_SCREEN_PITCH
                                       + IDLE_SPRITE_COLUMN,
                                   UNIT_SPRITE_W, UNIT_SPRITE_H,
                                   VGA_SCREEN_PITCH, 0, BLIT_MODE_OPAQUE);
                idle_rise_row = idle_rise_row - IDLE_RISE_STEP;
            }
        }

        /* What reaches the adapter.  In the field the whole 64000-byte frame
           goes out; in a village only the 312x192 window inside the border
           does, because the four-pixel frame around it belongs to the village
           screen and was never redrawn. */
        if (data_fdps_village_mode_flag == 0) {
            memmove((void *) VGA_SCREEN_BASE, frame,
                    (size_t) VGA_SCREEN_BYTES);
        } else {
            fdps_blit_rect((unsigned int) (frame + SCREEN_WINDOW_AT),
                           VGA_SCREEN_PITCH,
                           (void *) (VGA_SCREEN_BASE + SCREEN_WINDOW_AT),
                           VGA_SCREEN_PITCH, SCREEN_WINDOW_W,
                           SCREEN_WINDOW_H);
        }
        free(frame);
        free(sprite_cell);
        data_fdps_unit_status_window_last_tick =
            (int) data_fdps_timer_tick_counter;
    }

    return input_code;
}

/* The window cue, PUSH 0x6159c / CALL fdps_play_sfx at 000168ec.  It is the
   same member the opening plays (PUSH 0x6159c at 00016b09 in
   fdps_battle_show_unit_status_window), and those two are the only references
   to that string in the image -- the ring menu's own cue is a different member,
   "OpWin.wav" at 0x61584 (menu.c).

   It has to be a plain writable literal, for the reason menu.c's copy does:
   the lookup inside fdps_play_sfx upper-cases the caller's own storage in place
   (vfs.h), so this spelling is permanently "OPWIN1.WAV" after the first status
   window of the run, exactly as the original's copy at 0x6159c is
   (rebuild_info/pitfalls.md). */
#define STATUS_WINDOW_SOUND "OpWin1.wav"

/* Where the closing walk starts.  MOV dword ptr [EBP-0x4],0x5 at 000169a9 and
   CMP against 0 / JGE at 000169b0, so the steps drawn are 5, 4, 3, 2, 1, 0 --
   six frames, and step 0 is the closed position.  The opening walks 0 up to 8,
   so the closing does NOT retrace it: it starts from the resting frame's
   predecessor and never draws steps 6, 7 or 8, which are the one-pixel
   overshoot the opening settles through (statwin.h). */
#define CLOSE_ANIM_FIRST_STEP 5

/* The view's frame: the four pixels of screen around the 312x192 window that
   fdps_render_view_frame never writes.  VIEW_BORDER_BAND_BYTES is one band of
   four whole rows, the 0x500 pushed at 000169e8 and 000169fc, and
   VIEW_BOTTOM_BORDER_AT is where the lower band starts -- the 0xaf500 pushed at
   00016a03, which is row 196. */
#define VIEW_BORDER_WIDTH 4
#define VIEW_BORDER_BAND_BYTES (VIEW_BORDER_WIDTH * VGA_SCREEN_PITCH)
#define VIEW_BOTTOM_BORDER_AT \
    ((VGA_SCREEN_ROWS - VIEW_BORDER_WIDTH) * VGA_SCREEN_PITCH)
#define VIEW_RIGHT_BORDER_AT (VGA_SCREEN_PITCH - VIEW_BORDER_WIDTH)

/* How many times the loop that blanks the left and right margins runs: CMP
   dword ptr [EBP-0x4],0x140 / JL at 00016a17.

   IT IS THE SCREEN'S WIDTH AND NOT ITS HEIGHT, AND THAT IS THE ORIGINAL'S BUG.
   Only the first 200 iterations land on a scanline; the last 120 write their
   four-byte zero runs from the end of the frame at 0xafa00 up to 0xb8fff, which
   is past the mode 13h aperture entirely.  It is harmless only because the
   destination is the adapter and not a buffer -- writing the obvious thing, a
   border clear over a 64000-byte offscreen page, would overrun that page by 38
   KB (rebuild_info/pitfalls.md).  So the count stays 0x140 and the writes stay
   aimed at VGA_SCREEN_BASE; it is spelled as the pitch because that is the
   constant the original used, not because a row index belongs in it. */
#define BORDER_CLEAR_ROW_COUNT VGA_SCREEN_PITCH

/* 000168e0.  Takes the status window away again.  See statwin.h for what the
   two arguments are and what the caller still owns afterwards.

   THE SCRATCH PAGE IS TAKEN IN BOTH BRANCHES AND USED IN ONLY ONE.  The
   0x15180-byte scene page is allocated before the branch and freed after
   everything, but the village arm never reads it: the blit that arm performs
   writes the saved page's view window INTO the scratch, and nothing looks at it
   again.  That blit is dead work in the original too, and it stays -- dropping
   it would drop the read of the saved page and its 192 row-by-row memmoves, and
   keeping the allocation out of the village arm would change which block the
   next malloc in the program is handed.

   THE ANIMATION IS DRAWN OVER background AND ITS RESULT IS THROWN AWAY.  Each
   of the six frames presents itself to the screen, and then the restore below
   paints over the whole of what the last one put there.  What the player sees
   is the six frames; what is left on the adapter afterwards is the restore
   alone.

   NOTHING IS CHECKED.  malloc's result goes straight into the branch, the
   saved page pointer is dereferenced without a null test, and neither the cue
   nor the compositor reports anything back. */
void fdps_close_status_window(void *window_image, void *background)
{
    /* The 360x240 page the scene behind the window is composed on, taken and
       given back inside this one call. */
    unsigned char *scene_page;
    /* Which step of the four-panel slide is being drawn, counting down. */
    int step;
    /* The screen row whose four-pixel left and right margins are being
       blanked -- for the first 200 of the loop's 320 passes; see
       BORDER_CLEAR_ROW_COUNT above for what the rest write. */
    int border_row;

    fdps_play_sfx(STATUS_WINDOW_SOUND);
    scene_page = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);

    /* The picture that lies behind the window, into background.  On the battle
       map it is composed again from the scene layers and only the view window
       is written, the rest of the frame being cleared; in a village it is the
       saved page copied whole. */
    if (data_fdps_village_mode_flag == 0) {
        fdps_draw_scene_layers(scene_page);
        memset(background, 0, (size_t) VGA_SCREEN_BYTES);
        fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                       SCENE_PAGE_PITCH,
                       (void *) ((unsigned char *) background
                                 + SCREEN_WINDOW_AT),
                       VGA_SCREEN_PITCH, SCREEN_WINDOW_W, SCREEN_WINDOW_H);
    } else {
        fdps_blit_rect((unsigned int) (data_fdps_village_backdrop_page_ptr
                                       + SCREEN_WINDOW_AT),
                       VGA_SCREEN_PITCH,
                       scene_page + SCENE_PAGE_WINDOW_AT,
                       SCENE_PAGE_PITCH, SCREEN_WINDOW_W, SCREEN_WINDOW_H);
        memmove(background, data_fdps_village_backdrop_page_ptr,
                (size_t) VGA_SCREEN_BYTES);
    }

    for (step = CLOSE_ANIM_FIRST_STEP; step >= 0; step--) {
        fdps_draw_status_window_anim_frame(background, window_image, step);
    }

    /* And the visible screen.  The battle branch goes through the compositor,
       which writes only the 312x192 window, and then blanks the frame around it
       directly in the adapter; the village branch puts the saved page back and
       blanks nothing, because the border there belongs to the village screen
       and the saved page already holds it. */
    if (data_fdps_village_mode_flag == 0) {
        fdps_render_view_frame();
        memset((void *) VGA_SCREEN_BASE, 0, (size_t) VIEW_BORDER_BAND_BYTES);
        memset((void *) (VGA_SCREEN_BASE + VIEW_BOTTOM_BORDER_AT), 0,
               (size_t) VIEW_BORDER_BAND_BYTES);
        for (border_row = 0; border_row < BORDER_CLEAR_ROW_COUNT;
             border_row++) {
            memset((void *) (VGA_SCREEN_BASE
                             + border_row * VGA_SCREEN_PITCH),
                   0, (size_t) VIEW_BORDER_WIDTH);
            memset((void *) (VGA_SCREEN_BASE
                             + border_row * VGA_SCREEN_PITCH
                             + VIEW_RIGHT_BORDER_AT),
                   0, (size_t) VIEW_BORDER_WIDTH);
        }
    } else {
        memmove((void *) VGA_SCREEN_BASE, data_fdps_village_backdrop_page_ptr,
                (size_t) VGA_SCREEN_BYTES);
    }

    free(scene_page);
    data_fdps_ui_play_active_flag = data_fdps_ui_play_active_flag_saved;
}

/* The inventory list's geometry, straight out of the address arithmetic.  A
   row is 0x11 pixels tall (IMUL EAX,dword ptr [EBP+-0x14],0x11, three times
   over), and the three things a row holds sit on three different baselines:
   the category icon on row_top + 9 at column 3, the item name on row_top + 8
   at column 0x13, and the right-hand caption on row_top + 0xd at column 0x66
   with its figure 0x16 pixels further right.  The selection bar is drawn at
   column 0 on the name's baseline, selected_slot * 0x11 + 8.

   Every one of those is folded into the destination pointer as dest_base +
   y * pitch + x, so the caller's dest_base is the list's top-left corner and
   not the surface origin. */
#define INV_ROW_PITCH 0x11
#define INV_SEL_BAR_SPRITE 0
#define INV_SEL_BAR_X 0
#define INV_SEL_BAR_Y 8
#define INV_ICON_X 3
#define INV_ICON_Y 9
#define INV_NAME_X 0x13
#define INV_NAME_Y 8
#define INV_CAPTION_X 0x66
#define INV_CAPTION_Y 0x0d
#define INV_FIGURE_X_BIAS 0x16

/* How many slots a unit record carries and what the flag byte of a slot pair
   means: bit 7 empty, bit 6 equipped (TEST dword ptr [EBP+-0xc],0x80 and TEST
   ...,0x40).  The pair itself is inventory_slots[2 * slot] and
   inventory_slots[2 * slot + 1] of struct fdps_unit_record. */
#define INV_SLOT_COUNT 8
#define INV_SLOT_EMPTY 0x80
#define INV_SLOT_EQUIPPED 0x40

/* Where the item names live in data_fdps_all_game_text_ptr: entry 0xc9 plus
   the item id (ADD EAX,0xc9 at 00024fd9). */
#define INV_ITEM_NAME_TEXT_BASE 0xc9

/* The two upper bounds the item type byte is tested against.  Types 1..0x15
   are the weapons and 0x16..0x27 the armour; assets/items.md lists what each
   code is. */
#define ITEM_TYPE_WEAPON_LAST 0x15
#define ITEM_TYPE_ARMOUR_LAST 0x27

/* The Command.cel sprites the list draws: the three category icons, the four
   added to an icon when the item is equipped, and the four right-hand
   captions -- attack power, defence power, HP recovery and MP recovery -- plus
   the plain one an item with no headline number gets. */
#define INV_ICON_WEAPON 0x1d
#define INV_ICON_ARMOUR 0x1e
#define INV_ICON_OTHER 0x1f
#define INV_ICON_EQUIPPED_BIAS 4
#define INV_CAPTION_HP_RECOVERY 0x3e
#define INV_CAPTION_MP_RECOVERY 0x3f
#define INV_CAPTION_AP 0x40
#define INV_CAPTION_DP 0x41
#define INV_CAPTION_PLAIN 0x43

/* The two use_effect codes that put a recovery amount on the row (CMP EAX,0xb
   at 000250a2 and CMP EAX,0xc at 000250e6).  Every other effect, 0x20 among
   them, draws INV_CAPTION_PLAIN and no figure. */
#define ITEM_USE_RESTORE_HP 0x0b
#define ITEM_USE_RESTORE_MP 0x0c

/* The width of the headline figure and its sign flag: PUSH 0x4 and a zeroed
   EAX pushed behind it at every one of the four call sites. */
#define INV_FIGURE_DIGITS 4
#define INV_FIGURE_SHOW_PLUS 0

/* 00024ea0.  See statwin.h for the list's shape and for why the item type is
   classified twice rather than once. */
void fdps_draw_unit_inventory(int unit_index, int selected_slot,
                              unsigned char *dest_base, int pitch)
{
    struct fdps_unit_record *unit;
    struct fdps_item_effect *item;
    unsigned char *caption_dest;
    int slot;
    int slot_flags;
    int item_id;
    int item_type;
    int icon_sprite;

    unit = fdps_get_unit_record(unit_index);
    if (selected_slot >= 0 && selected_slot < INV_SLOT_COUNT) {
        fdps_cel_blit_sprite(data_fdps_selection_bar_sheet_ptr,
                             INV_SEL_BAR_SPRITE, dest_base, pitch,
                             INV_SEL_BAR_X,
                             selected_slot * INV_ROW_PITCH + INV_SEL_BAR_Y,
                             0, 0);
    }

    for (slot = 0; slot < INV_SLOT_COUNT; slot++) {
        slot_flags = unit->inventory_slots[slot * 2];
        if ((slot_flags & INV_SLOT_EMPTY) != 0) {
            continue;
        }
        item_id = unit->inventory_slots[slot * 2 + 1];
        item = fdps_get_item_record(item_id);

        /* The icon's classification.  Type 0 falls out of both bounded tests
           and takes INV_ICON_OTHER. */
        item_type = item->type;
        if (item_type > 0 && item_type <= ITEM_TYPE_WEAPON_LAST) {
            icon_sprite = INV_ICON_WEAPON;
        } else if (item_type > 0 && item_type <= ITEM_TYPE_ARMOUR_LAST) {
            icon_sprite = INV_ICON_ARMOUR;
        } else {
            icon_sprite = INV_ICON_OTHER;
        }
        if ((slot_flags & INV_SLOT_EQUIPPED) != 0) {
            icon_sprite += INV_ICON_EQUIPPED_BIAS;
        }
        fdps_blit_command_sprite(dest_base
                                 + (slot * INV_ROW_PITCH + INV_ICON_Y) * pitch
                                 + INV_ICON_X, pitch, icon_sprite);

        fdps_draw_text(data_fdps_all_game_text_ptr,
                       item_id + INV_ITEM_NAME_TEXT_BASE,
                       dest_base
                       + (slot * INV_ROW_PITCH + INV_NAME_Y) * pitch
                       + INV_NAME_X, pitch, PANEL_TEXT_FG_COLOR,
                       PANEL_TEXT_BG_COLOR, PANEL_TEXT_OUTLINE_COLOR);

        /* The caption's own classification, which is deliberately a second
           reading of the same byte: here type 0 misses the weapon test and
           then passes the armour one, so it prints the defence caption and a
           defence figure while its icon was the catch-all.  See statwin.h. */
        caption_dest = dest_base
            + (slot * INV_ROW_PITCH + INV_CAPTION_Y) * pitch + INV_CAPTION_X;
        if (item->type > 0 && item->type <= ITEM_TYPE_WEAPON_LAST) {
            fdps_blit_command_sprite(caption_dest, pitch, INV_CAPTION_AP);
            fdps_draw_number(caption_dest + INV_FIGURE_X_BIAS, pitch, item->ap,
                             INV_FIGURE_DIGITS, INV_FIGURE_SHOW_PLUS);
        } else if (item->type <= ITEM_TYPE_ARMOUR_LAST) {
            fdps_blit_command_sprite(caption_dest, pitch, INV_CAPTION_DP);
            fdps_draw_number(caption_dest + INV_FIGURE_X_BIAS, pitch, item->dp,
                             INV_FIGURE_DIGITS, INV_FIGURE_SHOW_PLUS);
        } else if (item->use_effect == ITEM_USE_RESTORE_HP) {
            fdps_blit_command_sprite(caption_dest, pitch,
                                     INV_CAPTION_HP_RECOVERY);
            fdps_draw_number(caption_dest + INV_FIGURE_X_BIAS, pitch,
                             item->use_amount, INV_FIGURE_DIGITS,
                             INV_FIGURE_SHOW_PLUS);
        } else if (item->use_effect == ITEM_USE_RESTORE_MP) {
            fdps_blit_command_sprite(caption_dest, pitch,
                                     INV_CAPTION_MP_RECOVERY);
            fdps_draw_number(caption_dest + INV_FIGURE_X_BIAS, pitch,
                             item->use_amount, INV_FIGURE_DIGITS,
                             INV_FIGURE_SHOW_PLUS);
        } else {
            fdps_blit_command_sprite(caption_dest, pitch, INV_CAPTION_PLAIN);
        }
    }
}
