/* tests/gauge.c -- cover for src/gauge.c.
 *
 * Every expected value below was worked out from the assembly at 0001d5d0 --
 * IMUL EAX,EAX,0x18 for the tile conversion, SUB EAX,[0x00069ce4] and
 * SUB EBX,EDX for the two window origins, ADD EDX,0x4 for the x anchor shift,
 * CMP EAX,0x2 / JGE for the facing split, and the four
 * compare-and-branch pairs at 0001d63d, 0001d65a, 0001d67a and 0001d699 --
 * together with the unit record layout ticket 17 settled (pos_x at +0, pos_y
 * at +1, facing at +3, record size 0x50).  Nothing here was read off the
 * emitted C.
 *
 * The function takes its whole input from the unit record and the two view
 * window origin globals, so the tests stage a block of records, point
 * data_fdps_map_unit_array_ptr at it and set the origins directly.  Nothing
 * below asserts what any of those globals holds on its own -- ticket 23 owns
 * that.
 *
 * The gauge bar cases further down come from the assembly at 00017740 --
 * IMUL EAX,dword ptr [EBP+0x1c],0x3a8 for the graphic stride, PUSH 0x75 and
 * PUSH 0x8 for the source pitch and the row count at both blits, the three
 * compares at 00017762, 00017784 and 0001778d, and MOV EAX,[0x000643c8] /
 * ADD EAX,[EBP+0x20] at 000177ab, which is what makes the remainder come out
 * of graphic 0 rather than out of the filled one.  They stage a sheet of
 * their own and point data_fdps_status_gauge_bar_sheet_ptr at it, for the
 * same reason: ticket 23 owns what the real sheet holds.
 *
 * The combat gauge fill cases come from the assembly at 00019250 -- IMUL
 * EAX,dword ptr [EBP+0x1c],0x271 at 0001925c for the strip stride, the signed
 * CMP/JGE pair at 0001926e for the clamp, the CMP ...,0x2 / JGE at 0001927b
 * for the alignment split, the two MOV EAX,0x7d / SUB EAX,[EBP+0x20] shifts at
 * 00019281 and 0001928c, the CMP ...,0x7d / JG at 00019297 that drops an
 * overfull gauge, and PUSH 0x7d / PUSH 0x5 at the call -- together with the
 * 0x9c4 the two builders malloc at 00018e15 and 0001a603.  They stage a sheet
 * of their own and point data_fdps_gauge_fill_sheet_ptr at it as well, since
 * in the shipped game that global is only live inside a combat animation.
 *
 * The unit gauge cases come from the assembly at 0001cb00 -- IMUL EAX,dword
 * ptr [EBP+0x1c],0x102 at 0001cb0c for the graphic stride, the signed CMP/JGE
 * clamp at 0001cb1e, the CMP ...,0x0 / JNZ at 0001cb2b and CMP ...,0x1 / JNZ at
 * 0001cbcb that pick the painter, PUSH 0x2b and PUSH 0x6 at every blit, PUSH
 * 0x2 for the two caps, MOV EAX,0x29 / SUB EAX,[EBP+0x20] at 0001cb79 for the
 * remainder's width, and MOV EAX,[0x000643b4] at 0001cb92 -- which is what
 * makes the remainder come out of graphic 0 -- together with the 0x306 the
 * loader mallocs at 00029cd5.  They stage a sheet of their own and point
 * data_fdps_unit_gauge_sheet_ptr at it, and the blended cases stage the two
 * blending tables as well, for the same reason: ticket 23 owns what any of the
 * three really holds.
 *
 * The stat gauge cases at the very end come from the assembly at 000192c0 --
 * CMP dword ptr [EBP+0x20],0x0 / JG at 000192cc..000192d0 for the empty path,
 * IMUL EDX,dword ptr [EBP+0x24],0x7d / ADD EDX,dword ptr [EBP+0x20] / DEC EDX
 * / MOV EAX,EDX / SAR EDX,0x1f / IDIV dword ptr [EBP+0x20] at
 * 000192db..000192eb for the signed ceiling, and the four pushes at
 * 000192f1..000192fd for what reaches fdps_draw_gauge_fill.  The width itself
 * is not observable -- the function returns nothing -- so every case reads it
 * back off the fill canvas, where the run's far edge sits at exactly that
 * column.  They reuse the fill sheet staging above, since the width is only
 * visible through the real fdps_draw_gauge_fill.
 *
 * The proportional unit gauge cases after those come from the assembly at
 * 0001caa0 -- CMP dword ptr [EBP+0x20],0x0 / JG at 0001caac..0001cab0 for the
 * empty path, IMUL EDX,dword ptr [EBP+0x24],0x29 / ADD EDX,dword ptr
 * [EBP+0x20] / DEC EDX / MOV EAX,EDX / SAR EDX,0x1f / IDIV dword ptr
 * [EBP+0x20] at 0001cabb..0001cac8 for the signed ceiling over the bar's
 * 41-column interior, and the six pushes at 0001cace..0001cae5 for what
 * reaches fdps_draw_unit_gauge.  The width itself is not observable either, so
 * every case reads it back off the unit canvas at the seam between the filled
 * graphic and the track, and the reading is only sharp out to column 0x28:
 * the right cap paints back over the interior's last two columns, so a full
 * bar is told from a nearly full one the way the remainder case above is, by
 * planting a transparent cap and seeing whether anything is left underneath.
 * They reuse the unit sheet and table staging above.
 */
#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "gauge.h"

/* Four records, so an index other than 0 has somewhere to land and a walk
   that strayed into a neighbouring record would be visible. */
#define STAGE_UNITS 4

/* Written into both output slots before every call.  The function writes both
   unconditionally, so a slot still holding this after a call means a path
   left one of them alone. */
#define UNWRITTEN (-12345)

static struct fdps_unit_record stage_units[STAGE_UNITS];

/* Zero every record, point the global at the block and park the view at the
   map origin.  A case that cares about the scroll sets the origins itself
   afterwards. */
static void stage(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
}

/* Fill one record's three read bytes.  The values go in as bytes because that
   is what the record holds; a column of 200 is a legal column. */
static void set_unit(int unit_index, int tile_column, int tile_row,
                     int facing)
{
    stage_units[unit_index].pos_x = (unsigned char) tile_column;
    stage_units[unit_index].pos_y = (unsigned char) tile_row;
    stage_units[unit_index].facing = (unsigned char) facing;
}

/* One call with the output slots poisoned first, so every assertion that
   follows also proves the slot was written. */
static void place(int unit_index, int *position)
{
    position[0] = UNWRITTEN;
    position[1] = UNWRITTEN;
    fdps_battle_compute_unit_gauge_position(position, unit_index);
}

/* IMUL EAX,dword ptr [EBP+0x14],0x50 in fdps_get_unit_record at 0002d21c is
   the stride, and the three bytes this function reads are record offsets 0, 1
   and 3.  If the record were a different size or those fields sat elsewhere,
   every case below would be addressing different bytes. */
static void unit_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, facing), 3);
}

/* The anchor with the view at the map origin and the unit on tile (0, 0):
   x is 0 * 0x18 - 0 + 4 = 4 and y is 0.  Facing 0 then takes the upward step,
   which 0 cannot afford, so y ends at the +5 nudge, and the right step, which
   4 can afford, so x ends at 0x1c.  This is the only case that pins the
   4-pixel x shift on its own: drop it and x would come out at 0x18. */
static void anchor_is_the_tile_times_24_plus_4(void)
{
    int position[2];

    stage();
    set_unit(0, 0, 0, 0);
    place(0, position);
    CHECK_EQ(position[0], 28);
    CHECK_EQ(position[1], 5);
}

/* Tile (10, 8) with the view scrolled to (0x10, 0x18), read through index 2:
   x is 240 - 16 + 4 = 228 and y is 192 - 24 = 168, and both offsets are then
   affordable, so x becomes 228 + 0x18 = 252 and y becomes 168 - 0x10 = 152.
   Index 2 is what makes a wrong stride visible -- the other records are all
   zero, which would place the gauge somewhere else entirely. */
static void scroll_origin_is_subtracted_and_the_index_scales(void)
{
    int position[2];

    stage();
    set_unit(2, 10, 8, 0);
    data_fdps_battle_view_window_origin_x = 16;
    data_fdps_battle_view_window_origin_y = 24;
    place(2, position);
    CHECK_EQ(position[0], 252);
    CHECK_EQ(position[1], 152);
}

/* CMP EAX,0x2 / JGE at 0001d62f puts facing 1 on the same side of the split
   as facing 0, so left-facing and down-facing units are placed identically.
   Same inputs as the case above. */
static void facing_1_places_like_facing_0(void)
{
    int position[2];

    stage();
    set_unit(2, 10, 8, 1);
    data_fdps_battle_view_window_origin_x = 16;
    data_fdps_battle_view_window_origin_y = 24;
    place(2, position);
    CHECK_EQ(position[0], 252);
    CHECK_EQ(position[1], 152);
}

/* ADD EAX,0x45 / CMP EAX,0x13b / JL at 0001d657: the right step is taken only
   while the sum stays BELOW 0x13b, so an anchor of 0xf6 -- whose sum is
   exactly 0x13b -- takes the fallback and one pixel less does not.  The two
   answers also pin the asymmetry: the fallback is -0x2d against a preferred
   +0x18, so it is not the mirror of the step it replaces.

   Anchor 0xf6 is tile column 11 with the view at x = 22: 264 - 22 + 4 = 246.
   Anchor 0xf5 is the same column one pixel further scrolled. */
static void right_step_falls_back_at_the_limit_not_before(void)
{
    int position[2];

    stage();
    set_unit(0, 11, 1, 0);
    data_fdps_battle_view_window_origin_x = 22;
    place(0, position);
    CHECK_EQ(position[0], 201);
    CHECK_EQ(position[1], 8);

    stage();
    set_unit(0, 11, 1, 0);
    data_fdps_battle_view_window_origin_x = 23;
    place(0, position);
    CHECK_EQ(position[0], 269);
    CHECK_EQ(position[1], 8);
}

/* SUB EAX,0x10 / CMP EAX,0x4 / JG at 0001d63a: the upward step is taken only
   while it leaves y ABOVE 4, so an anchor of 21 takes it and lands on 5 while
   an anchor of 20 falls back to the +5 nudge and lands on 25 -- below where it
   started, and above the unit rather than below it.  Anchor 21 is tile row 1
   with the view at y = 3; anchor 20 is the same row one pixel further
   scrolled. */
static void up_step_falls_back_at_the_top_margin_not_before(void)
{
    int position[2];

    stage();
    set_unit(0, 0, 1, 0);
    data_fdps_battle_view_window_origin_y = 3;
    place(0, position);
    CHECK_EQ(position[0], 28);
    CHECK_EQ(position[1], 5);

    stage();
    set_unit(0, 0, 1, 0);
    data_fdps_battle_view_window_origin_y = 4;
    place(0, position);
    CHECK_EQ(position[0], 28);
    CHECK_EQ(position[1], 25);
}

/* Both x compares are the signed ones -- JL at 0001d65f and JG at 0001d69c --
   and a unit scrolled off the left of the view has a negative anchor, which is
   the only place the distinction shows.  Anchor 4 - 100 = -96: the sum -96 +
   0x45 = -27 is below 0x13b, so the right step is taken and x ends at -72.
   Read unsigned, -27 is enormous, the fallback would be taken instead and x
   would end at -141. */
static void negative_x_takes_the_signed_branch(void)
{
    int position[2];

    stage();
    set_unit(0, 0, 0, 0);
    data_fdps_battle_view_window_origin_x = 100;
    place(0, position);
    CHECK_EQ(position[0], -72);
    CHECK_EQ(position[1], 5);
}

/* The same for y, whose compares are JG at 0001d640 and JL at 0001d67f.
   Anchor -50: -50 - 0x10 = -66 does not clear the margin, so the nudge is
   taken and y ends at -45.  Read unsigned, -66 clears it and y would end at
   -66. */
static void negative_y_takes_the_signed_branch(void)
{
    int position[2];

    stage();
    set_unit(0, 0, 0, 0);
    data_fdps_battle_view_window_origin_y = 50;
    place(0, position);
    CHECK_EQ(position[0], 28);
    CHECK_EQ(position[1], -45);
}

/* MOV AL,byte ptr [EAX] / AND EAX,0xff at 0001d5ee and 0001d60b widen both
   tile bytes UNSIGNED, so column 200 is 4800 world pixels to the right and not
   56 tiles to the left.  Column 200 gives an anchor of 4804, whose sum has
   long passed 0x13b, so the fallback lands x on 4759; read as a signed char
   the anchor would be -1340 and the answer -1316.  Row 200 gives an anchor of
   4800, which can afford the upward step and lands y on 4784; read signed it
   would be -1344 and the answer -1339. */
static void tile_bytes_widen_unsigned(void)
{
    int position[2];

    stage();
    set_unit(0, 200, 0, 0);
    place(0, position);
    CHECK_EQ(position[0], 4759);
    CHECK_EQ(position[1], 5);

    stage();
    set_unit(0, 0, 200, 0);
    place(0, position);
    CHECK_EQ(position[0], 28);
    CHECK_EQ(position[1], 4784);
}

/* Facing 2 takes the other side of the split: down by 0x16 and left by 0x2c.
   Tile (10, 8) with the view at (0x10, 0x18) again, so the anchor is the same
   (228, 168) as the facing-0 case above and the two answers can be compared
   directly -- 228 - 0x2c = 184 against 252, and 168 + 0x16 = 190 against
   152. */
static void facing_2_goes_down_and_left(void)
{
    int position[2];

    stage();
    set_unit(2, 10, 8, 2);
    data_fdps_battle_view_window_origin_x = 16;
    data_fdps_battle_view_window_origin_y = 24;
    place(2, position);
    CHECK_EQ(position[0], 184);
    CHECK_EQ(position[1], 190);
}

/* Facing 3 is on the same side of the split as facing 2, and so is any byte
   above 3: the compare is against a byte widened to 0..255, so facing 200 is
   200 and takes the low-left branch.  Read as a signed char it would be -56,
   which is below the split, and the gauge would jump to the other side. */
static void facing_3_and_beyond_place_like_facing_2(void)
{
    int position[2];

    stage();
    set_unit(2, 10, 8, 3);
    data_fdps_battle_view_window_origin_x = 16;
    data_fdps_battle_view_window_origin_y = 24;
    place(2, position);
    CHECK_EQ(position[0], 184);
    CHECK_EQ(position[1], 190);

    stage();
    set_unit(2, 10, 8, 200);
    data_fdps_battle_view_window_origin_x = 16;
    data_fdps_battle_view_window_origin_y = 24;
    place(2, position);
    CHECK_EQ(position[0], 184);
    CHECK_EQ(position[1], 190);
}

/* ADD EAX,0x16 / CMP EAX,0xc3 / JL at 0001d677: the downward step is taken
   only while the sum stays BELOW 0xc3, so an anchor of 172 takes it and lands
   on 194 while 173 -- whose sum is exactly 0xc3 -- falls back to the +5 nudge
   and lands on 178, above where the step would have put it.  Anchor 172 is
   tile row 8 with the view at y = 20; 173 is the same row one pixel less
   scrolled. */
static void down_step_falls_back_at_the_limit_not_before(void)
{
    int position[2];

    stage();
    set_unit(0, 10, 8, 2);
    data_fdps_battle_view_window_origin_x = 16;
    data_fdps_battle_view_window_origin_y = 20;
    place(0, position);
    CHECK_EQ(position[0], 184);
    CHECK_EQ(position[1], 194);

    stage();
    set_unit(0, 10, 8, 2);
    data_fdps_battle_view_window_origin_x = 16;
    data_fdps_battle_view_window_origin_y = 19;
    place(0, position);
    CHECK_EQ(position[0], 184);
    CHECK_EQ(position[1], 178);
}

/* SUB EAX,0x2c / CMP EAX,0x4 / JG at 0001d696: the left step is taken only
   while it leaves x ABOVE 4, so an anchor of 49 takes it and lands on 5 while
   48 falls back and lands on 76.  The fallback is +0x1c, not the +0x2c that
   would mirror the step, which is the second half of the asymmetry the right
   case above shows.  Anchor 49 is tile column 2 with the view at x = 3; 48 is
   the same column one pixel further scrolled. */
static void left_step_falls_back_at_the_margin_not_before(void)
{
    int position[2];

    stage();
    set_unit(0, 2, 0, 2);
    data_fdps_battle_view_window_origin_x = 3;
    place(0, position);
    CHECK_EQ(position[0], 5);
    CHECK_EQ(position[1], 22);

    stage();
    set_unit(0, 2, 0, 2);
    data_fdps_battle_view_window_origin_x = 4;
    place(0, position);
    CHECK_EQ(position[0], 76);
    CHECK_EQ(position[1], 22);
}

/* ------------------------------------------------------------------ *
 * fdps_draw_gauge_bar @ 00017740
 * ------------------------------------------------------------------ */

/* The sheet geometry, written out here rather than taken from a header so a
   test that disagreed with the assembly would show up as a failure and not as
   agreement between the code and itself: 0x3a8 between graphics, 0x75 per
   source row, 0x75 columns and 8 rows to a bar, three graphics to the
   0xaf8-byte sheet. */
#define ART_GRAPHIC_STRIDE 0x3a8
#define ART_ROW_PITCH 0x75
#define BAR_WIDTH 0x75
#define BAR_ROWS 8
#define SHEET_BYTES 0xaf8

/* A destination wider than any bar drawn into it, and with rows above, below
   and to either side of where the bar goes, so a blit that ran one column too
   far or one row too low lands on a guard byte rather than off the array. */
#define DST_PITCH 0x90
#define DST_ROWS 12
#define BAR_ORIGIN_ROW 2
#define BAR_ORIGIN_COLUMN 8
#define DST_GUARD 0xee

static unsigned char bar_sheet[SHEET_BYTES];
static unsigned char bar_canvas[DST_ROWS * DST_PITCH];

/* Every sheet byte is distinct from its neighbours and from the byte 0x75 or
   0x3a8 further on, which is what lets an assertion tell "graphic 1 column 9"
   from "graphic 0 column 9" and from "graphic 1 row 1 column 0".  Values run
   1..251, so none of them is the transparency key by accident; the cases that
   want a transparent pixel plant a 0 themselves. */
static void stage_bar(void)
{
    int offset;

    for (offset = 0; offset < SHEET_BYTES; offset++) {
        bar_sheet[offset] = (unsigned char) (offset % 251 + 1);
    }
    for (offset = 0; offset < DST_ROWS * DST_PITCH; offset++) {
        bar_canvas[offset] = DST_GUARD;
    }
    data_fdps_status_gauge_bar_sheet_ptr = bar_sheet;
}

/* Where the caller says the bar's top-left pixel is. */
static unsigned char *bar_dst(void)
{
    return bar_canvas + BAR_ORIGIN_ROW * DST_PITCH + BAR_ORIGIN_COLUMN;
}

/* One destination pixel of the bar, addressed in the bar's own coordinates so
   a case can name a column of the art directly. */
static int drawn(int row, int column)
{
    return (int) bar_canvas[(BAR_ORIGIN_ROW + row) * DST_PITCH
                            + BAR_ORIGIN_COLUMN + column];
}

/* One source pixel of the art, addressed the way the assembly addresses it. */
static int art(int graphic, int row, int column)
{
    return (int) bar_sheet[graphic * ART_GRAPHIC_STRIDE
                           + row * ART_ROW_PITCH + column];
}

/* The bar is two blits that meet at fill_width: columns 0..9 out of graphic
   1, columns 10..0x74 out of graphic 0 AT THEIR OWN COLUMNS -- the second
   source is base + fill_width, not base -- and nothing outside the 0x75 by 8
   rectangle.  Row 7 is checked as well as row 0, because both blits are
   handed 8 as their row count and advance the destination by dst_stride. */
static void filled_half_and_track_meet_at_fill_width(void)
{
    stage_bar();
    fdps_draw_gauge_bar(bar_dst(), DST_PITCH, 1, 10);

    CHECK_EQ(drawn(0, 0), art(1, 0, 0));
    CHECK_EQ(drawn(0, 9), art(1, 0, 9));
    CHECK_EQ(drawn(0, 10), art(0, 0, 10));
    CHECK_EQ(drawn(0, BAR_WIDTH - 1), art(0, 0, BAR_WIDTH - 1));
    CHECK_EQ(drawn(7, 9), art(1, 7, 9));
    CHECK_EQ(drawn(7, 10), art(0, 7, 10));

    CHECK_EQ(drawn(0, -1), DST_GUARD);
    CHECK_EQ(drawn(0, BAR_WIDTH), DST_GUARD);
    CHECK_EQ(drawn(-1, 0), DST_GUARD);
    CHECK_EQ(drawn(BAR_ROWS, 0), DST_GUARD);
}

/* MOV EAX,[0x000643c8] at 000177ab reloads the sheet base for the second
   blit instead of reusing the graphic the first one used, so the track is
   graphic 0 whatever bar_index was.  The two arts differ at column 10, which
   is what makes the assertion mean anything, so that is asserted too. */
static void track_comes_from_graphic_zero_whatever_the_index(void)
{
    stage_bar();
    fdps_draw_gauge_bar(bar_dst(), DST_PITCH, 2, 10);

    CHECK_EQ(art(0, 0, 10) != art(2, 0, 10), 1);
    CHECK_EQ(drawn(0, 9), art(2, 0, 9));
    CHECK_EQ(drawn(0, 10), art(0, 0, 10));
    CHECK_EQ(drawn(0, BAR_WIDTH - 1), art(0, 0, BAR_WIDTH - 1));
}

/* IMUL EAX,dword ptr [EBP+0x1c],0x3a8 at 0001774c: graphic 2 starts 0x750
   bytes into the sheet and its rows are 0x75 apart.  Both offsets are spelled
   out as literals here so a wrong stride cannot hide behind the helper. */
static void graphic_and_row_strides_are_0x3a8_and_0x75(void)
{
    stage_bar();
    fdps_draw_gauge_bar(bar_dst(), DST_PITCH, 2, BAR_WIDTH);

    CHECK_EQ(drawn(0, 0), (int) bar_sheet[0x750]);
    CHECK_EQ(drawn(3, 0), (int) bar_sheet[0x750 + 3 * 0x75]);
    CHECK_EQ(drawn(3, 6), (int) bar_sheet[0x750 + 3 * 0x75 + 6]);
}

/* CMP dword ptr [EBP+0x20],0x0 / JLE at 00017762 skips the filled blit at 0,
   and 0 < 0x75 so the whole track is drawn from graphic 0 column 0.  Graphic
   1 is left out of it entirely, which the differing first bytes make
   visible. */
static void zero_fill_draws_the_whole_track(void)
{
    stage_bar();
    fdps_draw_gauge_bar(bar_dst(), DST_PITCH, 1, 0);

    CHECK_EQ(art(0, 0, 0) != art(1, 0, 0), 1);
    CHECK_EQ(drawn(0, 0), art(0, 0, 0));
    CHECK_EQ(drawn(0, BAR_WIDTH - 1), art(0, 0, BAR_WIDTH - 1));
    CHECK_EQ(drawn(0, BAR_WIDTH), DST_GUARD);
}

/* CMP dword ptr [EBP+0x20],0x0 / JGE at 00017784 with MOV dword ptr
   [EBP+0x20],0x0 at 00017786: a negative fill is clamped, and the clamp
   happens after the filled blit has already been skipped, so -5 draws exactly
   what 0 draws.  Without the clamp the second blit would take its source from
   five bytes BEFORE the sheet, put it five columns before the bar's origin
   and run 0x7a columns wide, so the guard column to the left of the bar and
   the one past its right end are both checked. */
static void negative_fill_is_clamped_up_to_zero(void)
{
    stage_bar();
    fdps_draw_gauge_bar(bar_dst(), DST_PITCH, 1, -5);

    CHECK_EQ(drawn(0, 0), art(0, 0, 0));
    CHECK_EQ(drawn(0, BAR_WIDTH - 1), art(0, 0, BAR_WIDTH - 1));
    CHECK_EQ(drawn(0, -1), DST_GUARD);
    CHECK_EQ(drawn(0, BAR_WIDTH), DST_GUARD);
}

/* CMP dword ptr [EBP+0x20],0x75 / JGE at 0001778d: at exactly 0x75 the
   remainder is skipped, so the last column comes from the filled graphic and
   not from the track. */
static void full_fill_draws_no_track(void)
{
    stage_bar();
    fdps_draw_gauge_bar(bar_dst(), DST_PITCH, 1, BAR_WIDTH);

    CHECK_EQ(art(0, 0, BAR_WIDTH - 1) != art(1, 0, BAR_WIDTH - 1), 1);
    CHECK_EQ(drawn(0, 0), art(1, 0, 0));
    CHECK_EQ(drawn(0, BAR_WIDTH - 1), art(1, 0, BAR_WIDTH - 1));
    CHECK_EQ(drawn(0, BAR_WIDTH), DST_GUARD);
}

/* There is no upper clamp.  0x80 columns are blitted out of a source whose
   row pitch is 0x75, so the eleven columns past the bar's width read on into
   the art's NEXT ROW -- destination column 0x75 is graphic 1 row 1 column 0
   -- and the track is skipped even though eleven columns of it are still
   visible on screen.  Adding the symmetric clamp would make column 0x74 come
   from graphic 0 and columns 0x75 upwards not be written at all, so both are
   asserted. */
static void overfull_fill_is_not_capped(void)
{
    stage_bar();
    fdps_draw_gauge_bar(bar_dst(), DST_PITCH, 1, 0x80);

    CHECK_EQ(drawn(0, BAR_WIDTH - 1), art(1, 0, BAR_WIDTH - 1));
    CHECK_EQ(drawn(0, 0x75), art(1, 1, 0));
    CHECK_EQ(drawn(0, 0x7f), art(1, 1, 10));
    CHECK_EQ(drawn(0, 0x80), DST_GUARD);
    CHECK_EQ(drawn(1, 0x75), art(1, 2, 0));
}

/* Both halves go through fdps_blit_transparent_rect, so a source byte of 0 is
   the transparency key in the filled graphic and in the track alike and the
   destination pixel under it survives.  Column 3 is planted transparent in
   graphic 1 and column 20 in graphic 0, and the fill of 10 puts one on each
   side of the seam. */
static void palette_index_zero_is_transparent_in_both_halves(void)
{
    stage_bar();
    bar_sheet[ART_GRAPHIC_STRIDE + 3] = 0;
    bar_sheet[20] = 0;
    fdps_draw_gauge_bar(bar_dst(), DST_PITCH, 1, 10);

    CHECK_EQ(drawn(0, 3), DST_GUARD);
    CHECK_EQ(drawn(0, 20), DST_GUARD);
    CHECK_EQ(drawn(0, 2), art(1, 0, 2));
    CHECK_EQ(drawn(0, 21), art(0, 0, 21));
}

/* dst_stride is the destination's pitch and nothing else is derived from it:
   with a pitch of 0x90 the bar's row 1 starts 0x90 bytes after row 0, and the
   bytes between the end of one row and the start of the next keep their guard
   value.  A run that used the source's 0x75 pitch for the destination too
   would put row 1 twenty-seven columns to the left of where it belongs. */
static void destination_rows_step_by_dst_stride(void)
{
    stage_bar();
    fdps_draw_gauge_bar(bar_dst(), DST_PITCH, 1, BAR_WIDTH);

    CHECK_EQ((int) bar_canvas[(BAR_ORIGIN_ROW + 1) * DST_PITCH
                              + BAR_ORIGIN_COLUMN],
             art(1, 1, 0));
    CHECK_EQ(drawn(1, -1), DST_GUARD);
    CHECK_EQ(drawn(1, BAR_WIDTH), DST_GUARD);
    CHECK_EQ(drawn(BAR_ROWS, 0), DST_GUARD);
}

/* ------------------------------------------------------------------ *
 * fdps_draw_gauge_bar_proportional @ 000176f0
 * ------------------------------------------------------------------ */

/* Every width below is (current * 0x75 + max - 1) / max in signed 32-bit
   arithmetic, read off IMUL EDX,dword ptr [EBP+0x24],0x75 / ADD EDX,dword ptr
   [EBP+0x20] / DEC EDX / SAR EDX,0x1f / IDIV dword ptr [EBP+0x20] at
   0001770b..0001771b, with the zero path taken when CMP dword ptr
   [EBP+0x20],0x0 / JG at 000176fc falls through.  The width is not observable
   on its own -- the function returns nothing -- so each case reads it back off
   the canvas, where the seam between the filled graphic and the track sits at
   exactly that column. */

/* Where the seam has to be for a given width: the last filled column comes
   from the filled graphic and the next one from the track. */
static void seam_is_at(int bar_index, int fill_width)
{
    CHECK_EQ(drawn(0, fill_width - 1), art(bar_index, 0, fill_width - 1));
    CHECK_EQ(drawn(0, fill_width), art(0, 0, fill_width));
}

/* Nothing of the filled graphic reached the canvas: every column is the track
   at its own column. */
static void whole_bar_is_the_track(void)
{
    CHECK_EQ(art(0, 0, 0) != art(1, 0, 0), 1);
    CHECK_EQ(drawn(0, 0), art(0, 0, 0));
    CHECK_EQ(drawn(0, 1), art(0, 0, 1));
    CHECK_EQ(drawn(0, BAR_WIDTH - 1), art(0, 0, BAR_WIDTH - 1));
    CHECK_EQ(drawn(0, -1), DST_GUARD);
    CHECK_EQ(drawn(0, BAR_WIDTH), DST_GUARD);
}

/* CMP dword ptr [EBP+0x20],0x0 / JG at 000176fc..00017700 falls through to
   MOV dword ptr [EBP+-0x4],0x0, so a max of 0 never reaches the IDIV and the
   bar is drawn empty however large current is.  A current of 50 against it
   would be a division by zero if the guard were not there. */
static void zero_max_draws_an_empty_bar(void)
{
    stage_bar();
    fdps_draw_gauge_bar_proportional(bar_dst(), DST_PITCH, 1, 0, 50);
    whole_bar_is_the_track();
}

/* The same branch is JG and not JNE, so a negative max takes the empty path
   too rather than dividing by it.  Read as unsigned, -10 is above 0 and the
   divide would be reached instead. */
static void negative_max_draws_an_empty_bar(void)
{
    stage_bar();
    fdps_draw_gauge_bar_proportional(bar_dst(), DST_PITCH, 1, -10, 5);
    whole_bar_is_the_track();
}

/* DEC EDX after ADD EDX,max rounds the width up: 1 out of 1000 is
   (117 + 999) / 1000 = 1 filled column, where the truncating 117 / 1000 would
   be 0 and the bar would read as empty.  This is the one-hit-point sliver. */
static void one_unit_of_current_still_fills_one_column(void)
{
    stage_bar();
    fdps_draw_gauge_bar_proportional(bar_dst(), DST_PITCH, 1, 1000, 1);
    CHECK_EQ(art(0, 0, 0) != art(1, 0, 0), 1);
    seam_is_at(1, 1);
}

/* The rounding up stops at 0: (0 * 0x75 + 999) / 1000 is 0, so a current of 0
   against a positive max draws the empty bar and not a one-column sliver. */
static void zero_current_draws_an_empty_bar(void)
{
    stage_bar();
    fdps_draw_gauge_bar_proportional(bar_dst(), DST_PITCH, 1, 1000, 0);
    whole_bar_is_the_track();
}

/* Half of 234 is (117 * 0x75 + 233) / 234 = 13922 / 234 = 59, the ceiling of
   58.5 and not the 58 a truncating divide gives.  Row 7 is checked as well,
   which is what shows dst_stride reached the blits as the destination pitch:
   the fourth argument of the call is the width and the second is the stride,
   and swapping them would put row 7 nowhere near here. */
static void half_full_rounds_the_odd_column_up(void)
{
    stage_bar();
    fdps_draw_gauge_bar_proportional(bar_dst(), DST_PITCH, 1, 234, 117);
    seam_is_at(1, 59);
    CHECK_EQ(drawn(7, 58), art(1, 7, 58));
    CHECK_EQ(drawn(7, 59), art(0, 7, 59));
    CHECK_EQ(drawn(BAR_ROWS, 0), DST_GUARD);
}

/* current == max gives (50 * 0x75 + 49) / 50 = 5899 / 50 = 117 exactly, which
   is the whole bar, and 0x75 is the value at which fdps_draw_gauge_bar skips
   the track altogether -- so a full gauge has no track column at all. */
static void current_equal_to_max_fills_the_whole_bar(void)
{
    stage_bar();
    fdps_draw_gauge_bar_proportional(bar_dst(), DST_PITCH, 1, 50, 50);
    CHECK_EQ(art(0, 0, BAR_WIDTH - 1) != art(1, 0, BAR_WIDTH - 1), 1);
    CHECK_EQ(drawn(0, 0), art(1, 0, 0));
    CHECK_EQ(drawn(0, BAR_WIDTH - 1), art(1, 0, BAR_WIDTH - 1));
    CHECK_EQ(drawn(0, BAR_WIDTH), DST_GUARD);
}

/* bar_index is passed through untouched -- MOV EAX,dword ptr [EBP+0x1c] /
   PUSH EAX at 00017722 is the third argument of the call -- so the filled half
   of a 1-of-2 bar comes out of graphic 2 while the track still comes out of
   graphic 0.  The width is (117 + 1) / 2 = 59. */
static void bar_index_reaches_the_filled_half(void)
{
    stage_bar();
    fdps_draw_gauge_bar_proportional(bar_dst(), DST_PITCH, 2, 2, 1);
    CHECK_EQ(art(0, 0, 0) != art(2, 0, 0), 1);
    CHECK_EQ(drawn(0, 0), art(2, 0, 0));
    seam_is_at(2, 59);
}

/* SAR EDX,0x1f before the IDIV makes the division signed: -10 out of 100 is
   (-1170 + 99) / 100 = -1071 / 100 = -10, truncated toward zero, and
   fdps_draw_gauge_bar's own clamp then draws the empty bar.  Unsigned, the
   numerator would be a value near 2^32 and the width would be enormous. */
static void negative_current_draws_an_empty_bar(void)
{
    stage_bar();
    fdps_draw_gauge_bar_proportional(bar_dst(), DST_PITCH, 1, 100, -10);
    whole_bar_is_the_track();
}

/* Nothing here caps the width at the bar's own 0x75: 110 out of 100 is
   (12870 + 99) / 100 = 129 columns, so the filled blit runs 12 columns past
   the end of the bar and, its source pitch being 0x75, wraps into the next row
   of the art.  Clamping current to max would draw a clean full bar instead,
   which is not what the original does. */
static void current_above_max_is_not_capped(void)
{
    stage_bar();
    fdps_draw_gauge_bar_proportional(bar_dst(), DST_PITCH, 1, 100, 110);
    CHECK_EQ(drawn(0, BAR_WIDTH - 1), art(1, 0, BAR_WIDTH - 1));
    CHECK_EQ(drawn(0, 0x75), art(1, 1, 0));
    CHECK_EQ(drawn(0, 128), art(1, 1, 11));
    CHECK_EQ(drawn(0, 129), DST_GUARD);
}

/* The combat gauge fill sheet's geometry, again spelled out from the assembly
   at 00019250 rather than taken from a header: IMUL EAX,dword ptr
   [EBP+0x1c],0x271 at 0001925c for the strip stride, PUSH 0x7d at 000192ab
   for the source row pitch and the span width, PUSH 0x5 at 0001929d for the
   row count, and 4 * 0x271 = 0x9c4 for the whole sheet -- which is the size
   the builders at 00018e15 and 0001a603 malloc. */
#define FILL_STRIP_STRIDE 0x271
#define FILL_ROW_PITCH 0x7d
#define FILL_SPAN 0x7d
#define FILL_ROWS 5
#define FILL_SHEET_BYTES 0x9c4

/* A destination with a row above and below the five the fill occupies, and
   columns either side of the 125 the span occupies, so a blit that ran past
   any edge lands on a guard byte instead of off the array. */
#define FILL_DST_PITCH 0x90
#define FILL_DST_ROWS 9
#define FILL_ORIGIN_ROW 2
#define FILL_ORIGIN_COLUMN 8
#define FILL_GUARD 0xd7

static unsigned char fill_sheet[FILL_SHEET_BYTES];
static unsigned char fill_canvas[FILL_DST_ROWS * FILL_DST_PITCH];

/* Distinct neighbours and distinct values a row pitch or a strip stride
   apart, so an assertion can tell strip 1 column 115 from strip 1 column 0
   and from strip 0 column 115.  1..251, so nothing is the transparency key by
   accident; the transparency case plants its own zeroes. */
static void stage_fill(void)
{
    int offset;

    for (offset = 0; offset < FILL_SHEET_BYTES; offset++) {
        fill_sheet[offset] = (unsigned char) (offset % 251 + 1);
    }
    for (offset = 0; offset < FILL_DST_ROWS * FILL_DST_PITCH; offset++) {
        fill_canvas[offset] = FILL_GUARD;
    }
    data_fdps_gauge_fill_sheet_ptr = fill_sheet;
}

/* The left end of the gauge's 125-pixel span, which is what the caller hands
   over whichever way the gauge fills. */
static unsigned char *fill_dest(void)
{
    return fill_canvas + FILL_ORIGIN_ROW * FILL_DST_PITCH
           + FILL_ORIGIN_COLUMN;
}

/* One destination pixel, in the span's own coordinates. */
static int fill_drawn(int row, int column)
{
    return (int) fill_canvas[(FILL_ORIGIN_ROW + row) * FILL_DST_PITCH
                             + FILL_ORIGIN_COLUMN + column];
}

/* One source pixel, addressed the way the assembly addresses it: strip base
   at a 0x271 stride, rows at a 0x7d pitch within it. */
static int fill_art(int strip, int row, int column)
{
    return (int) fill_sheet[strip * FILL_STRIP_STRIDE + row * FILL_ROW_PITCH
                            + column];
}

/* CMP dword ptr [EBP+0x1c],0x2 / JGE at 0001927f: strip 2 skips the shift, so
   the run starts at the left end of the span and takes the strip's own
   leftmost columns.  Five rows, ten columns, and nothing outside them. */
static void index_two_fills_from_the_left(void)
{
    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 2, 10);

    CHECK_EQ(fill_drawn(0, 0), fill_art(2, 0, 0));
    CHECK_EQ(fill_drawn(0, 9), fill_art(2, 0, 9));
    CHECK_EQ(fill_drawn(4, 0), fill_art(2, 4, 0));
    CHECK_EQ(fill_drawn(4, 9), fill_art(2, 4, 9));

    CHECK_EQ(fill_drawn(0, 10), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, -1), FILL_GUARD);
    CHECK_EQ(fill_drawn(-1, 0), FILL_GUARD);
    CHECK_EQ(fill_drawn(FILL_ROWS, 0), FILL_GUARD);
}

/* MOV EAX,0x7d / SUB EAX,[EBP+0x20] / ADD [EBP-0x4],EAX at 00019281-00019289
   and the same again into [EBP+0x14] at 0001928c-00019294: for strip 1 the
   run sits at the RIGHT end of the span AND comes out of the right end of the
   strip.  The two arts differing at column 115 is what makes the second half
   of that mean anything, so it is asserted rather than assumed. */
static void index_below_two_shifts_dest_and_strip_together(void)
{
    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 1, 10);

    CHECK_EQ(fill_art(1, 0, 115) != fill_art(1, 0, 0), 1);
    CHECK_EQ(fill_drawn(0, 115), fill_art(1, 0, 115));
    CHECK_EQ(fill_drawn(0, 124), fill_art(1, 0, 124));
    CHECK_EQ(fill_drawn(4, 115), fill_art(1, 4, 115));

    CHECK_EQ(fill_drawn(0, 114), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, 125), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, 0), FILL_GUARD);
}

/* The alignment split is at exactly 2, not at 1 or 3: strip 1 lands at the
   right end and strip 2, the very next value, lands at the left. */
static void the_alignment_split_is_at_index_two(void)
{
    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 1, 20);
    CHECK_EQ(fill_drawn(0, 105), fill_art(1, 0, 105));
    CHECK_EQ(fill_drawn(0, 0), FILL_GUARD);

    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 2, 20);
    CHECK_EQ(fill_drawn(0, 0), fill_art(2, 0, 0));
    CHECK_EQ(fill_drawn(0, 105), FILL_GUARD);
}

/* Strip 0 is right-aligned like strip 1 and is a different strip, which the
   two arts differing at the same column shows. */
static void index_zero_uses_strip_zero_right_aligned(void)
{
    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 0, 10);

    CHECK_EQ(fill_art(0, 0, 115) != fill_art(1, 0, 115), 1);
    CHECK_EQ(fill_drawn(0, 115), fill_art(0, 0, 115));
    CHECK_EQ(fill_drawn(0, 114), FILL_GUARD);
}

/* Strip 3 starts 0x753 bytes in and its rows are 0x7d apart, and the last
   byte it reads is sheet byte 0x9c3 -- the last byte of the 0x9c4 the
   builders allocate, which is what pins the four-strips-in-one-buffer
   geometry.  The offsets are literals here so a wrong stride cannot hide
   behind the helper. */
static void strip_and_row_strides_are_0x271_and_0x7d(void)
{
    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 3, FILL_SPAN);

    CHECK_EQ(fill_drawn(0, 0), (int) fill_sheet[0x753]);
    CHECK_EQ(fill_drawn(2, 0), (int) fill_sheet[0x753 + 2 * 0x7d]);
    CHECK_EQ(fill_drawn(2, 6), (int) fill_sheet[0x753 + 2 * 0x7d + 6]);
    CHECK_EQ(fill_drawn(4, 0x7c), (int) fill_sheet[0x9c3]);
}

/* CMP dword ptr [EBP+0x20],0x7d / JG at 00019297: 0x7d is drawn and 0x7e is
   not, so the boundary is tested from both sides.  A full span leaves nothing
   for the guard column at 125. */
static void a_full_span_is_still_drawn(void)
{
    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 2, FILL_SPAN);

    CHECK_EQ(fill_drawn(0, 0), fill_art(2, 0, 0));
    CHECK_EQ(fill_drawn(0, FILL_SPAN - 1), fill_art(2, 0, FILL_SPAN - 1));
    CHECK_EQ(fill_drawn(4, FILL_SPAN - 1), fill_art(2, 4, FILL_SPAN - 1));
    CHECK_EQ(fill_drawn(0, FILL_SPAN), FILL_GUARD);
}

/* THE OVERFULL CASE DRAWS NOTHING, which is the whole point of the JG: one
   pixel past a full span and the frame is left empty rather than reading
   full.  Both alignments are checked, because the right-aligned one would
   shift the destination to a NEGATIVE offset if it drew at all. */
static void an_overfull_span_draws_nothing(void)
{
    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 2, FILL_SPAN + 1);
    CHECK_EQ(fill_drawn(0, 0), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, FILL_SPAN - 1), FILL_GUARD);
    CHECK_EQ(fill_drawn(4, 0), FILL_GUARD);

    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 1, FILL_SPAN + 1);
    CHECK_EQ(fill_drawn(0, -1), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, 0), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, FILL_SPAN - 1), FILL_GUARD);
}

/* A full span shifts by 0x7d - 0x7d = 0, so a right-aligned gauge at full
   draws over exactly the same 125 columns a left-aligned one would, and does
   not run off the left of the span. */
static void a_full_right_aligned_span_does_not_shift(void)
{
    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 0, FILL_SPAN);

    CHECK_EQ(fill_drawn(0, 0), fill_art(0, 0, 0));
    CHECK_EQ(fill_drawn(0, FILL_SPAN - 1), fill_art(0, 0, FILL_SPAN - 1));
    CHECK_EQ(fill_drawn(0, -1), FILL_GUARD);
}

/* CMP dword ptr [EBP+0x20],0x0 / JGE at 0001926e clamps a negative width up
   to 0, and a width of 0 hands fdps_blit_transparent_rect a rectangle whose
   signed column test fails immediately, so nothing is transferred.  0 and a
   negative therefore look identical on screen, and both alignments are
   checked because the right-aligned one still moves its pointers. */
static void zero_and_negative_widths_draw_nothing(void)
{
    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 2, 0);
    CHECK_EQ(fill_drawn(0, 0), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, FILL_SPAN - 1), FILL_GUARD);

    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 2, -5);
    CHECK_EQ(fill_drawn(0, 0), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, FILL_SPAN - 1), FILL_GUARD);

    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 1, -5);
    CHECK_EQ(fill_drawn(0, FILL_SPAN - 1), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, 0), FILL_GUARD);
    CHECK_EQ(fill_drawn(4, FILL_SPAN - 1), FILL_GUARD);
}

/* The blit is the transparent one, so a source byte of 0 leaves the frame
   pixel underneath alone -- that is what lets the bar's shaped ends show the
   frame through them.  A zero is planted in two different rows so a
   single-row special case could not pass. */
static void palette_index_zero_leaves_the_frame_alone(void)
{
    stage_fill();
    fill_sheet[2 * FILL_STRIP_STRIDE + 3] = 0;
    fill_sheet[2 * FILL_STRIP_STRIDE + FILL_ROW_PITCH] = 0;
    fdps_draw_gauge_fill(fill_dest(), FILL_DST_PITCH, 2, 10);

    CHECK_EQ(fill_drawn(0, 3), FILL_GUARD);
    CHECK_EQ(fill_drawn(1, 0), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, 4), fill_art(2, 0, 4));
    CHECK_EQ(fill_drawn(1, 1), fill_art(2, 1, 1));
}

/* dest_stride is passed through to the blit untouched and is what steps the
   destination between rows, so a stride that is not the canvas pitch puts row
   1 exactly 0x80 bytes past row 0 rather than a canvas row later. */
static void dest_stride_is_passed_through_untouched(void)
{
    int base;

    stage_fill();
    fdps_draw_gauge_fill(fill_dest(), 0x80, 2, 4);

    base = FILL_ORIGIN_ROW * FILL_DST_PITCH + FILL_ORIGIN_COLUMN;
    CHECK_EQ((int) fill_canvas[base], fill_art(2, 0, 0));
    CHECK_EQ((int) fill_canvas[base + 0x80], fill_art(2, 1, 0));
    CHECK_EQ((int) fill_canvas[base + 4 * 0x80 + 3], fill_art(2, 4, 3));
    CHECK_EQ((int) fill_canvas[base + FILL_DST_PITCH], FILL_GUARD);
}

/* Every width below is (current * 0x7d + max - 1) / max in signed 32-bit
   arithmetic, with the zero path taken when CMP dword ptr [EBP+0x20],0x0 / JG
   at 000192cc falls through.  Index 2 or 3 is used wherever a case only cares
   about the width, because those fill from the left and the run's far edge is
   then the width itself; the right-aligned cases say so in their comment. */

/* Nothing of the fill strip reached the canvas anywhere along the span. */
static void span_is_empty(void)
{
    CHECK_EQ(fill_drawn(0, 0), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, FILL_SPAN / 2), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, FILL_SPAN - 1), FILL_GUARD);
    CHECK_EQ(fill_drawn(4, 0), FILL_GUARD);
    CHECK_EQ(fill_drawn(4, FILL_SPAN - 1), FILL_GUARD);
}

/* CMP dword ptr [EBP+0x20],0x0 / JG at 000192cc..000192d0 falls through to
   MOV dword ptr [EBP+-0x4],0x0, so a max of 0 never reaches the IDIV and the
   gauge is drawn empty however large current is.  A current of 50 against it
   would be a division by zero if the guard were not there. */
static void stat_gauge_zero_max_draws_nothing(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 2, 0, 50);
    span_is_empty();
}

/* The same branch is JG and not JNE, so a negative max takes the empty path
   too rather than dividing by it and producing a negative width. */
static void stat_gauge_negative_max_draws_nothing(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 2, -3, 50);
    span_is_empty();
}

/* ADD EDX,dword ptr [EBP+0x20] / DEC EDX at 000192df..000192e2 is what makes
   the divide a ceiling: 1 out of 200 is (125 + 199) / 200 = 1 column, where
   the truncating 125 / 200 would be 0 and the gauge would read empty for a
   unit that is still alive. */
static void stat_gauge_one_current_still_lights_one_column(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 2, 200, 1);
    CHECK_EQ(fill_drawn(0, 0), fill_art(2, 0, 0));
    CHECK_EQ(fill_drawn(4, 0), fill_art(2, 4, 0));
    CHECK_EQ(fill_drawn(0, 1), FILL_GUARD);
}

/* The ceiling does not add a column that is not owed: 0 out of 200 is
   (0 + 199) / 200 = 0, so a dead unit's gauge is blank. */
static void stat_gauge_zero_current_draws_nothing(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 2, 200, 0);
    span_is_empty();
}

/* Half of 125 is 62.5 and the odd column goes to the fill: 2 out of 4 is
   (250 + 3) / 4 = 63 columns, not the 62 a truncating divide would give. */
static void stat_gauge_half_full_rounds_the_odd_column_up(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 2, 4, 2);
    CHECK_EQ(fill_drawn(0, 62), fill_art(2, 0, 62));
    CHECK_EQ(fill_drawn(0, 63), FILL_GUARD);
}

/* A ratio that divides badly rounds up as well: 1 out of 3 is (125 + 2) / 3 =
   42 columns where 125 / 3 would be 41. */
static void stat_gauge_a_third_rounds_up_to_42(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 2, 3, 1);
    CHECK_EQ(fill_drawn(0, 41), fill_art(2, 0, 41));
    CHECK_EQ(fill_drawn(0, 42), FILL_GUARD);
}

/* current == max comes out at exactly the span and not one past it, which is
   what keeps a full gauge on the drawn side of fdps_draw_gauge_fill's
   CMP ...,0x7d / JG: 37 out of 37 is (4625 + 36) / 37 = 125, since 37 * 126 =
   4662 is already past 4661. */
static void stat_gauge_current_equal_to_max_fills_the_span(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 2, 37, 37);
    CHECK_EQ(fill_drawn(0, 0), fill_art(2, 0, 0));
    CHECK_EQ(fill_drawn(0, FILL_SPAN - 1), fill_art(2, 0, FILL_SPAN - 1));
    CHECK_EQ(fill_drawn(4, FILL_SPAN - 1), fill_art(2, 4, FILL_SPAN - 1));
    CHECK_EQ(fill_drawn(0, FILL_SPAN), FILL_GUARD);
}

/* Nothing here caps the width at the span's own 0x7d, and the consequence is
   not a smear but a blank gauge: 110 out of 100 is (13750 + 99) / 100 = 138,
   which fdps_draw_gauge_fill's CMP ...,0x7d / JG at 00019297 drops outright.
   Adding min(125, width) here would draw a clean full gauge instead, which is
   not what the original puts on screen. */
static void stat_gauge_current_above_max_draws_nothing(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 2, 100, 110);
    span_is_empty();
}

/* The IDIV is signed and the numerator is sign extended by SAR EDX,0x1f at
   000192e5, so -10 out of 100 is (-1250 + 99) / 100 = -11 truncated toward
   zero, and fdps_draw_gauge_fill's own clamp turns that into an empty gauge
   rather than a run of 11 columns. */
static void stat_gauge_negative_current_draws_nothing(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 2, 100, -10);
    span_is_empty();
}

/* PUSH dword ptr [EBP+0x1c] at 000192f2 hands gauge_index through untouched,
   so it still picks the strip: 1 out of 4 is (125 + 3) / 4 = 32 columns, and
   strip 3 supplies them from its own leftmost column.  The two strips
   differing at column 0 is what makes that mean anything, so it is asserted
   rather than assumed. */
static void stat_gauge_index_reaches_the_strip(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 3, 4, 1);
    CHECK_EQ(fill_art(3, 0, 0) != fill_art(2, 0, 0), 1);
    CHECK_EQ(fill_drawn(0, 0), fill_art(3, 0, 0));
    CHECK_EQ(fill_drawn(0, 31), fill_art(3, 0, 31));
    CHECK_EQ(fill_drawn(0, 32), FILL_GUARD);
}

/* An index below 2 is passed through just as unchanged, so the same 32-column
   width lands at the RIGHT end of the span and comes out of the right end of
   the strip -- 125 - 32 = 93 onwards -- and the left end of the span is left
   showing the frame. */
static void stat_gauge_index_below_two_fills_from_the_right(void)
{
    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), FILL_DST_PITCH, 1, 4, 1);
    CHECK_EQ(fill_drawn(0, 93), fill_art(1, 0, 93));
    CHECK_EQ(fill_drawn(0, FILL_SPAN - 1), fill_art(1, 0, FILL_SPAN - 1));
    CHECK_EQ(fill_drawn(4, 93), fill_art(1, 4, 93));
    CHECK_EQ(fill_drawn(0, 92), FILL_GUARD);
    CHECK_EQ(fill_drawn(0, 0), FILL_GUARD);
}

/* PUSH dword ptr [EBP+0x18] at 000192f6 hands dest_stride through untouched
   as well, so a stride that is not the canvas pitch puts row 1 exactly 0x80
   bytes past row 0.  4 out of 125 is (500 + 124) / 125 = 4 columns. */
static void stat_gauge_dest_and_stride_pass_through(void)
{
    int base;

    stage_fill();
    fdps_draw_stat_gauge(fill_dest(), 0x80, 2, 125, 4);

    base = FILL_ORIGIN_ROW * FILL_DST_PITCH + FILL_ORIGIN_COLUMN;
    CHECK_EQ((int) fill_canvas[base], fill_art(2, 0, 0));
    CHECK_EQ((int) fill_canvas[base + 0x80], fill_art(2, 1, 0));
    CHECK_EQ((int) fill_canvas[base + 4 * 0x80 + 3], fill_art(2, 4, 3));
    CHECK_EQ((int) fill_canvas[base + 4 * 0x80 + 4], FILL_GUARD);
}

/* ------------------------------------------------------------------ *
 * fdps_draw_unit_gauge @ 0001cb00
 * ------------------------------------------------------------------ */

/* The unit gauge sheet's geometry, again written out from the assembly rather
   than taken from a header: IMUL EAX,dword ptr [EBP+0x1c],0x102 at 0001cb0c
   for the graphic stride, PUSH 0x2b for the source row pitch and PUSH 0x6 for
   the row count at every blit, PUSH 0x2 for each cap and 0x29 for the interior
   and for the right cap's column -- and 3 * 0x102 = 0x306, which is the size
   the loader mallocs at 00029cd5. */
#define UNIT_ART_GRAPHIC_STRIDE 0x102
#define UNIT_ART_ROW_PITCH 0x2b
#define UNIT_BAR_WIDTH 0x2b
#define UNIT_BAR_ROWS 6
#define UNIT_CAP_WIDTH 2
#define UNIT_INTERIOR 0x29
#define UNIT_SHEET_BYTES 0x306

/* Room for a bar that runs well past its own right edge -- the overfull case
   blits 0x30 columns starting at column 2 -- with rows above and below and
   columns either side, so an overrun lands on a guard byte and not off the
   array.  0xff is a value the staged art cannot hold: the sheet is filled
   1..251. */
#define UNIT_DST_PITCH 0x60
#define UNIT_DST_ROWS 10
#define UNIT_ORIGIN_ROW 2
#define UNIT_ORIGIN_COLUMN 8
#define UNIT_GUARD 0xff

/* The ramp's 18 rows of 256 entries and the cube's 16x16x16. */
#define RAMP_ROW_ENTRIES 256
#define RAMP_WEIGHT_ROWS 9
#define CUBE_ENTRIES 4096

static unsigned char unit_sheet[UNIT_SHEET_BYTES];
static unsigned char unit_canvas[UNIT_DST_ROWS * UNIT_DST_PITCH];

/* Distinct neighbours and distinct values a row pitch or a graphic stride
   apart, so an assertion can tell graphic 2 column 12 from graphic 0 column 12
   and from graphic 2 row 1 column 0.  1..251, so nothing is the transparency
   key by accident; the cases that want a transparent pixel plant one. */
static void stage_unit(void)
{
    int offset;

    for (offset = 0; offset < UNIT_SHEET_BYTES; offset++) {
        unit_sheet[offset] = (unsigned char) (offset % 251 + 1);
    }
    for (offset = 0; offset < UNIT_DST_ROWS * UNIT_DST_PITCH; offset++) {
        unit_canvas[offset] = UNIT_GUARD;
    }
    data_fdps_unit_gauge_sheet_ptr = unit_sheet;
}

/* The two blending tables the blended painters composite through, staged so
   that what they write is a readable function of the two pixels that went into
   it.  The real tables would make every expected value below the outcome of a
   colour match, and ticket 23 owns what they hold anyway.

   Row r of the ramp, for r up to 8, holds ((pixel + r) & 0xf) << 4, which after
   the painters' shift by four and mask with 0x0f0f0f is the BLUE nibble; row
   9 + r holds ((pixel + r) & 0xf) << 12, which is the GREEN nibble.  The fold
   (v & 0xffff) | (v >> 12) leaves green above red above blue, so with red
   always 0 the cube index is just those two nibbles, and the cube entry at that
   index is the same two nibbles side by side in one byte. */
static void stage_tables(void)
{
    int row;
    int pixel;
    int index;

    for (row = 0; row < RAMP_WEIGHT_ROWS; row++) {
        for (pixel = 0; pixel < RAMP_ROW_ENTRIES; pixel++) {
            data_fdps_palette_shade_ramp_table[row * RAMP_ROW_ENTRIES + pixel] =
                (unsigned int) (((pixel + row) & 0x0f) << 4);
            data_fdps_palette_shade_ramp_table[(row + RAMP_WEIGHT_ROWS)
                                               * RAMP_ROW_ENTRIES + pixel] =
                (unsigned int) (((pixel + row) & 0x0f) << 12);
        }
    }
    for (index = 0; index < CUBE_ENTRIES; index++) {
        data_fdps_inverse_palette_cube[index] =
            (unsigned char) ((((index >> 8) & 0x0f) << 4) | (index & 0x0f));
    }
}

/* What the staged tables make a blended painter write.  low_pixel is whichever
   pixel the painter reads at ramp row alpha -- the foreground for
   fdps_blit_blend_transparent_rect, the tint colour for
   fdps_blit_tint_transparent_rect -- and high_pixel is the one it reads nine
   rows on: the background for the first, the source pixel for the second.
   Only alpha 0..8 is spelled this way; above 8 the painters fold and swap. */
static int table_blend(int low_pixel, int high_pixel, int alpha)
{
    return (((high_pixel + alpha) & 0x0f) << 4) | ((low_pixel + alpha) & 0x0f);
}

/* Where the caller says the bar's top-left pixel is. */
static unsigned char *unit_dst(void)
{
    return unit_canvas + UNIT_ORIGIN_ROW * UNIT_DST_PITCH + UNIT_ORIGIN_COLUMN;
}

/* One destination pixel, in the bar's own coordinates. */
static int unit_drawn(int row, int column)
{
    return (int) unit_canvas[(UNIT_ORIGIN_ROW + row) * UNIT_DST_PITCH
                             + UNIT_ORIGIN_COLUMN + column];
}

/* One source pixel, addressed the way the assembly addresses it. */
static int unit_art(int graphic, int row, int column)
{
    return (int) unit_sheet[graphic * UNIT_ART_GRAPHIC_STRIDE
                            + row * UNIT_ART_ROW_PITCH + column];
}

/* The four segments and where each one's pixels come from: the 2-wide cap out
   of graphic 2, ten filled columns out of the same graphic, the rest of the
   interior out of GRAPHIC 0 at its own columns, and the right cap out of
   graphic 2 again.  Row 5 is checked alongside row 0 because every blit is
   handed 6 as its row count and steps the destination by dst_stride. */
static void unit_gauge_paints_cap_fill_track_cap(void)
{
    stage_unit();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, 10, 0, 0);

    CHECK_EQ(unit_drawn(0, 0), unit_art(2, 0, 0));
    CHECK_EQ(unit_drawn(0, 1), unit_art(2, 0, 1));
    CHECK_EQ(unit_drawn(5, 1), unit_art(2, 5, 1));

    CHECK_EQ(unit_drawn(0, 2), unit_art(2, 0, 2));
    CHECK_EQ(unit_drawn(0, 11), unit_art(2, 0, 11));
    CHECK_EQ(unit_drawn(5, 11), unit_art(2, 5, 11));

    CHECK_EQ(unit_art(0, 0, 12) != unit_art(2, 0, 12), 1);
    CHECK_EQ(unit_drawn(0, 12), unit_art(0, 0, 12));
    CHECK_EQ(unit_drawn(0, 0x28), unit_art(0, 0, 0x28));
    CHECK_EQ(unit_drawn(5, 12), unit_art(0, 5, 12));

    CHECK_EQ(unit_art(0, 0, UNIT_INTERIOR) != unit_art(2, 0, UNIT_INTERIOR), 1);
    CHECK_EQ(unit_drawn(0, UNIT_INTERIOR), unit_art(2, 0, UNIT_INTERIOR));
    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH - 1),
             unit_art(2, 0, UNIT_BAR_WIDTH - 1));
    CHECK_EQ(unit_drawn(5, UNIT_BAR_WIDTH - 1),
             unit_art(2, 5, UNIT_BAR_WIDTH - 1));

    CHECK_EQ(unit_drawn(0, -1), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH), UNIT_GUARD);
    CHECK_EQ(unit_drawn(-1, 0), UNIT_GUARD);
    CHECK_EQ(unit_drawn(UNIT_BAR_ROWS, 0), UNIT_GUARD);
}

/* IMUL EAX,dword ptr [EBP+0x1c],0x102 at 0001cb0c: graphic 2 starts 0x204
   bytes into the sheet and its rows are 0x2b apart, so the bar's bottom-right
   pixel is sheet byte 0x305 -- the last byte of the 0x306 the loader
   allocates.  The offsets are literals here so a wrong stride cannot hide
   behind the helper. */
static void unit_graphic_and_row_strides_are_0x102_and_0x2b(void)
{
    stage_unit();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, UNIT_INTERIOR, 0, 0);

    CHECK_EQ(unit_drawn(0, 0), (int) unit_sheet[0x204]);
    CHECK_EQ(unit_drawn(3, 0), (int) unit_sheet[0x204 + 3 * 0x2b]);
    CHECK_EQ(unit_drawn(3, 6), (int) unit_sheet[0x204 + 3 * 0x2b + 6]);
    CHECK_EQ(unit_drawn(5, UNIT_BAR_WIDTH - 1), (int) unit_sheet[0x305]);
}

/* THE REMAINDER RUNS UNDER THE RIGHT CAP.  Its width is 0x29 - fill_width from
   column 2, so it reaches column 0x2a, and the cap is then painted back over
   those two columns -- which only shows where the cap's own art is
   transparent.  With the cap's two pixels planted at 0 and a fill of 0 the
   remainder's graphic-0 pixels survive underneath; with a full fill there is no
   remainder left to run under it and the same transparent cap leaves the
   surface alone.  Trimming the remainder to 0x27 - fill_width would make the
   first half read like the second. */
static void unit_remainder_runs_under_the_right_cap(void)
{
    stage_unit();
    unit_sheet[UNIT_ART_GRAPHIC_STRIDE + UNIT_INTERIOR] = 0;
    unit_sheet[UNIT_ART_GRAPHIC_STRIDE + UNIT_BAR_WIDTH - 1] = 0;
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 1, 0, 0, 0);

    CHECK_EQ(unit_drawn(0, UNIT_INTERIOR), unit_art(0, 0, UNIT_INTERIOR));
    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH - 1),
             unit_art(0, 0, UNIT_BAR_WIDTH - 1));

    stage_unit();
    unit_sheet[UNIT_ART_GRAPHIC_STRIDE + UNIT_INTERIOR] = 0;
    unit_sheet[UNIT_ART_GRAPHIC_STRIDE + UNIT_BAR_WIDTH - 1] = 0;
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 1, UNIT_INTERIOR, 0, 0);

    CHECK_EQ(unit_drawn(0, UNIT_INTERIOR), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH - 1), UNIT_GUARD);
}

/* CMP dword ptr [EBP+0x20],0x0 / JLE at 0001cb4f skips the fill run at 0, and
   CMP ...,0x0 / JGE at 0001cb1e with MOV dword ptr [EBP+0x20],0x0 at 0001cb24
   clamps a negative one up to it BEFORE the painter is even chosen, so the two
   draw exactly the same bar.  Without the clamp the remainder would start
   three columns to the left of the bar, take its source from three bytes
   before the sheet and run 0x2e columns wide, so the guards on both sides are
   checked. */
static void unit_zero_and_negative_fill_draw_the_same_bar(void)
{
    stage_unit();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 1, 0, 0, 0);

    CHECK_EQ(unit_art(0, 0, 2) != unit_art(1, 0, 2), 1);
    CHECK_EQ(unit_drawn(0, 0), unit_art(1, 0, 0));
    CHECK_EQ(unit_drawn(0, 2), unit_art(0, 0, 2));
    CHECK_EQ(unit_drawn(0, 0x28), unit_art(0, 0, 0x28));
    CHECK_EQ(unit_drawn(0, -1), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH), UNIT_GUARD);

    stage_unit();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 1, -5, 0, 0);

    CHECK_EQ(unit_drawn(0, 0), unit_art(1, 0, 0));
    CHECK_EQ(unit_drawn(0, 2), unit_art(0, 0, 2));
    CHECK_EQ(unit_drawn(0, 0x28), unit_art(0, 0, 0x28));
    CHECK_EQ(unit_drawn(0, -1), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, -3), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH), UNIT_GUARD);
}

/* There is no upper clamp.  0x30 columns are blitted out of a source whose row
   pitch is 0x2b, so the run passes the right cap and reads on into the art's
   NEXT ROW -- destination column 0x2b is graphic 1 row 1 column 0 -- and the
   remainder, 0x29 - 0x30 wide, draws nothing.  Adding the symmetric clamp
   would stop the bar at column 0x2a and put the track back on screen. */
static void unit_overfull_fill_is_not_capped(void)
{
    stage_unit();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 1, 0x30, 0, 0);

    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH - 1),
             unit_art(1, 0, UNIT_BAR_WIDTH - 1));
    CHECK_EQ(unit_drawn(0, 0x2b), unit_art(1, 1, 0));
    CHECK_EQ(unit_drawn(0, 0x31), unit_art(1, 1, 6));
    CHECK_EQ(unit_drawn(0, 0x32), UNIT_GUARD);
    CHECK_EQ(unit_drawn(1, 0x2b), unit_art(1, 2, 0));
}

/* Mode 1 is fdps_blit_blend_transparent_rect with the DESTINATION handed over
   as its own background -- PUSH [EBP+0x18] / PUSH [EBP+0x14] twice at
   0001cbe9..0001cbf8 -- so every pixel is blended against what was already
   there.  That is what makes the overlap visible in this mode: the right cap
   blends against the remainder's own output rather than against the surface
   underneath, so those two columns come out different from a cap painted onto
   an untouched destination. */
static void unit_blend_mode_reads_the_destination_as_background(void)
{
    stage_unit();
    stage_tables();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, 10, 1, 0);

    CHECK_EQ(unit_drawn(0, 0), table_blend(unit_art(2, 0, 0), UNIT_GUARD, 0));
    CHECK_EQ(unit_drawn(0, 2), table_blend(unit_art(2, 0, 2), UNIT_GUARD, 0));
    CHECK_EQ(unit_drawn(0, 12), table_blend(unit_art(0, 0, 12), UNIT_GUARD, 0));
    CHECK_EQ(unit_drawn(5, 12), table_blend(unit_art(0, 5, 12), UNIT_GUARD, 0));

    CHECK_EQ(unit_drawn(0, UNIT_INTERIOR),
             table_blend(unit_art(2, 0, UNIT_INTERIOR),
                         table_blend(unit_art(0, 0, UNIT_INTERIOR),
                                     UNIT_GUARD, 0),
                         0));
    CHECK_EQ(unit_drawn(0, UNIT_INTERIOR)
             != table_blend(unit_art(2, 0, UNIT_INTERIOR), UNIT_GUARD, 0), 1);

    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH), UNIT_GUARD);
}

/* Any mode other than 0 and 1 falls through to
   fdps_blit_tint_transparent_rect with MOV EAX,dword ptr [EBP+0x24] / PUSH EAX
   at 0001ccdb: blit_mode ITSELF is the tint colour index, so 5 and 6 paint the
   same bar in two different colours.  The painter never reads the destination,
   so the two overlap columns are simply written twice and the cap's own value
   is what stays. */
static void unit_tint_mode_uses_the_mode_value_as_the_colour(void)
{
    stage_unit();
    stage_tables();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, 10, 5, 0);

    CHECK_EQ(unit_drawn(0, 0), table_blend(5, unit_art(2, 0, 0), 0));
    CHECK_EQ(unit_drawn(0, 2), table_blend(5, unit_art(2, 0, 2), 0));
    CHECK_EQ(unit_drawn(0, 12), table_blend(5, unit_art(0, 0, 12), 0));
    CHECK_EQ(unit_drawn(0, UNIT_INTERIOR),
             table_blend(5, unit_art(2, 0, UNIT_INTERIOR), 0));
    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH), UNIT_GUARD);

    stage_unit();
    stage_tables();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, 10, 6, 0);

    CHECK_EQ(unit_drawn(0, 0), table_blend(6, unit_art(2, 0, 0), 0));
    CHECK_EQ(table_blend(6, unit_art(2, 0, 0), 0)
             != table_blend(5, unit_art(2, 0, 0), 0), 1);
}

/* CMP dword ptr [EBP+0x24],0x1 / JNZ at 0001cbcb puts the split at exactly 1:
   mode 2, the very next value, is already a tint colour and not a third
   painting mode.  The two painters' results differ in shape as well as in
   value -- the blend's low nibble is the source pixel and its high nibble the
   destination, the tint's the other way about -- so the assertion says which
   painter ran and not merely that something was drawn. */
static void unit_mode_two_is_already_a_tint_colour(void)
{
    stage_unit();
    stage_tables();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, 10, 2, 0);

    CHECK_EQ(unit_drawn(0, 2), table_blend(2, unit_art(2, 0, 2), 0));
    CHECK_EQ(table_blend(2, unit_art(2, 0, 2), 0)
             != table_blend(unit_art(2, 0, 2), UNIT_GUARD, 0), 1);
}

/* alpha is the last argument of both blended calls -- MOV EAX,dword ptr
   [EBP+0x28] / PUSH EAX at 0001cbd5 and 0001ccd7 -- and it picks the ramp row
   the painter weighs with, so the same bar at alpha 3 comes out a different
   colour from the same bar at alpha 0 in both modes. */
static void unit_alpha_reaches_both_blended_painters(void)
{
    stage_unit();
    stage_tables();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, 10, 1, 3);

    CHECK_EQ(unit_drawn(0, 2), table_blend(unit_art(2, 0, 2), UNIT_GUARD, 3));
    CHECK_EQ(table_blend(unit_art(2, 0, 2), UNIT_GUARD, 3)
             != table_blend(unit_art(2, 0, 2), UNIT_GUARD, 0), 1);

    stage_unit();
    stage_tables();
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, 10, 5, 3);

    CHECK_EQ(unit_drawn(0, 2), table_blend(5, unit_art(2, 0, 2), 3));
    CHECK_EQ(table_blend(5, unit_art(2, 0, 2), 3)
             != table_blend(5, unit_art(2, 0, 2), 0), 1);
}

/* All three painters are the colour-keyed members of their families, so a
   source byte of 0 leaves the destination pixel alone in every mode -- which
   is what lets the bar's rounded ends show the window behind them.  A zero is
   planted in the filled graphic and another in the track, one on each side of
   the seam. */
static void unit_palette_index_zero_is_transparent_in_every_mode(void)
{
    stage_unit();
    unit_sheet[2 * UNIT_ART_GRAPHIC_STRIDE + 2] = 0;
    unit_sheet[12] = 0;
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, 10, 0, 0);
    CHECK_EQ(unit_drawn(0, 2), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, 12), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, 3), unit_art(2, 0, 3));

    stage_unit();
    stage_tables();
    unit_sheet[2 * UNIT_ART_GRAPHIC_STRIDE + 2] = 0;
    unit_sheet[12] = 0;
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, 10, 1, 0);
    CHECK_EQ(unit_drawn(0, 2), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, 12), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, 3), table_blend(unit_art(2, 0, 3), UNIT_GUARD, 0));

    stage_unit();
    stage_tables();
    unit_sheet[2 * UNIT_ART_GRAPHIC_STRIDE + 2] = 0;
    unit_sheet[12] = 0;
    fdps_draw_unit_gauge(unit_dst(), UNIT_DST_PITCH, 2, 10, 5, 0);
    CHECK_EQ(unit_drawn(0, 2), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, 12), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, 3), table_blend(5, unit_art(2, 0, 3), 0));
}

/* dst_stride is passed to every blit exactly as handed over and is the only
   thing that steps the destination between rows, so a stride that is not the
   canvas pitch puts row 1 exactly 0x50 bytes past row 0 rather than a canvas
   row later.  With that stride the six rows do not line up with the canvas at
   all; what stays guarded is the gap between the end of one bar row and the
   start of the next, which the byte just past row 0's last column sits in. */
static void unit_dst_stride_is_passed_through_untouched(void)
{
    int base;

    stage_unit();
    fdps_draw_unit_gauge(unit_dst(), 0x50, 2, 10, 0, 0);

    base = UNIT_ORIGIN_ROW * UNIT_DST_PITCH + UNIT_ORIGIN_COLUMN;
    CHECK_EQ((int) unit_canvas[base], unit_art(2, 0, 0));
    CHECK_EQ((int) unit_canvas[base + 0x50], unit_art(2, 1, 0));
    CHECK_EQ((int) unit_canvas[base + 5 * 0x50 + 12], unit_art(0, 5, 12));
    CHECK_EQ((int) unit_canvas[base + UNIT_BAR_WIDTH], UNIT_GUARD);
}

/* ------------------------------------------------------------------ *
 * fdps_draw_unit_gauge_proportional @ 0001caa0
 * ------------------------------------------------------------------ */

/* Every width below is (cur_value * 0x29 + max_value - 1) / max_value in
   signed 32-bit arithmetic, with the zero path taken when CMP dword ptr
   [EBP+0x20],0x0 / JG at 0001caac falls through.  The seam between the filled
   graphic and the track sits at column 2 + width, so a case that only cares
   about the width asserts the two columns either side of it and the fact that
   the two graphics differ there. */

/* Nothing of the filled graphic reached the bar's interior: the whole of it,
   from the first column past the left cap to the last one the right cap does
   not cover, came out of graphic 0. */
static void interior_is_all_track(void)
{
    CHECK_EQ(unit_art(0, 0, 2) != unit_art(2, 0, 2), 1);
    CHECK_EQ(unit_drawn(0, 2), unit_art(0, 0, 2));
    CHECK_EQ(unit_drawn(0, 3), unit_art(0, 0, 3));
    CHECK_EQ(unit_drawn(0, 0x28), unit_art(0, 0, 0x28));
    CHECK_EQ(unit_drawn(5, 0x28), unit_art(0, 5, 0x28));
}

/* CMP dword ptr [EBP+0x20],0x0 / JG at 0001caac..0001cab0 falls through to
   MOV dword ptr [EBP+-0x4],0x0, so a max_value of 0 never reaches the IDIV and
   the bar is drawn empty however large cur_value is.  A cur_value of 50
   against it would be a division by zero if the guard were not there.  The
   caps still come out of the filled graphic, which is what says the bar was
   drawn at all rather than skipped. */
static void prop_zero_max_draws_an_empty_bar(void)
{
    stage_unit();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, 0, 50, 0,
                                      0);

    interior_is_all_track();
    CHECK_EQ(unit_drawn(0, 0), unit_art(2, 0, 0));
    CHECK_EQ(unit_drawn(0, 1), unit_art(2, 0, 1));
}

/* The same branch is JG and not JNZ, so a negative max_value takes the empty
   path too rather than dividing by it and producing a negative width. */
static void prop_negative_max_draws_an_empty_bar(void)
{
    stage_unit();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, -3, 50, 0,
                                      0);

    interior_is_all_track();
}

/* ADD EDX,dword ptr [EBP+0x20] / DEC EDX at 0001cabf..0001cac2 is what makes
   the divide a ceiling: 1 out of 200 is (41 + 199) / 200 = 1 column, where the
   truncating 41 / 200 would be 0 and the bar would read empty for a unit that
   is still alive. */
static void prop_one_current_still_lights_one_column(void)
{
    stage_unit();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, 200, 1, 0,
                                      0);

    CHECK_EQ(unit_art(0, 0, 2) != unit_art(2, 0, 2), 1);
    CHECK_EQ(unit_drawn(0, 2), unit_art(2, 0, 2));
    CHECK_EQ(unit_drawn(5, 2), unit_art(2, 5, 2));
    CHECK_EQ(unit_drawn(0, 3), unit_art(0, 0, 3));
}

/* The ceiling does not add a column that is not owed: 0 out of 200 is
   (0 + 199) / 200 = 0, so a dead unit's bar is blank. */
static void prop_zero_current_draws_an_empty_bar(void)
{
    stage_unit();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, 200, 0, 0,
                                      0);

    interior_is_all_track();
}

/* Half of 41 is 20.5 and the odd column goes to the fill: 2 out of 4 is
   (82 + 3) / 4 = 21 columns, not the 20 a truncating divide would give, so the
   seam is at column 23 and not at 22. */
static void prop_half_full_rounds_the_odd_column_up(void)
{
    stage_unit();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, 4, 2, 0,
                                      0);

    CHECK_EQ(unit_art(0, 0, 22) != unit_art(2, 0, 22), 1);
    CHECK_EQ(unit_drawn(0, 22), unit_art(2, 0, 22));
    CHECK_EQ(unit_drawn(0, 23), unit_art(0, 0, 23));
}

/* A ratio that divides badly rounds up as well: 1 out of 3 is (41 + 2) / 3 =
   14 columns where 41 / 3 would be 13, so the seam is at column 16. */
static void prop_a_third_rounds_up_to_14(void)
{
    stage_unit();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, 3, 1, 0,
                                      0);

    CHECK_EQ(unit_drawn(0, 15), unit_art(2, 0, 15));
    CHECK_EQ(unit_drawn(0, 16), unit_art(0, 0, 16));
}

/* cur_value == max_value comes out at exactly the interior's 41 and not one
   past it: 37 out of 37 is (1517 + 36) / 37 = 41, since 37 * 42 = 1554 is
   already past 1553.  41 rather than 40 is not readable at the seam -- the
   right cap paints over the interior's last two columns either way -- so the
   cap's own two pixels are planted transparent and what shows through decides
   it: at 41 the remainder is zero columns wide and draws nothing, at 40 it
   would have left graphic 0 under the cap.  The guard at column 0x2b is what
   rules out a width past 41, which would carry the run on into the art's next
   row. */
static void prop_current_equal_to_max_fills_the_interior(void)
{
    stage_unit();
    unit_sheet[2 * UNIT_ART_GRAPHIC_STRIDE + UNIT_INTERIOR] = 0;
    unit_sheet[2 * UNIT_ART_GRAPHIC_STRIDE + UNIT_BAR_WIDTH - 1] = 0;
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, 37, 37, 0,
                                      0);

    CHECK_EQ(unit_art(0, 0, 0x28) != unit_art(2, 0, 0x28), 1);
    CHECK_EQ(unit_drawn(0, 0x28), unit_art(2, 0, 0x28));
    CHECK_EQ(unit_drawn(0, UNIT_INTERIOR), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH - 1), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH), UNIT_GUARD);
}

/* Nothing here caps the width at the interior's own 0x29: 110 out of 100 is
   (4510 + 99) / 100 = 46 columns, and fdps_draw_unit_gauge blits all 46 out of
   a source whose row pitch is 0x2b, so the run passes the right cap and reads
   on into the art's NEXT ROW -- destination column 0x2b is graphic 2 row 1
   column 0 -- while the remainder, 0x29 - 46 wide, draws nothing.  Adding
   min(0x29, width) here would stop the bar at column 0x2a and put a clean full
   bar on screen, which is not what the original draws. */
static void prop_current_above_max_smears_past_the_bar(void)
{
    stage_unit();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, 100, 110,
                                      0, 0);

    CHECK_EQ(unit_drawn(0, 0x2b), unit_art(2, 1, 0));
    CHECK_EQ(unit_drawn(0, 0x2f), unit_art(2, 1, 4));
    CHECK_EQ(unit_drawn(0, 0x30), UNIT_GUARD);
    CHECK_EQ(unit_drawn(1, 0x2b), unit_art(2, 2, 0));
}

/* The IDIV is signed and the numerator is sign extended by SAR EDX,0x1f at
   0001cac5, so -10 out of 100 is (-410 + 99) / 100 = -3 truncated toward zero,
   and fdps_draw_unit_gauge's own clamp turns that into an empty bar.  An
   unsigned divide of the same numerator would be a width of some forty million
   columns, so the guards on both sides of the bar are what say which one ran:
   an unclamped negative width would also have started the remainder three
   columns to the LEFT of the bar. */
static void prop_negative_current_draws_an_empty_bar(void)
{
    stage_unit();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, 100, -10,
                                      0, 0);

    interior_is_all_track();
    CHECK_EQ(unit_drawn(0, -1), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, -3), UNIT_GUARD);
    CHECK_EQ(unit_drawn(0, UNIT_BAR_WIDTH), UNIT_GUARD);
}

/* MOV EAX,dword ptr [EBP+0x1c] / PUSH EAX at 0001cada hands gfx_index through
   untouched, so it still picks the filled graphic: 1 out of 4 is (41 + 3) / 4
   = 11 columns out of graphic 1 here, and the track after the seam is graphic
   0 whatever the index says. */
static void prop_gfx_index_reaches_the_filled_run(void)
{
    stage_unit();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 1, 4, 1, 0,
                                      0);

    CHECK_EQ(unit_art(1, 0, 12) != unit_art(0, 0, 12), 1);
    CHECK_EQ(unit_drawn(0, 2), unit_art(1, 0, 2));
    CHECK_EQ(unit_drawn(0, 12), unit_art(1, 0, 12));
    CHECK_EQ(unit_drawn(0, 13), unit_art(0, 0, 13));
}

/* The last two pushes, MOV EAX,dword ptr [EBP+0x28] at 0001cad2 and MOV
   EAX,dword ptr [EBP+0x2c] at 0001cace, hand blit_mode and alpha through
   untouched and in that order, so the same 11-column bar comes out of the
   blending painter in mode 1 and out of the tint painter in mode 5 with 5
   itself as the colour, and alpha picks the ramp row in both. */
static void prop_mode_and_alpha_pass_through(void)
{
    stage_unit();
    stage_tables();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, 4, 1, 1,
                                      0);

    CHECK_EQ(unit_drawn(0, 2), table_blend(unit_art(2, 0, 2), UNIT_GUARD, 0));
    CHECK_EQ(unit_drawn(0, 13), table_blend(unit_art(0, 0, 13), UNIT_GUARD, 0));

    stage_unit();
    stage_tables();
    fdps_draw_unit_gauge_proportional(unit_dst(), UNIT_DST_PITCH, 2, 4, 1, 5,
                                      3);

    CHECK_EQ(unit_drawn(0, 2), table_blend(5, unit_art(2, 0, 2), 3));
    CHECK_EQ(table_blend(5, unit_art(2, 0, 2), 3)
             != table_blend(5, unit_art(2, 0, 2), 0), 1);
}

/* dst and dst_stride are pushed unchanged as well -- MOV EAX,dword ptr
   [EBP+0x14] at 0001cae2 and MOV EAX,dword ptr [EBP+0x18] at 0001cade -- so a
   stride that is not the canvas pitch puts row 1 exactly 0x50 bytes past row 0
   and the seam of the same 11-column bar is still at column 13. */
static void prop_dst_and_stride_pass_through(void)
{
    int base;

    stage_unit();
    fdps_draw_unit_gauge_proportional(unit_dst(), 0x50, 2, 4, 1, 0, 0);

    base = UNIT_ORIGIN_ROW * UNIT_DST_PITCH + UNIT_ORIGIN_COLUMN;
    CHECK_EQ((int) unit_canvas[base], unit_art(2, 0, 0));
    CHECK_EQ((int) unit_canvas[base + 0x50], unit_art(2, 1, 0));
    CHECK_EQ((int) unit_canvas[base + 5 * 0x50 + 12], unit_art(2, 5, 12));
    CHECK_EQ((int) unit_canvas[base + 5 * 0x50 + 13], unit_art(0, 5, 13));
    CHECK_EQ((int) unit_canvas[base + UNIT_BAR_WIDTH], UNIT_GUARD);
}

/* ------------------------------------------------------------------ *
 * fdps_battle_show_combat_gauges @ 0001cdc0
 *
 * HOW THE SEVEN FRAMES ARE WATCHED.  The function composes every frame on a
 * page it allocates and frees itself and blits the page's 312x192 window
 * straight over the live mode 13h screen, so the adapter is the only place its
 * output can be read back from.  Every case below therefore sets mode 13h,
 * fills the frame with a border sentinel, seeds the page through the heap,
 * runs a real timer interrupt so the six frame waits end, calls, snapshots the
 * 64,000 bytes and returns to text mode -- the same way tests/menu.c watches
 * the ring menu and tests/anim.c the turn banner.
 *
 * WHAT THE SNAPSHOT SHOWS IS THE LAST FRAME AND ONLY THE LAST FRAME.  The
 * seventh frame paints in blit mode 0, which is fdps_blit_transparent_rect and
 * copies the sheet's bytes through unchanged, and the staged sheet holds no
 * zero byte, so every one of the bar's 43x6 pixels is overwritten by raw art.
 * That is what makes the expected values below plain sheet bytes: if the run
 * ended on either blended phase instead they would be inverse-cube entries,
 * and if the phases ran in the other order the closing frame would be a tint.
 *
 * WHY THE PAGE IS SEEDED THROUGH THE HEAP.  The page is not cleared, and with
 * no scene layers, no map cursor and no units staged the compositor writes
 * nothing into it, so whatever malloc hands over is what shows everywhere the
 * two bars do not reach.  Each case allocates a block of exactly the page's
 * 0x15180 bytes, zeroes it and frees it immediately before the call, so the
 * block the function is handed is that one and every undrawn window pixel
 * reads 0.  combat_page_comes_back_from_the_heap is that assumption stated as
 * an assertion.
 *
 * Expected values come from the assembly at 0001cdc0 -- PUSH 0x15180 / CALL
 * malloc at 0001ce89, MOV EAX,[0x00064020] / ADD EAX,0x18 / IMUL EAX,EAX,0x168
 * / ADD [EBP-0x38] / ADD [0x0006401c] / ADD EAX,0x18 at 0001ce99 for the
 * destination, CMP EAX,0x1 / JNZ at 0001cdee for the counter test, MOV dword
 * ptr [0x0006401c],0xffffffff at 0001ce07 for the refusal, CMP byte ptr
 * [EAX+0x6],0x0 / JNZ at 0001ce32 and 0001ce4b for the two graphics, the four
 * MOVSX at 0001ce64..0001ce86 for the HP pairs, the IMUL ...,0x29 / ADD / DEC
 * / SAR / IDIV triples at 0001cf69, 0001d037, 0001d1b5, 0001d2aa, 0001d41d and
 * 0001d513 for the six fills, CMP [EBP-0x20],0x10 / JL with ADD ...,0x6 for
 * the three strengths of each animated phase, MOV ...,0x1a at 0001d139 for the
 * tint colour, the two MOV ...,0x0 at 0001d3a0 and 0001d3aa for the closing
 * frame, and PUSH 0xc0 / PUSH 0x138 / PUSH 0x140 / PUSH 0xa0504 / PUSH 0x168 /
 * page + 0x21d8 at 0001d098 for the window.  None of them is read off the
 * emitted C.
 * ------------------------------------------------------------------ */

/* The adapter, the frame it presents and the two modes the cases switch
   between. */
#define CG_VGA_BASE 0x000a0000
#define CG_SCREEN_W 0x140
#define CG_SCREEN_H 0xc8
#define CG_SCREEN_BYTES (CG_SCREEN_W * CG_SCREEN_H)
#define CG_MODE_TEXT 0x03
#define CG_MODE_320X200X256 0x13

/* The window the function copies out of its page: 312x192 taken from page byte
   0x21d8, which is page pixel (24,24), and landing at screen byte 0x504, which
   is screen pixel (4,4).  A gauge placed at view position (x,y) is therefore
   drawn at page pixel (x + 24, y + 24) and lands on screen at (x + 4, y + 4),
   which is what every expected position below is built from. */
#define CG_WINDOW_ROW 4
#define CG_WINDOW_COL 4
#define CG_WINDOW_W 0x138
#define CG_WINDOW_H 0xc0

/* The page, and the value that says a screen byte is outside the presented
   window.  The page seed is 0 because the staged sheet is filled 1..251, so
   "non-zero inside the window" counts exactly the pixels the bars painted. */
#define CG_PAGE_BYTES 0x15180
#define CG_BORDER_FILL 0xa5

/* The timer the six frame waits are paced by. */
#define CG_TIMER_VECTOR 8

/* One whole bar is 43 columns by 6 rows and every one of those pixels is
   written by the closing frame, so a run that drew one bar leaves exactly this
   many non-zero bytes inside the window. */
#define CG_BAR_PIXELS (UNIT_BAR_WIDTH * UNIT_BAR_ROWS)

/* Item record geometry, from IMUL EAX,dword ptr [EBP+0x14],0x17 in
   fdps_get_item_record, and the inventory flag bit fdps_unit_find_equipped_slot
   tests with AND AL,0x40. */
#define CG_ITEM_STRIDE 0x17
#define CG_ITEM_COUNT 256
#define CG_EQUIPPED 0x40
#define CG_ITEM_TYPE_WEAPON 0x01
#define CG_COUNTER_ITEM_ID 7

/* What the two position slots hold going in, so a case can say which of them
   the function wrote.  Neither is a value any placement produces. */
#define CG_POISON_X 200
#define CG_POISON_Y 100

/* The two units, and the tiles and facings that put their bars on rows that do
   not overlap: the defender faces 0 and is placed above and to the right of
   tile (0,0), the attacker faces 2 and is placed below and to the left of tile
   (0,1).  The two tiles are one step apart, which is what
   fdps_check_can_counter_attack needs. */
#define CG_ATTACKER 0
#define CG_DEFENDER 1

/* fdps_battle_compute_unit_gauge_position's answers for those two, worked out
   the same way the placement cases at the top of this file work theirs out:
   the defender's anchor is (4, 0), the up step is unaffordable so y takes the
   +5 nudge and the right step is affordable so x becomes 0x1c; the attacker's
   anchor is (4, 24), the down step is affordable so y becomes 46 and the left
   step is not so x takes the +0x1c fallback. */
#define CG_DEFENDER_POS_X 28
#define CG_DEFENDER_POS_Y 5
#define CG_ATTACKER_POS_X 32
#define CG_ATTACKER_POS_Y 46

/* THE SNAPSHOT IS ON THE HEAP AND NOT A STATIC, AND THAT IS NOT TIDINESS.
   tests/anim.c reads the whole 27 MB of MISC.VFS into one malloc for each of
   its banner cases, and the guest is given 32 MB, so the suite runs within
   about a megabyte of the ceiling.  A fourth 64,000-byte static frame buffer
   -- tests/anim.c already has two and tests/menu.c one -- is enough to make
   fdps_animate_turn_banner's untested malloc come back NULL, and that function
   writes through it: the run dies inside tests/anim.c with the extender's IDT
   overwritten, nowhere near here.  This buffer is therefore taken and given
   back around each run, when the big archive is not held. */
static unsigned char *cg_screen;
static unsigned char cg_items[(CG_ITEM_COUNT + 1) * CG_ITEM_STRIDE];
static void (__interrupt __far *cg_saved_timer)();
static int cg_blocks_before;
static int cg_blocks_after;
static unsigned int cg_ticks_used;

static void __interrupt __far cg_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(cg_saved_timer);
}

/* The item table is published one record PAST the start of its storage so
   record -1 is real addressable memory, the arrangement tests/aitarget.c and
   tests/unititem.c both use. */
static struct fdps_item_effect *cg_item(int item_id)
{
    return (struct fdps_item_effect *)
           (cg_items + (item_id + 1) * CG_ITEM_STRIDE);
}

/* One unit's side, HP pair and tile.  stage() has already zeroed every record,
   so nothing is equipped and every status timer is clear. */
static void cg_set_unit(int unit_index, int tile_column, int tile_row,
                        int facing, int side, int hp_current, int hp_max)
{
    set_unit(unit_index, tile_column, tile_row, facing);
    stage_units[unit_index].side = (unsigned char) side;
    stage_units[unit_index].hp_current = (short) hp_current;
    stage_units[unit_index].hp_max = (short) hp_max;
}

/* Nothing on the map and nothing in the way -- no scene layers, no map cursor
   overlay and no units -- so the compositor writes nothing into the page and
   every window pixel the bars do not reach is the seed.  The unit gauge sheet
   and the two blending tables come from the staging the unit gauge cases above
   already use. */
static void cg_stage(void)
{
    int offset;

    stage();
    stage_unit();
    stage_tables();
    for (offset = 0; offset < (int) sizeof(cg_items); offset++) {
        cg_items[offset] = 0;
    }
    data_fdps_item_effect_table_ptr = cg_items + CG_ITEM_STRIDE;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_battle_combat_gauge_pos_pairs[0] = CG_POISON_X;
    data_fdps_battle_combat_gauge_pos_pairs[1] = CG_POISON_Y;
    data_fdps_battle_combat_gauge_pos_pairs[2] = CG_POISON_X;
    data_fdps_battle_combat_gauge_pos_pairs[3] = CG_POISON_Y;
}

/* Put the staged globals back to the state a freshly started program has them
   in, for the reason tests/menu.c gives: three of them hold blocks the game's
   own loaders free, and leaving one pointing at a static in this file hands a
   later test a free() of storage that never came from the heap. */
static void cg_unstage(void)
{
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_unit_gauge_sheet_ptr = NULL;
    free(cg_screen);
    cg_screen = NULL;
}

/* Give the defender an equipped weapon whose minimum range is 1, which is the
   last of fdps_check_can_counter_attack's four tests. */
static void cg_arm_defender(void)
{
    struct fdps_item_effect *weapon;

    stage_units[CG_DEFENDER].inventory_slots[0] = CG_EQUIPPED;
    stage_units[CG_DEFENDER].inventory_slots[1] = CG_COUNTER_ITEM_ID;
    weapon = cg_item(CG_COUNTER_ITEM_ID);
    weapon->type = CG_ITEM_TYPE_WEAPON;
    weapon->range_min = 1;
    weapon->range_max = 1;
}

static void cg_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Used entries currently in the heap, so a case can say the page came back. */
static int cg_used_heap_blocks(void)
{
    struct _heapinfo entry;
    int used;

    used = 0;
    entry._pentry = NULL;
    while (_heapwalk(&entry) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY) {
            used++;
        }
    }
    return used;
}

/* Leave a zeroed block of exactly the page's size at the head of the free
   list. */
static void cg_seed_page(void)
{
    unsigned char *page;

    page = (unsigned char *) malloc((size_t) CG_PAGE_BYTES);
    if (page != NULL) {
        memset(page, 0, (size_t) CG_PAGE_BYTES);
        free(page);
    }
}

/* One whole run, leaving the frame in cg_screen[] and the answer in the
   returned pointer. */
static int *cg_run(void)
{
    unsigned int before_ticks;
    int *pairs;

    cg_screen = (unsigned char *) malloc((size_t) CG_SCREEN_BYTES);
    CHECK_EQ(cg_screen != NULL, 1);
    if (cg_screen == NULL) {
        return data_fdps_battle_combat_gauge_pos_pairs;
    }
    memset(cg_screen, CG_BORDER_FILL, (size_t) CG_SCREEN_BYTES);

    cg_blocks_before = cg_used_heap_blocks();
    cg_set_mode(CG_MODE_320X200X256);
    memset((void *) CG_VGA_BASE, CG_BORDER_FILL, (size_t) CG_SCREEN_BYTES);
    cg_seed_page();

    cg_saved_timer = _dos_getvect(CG_TIMER_VECTOR);
    _dos_setvect(CG_TIMER_VECTOR, cg_timer_isr);
    before_ticks = data_fdps_timer_tick_counter;
    pairs = fdps_battle_show_combat_gauges(CG_ATTACKER, CG_DEFENDER);
    cg_ticks_used = data_fdps_timer_tick_counter - before_ticks;
    _dos_setvect(CG_TIMER_VECTOR, cg_saved_timer);

    memmove(cg_screen, (void *) CG_VGA_BASE, (size_t) CG_SCREEN_BYTES);
    cg_set_mode(CG_MODE_TEXT);
    cg_blocks_after = cg_used_heap_blocks();
    return pairs;
}

static int cg_pixel(int row, int col)
{
    return (int) cg_screen[row * CG_SCREEN_W + col];
}

/* One pixel of a bar whose gauge position is (pos_x, pos_y), in the bar's own
   coordinates. */
static int cg_bar(int pos_x, int pos_y, int row, int column)
{
    return cg_pixel(pos_y + CG_WINDOW_ROW + row,
                    pos_x + CG_WINDOW_COL + column);
}

/* How many bytes inside the presented window are not the page seed. */
static int cg_painted(void)
{
    int row;
    int col;
    int painted;

    painted = 0;
    for (row = 0; row < CG_WINDOW_H; row++) {
        for (col = 0; col < CG_WINDOW_W; col++) {
            if (cg_pixel(CG_WINDOW_ROW + row, CG_WINDOW_COL + col) != 0) {
                painted++;
            }
        }
    }
    return painted;
}

/* How many bytes outside the presented window are no longer the sentinel the
   frame was filled with. */
static int cg_border_touched(void)
{
    int row;
    int col;
    int touched;

    touched = 0;
    for (row = 0; row < CG_SCREEN_H; row++) {
        for (col = 0; col < CG_SCREEN_W; col++) {
            if (row >= CG_WINDOW_ROW && row < CG_WINDOW_ROW + CG_WINDOW_H
                && col >= CG_WINDOW_COL && col < CG_WINDOW_COL + CG_WINDOW_W) {
                continue;
            }
            if (cg_pixel(row, col) != CG_BORDER_FILL) {
                touched++;
            }
        }
    }
    return touched;
}

/* Every segment of one closing-frame bar: the two-pixel left cap and the fill
   run out of graphic gfx_index, the rest of the 41-column interior out of
   GRAPHIC 0 at its own columns, and the right cap out of gfx_index again.  Row
   5 is checked alongside row 0 because every blit is handed 6 as its row count
   and steps by the page's 0x168 pitch. */
static void cg_bar_is(int pos_x, int pos_y, int gfx_index, int fill_width)
{
    CHECK_EQ(cg_bar(pos_x, pos_y, 0, 0), unit_art(gfx_index, 0, 0));
    CHECK_EQ(cg_bar(pos_x, pos_y, 0, 1), unit_art(gfx_index, 0, 1));
    CHECK_EQ(cg_bar(pos_x, pos_y, 5, 1), unit_art(gfx_index, 5, 1));
    if (fill_width > 0) {
        CHECK_EQ(cg_bar(pos_x, pos_y, 0, 2), unit_art(gfx_index, 0, 2));
        CHECK_EQ(cg_bar(pos_x, pos_y, 0, fill_width + 1),
                 unit_art(gfx_index, 0, fill_width + 1));
        CHECK_EQ(cg_bar(pos_x, pos_y, 5, fill_width + 1),
                 unit_art(gfx_index, 5, fill_width + 1));
    }
    CHECK_EQ(unit_art(0, 0, fill_width + 2)
             != unit_art(gfx_index, 0, fill_width + 2), 1);
    CHECK_EQ(cg_bar(pos_x, pos_y, 0, fill_width + 2),
             unit_art(0, 0, fill_width + 2));
    CHECK_EQ(cg_bar(pos_x, pos_y, 0, UNIT_INTERIOR - 1),
             unit_art(0, 0, UNIT_INTERIOR - 1));
    CHECK_EQ(cg_bar(pos_x, pos_y, 0, UNIT_INTERIOR),
             unit_art(gfx_index, 0, UNIT_INTERIOR));
    CHECK_EQ(cg_bar(pos_x, pos_y, 5, UNIT_BAR_WIDTH - 1),
             unit_art(gfx_index, 5, UNIT_BAR_WIDTH - 1));
}

/* The three record bytes the function addresses by literal displacement: +6
   for the side that picks the graphic and +0x40 / +0x42 for the HP pair the
   fill is taken over.  If the layout moved, every case below would still pass
   while reading the wrong bytes. */
static void combat_gauges_read_the_measured_offsets(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
}

/* MOV dword ptr [EBP-0x4],0x64014 / MOV EAX,[EBP-0x4] at 0001d5b6: the answer
   is the global itself and not a copy, and pair 0 holds the defender's
   placement -- the same (28,5) the placement cases at the top of this file get
   for tile (0,0) facing 0 with the view at the origin.

   fdps_check_can_counter_attack refuses a defender two tiles away, so element
   2 is the -1 that suppresses the attacker's bar, and element 3 is not written
   at all: the refusal arm is a single store to element 2.  A run that placed
   the attacker anyway would leave 46 there. */
static void combat_gauges_return_the_pair_and_suppress_without_a_counter(void)
{
    int *pairs;

    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 0, 1, 1000);
    cg_set_unit(CG_ATTACKER, 5, 5, 2, 1, 3, 4);
    pairs = cg_run();

    CHECK_EQ(pairs == data_fdps_battle_combat_gauge_pos_pairs, 1);
    CHECK_EQ(pairs[0], CG_DEFENDER_POS_X);
    CHECK_EQ(pairs[1], CG_DEFENDER_POS_Y);
    CHECK_EQ(pairs[2], -1);
    CHECK_EQ(pairs[3], CG_POISON_Y);
    CHECK_EQ(cg_painted(), CG_BAR_PIXELS);
    cg_unstage();
}

/* The one bar that run drew, read back segment by segment.  The defender's
   side byte is 0, so its graphic is 2 and not 1, and 1 HP of 1000 is
   (41 + 999) / 1000 = 1 filled column -- the sliver the ceiling exists for.

   The values being raw sheet bytes is also what pins the closing frame's blit
   mode at 0: the blended phases write inverse-cube entries, so a run that
   ended on one of those would not match here. */
static void combat_gauges_draw_the_defender_bar_from_side_zero(void)
{
    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 0, 1, 1000);
    cg_set_unit(CG_ATTACKER, 5, 5, 2, 1, 3, 4);
    cg_run();

    CHECK_EQ(unit_art(2, 0, 0) != unit_art(1, 0, 0), 1);
    cg_bar_is(CG_DEFENDER_POS_X, CG_DEFENDER_POS_Y, 2, 1);
    cg_unstage();
}

/* CMP byte ptr [EAX+0x6],0x0 / JNZ at 0001ce4b: any side other than 0 takes
   graphic 1, so the same bar in the same place is drawn out of a different
   graphic.  Side 2 is used rather than 1 to show the test is against 0 and not
   a two-way flag. */
static void combat_gauges_side_other_than_zero_takes_graphic_one(void)
{
    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 2, 1, 1000);
    cg_set_unit(CG_ATTACKER, 5, 5, 2, 1, 3, 4);
    cg_run();

    cg_bar_is(CG_DEFENDER_POS_X, CG_DEFENDER_POS_Y, 1, 1);
    cg_unstage();
}

/* Both bars, with the counter confirmed.  The attacker is one tile away and
   the defender holds an equipped weapon of minimum range 1, which is what
   makes fdps_check_can_counter_attack answer exactly 1; element 2 then holds
   the attacker's own placement instead of -1.

   The attacker's side is 1, so its graphic is 1 against the defender's 2, and
   3 HP of 4 is (123 + 3) / 4 = 31 filled columns against the defender's 1.
   The two facings put the bars 41 rows apart, so the painted count is exactly
   two whole bars and neither has overwritten the other. */
static void combat_gauges_draw_both_bars_on_a_counter(void)
{
    int *pairs;

    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 0, 1, 1000);
    cg_set_unit(CG_ATTACKER, 0, 1, 2, 1, 3, 4);
    cg_arm_defender();
    pairs = cg_run();

    CHECK_EQ(pairs[0], CG_DEFENDER_POS_X);
    CHECK_EQ(pairs[1], CG_DEFENDER_POS_Y);
    CHECK_EQ(pairs[2], CG_ATTACKER_POS_X);
    CHECK_EQ(pairs[3], CG_ATTACKER_POS_Y);
    CHECK_EQ(cg_painted(), 2 * CG_BAR_PIXELS);
    cg_bar_is(CG_DEFENDER_POS_X, CG_DEFENDER_POS_Y, 2, 1);
    cg_bar_is(CG_ATTACKER_POS_X, CG_ATTACKER_POS_Y, 1, 31);
    cg_unstage();
}

/* CMP dword ptr [EBP+0xffffff78],0x0 / JG at 0001d025 and its five siblings:
   a maximum of 0 never reaches the IDIV and the bar is drawn empty, so the
   interior is graphic 0 from its first column and the caps are still the
   unit's own graphic.  A current of 50 against it would be a division by zero
   if the guard were not there. */
static void combat_gauges_zero_max_hp_draws_an_empty_bar(void)
{
    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 0, 50, 0);
    cg_set_unit(CG_ATTACKER, 5, 5, 2, 1, 3, 4);
    cg_run();

    cg_bar_is(CG_DEFENDER_POS_X, CG_DEFENDER_POS_Y, 2, 0);
    CHECK_EQ(cg_painted(), CG_BAR_PIXELS);
    cg_unstage();
}

/* The same branch is JG and not JNE, so a negative maximum takes the empty
   path too rather than dividing by it. */
static void combat_gauges_negative_max_hp_draws_an_empty_bar(void)
{
    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 0, 5, -10);
    cg_set_unit(CG_ATTACKER, 5, 5, 2, 1, 3, 4);
    cg_run();

    cg_bar_is(CG_DEFENDER_POS_X, CG_DEFENDER_POS_Y, 2, 0);
    cg_unstage();
}

/* ADD EDX,max / DEC EDX before the IDIV is what makes the fill a ceiling and
   not a truncation: 1 HP of 2 is (41 + 1) / 2 = 21 columns, where 41 / 2 would
   be 20 and the seam would sit one column to the left.  The seam is read at
   both ends, so a fill of 20 or 22 fails here. */
static void combat_gauges_fill_is_the_ceiling_over_41_columns(void)
{
    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 0, 1, 2);
    cg_set_unit(CG_ATTACKER, 5, 5, 2, 1, 3, 4);
    cg_run();

    CHECK_EQ(cg_bar(CG_DEFENDER_POS_X, CG_DEFENDER_POS_Y, 0, 22),
             unit_art(2, 0, 22));
    CHECK_EQ(unit_art(0, 0, 23) != unit_art(2, 0, 23), 1);
    CHECK_EQ(cg_bar(CG_DEFENDER_POS_X, CG_DEFENDER_POS_Y, 0, 23),
             unit_art(0, 0, 23));
    cg_unstage();
}

/* The record's HP words are read with MOVSX at 0001ce64 and 0001ce7c, so a
   current above 0x7fff is negative and not a huge positive: -1 of 1000 gives
   (-41 + 999) / 1000 = 0 columns after truncation toward zero, which is the
   empty bar, where an unsigned read would give a fill far past the interior
   and smear the art's next row across it. */
static void combat_gauges_hp_words_are_read_signed(void)
{
    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 0, -1, 1000);
    cg_set_unit(CG_ATTACKER, 5, 5, 2, 1, 3, 4);
    cg_run();

    cg_bar_is(CG_DEFENDER_POS_X, CG_DEFENDER_POS_Y, 2, 0);
    CHECK_EQ(cg_painted(), CG_BAR_PIXELS);
    cg_unstage();
}

/* PUSH 0xa0504 / PUSH 0x140 / PUSH 0x138 / PUSH 0xc0 with page + 0x21d8 as the
   source: 312x192 out of page pixel (24,24) and down at screen pixel (4,4).
   Nothing outside that rectangle is touched, which is what the four-pixel
   margin of the sentinel proves, and the bar landing at screen (32,9) for a
   gauge position of (28,5) is what fixes the two origins against each other --
   a source of page byte 0 would put it at (52,29) and a destination of 0xa0000
   at (28,5). */
static void combat_gauges_present_312x192_at_screen_4_4(void)
{
    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 0, 1, 1000);
    cg_set_unit(CG_ATTACKER, 5, 5, 2, 1, 3, 4);
    cg_run();

    CHECK_EQ(cg_border_touched(), 0);
    CHECK_EQ(cg_pixel(9, 32), unit_art(2, 0, 0));
    CHECK_EQ(cg_pixel(9, 31), 0);
    CHECK_EQ(cg_pixel(8, 32), 0);
    CHECK_EQ(cg_pixel(9, 32 + UNIT_BAR_WIDTH), 0);
    CHECK_EQ(cg_pixel(9 + UNIT_BAR_ROWS, 32), 0);
    cg_unstage();
}

/* CALL free at 0001d5ae: the page is released before the return, so the heap
   holds no more used blocks after the run than before it.  This is also what
   the seeding above depends on -- if the block did not come back, every
   expected value in this section would be comparing against rubbish. */
static void combat_page_comes_back_from_the_heap(void)
{
    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 0, 1, 1000);
    cg_set_unit(CG_ATTACKER, 5, 5, 2, 1, 3, 4);
    cg_run();

    CHECK_EQ(cg_blocks_after, cg_blocks_before);
    cg_unstage();
}

/* Six of the seven frames end by waiting for the timer tick to move -- MOV
   EAX,[EBP-0x24] / CMP EAX,[0x00069d64] / JZ back at 0001d0c2, 0001d341 and
   nowhere after the closing frame.  The first frame's latch is uninitialised
   and may end its wait at once, so five ticks are the guaranteed floor and a
   run that waited nowhere, or that latched the counter before the first loop
   and waited seven times, does not land on it. */
static void combat_gauges_pace_six_of_the_seven_frames(void)
{
    cg_stage();
    cg_set_unit(CG_DEFENDER, 0, 0, 0, 0, 1, 1000);
    cg_set_unit(CG_ATTACKER, 5, 5, 2, 1, 3, 4);
    cg_run();

    CHECK_EQ(cg_ticks_used >= 5, 1);
    cg_unstage();
}

/* ------------------------------------------------------------------ *
 * fdps_draw_unit_hp_mp_gauges @ 00019310
 * ------------------------------------------------------------------ */

/* The panel geometry comes from the assembly at 00019310 and from nowhere
   else: SHL EAX,0x5 / ADD EAX,0xc3 at 0001936d and IMUL EAX,dword ptr
   [EBP+0x18],0xc7 / ADD EAX,0x1e at 00019388 for the two panel origins, MOV
   dword ptr [EBP+-0x18],0x0 / MOV dword ptr [EBP+-0x4],0x1 against 0x2 / 0x13
   for the strip base and the bar column, CMP byte ptr [EAX+0x6],0x0 / JZ at
   00019356 for the split, PUSH 0x0 and PUSH 0xa at 00019399 and 000193bb for
   the two frame rows, ADD EAX,EAX at 000193dd and IMUL EAX,dword ptr
   [EBP+0x18],0xc at 00019456 for the two fill rows, the four MOVSX word reads
   at 0001932e, 00019338, 00019342 and 0001934c for the stat pairs, and
   IMUL EDX,...,0x7d / ADD EDX / DEC EDX / SAR EDX,0x1f / IDIV at
   0001942f..0001943c for the ceiling.

   The function draws through two callees that are real code here -- the frames
   through fdps_cel_blit_sprite and the bars through fdps_draw_gauge_fill -- so
   every case below reads its answer back off the destination surface.  The two
   art sources are staged rather than loaded: the fill strips reuse stage_fill
   above, and the .CEL is a four-sprite sheet built here whose sprites are one
   pixel each, so a frame blit marks its own corner and nothing else.  Ticket 23
   owns what either real sheet holds, and neither is opened by name by anything
   in this file. */

/* The frame the panels are composed in.  0x170 is the pitch every call site
   passes; 0x180 is a second one, used by the case that shows the stride is not
   a constant inside the function, and the array is sized for that wider one. */
#define HPMP_FRAME_PITCH 0x170
#define HPMP_ALT_PITCH 0x180
#define HPMP_FRAME_ROWS 240
#define HPMP_FRAME_BYTES (HPMP_FRAME_ROWS * HPMP_ALT_PITCH)

/* Outside the 1..251 the staged art takes, so "nothing was drawn here" cannot
   be satisfied by an art byte that happened to match. */
#define HPMP_GUARD 0xfe

/* The two panel origins, spelled out from the assembly rather than taken from
   a header. */
#define HPMP_UP_RIGHT_ROW 0x20
#define HPMP_UP_RIGHT_COLUMN 0xc3
#define HPMP_UP_RIGHT_BAR_COLUMN 1
#define HPMP_LOW_LEFT_ROW 0xc7
#define HPMP_LOW_LEFT_COLUMN 0x1e
#define HPMP_LOW_LEFT_BAR_COLUMN 0x13

/* Four sprites is what the two panels between them ask for: strips 0 and 1 for
   one and 2 and 3 for the other.  The header is fifteen bytes, the offset table
   is sprite_count + 1 entries of four bytes right after it, and every stored
   offset is measured from the start of the sheet. */
#define HPMP_CEL_SPRITES 4
#define HPMP_CEL_TABLE_START 15
#define HPMP_CEL_STREAM_START (HPMP_CEL_TABLE_START + (HPMP_CEL_SPRITES + 1) * 4)
#define HPMP_CEL_STREAM_BYTES 2
#define HPMP_CEL_BYTES (HPMP_CEL_STREAM_START \
                        + HPMP_CEL_SPRITES * HPMP_CEL_STREAM_BYTES)

/* Sprite N paints the single byte HPMP_CEL_PIXEL_BASE + N, so which sprite
   reached the surface is readable from the surface. */
#define HPMP_CEL_PIXEL_BASE 0xa0

static unsigned char hpmp_cel[HPMP_CEL_BYTES];
static unsigned char hpmp_frame[HPMP_FRAME_BYTES];

/* Zero records, a staged fill sheet, a one-pixel-per-sprite .CEL and a frame
   of guard bytes. */
static void hpmp_stage(void)
{
    int sprite;
    int offset;

    stage();
    stage_fill();

    for (offset = 0; offset < HPMP_CEL_BYTES; offset++) {
        hpmp_cel[offset] = 0;
    }
    hpmp_cel[0] = 'C';
    hpmp_cel[1] = 'E';
    hpmp_cel[2] = 'L';
    *(short *) (hpmp_cel + 7) = 1;
    *(short *) (hpmp_cel + 9) = 1;
    *(short *) (hpmp_cel + 11) = HPMP_CEL_SPRITES;
    for (sprite = 0; sprite < HPMP_CEL_SPRITES; sprite++) {
        offset = HPMP_CEL_STREAM_START + sprite * HPMP_CEL_STREAM_BYTES;
        *(int *) (hpmp_cel + HPMP_CEL_TABLE_START + sprite * 4) = offset;
        /* Command 0x00 is a fill run of one pixel, and the byte after it is
           the pixel (rle.h). */
        hpmp_cel[offset] = 0x00;
        hpmp_cel[offset + 1] = (unsigned char) (HPMP_CEL_PIXEL_BASE + sprite);
    }
    *(int *) (hpmp_cel + HPMP_CEL_TABLE_START + HPMP_CEL_SPRITES * 4) =
        HPMP_CEL_BYTES;
    data_fdps_combat_gauge_sprite_sheet_ptr = hpmp_cel;

    for (offset = 0; offset < HPMP_FRAME_BYTES; offset++) {
        hpmp_frame[offset] = HPMP_GUARD;
    }
}

/* The five record fields this function reads. */
static void hpmp_set_unit(int unit_index, int side, int hp_current, int hp_max,
                          int mp_current, int mp_max)
{
    stage_units[unit_index].side = (unsigned char) side;
    stage_units[unit_index].hp_current = (short) hp_current;
    stage_units[unit_index].hp_max = (short) hp_max;
    stage_units[unit_index].mp_current = (short) mp_current;
    stage_units[unit_index].mp_max = (short) mp_max;
}

/* One frame pixel in each panel's own coordinates, at the pitch every call
   site uses. */
static int up_right(int row, int column)
{
    return (int) hpmp_frame[(HPMP_UP_RIGHT_ROW + row) * HPMP_FRAME_PITCH
                            + HPMP_UP_RIGHT_COLUMN + column];
}

static int low_left(int row, int column)
{
    return (int) hpmp_frame[(HPMP_LOW_LEFT_ROW + row) * HPMP_FRAME_PITCH
                            + HPMP_LOW_LEFT_COLUMN + column];
}

/* The offsets every case below addresses through.  If the record were shaped
   differently, each of them would be reading other bytes. */
static void hpmp_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_current), 0x44);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_max), 0x46);
}

/* Side 2 takes the fall-through arm: origin dest_stride * 0x20 + 0xc3, frames
   from sprites 0 and 1, bars one pixel in out of strips 0 and 1 -- and strips
   below 2 fill from the RIGHT, so both runs end at the span's last column
   rather than starting at its first.  HP is (50 * 125 + 99) / 100 = 63 columns
   and MP (25 * 125 + 99) / 100 = 32, so they are shifted right by 62 and 93.

   The two guard columns at bar column 0 are what pin the bar column at 1 and
   the MP frame row at 10 rather than at the fill's own 12. */
static void hpmp_side_two_takes_the_upper_right_panel(void)
{
    hpmp_stage();
    hpmp_set_unit(0, 2, 50, 100, 25, 100);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_FRAME_PITCH, 0);

    CHECK_EQ(up_right(0, 0), HPMP_CEL_PIXEL_BASE);
    CHECK_EQ(up_right(10, 0), HPMP_CEL_PIXEL_BASE + 1);
    CHECK_EQ(up_right(1, 0), HPMP_GUARD);
    CHECK_EQ(up_right(2, 0), HPMP_GUARD);
    CHECK_EQ(up_right(12, 0), HPMP_GUARD);

    CHECK_EQ(up_right(2, HPMP_UP_RIGHT_BAR_COLUMN + 62), fill_art(0, 0, 62));
    CHECK_EQ(up_right(2, HPMP_UP_RIGHT_BAR_COLUMN + 124), fill_art(0, 0, 124));
    CHECK_EQ(up_right(6, HPMP_UP_RIGHT_BAR_COLUMN + 62), fill_art(0, 4, 62));
    CHECK_EQ(up_right(2, HPMP_UP_RIGHT_BAR_COLUMN + 61), HPMP_GUARD);
    CHECK_EQ(up_right(7, HPMP_UP_RIGHT_BAR_COLUMN + 62), HPMP_GUARD);

    CHECK_EQ(up_right(12, HPMP_UP_RIGHT_BAR_COLUMN + 93), fill_art(1, 0, 93));
    CHECK_EQ(up_right(12, HPMP_UP_RIGHT_BAR_COLUMN + 124), fill_art(1, 0, 124));
    CHECK_EQ(up_right(16, HPMP_UP_RIGHT_BAR_COLUMN + 93), fill_art(1, 4, 93));
    CHECK_EQ(up_right(12, HPMP_UP_RIGHT_BAR_COLUMN + 92), HPMP_GUARD);

    CHECK_EQ(low_left(0, 0), HPMP_GUARD);
}

/* Side 0 takes the other arm: origin dest_stride * 0xc7 + 0x1e, frames from
   sprites 2 and 3, bars 0x13 pixels in out of strips 2 and 3 -- and strips from
   2 up fill from the LEFT, so both runs start at the span's first column.  The
   same 50 of 100 and 25 of 100 give the same 63 and 32 columns, which is what
   makes the mirroring the only difference between this case and the one
   above. */
static void hpmp_side_zero_takes_the_lower_left_panel(void)
{
    hpmp_stage();
    hpmp_set_unit(1, 0, 50, 100, 25, 100);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_FRAME_PITCH, 1);

    CHECK_EQ(low_left(0, 0), HPMP_CEL_PIXEL_BASE + 2);
    CHECK_EQ(low_left(10, 0), HPMP_CEL_PIXEL_BASE + 3);

    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN), fill_art(2, 0, 0));
    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN + 62), fill_art(2, 0, 62));
    CHECK_EQ(low_left(6, HPMP_LOW_LEFT_BAR_COLUMN), fill_art(2, 4, 0));
    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN + 63), HPMP_GUARD);
    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN - 1), HPMP_GUARD);

    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN), fill_art(3, 0, 0));
    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN + 31), fill_art(3, 0, 31));
    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN + 32), HPMP_GUARD);

    CHECK_EQ(up_right(0, 0), HPMP_GUARD);
}

/* The test is CMP byte ptr [EAX+0x6],0x0 and not a compare against the player
   side's own 2, so side 1 and side 255 both land in the upper right.  Read as a
   signed char, 255 would still be non-zero, so this is about the value the
   branch tests and not about the widening. */
static void hpmp_any_non_zero_side_takes_the_upper_right_panel(void)
{
    hpmp_stage();
    hpmp_set_unit(0, 1, 100, 100, 100, 100);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_FRAME_PITCH, 0);
    CHECK_EQ(up_right(0, 0), HPMP_CEL_PIXEL_BASE);
    CHECK_EQ(low_left(0, 0), HPMP_GUARD);

    hpmp_stage();
    hpmp_set_unit(0, 255, 100, 100, 100, 100);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_FRAME_PITCH, 0);
    CHECK_EQ(up_right(0, 0), HPMP_CEL_PIXEL_BASE);
    CHECK_EQ(low_left(0, 0), HPMP_GUARD);
}

/* unit_index goes straight to fdps_get_unit_record, whose stride is 0x50, so
   the panel drawn is record 3's and not record 0's.  Record 0 is staged with
   the other side and a full pair, which would put the panel in the other corner
   and fill it whole. */
static void hpmp_unit_index_selects_the_record(void)
{
    hpmp_stage();
    hpmp_set_unit(0, 0, 100, 100, 100, 100);
    hpmp_set_unit(3, 2, 50, 100, 25, 100);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_FRAME_PITCH, 3);

    CHECK_EQ(up_right(0, 0), HPMP_CEL_PIXEL_BASE);
    CHECK_EQ(low_left(0, 0), HPMP_GUARD);
    CHECK_EQ(up_right(2, HPMP_UP_RIGHT_BAR_COLUMN + 62), fill_art(0, 0, 62));
    CHECK_EQ(up_right(2, HPMP_UP_RIGHT_BAR_COLUMN + 61), HPMP_GUARD);
}

/* CMP dword ptr [EBP+-0x30],0x0 / JG at 00019420 and the same at 0001949c: a
   maximum of 0 and a negative maximum both fall through to a width of 0 and
   never reach the IDIV, so the bar is empty however large the current is.  The
   frames still go down, which is what separates "the bar was skipped" from
   "the function did nothing". */
static void hpmp_zero_and_negative_max_draw_empty_bars(void)
{
    hpmp_stage();
    hpmp_set_unit(0, 0, 50, 0, 5, -10);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_FRAME_PITCH, 0);

    CHECK_EQ(low_left(0, 0), HPMP_CEL_PIXEL_BASE + 2);
    CHECK_EQ(low_left(10, 0), HPMP_CEL_PIXEL_BASE + 3);
    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN), HPMP_GUARD);
    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN + 124), HPMP_GUARD);
    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN), HPMP_GUARD);
    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN + 124), HPMP_GUARD);
}

/* MOVSX word ptr [EAX+0x42] at 00019338 widens the maximum SIGNED, so the word
   0xffff is -1 and takes the empty path.  Widened unsigned it would be 65535,
   the divide would be reached, and (50 * 125 + 65534) / 65535 is 1 -- a single
   lit column.  The MP bar is a normal 32 of 125 so the call is known to have
   run. */
static void hpmp_stat_words_are_read_signed(void)
{
    hpmp_stage();
    hpmp_set_unit(0, 0, 50, -1, 25, 100);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_FRAME_PITCH, 0);

    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN), HPMP_GUARD);
    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN), fill_art(3, 0, 0));
    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN + 31), fill_art(3, 0, 31));
}

/* DEC EDX after ADD EDX,max rounds up: 1 of 1000 is (125 + 999) / 1000 = 1 lit
   column where the truncating 125 / 1000 would be an empty bar.  The rounding
   stops at zero, which the MP pair shows: (0 * 125 + 999) / 1000 is 0 and
   nothing is drawn. */
static void hpmp_one_point_of_current_still_lights_one_column(void)
{
    hpmp_stage();
    hpmp_set_unit(0, 0, 1, 1000, 0, 1000);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_FRAME_PITCH, 0);

    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN), fill_art(2, 0, 0));
    CHECK_EQ(low_left(6, HPMP_LOW_LEFT_BAR_COLUMN), fill_art(2, 4, 0));
    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN + 1), HPMP_GUARD);
    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN), HPMP_GUARD);
}

/* current == max is (77 * 125 + 76) / 77 = 125, the whole span, and 125 is
   still drawn.  One point above the maximum is 127, which fdps_draw_gauge_fill
   drops outright -- so an overfull gauge reads EMPTY and not full.  Adding the
   min(125, width) a rebuilder would reach for turns the MP bar here into a full
   one. */
static void hpmp_full_span_is_drawn_and_an_overfull_one_is_not(void)
{
    hpmp_stage();
    hpmp_set_unit(0, 0, 77, 77, 101, 100);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_FRAME_PITCH, 0);

    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN), fill_art(2, 0, 0));
    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN + 124), fill_art(2, 0, 124));
    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN + 125), HPMP_GUARD);

    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN), HPMP_GUARD);
    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN + 124), HPMP_GUARD);
}

/* The HP bar is fed from +0x40 and +0x42 and the MP bar from +0x44 and +0x46,
   and the two pairs are not interchangeable: a full HP pair with a
   one-of-a-thousand MP pair draws the whole span on row 2 and a single column
   on row 12, and swapping the pairs would swap those two readings. */
static void hpmp_hp_pair_feeds_row_two_and_mp_pair_row_twelve(void)
{
    hpmp_stage();
    hpmp_set_unit(0, 0, 77, 77, 1, 1000);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_FRAME_PITCH, 0);

    CHECK_EQ(low_left(2, HPMP_LOW_LEFT_BAR_COLUMN + 124), fill_art(2, 0, 124));
    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN), fill_art(3, 0, 0));
    CHECK_EQ(low_left(12, HPMP_LOW_LEFT_BAR_COLUMN + 1), HPMP_GUARD);
}

/* dest_stride is the row multiplier for the panel origin AND the pitch handed
   to both callees, so a pitch of 0x180 moves the panel to row 0xc7 of a
   0x180-byte row and steps the fill's own rows by the same 0x180.  Nothing is
   drawn where the 0x170 pitch would have put the panel. */
static void hpmp_dest_stride_places_the_panel_and_pitches_the_blits(void)
{
    int origin;

    hpmp_stage();
    hpmp_set_unit(0, 0, 50, 100, 0, 0);
    fdps_draw_unit_hp_mp_gauges(hpmp_frame, HPMP_ALT_PITCH, 0);

    origin = HPMP_LOW_LEFT_ROW * HPMP_ALT_PITCH + HPMP_LOW_LEFT_COLUMN;
    CHECK_EQ((int) hpmp_frame[origin], HPMP_CEL_PIXEL_BASE + 2);
    CHECK_EQ((int) hpmp_frame[origin + 10 * HPMP_ALT_PITCH],
             HPMP_CEL_PIXEL_BASE + 3);
    CHECK_EQ((int) hpmp_frame[origin + 2 * HPMP_ALT_PITCH
                              + HPMP_LOW_LEFT_BAR_COLUMN],
             fill_art(2, 0, 0));
    CHECK_EQ((int) hpmp_frame[origin + 3 * HPMP_ALT_PITCH
                              + HPMP_LOW_LEFT_BAR_COLUMN],
             fill_art(2, 1, 0));
    CHECK_EQ((int) hpmp_frame[origin + 2 * HPMP_ALT_PITCH
                              + HPMP_LOW_LEFT_BAR_COLUMN + 63],
             HPMP_GUARD);

    CHECK_EQ(low_left(0, 0), HPMP_GUARD);
}

void run_gauge_tests(void)
{
    RUN_TEST(zero_max_draws_an_empty_bar);
    RUN_TEST(negative_max_draws_an_empty_bar);
    RUN_TEST(one_unit_of_current_still_fills_one_column);
    RUN_TEST(zero_current_draws_an_empty_bar);
    RUN_TEST(half_full_rounds_the_odd_column_up);
    RUN_TEST(current_equal_to_max_fills_the_whole_bar);
    RUN_TEST(bar_index_reaches_the_filled_half);
    RUN_TEST(negative_current_draws_an_empty_bar);
    RUN_TEST(current_above_max_is_not_capped);

    RUN_TEST(filled_half_and_track_meet_at_fill_width);
    RUN_TEST(track_comes_from_graphic_zero_whatever_the_index);
    RUN_TEST(graphic_and_row_strides_are_0x3a8_and_0x75);
    RUN_TEST(zero_fill_draws_the_whole_track);
    RUN_TEST(negative_fill_is_clamped_up_to_zero);
    RUN_TEST(full_fill_draws_no_track);
    RUN_TEST(overfull_fill_is_not_capped);
    RUN_TEST(palette_index_zero_is_transparent_in_both_halves);
    RUN_TEST(destination_rows_step_by_dst_stride);

    RUN_TEST(index_two_fills_from_the_left);
    RUN_TEST(index_below_two_shifts_dest_and_strip_together);
    RUN_TEST(the_alignment_split_is_at_index_two);
    RUN_TEST(index_zero_uses_strip_zero_right_aligned);
    RUN_TEST(strip_and_row_strides_are_0x271_and_0x7d);
    RUN_TEST(a_full_span_is_still_drawn);
    RUN_TEST(an_overfull_span_draws_nothing);
    RUN_TEST(a_full_right_aligned_span_does_not_shift);
    RUN_TEST(zero_and_negative_widths_draw_nothing);
    RUN_TEST(palette_index_zero_leaves_the_frame_alone);
    RUN_TEST(dest_stride_is_passed_through_untouched);

    RUN_TEST(stat_gauge_zero_max_draws_nothing);
    RUN_TEST(stat_gauge_negative_max_draws_nothing);
    RUN_TEST(stat_gauge_one_current_still_lights_one_column);
    RUN_TEST(stat_gauge_zero_current_draws_nothing);
    RUN_TEST(stat_gauge_half_full_rounds_the_odd_column_up);
    RUN_TEST(stat_gauge_a_third_rounds_up_to_42);
    RUN_TEST(stat_gauge_current_equal_to_max_fills_the_span);
    RUN_TEST(stat_gauge_current_above_max_draws_nothing);
    RUN_TEST(stat_gauge_negative_current_draws_nothing);
    RUN_TEST(stat_gauge_index_reaches_the_strip);
    RUN_TEST(stat_gauge_index_below_two_fills_from_the_right);
    RUN_TEST(stat_gauge_dest_and_stride_pass_through);

    RUN_TEST(unit_gauge_paints_cap_fill_track_cap);
    RUN_TEST(unit_graphic_and_row_strides_are_0x102_and_0x2b);
    RUN_TEST(unit_remainder_runs_under_the_right_cap);
    RUN_TEST(unit_zero_and_negative_fill_draw_the_same_bar);
    RUN_TEST(unit_overfull_fill_is_not_capped);
    RUN_TEST(unit_blend_mode_reads_the_destination_as_background);
    RUN_TEST(unit_tint_mode_uses_the_mode_value_as_the_colour);
    RUN_TEST(unit_mode_two_is_already_a_tint_colour);
    RUN_TEST(unit_alpha_reaches_both_blended_painters);
    RUN_TEST(unit_palette_index_zero_is_transparent_in_every_mode);
    RUN_TEST(unit_dst_stride_is_passed_through_untouched);

    RUN_TEST(prop_zero_max_draws_an_empty_bar);
    RUN_TEST(prop_negative_max_draws_an_empty_bar);
    RUN_TEST(prop_one_current_still_lights_one_column);
    RUN_TEST(prop_zero_current_draws_an_empty_bar);
    RUN_TEST(prop_half_full_rounds_the_odd_column_up);
    RUN_TEST(prop_a_third_rounds_up_to_14);
    RUN_TEST(prop_current_equal_to_max_fills_the_interior);
    RUN_TEST(prop_current_above_max_smears_past_the_bar);
    RUN_TEST(prop_negative_current_draws_an_empty_bar);
    RUN_TEST(prop_gfx_index_reaches_the_filled_run);
    RUN_TEST(prop_mode_and_alpha_pass_through);
    RUN_TEST(prop_dst_and_stride_pass_through);

    RUN_TEST(unit_record_shape_matches_the_offsets);
    RUN_TEST(anchor_is_the_tile_times_24_plus_4);
    RUN_TEST(scroll_origin_is_subtracted_and_the_index_scales);
    RUN_TEST(facing_1_places_like_facing_0);
    RUN_TEST(right_step_falls_back_at_the_limit_not_before);
    RUN_TEST(up_step_falls_back_at_the_top_margin_not_before);
    RUN_TEST(negative_x_takes_the_signed_branch);
    RUN_TEST(negative_y_takes_the_signed_branch);
    RUN_TEST(tile_bytes_widen_unsigned);
    RUN_TEST(facing_2_goes_down_and_left);
    RUN_TEST(facing_3_and_beyond_place_like_facing_2);
    RUN_TEST(down_step_falls_back_at_the_limit_not_before);
    RUN_TEST(left_step_falls_back_at_the_margin_not_before);

    RUN_TEST(combat_gauges_read_the_measured_offsets);
    RUN_TEST(combat_gauges_return_the_pair_and_suppress_without_a_counter);
    RUN_TEST(combat_gauges_draw_the_defender_bar_from_side_zero);
    RUN_TEST(combat_gauges_side_other_than_zero_takes_graphic_one);
    RUN_TEST(combat_gauges_draw_both_bars_on_a_counter);
    RUN_TEST(combat_gauges_zero_max_hp_draws_an_empty_bar);
    RUN_TEST(combat_gauges_negative_max_hp_draws_an_empty_bar);
    RUN_TEST(combat_gauges_fill_is_the_ceiling_over_41_columns);
    RUN_TEST(combat_gauges_hp_words_are_read_signed);
    RUN_TEST(combat_gauges_present_312x192_at_screen_4_4);
    RUN_TEST(combat_page_comes_back_from_the_heap);
    RUN_TEST(combat_gauges_pace_six_of_the_seven_frames);

    RUN_TEST(hpmp_record_shape_matches_the_offsets);
    RUN_TEST(hpmp_side_two_takes_the_upper_right_panel);
    RUN_TEST(hpmp_side_zero_takes_the_lower_left_panel);
    RUN_TEST(hpmp_any_non_zero_side_takes_the_upper_right_panel);
    RUN_TEST(hpmp_unit_index_selects_the_record);
    RUN_TEST(hpmp_zero_and_negative_max_draw_empty_bars);
    RUN_TEST(hpmp_stat_words_are_read_signed);
    RUN_TEST(hpmp_one_point_of_current_still_lights_one_column);
    RUN_TEST(hpmp_full_span_is_drawn_and_an_overfull_one_is_not);
    RUN_TEST(hpmp_hp_pair_feeds_row_two_and_mp_pair_row_twelve);
    RUN_TEST(hpmp_dest_stride_places_the_panel_and_pitches_the_blits);
}
