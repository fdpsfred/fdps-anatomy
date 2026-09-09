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

/* ------------------------------------------------------------------
 * fdps_apply_item_effect_to_targets @ 000262a0
 *
 * Every expected value below is read off the assembly at 000262a0 -- the MOV
 * dword ptr [0x00064378],0x0 at 000262ac that clears the popup queue before
 * anything else; the ADD EDX,EDX / MOV AL,byte ptr [EDX+0xb] at 000262fa that
 * turns a bag slot into an item id; the MOVSX EAX,word ptr [EAX+0xe] at
 * 00026328 that reads the use amount as a SIGNED word and the MOV AL,byte ptr
 * [EAX+0xd] at 00026332 that reads the use effect; the CMP EAX,0x1e / JNZ at
 * 00026a5e that opens the beam-cannon arm and the CMP EAX,dword ptr [EBP+0x1c]
 * / JL at 00026a71 that makes its guard SIGNED; the MOV AL,byte ptr [EAX] / AND
 * EAX,0xff pairs inside that arm that widen a target id UNSIGNED; the absence
 * of any fdps_unit_remove_item call inside it; and the MOV dword ptr
 * [0x00069cec],0x0 at 00026dbc that opens the death settlement every path
 * reaches.  None of them is read off the emitted C.
 *
 * WHICH EFFECT CODES THESE CASES CAN DRIVE, AND WHY ONLY THOSE.  Eighteen of
 * the twenty-two arms open with fdps_play_vfs_animation_over_units, a message
 * window or a 25-frame view shake, none of which come back without the adapter,
 * the timer interrupt and, for three of them, an answer from the player.  Two
 * do not: 0x1e, the beam cannons, which walks the list applying damage and
 * draining the queue, and the four codes that match no arm at all -- 0x05,
 * 0x06, 0x0d and 0x1b -- which fall straight through to the death settlement.
 * Those two are what is exercised here.  The eighteen animated arms are the
 * same three calls over and over with a different clip name and a different
 * field; what those calls do is anim.c's, indicat.c's and msgwin.c's contract
 * and is covered in their own files.
 *
 * WHY NOTHING IS DEAD ON THE MAP.  The function ends by collecting death
 * scripts, playing the destruction sequence and running the scripts, and the
 * middle one composes explosion frames against the timer.  data_fdps_map_unit_-
 * count is staged at 0, so the collect writes nothing, the sequence finds
 * nothing dying and returns at its own count test, and fdps_run_death_scripts
 * returns on its script_count of 0.  What the three of them do with real
 * casualties is death.c's contract and is covered in tests/death.c.
 *
 * WHY EVERY TARGET IS STILL OFF SCREEN.  The beam-cannon arm queues a popup per
 * target and then drains the queue, and fdps_play_indicator_queue only returns
 * at once while that queue is empty.  The fixture above puts every unit at tile
 * 100,100 with the view at the map origin, outside the window
 * fdps_show_number_indicator culls against, so every request is dropped and the
 * drain is the empty-queue no-op.
 * ------------------------------------------------------------------ */

/* The stride fdps_get_item_record multiplies by, out of ITEM.DAT. */
#define ITEM_RECORD_STRIDE 0x17

/* Enough records that every id these cases use names one of them. */
#define STAGE_ITEMS 10

/* The bag entry the id is the second byte of: record +0x0b + 2 * slot. */
#define BAG_ENTRY_ID_INDEX(slot) ((slot) * 2 + 1)

/* The two effect codes these cases can drive: the weapon-borne direct damage,
   and one of the four that match no arm in the dispatch. */
#define EFFECT_BEAM_CANNON 0x1e
#define EFFECT_UNHANDLED 0x05

/* Which staged item carries which.  Neither is id 0: a read of the wrong bag
   byte finds a zero, and record 0 is left with a use effect of 0 so that
   mistake shows up as nothing happening rather than as the same thing
   happening. */
#define BEAM_ITEM_ID 3
#define INERT_ITEM_ID 5

/* The four records the animated cases further down use: the permanent
   maximum-HP item, the consumable HP restore, the healing weapon that shares
   its effect, and the earth item whose arm shakes the view. */
#define MAX_HP_ITEM_ID 6
#define HEAL_ITEM_ID 7
#define HEAL_WEAPON_ID 8
#define EARTH_ITEM_ID 9
#define EFFECT_MAX_HP_UP 0x0f
#define EFFECT_RESTORE_HP_ITEM 0x0b
#define EFFECT_RESTORE_HP_WEAPON 0x20
#define EFFECT_EARTH_ITEM 0x04

/* Where the view is parked before the quake, so that "put back" is a statement
   about two particular numbers and not about zero. */
#define QUAKE_ORIGIN_X 40
#define QUAKE_ORIGIN_Y 30

/* What the shake is allowed to reach on each axis, and how many of its 25
   frames the sampling interrupt has to have caught for the bounds above to mean
   anything.  SUB EDX,0x2 after an IDIV by 4 at 000264c8 and 000264e1 makes the
   offset rand() % 4 - 2, which is -2, -1, 0 or +1: it must NEVER reach +2, and
   it must reach the negative side, which together are what say the jitter is
   the asymmetric one and not the symmetric -2..+2 the shape invites.  The floor
   on samples is the same generous one tests/spell.c uses for the identical
   shake in 裂地術; both axes are zero on one frame in sixteen and the interrupt
   sees roughly one frame each. */
#define QUAKE_FRAMES 0x19
#define QUAKE_MIN_SAMPLES 15
#define QUAKE_MAX_OFFSET 1
#define QUAKE_MIN_OFFSET (-1)

/* ADD word ptr [EAX+0x42],0xf at 000265ef: the amount is a literal in the
   instruction.  The record these cases hand it carries a use amount of
   MAX_HP_DECOY_AMOUNT instead, which is what the item's own field would
   contribute if the emitted C read it -- every real item of this kind carries 0
   there (assets/items.md), so a `hp_max += use_amount` would be invisible
   against the shipped table and shows up loudly against this one. */
#define MAX_HP_UP_AMOUNT 15
#define MAX_HP_DECOY_AMOUNT 999

/* What fdps_unit_remove_item leaves behind: the entries above the removed one
   shift down two bytes and the last entry's flag byte becomes 0x80, so the id
   byte of the slot the item came out of reads 0 once the bag has closed up. */
#define BAG_ENTRY_ID_AFTER_REMOVAL 0

/* Which unit uses the item and which bag slot it comes out of.  Slot 3 is not
   slot 0: an index that dropped the doubling or the +1 would land on a byte
   these cases deliberately leave holding a different id. */
#define ACTING_UNIT 0
#define ACTING_SLOT 3
#define DECOY_SLOT 2

/* base_damage * 9 / 10 with the random bonus pinned to zero, as the damage
   section above establishes for a base of 10. */
#define USE_TEN 10
#define USE_TEN_ROLL 9

/* The same base with the sign bit set.  Read as the SIGNED word the MOVSX at
   00026328 makes it, -10 rolls -9 and fdps_unit_apply_damage's subtraction puts
   9 HP ON the target; read as an unsigned word it would be 65526 and read as a
   byte 246, and either would floor the target at 0.  That gap is the whole
   assertion. */
#define USE_MINUS_TEN (-10)
#define USE_MINUS_TEN_GAIN 9

/* Values the two "cleared on entry" cases park in the globals first, so a
   store that never happened is visible as the value still sitting there. */
#define QUEUE_COUNT_SENTINEL 7
#define XP_CREDIT_SENTINEL 5

static unsigned char item_block[STAGE_ITEMS * ITEM_RECORD_STRIDE];

static struct fdps_item_effect *item(int item_id)
{
    return (struct fdps_item_effect *)
        (item_block + item_id * ITEM_RECORD_STRIDE);
}

/* The ITEM.DAT these cases run against, and the acting unit's bag: the
   beam-cannon item in ACTING_SLOT and the inert one in DECOY_SLOT.  Record 0 is
   left with a use effect of 0 on purpose; see BEAM_ITEM_ID above. */
static void use_items_stage(void)
{
    int i;

    for (i = 0; i < (int) sizeof(item_block); i++) {
        item_block[i] = 0;
    }
    data_fdps_item_effect_table_ptr = item_block;
    data_fdps_map_unit_count = 0;
    data_fdps_battle_pending_xp_credit = 0;

    item(BEAM_ITEM_ID)->use_effect = EFFECT_BEAM_CANNON;
    item(BEAM_ITEM_ID)->use_amount = USE_TEN;
    item(INERT_ITEM_ID)->use_effect = EFFECT_UNHANDLED;
    item(INERT_ITEM_ID)->use_amount = USE_TEN;
    item(MAX_HP_ITEM_ID)->use_effect = EFFECT_MAX_HP_UP;
    item(MAX_HP_ITEM_ID)->use_amount = MAX_HP_DECOY_AMOUNT;
    item(HEAL_ITEM_ID)->use_effect = EFFECT_RESTORE_HP_ITEM;
    item(HEAL_ITEM_ID)->use_amount = USE_TEN;
    item(HEAL_WEAPON_ID)->use_effect = EFFECT_RESTORE_HP_WEAPON;
    item(HEAL_WEAPON_ID)->use_amount = USE_TEN;
    item(EARTH_ITEM_ID)->use_effect = EFFECT_EARTH_ITEM;
    item(EARTH_ITEM_ID)->use_amount = USE_TEN;

    unit(ACTING_UNIT)->inventory_slots[BAG_ENTRY_ID_INDEX(ACTING_SLOT)] =
        BEAM_ITEM_ID;
    unit(ACTING_UNIT)->inventory_slots[BAG_ENTRY_ID_INDEX(DECOY_SLOT)] =
        INERT_ITEM_ID;
}

/* The damage fixture plus that table and that bag, for the cases whose arm
   touches neither the adapter nor the timer. */
static void use_stage(void)
{
    stage();
    use_items_stage();
}

/* The animation fixture plus the same table and bag, for the two cases below
   whose arm opens with fdps_play_vfs_animation_over_units.  heal_stage is what
   leaves the compositor with nothing to draw. */
static void use_stage_animated(void)
{
    heal_stage();
    use_items_stage();
}

/* One whole call with the adapter in the mode the game plays it in and a timer
   interrupt running, so the clip's frame waits can end. */
static void use_run_animated(int item_slot, int target_count,
                             unsigned char *target_ids)
{
    heal_set_mode(HEAL_MODE_320X200X256);
    heal_saved_timer = _dos_getvect(HEAL_TIMER_VECTOR);
    _dos_setvect(HEAL_TIMER_VECTOR, heal_timer_isr);
    fdps_apply_item_effect_to_targets(ACTING_UNIT, item_slot, target_count,
                                      target_ids);
    _dos_setvect(HEAL_TIMER_VECTOR, heal_saved_timer);
    heal_set_mode(HEAL_MODE_TEXT);
}

/* Put back what the fixture published, for the reason heal_unstage gives. */
static void use_unstage(void)
{
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_map_unit_count = 0;
}

/* The offsets the dispatch reads and the stride the table lookup multiplies by,
   from the layouts ticket 17 settled. */
static void the_item_record_layout_matches_the_offsets_read(void)
{
    CHECK_EQ((int) sizeof(struct fdps_item_effect), ITEM_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_effect), 0x0d);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_amount), 0x0e);
    CHECK_EQ((int) sizeof(item(0)->use_amount), 2);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots), 0x0a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 0x07);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, char_id), 0x08);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_max), 0x46);
}

/* The beam-cannon arm walks the whole list and hits every id in it with the
   record's use amount, and the units either side are left alone. */
static void a_beam_cannon_damages_every_listed_target(void)
{
    unsigned char target_ids[3];

    use_stage();
    target_ids[0] = 1;
    target_ids[1] = 2;
    target_ids[2] = 3;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, ACTING_SLOT, 3,
                                      target_ids);

    CHECK_EQ(unit(0)->hp_current, START_HP);
    CHECK_EQ(unit(1)->hp_current, START_HP - USE_TEN_ROLL);
    CHECK_EQ(unit(2)->hp_current, START_HP - USE_TEN_ROLL);
    CHECK_EQ(unit(3)->hp_current, START_HP - USE_TEN_ROLL);
    CHECK_EQ(unit(4)->hp_current, START_HP);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
    use_unstage();
}

/* 0x1e is one of the weapon-borne codes: no arm of the dispatch consumes it, so
   the bag entry the effect was read out of is still there afterwards.  Its item
   twin would have been removed at this point. */
static void a_beam_cannon_is_not_taken_out_of_the_bag(void)
{
    unsigned char target_ids[1];

    use_stage();
    target_ids[0] = 1;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, ACTING_SLOT, 1,
                                      target_ids);

    CHECK_EQ(unit(ACTING_UNIT)
                 ->inventory_slots[BAG_ENTRY_ID_INDEX(ACTING_SLOT)],
             BEAM_ITEM_ID);
    CHECK_EQ(unit(ACTING_UNIT)
                 ->inventory_slots[BAG_ENTRY_ID_INDEX(DECOY_SLOT)],
             INERT_ITEM_ID);
    use_unstage();
}

/* ADD EDX,EDX / ADD EDX,record / MOV AL,byte ptr [EDX+0xb]: the id is the
   SECOND byte of the two-byte bag entry, so the subscript is 2 * slot + 1.  The
   same call made against the decoy slot finds the inert item and does nothing,
   which is what makes the first half of this case a statement about the index
   rather than about the effect. */
static void the_item_id_comes_from_the_second_byte_of_the_bag_entry(void)
{
    unsigned char target_ids[1];

    use_stage();
    target_ids[0] = 1;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, ACTING_SLOT, 1,
                                      target_ids);
    CHECK_EQ(unit(1)->hp_current, START_HP - USE_TEN_ROLL);

    use_stage();
    target_ids[0] = 1;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, DECOY_SLOT, 1,
                                      target_ids);
    CHECK_EQ(unit(1)->hp_current, START_HP);
    use_unstage();
}

/* MOVSX EAX,word ptr [EAX+0xe]: the use amount is a SIGNED word.  A negative
   one rolls a negative figure, and fdps_unit_apply_damage subtracts it, so the
   target gains HP instead of losing it.  Read unsigned, or read as a byte, the
   same record would floor the target at zero. */
static void the_use_amount_is_read_as_a_signed_word(void)
{
    unsigned char target_ids[1];

    use_stage();
    item(BEAM_ITEM_ID)->use_amount = USE_MINUS_TEN;
    target_ids[0] = 1;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, ACTING_SLOT, 1,
                                      target_ids);

    CHECK_EQ(unit(1)->hp_current, START_HP + USE_MINUS_TEN_GAIN);
    use_unstage();
}

/* MOV AL,byte ptr [EAX] / AND EAX,0xff inside the beam-cannon loop: an id is a
   byte widened UNSIGNED, so 0x82 names unit 130.  A signed read would name unit
   -126 and land in front of the array. */
static void a_beam_cannon_target_id_above_127_is_widened_unsigned(void)
{
    unsigned char target_ids[1];

    use_stage();
    target_ids[0] = HIGH_TARGET_ID;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, ACTING_SLOT, 1,
                                      target_ids);

    CHECK_EQ(unit(HIGH_TARGET_UNIT)->hp_current, START_HP - USE_TEN_ROLL);
    CHECK_EQ(unit(0)->hp_current, START_HP);
    CHECK_EQ(unit(1)->hp_current, START_HP);
    use_unstage();
}

/* JL at 00026a74 is the SIGNED branch: a count of zero fails the guard on the
   first test and so does a negative one.  Read unsigned the second would be a
   walk of billions of entries. */
static void a_beam_cannon_with_no_targets_hits_nothing(void)
{
    unsigned char target_ids[1];

    use_stage();
    target_ids[0] = 1;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, ACTING_SLOT, 0,
                                      target_ids);
    CHECK_EQ(unit(1)->hp_current, START_HP);

    fdps_apply_item_effect_to_targets(ACTING_UNIT, ACTING_SLOT, -1,
                                      target_ids);
    CHECK_EQ(unit(1)->hp_current, START_HP);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
    use_unstage();
}

/* 0x05 matches no arm of the dispatch, so using the item plays nothing, changes
   nothing and does not consume it.  0x06, 0x0d and 0x1b are the same silence,
   and it is the original's behaviour rather than a gap. */
static void an_unhandled_use_effect_does_nothing_at_all(void)
{
    unsigned char target_ids[2];

    use_stage();
    target_ids[0] = 1;
    target_ids[1] = 2;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, DECOY_SLOT, 2,
                                      target_ids);

    CHECK_EQ(unit(1)->hp_current, START_HP);
    CHECK_EQ(unit(2)->hp_current, START_HP);
    CHECK_EQ(unit(ACTING_UNIT)
                 ->inventory_slots[BAG_ENTRY_ID_INDEX(DECOY_SLOT)],
             INERT_ITEM_ID);
    use_unstage();
}

/* MOV dword ptr [0x00064378],0x0 at 000262ac, the very first store the function
   makes: whatever the previous action left queued is thrown away before this
   effect starts filling the queue.  The unhandled arm is what makes the case
   read only that store -- nothing on that path queues a cell or drains the
   queue, so a count of zero afterwards can have come from nowhere else. */
static void the_popup_queue_count_is_cleared_on_entry(void)
{
    unsigned char target_ids[1];

    use_stage();
    target_ids[0] = 1;
    data_fdps_indicator_queue_count = QUEUE_COUNT_SENTINEL;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, DECOY_SLOT, 1,
                                      target_ids);

    CHECK_EQ(data_fdps_indicator_queue_count, 0);
    use_unstage();
}

/* MOV dword ptr [0x00069cec],0x0 at 00026dbc: the death settlement opens by
   zeroing the experience the kills are about to credit, and it is reached from
   every arm -- including the unhandled one this case uses, which does nothing
   else observable at all. */
static void the_pending_xp_credit_is_cleared_before_the_settlement(void)
{
    unsigned char target_ids[1];

    use_stage();
    target_ids[0] = 1;
    data_fdps_battle_pending_xp_credit = XP_CREDIT_SENTINEL;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, DECOY_SLOT, 1,
                                      target_ids);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);

    use_stage();
    data_fdps_battle_pending_xp_credit = XP_CREDIT_SENTINEL;

    fdps_apply_item_effect_to_targets(ACTING_UNIT, ACTING_SLOT, 1,
                                      target_ids);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    use_unstage();
}

/* The permanent maximum-HP item, driven through its real Cure.saf clip.  Three
   things this case pins and nothing else can:

   THE AMOUNT IS THE CODE'S AND NOT THE ITEM'S.  The record handed over carries
   a use amount of 999 and the target gains exactly 15.  Every shipped item of
   this kind has 0 in that field, so `hp_max += use_amount` would look right and
   do nothing on a real ITEM.DAT.

   ONLY target_ids[0] IS TOUCHED.  The arm resolves one record, from
   *target_ids, however long the list is; the other two listed targets are left
   where they were.

   THE ITEM IS CONSUMED.  0x0f has no weapon twin, so unlike the beam cannon it
   reaches fdps_unit_remove_item and the bag closes up over the slot. */
static void a_max_hp_item_adds_its_own_constant_to_target_zero_only(void)
{
    unsigned char target_ids[3];

    if (!heal_archive_present()) {
        return;
    }
    use_stage_animated();
    unit(ACTING_UNIT)->inventory_slots[BAG_ENTRY_ID_INDEX(ACTING_SLOT)] =
        MAX_HP_ITEM_ID;
    target_ids[0] = 1;
    target_ids[1] = 2;
    target_ids[2] = 3;

    use_run_animated(ACTING_SLOT, 3, target_ids);

    CHECK_EQ(unit(1)->hp_max, MAX_HP + MAX_HP_UP_AMOUNT);
    CHECK_EQ(unit(2)->hp_max, MAX_HP);
    CHECK_EQ(unit(3)->hp_max, MAX_HP);
    CHECK_EQ(unit(1)->hp_current, START_HP);
    CHECK_EQ(unit(ACTING_UNIT)
                 ->inventory_slots[BAG_ENTRY_ID_INDEX(ACTING_SLOT)],
             BAG_ENTRY_ID_AFTER_REMOVAL);
    CHECK_EQ(unit(ACTING_UNIT)
                 ->inventory_slots[BAG_ENTRY_ID_INDEX(DECOY_SLOT)],
             INERT_ITEM_ID);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
    heal_unstage();
    use_unstage();
}

/* 0x0b and 0x20 are the same effect twice: both hand the whole list to
   fdps_apply_heal_to_targets with the record's amount, and the CMP EAX,0xb /
   JNZ at 00026847 after it is the only thing that separates them.  The
   consumable loses the bag entry and the healing staff keeps it, and each half
   of this case would pass on its own if the second test were dropped -- it is
   the pair that says the distinction survives. */
static void a_healing_item_is_consumed_and_a_healing_weapon_is_not(void)
{
    unsigned char target_ids[1];

    if (!heal_archive_present()) {
        return;
    }
    use_stage_animated();
    unit(ACTING_UNIT)->inventory_slots[BAG_ENTRY_ID_INDEX(ACTING_SLOT)] =
        HEAL_ITEM_ID;
    target_ids[0] = 1;

    use_run_animated(ACTING_SLOT, 1, target_ids);

    CHECK_EQ(unit(1)->hp_current, START_HP + HEAL_TEN_ROLL);
    CHECK_EQ(unit(ACTING_UNIT)
                 ->inventory_slots[BAG_ENTRY_ID_INDEX(ACTING_SLOT)],
             BAG_ENTRY_ID_AFTER_REMOVAL);

    use_stage_animated();
    unit(ACTING_UNIT)->inventory_slots[BAG_ENTRY_ID_INDEX(ACTING_SLOT)] =
        HEAL_WEAPON_ID;
    target_ids[0] = 1;

    use_run_animated(ACTING_SLOT, 1, target_ids);

    CHECK_EQ(unit(1)->hp_current, START_HP + HEAL_TEN_ROLL);
    CHECK_EQ(unit(ACTING_UNIT)
                 ->inventory_slots[BAG_ENTRY_ID_INDEX(ACTING_SLOT)],
             HEAL_WEAPON_ID);
    heal_unstage();
    use_unstage();
}

/* How far off its parked position the view was found on each axis, and on how
   many of the interrupts it was found off it at all.  Written only by the
   handler below and read only after the call has returned. */
static volatile int quake_min_dx;
static volatile int quake_max_dx;
static volatile int quake_min_dy;
static volatile int quake_max_dy;
static volatile int quake_samples;

/* The same IRQ0 handler the healing cases install, with one sample of both
   scroll globals taken before the tick is counted.  It is the only way to see
   the shake at all: the arm restores both origins before it returns, so a case
   that only looked afterwards could not tell a 25-frame quake from no quake. */
static void __interrupt __far quake_timer_isr(void)
{
    int dx;
    int dy;

    dx = data_fdps_battle_view_window_origin_x - QUAKE_ORIGIN_X;
    dy = data_fdps_battle_view_window_origin_y - QUAKE_ORIGIN_Y;
    if (dx != 0 || dy != 0) {
        if (dx < quake_min_dx) {
            quake_min_dx = dx;
        }
        if (dx > quake_max_dx) {
            quake_max_dx = dx;
        }
        if (dy < quake_min_dy) {
            quake_min_dy = dy;
        }
        if (dy > quake_max_dy) {
            quake_max_dy = dy;
        }
        quake_samples = quake_samples + 1;
    }
    ++data_fdps_timer_tick_counter;
    _chain_intr(heal_saved_timer);
}

/* The earth item, driven end to end: EarQu.wav out of the shipped MISC.VFS,
   25 real fdps_render_view_frame passes with both scroll globals displaced, and
   the damage landing afterwards.

   THE VIEW IS PUT BACK EXACTLY.  Both globals are saved before the loop and
   written back after it, so a caller that scrolled the map before using the
   item finds it where it left it.

   THE JITTER IS ASYMMETRIC.  The sampled offsets never reach +2 on either axis
   and do reach the negative side, which is rand() % 4 - 2 and not the -2..+2
   the shape invites: over the whole quake the view leans up and left.

   THE DAMAGE IS AFTER THE SHAKE AND THE ITEM IS CONSUMED.  0x04 is the item
   half of the earth pair, so unlike 0x0a it reaches fdps_unit_remove_item. */
static void the_earth_item_shakes_the_view_and_puts_it_back(void)
{
    unsigned char target_ids[1];

    if (!heal_archive_present()) {
        return;
    }
    use_stage_animated();
    unit(ACTING_UNIT)->inventory_slots[BAG_ENTRY_ID_INDEX(ACTING_SLOT)] =
        EARTH_ITEM_ID;
    target_ids[0] = 1;
    data_fdps_battle_view_window_origin_x = QUAKE_ORIGIN_X;
    data_fdps_battle_view_window_origin_y = QUAKE_ORIGIN_Y;
    quake_min_dx = 0;
    quake_max_dx = 0;
    quake_min_dy = 0;
    quake_max_dy = 0;
    quake_samples = 0;

    heal_set_mode(HEAL_MODE_320X200X256);
    heal_saved_timer = _dos_getvect(HEAL_TIMER_VECTOR);
    _dos_setvect(HEAL_TIMER_VECTOR, quake_timer_isr);
    fdps_apply_item_effect_to_targets(ACTING_UNIT, ACTING_SLOT, 1, target_ids);
    _dos_setvect(HEAL_TIMER_VECTOR, heal_saved_timer);
    heal_set_mode(HEAL_MODE_TEXT);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, QUAKE_ORIGIN_X);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, QUAKE_ORIGIN_Y);
    CHECK_EQ(quake_samples >= QUAKE_MIN_SAMPLES, 1);
    CHECK_EQ(quake_samples <= QUAKE_FRAMES, 1);
    CHECK_EQ(quake_max_dx <= QUAKE_MAX_OFFSET, 1);
    CHECK_EQ(quake_max_dy <= QUAKE_MAX_OFFSET, 1);
    CHECK_EQ(quake_min_dx <= QUAKE_MIN_OFFSET, 1);
    CHECK_EQ(quake_min_dy <= QUAKE_MIN_OFFSET, 1);
    CHECK_EQ(unit(1)->hp_current, START_HP - USE_TEN_ROLL);
    CHECK_EQ(unit(ACTING_UNIT)
                 ->inventory_slots[BAG_ENTRY_ID_INDEX(ACTING_SLOT)],
             BAG_ENTRY_ID_AFTER_REMOVAL);
    heal_unstage();
    use_unstage();
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
    RUN_TEST(the_item_record_layout_matches_the_offsets_read);
    RUN_TEST(a_beam_cannon_damages_every_listed_target);
    RUN_TEST(a_beam_cannon_is_not_taken_out_of_the_bag);
    RUN_TEST(the_item_id_comes_from_the_second_byte_of_the_bag_entry);
    RUN_TEST(the_use_amount_is_read_as_a_signed_word);
    RUN_TEST(a_beam_cannon_target_id_above_127_is_widened_unsigned);
    RUN_TEST(a_beam_cannon_with_no_targets_hits_nothing);
    RUN_TEST(an_unhandled_use_effect_does_nothing_at_all);
    RUN_TEST(the_popup_queue_count_is_cleared_on_entry);
    RUN_TEST(the_pending_xp_credit_is_cleared_before_the_settlement);
    RUN_TEST(a_max_hp_item_adds_its_own_constant_to_target_zero_only);
    RUN_TEST(a_healing_item_is_consumed_and_a_healing_weapon_is_not);
    RUN_TEST(the_earth_item_shakes_the_view_and_puts_it_back);
}
