/* tests/msgwin.c -- cover for src/msgwin.c.
 *
 * One subject so far: fdps_load_and_draw_portrait at 000177d0.
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
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
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
}
