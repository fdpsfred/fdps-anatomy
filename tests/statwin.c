/* tests/statwin.c -- cover for src/statwin.c.
 *
 * Expected values come from the assembly at 00016e80 -- the four nine-int
 * templates at 0x14788, 0x147ac, 0x147d0 and 0x147f4 read out of the image,
 * the CMP/JGE at 00016eff that picks the upper-left panel's clipped form, the
 * CMP 0x9e / JLE at 00016fc8 that caps the lower-right panel's width, the
 * literals 0x94, 0x870f, 0x3794, 0x3700, 0x85, 0x6c, 0x9e, 0x9c and 0xc8 in
 * the six-argument pushes, and the 0x140 stride every one of them carries.
 * Every source rectangle asserted below was worked out from those and none of
 * them is read off the emitted C.
 *
 * WHAT THE FRAME IS OBSERVED THROUGH.  This function's whole visible output
 * is one memmove to 0xa0000: the buffer it composes the frame in is malloc'd
 * and freed inside the call, so the VGA aperture is the only place any of the
 * geometry can be seen.  A VGA in text mode does not decode 0xa0000 at all --
 * its graphics controller has the aperture pointed at 0xb8000 -- so each case
 * puts the adapter into mode 13h, which is the mode the game draws this
 * window in, runs the step, copies the 64000-byte frame back out of the
 * aperture and restores text mode before it asserts anything.  That is the
 * same shape tests/icon.c uses for the DAC: assert against the hardware the
 * function actually wrote to, and put the console back before the report is
 * printed on it.  The first case establishes the premise -- an aperture that
 * did not read back would make every later assertion meaningless rather than
 * false.
 *
 * THE TWO STAGED IMAGES CANNOT BE CONFUSED.  The background's bytes always
 * have bit 7 clear and the window image's always have it set, so a single
 * byte says which image it came from; and within each image the value changes
 * with every step in either direction, so a panel that lands one row or one
 * column out is caught rather than matching anyway.  Both are whole 320x200
 * frames because the function reads both in full.
 *
 * WHAT IS NOT ASSERTED.  The delay and the retrace wait have no observable
 * beyond the time they take, and timing one would measure the emulator's
 * cycle setting rather than the code; that they are there, in that order, is
 * a playtest contract (rebuild_info/pitfalls.md), not a unit-test one.  The
 * order of the four blits is likewise unobservable: at every step the four
 * destination rectangles are disjoint, so no pixel is written twice.
 *
 * THE SECOND HALF, fdps_load_status_cel_image, IS PINNED AGAINST THE SHIPPED
 * MISC.VFS.  That function names its container and its member with literals
 * and takes no argument, so nothing can point it at a smaller fixture, and a
 * fabricated sheet would be one this file encoded with the rules the decoder
 * under test implements.  MISC.VFS is staged by tests/gamefile.lst for
 * tests/main.c already and holds STATUS.CEL at 19,003 bytes.
 *
 * The pixel values the cases below expect were decoded from that member
 * independently, by walking its stream with the four-op RLE rules written
 * down in resource_info/cel.md, and not by running the code under test.  What
 * is compared against the blitter itself -- one reference call with the seven
 * arguments the assembly pushes -- is the argument list, which is the only
 * thing this function contributes to the picture: whether the RLE is decoded
 * correctly is tests/rle.c's question, not this file's.
 *
 * THE COMPARISON HAS TO BE MASKED.  Mode 0's skip op leaves pixels untouched,
 * the destination is malloc'd rather than cleared, and STATUS.CEL skips 1,212
 * of its 64,000 -- so those positions hold uninitialised bytes and comparing
 * them would be comparing the heap.  The reference is therefore blitted twice
 * over two different fills, and only the positions that agree between them
 * were written by the blit at all.
 */
#include <i86.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "testharn.h"
#include "fdpstype.h"
#include "blit.h"
#include "statwin.h"
#include "vfs.h"

#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8
#define VGA_SCREEN_BYTES (VGA_SCREEN_PITCH * VGA_SCREEN_ROWS)

#define VIDEO_MODE_TEXT 0x03
#define VIDEO_MODE_320X200X256 0x13

static unsigned char background[VGA_SCREEN_BYTES];
static unsigned char window[VGA_SCREEN_BYTES];
static unsigned char shot[VGA_SCREEN_BYTES];

/* The background's pixel at a position.  Bit 7 is always clear. */
static int bg_pixel(int row, int col)
{
    return (row * 13 + col * 7) & 0x7f;
}

/* The window image's pixel at a position.  Bit 7 is always set, so no
   background byte can ever equal a window byte. */
static int win_pixel(int row, int col)
{
    return ((row * 31 + col * 17) & 0x7f) | 0x80;
}

static void stage_images(void)
{
    int row;
    int col;

    for (row = 0; row < VGA_SCREEN_ROWS; row++) {
        for (col = 0; col < VGA_SCREEN_PITCH; col++) {
            background[row * VGA_SCREEN_PITCH + col] =
                (unsigned char) bg_pixel(row, col);
            window[row * VGA_SCREEN_PITCH + col] =
                (unsigned char) win_pixel(row, col);
        }
    }
}

static void set_video_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Copies the whole visible frame out of the VGA aperture into shot[]. */
static void capture_aperture(void)
{
    memmove(shot, (void *) VGA_SCREEN_BASE, (size_t) VGA_SCREEN_BYTES);
}

/* Runs one animation step against the staged images with the adapter in the
   mode the game draws in, and leaves the resulting frame in shot[]. */
static void draw_step(int step)
{
    set_video_mode(VIDEO_MODE_320X200X256);
    fdps_draw_status_window_anim_frame(background, window, step);
    capture_aperture();
    set_video_mode(VIDEO_MODE_TEXT);
}

static int shot_pixel(int row, int col)
{
    return (int) shot[row * VGA_SCREEN_PITCH + col];
}

/* The premise every other case rests on: in mode 13h the aperture is a plain
   64000-byte linear framebuffer that reads back what was written to it.  The
   three positions are the first byte, the last byte and one in the middle of
   the last scanline, so a mapping that was short, offset or planar would show
   up here rather than as a wrong panel later. */
static void aperture_reads_back_in_mode_13h(void)
{
    unsigned char *aperture;
    int first;
    int middle;
    int last;

    set_video_mode(VIDEO_MODE_320X200X256);
    aperture = (unsigned char *) VGA_SCREEN_BASE;
    aperture[0] = 0x5a;
    aperture[VGA_SCREEN_PITCH * (VGA_SCREEN_ROWS - 1) + 160] = 0xa5;
    aperture[VGA_SCREEN_BYTES - 1] = 0x3c;
    first = (int) aperture[0];
    middle = (int) aperture[VGA_SCREEN_PITCH * (VGA_SCREEN_ROWS - 1) + 160];
    last = (int) aperture[VGA_SCREEN_BYTES - 1];
    set_video_mode(VIDEO_MODE_TEXT);

    CHECK_EQ(first, 0x5a);
    CHECK_EQ(middle, 0xa5);
    CHECK_EQ(last, 0x3c);
}

/* Step 8, the frame the window comes to rest on: upper_left_x 15,
   lower_left_y 108, lower_right_x 148, upper_right_y 0.  Every panel is then
   at its own resting position, and because the window image holds the window
   where it rests, all four blits are the identity -- source row and column
   equal destination row and column.  So this case pins down the resting
   geometry: columns 15..305 come from the window image and everything outside
   them is still the background, with the split rows at 108 on the left and 44
   on the right.

   It is also where the lower-right clamp is measured.  0x140 - 148 is 172,
   the clamp cuts it to 0x9e = 158, and columns 306 and 319 of row 199 are
   background exactly because of that; without the clamp the panel would run
   to the right edge of the screen. */
static void settled_frame_puts_the_window_where_it_rests(void)
{
    draw_step(8);

    /* Upper-left panel: rows 0..107, columns 15..147, identity. */
    CHECK_EQ(shot_pixel(0, 15), win_pixel(0, 15));
    CHECK_EQ(shot_pixel(107, 147), win_pixel(107, 147));
    CHECK_EQ(shot_pixel(0, 14), bg_pixel(0, 14));

    /* Lower-left panel: rows 108..199, the same columns, identity.  The row
       count is 0xc8 - 108 = 92, which is exactly the rest of the screen. */
    CHECK_EQ(shot_pixel(108, 15), win_pixel(108, 15));
    CHECK_EQ(shot_pixel(199, 147), win_pixel(199, 147));
    CHECK_EQ(shot_pixel(199, 14), bg_pixel(199, 14));

    /* Lower-right panel: rows 44..199, columns 148..305, identity, and the
       two background bytes past column 305 are the clamp. */
    CHECK_EQ(shot_pixel(44, 148), win_pixel(44, 148));
    CHECK_EQ(shot_pixel(199, 305), win_pixel(199, 305));
    CHECK_EQ(shot_pixel(199, 306), bg_pixel(199, 306));
    CHECK_EQ(shot_pixel(199, 319), bg_pixel(199, 319));

    /* Upper-right panel: rows 0..43, columns 148..305, identity. */
    CHECK_EQ(shot_pixel(0, 148), win_pixel(0, 148));
    CHECK_EQ(shot_pixel(43, 305), win_pixel(43, 305));
    CHECK_EQ(shot_pixel(43, 306), bg_pixel(43, 306));

    /* And the background survived where no panel reaches. */
    CHECK_EQ(shot_pixel(0, 0), bg_pixel(0, 0));
    CHECK_EQ(shot_pixel(0, 306), bg_pixel(0, 306));
}

/* Step 0, where three of the four panels are still mostly off screen:
   upper_left_x -120, lower_left_y 190, lower_right_x 300, upper_right_y -36.
   This is the case that exercises the signed reads.  The upper-left panel is
   120 columns off the left edge, so the CMP/JGE takes its other branch: 13
   columns wide, drawn at column 0, sourced from the panel's right-hand end at
   column 148 - 13 = 135.  The upper-right panel is 36 rows above the top, so
   8 of its 44 rows are on screen and they are its LAST eight, source rows 36
   to 43.  Read either table as unsigned and both of those come out somewhere
   else entirely. */
static void first_frame_clips_each_panel_to_its_edge(void)
{
    draw_step(0);

    /* Upper-left: destination rows 0..107, columns 0..12, from source
       columns 135..147. */
    CHECK_EQ(shot_pixel(0, 0), win_pixel(0, 135));
    CHECK_EQ(shot_pixel(107, 12), win_pixel(107, 147));
    CHECK_EQ(shot_pixel(0, 13), bg_pixel(0, 13));
    CHECK_EQ(shot_pixel(108, 0), bg_pixel(108, 0));

    /* Lower-left: 0xc8 - 190 = 10 rows at destination rows 190..199, from
       source rows 108..117 -- the panel's own top, not the screen's. */
    CHECK_EQ(shot_pixel(190, 15), win_pixel(108, 15));
    CHECK_EQ(shot_pixel(199, 147), win_pixel(117, 147));
    CHECK_EQ(shot_pixel(189, 15), bg_pixel(189, 15));

    /* Lower-right: 0x140 - 300 = 20 columns, below the 158 clamp, at
       destination columns 300..319 from source columns 148..167. */
    CHECK_EQ(shot_pixel(44, 300), win_pixel(44, 148));
    CHECK_EQ(shot_pixel(199, 319), win_pixel(199, 167));
    CHECK_EQ(shot_pixel(44, 299), bg_pixel(44, 299));
    CHECK_EQ(shot_pixel(43, 300), bg_pixel(43, 300));

    /* Upper-right: 44 - 36 = 8 rows at destination rows 0..7, from source
       rows 36..43, full width. */
    CHECK_EQ(shot_pixel(0, 148), win_pixel(36, 148));
    CHECK_EQ(shot_pixel(7, 305), win_pixel(43, 305));
    CHECK_EQ(shot_pixel(8, 148), bg_pixel(8, 148));
    CHECK_EQ(shot_pixel(0, 306), bg_pixel(0, 306));
}

/* Step 3, a frame in the middle of the slide where all four tables hold a
   value that appears nowhere else: upper_left_x -40, lower_left_y 140,
   lower_right_x 215, upper_right_y -16.  Nothing is clamped at this step, so
   what is measured here is the four table entries themselves and the
   arithmetic that turns each into a source rectangle. */
static void mid_frame_offsets_come_from_the_four_tables(void)
{
    draw_step(3);

    /* Upper-left: 133 - 40 = 93 columns at destination columns 0..92, from
       source columns 148 - 93 = 55 to 147. */
    CHECK_EQ(shot_pixel(0, 0), win_pixel(0, 55));
    CHECK_EQ(shot_pixel(107, 92), win_pixel(107, 147));
    CHECK_EQ(shot_pixel(0, 93), bg_pixel(0, 93));

    /* Lower-left: 0xc8 - 140 = 60 rows at destination rows 140..199, from
       source rows 108..167. */
    CHECK_EQ(shot_pixel(140, 15), win_pixel(108, 15));
    CHECK_EQ(shot_pixel(199, 147), win_pixel(167, 147));
    CHECK_EQ(shot_pixel(139, 15), bg_pixel(139, 15));

    /* Lower-right: 0x140 - 215 = 105 columns at destination columns
       215..319, from source columns 148..252. */
    CHECK_EQ(shot_pixel(44, 215), win_pixel(44, 148));
    CHECK_EQ(shot_pixel(199, 319), win_pixel(199, 252));
    CHECK_EQ(shot_pixel(44, 214), bg_pixel(44, 214));
    CHECK_EQ(shot_pixel(43, 215), bg_pixel(43, 215));

    /* Upper-right: 44 - 16 = 28 rows at destination rows 0..27, from source
       rows 16..43. */
    CHECK_EQ(shot_pixel(0, 148), win_pixel(16, 148));
    CHECK_EQ(shot_pixel(27, 305), win_pixel(43, 305));
    CHECK_EQ(shot_pixel(28, 148), bg_pixel(28, 148));
    CHECK_EQ(shot_pixel(0, 306), bg_pixel(0, 306));

    /* The background is still underneath everywhere no panel reaches. */
    CHECK_EQ(shot_pixel(199, 0), bg_pixel(199, 0));
}

/* ---- fdps_load_status_cel_image ------------------------------------- */

/* The container and the member the function names with literals at 00016840,
   held here as buffers rather than literals: fdps_vfs_load_file upper-cases
   its query in place, and this file's own copy is not the one under test. */
#define MISC_CONTAINER_NAME "MISC.VFS"
#define STATUS_MEMBER_NAME "STATUS.CEL"
#define NAME_MAX 16

/* STATUS.CEL's header, out of the shipped MISC.VFS and read the way
   resource_info/cel.md describes it.  The sentinel entry of a .CEL offset
   table is the file's own length, so SHEET_BYTES is both the member's byte
   count in the container's entry table and offset table entry 1. */
#define SHEET_BYTES 19003
#define SHEET_SPRITE_COUNT 1
#define SHEET_ENCODING 2
#define SHEET_STREAM_START 23

/* The blit the function performs, from the seven pushes between 0001689a and
   000168b4.  SOURCE_WIDTH is the third argument (PUSH 0x140 at 000168a8) and
   DEST_PITCH the fifth (PUSH 0x140 at 0001689e); they are equal here and are
   written apart so that a case reading this can see which is which. */
#define SOURCE_WIDTH 0x140
#define SOURCE_ROWS 0xc8
#define DEST_PITCH 0x140
#define BLIT_MODE_OPAQUE 0

/* How many of the 64,000 pixels the sheet's stream actually writes.  The rest
   are its skip runs, all of them between rows 118 and 144. */
#define WRITTEN_PIXELS 62788
#define A_SKIPPED_ROW 118
#define A_SKIPPED_COLUMN 24

static void *misc_vfs;
static unsigned char *sheet;

/* The same blit the function makes, over two different fills.  A position the
   two agree on was written by the blit; a position they disagree on was
   skipped, and in the function's own buffer it holds whatever malloc left. */
static unsigned char reference_over_zeroes[VGA_SCREEN_BYTES];
static unsigned char reference_over_ones[VGA_SCREEN_BYTES];

/* Used entries currently in the heap.  A used entry becomes a free entry the
   moment it is released, possibly merged with a neighbour, so the used ones
   are counted and the free ones are not. */
static int used_heap_blocks(void)
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

/* Loads STATUS.CEL once for the whole file and answers whether it is there,
   so a case can say so and stop rather than dereference nothing. */
static int sheet_is_loaded(void)
{
    char container[NAME_MAX];
    char member[NAME_MAX];

    if (sheet != NULL) {
        return 1;
    }
    strcpy(container, MISC_CONTAINER_NAME);
    misc_vfs = fdps_vfs_open(container);
    if (misc_vfs == NULL) {
        return 0;
    }
    strcpy(member, STATUS_MEMBER_NAME);
    sheet = (unsigned char *) fdps_vfs_load_file(member, misc_vfs);
    return sheet != NULL;
}

/* Blits the sheet twice into the two reference frames and answers how many
   positions the two agree on, which is how many the blit wrote. */
static int build_reference(void)
{
    unsigned char *stream;
    int index;
    int written;

    stream = sheet + *(int *) (sheet + (int) sizeof(struct fdps_cel_header));
    memset(reference_over_zeroes, 0x00, (size_t) VGA_SCREEN_BYTES);
    memset(reference_over_ones, 0xff, (size_t) VGA_SCREEN_BYTES);
    fdps_blit_dispatch(stream, reference_over_zeroes, SOURCE_WIDTH,
                       SOURCE_ROWS, DEST_PITCH, 0, BLIT_MODE_OPAQUE);
    fdps_blit_dispatch(stream, reference_over_ones, SOURCE_WIDTH, SOURCE_ROWS,
                       DEST_PITCH, 0, BLIT_MODE_OPAQUE);

    written = 0;
    for (index = 0; index < VGA_SCREEN_BYTES; index++) {
        if (reference_over_zeroes[index] == reference_over_ones[index]) {
            written++;
        }
    }
    return written;
}

/* The premise the geometry rests on: the sheet the function loads really is
   one 320x200 sprite in the encoding mode 0 decodes, and its stream starts
   where the hard-coded table position at +0x0f says.  The function reads none
   of these fields -- it pushes 0x140 and 0xc8 as immediates -- so if the
   shipped sheet ever disagreed with them, every later assertion here would be
   measuring the wrong rectangle rather than failing. */
static void status_cel_is_the_sheet_the_pushed_geometry_assumes(void)
{
    CHECK_EQ(sheet_is_loaded(), 1);
    if (sheet == NULL) {
        return;
    }

    CHECK_EQ(sheet[0], 'C');
    CHECK_EQ(sheet[1], 'E');
    CHECK_EQ(sheet[2], 'L');
    CHECK_EQ(*(short *) (sheet + 7), SOURCE_WIDTH);
    CHECK_EQ(*(short *) (sheet + 9), SOURCE_ROWS);
    CHECK_EQ(*(short *) (sheet + 11), SHEET_SPRITE_COUNT);
    CHECK_EQ(*(unsigned short *) (sheet + 13), SHEET_ENCODING);

    /* The offset table starts at 15 and its two entries are sprite 0's stream
       and the sentinel, which is the member's own byte count. */
    CHECK_EQ((int) sizeof(struct fdps_cel_header), 15);
    CHECK_EQ(*(int *) (sheet + 15), SHEET_STREAM_START);
    CHECK_EQ(*(int *) (sheet + 19), SHEET_BYTES);
}

/* The whole of what the function produces: STATUS.CEL's sprite 0 decoded into
   a fresh 64000-byte frame at a pitch of 320.  Every position the blit writes
   is compared, so a wrong source pointer, a wrong pitch, a wrong row count or
   a wrong mode all land here; and ten of them are also checked against values
   decoded from the member outside this program, so the case is not only
   saying "the same as another call to the blitter". */
static void loaded_image_is_status_cel_decoded_whole(void)
{
    unsigned char *image;
    int written;
    int mismatches;
    int index;

    CHECK_EQ(sheet_is_loaded(), 1);
    if (sheet == NULL) {
        return;
    }

    written = build_reference();
    CHECK_EQ(written, WRITTEN_PIXELS);

    /* The masked positions are real: this one is inside a skip run. */
    CHECK_EQ(reference_over_zeroes[A_SKIPPED_ROW * DEST_PITCH
                                   + A_SKIPPED_COLUMN]
                 != reference_over_ones[A_SKIPPED_ROW * DEST_PITCH
                                        + A_SKIPPED_COLUMN],
             1);

    image = (unsigned char *) fdps_load_status_cel_image();
    CHECK_EQ(image != NULL, 1);
    if (image == NULL) {
        return;
    }

    mismatches = 0;
    for (index = 0; index < VGA_SCREEN_BYTES; index++) {
        if (reference_over_zeroes[index] == reference_over_ones[index]
            && image[index] != reference_over_zeroes[index]) {
            mismatches++;
        }
    }
    CHECK_EQ(mismatches, 0);

    /* Ten positions spread over the frame, decoded from the shipped
       STATUS.CEL by the rules in resource_info/cel.md.  The last two are on
       the bottom scanline, which is where a pitch of anything but 320 stops
       landing. */
    CHECK_EQ(image[0 * DEST_PITCH + 0], 6);
    CHECK_EQ(image[0 * DEST_PITCH + 15], 175);
    CHECK_EQ(image[30 * DEST_PITCH + 300], 186);
    CHECK_EQ(image[50 * DEST_PITCH + 40], 183);
    CHECK_EQ(image[100 * DEST_PITCH + 160], 187);
    CHECK_EQ(image[145 * DEST_PITCH + 145], 175);
    CHECK_EQ(image[180 * DEST_PITCH + 290], 184);
    CHECK_EQ(image[198 * DEST_PITCH + 160], 26);
    CHECK_EQ(image[199 * DEST_PITCH + 305], 217);
    CHECK_EQ(image[199 * DEST_PITCH + 319], 6);

    free(image);
}

/* The two frees inside the call, at 00016886 and 000168c1, and the one block
   that is meant to outlive it.  The container handle and the loaded sheet are
   both released before the return, so a call that is followed by a free of
   what it returned leaves the heap exactly where it found it -- three heap
   blocks taken and three given back.  Dropping either free would show up as a
   surplus entry here and nowhere else.

   The measured call is the second one: the first is a warm-up, so that any
   one-off allocation the CRT makes for a stdio stream on the first fopen is
   already accounted for and does not read as a leak. */
static void the_call_frees_both_temporaries(void)
{
    unsigned char *image;
    int before;
    int after;
    int settled;

    CHECK_EQ(sheet_is_loaded(), 1);
    if (sheet == NULL) {
        return;
    }

    image = (unsigned char *) fdps_load_status_cel_image();
    free(image);

    before = used_heap_blocks();
    image = (unsigned char *) fdps_load_status_cel_image();
    after = used_heap_blocks();
    CHECK_EQ(image != NULL, 1);
    CHECK_EQ(after - before, 1);

    free(image);
    settled = used_heap_blocks();
    CHECK_EQ(settled, before);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

void run_statwin_tests(void)
{
    stage_images();

    RUN_TEST(aperture_reads_back_in_mode_13h);
    RUN_TEST(settled_frame_puts_the_window_where_it_rests);
    RUN_TEST(first_frame_clips_each_panel_to_its_edge);
    RUN_TEST(mid_frame_offsets_come_from_the_four_tables);
    RUN_TEST(status_cel_is_the_sheet_the_pushed_geometry_assumes);
    RUN_TEST(loaded_image_is_status_cel_decoded_whole);
    RUN_TEST(the_call_frees_both_temporaries);

    free(sheet);
    sheet = NULL;
    free(misc_vfs);
    misc_vfs = NULL;
}
