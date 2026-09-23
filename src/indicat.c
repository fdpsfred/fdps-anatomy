/* indicat.c -- the battle indicator popups and the queue they are played back
 * from.
 *
 * See indicat.h for what a caller has to know.  The file owns one of the four
 * queue globals, the glyph ids; the cell x offsets, the unit indices and the
 * cursor are gamedata.c's, in the contiguous run the queue's overrun writes
 * through.
 *
 * sprintf comes from <stdio.h> and strlen from <string.h>, and both are real
 * calls in the original -- CALL 0x00042d41 and CALL 0x00042dd2 at 0001f5df and
 * 0001f62f.  Watcom 10.0a only turns strlen into an instruction sequence when
 * the intrinsics are asked for, and -oi is not in this build's flag set
 * (rebuild_info/build_flags.md), so the plain declarations are what reproduce
 * the two calls.  malloc and free come from <stdlib.h> and inp from <conio.h>,
 * and all three are calls in the original too -- CALL 0x0003d375 at 0001f93f
 * and 0001f388, CALL 0x0003d478 at 0001fa17 and 0001f4e5, and CALL 0x0003d4e4
 * at 0001f9b9, 0001f9ca, 0001f487 and 0001f498.  delay comes from <i86.h>,
 * which is where Watcom 10.0a declares it, and it is a call too -- CALL
 * 0x0003d370 at 0001f4f7.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "blit.h"
#include "sprite.h"
#include "mapdraw.h"
#include "indicat.h"

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* 000642b0. Starts all zero; it is initialised only so it lands in _DATA
   directly after data_fdps_battle_indicator_queue_unit_idx, because the
   unbounded queue cursor lets a batch of more than 50 popups write unit
   indices past that array into this one, and this array's own overrun into the
   cursor. */
unsigned char data_fdps_indicator_queue_glyph_ids[INDICATOR_QUEUE_CELLS] = { 0 };

/* End of global data. */

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

/* The VGA input status register and its vertical retrace bit, tested as TEST
   AL,0x8 after each CALL to inp -- at 0001f48f and 0001f4a0 in the queue
   player and at 0001f9c1 and 0001f9d2 in the unit flash. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The mode 13h screen and its row stride.  Both frame presenters in this file
   push an absolute 0xa0504 and a stride of 0x140, so the screen base is named
   here once and the window offset stays with the presenter that uses it. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* How many frames one playback runs for -- CMP dword ptr [EBP-0x10],0x16 / JL
   at 0001f370 -- and how many steps the bounce table has.  The table is copied
   onto the stack as 6 MOVSD plus 1 MOVSB at 0001f359, which is the 25 bytes
   at 0001c2b4 and is the shape -mf gives an initialised local array
   (rebuild_info/build_flags.md), not a memcpy call. */
#define PLAYBACK_FRAMES 0x16
#define PLAYBACK_BOUNCE_STEPS 25

/* How the bounce is staggered across the queue: the phase is the QUEUE index
   modulo four, IDIV EBX with EBX = 4 at 0001f418, and the remainder is added to
   the frame number to index the table.  With the frame at most 21 and the phase
   at most 3 the index reaches exactly 24, the table's last byte, so the table
   is read to its end and never past it.

   The phase is the absolute queue index and NOT the cell's index inside its own
   popup (rebuild_info/pitfalls.md).  fdps_show_sprite_indicator advances the
   cursor by the number of cells it actually wrote rather than by four, so a
   short label leaves the cursor off a multiple of four and every popup queued
   after it bounces out of phase.  Writing the phase as a per-popup cell index
   is tidier and loses that. */
#define PLAYBACK_BOUNCE_PHASES 4

/* Where the popup sits inside the scene page, in pixels, once the unit's tile
   position has been converted and the camera scroll taken off: ADD EAX,0x18 at
   0001f453 on the column and ADD EAX,0x15 at 0001f433 on the row.  The x is the
   scene's own 24-pixel border and the y is three pixels less, which lifts the
   glyph so it floats over the unit rather than standing on it. */
#define PLAYBACK_POPUP_ORIGIN_X 0x18
#define PLAYBACK_POPUP_ORIGIN_Y 0x15

/* The offscreen scene each frame is composed on: 360 by 240 at a 360-byte
   pitch, the PUSH 0x15180 at 0001f383 and the PUSH 0x168 at 0001f457 and
   0001f4b8.  It is the surface fdps_draw_scene_layers hardwires. */
#define PLAYBACK_SCENE_BYTES 0x15180
#define PLAYBACK_SCENE_PITCH 0x168

/* What is presented and where: 312 by 192 taken from scene byte 0x21d8, which
   is scene pixel (24,24), landing at screen byte 0x504, which is screen pixel
   (4,4) -- the immediates of the six pushes at 0001f4a4 through 0001f4c5. */
#define PLAYBACK_SCENE_WINDOW_AT 0x21d8
#define PLAYBACK_WINDOW_AT 0x504
#define PLAYBACK_WINDOW_W 0x138
#define PLAYBACK_WINDOW_H 0xc0

/* How long the finished popup is left standing before the queue is cleared:
   PUSH 0x1f4 / CALL delay at 0001f4f2. */
#define PLAYBACK_TAIL_MS 500

/* 0001f340.  Plays the whole queue back and empties it.  Every cell any of the
   four producers above appended is bounced over its own unit for 22 frames,
   the last frame is left on screen for half a second, and the cursor goes back
   to zero.  Nothing is returned and nothing else happens while it runs: the
   call costs about 22 timer ticks, a little over a second.

   AN EMPTY QUEUE COSTS NOTHING.  CMP dword ptr [0x00064378],0x0 / JZ at
   0001f35c leaves through the epilogue before the first frame, so a caller that
   queued nothing -- every producer culls its request against the view window --
   does not pay the 22 frames or the half second, and the delay is not reached
   either.

   THE BOUNCE TABLE IS READ AT THE QUEUE INDEX MODULO FOUR PLUS THE FRAME.  See
   PLAYBACK_BOUNCE_PHASES above for why that is the queue index and not the
   cell's place in its popup.  The table's values are pixels of DOWNWARD
   displacement, so the run is a rest at 15, a hop 15 pixels up and back, a
   short rest, a smaller hop 7 pixels up and back, and a rest to finish.

   THE SCENE IS ALLOCATED AND FREED INSIDE THE FRAME LOOP, once per frame --
   CALL malloc at 0001f388 and CALL free at 0001f4e5 are both between the loop's
   test and its increment -- and nothing checks what malloc handed back and
   nothing clears it.  With no scene layers loaded fdps_draw_scene_layers writes
   nothing into it, so a frame can show whatever the previous one left in the
   same recycled block.  Hoisting the pair out of the loop is the obvious tidy-up
   and it is a different program: one allocation of an 86,400-byte block instead
   of 22, and a heap that never sees the playback.

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED, the same contract
   fdps_flash_units_in_color above and the whole of anim.c carry.  last_tick is
   read at 0001f4ce before anything has written it, so the first frame waits
   either not at all or a full tick depending on what that stack slot held on
   entry.  Initialising it, to zero or to the current tick, adds a tick to the
   first frame (rebuild_info/pitfalls.md).

   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of that wait: nothing here writes the counter, so a build allowed to
   hoist the load would spin forever.  The two retrace spins read a port and
   cannot be hoisted for the same reason.

   A BLANK CELL IS SKIPPED BY GLYPH ID.  CMP EAX,0xff / JZ at 0001f3cc jumps
   straight to the loop's increment, so a blank cell is not resolved to a unit
   record and nothing is drawn for it -- that is how a short number stays right
   aligned inside its four cells without the popup moving.

   THE QUEUED UNIT INDEX IS A BYTE AND IS WIDENED UNSIGNED.  MOV AL,byte ptr
   [EAX + 0x641e8] / AND EAX,0xff at 0001f3da, so a queued index of 0x82 names
   unit 130 and not unit -126.  It is resolved through fdps_get_unit_record on
   every cell of every frame and never cached, so a popup follows its unit if
   the unit moves or the array is relocated under it.

   TWO CALLS' ANSWERS ARE READ: malloc's, which is the scene page every other
   call in the frame is handed, and fdps_get_unit_record's, whose record bytes 0
   and 1 are the unit's tile position.  fdps_draw_scene_layers,
   fdps_cel_blit_sprite, fdps_blit_rect, free and delay return nothing the
   original looks at, and inp's answer is tested for bit 3 at both spins.

   The frames are paced by the retrace and by the timer tick, so how many
   instructions stand between them is not observable (contract D). */
void fdps_play_indicator_queue(void)
{
    /* Pixels of downward displacement, one per step of the bounce. */
    unsigned char bounce_offset[PLAYBACK_BOUNCE_STEPS] = {
        15, 15, 15, 15,
        7, 3, 1, 0, 0, 1, 3, 7,
        15, 15,
        11, 9, 8, 8, 9, 11,
        15, 15, 15, 15, 15
    };
    /* The 360x240 offscreen scene this frame is composed on. */
    unsigned char *scene_buf;
    /* The record of the unit the cell being drawn floats over. */
    struct fdps_unit_record *unit;
    /* Which of the 22 frames is being drawn. */
    int frame;
    /* Where the walk over the queued cells has got to. */
    int cell_index;
    /* The cell's unit's tile position, read afresh for every cell. */
    int tile_x;
    int tile_y;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    if (data_fdps_indicator_queue_count != 0) {
        for (frame = 0; frame < PLAYBACK_FRAMES; frame++) {
            scene_buf = (unsigned char *) malloc((size_t) PLAYBACK_SCENE_BYTES);
            fdps_draw_scene_layers(scene_buf);

            for (cell_index = 0;
                 cell_index < data_fdps_indicator_queue_count;
                 cell_index++) {
                if (data_fdps_indicator_queue_glyph_ids[cell_index]
                        != INDICATOR_BLANK_GLYPH) {
                    unit = fdps_get_unit_record(
                        (int) data_fdps_battle_indicator_queue_unit_idx[
                                  cell_index]);
                    tile_x = unit->pos_x;
                    tile_y = unit->pos_y;

                    fdps_cel_blit_sprite(
                        data_fdps_number_glyph_sheet_ptr,
                        (int) data_fdps_indicator_queue_glyph_ids[cell_index],
                        scene_buf, PLAYBACK_SCENE_PITCH,
                        tile_x * INDICATOR_TILE_SIZE
                            - data_fdps_battle_view_window_origin_x
                            + (int) data_fdps_indicator_queue_cell_x_offset[
                                        cell_index]
                            + PLAYBACK_POPUP_ORIGIN_X,
                        tile_y * INDICATOR_TILE_SIZE
                            - data_fdps_battle_view_window_origin_y
                            + (int) bounce_offset[
                                        cell_index % PLAYBACK_BOUNCE_PHASES
                                        + frame]
                            + PLAYBACK_POPUP_ORIGIN_Y,
                        0, 0);
                }
            }

            while ((inp(VGA_INPUT_STATUS_1)
                    & VGA_STATUS_VERTICAL_RETRACE) == 0) {
                /* Spin until the retrace begins, so the frame that has just
                   been composed is the one the monitor shows whole. */
            }
            while ((inp(VGA_INPUT_STATUS_1)
                    & VGA_STATUS_VERTICAL_RETRACE) != 0) {
                /* And until it ends, so the blit starts clear of it. */
            }
            fdps_blit_rect(
                (unsigned int) (scene_buf + PLAYBACK_SCENE_WINDOW_AT),
                PLAYBACK_SCENE_PITCH,
                (void *) (VGA_SCREEN_BASE + PLAYBACK_WINDOW_AT),
                VGA_SCREEN_PITCH, PLAYBACK_WINDOW_W, PLAYBACK_WINDOW_H);
            while (last_tick == data_fdps_timer_tick_counter) {
            }
            last_tick = data_fdps_timer_tick_counter;
            free(scene_buf);
        }

        delay((unsigned int) PLAYBACK_TAIL_MS);
        data_fdps_indicator_queue_count = 0;
    }
}

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

/* How many frames the flash runs for, and the cadence the blit mode switches
   on.  MOV EDX,i / SAR EDX,0x1f / SUB EAX,EDX / SAR EAX,0x1 at 0001f959 is the
   compiler's SIGNED divide of the frame index by two and TEST AL,0x1 at
   0001f963 takes bit 0 of it, so the schedule is two frames of one mode then
   two of the other, twice over: 0, 1, 4 and 5 one way and 2, 3, 6 and 7 the
   other. */
#define FLASH_FRAMES 8
#define FLASH_MODE_PERIOD 2

/* The two fdps_blit_dispatch kernels the cadence picks between (blit.h).  Mode
   0 is the plain passthrough, which redraws each listed sprite over the
   identical pixels fdps_draw_scene_layers has already painted there and so
   changes nothing visible; mode 3 is the recolour kernel, which paints every
   pixel of the sprite one palette index.  The listed units therefore go flat
   coloured on two stretches of two frames each and look like themselves in
   between, which is the flash. */
#define FLASH_MODE_PASSTHROUGH 0
#define FLASH_MODE_RECOLOR 3

/* Where the palette index has to sit in the recolour kernel's operand: bits
   8..15 are its colour base, bits 0..7 the tint offset and bits 16..23 the band
   mask, and a painted pixel is ((source + offset) & mask) + base (rlecolor.h).
   SHL dword ptr [EBP+0x1c],0x8 at 0001f91c shifts the argument slot itself,
   once, before the loop, which leaves the offset and the mask 0 and so collapses
   every pixel to the colour.  Passing the colour unshifted lands it in the tint
   offset instead, where the zero mask throws it away and every pixel comes out
   0 (rebuild_info/pitfalls.md). */
#define FLASH_COLOR_BASE_SHIFT 8

/* The offscreen scene the frame is composed on: 360 by 240 at a 360-byte pitch,
   the PUSH 0x15180 at 0001f93a and the PUSH 0x168 at 0001f9ea, and the same
   surface fdps_draw_scene_layers and fdps_blit_unit_sprite both hardwire. */
#define FLASH_SCENE_BYTES 0x15180
#define FLASH_SCENE_PITCH 0x168

/* What is presented and where.  312 by 192 taken from scene byte 0x21d8, which
   is scene pixel (24,24), and landing at screen byte 0x504, which is screen
   pixel (4,4) of the mode 13h page: the immediates of the six pushes at
   0001f9d6 through 0001f9f7. */
#define FLASH_SCENE_WINDOW_AT 0x21d8
#define FLASH_WINDOW_AT 0x504
#define FLASH_WINDOW_W 0x138
#define FLASH_WINDOW_H 0xc0

/* 0001f910.  Flashes a list of battle units in one palette colour so the player
   can see which units an effect has just been applied to.  Eight frames, each
   one whole scene composed offscreen and presented, each paced by one change of
   the timer tick, and nothing is returned.

   THE SCENE IS ALLOCATED AND FREED INSIDE THE LOOP, once per frame -- CALL
   malloc at 0001f93f and CALL free at 0001fa17 are both between the loop's
   test and its increment -- and nothing clears what comes back.  With no scene
   layers active fdps_draw_scene_layers writes nothing into it, so a frame can
   show whatever the previous one left in the same recycled block.  Hoisting the
   pair out of the loop is the obvious tidy-up and it is a different program:
   one allocation of a 86,400-byte block instead of eight, and a heap that no
   longer sees the flash at all.

   THE COLOUR IS SHIFTED ONCE, OVER THE ARGUMENT ITSELF, before the first frame
   -- see FLASH_COLOR_BASE_SHIFT above for what the shift is for.

   THE UNIT INDICES ARE BYTES AND ARE WIDENED UNSIGNED.  MOV AL,byte ptr [EAX] /
   AND EAX,0xff at 0001f99e steps the list one byte at a time and zero extends,
   so a list entry of 0x82 selects unit 130 and not unit -126.  The index is
   handed straight to fdps_blit_unit_sprite, which resolves it through
   fdps_get_unit_record without a range check and drops a unit whose sprite
   origin lies outside the visible scene, so a list entry that names nothing
   visible simply draws nothing.

   A COUNT OF ZERO IS LEGAL and turns the call into an eight-tick pause with the
   scene redrawn under it: the inner loop's CMP EAX,[EBP+0x14] / JL at 0001f981
   fails at once and everything else in the frame still runs.

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED, the same contract
   fdps_play_attack_animation and the rest of anim.c carry.  last_tick is read
   at 0001fa00 before anything has written it, so the first frame waits either
   not at all or a full tick depending on what that stack slot held on entry.
   Initialising it, or latching into data_fdps_view_frame_last_tick the way
   fdps_render_view_frame does, changes the pacing of the first flash frame, and
   the second of those would also perturb the next fdps_render_view_frame call
   (rebuild_info/pitfalls.md).

   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of that wait: nothing inside this function writes the counter, so a
   build allowed to hoist the load would spin here forever.  The two retrace
   spins read a port and cannot be hoisted for the same reason.

   ONE CALL'S ANSWER IS READ: malloc's, which is the scene and is what every
   other call in the frame is handed.  fdps_draw_scene_layers,
   fdps_blit_unit_sprite, fdps_blit_rect and free return nothing the original
   looks at, and inp's answer is tested for bit 3 at both spins.

   The frames are paced by the retrace and by the timer tick, so how many
   instructions stand between them is not observable (contract D). */
void fdps_flash_units_in_color(int unit_count, unsigned char *unit_indices,
                               unsigned int flash_color)
{
    /* The 360x240 offscreen scene this frame is composed on. */
    unsigned char *scene_buf;
    /* Which of the eight frames is being drawn. */
    int frame;
    /* Where the walk over the caller's index list has got to. */
    int list_position;
    /* Which fdps_blit_dispatch kernel this frame's sprites go through. */
    int blit_mode;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    flash_color <<= FLASH_COLOR_BASE_SHIFT;

    for (frame = 0; frame < FLASH_FRAMES; frame++) {
        scene_buf = (unsigned char *) malloc((size_t) FLASH_SCENE_BYTES);
        fdps_draw_scene_layers(scene_buf);

        if (((frame / FLASH_MODE_PERIOD) & 1) != 0) {
            blit_mode = FLASH_MODE_RECOLOR;
        } else {
            blit_mode = FLASH_MODE_PASSTHROUGH;
        }

        for (list_position = 0; list_position < unit_count; list_position++) {
            fdps_blit_unit_sprite(scene_buf,
                                  (int) unit_indices[list_position],
                                  flash_color, blit_mode);
        }

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the blit starts clear of it. */
        }
        fdps_blit_rect((unsigned int) (scene_buf + FLASH_SCENE_WINDOW_AT),
                       FLASH_SCENE_PITCH,
                       (void *) (VGA_SCREEN_BASE + FLASH_WINDOW_AT),
                       VGA_SCREEN_PITCH, FLASH_WINDOW_W, FLASH_WINDOW_H);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
        free(scene_buf);
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
