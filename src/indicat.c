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

/* A fixed-word popup is four cells wide too, one glyph per cell, and its cells
   sit six pixels apart starting ONE pixel in rather than two. */
#define INDICATOR_WORD_CELLS 4
#define INDICATOR_WORD_PITCH 6
#define INDICATOR_WORD_FIRST_X 1

/* Cell 1 alone is pushed a second pixel right, by a branch of its own.  The
   glyphs are drawn from a proportional sheet: the M of MISS is the full six
   pixels wide while the I is narrow, so without the nudge the I would sit
   against the M.  Writing the uniform i * 6 + 1 for all four cells compiles and
   looks right and moves the I one pixel left of where the original puts it
   (rebuild_info/pitfalls.md). */
#define INDICATOR_WORD_NUDGED_CELL 1
#define INDICATOR_WORD_NUDGED_X 2

/* The Number.cel glyph ids the MISS popup queues, from the four-byte initialiser
   image at 0001c2d2.  They are ids into that sheet and not characters, even
   though the values happen to be the ASCII digits 4, 5 and 6. */
#define MISS_GLYPH_M 0x34
#define MISS_GLYPH_I 0x35
#define MISS_GLYPH_S 0x36

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

/* 0001f690.  The MISS popup, four cells of one fixed word, appended to the same
   shared queue fdps_show_number_indicator appends its digits to.  Nothing is
   drawn here; fdps_play_indicator_queue drains the queue later.

   The four glyph ids are copied into a stack buffer before anything else --
   MOV EAX,[0x0001c2d2] / MOV [EBP-0x4],EAX at 0001f69c, one dword move out of
   the initialiser image, which is what a four-byte initialised local array
   compiles to and not a memcpy call.  They are then read back one cell at a
   time out of that buffer rather than being written as four constants, which is
   why the buffer is here at all.

   The cull is the same asymmetric window fdps_show_number_indicator uses and it
   is spelled out afresh here rather than shared: four separate compares against
   the two view origins, each with its own IDIV by 24 (0001f6cd, 0001f6e8,
   0001f707, 0001f724).  x is exclusive at both ends and y is inclusive at both,
   so a unit one row off the top of the view still gets its MISS and one a
   column off the left does not.  Both origins are signed and the division is
   the signed one; reading either as unsigned would turn a negative scroll into
   an enormous positive column and cull every unit on the map.

   The cells are appended at the cursor and the cursor is advanced by four only
   on the path that queued them, so a culled request costs the queue nothing. */
void fdps_show_miss_indicator(int unit_index)
{
    unsigned char glyph_ids[INDICATOR_WORD_CELLS] = {
        MISS_GLYPH_M, MISS_GLYPH_I, MISS_GLYPH_S, MISS_GLYPH_S
    };
    struct fdps_unit_record *unit;
    int tile_x;
    int tile_y;
    int cell_index;

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
        for (cell_index = 0; cell_index < INDICATOR_WORD_CELLS; cell_index++) {
            if (cell_index == INDICATOR_WORD_NUDGED_CELL) {
                data_fdps_indicator_queue_cell_x_offset[
                    data_fdps_indicator_queue_count + cell_index] =
                        (unsigned char) (cell_index * INDICATOR_WORD_PITCH
                                         + INDICATOR_WORD_NUDGED_X);
            } else {
                data_fdps_indicator_queue_cell_x_offset[
                    data_fdps_indicator_queue_count + cell_index] =
                        (unsigned char) (cell_index * INDICATOR_WORD_PITCH
                                         + INDICATOR_WORD_FIRST_X);
            }

            data_fdps_battle_indicator_queue_unit_idx[
                data_fdps_indicator_queue_count + cell_index] =
                    (unsigned char) unit_index;
            data_fdps_indicator_queue_glyph_ids[
                data_fdps_indicator_queue_count + cell_index] =
                    glyph_ids[cell_index];
        }

        data_fdps_indicator_queue_count += INDICATOR_WORD_CELLS;
    }
}
