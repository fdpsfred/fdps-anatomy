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
   nudge is positional and not kerning: the branch is on the cell index, and the
   CURE popup at 0001f7d0 is the same code with another word in it, so the same
   second pixel lands on the narrow I of MISS and on the U of CURE alike.
   Writing the uniform i * 6 + 1 for all four cells compiles and looks right and
   moves that cell one pixel left of where the original puts it, and writing it
   as a per-letter rule stops nudging CURE at all
   (rebuild_info/pitfalls.md). */
#define INDICATOR_WORD_NUDGED_CELL 1
#define INDICATOR_WORD_NUDGED_X 2

/* The Number.cel glyph ids the MISS popup queues, from the four-byte initialiser
   image at 0001c2d2.  They are ids into that sheet and not characters, even
   though the values happen to be the ASCII digits 4, 5 and 6. */
#define MISS_GLYPH_M 0x34
#define MISS_GLYPH_I 0x35
#define MISS_GLYPH_S 0x36

/* The Number.cel glyph ids the CURE popup queues, from the four-byte
   initialiser image at 0001c2d6 -- the four bytes that follow MISS's own.  Like
   MISS's they are ids into that sheet and not characters, even though the
   values happen to be the ASCII 7, 8, 9 and colon. */
#define CURE_GLYPH_C 0x37
#define CURE_GLYPH_U 0x38
#define CURE_GLYPH_R 0x39
#define CURE_GLYPH_E 0x3a

/* The glyph id that means "draw nothing here"; the player skips such a cell. */
#define INDICATOR_BLANK_GLYPH 0xff

/* What marks an unused cell in the four-byte label a caller hands to
   fdps_show_sprite_indicator: id 0, which is NOT the queue's own blank marker
   INDICATOR_BLANK_GLYPH.  A cell with this id is left out of the queue
   entirely rather than being queued blank. */
#define INDICATOR_SPRITE_UNUSED_ID 0

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

/* 0001f7d0.  The CURE popup.  Instruction for instruction the same body as
   fdps_show_miss_indicator above with one difference, the dword of glyph ids it
   starts from: MOV EAX,[0x0001c2d6] at 0001f7dc against MOV EAX,[0x0001c2d2] at
   0001f69c, four bytes further into the same initialiser image.  Everything
   else -- the four cull compares each with its own IDIV by 24 (0001f81b,
   0001f836, 0001f855, 0001f872), the cell 1 branch, the three stores per cell
   and the ADD [0x00064378],0x4 -- is the same code at the same shape, so the
   two are kept as two functions here rather than folded into one helper taking
   the glyph ids: the image really does hold both bodies, and the caller-supplied
   variant it would collapse into already exists as fdps_show_sprite_indicator.

   The cull is the same asymmetric window (rebuild_info/pitfalls.md): x
   exclusive at both ends, y inclusive at both, both origins signed and divided
   by 24 with the signed divide.  A culled request queues nothing and leaves the
   cursor where it was.

   The cell x offsets are MISS's, 1, 8, 13 and 19, because the nudge is on the
   cell index and not on the letter -- here the extra pixel lands on the U. */
void fdps_show_cure_indicator(int unit_index)
{
    unsigned char glyph_ids[INDICATOR_WORD_CELLS] = {
        CURE_GLYPH_C, CURE_GLYPH_U, CURE_GLYPH_R, CURE_GLYPH_E
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

/* 0001fc00.  The caller-supplied popup: the one producer of this family whose
   word is an argument rather than a constant.  sprite_ids points at up to four
   Number.cel glyph ids and each non-zero one is appended to the shared queue as
   its own cell; the only shipped caller, fdps_cast_spell_on_targets, passes one
   four-byte row of a three-row label table (0x00027668: 3b 3c 3c 00, 3d 3e 3f
   00, 3d 3e 40 00) that spells Att, Def and Dex -- the three buff slots
   fdps_unit_apply_status_effect writes to record bytes +0x22, +0x23 and +0x24.
   Nothing in this body depends on that; the ids are copied unread.

   Two things here are NOT what the sibling producers do and both are load
   bearing (rebuild_info/pitfalls.md).

   First, a zero id SKIPS its cell and the loop carries straight on to the next
   one -- JZ at 0001fcd3 jumps to the increment at 0001fcc2, not out of the loop
   -- and the skipped cell is not queued blank either.  Queueing it with
   INDICATOR_BLANK_GLYPH instead would put a cell in the queue that the player
   then skips at draw time, which looks the same on screen but moves every
   later popup along by one cell.

   Second, the cursor is advanced by the number of cells actually written and
   not by four (MOV EAX,[EBP-0x4] / ADD [0x00064378],EAX at 0001fd3e), while the
   cells themselves are stored at cursor + cell_index -- the loop counter.  The
   two only agree while the non-zero ids form a prefix of the label, which is
   true of every row of the shipped table but is not enforced here.  Writing the
   siblings' += 4 leaves a stale cell inside the drawn range for a three-id
   label; writing the stores at cursor + cells_written instead would pack the
   cells and change where a hypothetical gapped label lands.

   Everything else is the fixed-word popup's code: the same four cull compares,
   each with its own signed IDIV by 24 (0001fc4a, 0001fc65, 0001fc84, 0001fca1),
   x exclusive at both ends and y inclusive at both; and the same cell x offsets
   1, 8, 13, 19, with cell 1 alone nudged by the branch at 0001fcd5.  A culled
   request writes nothing and leaves the cursor where it was. */
void fdps_show_sprite_indicator(int unit_index, unsigned char *sprite_ids)
{
    struct fdps_unit_record *unit;
    int tile_x;
    int tile_y;
    int cell_index;
    int cells_written;

    cells_written = 0;

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
            if (sprite_ids[cell_index] != INDICATOR_SPRITE_UNUSED_ID) {
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
                        sprite_ids[cell_index];
                cells_written++;
            }
        }

        data_fdps_indicator_queue_count += cells_written;
    }
}
