/* tests/mapdraw.c -- cover for src/mapdraw.c.
 *
 * Every expected value below is worked out from the assembly at 0002c220 --
 * the fill loop's JL against data_fdps_scene_layer_count, the two JG pass
 * bounds, and the CMP AL,byte ptr [EDX+0x69cfe] / JBE that decides a swap --
 * by hand-running that bubble sort over the staged depths.  None of them is
 * read off the emitted C.
 *
 * The two globals the function reads are staged here rather than asserted on:
 * the count and the depth bytes are what a chapter's layer descriptor file
 * puts there at load time, and ticket 23 owns their contents.  Writing them is
 * the only way to reach the body at all, since they are its entire input
 * beside the caller's array.
 *
 * The output array is eight ints wide although the game's is six, and the
 * slots past the count are pre-filled with a sentinel: the function writes
 * exactly count entries and every case checks that the ninth byte past them is
 * still untouched, which is the only way a test can see the difference between
 * "stopped at count" and "ran to the end of the array".
 */
#include "testharn.h"
#include "gamedata.h"
#include "mapdraw.h"

#define ORDER_SLOTS 8
#define SENTINEL 0x7bad

static int order[ORDER_SLOTS];

/* Put the six depth bytes and the active-slot count in place and poison the
   whole output array, so that a slot holding its slot index afterwards can
   only have got there from the fill loop. */
static void stage(int count, int d0, int d1, int d2, int d3, int d4, int d5)
{
    int i;

    data_fdps_scene_layer_count = count;
    data_fdps_scene_layer_draw_depth[0] = (unsigned char) d0;
    data_fdps_scene_layer_draw_depth[1] = (unsigned char) d1;
    data_fdps_scene_layer_draw_depth[2] = (unsigned char) d2;
    data_fdps_scene_layer_draw_depth[3] = (unsigned char) d3;
    data_fdps_scene_layer_draw_depth[4] = (unsigned char) d4;
    data_fdps_scene_layer_draw_depth[5] = (unsigned char) d5;

    for (i = 0; i < ORDER_SLOTS; i++) {
        order[i] = SENTINEL;
    }
}

/* Depths already ascending: the fill loop's identity permutation survives the
   sort untouched, because no comparison is ever a strict greater-than. */
static void test_ascending_depths_keep_identity_order(void)
{
    stage(6, 0, 1, 2, 3, 4, 5);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(order[1], 1);
    CHECK_EQ(order[2], 2);
    CHECK_EQ(order[3], 3);
    CHECK_EQ(order[4], 4);
    CHECK_EQ(order[5], 5);
    CHECK_EQ(order[6], SENTINEL);
    CHECK_EQ(order[7], SENTINEL);
}

/* Depths strictly descending: every pass moves the current maximum to the far
   end, and the six passes' worth of bound shrinking still leaves the list
   fully reversed -- which is the check that the outer bound is count - 1 and
   not something shorter. */
static void test_descending_depths_reverse_the_list(void)
{
    stage(6, 50, 40, 30, 20, 10, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 5);
    CHECK_EQ(order[1], 4);
    CHECK_EQ(order[2], 3);
    CHECK_EQ(order[3], 2);
    CHECK_EQ(order[4], 1);
    CHECK_EQ(order[5], 0);
    CHECK_EQ(order[6], SENTINEL);
    CHECK_EQ(order[7], SENTINEL);
}

/* All six depths equal.  The swap fires only on a strict greater-than (JBE
   skips it), so the identity order is kept exactly -- the stability the plate
   comment and rebuild_info/pitfalls.md say fdps_draw_scene_layers depends on
   for which of two equal-depth layers covers the other. */
static void test_equal_depths_are_left_in_slot_order(void)
{
    stage(6, 7, 7, 7, 7, 7, 7);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(order[1], 1);
    CHECK_EQ(order[2], 2);
    CHECK_EQ(order[3], 3);
    CHECK_EQ(order[4], 4);
    CHECK_EQ(order[5], 5);
}

/* Two depth values interleaved -- slots 1, 3, 5 at depth 1 and slots 0, 2, 4
   at depth 2.  Hand-running the six passes gives 1, 3, 5, 0, 2, 4: each group
   comes out in ascending slot order, which an unstable sort is free not to do
   even while producing a correctly sorted key sequence. */
static void test_ties_keep_slot_order_within_each_depth(void)
{
    stage(6, 2, 1, 2, 1, 2, 1);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 1);
    CHECK_EQ(order[1], 3);
    CHECK_EQ(order[2], 5);
    CHECK_EQ(order[3], 0);
    CHECK_EQ(order[4], 2);
    CHECK_EQ(order[5], 4);
}

/* The signedness of the key (contract C).  Slot 0's depth is 0x80 and slot 1's
   is 0x01.  CMP AL,... / JBE is the unsigned compare, so 0x80 is 128, the swap
   fires and slot 1 comes first.  Read through a signed char 0x80 would be -128
   and the pair would come out 0, 1 instead. */
static void test_depth_key_is_compared_unsigned(void)
{
    stage(2, 0x80, 0x01, 0, 0, 0, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 1);
    CHECK_EQ(order[1], 0);
    CHECK_EQ(order[2], SENTINEL);
}

/* The same question at the other end of the byte: 0xff against 0xfe.  Unsigned
   these differ by one and no swap fires; signed they are -1 and -2 and the
   pair would be exchanged. */
static void test_high_depth_bytes_order_by_magnitude(void)
{
    stage(2, 0xfe, 0xff, 0, 0, 0, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(order[1], 1);
    CHECK_EQ(order[2], SENTINEL);
}

/* A count below the array width.  Three slots are filled and sorted -- depths
   2, 0, 1 give 1, 2, 0 -- and nothing is written past index 2, even though the
   depth bytes of slots 3 to 5 would sort earlier if the loops ran to six. */
static void test_only_the_active_slots_are_written(void)
{
    stage(3, 2, 0, 1, 0, 0, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 1);
    CHECK_EQ(order[1], 2);
    CHECK_EQ(order[2], 0);
    CHECK_EQ(order[3], SENTINEL);
    CHECK_EQ(order[4], SENTINEL);
    CHECK_EQ(order[5], SENTINEL);
}

/* One active slot.  The fill loop writes index 0, the outer pass test
   count - 1 > 0 is false at once and the inner loop never runs. */
static void test_single_slot_writes_one_entry(void)
{
    stage(1, 9, 0, 0, 0, 0, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(order[1], SENTINEL);
}

/* No active slots.  Every one of the three loop tests fails on entry, so the
   caller's array comes back exactly as it went in -- the case a rewrite using
   a do/while or a count - 1 unsigned bound would get wrong. */
static void test_zero_count_writes_nothing(void)
{
    stage(0, 3, 2, 1, 0, 0, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], SENTINEL);
    CHECK_EQ(order[1], SENTINEL);
    CHECK_EQ(order[2], SENTINEL);
}

void run_mapdraw_tests(void)
{
    RUN_TEST(test_ascending_depths_keep_identity_order);
    RUN_TEST(test_descending_depths_reverse_the_list);
    RUN_TEST(test_equal_depths_are_left_in_slot_order);
    RUN_TEST(test_ties_keep_slot_order_within_each_depth);
    RUN_TEST(test_depth_key_is_compared_unsigned);
    RUN_TEST(test_high_depth_bytes_order_by_magnitude);
    RUN_TEST(test_only_the_active_slots_are_written);
    RUN_TEST(test_single_slot_writes_one_entry);
    RUN_TEST(test_zero_count_writes_nothing);
}
