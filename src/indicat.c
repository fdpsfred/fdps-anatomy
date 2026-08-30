/* indicat.c -- the battle indicator popups and the queue they are played back
 * from.
 *
 * See indicat.h for what a caller has to know.  The file owns three of the four
 * queue globals; the cursor is gamedata.c's because the item code reads it too.
 *
 * sprintf comes from <stdio.h> and strlen from <string.h>, and both are real
 * calls in the original -- CALL 0x00042d41 and CALL 0x00042dd2 at 0001f5df and
 * 0001f62f.  Watcom 10.0a only turns strlen into an instruction sequence when
 * the intrinsics are asked for, and -oi is not in this build's flag set
 * (rebuild_info/build_flags.md), so the plain declarations are what reproduce
 * the two calls.
 */
#include <stdio.h>
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "indicat.h"

/* One map tile is 24 pixels square, and the view origins are in pixels, so this
   is what turns a scroll position into the tile column and row the screen
   starts at. */
#define INDICATOR_TILE_SIZE 0x18

/* How wide and how tall the cull window is, in tiles, measured from the origin
   tile.  The two are used differently on purpose and the asymmetry is the
   original's: the x test is exclusive at both ends, so the columns kept are
   origin_tx .. origin_tx + 12, while the y test is inclusive at both ends, so
   the rows kept are origin_ty - 1 .. origin_ty + 8. */
#define INDICATOR_VIEW_COLUMNS 0xd
#define INDICATOR_VIEW_LAST_ROW 8

/* A number popup is always four cells wide however few digits it has. */
#define INDICATOR_NUMBER_CELLS 4

/* Where cell i's glyph sits inside the popup: six pixels apart, starting two in.
   The fixed-word popups at 0001f690 and 0001f7d0 start one pixel in instead and
   nudge their second cell, so this spacing belongs to the number popup alone. */
#define INDICATOR_DIGIT_PITCH 6
#define INDICATOR_DIGIT_FIRST_X 2

/* The glyph id that means "draw nothing here"; the player skips such a cell. */
#define INDICATOR_BLANK_GLYPH 0xff

/* The right-alignment countdown's starting value: cells per popup minus one, so
   cell i emits a digit only while the formatted number is longer than
   INDICATOR_NUMBER_CELLS - 1 - i characters. */
#define INDICATOR_ALIGN_COUNTDOWN 3

/* 0001f510.  The digit buffer is a five-byte local seeded with four spaces --
   MOVSD / MOVSB out of the initialiser image at 0001c2cd, which is the shape
   -mf gives a local array's initialiser (rebuild_info/build_flags.md) and not a
   strcpy call.  The spaces never reach the screen: sprintf overwrites them
   before anything reads the buffer, and the cells a short number leaves over
   are blanked by glyph id rather than by a space glyph.

   The cull is four separate compares against the two view origins rather than
   one window computed once, and each divides the origin afresh -- IDIV by 24
   four times over.  Both origins are signed and the division is the signed one,
   so a rebuild that read either as unsigned would divide a negative scroll into
   an enormous positive column and cull every unit on the map.

   strlen is called again for every cell rather than once before the loop, which
   is what the assembly does (CALL at 0001f62f is inside the loop), and it has to
   stay inside it for the right-alignment test to read the buffer that sprintf
   left rather than a length latched before it.

   The four cells are appended at the cursor and the cursor is advanced by four
   only on the path that queued them, so a culled request costs the queue
   nothing. */
void fdps_show_number_indicator(int value, unsigned char glyph_base,
                                int unit_index)
{
    char digits[5] = "    ";
    struct fdps_unit_record *unit;
    int tile_x;
    int tile_y;
    int cell_index;
    unsigned int blank_countdown;
    int digit_cursor;

    blank_countdown = INDICATOR_ALIGN_COUNTDOWN;
    digit_cursor = 0;

    unit = fdps_get_unit_record(unit_index);
    tile_x = unit->pos_x;
    tile_y = unit->pos_y;

    if (data_fdps_battle_view_window_origin_x / INDICATOR_TILE_SIZE - 1
            < tile_x
        && tile_x < data_fdps_battle_view_window_origin_x / INDICATOR_TILE_SIZE
                        + INDICATOR_VIEW_COLUMNS
        && data_fdps_battle_view_window_origin_y / INDICATOR_TILE_SIZE - 1
            <= tile_y
        && tile_y <= data_fdps_battle_view_window_origin_y / INDICATOR_TILE_SIZE
                        + INDICATOR_VIEW_LAST_ROW) {
        sprintf(digits, "%d", value);

        for (cell_index = 0; cell_index < INDICATOR_NUMBER_CELLS;
             cell_index++) {
            data_fdps_indicator_queue_cell_x_offset[
                data_fdps_indicator_queue_count + cell_index] =
                    (unsigned char) (cell_index * INDICATOR_DIGIT_PITCH
                                     + INDICATOR_DIGIT_FIRST_X);
            data_fdps_battle_indicator_queue_unit_idx[
                data_fdps_indicator_queue_count + cell_index] =
                    (unsigned char) unit_index;

            if (strlen(digits) > blank_countdown) {
                data_fdps_indicator_queue_glyph_ids[
                    data_fdps_indicator_queue_count + cell_index] =
                        (unsigned char) (glyph_base + digits[digit_cursor]
                                         - '0');
                digit_cursor++;
            } else {
                data_fdps_indicator_queue_glyph_ids[
                    data_fdps_indicator_queue_count + cell_index] =
                        INDICATOR_BLANK_GLYPH;
            }

            blank_countdown--;
        }

        data_fdps_indicator_queue_count += INDICATOR_NUMBER_CELLS;
    }
}
