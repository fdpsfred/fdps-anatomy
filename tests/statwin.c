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
 * THE THIRD FUNCTION, fdps_unit_status_window_wait_input, IS COVERED ONLY
 * ON THE PASS THAT ENDS THE WAIT.  Its loop polls the keyboard and draws a
 * frame whenever the timer tick has moved; nothing in the test binary moves
 * that tick, because the counter is written by the game's timer interrupt
 * handler and no test installs it.  So a call that is made to draw one frame
 * cannot then be made to return: the auto-repeat reader answers 0xff for
 * every poll after the first while the tick stands still, and the loop spins.
 * The cases below therefore drive the reader to hand back an accepted code on
 * the first poll and pin what that pass does -- which code ends the wait, that
 * the poll is tested before the tick, that nothing is drawn or allocated, and
 * that the idle flag draws exactly one rand or none.  Everything the drawing
 * pass does is a playtest contract until the sprite cache and the shadow sheet
 * hold real sheets and a timer is running.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testharn.h"
#include "fdpstype.h"
#include "blit.h"
#include "gamedata.h"
#include "keybd.h"
#include "mapdraw.h"
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


/* The seed the two rand cases below run from.  Any seed does, as long as its
   first two draws differ, which the last assertion of that case checks. */
#define RAND_SEED 7

/* Scancodes the wait loop is driven with.  0x1c is Enter, one of the codes
   fdps_unit_item_select_loop acts on (the battle window discards the result
   and closes on any accepted code); 0x7f is the last code the loop will hand
   back at all (CMP ...,0x7f / JLE at 00017117 is <=, so 0x7f ends the wait
   and 0x80 would not). */
#define SCANCODE_ENTER 0x1c
#define SCANCODE_LAST_ACCEPTED 0x7f

/* Puts the auto-repeat reader in the state where its very next poll reports
   `code` unchanged: the latch holds the code and the repeat filter remembers
   a different one, which is that reader's "a key went down" path
   (src/keybd.h).  0x100 is not a byte, so it is different from every latch
   value including 0xff. */
static void arm_next_scancode(unsigned int code)
{
    data_fdps_input_last_scancode = (unsigned char) code;
    data_fdps_input_key_repeat_prev_scancode = 0x100;
    data_fdps_input_key_repeat_counter = 0;
}

/* Which codes end the wait.  The loop keeps running while the poll answers
   above 0x7f and returns the first answer at or below it, unchanged and
   without sign-extending -- 0x7f is the largest value that gets out, and the
   test that lets it out is signed (JLE), so the code is carried in an int.

   0x1c is an ordinary accepted code -- one of the six fdps_unit_item_select_loop
   discriminates on, though the assertion does not depend on that -- 0x7f is the
   boundary the JLE puts inside the accepted range, and 0 is the other end. */
static void a_code_at_or_below_7f_ends_the_wait(void)
{
    int enter;
    int boundary;
    int zero;

    arm_next_scancode(SCANCODE_ENTER);
    enter = fdps_unit_status_window_wait_input(window, 0, 0);
    arm_next_scancode(SCANCODE_LAST_ACCEPTED);
    boundary = fdps_unit_status_window_wait_input(window, 0, 0);
    arm_next_scancode(0);
    zero = fdps_unit_status_window_wait_input(window, 0, 0);

    CHECK_EQ(enter, SCANCODE_ENTER);
    CHECK_EQ(boundary, SCANCODE_LAST_ACCEPTED);
    CHECK_EQ(zero, 0);
}

/* The poll is tested before the tick, and a pass that ends the wait draws
   nothing at all.  The last-drawn tick is set here to a value the timer
   counter does not hold, which is exactly the condition a drawing pass needs
   -- so if the tick comparison came first, or if the accepted code were
   allowed to fall through it, this call would compose and present a frame and
   leave its own tick behind.  It does neither: the last-drawn tick is
   untouched and the window image still holds the pixel the staging put in the
   unit's cell at row 10, column 161.

   Both are checked because they fail apart.  A frame drawn for the wrong
   reason writes the cell; a tick recorded without a frame writes only the
   tick. */
static void an_accepted_code_draws_no_frame(void)
{
    int premise;
    int code;
    int cell_pixel;
    int tick_after;

    data_fdps_unit_status_window_last_tick = 0x5a5a;
    premise = ((int) data_fdps_timer_tick_counter
               != data_fdps_unit_status_window_last_tick);
    cell_pixel = (int) window[10 * VGA_SCREEN_PITCH + 161];

    arm_next_scancode(SCANCODE_ENTER);
    code = fdps_unit_status_window_wait_input(window, 0, 1);
    tick_after = data_fdps_unit_status_window_last_tick;

    CHECK_EQ(premise, 1);
    CHECK_EQ(code, SCANCODE_ENTER);
    CHECK_EQ(tick_after, 0x5a5a);
    CHECK_EQ((int) window[10 * VGA_SCREEN_PITCH + 161], cell_pixel);
    CHECK_EQ((int) window[10 * VGA_SCREEN_PITCH + 161],
             win_pixel(10, 161));
}

/* allow_idle_animation is read before the loop and it decides whether rand is
   called at all: CMP byte ptr [EBP+0x1c],0x0 / JZ at 000170e9 jumps past the
   CALL at 000170ef.  So the flag is observable even on a pass that draws
   nothing -- it moves the CRT's random state by exactly one draw or by none.

   That is the whole of what a caller can see of the arming from outside: the
   frame counter it sets is a local, and the animation it starts is drawn only
   on a pass that this test cannot reach.  Consuming two draws, or consuming
   one with the flag clear, would show up here and in nothing else. */
static void the_idle_flag_decides_whether_rand_is_drawn(void)
{
    int first;
    int second;
    int after_flag_clear;
    int after_flag_set;

    srand(RAND_SEED);
    first = rand();
    second = rand();

    srand(RAND_SEED);
    arm_next_scancode(SCANCODE_ENTER);
    fdps_unit_status_window_wait_input(window, 0, 0);
    after_flag_clear = rand();

    srand(RAND_SEED);
    arm_next_scancode(SCANCODE_ENTER);
    fdps_unit_status_window_wait_input(window, 0, 1);
    after_flag_set = rand();

    CHECK_EQ(first != second, 1);
    CHECK_EQ(after_flag_clear, first);
    CHECK_EQ(after_flag_set, second);
}

/* A pass that ends the wait takes no heap.  All three of the loop's
   allocations are inside the drawing branch and all three are freed before
   that branch ends, so a wait that returns on its first poll must leave the
   heap exactly as it found it.  The first call is a warm-up, so that anything
   the CRT allocates once is already accounted for. */
static void an_accepted_code_takes_no_heap(void)
{
    int before;
    int after;

    arm_next_scancode(SCANCODE_ENTER);
    fdps_unit_status_window_wait_input(window, 0, 1);

    before = used_heap_blocks();
    arm_next_scancode(SCANCODE_ENTER);
    fdps_unit_status_window_wait_input(window, 0, 1);
    after = used_heap_blocks();

    CHECK_EQ(after, before);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

/* ---- fdps_close_status_window @ 000168e0 ---------------------------- */

/* WHAT A CLOSE LEAVES BEHIND IS THE RESTORE, NOT THE ANIMATION.  The six
   retreating frames each present themselves to 0xa0000 and are then painted
   over in full -- by the saved page in a village, by the compositor and the
   border blank on the battle map -- so nothing of them survives to be asserted
   and their only observable is the time they take, which is the same playtest
   contract the opening's frames are (rebuild_info/pitfalls.md).  What the cases
   below pin is therefore the two ends of the call: where the picture behind the
   window is fetched from, and what reaches the screen and the caller's
   background afterwards.

   THE TWO BRANCHES ARE ASSERTED AGAINST EACH OTHER.  The village page below is
   filled so that no byte of it is zero, so "the border is blank" and "the page
   came back whole" are mutually exclusive statements about the same four bands
   of screen: the village case checks that the top band still holds the page and
   the battle case that it holds zeros.  A branch taken the wrong way round
   fails both.

   HOW THE BATTLE BRANCH IS MADE PREDICTABLE.  fdps_draw_scene_layers writes
   nothing into the 360x240 page with no layers, no units and no cursor staged,
   so what that branch blits into the caller's background is the page as malloc
   handed it over.  Each run therefore fills a block of exactly that size and
   releases it immediately before the call, which is the same seeding
   tests/menu.c uses on the ring menu's page: the allocator hands the freed
   block straight back, contents and all, and the first assertion of the case
   that relies on it says so in as many words rather than assuming it.

   WHY THE CUE IS A CONTAINER WITH NO MEMBERS.  fdps_play_sfx looks the name up
   in data_fdps_audio_basewav_sfx_bank_buf_ptr, and an image whose entry count is
   zero makes the lookup miss without touching the name and without allocating,
   which is what the original does on a machine whose effect pack does not hold
   the member (audio.h).  Whether a found clip reaches the mixer is that
   function's own contract and is covered in tests/audio.c.

   WHAT IS NOT COVERED.  The 120 iterations of the border loop that run past the
   end of the frame write to 0xb0000 upwards, which the adapter does not decode
   in mode 13h, so they can be neither observed nor asserted from here -- that
   the loop must keep running 320 times rather than 200 is a rebuild note
   (statwin.c), not a unit-test one.  The village branch's blit into the scratch
   page is likewise unobservable: the page is freed without being read. */

/* The view the compositor presents and the frame around it: 312 x 192 at
   (4, 4), with four pixels of border on every side. */
#define CLOSE_VIEW_TOP 4
#define CLOSE_VIEW_LEFT 4
#define CLOSE_VIEW_W 312
#define CLOSE_VIEW_H 192
#define CLOSE_BORDER 4

/* The 360x240 scene page, the size of the block each run seeds. */
#define CLOSE_SCENE_PAGE_BYTES 0x15180

/* The three fills.  All three are distinct, none of them is zero, and the
   scene fill is outside the range page_pixel produces, so any byte of any
   assertion below names where it came from. */
#define CLOSE_SCREEN_GUARD 0x5a
#define CLOSE_BG_GUARD 0xab
#define CLOSE_SCENE_FILL 0x6d

/* The two saved play-flag values the restore is measured with.  Neither is 0,
   because the staging clears the live flag before every call, and they differ
   from each other so the restore cannot pass by writing a constant. */
#define CLOSE_SAVED_FLAG_A 0x37
#define CLOSE_SAVED_FLAG_B 0x12

static unsigned char close_page[VGA_SCREEN_BYTES];
static unsigned char close_bg[VGA_SCREEN_BYTES];

/* The effect pack the cue misses in.  See the note above. */
static struct fdps_vfs_image_header close_sfx_pack;

/* The saved village page's pixel at a position.  Never zero and never equal to
   any of the three fills, so a byte of it on the screen says both that the page
   was copied and which of its positions the byte came from. */
static int page_pixel(int row, int col)
{
    return ((row * 5 + col * 3) & 0x7e) + 1;
}

/* How many bytes of a rectangle are not `value`.  Zero means the whole
   rectangle holds it. */
static long close_differing(unsigned char *buf, int row, int rows, int col,
                            int cols, int value)
{
    long count;
    int r;
    int c;

    count = 0;
    for (r = row; r < row + rows; r++) {
        for (c = col; c < col + cols; c++) {
            if ((int) buf[r * VGA_SCREEN_PITCH + c] != value) {
                count++;
            }
        }
    }
    return count;
}

/* Everything the call reads that this file can set: the saved page, the mode
   flag, the cue's container, and the scene state that keeps the compositor
   silent and its pacing spin from blocking.  The frame latch is put one off the
   counter because no timer is installed here, so a latch equal to the counter
   would spin for ever. */
static void close_stage(int village)
{
    int row;
    int col;

    for (row = 0; row < VGA_SCREEN_ROWS; row++) {
        for (col = 0; col < VGA_SCREEN_PITCH; col++) {
            close_page[row * VGA_SCREEN_PITCH + col] =
                (unsigned char) page_pixel(row, col);
        }
    }
    memset(close_bg, CLOSE_BG_GUARD, (size_t) VGA_SCREEN_BYTES);

    memset(&close_sfx_pack, 0, sizeof(close_sfx_pack));
    data_fdps_audio_basewav_sfx_bank_buf_ptr =
        (unsigned char *) &close_sfx_pack;

    data_fdps_village_backdrop_page_ptr = close_page;
    data_fdps_village_mode_flag = (unsigned char) village;

    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_scene_layer_scroll_last_tick = data_fdps_timer_tick_counter;
    data_fdps_view_frame_last_tick = data_fdps_timer_tick_counter + 1;
}

/* The pack and the page are statics, and fdps_shutdown_free_resources frees
   both of those globals unguarded, so neither may be left published. */
static void close_unstage(void)
{
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_village_backdrop_page_ptr = NULL;
    data_fdps_village_mode_flag = 0;
}

/* Fills a block the size of the scene page and gives it straight back, so the
   allocation the call is about to make lands on it. */
static void close_seed_scene_page(void)
{
    void *page;

    page = malloc((size_t) CLOSE_SCENE_PAGE_BYTES);
    memset(page, CLOSE_SCENE_FILL, (size_t) CLOSE_SCENE_PAGE_BYTES);
    free(page);
}

/* One close, against a guarded screen and a guarded background, with the
   adapter in the mode the window is drawn in.  The frame it leaves is in
   shot[] and the background it filled is in close_bg[]. */
static void close_run(int village)
{
    close_stage(village);
    close_seed_scene_page();
    set_video_mode(VIDEO_MODE_320X200X256);
    memset((void *) VGA_SCREEN_BASE, CLOSE_SCREEN_GUARD,
           (size_t) VGA_SCREEN_BYTES);
    fdps_close_status_window(window, close_bg);
    capture_aperture();
    set_video_mode(VIDEO_MODE_TEXT);
    close_unstage();
}

/* With the flag set both the screen and the caller's background come out as
   the saved page, byte for byte and all 64000 of them.  That is one memmove
   each and nothing else: the six animation frames drew over the screen and were
   painted out, the border was not blanked -- every byte of the page is non-zero
   and the top band still holds it -- and the background's guard is gone from
   the corners the battle branch would have zeroed rather than copied.

   The whole-frame comparisons are what pin the length: a copy of the 312 x 192
   window instead of the frame, or a copy of 0xfa00 bytes from the wrong origin,
   leaves the guard somewhere in the 64000. */
static void village_close_puts_the_saved_page_back(void)
{
    close_run(1);

    CHECK_EQ(memcmp(shot, close_page, (size_t) VGA_SCREEN_BYTES), 0);
    CHECK_EQ(memcmp(close_bg, close_page, (size_t) VGA_SCREEN_BYTES), 0);

    CHECK_EQ(shot_pixel(0, 0), page_pixel(0, 0));
    CHECK_EQ(shot_pixel(0, VGA_SCREEN_PITCH - 1),
             page_pixel(0, VGA_SCREEN_PITCH - 1));
    CHECK_EQ(shot_pixel(VGA_SCREEN_ROWS - 1, 0),
             page_pixel(VGA_SCREEN_ROWS - 1, 0));
    CHECK_EQ(shot_pixel(VGA_SCREEN_ROWS - 1, VGA_SCREEN_PITCH - 1),
             page_pixel(VGA_SCREEN_ROWS - 1, VGA_SCREEN_PITCH - 1));

    /* No byte of the top band is zero, which is the statement the battle
       branch's case contradicts. */
    CHECK_EQ(close_differing(shot, 0, CLOSE_BORDER, 0, VGA_SCREEN_PITCH, 0),
             (long) CLOSE_BORDER * VGA_SCREEN_PITCH);

    CHECK_EQ((int) close_bg[0], page_pixel(0, 0));
    CHECK_EQ((int) close_bg[VGA_SCREEN_BYTES - 1],
             page_pixel(VGA_SCREEN_ROWS - 1, VGA_SCREEN_PITCH - 1));
}

/* With the flag clear the screen is put back by the compositor, which writes
   only the 312 x 192 window, and the four-pixel frame around it is blanked
   afterwards.  All four bands are checked whole rather than at their corners,
   so a band four rows deep instead of four rows of 320 bytes, or a right margin
   at column 315 instead of 316, shows up as a count rather than as a lucky
   byte.

   The interior is not asserted: it is whatever the compositor's own page held,
   and that page is the second allocation of the call rather than the seeded
   first one. */
static void battle_close_blanks_the_views_four_pixel_border(void)
{
    close_run(0);

    CHECK_EQ(close_differing(shot, 0, CLOSE_BORDER, 0, VGA_SCREEN_PITCH, 0), 0);
    CHECK_EQ(close_differing(shot, VGA_SCREEN_ROWS - CLOSE_BORDER,
                             CLOSE_BORDER, 0, VGA_SCREEN_PITCH, 0),
             0);
    CHECK_EQ(close_differing(shot, CLOSE_VIEW_TOP, CLOSE_VIEW_H, 0,
                             CLOSE_BORDER, 0),
             0);
    CHECK_EQ(close_differing(shot, CLOSE_VIEW_TOP, CLOSE_VIEW_H,
                             CLOSE_VIEW_LEFT + CLOSE_VIEW_W, CLOSE_BORDER, 0),
             0);
}

/* And the caller's background is rebuilt rather than copied: cleared to zero
   over the whole frame and then written only inside the 312 x 192 window, from
   the scene page at page byte 0x21d8 with the two strides 0x168 and 0x140.

   The first assertion is the premise -- the seeded block came back, so the
   scene page really does hold CLOSE_SCENE_FILL and the window's contents are
   determined.  The interior count then pins the destination extent and both
   strides, and the four bands around it pin the memset that a village close
   does not do.  The guard is what makes both halves fail rather than pass
   vacuously: it is neither zero nor the scene fill, so a background left
   untouched fails every one of them. */
static void battle_close_rebuilds_the_background_from_the_scene(void)
{
    close_run(0);

    CHECK_EQ((int) close_bg[CLOSE_VIEW_TOP * VGA_SCREEN_PITCH
                            + CLOSE_VIEW_LEFT],
             CLOSE_SCENE_FILL);
    CHECK_EQ(close_differing(close_bg, CLOSE_VIEW_TOP, CLOSE_VIEW_H,
                             CLOSE_VIEW_LEFT, CLOSE_VIEW_W, CLOSE_SCENE_FILL),
             0);

    CHECK_EQ(close_differing(close_bg, 0, CLOSE_BORDER, 0, VGA_SCREEN_PITCH,
                             0),
             0);
    CHECK_EQ(close_differing(close_bg, VGA_SCREEN_ROWS - CLOSE_BORDER,
                             CLOSE_BORDER, 0, VGA_SCREEN_PITCH, 0),
             0);
    CHECK_EQ(close_differing(close_bg, CLOSE_VIEW_TOP, CLOSE_VIEW_H, 0,
                             CLOSE_BORDER, 0),
             0);
    CHECK_EQ(close_differing(close_bg, CLOSE_VIEW_TOP, CLOSE_VIEW_H,
                             CLOSE_VIEW_LEFT + CLOSE_VIEW_W, CLOSE_BORDER, 0),
             0);
}

/* The play flag is restored from the saved copy, and the copy goes one way.
   The staging clears the live flag before each call, so a flag that comes back
   holding the saved value can only have been copied from it; two different
   saved values rule out a constant, and the saved byte itself is unchanged
   afterwards, which is what says this function reads 0x00063fb8 and does not
   write it.

   That the restore happens after the repaint rather than before it is not
   asserted here.  Its only consequence is whether fdps_draw_cursor_info_panel
   paints into the frame this call presents, and the panel needs the terrain
   tables and the glyph sheet to draw at all; it is a playtest contract
   (statwin.h). */
static void the_play_flag_comes_back_from_the_saved_copy(void)
{
    int after_a;
    int saved_after_a;
    int after_b;

    data_fdps_ui_play_active_flag_saved = CLOSE_SAVED_FLAG_A;
    close_run(1);
    after_a = (int) data_fdps_ui_play_active_flag;
    saved_after_a = (int) data_fdps_ui_play_active_flag_saved;

    data_fdps_ui_play_active_flag_saved = CLOSE_SAVED_FLAG_B;
    close_run(1);
    after_b = (int) data_fdps_ui_play_active_flag;

    CHECK_EQ(after_a, CLOSE_SAVED_FLAG_A);
    CHECK_EQ(saved_after_a, CLOSE_SAVED_FLAG_A);
    CHECK_EQ(after_b, CLOSE_SAVED_FLAG_B);
    CHECK_EQ((int) data_fdps_ui_play_active_flag_saved, CLOSE_SAVED_FLAG_B);
}

/* The scene page is given back.  One block is taken at the top of the call and
   released at the bottom, in both branches, and every block the six animation
   frames take is released inside the frame that took it -- so a close leaves
   the heap exactly where it found it.  Both branches are measured, because the
   village arm is the one that never reads the page it allocated and is
   therefore the one an over-tidy rewrite would drop.

   The first call of each pair is a warm-up, so anything the CRT allocates once
   is already accounted for. */
static void the_close_gives_its_scene_page_back(void)
{
    int before_village;
    int after_village;
    int before_battle;
    int after_battle;

    close_run(1);
    before_village = used_heap_blocks();
    close_run(1);
    after_village = used_heap_blocks();

    close_run(0);
    before_battle = used_heap_blocks();
    close_run(0);
    after_battle = used_heap_blocks();

    CHECK_EQ(after_village, before_village);
    CHECK_EQ(after_battle, before_battle);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

/* ------------------------------------------------------------------
 * fdps_draw_unit_status_panel @ 00016300
 * ------------------------------------------------------------------
 *
 * Every offset, digit count, colour row and text-id base below is read off
 * the assembly between 0001630c and 00016826 -- the nineteen ADD EAX,imm that
 * form the destinations, the PUSH 0x4 / 0x3 / 0x2 field widths, the MOV
 * [0x0006000c],0x3 / 0x1 / 0x0 stores, the IMUL by 0x75 with the ADD max /
 * DEC / IDIV that rounds the gauge up, and the ADD EAX,0x1 / 0xa1 / 0x97 in
 * front of the three text calls.  None of it is taken from the emitted C.
 *
 * WHAT THE PANEL IS OBSERVED THROUGH IS THE SURFACE IT DRAWS ON.  This
 * function writes nothing else: no return value, no global but the colour row
 * and the village index.  So the cases stage sheets whose pixels SAY which
 * slot was picked, run the whole path, and read the answer out of the
 * destination.
 *
 *   - The number sheet is the same shape tests/text.c stages: sprite n is a
 *     flat 6x8 fill of colour n + 1, and a sprite index is colour row * 13 +
 *     glyph.  One destination pixel therefore names both the digit drawn and
 *     the colour row it came from, which is what lets one assertion cover a
 *     figure's value, its position and its highlight together.
 *   - The gauge sheet's three graphics are 1, 2 and 3 in their top four rows
 *     and transparent below, so a gauge row reads back as a filled run
 *     followed by track and the fill width is the length of that run.
 *
 * THE GAUGES OVERLAP THE HP AND MP FIGURES AND ARE DRAWN OVER THEM, WHICH IS
 * WHY THE FIXTURE'S LOWER HALF IS TRANSPARENT.  The HP figures are at row 122
 * and the HP gauge covers rows 118..125; the MP pair is the same four rows
 * apart.  Both figures are drawn first and the gauge second, so an opaque bar
 * would bury them.  The shipped Bar.cel does not: its three 117x8 graphics are
 * one tapering wedge, opaque across the full width only in rows 0 and 1 and
 * narrowing to 57 columns by row 7, and the packed sheet main.c builds keeps
 * those skip runs as index 0 for fdps_blit_transparent_rect to skip.  Both
 * figure columns -- 66 and 95 measured from the gauge's own origin -- are
 * outside the wedge from row 2 down.  The fixture models that with a flat cut
 * at row 4, which is above every figure pixel an assertion reads.
 *   - The sprite cache's slot s decodes to a flat 24x24 of 0x40 + s, so the
 *     cell says which slot the village flag chose.
 *   - The font is one byte per glyph and the text block's entry k is the
 *     single glyph k, so the eight pixels a name draws spell out IN BINARY
 *     the text id the function computed.  A wrong id is read back as itself
 *     rather than as an absence.
 *
 * THE PORTRAIT IS THE REAL FACE.CEL AND CANNOT BE STOOD IN FOR.  The record
 * byte that selects it is zero-extended, so it is never the -1 that would
 * make the loader return early: every call reaches fdps_load_and_draw_portrait
 * and that routine names its sheet with a literal and exits the process on a
 * sheet it cannot open (msgwin.h).  FACE.CEL is staged by tests/gamefile.lst
 * and each case skips itself when it is not there.  Its 125x100 blit lands at
 * rows 4..103, columns 19..143, which no assertion below reads.
 *
 * WHAT IS NOT ASSERTED.  Nothing here checks the artwork -- which colour row 3
 * looks like, what portrait 0 contains -- only which slot of it was selected.
 * And the order of the draws is unobservable: all nineteen destination
 * rectangles are disjoint, so no pixel is written twice.
 */

/* The nineteen destinations, as the byte offsets the assembly adds. */
#define PN_PORTRAIT_AT 0xd21
#define PN_HP_CURRENT_AT 0x98d8
#define PN_HP_MAX_AT 0x98f5
#define PN_MP_CURRENT_AT 0xb098
#define PN_MP_MAX_AT 0xb0b5
#define PN_LEVEL_AT 0xc1fe
#define PN_EXP_AT 0xcd38
#define PN_MOVE_AT 0xc236
#define PN_HP_GAUGE_AT 0x9396
#define PN_MP_GAUGE_AT 0xab56
#define PN_HIT_AT 0xe3b8
#define PN_EV_AT 0xd878
#define PN_DX_AT 0xcd70
#define PN_AP_AT 0xd8b0
#define PN_DP_AT 0xe3f0
#define PN_NAME_AT 0x709
#define PN_CLASS_AT 0x1d80
#define PN_RACE_AT 0x1dc7

/* The three text-id bases and the colour the names are drawn in. */
#define PN_NAME_BASE 1
#define PN_RACE_BASE 0x97
#define PN_CLASS_BASE 0xa1
#define PN_TEXT_FG 0xd0

/* The colour rows: 0 plain, 1 buffed, 3 below maximum. */
#define PN_ROW_NORMAL 0
#define PN_ROW_BUFFED 1
#define PN_ROW_REDUCED 3

/* A full gauge is 0x75 columns and the two bars take graphics 1 and 2. */
#define PN_BAR_WIDTH 0x75
#define PN_HP_BAR 1
#define PN_MP_BAR 2

/* The sentinel the destination is filled with.  It is above the 65 sprite
   colours and is none of the sheet values, so an untouched pixel is always
   distinguishable from a drawn one. */
#define PN_FILL 0x5a

/* Number.cel's shape (resource_info/cel.md): the sprite offset table at a
   fixed +0x0f, one dword per sprite, five colour rows of thirteen glyphs. */
#define PN_NUM_TABLE_AT 0x0f
#define PN_NUM_GLYPHS 13
#define PN_NUM_SPRITES 65
#define PN_NUM_STREAM_AT (PN_NUM_TABLE_AT + PN_NUM_SPRITES * 4)
#define PN_NUM_STREAM_BYTES 16
#define PN_NUM_SHEET_BYTES \
    (PN_NUM_STREAM_AT + PN_NUM_SPRITES * PN_NUM_STREAM_BYTES)

/* The gauge sheet: three 0x75 x 8 graphics 0x3a8 bytes apart, opaque down to
   PN_BAR_OPAQUE_ROWS and index 0 below it. */
#define PN_BAR_GRAPHIC_STRIDE 0x3a8
#define PN_BAR_SHEET_BYTES (3 * PN_BAR_GRAPHIC_STRIDE)
#define PN_BAR_OPAQUE_ROWS 4

/* The sprite cache: a table of slots at the base, each twelve dword offsets,
   followed by one 24x24 stream per slot.  Eight slots is more than any case
   asks for and lets a wrong slot land on a different value rather than off
   the block. */
#define PN_CACHE_SLOTS 8
#define PN_CELL_SIZE 0x18
#define PN_CELL_STREAM_BYTES (PN_CELL_SIZE * 2)
#define PN_CACHE_TABLE_BYTES \
    (PN_CACHE_SLOTS * (int) sizeof(struct fdps_cel_cache_slot))
#define PN_CACHE_BYTES \
    (PN_CACHE_TABLE_BYTES + PN_CACHE_SLOTS * PN_CELL_STREAM_BYTES)

/* The text block: 256 two-byte table entries followed by 256 streams of one
   glyph and a terminator, so every id the panel can compute has an entry. */
#define PN_TEXT_ENTRIES 256
#define PN_TEXT_TABLE_BYTES (PN_TEXT_ENTRIES * 2)
#define PN_TEXT_STREAM_BYTES 4
#define PN_TEXT_BYTES \
    (PN_TEXT_TABLE_BYTES + PN_TEXT_ENTRIES * PN_TEXT_STREAM_BYTES)

/* One glyph is eight pixels wide and one row tall, so a name's eight pixels
   are the glyph byte's eight bits, most significant first. */
#define PN_GLYPH_WIDTH 8
#define PN_GLYPH_ROWS 1

#define PN_UNIT_COUNT 8

static unsigned char panel_surface[VGA_SCREEN_BYTES];
static struct fdps_unit_record panel_units[PN_UNIT_COUNT];
static unsigned char panel_number_sheet[PN_NUM_SHEET_BYTES];
static unsigned char panel_bar_sheet[PN_BAR_SHEET_BYTES];
static unsigned char panel_cache[PN_CACHE_BYTES];
static unsigned char panel_text[PN_TEXT_BYTES];
static unsigned char panel_font[PN_TEXT_ENTRIES];

/* Slot s of the sprite cache decodes to a flat cell of this colour. */
static int cell_color(int slot)
{
    return 0x40 + slot;
}

static int face_sheet_present(void)
{
    FILE *probe;

    probe = fopen("FACE.CEL", "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);
    return 1;
}

/* Builds every sheet the panel draws through and points the globals at them.
   Called once; the per-case staging below only refills the destination and
   the unit records. */
static void panel_stage_sheets(void)
{
    int index;
    int sprite;
    int row;
    int stream_at;
    int slot;

    for (index = 0; index < PN_NUM_SHEET_BYTES; index++) {
        panel_number_sheet[index] = 0;
    }
    for (sprite = 0; sprite < PN_NUM_SPRITES; sprite++) {
        stream_at = PN_NUM_STREAM_AT + sprite * PN_NUM_STREAM_BYTES;
        *(int *) (panel_number_sheet + PN_NUM_TABLE_AT + sprite * 4) =
            stream_at;
        /* Eight rows of one fill op: command 0x05 is op 00 with a run of
           (5 & 0x3f) + 1 = 6, which is exactly the cell width. */
        for (row = 0; row < 8; row++) {
            panel_number_sheet[stream_at + row * 2] = 0x05;
            panel_number_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (sprite + 1);
        }
    }

    for (index = 0; index < PN_BAR_SHEET_BYTES; index++) {
        row = index % PN_BAR_GRAPHIC_STRIDE / PN_BAR_WIDTH;
        if (row < PN_BAR_OPAQUE_ROWS) {
            panel_bar_sheet[index] =
                (unsigned char) (index / PN_BAR_GRAPHIC_STRIDE + 1);
        } else {
            panel_bar_sheet[index] = (unsigned char) 0;
        }
    }

    for (index = 0; index < PN_CACHE_BYTES; index++) {
        panel_cache[index] = 0;
    }
    for (slot = 0; slot < PN_CACHE_SLOTS; slot++) {
        stream_at = PN_CACHE_TABLE_BYTES + slot * PN_CELL_STREAM_BYTES;
        ((struct fdps_cel_cache_slot *) panel_cache)[slot].sprite_offset[0] =
            stream_at;
        /* 24 rows of one fill op: command 0x17 is a run of 24, the cell
           width, so every row closes on its own column count. */
        for (row = 0; row < PN_CELL_SIZE; row++) {
            panel_cache[stream_at + row * 2] = 0x17;
            panel_cache[stream_at + row * 2 + 1] =
                (unsigned char) cell_color(slot);
        }
    }

    for (index = 0; index < PN_TEXT_ENTRIES; index++) {
        panel_font[index] = (unsigned char) index;
        stream_at = PN_TEXT_TABLE_BYTES + index * PN_TEXT_STREAM_BYTES;
        *(short *) (panel_text + index * 2) = (short) stream_at;
        *(short *) (panel_text + stream_at) = (short) index;
        *(short *) (panel_text + stream_at + 2) = (short) -1;
    }

    data_fdps_number_glyph_sheet_ptr = panel_number_sheet;
    data_fdps_status_gauge_bar_sheet_ptr = panel_bar_sheet;
    data_fdps_cel_sprite_cache_ptr = panel_cache;
    data_fdps_all_game_text_ptr = panel_text;
    data_fdps_font_sheet_ptr = panel_font;
    data_fdps_font_glyph_width = (unsigned char) PN_GLYPH_WIDTH;
    data_fdps_glyph_cell_height = (unsigned char) PN_GLYPH_ROWS;
    data_fdps_font_glyph_stride_bytes = 1;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = PN_GLYPH_WIDTH;
    data_fdps_font_line_height = PN_GLYPH_ROWS;
    data_fdps_map_unit_array_ptr = (unsigned char *) panel_units;
}

/* Clears the records and the destination and puts the two globals the panel
   reads into their battle-phase state. */
static struct fdps_unit_record *panel_reset(void)
{
    int index;

    for (index = 0; index < VGA_SCREEN_BYTES; index++) {
        panel_surface[index] = PN_FILL;
    }
    memset(panel_units, 0, sizeof panel_units);
    data_fdps_village_mode_flag = (unsigned char) 0;
    data_fdps_number_glyph_color_row = 0;
    return &panel_units[0];
}

/* Gives the portrait buffer back between cases, the way tests/msgwin.c does:
   the loader leaves it allocated on purpose. */
static void panel_release_portrait(void)
{
    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
}

/* The top-left pixel of a figure's cell number `cell`. */
static int figure_pixel(int at, int cell)
{
    return (int) panel_surface[at + cell * 6];
}

/* Which sprite of the staged sheet stands in that cell, or -1 for a cell
   nothing drew in. */
static int figure_sprite(int at, int cell)
{
    int sprite;

    sprite = figure_pixel(at, cell) - 1;
    if (sprite < 0 || sprite >= PN_NUM_SPRITES) {
        return -1;
    }
    return sprite;
}

/* The glyph the cell holds -- 0..9 for a digit, 12 for '?' -- or -1. */
static int figure_glyph(int at, int cell)
{
    int sprite;

    sprite = figure_sprite(at, cell);
    if (sprite < 0) {
        return -1;
    }
    return sprite % PN_NUM_GLYPHS;
}

/* The colour row the cell's sprite came out of, or -1. */
static int figure_row(int at, int cell)
{
    int sprite;

    sprite = figure_sprite(at, cell);
    if (sprite < 0) {
        return -1;
    }
    return sprite / PN_NUM_GLYPHS;
}

/* How many of the bar's columns came out of the filled graphic. */
static int gauge_fill(int at, int bar_index)
{
    int column;

    column = 0;
    while (column < PN_BAR_WIDTH
           && panel_surface[at + column] == (unsigned char) (bar_index + 1)) {
        column++;
    }
    return column;
}

/* The text id a name drew, read back out of its eight pixels. */
static int name_text_id(int at)
{
    int column;
    int value;

    value = 0;
    for (column = 0; column < PN_GLYPH_WIDTH; column++) {
        if (panel_surface[at + column] == (unsigned char) PN_TEXT_FG) {
            value |= 1 << (PN_GLYPH_WIDTH - 1 - column);
        }
    }
    return value;
}

/* Nine figures, nine values, nine destinations.  Each value is chosen so its
   digits identify it, so a figure that landed at another figure's offset --
   or that was drawn from another record field -- reads back as the wrong
   digits rather than as the right ones by luck.  The field widths are pinned
   at the same time: the two-digit fields must not touch a third cell and the
   four-digit ones must pad, so 56 in a four-digit field is 0056. */
static void every_figure_lands_at_its_own_offset(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->hp_current = 1234;
    unit->hp_max = 1234;
    unit->mp_current = 56;
    unit->mp_max = 56;
    unit->level = 12;
    unit->move = 34;
    unit->exp_carry = 56;
    unit->ap = 123;
    unit->dp = 456;
    unit->hit = 789;
    unit->ev = 246;
    unit->dx_base = 135;

    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();

    CHECK_EQ(figure_glyph(PN_HP_CURRENT_AT, 0), 1);
    CHECK_EQ(figure_glyph(PN_HP_CURRENT_AT, 3), 4);
    CHECK_EQ(figure_glyph(PN_HP_MAX_AT, 0), 1);
    CHECK_EQ(figure_glyph(PN_HP_MAX_AT, 3), 4);
    CHECK_EQ(figure_glyph(PN_MP_CURRENT_AT, 0), 0);
    CHECK_EQ(figure_glyph(PN_MP_CURRENT_AT, 2), 5);
    CHECK_EQ(figure_glyph(PN_MP_CURRENT_AT, 3), 6);
    CHECK_EQ(figure_glyph(PN_MP_MAX_AT, 2), 5);
    CHECK_EQ(figure_glyph(PN_LEVEL_AT, 0), 1);
    CHECK_EQ(figure_glyph(PN_LEVEL_AT, 1), 2);
    CHECK_EQ(figure_pixel(PN_LEVEL_AT, 2), PN_FILL);
    CHECK_EQ(figure_glyph(PN_MOVE_AT, 0), 3);
    CHECK_EQ(figure_glyph(PN_MOVE_AT, 1), 4);
    CHECK_EQ(figure_pixel(PN_MOVE_AT, 2), PN_FILL);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 0), 0);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 1), 5);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 2), 6);
    CHECK_EQ(figure_glyph(PN_AP_AT, 0), 1);
    CHECK_EQ(figure_glyph(PN_AP_AT, 2), 3);
    CHECK_EQ(figure_glyph(PN_DP_AT, 0), 4);
    CHECK_EQ(figure_glyph(PN_DP_AT, 2), 6);
    CHECK_EQ(figure_glyph(PN_HIT_AT, 0), 7);
    CHECK_EQ(figure_glyph(PN_HIT_AT, 2), 9);
    CHECK_EQ(figure_glyph(PN_EV_AT, 0), 2);
    CHECK_EQ(figure_glyph(PN_EV_AT, 2), 6);
    CHECK_EQ(figure_glyph(PN_DX_AT, 0), 1);
    CHECK_EQ(figure_glyph(PN_DX_AT, 2), 5);
}

/* CMP EAX,dword ptr [EBP+-0x3c] / JZ at 00016450 and its MP twin at 000164ad
   set row 3 only when the pair differs, and the MOV [0x0006000c],0x0 that
   follows each figure is what puts it back -- so the maximum is always drawn
   plain, and the global is 0 at the RET. */
static void a_current_below_its_maximum_draws_in_the_reduced_row(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->hp_current = 1;
    unit->hp_max = 2;
    unit->mp_current = 3;
    unit->mp_max = 4;

    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();

    CHECK_EQ(figure_row(PN_HP_CURRENT_AT, 3), PN_ROW_REDUCED);
    CHECK_EQ(figure_row(PN_HP_MAX_AT, 3), PN_ROW_NORMAL);
    CHECK_EQ(figure_row(PN_MP_CURRENT_AT, 3), PN_ROW_REDUCED);
    CHECK_EQ(figure_row(PN_MP_MAX_AT, 3), PN_ROW_NORMAL);
    CHECK_EQ(data_fdps_number_glyph_color_row, PN_ROW_NORMAL);
}

/* The HP compare has no else arm: JZ at 00016453 jumps past the store rather
   than to one that clears it, so a full unit inherits whatever colour row the
   caller left behind and the first figure is drawn in it.  Row 2 is a row no
   branch in this function can select, so seeing it proves the inheritance
   rather than a missed store.  MP is drawn after the first put-back and so
   starts from 0 whatever the caller did. */
static void a_full_unit_inherits_the_callers_colour_row(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->hp_current = 9;
    unit->hp_max = 9;
    unit->mp_current = 9;
    unit->mp_max = 9;
    data_fdps_number_glyph_color_row = 2;

    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();

    CHECK_EQ(figure_row(PN_HP_CURRENT_AT, 3), 2);
    CHECK_EQ(figure_row(PN_HP_MAX_AT, 3), PN_ROW_NORMAL);
    CHECK_EQ(figure_row(PN_MP_CURRENT_AT, 3), PN_ROW_NORMAL);
    CHECK_EQ(data_fdps_number_glyph_color_row, PN_ROW_NORMAL);
}

/* Each buff timer lights exactly the figures its buff moves, which is the
   grouping fdps_unit_recompute_combat_stats applies: timer 2 raises dx and so
   both hit and ev, timer 0 multiplies ap and timer 1 multiplies dp.  The
   three tests are separate CMP byte ptr [EAX+0x2x],0x0 with their own
   else arms, so a timer that is clear drives its figures back to row 0
   instead of leaving the previous group's row standing. */
static void each_buff_timer_lights_only_its_own_figures(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->status_timers[2] = 1;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(figure_row(PN_HIT_AT, 2), PN_ROW_BUFFED);
    CHECK_EQ(figure_row(PN_EV_AT, 2), PN_ROW_BUFFED);
    CHECK_EQ(figure_row(PN_DX_AT, 2), PN_ROW_BUFFED);
    CHECK_EQ(figure_row(PN_AP_AT, 2), PN_ROW_NORMAL);
    CHECK_EQ(figure_row(PN_DP_AT, 2), PN_ROW_NORMAL);

    unit = panel_reset();
    unit->status_timers[0] = 1;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(figure_row(PN_AP_AT, 2), PN_ROW_BUFFED);
    CHECK_EQ(figure_row(PN_DP_AT, 2), PN_ROW_NORMAL);
    CHECK_EQ(figure_row(PN_HIT_AT, 2), PN_ROW_NORMAL);

    unit = panel_reset();
    unit->status_timers[1] = 1;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(figure_row(PN_DP_AT, 2), PN_ROW_BUFFED);
    CHECK_EQ(figure_row(PN_AP_AT, 2), PN_ROW_NORMAL);
    CHECK_EQ(data_fdps_number_glyph_color_row, PN_ROW_NORMAL);
}

/* CMP dword ptr [EBP+-0x2c],0xff / MOV ...,0x3e8 at 000163de replaces the
   sentinel with a thousand, and the field it is drawn in is three digits --
   PUSH 0x3 at 00016523 -- so fdps_draw_number's overflow guard turns it into
   three '?' glyphs, sprite 12 of the row.  A player's own unit carries 0..99
   and is drawn as a figure. */
static void the_not_player_experience_sentinel_fills_the_field(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->exp_carry = (unsigned char) 0xff;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(figure_glyph(PN_EXP_AT, 0), 12);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 1), 12);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 2), 12);

    unit = panel_reset();
    unit->exp_carry = (unsigned char) 99;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(figure_glyph(PN_EXP_AT, 0), 0);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 1), 9);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 2), 9);
}

/* The fill is (current * 0x75 + max - 1) / max and it rounds UP, so one hit
   point out of a thousand still shows a column; a full pair fills all 0x75;
   and a maximum of zero or less never reaches the divide at all (CMP ...,0x0
   / JG at 000165a5).  The HP bar comes out of graphic 1 and the MP bar out of
   graphic 2, which is what separates the two rows. */
static void the_gauges_round_up_and_take_a_graphic_each(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->hp_current = 1;
    unit->hp_max = 1000;
    unit->mp_current = 40;
    unit->mp_max = 40;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(gauge_fill(PN_HP_GAUGE_AT, PN_HP_BAR), 1);
    CHECK_EQ(gauge_fill(PN_MP_GAUGE_AT, PN_MP_BAR), PN_BAR_WIDTH);
    /* The column past the HP fill is the track, graphic 0, and the fill is
       the same width three rows down -- the two blits are rectangles at the
       destination's own pitch. */
    CHECK_EQ(panel_surface[PN_HP_GAUGE_AT + 1], 1);
    CHECK_EQ(panel_surface[PN_HP_GAUGE_AT + 3 * VGA_SCREEN_PITCH], 2);

    unit = panel_reset();
    unit->hp_current = 5;
    unit->hp_max = 0;
    unit->mp_current = 5;
    unit->mp_max = -1;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(gauge_fill(PN_HP_GAUGE_AT, PN_HP_BAR), 0);
    CHECK_EQ(gauge_fill(PN_MP_GAUGE_AT, PN_MP_BAR), 0);
    CHECK_EQ(panel_surface[PN_HP_GAUGE_AT], 1);

    /* Half of an odd maximum rounds up rather than down: 3 of 5 is
       (3 * 117 + 4) / 5 = 71, where a truncating divide would give 70. */
    unit = panel_reset();
    unit->hp_current = 3;
    unit->hp_max = 5;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(gauge_fill(PN_HP_GAUGE_AT, PN_HP_BAR), 71);
}

/* CMP byte ptr [0x00060070],0x0 / JNZ at 000163f5 picks which index reaches
   the slot table: the record's own sprite_cache_slot on the battle map, the
   caller's unit_index in a village phase.  The store at 00016318 is the only
   thing in the image that writes data_fdps_village_status_window_unit_idx, and
   it happens only on the village arm. */
static void the_village_flag_swaps_the_cache_slot_and_latches_the_index(void)
{
    if (!face_sheet_present()) {
        return;
    }
    panel_reset();
    panel_units[2].sprite_cache_slot = (unsigned char) 5;
    data_fdps_village_status_window_unit_idx = -1;
    fdps_draw_unit_status_panel(2, panel_surface);
    panel_release_portrait();
    CHECK_EQ(panel_surface[PN_PORTRAIT_AT], cell_color(5));
    CHECK_EQ(data_fdps_village_status_window_unit_idx, -1);

    panel_reset();
    panel_units[2].sprite_cache_slot = (unsigned char) 5;
    data_fdps_village_mode_flag = (unsigned char) 1;
    fdps_draw_unit_status_panel(2, panel_surface);
    panel_release_portrait();
    data_fdps_village_mode_flag = (unsigned char) 0;
    CHECK_EQ(panel_surface[PN_PORTRAIT_AT], cell_color(2));
    CHECK_EQ(data_fdps_village_status_window_unit_idx, 2);
    /* And the cell is 24 x 24 at that offset and no larger. */
    CHECK_EQ(panel_surface[PN_PORTRAIT_AT + 23 * VGA_SCREEN_PITCH + 23],
             cell_color(2));
    CHECK_EQ(panel_surface[PN_PORTRAIT_AT + 24 * VGA_SCREEN_PITCH], PN_FILL);
    CHECK_EQ(panel_surface[PN_PORTRAIT_AT + 24], PN_FILL);
}

/* ADD EAX,0x1 at 000167c2, ADD EAX,0xa1 at 000167ec and ADD EAX,0x97 at
   0001681a: the name comes from char_id, the class name from clazz and the
   race name from race, each offset by its block's first entry.  The three
   destinations are separate, so a pair swapped between two of them is caught
   as well as a wrong base. */
static void the_three_names_come_from_their_own_blocks(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->char_id = (unsigned char) 20;
    unit->clazz = (unsigned char) 3;
    unit->race = (unsigned char) 4;

    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();

    CHECK_EQ(name_text_id(PN_NAME_AT), PN_NAME_BASE + 20);
    CHECK_EQ(name_text_id(PN_CLASS_AT), PN_CLASS_BASE + 3);
    CHECK_EQ(name_text_id(PN_RACE_AT), PN_RACE_BASE + 4);
}

/* ------------------------------------------------------------------ *
 * fdps_draw_unit_inventory @ 00024ea0
 *
 * The list is drawn entirely through four callees, so what this function
 * contributes is which sprite, which text entry, which record field and which
 * pixel -- and all four are readable off a surface once the sheets are
 * fabricated so that a drawn pixel names what drew it.  Every expected value
 * below is the literal the assembly pushes: 0x11 between rows, 0x1d/0x1e/0x1f
 * for the three icons and +4 when equipped, 0xc9 added to the item id for the
 * name, 0x40/0x41/0x3e/0x3f/0x43 for the five captions, the bounds 0, 0x15,
 * 0x27, 0x0b and 0x0c the two classifications test, and the offsets 3/9,
 * 0x13/8, 0x66/0xd and the further 0x16 for the figure.
 *
 * THE FIXTURE IS BUILT SO NOTHING COLLIDES.  A command sprite paints 0x80 plus
 * its index, a number glyph paints its digit plus one, the selection bar
 * paints 0x70, a text glyph paints 0xd0, and the surface starts at 0x5a -- so
 * one byte says both what drew it and which sprite it was.  The bar is made
 * three pixels wide and one row tall so it lives in columns 0..2, which
 * nothing else in the list ever reaches; that is what lets a case assert the
 * bar's row is the ONLY row it touched rather than merely one of them.
 *
 * The command sprites are 25 by 22 and the rows are 17 apart, so consecutive
 * rows overlap.  Every position read back below is the top-left pixel of the
 * thing that owns it, and the drawing order -- icon, name, caption, figure,
 * slot by slot -- puts the later writer on top at each of them, so no
 * assertion is reading a neighbour's overspill.
 */

#define INV_PITCH 0x140
/* A pitch that is not the 0x140 every call site passes.  Each of the three
   destinations is row * pitch + column, so at another pitch a coordinate pair
   that was folded into a fixed byte offset would land somewhere else. */
#define INV_ODD_PITCH 0x100
#define INV_ROWS 200
#define INV_SURFACE_BYTES (INV_PITCH * INV_ROWS)
#define INV_FILL 0x5a

#define INV_ROW_H 0x11
#define INV_SLOTS 8
#define INV_EMPTY 0x80
#define INV_EQUIPPED 0x40
#define INV_ICON_COL 3
#define INV_ICON_ROW_BIAS 9
#define INV_NAME_COL 0x13
#define INV_NAME_ROW_BIAS 8
#define INV_CAP_COL 0x66
#define INV_CAP_ROW_BIAS 0x0d
#define INV_FIG_COL (INV_CAP_COL + 0x16)
#define INV_BAR_ROW_BIAS 8

/* A .CEL's sprite offset table starts at +0x0f (resource_info/cel.md) and
   fdps_blit_command_sprite draws 25 by 22 of whatever the entry points at
   (sprite.h), so each stream is 22 rows of one fill op with a run of 25 --
   command byte 0x18 is op 00 with a run of (0x18 & 0x3f) + 1.  0x44 sprites is
   one more than 0x43, the highest caption the list asks for. */
#define INV_CEL_TABLE_AT 0x0f
#define INV_CMD_SPRITES 0x44
#define INV_CMD_RUN 0x18
#define INV_CMD_ROWS 22
#define INV_CMD_STREAM_BYTES (INV_CMD_ROWS * 2)
#define INV_CMD_STREAMS_AT (INV_CEL_TABLE_AT + INV_CMD_SPRITES * 4)
#define INV_CMD_SHEET_BYTES \
    (INV_CMD_STREAMS_AT + INV_CMD_SPRITES * INV_CMD_STREAM_BYTES)
#define INV_CMD_COLOR_BASE 0x80

/* Number.cel: fdps_draw_number picks colour_row * 13 + glyph off the same
   +0x0f table and draws 6 by 8, so command byte 0x05 is a run of 6.  A
   sprite's colour is its index plus one, and every case leaves
   data_fdps_number_glyph_color_row at 0, so a digit paints digit + 1. */
#define INV_NUM_GLYPHS 13
#define INV_NUM_COLOR_ROWS 5
#define INV_NUM_SPRITES (INV_NUM_GLYPHS * INV_NUM_COLOR_ROWS)
#define INV_NUM_RUN 0x05
#define INV_NUM_CELL_W 6
#define INV_NUM_CELL_H 8
#define INV_NUM_STREAM_BYTES (INV_NUM_CELL_H * 2)
#define INV_NUM_STREAMS_AT (INV_CEL_TABLE_AT + INV_NUM_SPRITES * 4)
#define INV_NUM_SHEET_BYTES \
    (INV_NUM_STREAMS_AT + INV_NUM_SPRITES * INV_NUM_STREAM_BYTES)

/* SelBar.cel.  fdps_cel_blit_sprite takes the sprite size out of the header
   rather than from its caller, so the fixture's header is what decides it:
   three columns and one row, which keeps the bar inside columns 0..2. */
#define INV_BAR_W 3
#define INV_BAR_H 1
#define INV_BAR_RUN 0x02
#define INV_BAR_SPRITES 1
#define INV_BAR_STREAMS_AT (INV_CEL_TABLE_AT + INV_BAR_SPRITES * 4)
#define INV_BAR_SHEET_BYTES (INV_BAR_STREAMS_AT + INV_BAR_H * 2)
#define INV_BAR_COLOR 0x70

/* The text block, laid out the way text.h describes: signed 16-bit offsets
   from the block's own base, then the token streams.  Entry 0xc9 + id holds
   one glyph whose index IS the id, so the eight pixels a name paints read back
   as the item id the name was fetched for. */
#define INV_NAME_TEXT_BASE 0xc9
#define INV_ITEM_IDS 256
#define INV_TEXT_ENTRIES (INV_NAME_TEXT_BASE + INV_ITEM_IDS)
#define INV_TEXT_TABLE_BYTES (INV_TEXT_ENTRIES * 2)
#define INV_TEXT_STREAM_BYTES 4
#define INV_TEXT_BYTES \
    (INV_TEXT_TABLE_BYTES + INV_TEXT_ENTRIES * INV_TEXT_STREAM_BYTES)
#define INV_TEXT_END (-1)
#define INV_TEXT_FG 0xd0

/* One glyph is eight pixels wide and one row tall, so a name's eight pixels
   are the glyph byte's eight bits, most significant first. */
#define INV_GLYPH_W 8
#define INV_GLYPH_ROWS 1

/* ITEM.DAT's stride is 0x17 and not sizeof (src/table.c). */
#define INV_ITEM_STRIDE 0x17
#define INV_ITEM_TABLE_BYTES (INV_ITEM_IDS * INV_ITEM_STRIDE)

#define INV_UNIT_COUNT 2

static unsigned char inv_surface[INV_SURFACE_BYTES];
static unsigned char inv_command_sheet[INV_CMD_SHEET_BYTES];
static unsigned char inv_number_sheet[INV_NUM_SHEET_BYTES];
static unsigned char inv_bar_sheet[INV_BAR_SHEET_BYTES];
static unsigned char inv_text[INV_TEXT_BYTES];
static unsigned char inv_font[INV_ITEM_IDS];
static unsigned char inv_item_table[INV_ITEM_TABLE_BYTES];
static struct fdps_unit_record inv_units[INV_UNIT_COUNT];
static int inv_pitch;

static void inv_stage_sheets(void)
{
    int sprite;
    int row;
    int stream_at;
    int index;
    struct fdps_cel_header *bar_header;

    memset(inv_command_sheet, 0, sizeof inv_command_sheet);
    for (sprite = 0; sprite < INV_CMD_SPRITES; sprite++) {
        stream_at = INV_CMD_STREAMS_AT + sprite * INV_CMD_STREAM_BYTES;
        *(int *) (inv_command_sheet + INV_CEL_TABLE_AT + sprite * 4) =
            stream_at;
        for (row = 0; row < INV_CMD_ROWS; row++) {
            inv_command_sheet[stream_at + row * 2] =
                (unsigned char) INV_CMD_RUN;
            inv_command_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (INV_CMD_COLOR_BASE + sprite);
        }
    }

    memset(inv_number_sheet, 0, sizeof inv_number_sheet);
    for (sprite = 0; sprite < INV_NUM_SPRITES; sprite++) {
        stream_at = INV_NUM_STREAMS_AT + sprite * INV_NUM_STREAM_BYTES;
        *(int *) (inv_number_sheet + INV_CEL_TABLE_AT + sprite * 4) = stream_at;
        for (row = 0; row < INV_NUM_CELL_H; row++) {
            inv_number_sheet[stream_at + row * 2] =
                (unsigned char) INV_NUM_RUN;
            inv_number_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (sprite + 1);
        }
    }

    memset(inv_bar_sheet, 0, sizeof inv_bar_sheet);
    bar_header = (struct fdps_cel_header *) inv_bar_sheet;
    bar_header->sprite_width = (short) INV_BAR_W;
    bar_header->sprite_height = (short) INV_BAR_H;
    bar_header->sprite_count = (short) INV_BAR_SPRITES;
    *(int *) (inv_bar_sheet + INV_CEL_TABLE_AT) = INV_BAR_STREAMS_AT;
    for (row = 0; row < INV_BAR_H; row++) {
        inv_bar_sheet[INV_BAR_STREAMS_AT + row * 2] =
            (unsigned char) INV_BAR_RUN;
        inv_bar_sheet[INV_BAR_STREAMS_AT + row * 2 + 1] =
            (unsigned char) INV_BAR_COLOR;
    }

    for (index = 0; index < INV_ITEM_IDS; index++) {
        inv_font[index] = (unsigned char) index;
    }
    memset(inv_text, 0, sizeof inv_text);
    for (index = 0; index < INV_TEXT_ENTRIES; index++) {
        stream_at = INV_TEXT_TABLE_BYTES + index * INV_TEXT_STREAM_BYTES;
        *(short *) (inv_text + index * 2) = (short) stream_at;
        if (index >= INV_NAME_TEXT_BASE) {
            *(short *) (inv_text + stream_at) =
                (short) (index - INV_NAME_TEXT_BASE);
        } else {
            *(short *) (inv_text + stream_at) = (short) 0;
        }
        *(short *) (inv_text + stream_at + 2) = (short) INV_TEXT_END;
    }
}

/* Publishes the fixture, clears the surface, the item table and the records,
   and fixes the pitch the case draws at. */
static void inv_reset(int pitch)
{
    inv_pitch = pitch;
    memset(inv_surface, INV_FILL, (size_t) INV_SURFACE_BYTES);
    memset(inv_item_table, 0, sizeof inv_item_table);
    memset(inv_units, 0, sizeof inv_units);

    data_fdps_command_sprite_sheet_ptr = inv_command_sheet;
    data_fdps_number_glyph_sheet_ptr = inv_number_sheet;
    data_fdps_selection_bar_sheet_ptr = inv_bar_sheet;
    data_fdps_all_game_text_ptr = inv_text;
    data_fdps_font_sheet_ptr = inv_font;
    data_fdps_item_effect_table_ptr = inv_item_table;
    data_fdps_map_unit_array_ptr = (unsigned char *) inv_units;
    data_fdps_font_glyph_width = (unsigned char) INV_GLYPH_W;
    data_fdps_glyph_cell_height = (unsigned char) INV_GLYPH_ROWS;
    data_fdps_font_glyph_stride_bytes = 1;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = INV_GLYPH_W;
    data_fdps_font_line_height = INV_GLYPH_ROWS;
    data_fdps_number_glyph_color_row = 0;
}

static void inv_set_slot(int slot, int flags, int item_id)
{
    inv_units[0].inventory_slots[slot * 2] = (unsigned char) flags;
    inv_units[0].inventory_slots[slot * 2 + 1] = (unsigned char) item_id;
}

static void inv_empty_all_slots(void)
{
    int slot;

    for (slot = 0; slot < INV_SLOTS; slot++) {
        inv_set_slot(slot, INV_EMPTY, 0);
    }
}

static struct fdps_item_effect *inv_item(int item_id)
{
    return (struct fdps_item_effect *)
        (inv_item_table + item_id * INV_ITEM_STRIDE);
}

static int inv_pixel(int row, int column)
{
    return (int) inv_surface[row * inv_pitch + column];
}

/* Which command sprite painted a position, or -1 for a position no command
   sprite reached. */
static int inv_command_at(int row, int column)
{
    int value;

    value = inv_pixel(row, column);
    if (value < INV_CMD_COLOR_BASE
        || value >= INV_CMD_COLOR_BASE + INV_CMD_SPRITES) {
        return -1;
    }
    return value - INV_CMD_COLOR_BASE;
}

static int inv_icon_sprite(int slot)
{
    return inv_command_at(slot * INV_ROW_H + INV_ICON_ROW_BIAS, INV_ICON_COL);
}

static int inv_caption_sprite(int slot)
{
    return inv_command_at(slot * INV_ROW_H + INV_CAP_ROW_BIAS, INV_CAP_COL);
}

/* The digit standing in cell `cell` of a row's figure, or -1 when no number
   glyph reached it. */
static int inv_figure_digit(int slot, int cell)
{
    int value;

    value = inv_pixel(slot * INV_ROW_H + INV_CAP_ROW_BIAS,
                      INV_FIG_COL + cell * INV_NUM_CELL_W);
    if (value < 1 || value > INV_NUM_GLYPHS) {
        return -1;
    }
    return value - 1;
}

/* The item id a row's name was fetched for, read back out of the glyph's
   eight pixels. */
static int inv_name_id(int slot)
{
    int column;
    int value;
    int row;

    row = slot * INV_ROW_H + INV_NAME_ROW_BIAS;
    value = 0;
    for (column = 0; column < INV_GLYPH_W; column++) {
        if (inv_pixel(row, INV_NAME_COL + column) == INV_TEXT_FG) {
            value |= 1 << (INV_GLYPH_W - 1 - column);
        }
    }
    return value;
}

/* The one row the selection bar touched, -1 when it touched none and -2 when
   it touched more than one. */
static int inv_bar_row(void)
{
    int row;
    int found;

    found = -1;
    for (row = 0; row < INV_ROWS; row++) {
        if (inv_pixel(row, 0) != INV_FILL) {
            if (found >= 0) {
                return -2;
            }
            found = row;
        }
    }
    return found;
}

static int inv_touched_bytes(void)
{
    int index;
    int count;

    count = 0;
    for (index = 0; index < INV_SURFACE_BYTES; index++) {
        if (inv_surface[index] != INV_FILL) {
            count++;
        }
    }
    return count;
}

/* IMUL EAX,dword ptr [EBP+0x18],0x11 / ADD EAX,0x8 with x = 0 and sprite 0:
   the bar sits on the highlighted row's name baseline, and every row above and
   below it is left alone. */
static void the_bar_sits_on_the_selected_rows_baseline(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, 3, inv_surface, INV_PITCH);
    CHECK_EQ(inv_bar_row(), 3 * INV_ROW_H + INV_BAR_ROW_BIAS);
    CHECK_EQ(inv_pixel(3 * INV_ROW_H + INV_BAR_ROW_BIAS, 0), INV_BAR_COLOR);
    CHECK_EQ(inv_pixel(3 * INV_ROW_H + INV_BAR_ROW_BIAS, INV_BAR_W - 1),
             INV_BAR_COLOR);
    CHECK_EQ(inv_pixel(3 * INV_ROW_H + INV_BAR_ROW_BIAS, INV_BAR_W), INV_FILL);

    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, 0, inv_surface, INV_PITCH);
    CHECK_EQ(inv_bar_row(), INV_BAR_ROW_BIAS);

    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, INV_SLOTS - 1, inv_surface, INV_PITCH);
    CHECK_EQ(inv_bar_row(), (INV_SLOTS - 1) * INV_ROW_H + INV_BAR_ROW_BIAS);
}

/* CMP dword ptr [EBP+0x18],0x0 / JL and CMP ...,0x8 / JL: the bar is drawn
   only for 0..7, and -1 is what fdps_battle_show_unit_status_window passes. */
static void a_selection_outside_the_eight_rows_draws_no_bar(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);
    CHECK_EQ(inv_bar_row(), -1);

    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, INV_SLOTS, inv_surface, INV_PITCH);
    CHECK_EQ(inv_bar_row(), -1);
}

/* TEST dword ptr [EBP+-0xc],0x80 / JNZ: bit 7 means the slot is empty and the
   row is skipped whole -- no icon, no name, no caption -- and the equipped bit
   alongside it does not change that. */
static void the_empty_bit_skips_the_whole_row(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);
    CHECK_EQ(inv_touched_bytes(), 0);

    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(2, INV_EMPTY | INV_EQUIPPED, 7);
    inv_item(7)->type = (unsigned char) 5;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);
    CHECK_EQ(inv_touched_bytes(), 0);
}

/* The three IMUL ...,0x11 and the biases 9, 8 and 0xd beside them: eight rows
   seventeen pixels apart, each with its icon at column 3, its name at column
   0x13 and its caption at column 0x66. */
static void every_row_sits_seventeen_pixels_below_the_last(void)
{
    int slot;

    inv_reset(INV_PITCH);
    for (slot = 0; slot < INV_SLOTS; slot++) {
        inv_set_slot(slot, 0, slot + 1);
        inv_item(slot + 1)->type = (unsigned char) 1;
    }
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    for (slot = 0; slot < INV_SLOTS; slot++) {
        CHECK_EQ(inv_icon_sprite(slot), 0x1d);
        CHECK_EQ(inv_caption_sprite(slot), 0x40);
        CHECK_EQ(inv_name_id(slot), slot + 1);
    }
    /* One row above the first icon and above the first name is outside the
       list: nothing widened it upwards. */
    CHECK_EQ(inv_pixel(INV_ICON_ROW_BIAS - 1, INV_ICON_COL), INV_FILL);
    CHECK_EQ(inv_pixel(INV_NAME_ROW_BIAS - 1, INV_NAME_COL), INV_FILL);
}

/* CMP ...,0x0 / JLE and CMP ...,0x15 / JLE, then CMP ...,0x27 / JLE: the icon
   is 0x1d for 1..0x15, 0x1e for 0x16..0x27 and 0x1f for everything else,
   TYPE 0 INCLUDED. */
static void the_icon_says_which_category_the_type_falls_in(void)
{
    inv_reset(INV_PITCH);
    inv_set_slot(0, 0, 1);
    inv_item(1)->type = (unsigned char) 1;
    inv_set_slot(1, 0, 2);
    inv_item(2)->type = (unsigned char) 0x15;
    inv_set_slot(2, 0, 3);
    inv_item(3)->type = (unsigned char) 0x16;
    inv_set_slot(3, 0, 4);
    inv_item(4)->type = (unsigned char) 0x27;
    inv_set_slot(4, 0, 5);
    inv_item(5)->type = (unsigned char) 0x28;
    inv_set_slot(5, 0, 6);
    inv_item(6)->type = (unsigned char) 0;
    inv_set_slot(6, 0, 7);
    inv_item(7)->type = (unsigned char) 0xff;
    inv_set_slot(7, INV_EMPTY, 0);
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_icon_sprite(0), 0x1d);
    CHECK_EQ(inv_icon_sprite(1), 0x1d);
    CHECK_EQ(inv_icon_sprite(2), 0x1e);
    CHECK_EQ(inv_icon_sprite(3), 0x1e);
    CHECK_EQ(inv_icon_sprite(4), 0x1f);
    CHECK_EQ(inv_icon_sprite(5), 0x1f);
    CHECK_EQ(inv_icon_sprite(6), 0x1f);
}

/* TEST dword ptr [EBP+-0xc],0x40 / ADD dword ptr [EBP+-0x8],0x4: the equipped
   bit moves the icon four sprites along, whichever of the three it was. */
static void the_equipped_bit_moves_the_icon_four_sprites_along(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(0, INV_EQUIPPED, 1);
    inv_item(1)->type = (unsigned char) 1;
    inv_set_slot(1, INV_EQUIPPED, 2);
    inv_item(2)->type = (unsigned char) 0x20;
    inv_set_slot(2, INV_EQUIPPED, 3);
    inv_item(3)->type = (unsigned char) 0x30;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_icon_sprite(0), 0x1d + 4);
    CHECK_EQ(inv_icon_sprite(1), 0x1e + 4);
    CHECK_EQ(inv_icon_sprite(2), 0x1f + 4);
}

/* ADD EAX,0xc9 on the slot's id byte: the name is message 0xc9 + item id, and
   the id read is the SECOND byte of the slot pair, not the flag byte. */
static void the_name_is_message_c9_plus_the_item_id(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(0, 0, 0x5a);
    inv_item(0x5a)->type = (unsigned char) 1;
    inv_set_slot(1, INV_EQUIPPED, 0xa5);
    inv_item(0xa5)->type = (unsigned char) 1;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_name_id(0), 0x5a);
    CHECK_EQ(inv_name_id(1), 0xa5);
}

/* The weapon caption: CMP EAX,0x15 / JLE then PUSH 0x40 and MOVSX EAX,word ptr
   [EAX + 0x1] -- caption 0x40 with the record's ap, in a four-digit field that
   pads. */
static void a_weapon_prints_caption_40_and_its_ap(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(0, 0, 1);
    inv_item(1)->type = (unsigned char) 0x15;
    inv_item(1)->ap = (short) 1234;
    inv_item(1)->dp = (short) 5678;
    inv_set_slot(1, 0, 2);
    inv_item(2)->type = (unsigned char) 1;
    inv_item(2)->ap = (short) 7;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_caption_sprite(0), 0x40);
    CHECK_EQ(inv_figure_digit(0, 0), 1);
    CHECK_EQ(inv_figure_digit(0, 1), 2);
    CHECK_EQ(inv_figure_digit(0, 2), 3);
    CHECK_EQ(inv_figure_digit(0, 3), 4);
    CHECK_EQ(inv_caption_sprite(1), 0x40);
    CHECK_EQ(inv_figure_digit(1, 0), 0);
    CHECK_EQ(inv_figure_digit(1, 2), 0);
    CHECK_EQ(inv_figure_digit(1, 3), 7);
}

/* The armour caption, and the point of keeping the two classifications apart:
   CMP EAX,0x27 / JG is reached with the type reloaded, so TYPE 0 -- which took
   the catch-all ICON -- prints caption 0x41 and a DP figure here.  A rewrite
   that classified once and reused the answer would give type 0 the 0x43
   caption and no number instead (statwin.h). */
static void armour_and_type_zero_both_print_caption_41_and_dp(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(0, 0, 1);
    inv_item(1)->type = (unsigned char) 0x16;
    inv_item(1)->dp = (short) 907;
    inv_set_slot(1, 0, 2);
    inv_item(2)->type = (unsigned char) 0x27;
    inv_item(2)->dp = (short) 12;
    inv_set_slot(2, 0, 3);
    /* Type 0 with a use_effect that would otherwise print an HP recovery: it
       never reaches that test. */
    inv_item(3)->type = (unsigned char) 0;
    inv_item(3)->dp = (short) 34;
    inv_item(3)->use_effect = (unsigned char) 0x0b;
    inv_item(3)->use_amount = (short) 9999;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_caption_sprite(0), 0x41);
    CHECK_EQ(inv_figure_digit(0, 1), 9);
    CHECK_EQ(inv_figure_digit(0, 2), 0);
    CHECK_EQ(inv_figure_digit(0, 3), 7);
    CHECK_EQ(inv_caption_sprite(1), 0x41);
    CHECK_EQ(inv_figure_digit(1, 2), 1);
    CHECK_EQ(inv_figure_digit(1, 3), 2);
    CHECK_EQ(inv_icon_sprite(2), 0x1f);
    CHECK_EQ(inv_caption_sprite(2), 0x41);
    CHECK_EQ(inv_figure_digit(2, 2), 3);
    CHECK_EQ(inv_figure_digit(2, 3), 4);
}

/* CMP EAX,0xb / JNZ and CMP EAX,0xc / JNZ on the use_effect byte at +0x0d,
   with MOVSX EAX,word ptr [EAX + 0xe] for the amount: only those two print a
   figure, and every other effect -- 0x20 among them, which also restores HP --
   falls through to caption 0x43 with no number beside it. */
static void only_use_effects_b_and_c_print_a_recovery_amount(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(0, 0, 1);
    inv_item(1)->type = (unsigned char) 0x28;
    inv_item(1)->use_effect = (unsigned char) 0x0b;
    inv_item(1)->use_amount = (short) 50;
    inv_item(1)->dp = (short) 7777;
    inv_set_slot(1, 0, 2);
    inv_item(2)->type = (unsigned char) 0x40;
    inv_item(2)->use_effect = (unsigned char) 0x0c;
    inv_item(2)->use_amount = (short) 306;
    inv_set_slot(2, 0, 3);
    inv_item(3)->type = (unsigned char) 0x28;
    inv_item(3)->use_effect = (unsigned char) 0x20;
    inv_item(3)->use_amount = (short) 40;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_caption_sprite(0), 0x3e);
    CHECK_EQ(inv_figure_digit(0, 2), 5);
    CHECK_EQ(inv_figure_digit(0, 3), 0);
    CHECK_EQ(inv_caption_sprite(1), 0x3f);
    CHECK_EQ(inv_figure_digit(1, 1), 3);
    CHECK_EQ(inv_figure_digit(1, 2), 0);
    CHECK_EQ(inv_figure_digit(1, 3), 6);
    CHECK_EQ(inv_caption_sprite(2), 0x43);
    /* No number was drawn, so the figure's second cell still holds the fill
       the caption sprite did not reach. */
    CHECK_EQ(inv_figure_digit(2, 1), -1);
    CHECK_EQ(inv_pixel(2 * INV_ROW_H + INV_CAP_ROW_BIAS,
                       INV_FIG_COL + INV_NUM_CELL_W), INV_FILL);
}

/* Every destination is dest_base + y * pitch + x, computed against the pitch
   the caller passed and never against a surface descriptor -- so at a pitch
   that is not 0x140 the same row and column land somewhere else. */
static void the_list_is_folded_into_the_pointer_with_the_callers_pitch(void)
{
    inv_reset(INV_ODD_PITCH);
    inv_empty_all_slots();
    inv_set_slot(2, 0, 9);
    inv_item(9)->type = (unsigned char) 3;
    inv_item(9)->ap = (short) 8;
    fdps_draw_unit_inventory(0, 2, inv_surface, INV_ODD_PITCH);

    CHECK_EQ(inv_bar_row(), 2 * INV_ROW_H + INV_BAR_ROW_BIAS);
    CHECK_EQ(inv_icon_sprite(2), 0x1d);
    CHECK_EQ(inv_caption_sprite(2), 0x40);
    CHECK_EQ(inv_name_id(2), 9);
    CHECK_EQ(inv_figure_digit(2, 3), 8);
}

/* dest_base is the list's own corner and not the surface origin: the same list
   drawn onto a base 0x3b58 bytes in -- what all four call sites pass -- lands
   0x3b58 bytes further on and nothing is left at the origin. */
static void the_list_hangs_off_dest_base(void)
{
    int shifted_row;
    int shifted_column;

    shifted_row = 0x3b58 / INV_PITCH;
    shifted_column = 0x3b58 % INV_PITCH;

    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(1, INV_EQUIPPED, 0x21);
    inv_item(0x21)->type = (unsigned char) 0x16;
    inv_item(0x21)->dp = (short) 42;
    fdps_draw_unit_inventory(0, 1, inv_surface + 0x3b58, INV_PITCH);

    CHECK_EQ(inv_command_at(shifted_row + INV_ROW_H + INV_ICON_ROW_BIAS,
                            shifted_column + INV_ICON_COL), 0x1e + 4);
    CHECK_EQ(inv_command_at(shifted_row + INV_ROW_H + INV_CAP_ROW_BIAS,
                            shifted_column + INV_CAP_COL), 0x41);
    CHECK_EQ(inv_pixel(shifted_row + INV_ROW_H + INV_BAR_ROW_BIAS,
                       shifted_column), INV_BAR_COLOR);
    CHECK_EQ(inv_pixel(INV_ROW_H + INV_ICON_ROW_BIAS, INV_ICON_COL), INV_FILL);
    CHECK_EQ(inv_pixel(INV_ROW_H + INV_BAR_ROW_BIAS, 0), INV_FILL);
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
    RUN_TEST(a_code_at_or_below_7f_ends_the_wait);
    RUN_TEST(an_accepted_code_draws_no_frame);
    RUN_TEST(the_idle_flag_decides_whether_rand_is_drawn);
    RUN_TEST(an_accepted_code_takes_no_heap);
    RUN_TEST(village_close_puts_the_saved_page_back);
    RUN_TEST(battle_close_blanks_the_views_four_pixel_border);
    RUN_TEST(battle_close_rebuilds_the_background_from_the_scene);
    RUN_TEST(the_play_flag_comes_back_from_the_saved_copy);
    RUN_TEST(the_close_gives_its_scene_page_back);

    panel_stage_sheets();
    RUN_TEST(every_figure_lands_at_its_own_offset);
    RUN_TEST(a_current_below_its_maximum_draws_in_the_reduced_row);
    RUN_TEST(a_full_unit_inherits_the_callers_colour_row);
    RUN_TEST(each_buff_timer_lights_only_its_own_figures);
    RUN_TEST(the_not_player_experience_sentinel_fills_the_field);
    RUN_TEST(the_gauges_round_up_and_take_a_graphic_each);
    RUN_TEST(the_village_flag_swaps_the_cache_slot_and_latches_the_index);
    RUN_TEST(the_three_names_come_from_their_own_blocks);

    inv_stage_sheets();
    RUN_TEST(the_bar_sits_on_the_selected_rows_baseline);
    RUN_TEST(a_selection_outside_the_eight_rows_draws_no_bar);
    RUN_TEST(the_empty_bit_skips_the_whole_row);
    RUN_TEST(every_row_sits_seventeen_pixels_below_the_last);
    RUN_TEST(the_icon_says_which_category_the_type_falls_in);
    RUN_TEST(the_equipped_bit_moves_the_icon_four_sprites_along);
    RUN_TEST(the_name_is_message_c9_plus_the_item_id);
    RUN_TEST(a_weapon_prints_caption_40_and_its_ap);
    RUN_TEST(armour_and_type_zero_both_print_caption_41_and_dp);
    RUN_TEST(only_use_effects_b_and_c_print_a_recovery_amount);
    RUN_TEST(the_list_is_folded_into_the_pointer_with_the_callers_pitch);
    RUN_TEST(the_list_hangs_off_dest_base);

    free(sheet);
    sheet = NULL;
    free(misc_vfs);
    misc_vfs = NULL;

    /* Every global panel_stage_sheets() wrote goes back: all five sheet
       pointers and the unit array base are statics of this file and would
       outlive it as live pointers into a unit that has finished, and the font
       metrics and the colour row are what a later unit expects to own. */
    panel_release_portrait();
    data_fdps_number_glyph_sheet_ptr = (unsigned char *) 0;
    data_fdps_status_gauge_bar_sheet_ptr = (unsigned char *) 0;
    data_fdps_cel_sprite_cache_ptr = (unsigned char *) 0;
    data_fdps_all_game_text_ptr = (unsigned char *) 0;
    data_fdps_font_sheet_ptr = (unsigned char *) 0;
    data_fdps_map_unit_array_ptr = (unsigned char *) 0;
    data_fdps_command_sprite_sheet_ptr = (unsigned char *) 0;
    data_fdps_selection_bar_sheet_ptr = (unsigned char *) 0;
    data_fdps_item_effect_table_ptr = (unsigned char *) 0;
    data_fdps_font_glyph_width = (unsigned char) 0;
    data_fdps_glyph_cell_height = (unsigned char) 0;
    data_fdps_font_glyph_stride_bytes = 0;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = 0;
    data_fdps_font_line_height = 0;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_village_mode_flag = (unsigned char) 0;
}
