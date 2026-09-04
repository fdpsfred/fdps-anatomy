/* gauge.c -- the game's gauge bars, where a combat gauge is placed, and the
 * seven-frame appearance the two combatants' bars make before an attack.
 *
 * See gauge.h for what a caller has to know.  Every drawing routine here is
 * stateless: the unit record comes from fdps_get_unit_record (unit.h), the
 * scroll position from the two view window origin globals and the bar art from
 * the gauge sheet pointers gamedata.h declares, and the only thing written is
 * the caller's own memory -- a pair of ints, or a destination surface.
 *
 * The one piece of state in the file is the pair of gauge positions
 * fdps_battle_show_combat_gauges publishes, declared in gauge.h because it is
 * that function's answer to its caller rather than anything the drawing
 * routines read.
 *
 * malloc and free come from <stdlib.h> and inp from <conio.h>, which is where
 * Watcom 10.0a declares them; all three are real calls in the original --
 * CALL 0x0003d375 at 0001ce8e, CALL 0x0003d478 at 0001d5ae and CALL 0x0003d4e4
 * at 0001d07b -- so the plain declarations are what reproduce them.
 */
#include <stdlib.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "aitarget.h"
#include "blit.h"
#include "mapdraw.h"
#include "unit.h"
#include "gauge.h"

/* The status gauge bar is 117 pixels wide and 8 rows tall, and the sheet holds
   its three graphics that many bytes apart at that row pitch: PUSH 0x75 for
   the source stride and PUSH 0x8 for the row count at both blits, and
   IMUL EAX,dword ptr [EBP+0x1c],0x3a8 at 0001774c for the graphic stride.
   0x3a8 is 0x75 * 8, so the sheet is three tightly packed frames and nothing
   pads between them. */
#define GAUGE_BAR_WIDTH 0x75
#define GAUGE_BAR_HEIGHT 8
#define GAUGE_BAR_GRAPHIC_STRIDE 0x3a8

/* See gauge.h.  The zero case is written first because that is the order the
   original has its blocks in: CMP dword ptr [EBP+0x20],0x0 / JG at 00017700
   jumps forward to the division and falls through to the empty bar.  The
   division itself is signed -- MOV EAX,EDX / SAR EDX,0x1f / IDIV at 00017713
   -- so the numerator is sign extended and a negative result truncates toward
   zero, which the callee's own clamp then turns into an empty bar.

   The rounding is up, not down: ADD EDX,[EBP+0x20] / DEC EDX at 0001770f adds
   max - 1 to the product before the divide.  That is what keeps one filled
   pixel on screen for any current of 1 or more, however large max is. */
void fdps_draw_gauge_bar_proportional(unsigned char *dst, int dst_stride,
                                      int bar_index, int max, int current)
{
    /* [EBP-4]: how many of the bar's 117 columns the filled graphic supplies.
       The original stages it in this local and reads it back to push it, so it
       is a named local here rather than an expression in the call. */
    int fill_width;

    if (max <= 0) {
        fill_width = 0;
    } else {
        fill_width = (current * GAUGE_BAR_WIDTH + max - 1) / max;
    }

    fdps_draw_gauge_bar(dst, dst_stride, bar_index, fill_width);
}

/* See gauge.h.  The three compares are separate and in this order -- CMP
   dword ptr [EBP+0x20],0x0 / JLE at 00017762 guarding the filled blit, then
   CMP ...,0x0 / JGE at 00017784 doing the clamp, then CMP ...,0x75 / JGE at
   0001778d guarding the remainder -- so a fill_width of exactly 0 skips the
   first blit and still draws the whole track, and a negative one is clamped
   only after the first blit has already been skipped.  The clamp writes the
   parameter slot itself at 00017786 and the remainder blit reads it back, so
   the clamped value is what positions and sizes the second half.

   The graphic base is worked out before the first compare, at 0001774c, and
   so is computed even on the path that never blits from it.  That is codegen
   and not behaviour: bar_index is only ever 0 to 2 here and the multiply
   cannot trap. */
void fdps_draw_gauge_bar(unsigned char *dst, int dst_stride, int bar_index,
                         int fill_width)
{
    /* [EBP-4]: the filled colour's graphic within the sheet.  The remainder
       deliberately does not go through it. */
    unsigned char *filled_graphic;

    filled_graphic = data_fdps_status_gauge_bar_sheet_ptr
                     + bar_index * GAUGE_BAR_GRAPHIC_STRIDE;

    if (fill_width > 0) {
        fdps_blit_transparent_rect(filled_graphic, GAUGE_BAR_WIDTH, dst,
                                   dst_stride, fill_width, GAUGE_BAR_HEIGHT);
    }

    if (fill_width < 0) {
        fill_width = 0;
    }

    if (fill_width < GAUGE_BAR_WIDTH) {
        fdps_blit_transparent_rect(data_fdps_status_gauge_bar_sheet_ptr
                                       + fill_width,
                                   GAUGE_BAR_WIDTH, dst + fill_width,
                                   dst_stride, GAUGE_BAR_WIDTH - fill_width,
                                   GAUGE_BAR_HEIGHT);
    }
}

/* The combat gauge's fill strip is 125 pixels wide and 5 rows tall, and the
   four strips of the scratch sheet are that many bytes apart at that row
   pitch: PUSH 0x7d for the source stride and PUSH 0x5 for the row count at
   000192ab and 0001929d, and IMUL EAX,dword ptr [EBP+0x1c],0x271 at 0001925c
   for the strip stride.  0x271 is 0x7d * 5, so the four strips are one packed
   0x9c4-byte surface with nothing between them. */
#define GAUGE_FILL_WIDTH 0x7d
#define GAUGE_FILL_HEIGHT 5
#define GAUGE_FILL_STRIP_STRIDE 0x271

/* CMP dword ptr [EBP+0x1c],0x2 / JGE at 0001927b: strips 0 and 1 fill
   right-to-left, 2 and upwards fill left-to-right. */
#define GAUGE_FILL_LEFT_ALIGNED_FROM 2

/* See gauge.h.  The three compares are in this order in the original and the
   order is behaviour, not layout: CMP dword ptr [EBP+0x20],0x0 / JGE at
   0001926e clamps first and writes the parameter slot itself at 00019274, so
   the alignment shift and the blit width both read the clamped value; then
   CMP ...,0x2 / JGE at 0001927b picks the alignment; then CMP ...,0x7d / JG
   at 00019297 decides whether anything is drawn at all.  All three are signed
   compares, so a negative fill_width and a negative gauge_index take the
   branch a value that had been read as unsigned would not.

   The strip base is worked out before the clamp, at 0001925c, and so is
   computed on the path that never blits.  That is codegen and not behaviour:
   the multiply cannot trap and the pointer is only dereferenced inside
   fdps_blit_transparent_rect.

   The original recomputes 0x7d - fill_width once for the strip at 00019286
   and again for dest at 00019291 rather than keeping it; the same value is
   added to both either way. */
void fdps_draw_gauge_fill(unsigned char *dest, int dest_stride,
                          int gauge_index, int fill_width)
{
    /* [EBP-4]: where in the sheet the run to copy starts -- strip
       gauge_index, advanced within that strip's own row when the gauge fills
       from the right. */
    unsigned char *fill_strip;

    fill_strip = data_fdps_gauge_fill_sheet_ptr
                 + gauge_index * GAUGE_FILL_STRIP_STRIDE;

    if (fill_width < 0) {
        fill_width = 0;
    }

    if (gauge_index < GAUGE_FILL_LEFT_ALIGNED_FROM) {
        fill_strip += GAUGE_FILL_WIDTH - fill_width;
        dest += GAUGE_FILL_WIDTH - fill_width;
    }

    if (fill_width <= GAUGE_FILL_WIDTH) {
        fdps_blit_transparent_rect(fill_strip, GAUGE_FILL_WIDTH, dest,
                                   dest_stride, fill_width,
                                   GAUGE_FILL_HEIGHT);
    }
}

/* See gauge.h.  The zero case is written first because that is the order the
   original has its blocks in: CMP dword ptr [EBP+0x20],0x0 / JG at 000192cc
   jumps forward to the division and falls through to the empty gauge, so a
   max of 0 and every negative max never reach the IDIV.

   The division is signed throughout -- IMUL EDX,dword ptr [EBP+0x24],0x7d /
   ADD EDX,dword ptr [EBP+0x20] / DEC EDX / MOV EAX,EDX / SAR EDX,0x1f / IDIV
   dword ptr [EBP+0x20] at 000192db..000192eb -- and the ADD/DEC pair is what
   makes it a ceiling rather than the obvious current * 0x7d / max, so any
   current of 1 or more keeps one lit pixel on screen however large max is.

   Nothing caps the width at the span's own 0x7d, and that is behaviour:
   fdps_draw_gauge_fill drops its blit outright above 0x7d, so a current above
   max leaves the gauge blank where an added min() would fill it. */
void fdps_draw_stat_gauge(unsigned char *dest, int dest_stride,
                          int gauge_index, int max, int current)
{
    /* [EBP-4]: how many of the span's 125 columns the fill strip supplies.
       The original stages it in this local and reads it back to push it, so
       it is a named local here rather than an expression in the call. */
    int fill_width;

    if (max <= 0) {
        fill_width = 0;
    } else {
        fill_width = (current * GAUGE_FILL_WIDTH + max - 1) / max;
    }

    fdps_draw_gauge_fill(dest, dest_stride, gauge_index, fill_width);
}

/* The unit gauge bar is 43 pixels wide and 6 rows tall, and the sheet holds
   its three graphics that many bytes apart at that row pitch: PUSH 0x2b for
   the source stride and PUSH 0x6 for the row count at every one of the twelve
   blits, and IMUL EAX,dword ptr [EBP+0x1c],0x102 at 0001cb0c for the graphic
   stride.  0x102 is 0x2b * 6, so the sheet is three tightly packed frames and
   nothing pads between them. */
#define UNIT_GAUGE_WIDTH 0x2b
#define UNIT_GAUGE_HEIGHT 6
#define UNIT_GAUGE_GRAPHIC_STRIDE 0x102

/* The rounded cap at each end is two pixels wide -- PUSH 0x2 at the first and
   the last blit of every mode -- and the right one begins at column 0x29,
   which is also the width of the interior the fill and the remainder divide
   between them: MOV EAX,0x29 / SUB EAX,dword ptr [EBP+0x20] at 0001cb79.
   0x29 + 2 is the bar's own 0x2b, so the remainder reaches the last column of
   the bar and the right cap is painted back over its last two. */
#define UNIT_GAUGE_CAP_WIDTH 2
#define UNIT_GAUGE_RIGHT_CAP_COLUMN 0x29

/* CMP dword ptr [EBP+0x24],0x0 / JNZ at 0001cb2b and CMP ...,0x1 / JNZ at
   0001cbcb.  These two are the only values with a meaning of their own;
   everything else falls through to the tint painter and is the tint colour. */
#define UNIT_GAUGE_MODE_PLAIN 0
#define UNIT_GAUGE_MODE_BLEND 1

/* The interior the fill and the remainder divide between them is 41 columns
   wide, and that is what a proportion is taken over: IMUL EDX,dword ptr
   [EBP+0x24],0x29 at 0001cabb.  It is the same 0x29 as
   UNIT_GAUGE_RIGHT_CAP_COLUMN above because the right cap begins exactly where
   the interior ends, but the two are different facts about the bar and the
   proportional entry only knows the first one. */
#define UNIT_GAUGE_INTERIOR_WIDTH 0x29

/* See gauge.h.  The empty case is written first because that is the order the
   original has its blocks in: CMP dword ptr [EBP+0x20],0x0 / JG at 0001caac
   jumps forward to the division and falls through to the empty bar, so a
   max_value of 0 and every negative max_value never reach the IDIV.

   The division is signed throughout -- IMUL EDX,dword ptr [EBP+0x24],0x29 /
   ADD EDX,dword ptr [EBP+0x20] / DEC EDX / MOV EAX,EDX / SAR EDX,0x1f / IDIV
   dword ptr [EBP+0x20] at 0001cabb..0001cac8 -- and the ADD/DEC pair is what
   makes it a ceiling rather than the obvious cur_value * 0x29 / max_value, so
   any cur_value of 1 or more keeps one lit pixel on screen however large
   max_value is.

   Nothing caps the width at the interior's own 0x29, and the consequence is a
   smear rather than a full bar: fdps_draw_unit_gauge blits the run out of a
   0x2b-pitch source with no upper clamp of its own, so a cur_value above
   max_value drags the art's next row across the bar where an added min() would
   draw a clean full one.

   The shipped image reaches this code only through the compiler's eight
   inline expansions of it, so nothing here can be checked against a CALL; the
   argument order is the one the body's own stack slots fix, with the divisor
   and the guard both reading [EBP+0x20]. */
void fdps_draw_unit_gauge_proportional(unsigned char *dst, int dst_stride,
                                       int gfx_index, int max_value,
                                       int cur_value, int blit_mode, int alpha)
{
    /* [EBP-4]: how many of the interior's 41 columns the filled graphic
       supplies.  The original stages it in this local and reads it back to
       push it, so it is a named local here rather than an expression in the
       call. */
    int fill_width;

    if (max_value <= 0) {
        fill_width = 0;
    } else {
        fill_width = (cur_value * UNIT_GAUGE_INTERIOR_WIDTH + max_value - 1)
                     / max_value;
    }

    fdps_draw_unit_gauge(dst, dst_stride, gfx_index, fill_width, blit_mode,
                         alpha);
}

/* See gauge.h.  The clamp is at 0001cb1e, before the mode test, so all three
   painters see the clamped value; the graphic base is worked out before it, at
   0001cb0c, and so is computed even in the modes and on the paths that never
   read from it.  That is codegen and not behaviour: the multiply cannot trap
   and the pointer is only dereferenced inside the blits.

   The four segments are written out once per mode rather than through a
   per-segment helper because that is the shape of the original -- three blocks
   of four calls, each block reached by its own branch -- and because the three
   painters take three different argument lists.  Within a block the guard on
   the fill run is CMP dword ptr [EBP+0x20],0x0 / JLE, so a fill_width of
   exactly 0 skips it and the whole interior still comes out of graphic 0.

   The two tables reach the blended painters as arguments and not as globals
   the painters read for themselves: MOV EAX,0x653f0 / PUSH EAX and
   MOV EAX,0x643f0 / PUSH EAX at every one of the eight blended calls. */
void fdps_draw_unit_gauge(unsigned char *dst, int dst_stride, int gfx_index,
                          int fill_width, int blit_mode, int alpha)
{
    /* [EBP-4]: the filled colour's graphic within the sheet.  The remainder
       deliberately does not go through it. */
    unsigned char *filled_graphic;

    filled_graphic = data_fdps_unit_gauge_sheet_ptr
                     + gfx_index * UNIT_GAUGE_GRAPHIC_STRIDE;

    if (fill_width < 0) {
        fill_width = 0;
    }

    if (blit_mode == UNIT_GAUGE_MODE_PLAIN) {
        fdps_blit_transparent_rect(filled_graphic, UNIT_GAUGE_WIDTH, dst,
                                   dst_stride, UNIT_GAUGE_CAP_WIDTH,
                                   UNIT_GAUGE_HEIGHT);
        if (fill_width > 0) {
            fdps_blit_transparent_rect(filled_graphic + UNIT_GAUGE_CAP_WIDTH,
                                       UNIT_GAUGE_WIDTH,
                                       dst + UNIT_GAUGE_CAP_WIDTH, dst_stride,
                                       fill_width, UNIT_GAUGE_HEIGHT);
        }
        fdps_blit_transparent_rect(data_fdps_unit_gauge_sheet_ptr
                                       + UNIT_GAUGE_CAP_WIDTH + fill_width,
                                   UNIT_GAUGE_WIDTH,
                                   dst + UNIT_GAUGE_CAP_WIDTH + fill_width,
                                   dst_stride,
                                   UNIT_GAUGE_RIGHT_CAP_COLUMN - fill_width,
                                   UNIT_GAUGE_HEIGHT);
        fdps_blit_transparent_rect(filled_graphic
                                       + UNIT_GAUGE_RIGHT_CAP_COLUMN,
                                   UNIT_GAUGE_WIDTH,
                                   dst + UNIT_GAUGE_RIGHT_CAP_COLUMN,
                                   dst_stride, UNIT_GAUGE_CAP_WIDTH,
                                   UNIT_GAUGE_HEIGHT);
    } else if (blit_mode == UNIT_GAUGE_MODE_BLEND) {
        fdps_blit_blend_transparent_rect(filled_graphic, UNIT_GAUGE_WIDTH, dst,
                                         dst_stride, dst, dst_stride,
                                         UNIT_GAUGE_CAP_WIDTH,
                                         UNIT_GAUGE_HEIGHT,
                                         data_fdps_palette_shade_ramp_table,
                                         data_fdps_inverse_palette_cube,
                                         alpha);
        if (fill_width > 0) {
            fdps_blit_blend_transparent_rect(
                filled_graphic + UNIT_GAUGE_CAP_WIDTH, UNIT_GAUGE_WIDTH,
                dst + UNIT_GAUGE_CAP_WIDTH, dst_stride,
                dst + UNIT_GAUGE_CAP_WIDTH, dst_stride, fill_width,
                UNIT_GAUGE_HEIGHT, data_fdps_palette_shade_ramp_table,
                data_fdps_inverse_palette_cube, alpha);
        }
        fdps_blit_blend_transparent_rect(
            data_fdps_unit_gauge_sheet_ptr + UNIT_GAUGE_CAP_WIDTH + fill_width,
            UNIT_GAUGE_WIDTH, dst + UNIT_GAUGE_CAP_WIDTH + fill_width,
            dst_stride, dst + UNIT_GAUGE_CAP_WIDTH + fill_width, dst_stride,
            UNIT_GAUGE_RIGHT_CAP_COLUMN - fill_width, UNIT_GAUGE_HEIGHT,
            data_fdps_palette_shade_ramp_table, data_fdps_inverse_palette_cube,
            alpha);
        fdps_blit_blend_transparent_rect(
            filled_graphic + UNIT_GAUGE_RIGHT_CAP_COLUMN, UNIT_GAUGE_WIDTH,
            dst + UNIT_GAUGE_RIGHT_CAP_COLUMN, dst_stride,
            dst + UNIT_GAUGE_RIGHT_CAP_COLUMN, dst_stride,
            UNIT_GAUGE_CAP_WIDTH, UNIT_GAUGE_HEIGHT,
            data_fdps_palette_shade_ramp_table, data_fdps_inverse_palette_cube,
            alpha);
    } else {
        fdps_blit_tint_transparent_rect(filled_graphic, UNIT_GAUGE_WIDTH, dst,
                                        dst_stride, UNIT_GAUGE_CAP_WIDTH,
                                        UNIT_GAUGE_HEIGHT,
                                        data_fdps_palette_shade_ramp_table,
                                        data_fdps_inverse_palette_cube,
                                        blit_mode, alpha);
        if (fill_width > 0) {
            fdps_blit_tint_transparent_rect(
                filled_graphic + UNIT_GAUGE_CAP_WIDTH, UNIT_GAUGE_WIDTH,
                dst + UNIT_GAUGE_CAP_WIDTH, dst_stride, fill_width,
                UNIT_GAUGE_HEIGHT, data_fdps_palette_shade_ramp_table,
                data_fdps_inverse_palette_cube, blit_mode, alpha);
        }
        fdps_blit_tint_transparent_rect(
            data_fdps_unit_gauge_sheet_ptr + UNIT_GAUGE_CAP_WIDTH + fill_width,
            UNIT_GAUGE_WIDTH, dst + UNIT_GAUGE_CAP_WIDTH + fill_width,
            dst_stride, UNIT_GAUGE_RIGHT_CAP_COLUMN - fill_width,
            UNIT_GAUGE_HEIGHT, data_fdps_palette_shade_ramp_table,
            data_fdps_inverse_palette_cube, blit_mode, alpha);
        fdps_blit_tint_transparent_rect(
            filled_graphic + UNIT_GAUGE_RIGHT_CAP_COLUMN, UNIT_GAUGE_WIDTH,
            dst + UNIT_GAUGE_RIGHT_CAP_COLUMN, dst_stride,
            UNIT_GAUGE_CAP_WIDTH, UNIT_GAUGE_HEIGHT,
            data_fdps_palette_shade_ramp_table, data_fdps_inverse_palette_cube,
            blit_mode, alpha);
    }
}

/* The offscreen page one combat frame is composed on: 360 x 240 8bpp at pitch
   0x168, PUSH 0x15180 / CALL malloc at 0001ce89.  The 24-pixel apron on all
   four sides is why the page is wider than the window it presents: a gauge
   whose unit has scrolled to the edge of the view still lands inside the
   allocation instead of over the adapter.

   THE PAGE IS NOT CLEARED.  malloc's block goes straight to the compositor,
   which paints only the layers, cursor and units it is given, so whatever the
   heap left behind shows through everywhere those do not reach.  malloc's
   answer is not tested against NULL either: there is no CMP EAX,0x0 between
   the CALL at 0001ce8e and the store at 0001ce96. */
#define SCENE_PAGE_PITCH 0x168
#define SCENE_PAGE_BYTES 0x15180
#define SCENE_PAGE_BORDER 0x18

/* The window handed to the adapter, and where the adapter answers.  312 x 192
   taken from page byte 0x21d8 -- page coordinate (24,24), the top-left of the
   picture inside the apron -- and put down at screen byte 0x504, screen
   coordinate (4,4).  Both are hard-coded in the original (PUSH 0xa0504 at
   0001d0a7) and stay literals here: 0xa0000 is where the display adapter
   answers, not the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define SCENE_PAGE_WINDOW_AT 0x21d8
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define COMBAT_WINDOW_AT 0x504
#define COMBAT_WINDOW_W 0x138
#define COMBAT_WINDOW_H 0xc0

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, which is what every frame straddles. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The four elements of data_fdps_battle_combat_gauge_pos_pairs, and the x that
   says the attacker has no bar this run: MOV dword ptr [0x0006401c],
   0xffffffff at 0001ce07 and the three CMP ...,-0x1 that read it back. */
#define COMBAT_GAUGE_DEFENDER_X 0
#define COMBAT_GAUGE_DEFENDER_Y 1
#define COMBAT_GAUGE_ATTACKER_X 2
#define COMBAT_GAUGE_ATTACKER_Y 3
#define COMBAT_GAUGE_NO_BAR (-1)

/* CMP EAX,0x1 / JNZ at 0001cdee: fdps_check_can_counter_attack answers -1 on
   every refusal and never 0, so the test is against 1 and is not a bare
   predicate (aitarget.h). */
#define COUNTER_ATTACK_CONFIRMED 1

/* CMP byte ptr [EAX+0x6],0x0 / JNZ at 0001ce32 and 0001ce4b: a side byte of 0
   picks graphic 2 of the unit gauge sheet and every other side picks graphic
   1. */
#define COMBAT_GAUGE_SIDE_ZERO_GRAPHIC 2
#define COMBAT_GAUGE_OTHER_SIDE_GRAPHIC 1

/* The blend strength both animated phases walk: CMP dword ptr [EBP-0x20],0x10
   / JL and ADD dword ptr [EBP-0x20],0x6, so three frames at 0, 6 and 12 out of
   the 16 the blending painters take.  The limit is 0x10 and not 0x0d, so a
   step of 6 would give a fourth frame at 18 if the step were smaller; it is
   the pair that fixes the three. */
#define COMBAT_GAUGE_ALPHA_LIMIT 0x10
#define COMBAT_GAUGE_ALPHA_STEP 6

/* MOV dword ptr [EBP+0xffffff28],0x1a at 0001d139: the second phase's blit
   mode, which fdps_draw_unit_gauge reads as a tint colour index because it is
   neither 0 nor 1. */
#define COMBAT_GAUGE_TINT_COLOR 0x1a

/* See gauge.h.  The two animated phases are the same eleven statements written
   out twice and the closing frame is the same again with both painting
   arguments fixed at 0.  The original has all three inline and not behind a
   call, so they stay written out here; the seven calls of
   fdps_draw_unit_gauge_proportional the source made are inline expansions in
   the shipped image (rebuild_info/build_flags.md) and are open-coded here for
   the same reason -- writing them as calls would put seven CALLs in the
   rebuild that the original does not have.

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED, the same contract
   fdps_animate_turn_banner and fdps_play_vfs_animation (anim.c) carry.
   last_tick is read at 0001d0c2 before anything has written it, so the first
   of the seven frames ends its wait at once unless the stack garbage happens
   to equal the counter.  Writing the obvious last_tick =
   data_fdps_timer_tick_counter before the first loop adds a tick of delay the
   original does not have (rebuild_info/pitfalls.md).

   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of these waits: nothing inside them writes the counter, so a build
   allowed to hoist the load would spin here forever.  The retrace spins read a
   port and cannot be hoisted for the same reason.

   THE ATTACKER'S DESTINATION IS FORMED EVEN WHEN THERE IS NO ATTACKER BAR.
   The multiply and the two adds at 0001ce99 run before the loops and read
   element 2 whatever it holds, so with the -1 in place the pointer is one byte
   short of where a zero would put it.  Nothing dereferences it on that path --
   every use is inside the CMP ...,-0x1 guard -- so it is arithmetic on a
   pointer that is never read, not a fault.  Guarding it changes nothing on
   screen.

   NO CALL'S ANSWER IS READ EXCEPT THREE.  fdps_check_can_counter_attack's is
   compared against 1, the two fdps_get_unit_record pointers are the records
   every field below comes out of, and malloc's is the page.  inp's is tested
   for bit 3 at each of the six spins.  fdps_battle_compute_unit_gauge_position,
   fdps_draw_scene_layers, fdps_draw_unit_gauge, fdps_blit_rect and free all
   return nothing the original reads.

   The seven frames are paced by the retrace and the timer tick, so the number
   of instructions between them is not observable (contract D); what is
   observable is that the last frame does NOT wait for a tick, which is what
   lets the attack animation start against the bars immediately. */
int *fdps_battle_show_combat_gauges(int attacker_unit, int defender_unit)
{
    /* The two records every field below is read out of.  Both are resolved
       once, before the page is allocated, and the loops re-read neither. */
    struct fdps_unit_record *attacker;
    struct fdps_unit_record *defender;
    /* The 360x240 page the frame is composed on, allocated and freed here. */
    unsigned char *scene_page;
    /* Where each bar's top-left pixel sits in that page, the position pair
       with the page's 24-pixel border added on both axes. */
    unsigned char *attacker_bar_pixel;
    unsigned char *defender_bar_pixel;
    /* Which of the sheet's three graphics fills each bar. */
    int attacker_gfx_index;
    int defender_gfx_index;
    /* The HP pair each bar is filled from, widened from the record's signed
       words by MOVSX at 0001ce64 and the three that follow it. */
    int attacker_hp_current;
    int attacker_hp_max;
    int defender_hp_current;
    int defender_hp_max;
    /* How many of the bar's 41 interior columns this frame fills. */
    int attacker_fill_width;
    int defender_fill_width;
    /* The blend strength this frame paints at, 0, 6 or 12 of 16. */
    int alpha;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    fdps_battle_compute_unit_gauge_position(
        &data_fdps_battle_combat_gauge_pos_pairs[COMBAT_GAUGE_DEFENDER_X],
        defender_unit);
    if (fdps_check_can_counter_attack(attacker_unit, defender_unit)
            == COUNTER_ATTACK_CONFIRMED) {
        fdps_battle_compute_unit_gauge_position(
            &data_fdps_battle_combat_gauge_pos_pairs[COMBAT_GAUGE_ATTACKER_X],
            attacker_unit);
    } else {
        data_fdps_battle_combat_gauge_pos_pairs[COMBAT_GAUGE_ATTACKER_X] =
            COMBAT_GAUGE_NO_BAR;
    }

    attacker = fdps_get_unit_record(attacker_unit);
    defender = fdps_get_unit_record(defender_unit);

    if (attacker->side == 0) {
        attacker_gfx_index = COMBAT_GAUGE_SIDE_ZERO_GRAPHIC;
    } else {
        attacker_gfx_index = COMBAT_GAUGE_OTHER_SIDE_GRAPHIC;
    }
    if (defender->side == 0) {
        defender_gfx_index = COMBAT_GAUGE_SIDE_ZERO_GRAPHIC;
    } else {
        defender_gfx_index = COMBAT_GAUGE_OTHER_SIDE_GRAPHIC;
    }

    attacker_hp_current = (int) attacker->hp_current;
    attacker_hp_max = (int) attacker->hp_max;
    defender_hp_current = (int) defender->hp_current;
    defender_hp_max = (int) defender->hp_max;

    scene_page = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);
    attacker_bar_pixel = scene_page
        + (data_fdps_battle_combat_gauge_pos_pairs[COMBAT_GAUGE_ATTACKER_Y]
           + SCENE_PAGE_BORDER) * SCENE_PAGE_PITCH
        + data_fdps_battle_combat_gauge_pos_pairs[COMBAT_GAUGE_ATTACKER_X]
        + SCENE_PAGE_BORDER;
    defender_bar_pixel = scene_page
        + (data_fdps_battle_combat_gauge_pos_pairs[COMBAT_GAUGE_DEFENDER_Y]
           + SCENE_PAGE_BORDER) * SCENE_PAGE_PITCH
        + data_fdps_battle_combat_gauge_pos_pairs[COMBAT_GAUGE_DEFENDER_X]
        + SCENE_PAGE_BORDER;

    for (alpha = 0; alpha < COMBAT_GAUGE_ALPHA_LIMIT;
         alpha += COMBAT_GAUGE_ALPHA_STEP) {
        fdps_draw_scene_layers(scene_page);
        if (data_fdps_battle_combat_gauge_pos_pairs[COMBAT_GAUGE_ATTACKER_X]
                != COMBAT_GAUGE_NO_BAR) {
            if (attacker_hp_max <= 0) {
                attacker_fill_width = 0;
            } else {
                attacker_fill_width =
                    (attacker_hp_current * UNIT_GAUGE_INTERIOR_WIDTH
                     + attacker_hp_max - 1) / attacker_hp_max;
            }
            fdps_draw_unit_gauge(attacker_bar_pixel, SCENE_PAGE_PITCH,
                                 attacker_gfx_index, attacker_fill_width,
                                 UNIT_GAUGE_MODE_BLEND, alpha);
        }
        if (defender_hp_max <= 0) {
            defender_fill_width = 0;
        } else {
            defender_fill_width =
                (defender_hp_current * UNIT_GAUGE_INTERIOR_WIDTH
                 + defender_hp_max - 1) / defender_hp_max;
        }
        fdps_draw_unit_gauge(defender_bar_pixel, SCENE_PAGE_PITCH,
                             defender_gfx_index, defender_fill_width,
                             UNIT_GAUGE_MODE_BLEND, alpha);
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the blit starts clear of it. */
        }
        fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                       SCENE_PAGE_PITCH,
                       (void *) (VGA_SCREEN_BASE + COMBAT_WINDOW_AT),
                       VGA_SCREEN_PITCH, COMBAT_WINDOW_W, COMBAT_WINDOW_H);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    for (alpha = 0; alpha < COMBAT_GAUGE_ALPHA_LIMIT;
         alpha += COMBAT_GAUGE_ALPHA_STEP) {
        fdps_draw_scene_layers(scene_page);
        if (data_fdps_battle_combat_gauge_pos_pairs[COMBAT_GAUGE_ATTACKER_X]
                != COMBAT_GAUGE_NO_BAR) {
            if (attacker_hp_max <= 0) {
                attacker_fill_width = 0;
            } else {
                attacker_fill_width =
                    (attacker_hp_current * UNIT_GAUGE_INTERIOR_WIDTH
                     + attacker_hp_max - 1) / attacker_hp_max;
            }
            fdps_draw_unit_gauge(attacker_bar_pixel, SCENE_PAGE_PITCH,
                                 attacker_gfx_index, attacker_fill_width,
                                 COMBAT_GAUGE_TINT_COLOR, alpha);
        }
        if (defender_hp_max <= 0) {
            defender_fill_width = 0;
        } else {
            defender_fill_width =
                (defender_hp_current * UNIT_GAUGE_INTERIOR_WIDTH
                 + defender_hp_max - 1) / defender_hp_max;
        }
        fdps_draw_unit_gauge(defender_bar_pixel, SCENE_PAGE_PITCH,
                             defender_gfx_index, defender_fill_width,
                             COMBAT_GAUGE_TINT_COLOR, alpha);
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends. */
        }
        fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                       SCENE_PAGE_PITCH,
                       (void *) (VGA_SCREEN_BASE + COMBAT_WINDOW_AT),
                       VGA_SCREEN_PITCH, COMBAT_WINDOW_W, COMBAT_WINDOW_H);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    fdps_draw_scene_layers(scene_page);
    if (data_fdps_battle_combat_gauge_pos_pairs[COMBAT_GAUGE_ATTACKER_X]
            != COMBAT_GAUGE_NO_BAR) {
        if (attacker_hp_max <= 0) {
            attacker_fill_width = 0;
        } else {
            attacker_fill_width = (attacker_hp_current
                                   * UNIT_GAUGE_INTERIOR_WIDTH
                                   + attacker_hp_max - 1) / attacker_hp_max;
        }
        fdps_draw_unit_gauge(attacker_bar_pixel, SCENE_PAGE_PITCH,
                             attacker_gfx_index, attacker_fill_width,
                             UNIT_GAUGE_MODE_PLAIN, 0);
    }
    if (defender_hp_max <= 0) {
        defender_fill_width = 0;
    } else {
        defender_fill_width = (defender_hp_current * UNIT_GAUGE_INTERIOR_WIDTH
                               + defender_hp_max - 1) / defender_hp_max;
    }
    fdps_draw_unit_gauge(defender_bar_pixel, SCENE_PAGE_PITCH,
                         defender_gfx_index, defender_fill_width,
                         UNIT_GAUGE_MODE_PLAIN, 0);
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        /* Spin until the retrace begins. */
    }
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        /* And until it ends. */
    }
    fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                   SCENE_PAGE_PITCH,
                   (void *) (VGA_SCREEN_BASE + COMBAT_WINDOW_AT),
                   VGA_SCREEN_PITCH, COMBAT_WINDOW_W, COMBAT_WINDOW_H);
    free(scene_page);

    return data_fdps_battle_combat_gauge_pos_pairs;
}

/* One map tile is 24 pixels square, and a map object's view position is its
   tile times this minus the view window origin.  IMUL EAX,EAX,0x18 at
   0001d5f5 and 0001d613. */
#define MAP_TILE_SIZE 0x18

/* The extra 4 pixels x carries over the plain tile conversion.  It is part of
   the position the gauge is measured from, not part of a step off the unit:
   ADD EDX,0x4 at 0001d603 happens before any of the branches. */
#define GAUGE_ANCHOR_X_SHIFT 4

/* The facing byte, record offset 3, is 0 down, 1 left, 2 up, 3 right, and the
   placement splits it at 2 -- CMP EAX,0x2 / JGE at 0001d62f.  Facings below
   this go above and to the right, facings from it up go below and to the
   left. */
#define GAUGE_FIRST_LOW_LEFT_FACING 2

/* The preferred vertical steps, up for the first pair of facings and down for
   the second, and the single nudge that replaces either of them when it would
   put the gauge off the top or the bottom of the screen.  The nudge is
   downward in both cases, which is why a clamped gauge sits on the unit
   rather than on the unit's other side. */
#define GAUGE_UP_STEP 0x10
#define GAUGE_DOWN_STEP 0x16
#define GAUGE_CLAMPED_Y_NUDGE 5

/* The two vertical limits.  The upward step is taken only while it leaves y
   past the top margin -- CMP EAX,0x4 / JG at 0001d63d, so y - 0x10 must reach
   5 -- and the downward step only while it leaves y short of 0xc3, which is
   where the gauge's six rows stop fitting above the bottom of the screen. */
#define GAUGE_TOP_MARGIN 4
#define GAUGE_BOTTOM_LIMIT 0xc3

/* The horizontal steps.  Right is preferred for the first pair of facings and
   left for the second, and each has its own fallback in the other direction;
   the fallbacks are 0x2d and 0x1c against preferred steps of 0x18 and 0x2c,
   so neither pair mirrors the other and all four are written out literally in
   the original. */
#define GAUGE_RIGHT_STEP 0x18
#define GAUGE_CLAMPED_LEFT_STEP 0x2d
#define GAUGE_LEFT_STEP 0x2c
#define GAUGE_CLAMPED_RIGHT_STEP 0x1c

/* The two horizontal limits.  0x45 is the right step plus the 0x2d-pixel box
   the gauge occupies, so the right step is taken only while the gauge's far
   edge stays short of 0x13b; the left step is taken only while it leaves x
   past the same 4-pixel margin the top uses -- CMP EAX,0x4 / JG at
   0001d699. */
#define GAUGE_RIGHT_EXTENT 0x45
#define GAUGE_RIGHT_LIMIT 0x13b
#define GAUGE_LEFT_MARGIN 4

/* See gauge.h.  The original recomputes each coordinate out of the caller's
   pair at every step rather than holding it in a register, and the pair is
   written before the branches rather than at the end, so a caller that passed
   two ints it also reads from elsewhere sees the intermediate values; nothing
   in the image does.

   Each of the four clamps is written with the fallback as the THEN arm, which
   is the way round the original has them: its JG at 0001d640 and JL at
   0001d65f jump to the preferred step and fall through to the fallback.  The
   other way round is the same behaviour and only mirrors the two blocks, but
   this way the emitted object comes out with the original's block order and
   jump senses, which is one less difference to explain to the next reader. */
void fdps_battle_compute_unit_gauge_position(int *out_position, int unit_index)
{
    struct fdps_unit_record *unit;

    unit = fdps_get_unit_record(unit_index);

    /* Both record bytes are widened unsigned -- MOV AL,byte ptr [EAX] then
       AND EAX,0xff at 0001d5ee and 0001d60b -- so a tile column past 127 is
       far to the right and not far to the left. */
    out_position[0] = (int) unit->pos_x * MAP_TILE_SIZE
                      - data_fdps_battle_view_window_origin_x
                      + GAUGE_ANCHOR_X_SHIFT;
    out_position[1] = (int) unit->pos_y * MAP_TILE_SIZE
                      - data_fdps_battle_view_window_origin_y;

    if (unit->facing < GAUGE_FIRST_LOW_LEFT_FACING) {
        if (out_position[1] - GAUGE_UP_STEP <= GAUGE_TOP_MARGIN) {
            out_position[1] += GAUGE_CLAMPED_Y_NUDGE;
        } else {
            out_position[1] -= GAUGE_UP_STEP;
        }
        if (out_position[0] + GAUGE_RIGHT_EXTENT >= GAUGE_RIGHT_LIMIT) {
            out_position[0] -= GAUGE_CLAMPED_LEFT_STEP;
        } else {
            out_position[0] += GAUGE_RIGHT_STEP;
        }
    } else {
        if (out_position[1] + GAUGE_DOWN_STEP >= GAUGE_BOTTOM_LIMIT) {
            out_position[1] += GAUGE_CLAMPED_Y_NUDGE;
        } else {
            out_position[1] += GAUGE_DOWN_STEP;
        }
        if (out_position[0] - GAUGE_LEFT_STEP <= GAUGE_LEFT_MARGIN) {
            out_position[0] += GAUGE_CLAMPED_RIGHT_STEP;
        } else {
            out_position[0] -= GAUGE_LEFT_STEP;
        }
    }
}
