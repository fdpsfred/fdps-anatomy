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
}
