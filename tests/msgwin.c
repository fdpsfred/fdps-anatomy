/* tests/msgwin.c -- cover for src/msgwin.c.
 *
 * Two subjects: fdps_load_and_draw_portrait at 000177d0 and
 * fdps_message_window_wait_key at 000203d0.  The notes below belong to the
 * first; the second has its own banner further down.
 *
 * These read the real FACE.CEL, staged through tests/gamefile.lst, and they
 * have to.  The routine formats no name and takes no file argument -- the
 * sheet's name is a literal inside it -- so there is nothing to point at a
 * smaller fixture, and a sheet it cannot open ends the process at exit(1)
 * rather than failing a check.  Every case below therefore asks first whether
 * the sheet is next to the executable and skips itself when it is not.
 *
 * Expected values come from two places and neither of them is the emitted C.
 * The file addressing comes from the assembly: PUSH 0x0 in front of both
 * fseeks for SEEK_SET, LEA EAX,[EAX*0x4 + 0x0] / ADD EAX,0xf at 00017841 for
 * the directory's base and stride, PUSH 0x8 / PUSH 0x1 at 0001785c for the
 * eight bytes read there, MOV EAX,[EBP + -0xc] / SUB EAX,[EBP + -0x10] at
 * 0001786c for the record's length, and PUSH 0x7d / PUSH 0x64 at 000178c6 and
 * 000178c4 for the blit rectangle.  The numbers those are checked against are
 * read back out of FACE.CEL here, independently.
 *
 * The three decoded pixel values below come from the stream format rle.h
 * documents, applied by hand to FACE.CEL's record 0: its rows 0..3 are wholly
 * transparent, its last row ends in a run of palette index 0x2b, and the pixel
 * at row 50, column 62 is 0x2e.  A reader that loaded the wrong record, sized
 * it from the wrong pair, or blitted it at any width, height or pitch but the
 * ones the assembly pushes puts different bytes at those three addresses.
 *
 * The not-found path is not exercised: it ends in exit(1), so a test of it
 * would take the whole run with it.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "mapdraw.h"
#include "msgwin.h"

/* 8.3, and the name the routine itself holds. */
#define SHEET_NAME "FACE.CEL"

/* struct fdps_cel_header is fifteen bytes and the u32 directory starts right
   after it -- the ADD EAX,0xf the index is scaled into. */
#define SHEET_TABLE_START 15

/* Records FACE.CEL declares, and so the directory entries that close them. */
#define SHEET_RECORDS 160

/* The sheet's own sprite size, checked against the literals the blit is handed
   so that the two are known to agree. */
#define PORTRAIT_WIDTH 125
#define PORTRAIT_HEIGHT 100

/* Records picked apart from each other: 0 is the first, so its directory entry
   is the first thing after the header and pins the 0x0f base on its own; 5 is
   far enough along that a stride of anything but four lands elsewhere; 37 is
   the shortest record on the sheet at 200 bytes and decodes to nothing at all,
   which is what makes it worth loading. */
#define RECORD_A 0
#define RECORD_B 5
#define RECORD_C 37

/* Bytes compared at each end of a loaded record.  Both ends are checked
   because one end alone does not separate a wrong offset from a wrong length:
   the shortest record on the sheet is 200 bytes, so 64 stays inside every
   record and is long enough that two of them cannot agree by accident. */
#define RECORD_PROBE_BYTES 64

/* The surface the portrait is drawn into: a full mode 13h page, so a blit that
   ran past the rectangle at the pitch the caller gave has somewhere to land
   where it can be seen. */
#define SCREEN_PITCH 320
#define SCREEN_ROWS 200
#define SCREEN_BYTES (SCREEN_PITCH * SCREEN_ROWS)

/* A second pitch, narrower than the screen and wider than the portrait, so
   that a body which drew at a hard-coded 0x140 instead of forwarding its
   argument puts the same pixels at different addresses. */
#define NARROW_PITCH 160

/* A byte no portrait record decodes to here, so "untouched" is visible. */
#define SENTINEL 0xaa

/* Three pixels of record 0 decoded by hand from the stream format, and the
   coordinates they sit at.  The last-row, last-column one is the load-bearing
   pair: it is written only if the rectangle really is 125 wide and 100 tall
   and the rows really are dest_pitch apart. */
#define PIXEL_MID_ROW 50
#define PIXEL_MID_COL 62
#define PIXEL_MID_VALUE 0x2e
#define PIXEL_LAST_ROW 99
#define PIXEL_LAST_COL 124
#define PIXEL_LAST_VALUE 0x2b

/* Rows 0..3 of record 0 are wholly transparent, so row 0 column 0 stays as it
   was and a body that filled the rectangle would say so. */
#define PIXEL_CLEAR_ROW 0
#define PIXEL_CLEAR_COL 0

/* An offset given to the routine as part of dest, to show it adds no origin of
   its own: three rows down and seven columns in. */
#define DRAW_OFFSET_ROWS 3
#define DRAW_OFFSET_COLS 7

/* Comfortably more loads than the CRT has file handles for (_NFILES is 20), so
   a body that leaked its handle runs out during the loop. */
#define REPEATED_LOADS 24

static unsigned char screen[SCREEN_BYTES];
static int sheet_checked = 0;
static int sheet_ready = 0;

/* Reads one little-endian u32 out of the sheet at an absolute file offset,
   byte by byte so the reader does not depend on the host's own layout. */
static long sheet_dword(long file_offset)
{
    FILE *fp;
    unsigned char raw[4];
    long value;

    fp = fopen(SHEET_NAME, "rb");
    if (fp == NULL) {
        return -1;
    }
    fseek(fp, file_offset, SEEK_SET);
    if (fread(raw, 1, 4, fp) != 4) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    value = (long) raw[0] | ((long) raw[1] << 8) | ((long) raw[2] << 16)
            | ((long) raw[3] << 24);
    return value;
}

/* The directory entry for a record: base 0x0f plus four bytes per index. */
static long record_start(int index)
{
    return sheet_dword(SHEET_TABLE_START + (long) index * 4);
}

static long record_length(int index)
{
    return record_start(index + 1) - record_start(index);
}

/* Whether the staged sheet is there and declares the sprite size the blit is
   handed.  Every case asks first: nothing in the routine tests a result it
   gets back, so a wrong or short sheet does not fail a check, it loads
   rubbish. */
static int sheet_present(void)
{
    FILE *fp;
    unsigned char header[SHEET_TABLE_START];
    long size;

    if (sheet_checked) {
        return sheet_ready;
    }
    sheet_checked = 1;
    sheet_ready = 0;
    fp = fopen(SHEET_NAME, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (fread(header, 1, SHEET_TABLE_START, fp) != SHEET_TABLE_START) {
        fclose(fp);
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    if (header[0] != 'C' || header[1] != 'E' || header[2] != 'L') {
        return 0;
    }
    if (header[7] != PORTRAIT_WIDTH || header[9] != PORTRAIT_HEIGHT) {
        return 0;
    }
    if (size < (long) (SHEET_TABLE_START + (SHEET_RECORDS + 1) * 4)) {
        return 0;
    }
    sheet_ready = 1;
    return 1;
}

/* How many bytes of a loaded record match the sheet, starting at a byte offset
   into the record and comparing at most count of them. */
static int record_probe_matches(int index, long into_record, int count)
{
    FILE *fp;
    unsigned char raw;
    int matched;
    int i;

    if (data_fdps_portrait_sprite_buf_ptr == NULL) {
        return -1;
    }
    fp = fopen(SHEET_NAME, "rb");
    if (fp == NULL) {
        return -1;
    }
    fseek(fp, record_start(index) + into_record, SEEK_SET);
    matched = 0;
    for (i = 0; i < count; i++) {
        if (fread(&raw, 1, 1, fp) != 1) {
            break;
        }
        if (raw != data_fdps_portrait_sprite_buf_ptr[into_record + i]) {
            break;
        }
        matched++;
    }
    fclose(fp);
    return matched;
}

static void screen_fill_sentinel(void)
{
    memset(screen, SENTINEL, SCREEN_BYTES);
}

/* Bytes of the page that are no longer the sentinel, counted over a window of
   rows and columns.  The blit's rectangle is pinned by asking this about the
   bands outside it. */
static long screen_changed(int first_row, int rows, int first_col, int cols,
                           int pitch)
{
    long changed;
    int row;
    int col;

    changed = 0;
    for (row = first_row; row < first_row + rows; row++) {
        for (col = first_col; col < first_col + cols; col++) {
            if (screen[(long) row * pitch + col] != SENTINEL) {
                changed++;
            }
        }
    }
    return changed;
}

/* Puts the global back the way a fresh process has it, through the routine's
   own release path rather than by hand: -1 frees whatever is live and nulls
   the slot. */
static void portrait_release(void)
{
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, -1);
}

/* ---------------------------------------------------------------------- */

/* The record the directory names is the one that ends up in the buffer, at
   both ends of it: a body that seeked to any base but 0x0f, scaled the index
   by anything but four, or took its length from the wrong pair of entries
   lands on other bytes of the sheet. */
static void loads_the_record_the_directory_names(void)
{
    long length;

    if (!sheet_present()) {
        return;
    }
    screen_fill_sentinel();
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, RECORD_A);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr != NULL, 1);
    length = record_length(RECORD_A);
    CHECK_EQ(record_probe_matches(RECORD_A, 0, RECORD_PROBE_BYTES),
             RECORD_PROBE_BYTES);
    CHECK_EQ(record_probe_matches(RECORD_A, length - RECORD_PROBE_BYTES,
                                  RECORD_PROBE_BYTES),
             RECORD_PROBE_BYTES);
    portrait_release();
}

/* The same for a record further along the directory, where a stride of
   anything but four has moved far enough to be unmistakable. */
static void loads_a_record_further_along_the_directory(void)
{
    long length;

    if (!sheet_present()) {
        return;
    }
    screen_fill_sentinel();
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, RECORD_B);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr != NULL, 1);
    length = record_length(RECORD_B);
    CHECK_EQ(record_probe_matches(RECORD_B, 0, RECORD_PROBE_BYTES),
             RECORD_PROBE_BYTES);
    CHECK_EQ(record_probe_matches(RECORD_B, length - RECORD_PROBE_BYTES,
                                  RECORD_PROBE_BYTES),
             RECORD_PROBE_BYTES);
    portrait_release();
}

/* Record 37 is 200 bytes of nothing but skip commands, so it decodes to no
   written pixel at all.  It still has to be loaded: the load is not
   conditional on the drawing, and the buffer it leaves behind is what a later
   repaint reads. */
static void loads_a_record_that_draws_nothing(void)
{
    if (!sheet_present()) {
        return;
    }
    screen_fill_sentinel();
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, RECORD_C);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr != NULL, 1);
    CHECK_EQ(record_probe_matches(RECORD_C, 0, RECORD_PROBE_BYTES),
             RECORD_PROBE_BYTES);
    CHECK_EQ(screen_changed(0, SCREEN_ROWS, 0, SCREEN_PITCH, SCREEN_PITCH), 0);
    portrait_release();
}

/* The buffer is still allocated at the return and holds the record, which is
   the whole reason the message window can repaint without reopening the file.
   A body that freed it after the blit leaves a null here. */
static void leaves_the_record_allocated_after_drawing(void)
{
    if (!sheet_present()) {
        return;
    }
    screen_fill_sentinel();
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, RECORD_A);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr != NULL, 1);
    CHECK_EQ(record_probe_matches(RECORD_A, 0, RECORD_PROBE_BYTES),
             RECORD_PROBE_BYTES);
    portrait_release();
}

/* An index of -1 releases the buffer and returns before the fopen: the slot is
   null afterwards and nothing was drawn.  It is the only way a caller clears
   the portrait. */
static void index_minus_one_releases_and_draws_nothing(void)
{
    if (!sheet_present()) {
        return;
    }
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, RECORD_A);
    screen_fill_sentinel();
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, -1);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr == NULL, 1);
    CHECK_EQ(screen_changed(0, SCREEN_ROWS, 0, SCREEN_PITCH, SCREEN_PITCH), 0);
}

/* -1 on an already empty slot is not an error either: the null store sits
   outside the free's guard, so the slot is null whether or not there was
   anything to release. */
static void index_minus_one_on_an_empty_slot_is_harmless(void)
{
    if (!sheet_present()) {
        return;
    }
    portrait_release();
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr == NULL, 1);
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, -1);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr == NULL, 1);
}

/* At most one portrait is ever resident: the free and the null store at the
   top run on every call, so the second load replaces the first and what is in
   the buffer afterwards is the second record end to end.  Every record on this
   sheet opens with the same transparent-row commands, so the tail is what
   separates them. */
static void the_next_load_replaces_the_last(void)
{
    long length;

    if (!sheet_present()) {
        return;
    }
    screen_fill_sentinel();
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, RECORD_A);
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, RECORD_B);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr != NULL, 1);
    length = record_length(RECORD_B);
    CHECK_EQ(record_probe_matches(RECORD_B, 0, RECORD_PROBE_BYTES),
             RECORD_PROBE_BYTES);
    CHECK_EQ(record_probe_matches(RECORD_B, length - RECORD_PROBE_BYTES,
                                  RECORD_PROBE_BYTES),
             RECORD_PROBE_BYTES);
    portrait_release();
}

/* CALL fclose at 000178b4 gives the handle back on every load, so the window
   can be reopened as often as the player likes.  A body that leaked it does
   not fail a check, it takes the run down at its own fopen past _NFILES; what
   survives to be asserted is that the sheet can still be opened afterwards. */
static void closes_the_sheet_each_time(void)
{
    FILE *probe;
    int load;

    if (!sheet_present()) {
        return;
    }
    for (load = 0; load < REPEATED_LOADS; load++) {
        fdps_load_and_draw_portrait(screen, SCREEN_PITCH, RECORD_A);
    }
    probe = fopen(SHEET_NAME, "rb");
    CHECK_EQ(probe != NULL, 1);
    if (probe != NULL) {
        fclose(probe);
    }
    portrait_release();
}

/* The blit rectangle is 125 x 100 at the caller's pitch and nothing outside it
   is touched.  The three decoded pixels pin where the content lands; the two
   bands pin where it stops.  A height of anything but 100 leaves the last row
   unwritten or reaches row 100; a width of anything but 125 misses the last
   column or spills past it. */
static void blits_the_sheets_rectangle_at_the_callers_pitch(void)
{
    if (!sheet_present()) {
        return;
    }
    screen_fill_sentinel();
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, RECORD_A);
    CHECK_EQ(screen[(long) PIXEL_MID_ROW * SCREEN_PITCH + PIXEL_MID_COL],
             PIXEL_MID_VALUE);
    CHECK_EQ(screen[(long) PIXEL_LAST_ROW * SCREEN_PITCH + PIXEL_LAST_COL],
             PIXEL_LAST_VALUE);
    CHECK_EQ(screen[(long) PIXEL_CLEAR_ROW * SCREEN_PITCH + PIXEL_CLEAR_COL],
             SENTINEL);
    CHECK_EQ(screen_changed(0, PORTRAIT_HEIGHT, PORTRAIT_WIDTH,
                            SCREEN_PITCH - PORTRAIT_WIDTH, SCREEN_PITCH), 0);
    CHECK_EQ(screen_changed(PORTRAIT_HEIGHT, SCREEN_ROWS - PORTRAIT_HEIGHT, 0,
                            SCREEN_PITCH, SCREEN_PITCH), 0);
    portrait_release();
}

/* dest_pitch is forwarded and not assumed: at a pitch of 160 the same decoded
   pixels land 160 bytes per row apart, and the band to the right of the
   portrait is still untouched. */
static void forwards_the_pitch_it_was_given(void)
{
    if (!sheet_present()) {
        return;
    }
    screen_fill_sentinel();
    fdps_load_and_draw_portrait(screen, NARROW_PITCH, RECORD_A);
    CHECK_EQ(screen[(long) PIXEL_MID_ROW * NARROW_PITCH + PIXEL_MID_COL],
             PIXEL_MID_VALUE);
    CHECK_EQ(screen[(long) PIXEL_LAST_ROW * NARROW_PITCH + PIXEL_LAST_COL],
             PIXEL_LAST_VALUE);
    CHECK_EQ(screen_changed(0, PORTRAIT_HEIGHT, PORTRAIT_WIDTH,
                            NARROW_PITCH - PORTRAIT_WIDTH, NARROW_PITCH), 0);
    CHECK_EQ(screen_changed(PORTRAIT_HEIGHT,
                            SCREEN_BYTES / NARROW_PITCH - PORTRAIT_HEIGHT, 0,
                            NARROW_PITCH, NARROW_PITCH), 0);
    portrait_release();
}

/* dest is the rectangle's top-left pixel and no origin is added to it: the
   callers have already done that -- fdps_draw_unit_status_panel with + 0x513,
   the two window callers with the screen address 0xa708c.  Drawn three rows
   down and seven columns in, every pixel moves by exactly that much. */
static void draws_at_the_address_it_was_handed(void)
{
    long origin;

    if (!sheet_present()) {
        return;
    }
    screen_fill_sentinel();
    origin = (long) DRAW_OFFSET_ROWS * SCREEN_PITCH + DRAW_OFFSET_COLS;
    fdps_load_and_draw_portrait(screen + origin, SCREEN_PITCH, RECORD_A);
    CHECK_EQ(screen[origin + (long) PIXEL_MID_ROW * SCREEN_PITCH
                    + PIXEL_MID_COL],
             PIXEL_MID_VALUE);
    CHECK_EQ(screen[origin + (long) PIXEL_LAST_ROW * SCREEN_PITCH
                    + PIXEL_LAST_COL],
             PIXEL_LAST_VALUE);
    CHECK_EQ(screen_changed(0, DRAW_OFFSET_ROWS, 0, SCREEN_PITCH,
                            SCREEN_PITCH), 0);
    CHECK_EQ(screen_changed(DRAW_OFFSET_ROWS, PORTRAIT_HEIGHT, 0,
                            DRAW_OFFSET_COLS, SCREEN_PITCH), 0);
    portrait_release();
}


/* ---- fdps_message_window_wait_key, 000203d0 ------------------------------
 *
 * Expected values come from the assembly and from resource_info/cel.md, never
 * from the emitted C.  The geometry is the six-push blit at 0002040d --
 * 0x49, 0x12e, 0x12e, the buffer, 0x140, 0xa9609 -- for the 302 x 73 window at
 * screen (9, 120); the pair at 00020495 and 00020567 -- 0xc0, 0x138, 0x168,
 * page + 0x21d8 against 0xa0504 and 0x140 -- for the 312 x 192 viewport at
 * screen (4, 4); and the three page offsets 0xc4fd, 0x106bc and 0x9ad0 pushed
 * at 000204ac, 000204e8 and 0002050f, which at a 360 pitch and the scene
 * layers' 20-row, 20-column apron are screen (9, 120), (280, 166) and
 * (12, 90).  The loop shape is the JLE at 00020446 out of the top, the
 * ADD dword ptr [EBP + 0x18],-0x1 / JNZ at 0002058e and 00020596 at the
 * bottom, and MOV dword ptr [EBP + -0x4],0x0 at 000203dc for the tick the
 * first pass derives its indicator phase from.  The indicator's own arithmetic
 * is the IDIV against 3, the AND EAX,0x3 and the ADD EAX,0x48 at 000204d7 to
 * 000204dc.
 *
 * WHAT THE RUN IS OBSERVED THROUGH.  The routine's whole output is the VGA
 * aperture at 0xa0000, which only answers in a graphics mode, so every case
 * puts the adapter into mode 13h the way the game does, paints a known
 * pattern over the page, calls, captures the frame and returns to text mode.
 * The composition page and the saved window are both allocated and freed
 * inside the call and cannot be looked at.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED.  Every pass ends waiting for
 * data_fdps_timer_tick_counter to change, and in the game that counter is
 * advanced by fdps_timer_tick_handler off AIL's timer.  Nothing advances it in
 * a test image, so the first pass would never end.  Each run hooks IRQ0 for
 * the duration of the call and chains to the handler that was there.
 *
 * THE PATTERN HAS BIT 7 SET IN EVERY BYTE, so no byte of the background can be
 * mistaken for a sprite pixel below 0x80, and none of it is zero -- which
 * matters because fdps_blit_transparent_rect skips a source byte of 0 and a
 * background carrying zeros would put holes in the window it puts back.
 *
 * WHY VILLAGE MODE IS THE DEFAULT HERE.  With data_fdps_village_mode_flag set
 * the backdrop is a copy of the visible page, so a pass that draws nothing
 * over it leaves the screen byte for byte as it was and any pixel that did
 * change is something the routine drew.  With the flag clear the backdrop is
 * whatever fdps_draw_scene_layers composes, and with no layers, no units and
 * no cursor staged that is the composition page as malloc handed it over --
 * unpredictable, so the one case that takes that branch asserts only what is
 * still determined: that the compositor ran, that the saved window came back
 * over it, and that nothing outside the 312 x 192 viewport was touched.
 *
 * WHAT THE COMMAND SHEET IS.  A byte buffer published through
 * data_fdps_command_sprite_sheet_ptr, the way tests/sprite.c stages one: the
 * drawer never opens a file, it reads an already-unpacked block out of that
 * global.  Every one of its 0x4c entries is 22 rows of one 25-pixel fill run
 * -- command 0x18 then the pixel (resource_info/cel.md) -- and entry i is
 * filled with the byte i, so a pixel on the screen names the sprite index that
 * was drawn.  The header's table-position field is deliberately wrong, so a
 * reader that consulted it instead of hardwiring 0x0f would draw nothing
 * recognisable.
 *
 * WHAT IS NOT COVERED.  The four corner pixels the copy is zeroed at cannot be
 * seen from here: the transparent put-back skips them, so the destination
 * keeps its backdrop, and in village mode that backdrop is the same screen
 * byte the copy was taken from.  Only a live battle map behind the window
 * makes them visible, and that is a playtest contract.  timeout_ticks == 0 is
 * not exercised either -- the decrement precedes the test, so it wraps and
 * spins for 2^32 ticks -- and no call site in the game passes it.
 * ------------------------------------------------------------------ */

/* The adapter, and the two modes the run moves between. */
#define WAIT_VGA_BASE 0x000a0000
#define WAIT_MODE_TEXT 0x03
#define WAIT_MODE_320X200X256 0x13

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the routine spins on the counter. */
#define WAIT_TIMER_VECTOR 8

/* The message window: 302 x 73 at screen (9, 120), the rectangle lifted from
   0xa9609 at pitch 0x140. */
#define WAIT_WINDOW_ROW 120
#define WAIT_WINDOW_COL 9
#define WAIT_WINDOW_W 302
#define WAIT_WINDOW_H 73

/* The part of the screen a pass presents: 312 x 192 at (4, 4). */
#define WAIT_VIEWPORT_ROW 4
#define WAIT_VIEWPORT_COL 4
#define WAIT_VIEWPORT_W 312
#define WAIT_VIEWPORT_H 192

/* The prompt indicator: a 25 x 22 Command.cel sprite at screen (280, 166). */
#define WAIT_INDICATOR_ROW 166
#define WAIT_INDICATOR_COL 280
#define WAIT_INDICATOR_W 25
#define WAIT_INDICATOR_H 22

/* The portrait: 125 x 100 at screen (12, 90). */
#define WAIT_PORTRAIT_ROW 90
#define WAIT_PORTRAIT_COL 12

/* The scancodes staged in the ring.  0x1c is Enter's make code and ends the
   wait; 0x9c is its break code and does not.  Only make codes ever reach the
   ring in the game -- fdps_keyboard_isr drops anything with bit 7 set -- so a
   break code is staged by hand here purely to pin the 0x80 threshold the
   routine tests, which is the same threshold the queue-empty marker 0xff
   clears. */
#define WAIT_MAKE_CODE 0x1c
#define WAIT_BREAK_CODE 0x9c

/* Sprite indices the indicator can draw: 0x48 plus the phase. */
#define WAIT_INDICATOR_SPRITE_0 0x48
#define WAIT_INDICATOR_SPRITE_3 0x4b

/* The synthetic Command.cel: a 15-byte header, an offset table at 0x0f with
   0x4c entries, then one 44-byte fill stream per entry.  0x4c is one more than
   the highest index the indicator can ask for. */
#define WAIT_CEL_TABLE_AT 0x0f
#define WAIT_CEL_ENTRIES 0x4c
#define WAIT_CEL_SPRITE_W 25
#define WAIT_CEL_SPRITE_H 22
#define WAIT_CEL_FILL_RUN_25 0x18
#define WAIT_CEL_STREAM_BYTES (WAIT_CEL_SPRITE_H * 2)
#define WAIT_CEL_STREAM_BASE 0x140
#define WAIT_CEL_SHEET_BYTES \
    (WAIT_CEL_STREAM_BASE + WAIT_CEL_ENTRIES * WAIT_CEL_STREAM_BYTES)

/* A table position the drawer must not read: it hardwires 0x0f. */
#define WAIT_CEL_DECOY_TABLE_AT 0x100

/* Which handler the installed ISR behaves as.  0 advances the counter the way
   the game's timer does.  1 drives it between two fixed values, 9 and 10, so
   that whatever a pass latches gives the same indicator phase -- 9 / 3 and
   10 / 3 are both 3 -- and the phase becomes deterministic however many times
   the interrupt happens to fire while a pass is drawing.  The two values also
   always differ from each other, so the pacing spin can never wait on a value
   equal to the one it latched. */
#define WAIT_ISR_ADVANCE 0
#define WAIT_ISR_PHASE_PAIR 1
#define WAIT_PHASE_TICK_BASE 9

/* A value data_fdps_scene_layer_scroll_last_tick cannot reach on its own here,
   so that the compositor having run is visible as that global no longer
   holding it. */
#define WAIT_SCROLL_MARKER 0x7fffff00

static unsigned char wait_sheet[WAIT_CEL_SHEET_BYTES];
static unsigned char wait_capture[SCREEN_BYTES];
static unsigned char wait_queue_codes[SCANCODE_QUEUE_LEN];
static void (__interrupt __far *wait_saved_timer)();
static unsigned int wait_timer_fires;
static int wait_isr_mode;
static int wait_sheet_staged = 0;

static void __interrupt __far wait_timer_isr(void)
{
    ++wait_timer_fires;
    if (wait_isr_mode == WAIT_ISR_ADVANCE) {
        ++data_fdps_timer_tick_counter;
    } else {
        data_fdps_timer_tick_counter =
            (unsigned int) (WAIT_PHASE_TICK_BASE + (wait_timer_fires & 1));
    }
    _chain_intr(wait_saved_timer);
}

static void wait_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void wait_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

static void wait_stage_sheet(void)
{
    int index;
    int row;
    int at;

    if (wait_sheet_staged) {
        return;
    }
    wait_sheet_staged = 1;
    memset(wait_sheet, 0, (size_t) WAIT_CEL_SHEET_BYTES);
    wait_sheet[0] = 'C';
    wait_sheet[1] = 'E';
    wait_sheet[2] = 'L';
    wait_u16(wait_sheet, 0x03, 1);
    wait_u16(wait_sheet, 0x05, WAIT_CEL_DECOY_TABLE_AT);
    wait_u16(wait_sheet, 0x07, WAIT_CEL_SPRITE_W);
    wait_u16(wait_sheet, 0x09, WAIT_CEL_SPRITE_H);
    wait_u16(wait_sheet, 0x0b, WAIT_CEL_ENTRIES - 1);
    wait_u16(wait_sheet, 0x0d, 2);

    for (index = 0; index < WAIT_CEL_ENTRIES; index++) {
        at = WAIT_CEL_STREAM_BASE + index * WAIT_CEL_STREAM_BYTES;
        wait_u32(wait_sheet, WAIT_CEL_TABLE_AT + index * 4, (unsigned long) at);
        for (row = 0; row < WAIT_CEL_SPRITE_H; row++) {
            wait_sheet[at + row * 2] = WAIT_CEL_FILL_RUN_25;
            wait_sheet[at + row * 2 + 1] = (unsigned char) index;
        }
    }
}

static void wait_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Every byte has bit 7 set, so none of it is zero and none of it can be
   mistaken for a sprite index the indicator draws. */
static int wait_pattern(int row, int col)
{
    return ((row * 31 + col * 17) & 0x7f) | 0x80;
}

static void wait_paint_pattern(unsigned char *page)
{
    int row;
    int col;

    for (row = 0; row < SCREEN_ROWS; row++) {
        for (col = 0; col < SCREEN_PITCH; col++) {
            page[(long) row * SCREEN_PITCH + col] =
                (unsigned char) wait_pattern(row, col);
        }
    }
}

static void wait_stage_queue(int count)
{
    int index;

    for (index = 0; index < SCANCODE_QUEUE_LEN; index++) {
        data_fdps_input_scancode_queue[index] = wait_queue_codes[index];
    }
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = count;
}

/* One whole wait, with the adapter in the mode the game runs it in, the
   pattern on the screen, the ring staged and a timer interrupt running.
   Leaves the frame in wait_capture[]. */
static void wait_run(int show_indicator, int timeout, int code_count,
                     int village_mode, int isr_mode)
{
    unsigned char *previous_sheet;

    wait_stage_sheet();
    wait_stage_queue(code_count);
    data_fdps_village_mode_flag = (unsigned char) village_mode;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_timer_tick_counter = 0;
    wait_timer_fires = 0;
    wait_isr_mode = isr_mode;
    previous_sheet = data_fdps_command_sprite_sheet_ptr;
    data_fdps_command_sprite_sheet_ptr = wait_sheet;

    wait_set_mode(WAIT_MODE_320X200X256);
    wait_paint_pattern((unsigned char *) WAIT_VGA_BASE);

    wait_saved_timer = _dos_getvect(WAIT_TIMER_VECTOR);
    _dos_setvect(WAIT_TIMER_VECTOR, wait_timer_isr);
    fdps_message_window_wait_key(show_indicator, timeout);
    _dos_setvect(WAIT_TIMER_VECTOR, wait_saved_timer);

    memmove(wait_capture, (void *) WAIT_VGA_BASE, (size_t) SCREEN_BYTES);
    wait_set_mode(WAIT_MODE_TEXT);

    data_fdps_command_sprite_sheet_ptr = previous_sheet;
    data_fdps_village_mode_flag = 0;
}

static int wait_pixel(int row, int col)
{
    return (int) wait_capture[(long) row * SCREEN_PITCH + col];
}

/* Bytes of the captured page that are no longer the pattern, over a window of
   rows and columns. */
static long wait_changed(int first_row, int rows, int first_col, int cols)
{
    long changed;
    int row;
    int col;

    changed = 0;
    for (row = first_row; row < first_row + rows; row++) {
        for (col = first_col; col < first_col + cols; col++) {
            if (wait_capture[(long) row * SCREEN_PITCH + col]
                    != (unsigned char) wait_pattern(row, col)) {
                changed++;
            }
        }
    }
    return changed;
}

/* How many scancodes the run took out of the ring: the read index is the only
   thing fdps_read_keyboard_queue advances, and it starts each run at 0. */
static int wait_codes_taken(void)
{
    return data_fdps_input_scancode_queue_head;
}

/* ---------------------------------------------------------------------- */

/* A make code already in the ring ends the wait at the top of the first pass,
   before anything at all is drawn: the JLE at 00020446 leaves for the free at
   0002059c.  One code is consumed and the screen is exactly as it was, which
   is what lets a window be dismissed by a key that was already down without
   the window flickering. */
static void a_queued_make_code_ends_it_before_drawing(void)
{
    wait_queue_codes[0] = WAIT_MAKE_CODE;
    wait_run(1, 30, 1, 1, WAIT_ISR_ADVANCE);
    CHECK_EQ(wait_codes_taken(), 1);
    CHECK_EQ(wait_changed(0, SCREEN_ROWS, 0, SCREEN_PITCH), 0);
}

/* An empty ring does not end it.  fdps_read_keyboard_queue reports 0xff, which
   is above the threshold, so the pass runs and the wait ends on the tick
   budget instead -- and an empty read advances no index, so nothing was
   consumed.  That a pass really ran is read off the indicator, which is drawn
   only inside one. */
static void an_empty_ring_does_not_end_it(void)
{
    wait_run(1, 1, 0, 1, WAIT_ISR_ADVANCE);
    CHECK_EQ(wait_codes_taken(), 0);
    CHECK_EQ(wait_pixel(WAIT_INDICATOR_ROW, WAIT_INDICATOR_COL),
             WAIT_INDICATOR_SPRITE_0);
}

/* A code with bit 7 set does not end it either: 0x9c is consumed, a pass runs,
   and the make code behind it ends the wait on the next pass.  Two codes taken
   is the whole of the evidence that the threshold is 0x80 and not "anything
   the ring hands back". */
static void a_code_above_the_threshold_does_not_end_it(void)
{
    wait_queue_codes[0] = WAIT_BREAK_CODE;
    wait_queue_codes[1] = WAIT_MAKE_CODE;
    wait_run(0, 30, 2, 1, WAIT_ISR_ADVANCE);
    CHECK_EQ(wait_codes_taken(), 2);
}

/* timeout_ticks is a pass count: one scancode is read per pass, the count is
   decremented at the bottom and the loop stops when it reaches 0.  With a ring
   full of codes that do not end the wait, the codes taken are exactly the
   passes made. */
static void the_tick_budget_counts_passes(void)
{
    int index;

    for (index = 0; index < SCANCODE_QUEUE_LEN; index++) {
        wait_queue_codes[index] = (unsigned char) (0x80 + index);
    }
    wait_run(0, 1, SCANCODE_QUEUE_LEN, 1, WAIT_ISR_ADVANCE);
    CHECK_EQ(wait_codes_taken(), 1);
    wait_run(0, 3, SCANCODE_QUEUE_LEN, 1, WAIT_ISR_ADVANCE);
    CHECK_EQ(wait_codes_taken(), 3);
}

/* In village mode the backdrop is a copy of the visible page, so a pass with
   nothing drawn over it puts the screen back exactly as it found it: the
   window it saved from (9, 120) lands at (9, 120) again and the 312 x 192
   viewport it copied out of (4, 4) goes back to (4, 4).  A pitch, an origin or
   an extent wrong on either side of that round trip scrambles the page. */
static void village_mode_round_trips_the_page(void)
{
    data_fdps_scene_layer_scroll_last_tick = WAIT_SCROLL_MARKER;
    wait_run(0, 1, 0, 1, WAIT_ISR_ADVANCE);
    CHECK_EQ(wait_changed(0, SCREEN_ROWS, 0, SCREEN_PITCH), 0);
    CHECK_EQ(data_fdps_scene_layer_scroll_last_tick == WAIT_SCROLL_MARKER, 1);
}

/* With the flag clear the other branch runs and fdps_draw_scene_layers
   composes the backdrop, which is visible in the scroll gate it advances.  The
   page it composes on is malloc's, and with no layers, units or cursor staged
   nothing writes to it, so only two things about the frame are still
   determined: the saved window came back over it, and the present touched
   nothing outside the 312 x 192 viewport. */
static void the_battle_branch_recomposes_the_scene(void)
{
    data_fdps_scene_layer_scroll_last_tick = WAIT_SCROLL_MARKER;
    wait_run(0, 1, 0, 0, WAIT_ISR_ADVANCE);
    CHECK_EQ(data_fdps_scene_layer_scroll_last_tick == WAIT_SCROLL_MARKER, 0);
    CHECK_EQ(wait_changed(WAIT_WINDOW_ROW, 1, WAIT_WINDOW_COL + 1,
                          WAIT_WINDOW_W - 2), 0);
    CHECK_EQ(wait_changed(WAIT_WINDOW_ROW + WAIT_WINDOW_H - 1, 1,
                          WAIT_WINDOW_COL + 1, WAIT_WINDOW_W - 2), 0);
    CHECK_EQ(wait_changed(WAIT_WINDOW_ROW + 1, WAIT_WINDOW_H - 2,
                          WAIT_WINDOW_COL, WAIT_WINDOW_W), 0);
    CHECK_EQ(wait_changed(0, WAIT_VIEWPORT_ROW, 0, SCREEN_PITCH), 0);
    CHECK_EQ(wait_changed(WAIT_VIEWPORT_ROW + WAIT_VIEWPORT_H,
                          SCREEN_ROWS - WAIT_VIEWPORT_ROW - WAIT_VIEWPORT_H,
                          0, SCREEN_PITCH), 0);
    CHECK_EQ(wait_changed(WAIT_VIEWPORT_ROW, WAIT_VIEWPORT_H, 0,
                          WAIT_VIEWPORT_COL), 0);
    CHECK_EQ(wait_changed(WAIT_VIEWPORT_ROW, WAIT_VIEWPORT_H,
                          WAIT_VIEWPORT_COL + WAIT_VIEWPORT_W,
                          SCREEN_PITCH - WAIT_VIEWPORT_COL
                              - WAIT_VIEWPORT_W), 0);
}

/* show_wait_indicator nonzero draws a 25 x 22 sprite at screen (280, 166) and
   nothing outside it.  The four bands checked around the rectangle are what
   pin 25 and 22 rather than merely the bytes they multiply to, and the pixel
   value is the sprite index the fixture writes, so it also says which entry of
   the table was drawn: 0x48 on the first pass, from the tick of 0 the prologue
   stores at 000203dc. */
static void the_indicator_is_drawn_at_the_windows_corner(void)
{
    wait_run(1, 1, 0, 1, WAIT_ISR_ADVANCE);
    CHECK_EQ(wait_pixel(WAIT_INDICATOR_ROW, WAIT_INDICATOR_COL),
             WAIT_INDICATOR_SPRITE_0);
    CHECK_EQ(wait_pixel(WAIT_INDICATOR_ROW + WAIT_INDICATOR_H - 1,
                        WAIT_INDICATOR_COL + WAIT_INDICATOR_W - 1),
             WAIT_INDICATOR_SPRITE_0);
    CHECK_EQ(wait_changed(WAIT_INDICATOR_ROW, WAIT_INDICATOR_H,
                          WAIT_INDICATOR_COL, WAIT_INDICATOR_W),
             (long) WAIT_INDICATOR_W * WAIT_INDICATOR_H);
    CHECK_EQ(wait_changed(WAIT_INDICATOR_ROW - 1, 1, WAIT_INDICATOR_COL - 1,
                          WAIT_INDICATOR_W + 2), 0);
    CHECK_EQ(wait_changed(WAIT_INDICATOR_ROW + WAIT_INDICATOR_H, 1,
                          WAIT_INDICATOR_COL - 1, WAIT_INDICATOR_W + 2), 0);
    CHECK_EQ(wait_changed(WAIT_INDICATOR_ROW, WAIT_INDICATOR_H,
                          WAIT_INDICATOR_COL - 1, 1), 0);
    CHECK_EQ(wait_changed(WAIT_INDICATOR_ROW, WAIT_INDICATOR_H,
                          WAIT_INDICATOR_COL + WAIT_INDICATOR_W, 1), 0);
}

/* show_wait_indicator zero leaves it out entirely -- the CMP dword ptr
   [EBP + 0x14],0x0 / JZ at 000204c3 skips the call -- and in village mode that
   makes the whole pass invisible. */
static void a_zero_indicator_flag_draws_nothing(void)
{
    wait_run(0, 1, 0, 1, WAIT_ISR_ADVANCE);
    CHECK_EQ(wait_changed(WAIT_INDICATOR_ROW, WAIT_INDICATOR_H,
                          WAIT_INDICATOR_COL, WAIT_INDICATOR_W), 0);
    CHECK_EQ(wait_changed(0, SCREEN_ROWS, 0, SCREEN_PITCH), 0);
}

/* The phase is the PREVIOUS pass's tick divided by three and taken modulo
   four, not the current one: the divide reads the slot the previous pass
   stored the counter into.  Two passes are made with the counter driven
   between 9 and 10, both of which divide by three to 3, so whichever of them
   the first pass latched the second draws sprite 0x48 + 3.  Only the second
   pass's frame survives on the screen. */
static void the_phase_comes_from_the_previous_passs_tick(void)
{
    wait_run(1, 2, 0, 1, WAIT_ISR_PHASE_PAIR);
    CHECK_EQ(wait_pixel(WAIT_INDICATOR_ROW, WAIT_INDICATOR_COL),
             WAIT_INDICATOR_SPRITE_3);
    CHECK_EQ(wait_pixel(WAIT_INDICATOR_ROW + WAIT_INDICATOR_H - 1,
                        WAIT_INDICATOR_COL + WAIT_INDICATOR_W - 1),
             WAIT_INDICATOR_SPRITE_3);
}

/* A loaded portrait is drawn at screen (12, 90), and OVER the window rather
   than under it: both pixels checked lie inside the 302 x 73 window rectangle
   that was put back one call earlier, so a body that drew the portrait first
   would show the window's bytes there instead.  The two values are record 0 of
   FACE.CEL decoded by hand, the same pair the portrait cases above use, offset
   by the portrait's origin.  Rows 0..3 of that record are wholly transparent,
   so the window still shows through at the top left. */
static void a_loaded_portrait_is_drawn_over_the_window(void)
{
    if (!sheet_present()) {
        return;
    }
    fdps_load_and_draw_portrait(screen, SCREEN_PITCH, RECORD_A);
    wait_run(0, 1, 0, 1, WAIT_ISR_ADVANCE);
    CHECK_EQ(wait_pixel(WAIT_PORTRAIT_ROW + PIXEL_MID_ROW,
                        WAIT_PORTRAIT_COL + PIXEL_MID_COL),
             PIXEL_MID_VALUE);
    CHECK_EQ(wait_pixel(WAIT_PORTRAIT_ROW + PIXEL_LAST_ROW,
                        WAIT_PORTRAIT_COL + PIXEL_LAST_COL),
             PIXEL_LAST_VALUE);
    CHECK_EQ(wait_changed(WAIT_PORTRAIT_ROW + PIXEL_CLEAR_ROW, 1,
                          WAIT_PORTRAIT_COL + PIXEL_CLEAR_COL, 1), 0);
    portrait_release();
}

/* A null portrait buffer is an ordinary state and not a failure: the CMP
   dword ptr [0x00060120],0x0 / JZ at 000204f6 skips the blit, and the two
   pixels the portrait would have written keep the pattern. */
static void a_null_portrait_buffer_draws_nothing(void)
{
    if (!sheet_present()) {
        return;
    }
    portrait_release();
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr == NULL, 1);
    wait_run(0, 1, 0, 1, WAIT_ISR_ADVANCE);
    CHECK_EQ(wait_changed(WAIT_PORTRAIT_ROW + PIXEL_MID_ROW, 1,
                          WAIT_PORTRAIT_COL + PIXEL_MID_COL, 1), 0);
    CHECK_EQ(wait_changed(WAIT_PORTRAIT_ROW + PIXEL_LAST_ROW, 1,
                          WAIT_PORTRAIT_COL + PIXEL_LAST_COL, 1), 0);
}

void run_msgwin_tests(void)
{
    RUN_TEST(loads_the_record_the_directory_names);
    RUN_TEST(loads_a_record_further_along_the_directory);
    RUN_TEST(loads_a_record_that_draws_nothing);
    RUN_TEST(leaves_the_record_allocated_after_drawing);
    RUN_TEST(index_minus_one_releases_and_draws_nothing);
    RUN_TEST(index_minus_one_on_an_empty_slot_is_harmless);
    RUN_TEST(the_next_load_replaces_the_last);
    RUN_TEST(closes_the_sheet_each_time);
    RUN_TEST(blits_the_sheets_rectangle_at_the_callers_pitch);
    RUN_TEST(forwards_the_pitch_it_was_given);
    RUN_TEST(draws_at_the_address_it_was_handed);
    RUN_TEST(a_queued_make_code_ends_it_before_drawing);
    RUN_TEST(an_empty_ring_does_not_end_it);
    RUN_TEST(a_code_above_the_threshold_does_not_end_it);
    RUN_TEST(the_tick_budget_counts_passes);
    RUN_TEST(village_mode_round_trips_the_page);
    RUN_TEST(the_battle_branch_recomposes_the_scene);
    RUN_TEST(the_indicator_is_drawn_at_the_windows_corner);
    RUN_TEST(a_zero_indicator_flag_draws_nothing);
    RUN_TEST(the_phase_comes_from_the_previous_passs_tick);
    RUN_TEST(a_loaded_portrait_is_drawn_over_the_window);
    RUN_TEST(a_null_portrait_buffer_draws_nothing);
}
