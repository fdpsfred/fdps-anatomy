/* statunit.c -- one unit inside the status window.
 *
 * Everything the window is filled with once statwin.c has opened it: the
 * figures, gauges, portrait and three names of one unit, that unit's
 * eight-slot inventory list, and the loop that holds the window up and keeps
 * the unit walking in it until the player presses something.
 *
 * The two drawing routines own no state: each works on the surface it is
 * handed.  The event loop does own state -- the tick it last drew a frame on,
 * and the unit a village phase substitutes for the one it was asked for --
 * and statunit.h declares both.  The panel drawer is what writes the second
 * of those.
 */
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "blit.h"
#include "gamedata.h"
#include "gauge.h"
#include "keybd.h"
#include "mapdraw.h"
#include "msgwin.h"
#include "palcycle.h"
#include "sprite.h"
#include "statunit.h"
#include "table.h"
#include "text.h"
#include "unit.h"

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* 00063fb0. Starts at zero in the image (BSS); it only carries a unit index
   from the status panel draw to the following wait-input loop while in the
   village, so no initial value is observed. */
int data_fdps_village_status_window_unit_idx;

/* 00063fc0. Starts at zero in the image (BSS); the first pass of the status
   window's wait loop compares the live tick counter against this zero and
   latches the counter at 0001750b, so no non-zero seed is required. */
int data_fdps_unit_status_window_last_tick;

/* End of global data. */

/* The VGA graphics aperture as a flat linear address, the mode 13h scanline
   pitch and the size of one whole frame.  All three are hard-coded in the
   original (PUSH 0xa0000, PUSH 0x140, PUSH 0xfa00) and stay literals here:
   0xa0000 is where the display adapter answers, not the address of anything
   the linker places, so there is no symbol to reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_BYTES 0xfa00

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
   identically, and spelling it as a call under the rebuild's current build,
   which has no -oe (the original's game units used -oe=25), would put two
   CALLs here that the original does not have.

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

/* Mode 0, the plain opaque RLE kernel.  It is the last of the sprite blitter's
   eight arguments and so the first pushed, PUSH 0x0 at 00017229. */
#define BLIT_MODE_OPAQUE 0

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
