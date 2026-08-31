/* tests/chevt2.c -- cover for src/chevt2.c.
 *
 * The chapter 13 handler at 000379d0 is the chapter 7 handler's shape with the
 * range 9..0x2c in place of 4..8, and is covered the same way: its two bounds,
 * the signed inclusive compare between them and the 0xf0 mask are all literals
 * in its instruction stream -- MOV dword ptr [EBP-0x20],0x9 at 000379e3, MOV
 * dword ptr [EBP-0x1c],0x2c at 000379ea, CMP EAX,[EBP-0x10] / JLE at 00037a13,
 * and AND DL,0xf0 at 00037a33 -- and the absence of a guard in front of its
 * loop is asserted by putting the shared one-shot latch slot up and watching it
 * run anyway.
 *
 * The unit array is staged here rather than read from a game file, because the
 * handler takes its whole effect through data_fdps_map_unit_array_ptr --
 * pointing that global at a local block is the only way to see the stores.
 * What the global itself holds is ticket 23's and is not asserted, so every
 * case writes the state it wants to see changed.
 *
 * Which indices the range means comes from map12.dat: its header byte 1 is 9,
 * the count of player records laid down first at 0..8, and its 37 deployment
 * records become unit indices 9..0x2d.  The loop's last index is 0x2c, so the
 * 37th of them is deliberately left in the behaviour the map gave it; that is
 * the boundary the cases below pin hardest, because every obvious rewrite of
 * the loop would sweep it in too.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "chevt2.h"

/* The single inclusive range the one inline loop covers, read off the
   constants at 000379e3 (9) and 000379ea (0x2c) with the signed JLE at
   00037a16. */
#define CH13_FIRST_INDEX 9
#define CH13_LAST_INDEX  0x2c

/* The last unit index chapter 13's map deploys, one past the loop's last:
   map12.dat's 37th record, which the event leaves holding position. */
#define CH13_LAST_DEPLOYED_INDEX 0x2d

/* Two records past the last deployed unit, so an off-by-one at the top end of
   the range has somewhere visible to land. */
#define CH13_STAGE_UNITS 0x30

/* The slot of data_fdps_map_cell_event_triggered_flags the one-shot handlers
   of this family latch -- element 0x10, the first the map's own event codes
   cannot reach.  This handler does not use it, and that is what is asserted. */
#define CH13_LATCH_SLOT 0x10

static struct fdps_unit_record ch13_units[CH13_STAGE_UNITS];

/* Give every record the same AI byte and point the array global at the block.
   The staged value carries a high nibble as well as a behaviour code, because
   the whole point of the merge is that only one of the two moves. */
static void stage_ch13_units(int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch13_units;
    for (i = 0; i < (int) sizeof(ch13_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH13_STAGE_UNITS; i++) {
        ch13_units[i].ai_behavior = (unsigned char) ai_behavior;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch13_units;
}

/* The record has to be 0x50 bytes with its AI byte at +0x34 for the emitted C
   to address the byte the MOV byte ptr [EAX+0x34] store at 00037a3e addresses;
   the stride is the IMUL 0x50 inside fdps_get_unit_record. */
static void ch13_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
}

/* Exactly indices 9..0x2c are rewritten and every record either side of the
   range is left as it was.  The staged 0x52 is behaviour code 2 -- hold
   position, which is what byte 0x11 of all 37 of map12.dat's deployment
   records gives its units -- under a high nibble of 0x50; the range comes out
   0x50 because the mode ORed in is 0, and the rest keep 0x52.  Indices 0..8 are
   chapter 13's nine player records, which the range deliberately starts above,
   and 0x2d..0x2f are the last deployed unit and two past it: both ends have to
   be untouched.  Index 0x2c itself has to be written, which is what makes the
   JLE inclusive rather than a bound one short. */
static void ch13_advance_clears_exactly_the_range(void)
{
    int i;

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(0);

    for (i = 0; i < CH13_STAGE_UNITS; i++) {
        if (i >= CH13_FIRST_INDEX && i <= CH13_LAST_INDEX) {
            CHECK_EQ(ch13_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch13_units[i].ai_behavior, 0x52);
        }
    }
}

/* The range covers 36 records, not 37: 0x2c - 9 + 1.  The count is asserted by
   counting the records that moved, because it is the one number every obvious
   rewrite of the loop -- over the map's deployment record count, up to the live
   unit count, or over the 36 enemies the strategy guide lists, which is a
   different set of 36 -- gets wrong at exactly one record. */
static void ch13_advance_leaves_the_last_deployed_unit(void)
{
    int i;
    int moved;

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(0);

    moved = 0;
    for (i = 0; i < CH13_STAGE_UNITS; i++) {
        if (ch13_units[i].ai_behavior == 0x50) {
            moved++;
        }
    }
    CHECK_EQ(moved, 36);
    CHECK_EQ(ch13_units[CH13_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[CH13_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
}

/* The high nibble is carried across untouched by the AND 0xf0 at 00037a33 and
   the low nibble ends at 0 whatever it held.  0x40 and 0x80 are the two AI
   flags read elsewhere, so a merge that assigned the mode whole -- or that
   masked with anything wider -- would drop them.  Expected values are the
   staged byte ANDed with 0xf0. */
static void ch13_advance_keeps_the_high_nibble(void)
{
    stage_ch13_units(0);
    ch13_units[9].ai_behavior = 0xc2;
    ch13_units[10].ai_behavior = 0x02;
    ch13_units[11].ai_behavior = 0xff;
    ch13_units[0x25].ai_behavior = 0x40;
    ch13_units[0x2c].ai_behavior = 0x8b;

    fdps_chapter_13_event_enemies_advance(0);

    CHECK_EQ(ch13_units[9].ai_behavior, 0xc0);
    CHECK_EQ(ch13_units[10].ai_behavior, 0x00);
    CHECK_EQ(ch13_units[11].ai_behavior, 0xf0);
    CHECK_EQ(ch13_units[0x25].ai_behavior, 0x40);
    CHECK_EQ(ch13_units[0x2c].ai_behavior, 0x80);
}

/* Nothing guards the loop: the instruction after the argument-slot store at
   000379dc is the first of the three constant stores, with no compare between
   them, so unlike the chapter 5 handler this one has no one-shot latch and runs
   its loop every time it is called.  The latch slot is put up before the call
   and the range still moves; the slot is also asserted unchanged, because a
   handler that had grown a latch would have written it. */
static void ch13_advance_has_no_one_shot_latch(void)
{
    stage_ch13_units(0x52);
    data_fdps_map_cell_event_triggered_flags[CH13_LATCH_SLOT] = 1;

    fdps_chapter_13_event_enemies_advance(0);

    CHECK_EQ(ch13_units[CH13_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[CH13_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH13_LATCH_SLOT], 1);

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(0);
    CHECK_EQ(ch13_units[CH13_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[CH13_LAST_INDEX].ai_behavior, 0x50);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first, so a store that landed at +0x33 or +0x35
   -- the death-script operand's high byte and ai_dest_x -- is visible; 0x55 is
   also a value whose low nibble is not already 0, so the write that should
   happen is visible too.  Both ends of the range are checked, because the
   stride the store is indexed by is the IMUL 0x50 inside fdps_get_unit_record
   and an error in it shows up furthest from the base. */
static void ch13_advance_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch13_units;
    for (i = 0; i < (int) sizeof(ch13_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_13_event_enemies_advance(0);

    CHECK_EQ(bytes[9 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[9 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[9 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x2c * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x2c * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x2c * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[8 * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x2d * 0x50 + 0x34], 0x55);
}

/* The incoming argument slot is overwritten with 0 at 000379dc and never read,
   and no loop bound comes from it, so the index the dispatcher passes cannot
   reach the result.  The death-script runner is the only path this slot is
   reached by in the shipped data and it pushes the index of the unit whose
   death is being resolved -- 0x25 for map12.dat's record 28 -- so that index is
   the realistic argument; 0x2d, 8, -1 and 30000 are the ones an
   argument-driven handler would betray itself on, and the two in-block ones are
   asserted unchanged. */
static void ch13_advance_ignores_the_unit_index_argument(void)
{
    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(0x25);
    CHECK_EQ(ch13_units[0x25].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[CH13_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(CH13_LAST_DEPLOYED_INDEX);
    CHECK_EQ(ch13_units[CH13_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
    CHECK_EQ(ch13_units[CH13_LAST_INDEX].ai_behavior, 0x50);

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(8);
    CHECK_EQ(ch13_units[8].ai_behavior, 0x52);
    CHECK_EQ(ch13_units[CH13_FIRST_INDEX].ai_behavior, 0x50);

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(-1);
    CHECK_EQ(ch13_units[CH13_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[0].ai_behavior, 0x52);

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(30000);
    CHECK_EQ(ch13_units[CH13_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[0].ai_behavior, 0x52);
}

void run_chevt2_tests(void)
{
    RUN_TEST(ch13_record_shape_matches_the_offsets);
    RUN_TEST(ch13_advance_clears_exactly_the_range);
    RUN_TEST(ch13_advance_leaves_the_last_deployed_unit);
    RUN_TEST(ch13_advance_keeps_the_high_nibble);
    RUN_TEST(ch13_advance_has_no_one_shot_latch);
    RUN_TEST(ch13_advance_touches_no_neighbouring_byte);
    RUN_TEST(ch13_advance_ignores_the_unit_index_argument);
}
