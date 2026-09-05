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
 *
 * THE CLOSE IS COVERED AT ITS TWO ENDS AND NOT IN BETWEEN, for the reason its
 * own section below sets out: the six retreating frames are painted over in
 * full before the call returns.
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
    RUN_TEST(village_close_puts_the_saved_page_back);
    RUN_TEST(battle_close_blanks_the_views_four_pixel_border);
    RUN_TEST(battle_close_rebuilds_the_background_from_the_scene);
    RUN_TEST(the_play_flag_comes_back_from_the_saved_copy);
    RUN_TEST(the_close_gives_its_scene_page_back);

    free(sheet);
    sheet = NULL;
    free(misc_vfs);
    misc_vfs = NULL;
}
