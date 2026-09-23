/* tests/rledisp.c -- cover for src/rledisp.asm, fdps_blit_dispatch (000568db).
 *
 * The dispatcher is the one way into the RLE kernels: the kernels are
 * hand-written assembly with no C interface (they take their inputs in
 * registers and out of the dispatcher's own stack frame), so every test of the
 * RLE family calls fdps_blit_dispatch.  This file covers what the dispatcher
 * itself does; tests/rlebase.c, rlepal.c, rleturn.c and rlemix.c cover each
 * kernel through it, one file per assembly file (rebuild_info/emit_pipeline.md).
 * None of the cases depends on which spelling of the routines is linked, so
 * the same cases hold for the C translation kept for reference
 * (rebuild_info/code_layout.md).
 */
#include "testharn.h"
#include "blit.h"
#include "gamedata.h"
#include "rleblend.h"
#include "rlerot.h"

/* The dispatcher itself draws nothing: what it does is publish the rectangle
   into the three globals at 0x00070022, 0x00070024 and 0x0007002e, work out
   pitch - width, and pick one of thirteen kernels off the compare chain at
   00056911.  So the cases below check three separate things -- what reaches
   the globals, what the row advance is computed from, and which kernel each
   mode value actually reaches -- and the last of those is measured through the
   real kernels rather than through a stand-in, because all thirteen are
   linked.

   HOW ONE KERNEL IS TOLD FROM ANOTHER.  Most cases below draw the same
   four-pixel row -- two painted pixels then a two-pixel transparent run -- into
   a surface pre-filled with 0x5a, and the tables are rigged so that the
   kernels leave distinguishable marks:

     the palette remap is index ^ 0x22, so a remapped 0x11 is 0x33 and a
     remapped backdrop byte 0x5a is 0x78

     the shade ramp is zero everywhere EXCEPT its 0x5a column, which is 0x10 in
     every row, and the inverse colour cube has 0x77 at index 0 and 0x99 at
     index 1.  A blend that reads the destination byte therefore resolves to
     0x99 and one that does not resolves to 0x77, which is what separates mode
     9 from mode 11 and mode 10's transparent runs from both.

   The expected values come from the assembly of the kernels, by way of the
   contracts stated in rle.h, rlecolor.h, rleblend.h and rlerot.h; a mode that
   dispatched to the wrong kernel would leave one of the other marks. */
#define DISP_SURFACE_BYTES 256
#define DISP_PITCH 8
#define DISP_WIDTH 4
#define DISP_SENTINEL 0x5a
#define DISP_PIXEL 0x11
#define DISP_PIXEL_ROW_TWO 0x22
#define DISP_RAMP_ENTRIES (18 * 256)
#define DISP_CUBE_BYTES 4096
#define DISP_BLEND_LEVEL 8
#define DISP_REMAP_XOR 0x22

static unsigned char disp_surface[DISP_SURFACE_BYTES];
static unsigned char disp_remap[256];
static unsigned int disp_ramp[DISP_RAMP_ENTRIES];
static unsigned char disp_cube[DISP_CUBE_BYTES];
static int disp_descriptor[5];
static int disp_geometry[4];

/* One row, four pixels wide: op 10 (literal) of two bytes, then op 11 (skip)
   of two.  Both are read straight off the encoding in rle.h -- top two bits
   select the op, low six bits carry len-1. */
static unsigned char disp_row_stream[4] = {0x81, DISP_PIXEL, DISP_PIXEL, 0xc1};

/* The same row twice, the second row painted in a second colour so the two can
   be told apart wherever they land. */
static unsigned char disp_two_row_stream[8] = {
    0x81, DISP_PIXEL, DISP_PIXEL, 0xc1,
    0x81, DISP_PIXEL_ROW_TWO, DISP_PIXEL_ROW_TWO, 0xc1};

/* Op 00 (fill) of four pixels: one row, every pixel of it the same colour.
   The scaling and rotating kernels resample the row, so a run of one colour is
   what makes their output stateable without predicting where each Bresenham
   step falls. */
static unsigned char disp_fill_stream[2] = {0x03, DISP_PIXEL};
static unsigned char disp_two_fill_rows[4] = {0x03, DISP_PIXEL,
                                              0x03, DISP_PIXEL_ROW_TWO};

static void disp_setup(void)
{
    int index;

    for (index = 0; index < DISP_SURFACE_BYTES; index++) {
        disp_surface[index] = DISP_SENTINEL;
    }
    for (index = 0; index < 256; index++) {
        disp_remap[index] = (unsigned char) (index ^ DISP_REMAP_XOR);
    }
    for (index = 0; index < DISP_RAMP_ENTRIES; index++) {
        disp_ramp[index] = 0;
    }
    for (index = 0; index < 18; index++) {
        disp_ramp[index * 256 + DISP_SENTINEL] = 0x10;
    }
    for (index = 0; index < DISP_CUBE_BYTES; index++) {
        disp_cube[index] = 0;
    }
    disp_cube[0] = 0x77;
    disp_cube[1] = 0x99;

    disp_descriptor[0] = (int) disp_ramp;
    disp_descriptor[1] = DISP_BLEND_LEVEL;
    disp_descriptor[2] = (int) disp_cube;
    disp_descriptor[3] = 0;
    disp_descriptor[4] = 0;
}

/* The three stores at 000568ea, 000568f8 and 00056903 happen before the
   compare chain is entered, so a mode that matches no arm still publishes the
   rectangle -- and draws nothing at all, because the chain has no default. */
static void an_unknown_mode_publishes_the_rectangle_and_draws_nothing(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface, DISP_WIDTH, 3,
                       DISP_PITCH, 0, 13);

    CHECK_EQ(data_fdps_graphics_rle_blit_src_width, DISP_WIDTH);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 3);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_pitch, DISP_PITCH);
    CHECK_EQ(disp_surface[0], DISP_SENTINEL);
    CHECK_EQ(disp_surface[1], DISP_SENTINEL);
    CHECK_EQ(disp_surface[8], DISP_SENTINEL);
}

/* All three globals are sixteen bits and all three stores are sixteen-bit
   stores (MOV word ptr), so the high half of each argument is dropped. */
static void the_published_rectangle_is_truncated_to_sixteen_bits(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface, 0x10004, 0x30002,
                       0x20168, 0, 13);

    CHECK_EQ(data_fdps_graphics_rle_blit_src_width, 4);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 2);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_pitch, 0x168);
}

/* SUB EDX,[EBP+0x10] at 000568f1 subtracts the whole 32-bit width from the
   whole 32-bit pitch, and only afterwards is either truncated into a global.
   A pitch of 0x10008 therefore gives an advance of 0x10004 and not the 4 that
   recomputing it from the published pitch would give.  Mode 9 is the witness:
   it stores whatever advance it was handed into
   data_fdps_graphics_rle_blit_dst_row_advance on entry. */
static void the_row_advance_is_computed_before_the_truncation(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface, DISP_WIDTH, 1, 0x10008,
                       (unsigned int) disp_descriptor, 9);

    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, 0x10004);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_pitch, 8);
}

/* The advance is a signed int: a pitch narrower than the row gives a negative
   one, which is what mode 8 relies on to climb the rectangle. */
static void a_pitch_narrower_than_the_row_gives_a_negative_advance(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface + 32, DISP_WIDTH, 1, 2,
                       (unsigned int) disp_descriptor, 9);

    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, -2);
}

/* Mode 0 reaches the pass-through kernel, and the advance it is handed is what
   puts the second row a whole pitch below the first: the cursor has already
   walked the four pixels of row one, so pitch - width lands it on row two. */
static void mode_zero_blits_plainly_and_steps_rows_by_the_advance(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_two_row_stream, disp_surface, DISP_WIDTH, 2,
                       DISP_PITCH, 0, 0);

    CHECK_EQ(disp_surface[0], DISP_PIXEL);
    CHECK_EQ(disp_surface[1], DISP_PIXEL);
    CHECK_EQ(disp_surface[2], DISP_SENTINEL);
    CHECK_EQ(disp_surface[3], DISP_SENTINEL);
    CHECK_EQ(disp_surface[8], DISP_PIXEL_ROW_TWO);
    CHECK_EQ(disp_surface[9], DISP_PIXEL_ROW_TWO);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Mode 1 takes the sixth argument as the address of a 256-byte table and
   remaps the backdrop showing through the transparent run as well as the
   sprite's own pixels. */
static void mode_one_remaps_the_sprite_and_the_backdrop(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface, DISP_WIDTH, 1,
                       DISP_PITCH, (unsigned int) disp_remap, 1);

    CHECK_EQ(disp_surface[0], DISP_PIXEL ^ DISP_REMAP_XOR);
    CHECK_EQ(disp_surface[1], DISP_PIXEL ^ DISP_REMAP_XOR);
    CHECK_EQ(disp_surface[2], DISP_SENTINEL ^ DISP_REMAP_XOR);
    CHECK_EQ(disp_surface[3], DISP_SENTINEL ^ DISP_REMAP_XOR);
    CHECK_EQ(disp_surface[4], DISP_SENTINEL);
}

/* Mode 2 takes the same table and remaps only what the sprite paints, which is
   the one thing separating it from mode 1. */
static void mode_two_remaps_the_sprite_only(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface, DISP_WIDTH, 1,
                       DISP_PITCH, (unsigned int) disp_remap, 2);

    CHECK_EQ(disp_surface[0], DISP_PIXEL ^ DISP_REMAP_XOR);
    CHECK_EQ(disp_surface[1], DISP_PIXEL ^ DISP_REMAP_XOR);
    CHECK_EQ(disp_surface[2], DISP_SENTINEL);
    CHECK_EQ(disp_surface[3], DISP_SENTINEL);
}

/* Mode 3 takes the sixth argument as three packed bytes and not as an address:
   0x0000ff00 is tint offset 0, colour base 0xff and band mask 0, which is the
   operand fdps_blit_unit_sprite reaches this mode with, and it flattens every
   pixel the sprite paints to palette index 0xff. */
static void mode_three_recolors_from_the_packed_operand(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface, DISP_WIDTH, 1,
                       DISP_PITCH, 0x0000ff00u, 3);

    CHECK_EQ(disp_surface[0], 0xff);
    CHECK_EQ(disp_surface[1], 0xff);
    CHECK_EQ(disp_surface[2], DISP_SENTINEL);
}

/* Mode 4 splits the sixth argument into two words -- the low one the
   destination width, the high one the destination height -- and the scaled
   kernel publishes both at 0x00070028 and 0x0007002a on entry, so a split the
   other way round would show up there.  It takes no row advance: it computes
   its own from the published pitch. */
static void mode_four_splits_the_operand_into_width_then_height(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_two_fill_rows, disp_surface, DISP_WIDTH, 2,
                       DISP_PITCH, (2u << 16) | 4u, 4);

    CHECK_EQ(data_fdps_graphics_rle_blit_dest_width, 4);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_height, 2);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_rows_remaining, 0);
    CHECK_EQ(disp_surface[0], DISP_PIXEL);
    CHECK_EQ(disp_surface[3], DISP_PIXEL);
    CHECK_EQ(disp_surface[4], DISP_SENTINEL);
}

/* Mode 5 splits the same argument into a signed dx and dy, and the rotating
   kernel publishes their magnitudes at 0x0007004c and 0x0007004e.  dx = 0x1000
   with dy = 0 is one whole destination pixel per source pixel along the row
   and no vertical component at all, so the row lands exactly where a plain
   blit would put it. */
static void mode_five_splits_the_operand_into_dx_then_dy(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_fill_stream, disp_surface, DISP_WIDTH, 1,
                       DISP_PITCH, 0x00001000u, 5);

    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0);
    CHECK_EQ(disp_surface[0], DISP_PIXEL);
    CHECK_EQ(disp_surface[3], DISP_PIXEL);
    CHECK_EQ(disp_surface[4], DISP_SENTINEL);
}

/* Mode 6 is the one mode that dereferences the sixth argument as a record of
   four slots: destination width, destination height, dx, dy.  Passing it the
   packed form modes 4 and 5 take would make it read whatever lies at address
   0x1000. */
static void mode_six_reads_the_operand_as_a_geometry_record(void)
{
    disp_setup();
    disp_geometry[0] = DISP_WIDTH;
    disp_geometry[1] = 1;
    disp_geometry[2] = 0x1000;
    disp_geometry[3] = 0;
    fdps_blit_dispatch(disp_fill_stream, disp_surface, DISP_WIDTH, 1,
                       DISP_PITCH, (unsigned int) disp_geometry, 6);

    CHECK_EQ(data_fdps_graphics_rle_blit_dest_width, DISP_WIDTH);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_height, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0);
    CHECK_EQ(disp_surface[0], DISP_PIXEL);
    CHECK_EQ(disp_surface[3], DISP_PIXEL);
}

/* Mode 7 fills each row from its right-hand column leftwards, so the literal
   run's two bytes land in the last two columns of the four-pixel row and the
   transparent run covers the first two. */
static void mode_seven_mirrors_the_row_left_to_right(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface, DISP_WIDTH, 1,
                       DISP_PITCH, 0, 7);

    CHECK_EQ(disp_surface[3], DISP_PIXEL);
    CHECK_EQ(disp_surface[2], DISP_PIXEL);
    CHECK_EQ(disp_surface[1], DISP_SENTINEL);
    CHECK_EQ(disp_surface[0], DISP_SENTINEL);
}

/* Mode 8 aims at the bottom row and climbs, so the stream's first row lands on
   the rectangle's last row and its second row on the first. */
static void mode_eight_mirrors_the_rectangle_top_to_bottom(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_two_row_stream, disp_surface, DISP_WIDTH, 2,
                       DISP_PITCH, 0, 8);

    CHECK_EQ(disp_surface[8], DISP_PIXEL);
    CHECK_EQ(disp_surface[9], DISP_PIXEL);
    CHECK_EQ(disp_surface[0], DISP_PIXEL_ROW_TWO);
    CHECK_EQ(disp_surface[1], DISP_PIXEL_ROW_TWO);
}

/* Mode 9 blends every pixel it paints WITH THE DESTINATION BYTE under it, so
   the rigged ramp resolves it through cube index 1, and it leaves the
   transparent run alone. */
static void mode_nine_blends_the_sprite_into_the_destination(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface, DISP_WIDTH, 1,
                       DISP_PITCH, (unsigned int) disp_descriptor, 9);

    CHECK_EQ(disp_surface[0], 0x99);
    CHECK_EQ(disp_surface[1], 0x99);
    CHECK_EQ(disp_surface[2], DISP_SENTINEL);
    CHECK_EQ(disp_surface[3], DISP_SENTINEL);
}

/* Mode 10 blends each painted pixel with the constant tint instead -- cube
   index 0, because neither term touches the 0x5a column -- and tints the
   backdrop showing through the transparent run as well, which is where its
   destination read shows up. */
static void mode_ten_tints_the_sprite_and_the_backdrop(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface, DISP_WIDTH, 1,
                       DISP_PITCH, (unsigned int) disp_descriptor, 10);

    CHECK_EQ(disp_surface[0], 0x77);
    CHECK_EQ(disp_surface[1], 0x77);
    CHECK_EQ(disp_surface[2], 0x99);
    CHECK_EQ(disp_surface[3], 0x99);
    CHECK_EQ(disp_surface[4], DISP_SENTINEL);
}

/* Mode 11 is mode 10 with the transparent run reverted to a plain skip, which
   is the only thing that separates the two. */
static void mode_eleven_tints_the_sprite_only(void)
{
    disp_setup();
    fdps_blit_dispatch(disp_row_stream, disp_surface, DISP_WIDTH, 1,
                       DISP_PITCH, (unsigned int) disp_descriptor, 11);

    CHECK_EQ(disp_surface[0], 0x77);
    CHECK_EQ(disp_surface[1], 0x77);
    CHECK_EQ(disp_surface[2], DISP_SENTINEL);
    CHECK_EQ(disp_surface[3], DISP_SENTINEL);
}

/* Mode 12 reads two more slots off the descriptor and copies them into its own
   two globals, so those are the proof the pointer reached it; a pixel outside
   the range it names is stored opaque rather than blended. */
static void mode_twelve_takes_the_colour_range_from_the_descriptor(void)
{
    disp_setup();
    disp_descriptor[3] = 0x20;
    disp_descriptor[4] = 0x30;
    fdps_blit_dispatch(disp_row_stream, disp_surface, DISP_WIDTH, 1,
                       DISP_PITCH, (unsigned int) disp_descriptor, 12);

    CHECK_EQ(data_fdps_graphics_rle_blit_translucent_color_min, 0x20);
    CHECK_EQ(data_fdps_graphics_rle_blit_translucent_color_max, 0x30);
    CHECK_EQ(disp_surface[0], DISP_PIXEL);
    CHECK_EQ(disp_surface[1], DISP_PIXEL);
    CHECK_EQ(disp_surface[2], DISP_SENTINEL);
}

void run_rledisp_tests(void)
{
    RUN_TEST(an_unknown_mode_publishes_the_rectangle_and_draws_nothing);
    RUN_TEST(the_published_rectangle_is_truncated_to_sixteen_bits);
    RUN_TEST(the_row_advance_is_computed_before_the_truncation);
    RUN_TEST(a_pitch_narrower_than_the_row_gives_a_negative_advance);
    RUN_TEST(mode_zero_blits_plainly_and_steps_rows_by_the_advance);
    RUN_TEST(mode_one_remaps_the_sprite_and_the_backdrop);
    RUN_TEST(mode_two_remaps_the_sprite_only);
    RUN_TEST(mode_three_recolors_from_the_packed_operand);
    RUN_TEST(mode_four_splits_the_operand_into_width_then_height);
    RUN_TEST(mode_five_splits_the_operand_into_dx_then_dy);
    RUN_TEST(mode_six_reads_the_operand_as_a_geometry_record);
    RUN_TEST(mode_seven_mirrors_the_row_left_to_right);
    RUN_TEST(mode_eight_mirrors_the_rectangle_top_to_bottom);
    RUN_TEST(mode_nine_blends_the_sprite_into_the_destination);
    RUN_TEST(mode_ten_tints_the_sprite_and_the_backdrop);
    RUN_TEST(mode_eleven_tints_the_sprite_only);
    RUN_TEST(mode_twelve_takes_the_colour_range_from_the_descriptor);
}
