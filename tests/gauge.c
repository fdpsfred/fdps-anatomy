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

void run_gauge_tests(void)
{
    RUN_TEST(filled_half_and_track_meet_at_fill_width);
    RUN_TEST(track_comes_from_graphic_zero_whatever_the_index);
    RUN_TEST(graphic_and_row_strides_are_0x3a8_and_0x75);
    RUN_TEST(zero_fill_draws_the_whole_track);
    RUN_TEST(negative_fill_is_clamped_up_to_zero);
    RUN_TEST(full_fill_draws_no_track);
    RUN_TEST(overfull_fill_is_not_capped);
    RUN_TEST(palette_index_zero_is_transparent_in_both_halves);
    RUN_TEST(destination_rows_step_by_dst_stride);
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
