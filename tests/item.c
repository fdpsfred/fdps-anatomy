/* tests/item.c -- cover for src/item.c.
 *
 * Every expected value below is read off the assembly of
 * fdps_apply_damage_to_targets at 00026230 -- the CMP EAX,dword ptr [EBP+0x14]
 * / JL at 00026246 that makes the loop guard SIGNED, the MOV AL,byte ptr [EAX]
 * / AND EAX,0xff pairs at 0002625f and 00026278 that widen a target id
 * UNSIGNED, the MOV dword ptr [EBP+-0x4],EAX at 0002626f that keeps
 * fdps_unit_apply_damage's answer and the PUSH of it at 00026285 that hands the
 * same figure to the popup, and the single CALL 0x0001f340 at 00026290 outside
 * the loop -- together with what fdps_unit_apply_damage does to a record, read
 * off 00028460 and already pinned in tests/unitstat.c.  None of them is read
 * off the emitted C.
 *
 * HOW THE RANDOMNESS IS TAKEN OUT.  A hit rolls base_damage * 9 / 10 plus
 * (rand() % 100) * base_damage / 1000, and rand() % 100 is 0..99.  For a
 * base_damage of 10 the largest numerator the bonus can form is 990, which the
 * signed divide by 1000 truncates to 0 whatever rand() returned, so every hit
 * of 10 takes off exactly 9 and no test here depends on the generator.
 *
 * WHY EVERY TARGET IS STAGED OFF SCREEN.  This function ends by draining the
 * popup queue, and fdps_play_indicator_queue only returns at once while that
 * queue is empty: with cells queued it composes 22 frames, each waiting for the
 * timer interrupt to advance data_fdps_timer_tick_counter, and that interrupt
 * is not installed under the test runner, so the wait would never end.  The
 * fixture therefore puts every unit at tile 100,100 with the view at the map
 * origin, which is outside the window fdps_show_number_indicator culls against
 * (columns 0..12, rows -1..8), so each request is culled, the queue stays empty
 * and the drain is the no-op the empty-queue branch at 0001f340 takes.  What
 * the digits themselves look like is fdps_show_number_indicator's contract and
 * is covered in tests/indicat.c.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "item.h"

/* The stride fdps_get_unit_record multiplies by. */
#define UNIT_RECORD_STRIDE 0x50

/* Enough records that the id 0x82 the unsigned-widening case uses names one of
   them, plus a few beyond so a walk that overran would be visible. */
#define STAGE_UNITS 136

/* The id whose top bit is set: read unsigned it is unit 130, read signed it
   would be unit -126 and would fault or damage memory in front of the array. */
#define HIGH_TARGET_ID 0x82
#define HIGH_TARGET_UNIT 130

/* deploy.c's player side.  fdps_unit_apply_damage credits experience only for
   side 0, so staging every unit on the player's side keeps the enemy record
   table out of these cases entirely. */
#define PLAYER_SIDE 2

/* An ordinary roster portrait, well below the 0x3c enemy cut-off. */
#define PLAIN_PORTRAIT 5

/* Where every staged unit stands: far outside the cull window, so no popup is
   ever queued.  See the note at the top of the file. */
#define OFF_SCREEN_TILE 100

/* The fixture's HP: far enough from both ends that nothing clamps unless a
   case asks for it. */
#define START_HP 50
#define MAX_HP 100

/* base_damage * 9 / 10 with the random bonus pinned to zero. */
#define HIT_TEN 10
#define HIT_TEN_ROLL 9

/* Only ever read for its field offsets. */
static struct fdps_unit_record layout_probe;

static unsigned char unit_block[STAGE_UNITS * UNIT_RECORD_STRIDE];

static struct fdps_unit_record *unit(int unit_index)
{
    return (struct fdps_unit_record *)
        (unit_block + unit_index * UNIT_RECORD_STRIDE);
}

/* Publishes the unit array and puts every record in the same known state: on
   the player's side, on 50 of 100 HP, and standing off screen. */
static void stage(void)
{
    int i;

    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_block[i] = 0;
    }

    data_fdps_map_unit_array_ptr = unit_block;
    data_fdps_indicator_queue_count = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;

    for (i = 0; i < STAGE_UNITS; i++) {
        unit(i)->pos_x = OFF_SCREEN_TILE;
        unit(i)->pos_y = OFF_SCREEN_TILE;
        unit(i)->side = PLAYER_SIDE;
        unit(i)->portrait_id = PLAIN_PORTRAIT;
        unit(i)->level = 4;
        unit(i)->hp_current = START_HP;
        unit(i)->hp_max = MAX_HP;
    }
}

/* The offsets the index arithmetic and the fixture both depend on, from the
   layout ticket 17 settled. */
static void the_record_layout_matches_the_offsets_read(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), UNIT_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 0x06);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) sizeof(layout_probe.hp_current), 2);
}

/* The loop walks the whole list and hits every id in it: three targets each
   lose the rolled 9, and the units either side of them are left alone, so a
   walk that strayed by one record would show. */
static void every_listed_target_takes_the_rolled_damage(void)
{
    unsigned char target_ids[3];

    stage();
    target_ids[0] = 1;
    target_ids[1] = 2;
    target_ids[2] = 3;

    fdps_apply_damage_to_targets(3, target_ids, HIT_TEN);

    CHECK_EQ(unit(0)->hp_current, START_HP);
    CHECK_EQ(unit(1)->hp_current, START_HP - HIT_TEN_ROLL);
    CHECK_EQ(unit(2)->hp_current, START_HP - HIT_TEN_ROLL);
    CHECK_EQ(unit(3)->hp_current, START_HP - HIT_TEN_ROLL);
    CHECK_EQ(unit(4)->hp_current, START_HP);
}

/* Nothing deduplicates the list: the loop applies the effect once per entry, so
   a unit named twice is damaged twice. */
static void a_target_listed_twice_is_hit_twice(void)
{
    unsigned char target_ids[2];

    stage();
    target_ids[0] = 1;
    target_ids[1] = 1;

    fdps_apply_damage_to_targets(2, target_ids, HIT_TEN);

    CHECK_EQ(unit(1)->hp_current, START_HP - 2 * HIT_TEN_ROLL);
}

/* MOV AL,byte ptr [EAX] / AND EAX,0xff: the id is a byte widened UNSIGNED, so
   0x82 names unit 130.  A signed read would name unit -126 and land in front of
   the array. */
static void a_target_id_above_127_is_widened_unsigned(void)
{
    unsigned char target_ids[1];

    stage();
    target_ids[0] = HIGH_TARGET_ID;

    fdps_apply_damage_to_targets(1, target_ids, HIT_TEN);

    CHECK_EQ(unit(HIGH_TARGET_UNIT)->hp_current, START_HP - HIT_TEN_ROLL);
    CHECK_EQ(unit(0)->hp_current, START_HP);
    CHECK_EQ(unit(1)->hp_current, START_HP);
    CHECK_EQ(unit(2)->hp_current, START_HP);
}

/* The guard is tested before the body, so a count of zero hits nothing at all
   -- and the drain that follows is still reached with an empty queue, which is
   what makes the unconditional call at 00026290 cost nothing. */
static void a_target_count_of_zero_hits_nothing(void)
{
    unsigned char target_ids[1];

    stage();
    target_ids[0] = 1;

    fdps_apply_damage_to_targets(0, target_ids, HIT_TEN);

    CHECK_EQ(unit(1)->hp_current, START_HP);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
}

/* JL at 00026249 is the SIGNED branch: a negative count fails the guard on the
   first test.  Read unsigned it would be a walk of billions of entries. */
static void a_negative_target_count_hits_nothing(void)
{
    unsigned char target_ids[1];

    stage();
    target_ids[0] = 1;

    fdps_apply_damage_to_targets(-1, target_ids, HIT_TEN);

    CHECK_EQ(unit(1)->hp_current, START_HP);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
}

/* The clamp belongs to fdps_unit_apply_damage and it is per target: a unit with
   less HP than the roll is left on 0 while the target listed beside it takes
   its full 9. */
static void a_target_with_too_little_hp_is_left_at_zero(void)
{
    unsigned char target_ids[2];

    stage();
    unit(1)->hp_current = 5;
    target_ids[0] = 1;
    target_ids[1] = 2;

    fdps_apply_damage_to_targets(2, target_ids, HIT_TEN);

    CHECK_EQ(unit(1)->hp_current, 0);
    CHECK_EQ(unit(2)->hp_current, START_HP - HIT_TEN_ROLL);
}

/* This function queues nothing of its own and the drain empties the queue, so
   whatever the run of popups did, the cursor is back at zero when it returns.
   Here every request was culled, so it never left zero. */
static void the_popup_queue_is_empty_when_the_effect_returns(void)
{
    unsigned char target_ids[3];

    stage();
    target_ids[0] = 1;
    target_ids[1] = 2;
    target_ids[2] = 3;

    fdps_apply_damage_to_targets(3, target_ids, HIT_TEN);

    CHECK_EQ(data_fdps_indicator_queue_count, 0);
}

void run_item_tests(void)
{
    RUN_TEST(the_record_layout_matches_the_offsets_read);
    RUN_TEST(every_listed_target_takes_the_rolled_damage);
    RUN_TEST(a_target_listed_twice_is_hit_twice);
    RUN_TEST(a_target_id_above_127_is_widened_unsigned);
    RUN_TEST(a_target_count_of_zero_hits_nothing);
    RUN_TEST(a_negative_target_count_hits_nothing);
    RUN_TEST(a_target_with_too_little_hp_is_left_at_zero);
    RUN_TEST(the_popup_queue_is_empty_when_the_effect_returns);
}
