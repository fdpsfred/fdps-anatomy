/* tests/item.c -- cover for src/item.c.
 *
 * The file is in two sections, one per effect path.  This first note and the
 * fixture under it belong to fdps_apply_damage_to_targets; the banner further
 * down opens fdps_apply_heal_to_targets, which reuses this fixture and adds
 * what its two animations need.
 *
 * Every expected value in this section is read off the assembly of
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
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
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

/* ---- fdps_apply_heal_to_targets, 00026fd0 --------------------------------
 *
 * Every expected value below is read off the assembly at 00026fd0 -- the two
 * CALLs at 00026fea and 00026fff that sit ABOVE the counter's initialisation at
 * 00027007, so both animations are paid for before the loop guard exists; the
 * CMP EAX,dword ptr [EBP+0x14] / JL at 00027011 that makes the guard SIGNED;
 * the MOV AL,byte ptr [EAX] / AND EAX,0xff pairs at 00027027 and 00027040 that
 * widen a target id UNSIGNED; the MOV dword ptr [EBP+-0x4],EAX at 0002703a that
 * keeps fdps_unit_apply_heal's answer; and the single CALL 0x0001f340 at
 * 0002705b outside the loop -- together with what fdps_unit_apply_heal does to
 * a record, read off 00027070 and already pinned in tests/unitstat.c.  None of
 * them is read off the emitted C.
 *
 * HOW THE RANDOMNESS IS TAKEN OUT.  A heal rolls base_heal * 9 / 10 plus
 * (rand() % 100) * base_heal / 1000, and rand() % 100 is 0..99.  For a
 * base_heal of 10 the largest numerator the bonus can form is 990, which the
 * signed divide by 1000 truncates to 0 whatever rand() returned, so every heal
 * of 10 restores exactly 9 and no case here depends on the generator.
 *
 * WHY THESE CASES DRIVE THE REAL ANIMATIONS.  Unlike the damage path this
 * function calls two routines that do not come back until the adapter and the
 * timer have moved on: fdps_play_vfs_animation_over_units loads Posion.saf out
 * of the shipped MISC.VFS and holds each of its frames for two timer ticks, and
 * fdps_flash_units_in_color composes eight more.  Neither can be stood in for
 * -- the container's name is a literal inside the callee and a member it cannot
 * find ends the process -- so the cases stage what the game stages: the adapter
 * in mode 13h and an IRQ0 handler advancing data_fdps_timer_tick_counter, and
 * they skip themselves when the container is not staged next to the executable
 * (tests/gamefile.lst).
 *
 * WHY EVERY TARGET IS STILL OFF SCREEN.  Two reasons, and they are the damage
 * cases' reason plus one.  The popup queue: this function ends by draining it,
 * and fdps_play_indicator_queue only returns at once while it is empty, so
 * every target sits at tile 100,100 with the view at the map origin, outside
 * the window fdps_show_number_indicator culls against (columns 0..12, rows
 * -1..8), and no cell is ever queued.  The animations: a copy of the clip and a
 * flashed sprite are both placed from the same tile, so at that distance every
 * one of them fails the strict placement test in fdps_draw_tilemap_cell and
 * fdps_blit_unit_sprite and nothing is drawn into the page at all.  That is
 * what lets these cases run with no sprite cache and no scene layers staged.
 *
 * WHAT IS NOT COVERED.  The glyph base 0x27 and the value handed to the popup
 * reach the queue only for a target inside the cull window, and animating that
 * queue costs 22 more frames plus a half-second tail and a Number.cel sheet
 * these cases have no other use for; what a queued number looks like is
 * fdps_show_number_indicator's contract and is covered in tests/indicat.c.
 * Which pixels either animation paints is anim.c's and indicat.c's business and
 * is covered in their own files against clips of their own.
 * ------------------------------------------------------------------ */

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the two animations spin on the counter. */
#define HEAL_TIMER_VECTOR 8

#define HEAL_MODE_TEXT 0x03
#define HEAL_MODE_320X200X256 0x13

/* The container the clip is a member of.  Only its presence is tested here; the
   name is a literal inside fdps_play_vfs_animation_over_units, so there is
   nothing to point at a smaller file. */
#define HEAL_ARCHIVE "MISC.VFS"

/* POSION.SAF's own frame count, out of the member's header at +0x0c, and the
   two ticks fdps_play_vfs_animation_over_units holds every frame for; then the
   eight frames fdps_flash_units_in_color composes.  Each of those frames ends
   on one change of the timer tick, and the FIRST wait of each of the two
   routines may end at once because its latch is deliberately uninitialised
   (anim.h, indicat.h) -- so what can be asserted is a floor, one tick short at
   each end.  A frame can also outlast a tick, so there is no ceiling to
   assert. */
#define HEAL_CLIP_FRAMES 18
#define HEAL_CLIP_TICKS_PER_FRAME 2
#define HEAL_FLASH_FRAMES 8
#define HEAL_TICK_FLOOR (HEAL_CLIP_FRAMES * HEAL_CLIP_TICKS_PER_FRAME - 1 \
                         + HEAL_FLASH_FRAMES - 1)

/* base_heal * 9 / 10 with the random bonus pinned to zero. */
#define HEAL_TEN 10
#define HEAL_TEN_ROLL 9

/* A target two HP short of full, so the clamp in fdps_unit_apply_heal is what
   decides where it ends up and the roll is not. */
#define NEARLY_FULL_HP (MAX_HP - 2)

static void (__interrupt __far *heal_saved_timer)();
static unsigned int heal_ticks_used;

static void __interrupt __far heal_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(heal_saved_timer);
}

static void heal_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Whether the container the clip lives in was staged.  A case that ran without
   it would not fail an assertion -- fdps_vfs_load_entry ends the process on a
   member it cannot find -- so the cases skip instead. */
static int heal_archive_present(void)
{
    FILE *fp;

    fp = fopen(HEAL_ARCHIVE, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* The damage fixture plus the four globals the compositor reads: nothing on the
   map, no scene layers and no cursor overlay, so the scene repaint inside each
   animation frame writes nothing into its page. */
static void heal_stage(void)
{
    stage();
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_map_unit_walk_anim_counter = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
}

/* Put the staged globals back the way a freshly started program has them, for
   the reason tests/anim.c gives: the array pointer holds storage the game's own
   loaders free, and a later unit that expects an empty battle would otherwise
   inherit this fixture. */
static void heal_unstage(void)
{
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
}

/* One whole call, with the adapter in the mode the game plays it in and a timer
   interrupt running so the frame waits can end, leaving how many ticks it cost
   in heal_ticks_used. */
static void heal_run(int target_count, unsigned char *target_ids)
{
    unsigned int before_ticks;

    heal_set_mode(HEAL_MODE_320X200X256);
    heal_saved_timer = _dos_getvect(HEAL_TIMER_VECTOR);
    _dos_setvect(HEAL_TIMER_VECTOR, heal_timer_isr);
    before_ticks = data_fdps_timer_tick_counter;
    fdps_apply_heal_to_targets(target_count, target_ids, HEAL_TEN);
    heal_ticks_used = data_fdps_timer_tick_counter - before_ticks;
    _dos_setvect(HEAL_TIMER_VECTOR, heal_saved_timer);
    heal_set_mode(HEAL_MODE_TEXT);
}

/* The loop walks the whole list and heals every id in it; a unit named twice is
   healed twice, because nothing here deduplicates the list, and the units
   either side of the named ones are left alone, so a walk that strayed by one
   record would show. */
static void every_listed_target_takes_the_rolled_heal(void)
{
    unsigned char target_ids[4];

    if (!heal_archive_present()) {
        return;
    }
    heal_stage();
    target_ids[0] = 1;
    target_ids[1] = 2;
    target_ids[2] = 2;
    target_ids[3] = 3;

    heal_run(4, target_ids);

    CHECK_EQ(unit(0)->hp_current, START_HP);
    CHECK_EQ(unit(1)->hp_current, START_HP + HEAL_TEN_ROLL);
    CHECK_EQ(unit(2)->hp_current, START_HP + 2 * HEAL_TEN_ROLL);
    CHECK_EQ(unit(3)->hp_current, START_HP + HEAL_TEN_ROLL);
    CHECK_EQ(unit(4)->hp_current, START_HP);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
    heal_unstage();
}

/* MOV AL,byte ptr [EAX] / AND EAX,0xff: the id is a byte widened UNSIGNED, so
   0x82 names unit 130.  A signed read would name unit -126 and land in front of
   the array -- and it would do so inside the clip's own per-unit loop as well,
   which reads the list the same way. */
static void a_heal_target_id_above_127_is_widened_unsigned(void)
{
    unsigned char target_ids[1];

    if (!heal_archive_present()) {
        return;
    }
    heal_stage();
    target_ids[0] = HIGH_TARGET_ID;

    heal_run(1, target_ids);

    CHECK_EQ(unit(HIGH_TARGET_UNIT)->hp_current, START_HP + HEAL_TEN_ROLL);
    CHECK_EQ(unit(0)->hp_current, START_HP);
    CHECK_EQ(unit(1)->hp_current, START_HP);
    CHECK_EQ(unit(2)->hp_current, START_HP);
    heal_unstage();
}

/* The clamp belongs to fdps_unit_apply_heal and it is per target: a unit whose
   maximum is nearer than the roll stops at its maximum while the target listed
   beside it takes its full 9. */
static void a_target_near_its_maximum_hp_stops_at_the_maximum(void)
{
    unsigned char target_ids[2];

    if (!heal_archive_present()) {
        return;
    }
    heal_stage();
    unit(1)->hp_current = NEARLY_FULL_HP;
    target_ids[0] = 1;
    target_ids[1] = 2;

    heal_run(2, target_ids);

    CHECK_EQ(unit(1)->hp_current, MAX_HP);
    CHECK_EQ(unit(2)->hp_current, START_HP + HEAL_TEN_ROLL);
    heal_unstage();
}

/* THE TWO ANIMATIONS ARE AHEAD OF THE LOOP GUARD.  With a count of zero nothing
   is healed and nothing is queued, and the call still costs the clip's 36 ticks
   and the flash's eight: a rebuild that returned early on an empty list, or put
   either call inside the loop, would come back in no time at all. */
static void a_target_count_of_zero_heals_nothing_and_still_animates(void)
{
    unsigned char target_ids[1];

    if (!heal_archive_present()) {
        return;
    }
    heal_stage();
    target_ids[0] = 1;

    heal_run(0, target_ids);

    CHECK_EQ(unit(1)->hp_current, START_HP);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
    CHECK_EQ(heal_ticks_used >= (unsigned int) HEAL_TICK_FLOOR, 1);
    heal_unstage();
}

/* JL at 00027014 is the SIGNED branch: a negative count fails the guard on the
   first test.  Read unsigned it would be a walk of billions of entries.  The
   animations are ahead of it and run in full here too. */
static void a_negative_heal_target_count_heals_nothing(void)
{
    unsigned char target_ids[1];

    if (!heal_archive_present()) {
        return;
    }
    heal_stage();
    target_ids[0] = 1;

    heal_run(-1, target_ids);

    CHECK_EQ(unit(1)->hp_current, START_HP);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
    CHECK_EQ(heal_ticks_used >= (unsigned int) HEAL_TICK_FLOOR, 1);
    heal_unstage();
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
    RUN_TEST(every_listed_target_takes_the_rolled_heal);
    RUN_TEST(a_heal_target_id_above_127_is_widened_unsigned);
    RUN_TEST(a_target_near_its_maximum_hp_stops_at_the_maximum);
    RUN_TEST(a_target_count_of_zero_heals_nothing_and_still_animates);
    RUN_TEST(a_negative_heal_target_count_heals_nothing);
}
