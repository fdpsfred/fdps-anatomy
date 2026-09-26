/* tests/chevt6.c -- cover for src/chevt6.c.
 *
 * The chapter 29 handler at 000395d0 is the family's inline range merge with a
 * side gate bolted in front of it, and the two halves are covered separately.
 *
 * The merge half is covered the way the chapter 15 and 16 handlers are: its two
 * bounds, the signed inclusive compare between them and the 0xf0 mask are all
 * literals in its instruction stream -- MOV dword ptr [EBP-0x24],0x24 at
 * 000395fb, MOV dword ptr [EBP-0x20],0x59 at 00039602, CMP EAX,[EBP-0x14] / JLE
 * at 0003962b, and AND DL,0xf0 at 0003964b -- and the absence of a guard in
 * front of the loop is asserted by putting the shared one-shot latch slot up and
 * watching it run anyway.
 *
 * The gate half is what this handler has and its siblings do not: MOV AL,byte
 * ptr [EAX+0x6] / AND EAX,0xff / CMP EAX,0x2 / JNZ at 000395ee..000395f9, an
 * unsigned equality on the side byte of the record the argument names.  It is
 * covered by driving every side value through it and by moving which record the
 * argument names, because this is the only handler of the family whose incoming
 * index reaches anything -- the others overwrite the slot with 0 on entry.
 *
 * The unit array is staged here rather than read from a game file, because the
 * handler takes its whole effect through data_fdps_map_unit_array_ptr --
 * pointing that global at a local block is the only way to see the stores, and
 * it is also the only way to give the argument a record to be gated on.  What
 * the global itself holds is ticket 23's and is not asserted, so every case
 * writes the state it wants to see changed.
 *
 * Which indices the range means comes from map28.dat: byte 1 declares 12 player
 * slots and byte 2 declares 80 scenario units, so unit indices 0x00..0x0b are
 * the party and deployment records 0..79 follow at 0x0c..0x5b, and 0x24..0x59
 * is records 24..77.  Records 24..59 carry behaviour code 2 at deployment record
 * offset 0x11 and records 60..77 carry 0.  The top bound is pinned hardest here
 * for a reason the sibling handlers do not have: records 78 and 79, the two
 * level 30 Guardian Dragons, sit at unit indices 0x5a and 0x5b and carry
 * behaviour 2 as well, so a loop written with <= 0x5b, or one that ran to the
 * end of the array, would release the chapter's two boss units with the ambush.
 *
 * The chapter 29 handler at 00039670 is the same gate in front of two of those
 * merges instead of one, so the ch29all_ cases cover the same ground once more
 * -- both bounds of both ranges, both 0xf0 masks, the gate and the absent latch
 * -- and add the one thing the single-range handler cannot show: which of the
 * two values a record ends up with.  The ranges are 0x0c..0x59 with mode 0 and
 * 0x5a..0x5b with mode 0x0a, so together they are every one of map28.dat's 80
 * deployment records, and the two Guardian Dragons at 0x5a and 0x5b are the
 * pair that must come out at 0x0a rather than at the 0 everything below them
 * gets.
 *
 * The chapter 30 handler at 000398c0 shares none of that ground: its whole body
 * is one deployment call, so the ch30w3_ cases run that deployment for real and
 * read back which record landed on which tile.  They need ICON.CEL and FIELD.VFS
 * next to the executable and skip themselves without them.
 *
 * The chapter 30 handler at 00039840 is a third deployment with two spoken lines
 * and a terrain change around it, so its ch30w2_ cases reuse the same fixture
 * for the deployment half and add the half no other handler in this file has:
 * a write into the shared cell-trigger table that only means anything through
 * the sweep called on the next line.  They are defined last because they are
 * built on both the ch30w3_ staging helpers and the ch30w4_ decoy tagging.
 *
 * The chapter 28 handler at 00039550 is the same deployment with a pan behind
 * it, so its ch28_ cases reuse the chapter 30 fixture for the deployment half
 * and add the half the ch30w3_ cases cannot show: a wave number computed from
 * the battle turn counter rather than written as a literal, and a view that is
 * walked to a fixed map pixel and held there.  They are defined after the
 * ch30w3_ block because they are built on its staging helpers.
 *
 * The undead top-up at 00010760 shares none of it either, and it is the only
 * function in this file with arithmetic in it.  Its ch30rev_ cases split in two.
 * The gate cases -- which character ids are accepted, whether the unit has to be
 * dead, and where the sweep stops -- stage records that qualify for nothing, so
 * the body never starts and they cost nothing and need no game file.  The search
 * cases have to run the whole thing, because the tile the search picks is only
 * readable off the record it is written into, and everything between the search
 * and that write is real hardware work: the cursor walk, 65 palette uploads and
 * the Posion.saf effect out of MISC.VFS.  They stage what the game stages -- the
 * adapter in mode 13h and an IRQ0 handler advancing data_fdps_timer_tick_counter
 * -- the way tests/item.c does for the same clip, and skip themselves when the
 * container is not staged next to the executable (tests/gamefile.lst).
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "mapdraw.h"
#include "chevt6.h"

/* The single inclusive range the one inline loop covers, read off the constants
   at 000395fb (0x24) and 00039602 (0x59) with the signed JLE at 0003962e. */
#define CH29_FIRST_INDEX 0x24
#define CH29_LAST_INDEX  0x59

/* How many records that range covers: 0x59 - 0x24 + 1. */
#define CH29_RANGE_UNITS 54

/* map28.dat's byte 1: the party occupies unit indices 0x00..0x0b, so the first
   deployment record is unit index 0x0c. */
#define CH29_FIRST_ENEMY_INDEX 0x0c

/* Unit index of deployment record 78, the first of the two level 30 Guardian
   Dragons.  It is one past the top of the range and holds behaviour 2, so it is
   the record an inclusive-bound mistake at the top would release. */
#define CH29_GUARDIAN_DRAGON_INDEX 0x5a

/* Unit index of deployment record 79, the second Guardian Dragon and the last
   unit map28.dat deploys: 0x0c + 79. */
#define CH29_LAST_DEPLOYED_INDEX 0x5b

/* Enough records to hold that whole deployment and four past it, so an
   off-by-one at either end of the range has somewhere visible to land. */
#define CH29_STAGE_UNITS 0x60

/* The three side codes: 2 is the player, 0 the enemy and 1 the neutral side. */
#define CH29_PLAYER_SIDE  2
#define CH29_ENEMY_SIDE   0
#define CH29_NEUTRAL_SIDE 1

/* The slot of data_fdps_map_cell_event_triggered_flags the one-shot handlers of
   this family latch -- element 0x10, the first the map's own event codes cannot
   reach.  This handler does not use it, and that is what is asserted. */
#define CH29_LATCH_SLOT 0x10

static struct fdps_unit_record ch29_units[CH29_STAGE_UNITS];

/* Give every record the same AI byte, lay the sides out the way map28.dat does
   -- the party on the player side at 0x00..0x0b and every deployment record on
   the enemy side above it -- and point the array global at the block.  The
   staged AI value carries a high nibble as well as a behaviour code, because the
   whole point of the merge is that only one of the two moves. */
static void stage_ch29_units(int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch29_units;
    for (i = 0; i < (int) sizeof(ch29_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH29_STAGE_UNITS; i++) {
        ch29_units[i].ai_behavior = (unsigned char) ai_behavior;
        if (i < CH29_FIRST_ENEMY_INDEX) {
            ch29_units[i].side = CH29_PLAYER_SIDE;
        } else {
            ch29_units[i].side = CH29_ENEMY_SIDE;
        }
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch29_units;
}

/* The record has to be 0x50 bytes with its side byte at +6 and its AI byte at
   +0x34 for the emitted C to address the bytes the MOV AL,[EAX+0x6] load at
   000395ee and the MOV [EAX+0x34],DH store at 00039656 address; the stride is
   the IMUL 0x50 inside fdps_get_unit_record. */
static void ch29_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
}

/* Fired for a player unit, exactly indices 0x24..0x59 are rewritten and every
   record either side of the range is left as it was.  The staged 0x52 is
   behaviour code 2 -- hold position, which is what byte 0x11 of map28.dat's
   deployment records gives records 24..59 and 78..79 -- under a high nibble of
   0x50; the range comes out 0x50 because the mode ORed in is 0, and the rest
   keep 0x52.  Indices 0..0x23 cover the twelve party slots and deployment
   records 0..23, and 0x5a..0x5f run from the two Guardian Dragons past the end
   of the deployment: both ends have to be untouched. */
static void ch29_player_unit_clears_exactly_the_range(void)
{
    int i;

    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_enemy_groups(0);

    for (i = 0; i < CH29_STAGE_UNITS; i++) {
        if (i >= CH29_FIRST_INDEX && i <= CH29_LAST_INDEX) {
            CHECK_EQ(ch29_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch29_units[i].ai_behavior, 0x52);
        }
    }
}

/* The range covers 54 records, not 53: 0x59 - 0x24 + 1, and the top bound is
   inclusive because the compare at 0003962b is JLE.  The count is asserted by
   counting the records that moved, and both boundary records are named on their
   own -- 0x59 must move and 0x5a must not -- because a rewrite of the loop as
   i < 0x59 is exactly the mistake that leaves map28.dat's deployment record 77
   guarding its tile and changes nothing else. */
static void ch29_range_includes_the_last_index(void)
{
    int i;
    int moved;

    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_enemy_groups(0);

    moved = 0;
    for (i = 0; i < CH29_STAGE_UNITS; i++) {
        if (ch29_units[i].ai_behavior == 0x50) {
            moved++;
        }
    }
    CHECK_EQ(moved, CH29_RANGE_UNITS);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX + 1].ai_behavior, 0x52);
}

/* The two Guardian Dragons sit immediately above the top bound and hold the same
   behaviour code as the units that are released, so nothing but the bound keeps
   them in place.  Both are asserted unchanged, along with the first record below
   the bottom bound, which is deployment record 23 and holds position for the
   same reason. */
static void ch29_leaves_the_guardian_dragons_holding(void)
{
    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_enemy_groups(0);

    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior, 0x52);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x52);
}

/* The gate is an equality on 2 and nothing else reaches the loop.  The side byte
   is zero-extended by AND EAX,0xff at 000395f1 before the compare, so it is the
   whole byte that is tested: the enemy side 0, the neutral side 1, and the
   values 3 and 0xff that no deployment record carries all have to leave the map
   alone.  Each case is checked at both ends of the range, which is the pair a
   partially-run loop would show up in. */
static void ch29_only_the_player_side_springs_the_ambush(void)
{
    static int blocked_sides[4] = {CH29_ENEMY_SIDE, CH29_NEUTRAL_SIDE, 3, 0xff};
    int i;

    for (i = 0; i < 4; i++) {
        stage_ch29_units(0x52);
        ch29_units[0].side = (unsigned char) blocked_sides[i];
        fdps_chapter_29_event_activate_enemy_groups(0);
        CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x52);
        CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x52);
    }

    stage_ch29_units(0x52);
    ch29_units[0].side = CH29_PLAYER_SIDE;
    fdps_chapter_29_event_activate_enemy_groups(0);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
}

/* The gate reads the record the argument names, not a fixed one, so which unit
   walked over the tile is what decides.  With map28.dat's own side layout staged
   -- the party at 0x00..0x0b and every deployment record on the enemy side above
   it -- each of the twelve party indices springs the ambush and a spread of
   enemy indices, including ones inside the released range and the last deployed
   unit, does not.  A unit inside the range that is on the player side is not
   special-cased either: it springs the ambush and is itself rewritten, because
   the loop covers its index like any other. */
static void ch29_gate_follows_the_argument_unit(void)
{
    static int enemy_indices[5] = {CH29_FIRST_ENEMY_INDEX, 0x24, 0x40, 0x59,
                                   CH29_LAST_DEPLOYED_INDEX};
    int i;

    for (i = 0; i < CH29_FIRST_ENEMY_INDEX; i++) {
        stage_ch29_units(0x52);
        fdps_chapter_29_event_activate_enemy_groups(i);
        CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch29_units[i].ai_behavior, 0x52);
    }

    for (i = 0; i < 5; i++) {
        stage_ch29_units(0x52);
        fdps_chapter_29_event_activate_enemy_groups(enemy_indices[i]);
        CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x52);
        CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x52);
    }

    stage_ch29_units(0x52);
    ch29_units[0x30].side = CH29_PLAYER_SIDE;
    fdps_chapter_29_event_activate_enemy_groups(0x30);
    CHECK_EQ(ch29_units[0x30].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
}

/* The high nibble is carried across untouched by the AND 0xf0 at 0003964b and
   the low nibble ends at 0 whatever it held.  0x40 and 0x80 are the two AI flags
   read elsewhere -- fdps_map_actor_take_best_action and
   fdps_score_targets_for_item -- so a merge that assigned the mode whole, or
   that masked with anything wider, would drop them.  Expected values are the
   staged byte ANDed with 0xf0. */
static void ch29_keeps_the_high_nibble(void)
{
    stage_ch29_units(0);
    ch29_units[0x24].ai_behavior = 0xc2;
    ch29_units[0x25].ai_behavior = 0x02;
    ch29_units[0x30].ai_behavior = 0xff;
    ch29_units[0x44].ai_behavior = 0x40;
    ch29_units[0x59].ai_behavior = 0x8b;

    fdps_chapter_29_event_activate_enemy_groups(0);

    CHECK_EQ(ch29_units[0x24].ai_behavior, 0xc0);
    CHECK_EQ(ch29_units[0x25].ai_behavior, 0x00);
    CHECK_EQ(ch29_units[0x30].ai_behavior, 0xf0);
    CHECK_EQ(ch29_units[0x44].ai_behavior, 0x40);
    CHECK_EQ(ch29_units[0x59].ai_behavior, 0x80);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first -- which also happens to make the acting
   unit's side byte 0x55, so the gate is opened by setting that one byte to 2 and
   nothing else is disturbed.  A store that landed at +0x33 or +0x35 -- the
   death-script operand's high byte and ai_dest_x -- is visible against 0x55, and
   0x55's low nibble is not already 0, so the write that should happen is visible
   too.  Both ends of the range are checked, because the stride the store is
   indexed by is the IMUL 0x50 inside fdps_get_unit_record and an error in it
   shows up furthest from the base. */
static void ch29_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch29_units;
    for (i = 0; i < (int) sizeof(ch29_units); i++) {
        bytes[i] = 0x55;
    }
    bytes[6] = CH29_PLAYER_SIDE;
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_29_event_activate_enemy_groups(0);

    CHECK_EQ(bytes[0x24 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x24 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x24 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x23 * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x5a * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[6], CH29_PLAYER_SIDE);
}

/* Nothing guards the loop: the instruction after the gate's JNZ at 000395f9 is
   the first of the three constant stores, with no compare between them, so this
   handler has no one-shot latch and runs its loop every time it is called with a
   player unit.  The latch slot is put up before the call and the range still
   moves; the slot is also asserted unchanged, because a handler that had grown a
   latch would have written it.  A second call is made too: the merge is
   idempotent, so it must leave the same values. */
static void ch29_has_no_one_shot_latch(void)
{
    stage_ch29_units(0x52);
    data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT] = 1;

    fdps_chapter_29_event_activate_enemy_groups(0);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);

    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_enemy_groups(0);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);

    fdps_chapter_29_event_activate_enemy_groups(0);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX + 1].ai_behavior, 0x52);
}

/* How many records the all-enemies handler's first range covers: 0x59 - 0x0c +
   1, map28.dat's deployment records 0..77.  Records 78 and 79 are the second
   range's and are not in this count. */
#define CH29ALL_ADVANCE_UNITS 78

/* The behaviour code the second range ORs in, read off MOV dword ptr
   [EBP-0x30],0xa at 0003970d, and the byte it produces over the staged high
   nibble 0x50. */
#define CH29ALL_DRAGON_MODE 0x0a
#define CH29ALL_DRAGON_BYTE 0x5a

/* The handler at 00039670 releases the whole deployment, and the two ranges
   land different values: unit indices 0x0c..0x59 come out with behaviour code 0
   under their own high nibble, 0x5a and 0x5b with 0x0a, the party at
   0x00..0x0b is outside both ranges, and so is everything above 0x5b.  Staged
   0x52 is behaviour code 2 -- hold position, which is what byte 0x11 of
   map28.dat's deployment records gives records 0..59 and 78..79 -- under a high
   nibble of 0x50, so the expected bytes are 0x50 and 0x5a. */
static void ch29all_releases_the_whole_deployment(void)
{
    int i;

    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_all_enemies(0);

    for (i = 0; i < CH29_STAGE_UNITS; i++) {
        if (i < CH29_FIRST_ENEMY_INDEX) {
            CHECK_EQ(ch29_units[i].ai_behavior, 0x52);
        } else if (i <= CH29_LAST_INDEX) {
            CHECK_EQ(ch29_units[i].ai_behavior, 0x50);
        } else if (i <= CH29_LAST_DEPLOYED_INDEX) {
            CHECK_EQ(ch29_units[i].ai_behavior, CH29ALL_DRAGON_BYTE);
        } else {
            CHECK_EQ(ch29_units[i].ai_behavior, 0x52);
        }
    }
}

/* Both compares are JLE, so both ranges are inclusive at the top: 0x59 is the
   last index the first loop writes and 0x5b the last the second writes.  The
   count of records that came out at 0x50 is 78 -- 0x59 - 0x0c + 1 -- and the
   four boundary records are named on their own, because i < 0x59 would leave
   deployment record 77 guarding its tile and i < 0x5b would leave one of the
   two Guardian Dragons in hold-position mode. */
static void ch29all_both_ranges_are_inclusive(void)
{
    int i;
    int advanced;

    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_all_enemies(0);

    advanced = 0;
    for (i = 0; i < CH29_STAGE_UNITS; i++) {
        if (ch29_units[i].ai_behavior == 0x50) {
            advanced++;
        }
    }
    CHECK_EQ(advanced, CH29ALL_ADVANCE_UNITS);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX + 1].ai_behavior, 0x52);
}

/* The two ranges are disjoint -- the first covers 0x0c..0x59 and the second
   0x5a..0x5b -- so the dragons are written by the second loop alone, and what
   they come out with is 0x0a and not the 0 their 78 neighbours take.  Guide chapter 29: the 魔龍 fires 火焰 for 300 direct damage down a
   14-tile line and 使用後還可再近身攻擊一次, still gets a melee attack after
   using it, which is mode 0x0a's item arm followed by its attack arm; mode 0
   would pick one action or the other and would also let the dragons cross the
   map.  The dragons are staged with a high nibble of their own, 0x30, so the
   assertion pins the byte to the second range's mode rather than to the value
   the first range's would have produced. */
static void ch29all_dragons_get_the_item_then_attack_mode(void)
{
    stage_ch29_units(0x52);
    ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior = 0x32;
    ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior = 0x32;

    fdps_chapter_29_event_activate_all_enemies(0);

    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior,
             0x30 | CH29ALL_DRAGON_MODE);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
             0x30 | CH29ALL_DRAGON_MODE);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
}

/* The gate is an equality on 2 and nothing else reaches either loop.  The side
   byte is zero-extended by AND EAX,0xff at 00039691 before the compare, so the
   whole byte is tested: the enemy side 0, the neutral side 1, and the values 3
   and 0xff that no deployment record carries all have to leave the map alone.
   Each case is checked at both ends of the first range and on a dragon, which
   is the trio a partially-run pair of loops would show up in. */
static void ch29all_only_the_player_side_springs_the_ambush(void)
{
    static int blocked_sides[4] = {CH29_ENEMY_SIDE, CH29_NEUTRAL_SIDE, 3, 0xff};
    int i;

    for (i = 0; i < 4; i++) {
        stage_ch29_units(0x52);
        ch29_units[0].side = (unsigned char) blocked_sides[i];
        fdps_chapter_29_event_activate_all_enemies(0);
        CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x52);
        CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x52);
        CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
    }

    stage_ch29_units(0x52);
    ch29_units[0].side = CH29_PLAYER_SIDE;
    fdps_chapter_29_event_activate_all_enemies(0);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
}

/* The gate reads the record the argument names, not a fixed one.  With
   map28.dat's own side layout staged, each of the twelve party indices springs
   the ambush -- and is itself left alone, because the party sits below the
   bottom bound -- while a spread of enemy indices inside both ranges does not.
   A unit inside the first range that is on the player side is not special-cased
   either: it springs the ambush and is rewritten like any other index, and the
   same goes for a dragon index. */
static void ch29all_gate_follows_the_argument_unit(void)
{
    static int enemy_indices[5] = {CH29_FIRST_ENEMY_INDEX, 0x40, CH29_LAST_INDEX,
                                   CH29_GUARDIAN_DRAGON_INDEX,
                                   CH29_LAST_DEPLOYED_INDEX};
    int i;

    for (i = 0; i < CH29_FIRST_ENEMY_INDEX; i++) {
        stage_ch29_units(0x52);
        fdps_chapter_29_event_activate_all_enemies(i);
        CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
                 CH29ALL_DRAGON_BYTE);
        CHECK_EQ(ch29_units[i].ai_behavior, 0x52);
    }

    for (i = 0; i < 5; i++) {
        stage_ch29_units(0x52);
        fdps_chapter_29_event_activate_all_enemies(enemy_indices[i]);
        CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x52);
        CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
    }

    stage_ch29_units(0x52);
    ch29_units[CH29_GUARDIAN_DRAGON_INDEX].side = CH29_PLAYER_SIDE;
    fdps_chapter_29_event_activate_all_enemies(CH29_GUARDIAN_DRAGON_INDEX);
    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
}

/* Both merges carry the high nibble across untouched -- AND DL,0xf0 at 000396ef
   and 0003974f -- and set the low nibble to 0 and 0x0a respectively whatever it
   held.  0x40 and 0x80 are the two AI flags read elsewhere, so a merge that
   assigned the mode whole would drop them.  Expected values are the staged byte
   ANDed with 0xf0, ORed with the mode of whichever range the index falls in. */
static void ch29all_keeps_the_high_nibble(void)
{
    stage_ch29_units(0);
    ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior = 0xc2;
    ch29_units[0x30].ai_behavior = 0xff;
    ch29_units[0x44].ai_behavior = 0x40;
    ch29_units[CH29_LAST_INDEX].ai_behavior = 0x8b;
    ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior = 0xc2;
    ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior = 0x0f;

    fdps_chapter_29_event_activate_all_enemies(0);

    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0xc0);
    CHECK_EQ(ch29_units[0x30].ai_behavior, 0xf0);
    CHECK_EQ(ch29_units[0x44].ai_behavior, 0x40);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x80);
    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior, 0xca);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior, 0x0a);
}

/* One byte of each record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 -- which makes the acting unit's side byte 0x55
   as well, so the gate is opened by setting that one byte to 2 and nothing else
   is disturbed.  A store that landed at +0x33 or +0x35 -- the death-script
   operand's high byte and ai_dest_x -- is visible against 0x55, and 0x55's low
   nibble is not already 0 or 0x0a, so the writes that should happen are visible
   too.  Both ends of each range are checked, because the stride the stores are
   indexed by is the IMUL 0x50 inside fdps_get_unit_record and an error in it
   shows up furthest from the base. */
static void ch29all_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch29_units;
    for (i = 0; i < (int) sizeof(ch29_units); i++) {
        bytes[i] = 0x55;
    }
    bytes[6] = CH29_PLAYER_SIDE;
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_29_event_activate_all_enemies(0);

    CHECK_EQ(bytes[0x0c * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x0c * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x0c * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x5a * 0x50 + 0x34], CH29ALL_DRAGON_BYTE);
    CHECK_EQ(bytes[0x5b * 0x50 + 0x34], CH29ALL_DRAGON_BYTE);
    CHECK_EQ(bytes[0x5b * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x5b * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x0b * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x5c * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[6], CH29_PLAYER_SIDE);
}

/* Nothing guards either loop: the instruction after the gate's JNZ at 00039699
   is the first of the first expansion's constant stores, with no compare
   between them, so this handler has no one-shot latch and runs both loops every
   time it is called with a player unit.  The latch slot is put up before the
   call and the ranges still move; the slot is also asserted unchanged, because
   a handler that had grown a latch would have written it.  A second call is
   made too: both merges are idempotent, so it must leave the same values --
   including on the dragons, whose byte the first loop does not reach. */
static void ch29all_has_no_one_shot_latch(void)
{
    stage_ch29_units(0x52);
    data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT] = 1;

    fdps_chapter_29_event_activate_all_enemies(0);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);

    fdps_chapter_29_event_activate_all_enemies(0);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX + 1].ai_behavior, 0x52);
}

/* Chapter 30's "the second form has fallen" event at 000398c0, from here down.
 *
 * The handler's whole body is one call, and none of the three values it hands
 * fdps_deploy_wave -- PUSH dword ptr [0x00069cf4], PUSH 0x3 and a EAX holding 1,
 * at 000398d3..000398db -- is left anywhere afterwards.  So the only way to see
 * any of them is to let the deployment happen, and these cases run
 * fdps_deploy_wave for real, then read back what landed where:
 *
 *   MAP00.COD  record 0 (18, 0)  record 1 (22, 12)  record 2 (8, 10)
 *   MAP01.COD  record 0 (9, 4)
 *
 * -- the same records tests/deploy.c's own wave cases and the chapter 17 and 18
 * cases in tests/chevt3.c expect, staged through tests/gamefile.lst.  That
 * function opens ICON.CEL and FIELD.VFS for itself and a run without them would
 * not fail a check, it would hang in fdps_wait_any_key, so every case here skips
 * itself when they are not there.
 *
 * Which wave the spawn table's records carry is staged rather than read from
 * map29.dat, because what is under test is that the wave asked for is the
 * literal 3 and not something derived: a record is planted at each of the waves
 * either side of it, so asking for the wrong one is visible as a different
 * character id on a different tile.  map29.dat's own wave 3 is one record, the
 * boss's third form, and a fixture of one record could not tell "wave 3" from
 * "every record" or from "the first record".
 *
 * The unit array is malloc'd rather than staged into a static block, because a
 * deployment reallocs it: a static block handed to realloc is not a smaller
 * fixture, it is undefined behaviour.  It is never freed, for the same reason
 * tests/deploy.c does not free it -- the block moves under the global on every
 * deployment.
 */

/* The wave the handler asks for: PUSH 0x3 at 000398d9. */
#define CH30W3_WAVE_NO 3

/* A map big enough for MAP00.COD's records, which reach (22, 12).  The tile
   search a placement flag of 0 would run walks the whole grid, and the marking
   pass writes at a unit's own tile with no bound, so the grid has to cover the
   coordinates the real placement file names either way. */
#define CH30W3_GRID_W 32
#define CH30W3_GRID_H 16

/* Where the deployment records start in the MAP%02d.DAT block and where its
   header keeps their count: ADD EAX,0x83 at 00023463 and MOV AL,byte ptr
   [EAX+0x2] at 000238bb. */
#define CH30W3_SPAWN_TABLE_RECORD_BASE 0x83
#define CH30W3_SPAWN_TABLE_COUNT_OFFSET 2

/* The scene-layer offsets fdps_map_load_tile_info reads through: the tile map's
   width word at +7 with its 16-bit ids from +0xb, the attribute table's 4-byte
   rows from +0x11, and the event layer's width at +7 with its cells from +0x10
   (src/maptile.c). */
#define CH30W3_TILE_MAP_WIDTH_OFFSET 7
#define CH30W3_TILE_MAP_IDS_OFFSET 0xb
#define CH30W3_TILE_ATTR_ROWS_OFFSET 0x11
#define CH30W3_EVENT_LAYER_WIDTH_OFFSET 7
#define CH30W3_EVENT_LAYER_CELLS_OFFSET 0x10

/* A terrain code the deployment search accepts and one it rejects, either side
   of the CMP EAX,0x5 / JGE at 00023404. */
#define CH30W3_TERRAIN_WALKABLE 1
#define CH30W3_TERRAIN_BLOCKED 5

#define CH30W3_TILE_ATTR_ROWS 16
#define CH30W3_CHAR_TABLE_ROWS 8
#define CH30W3_ENEMY_TABLE_ROWS 8

/* Every item id is a valid index into the staged item table, 0xff included:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define CH30W3_ITEM_TABLE_ROWS 256
#define CH30W3_ITEM_ID_NONE 0xff

/* The unit array's stride, as the deployment path allocates it: PUSH 0x50 at
   000232cc. */
#define CH30W3_UNIT_STRIDE 0x50

/* The three staged deployment records and the wave each is tagged with, one at
   the wave the handler must ask for and one at each of its neighbours.  The
   character ids are arbitrary and only have to differ, so that which record was
   deployed is readable off the unit as well as off the tile. */
#define CH30W3_WAVE2_RECORD 0
#define CH30W3_WAVE3_RECORD 1
#define CH30W3_WAVE4_RECORD 2
#define CH30W3_WAVE2_CHAR_ID 5
#define CH30W3_WAVE3_CHAR_ID 6
#define CH30W3_WAVE4_CHAR_ID 7

static unsigned char ch30w3_grid[4 + CH30W3_GRID_W * CH30W3_GRID_H * 2];
static unsigned char ch30w3_spawn_table[CH30W3_SPAWN_TABLE_RECORD_BASE +
                                        8 * 0x1a];
static unsigned char ch30w3_tile_map[CH30W3_TILE_MAP_IDS_OFFSET +
                                     CH30W3_GRID_W * CH30W3_GRID_H * 2];
static unsigned char ch30w3_tile_attr[CH30W3_TILE_ATTR_ROWS_OFFSET +
                                      CH30W3_TILE_ATTR_ROWS * 4];
static unsigned char ch30w3_event_layer[CH30W3_EVENT_LAYER_CELLS_OFFSET +
                                        CH30W3_GRID_W * CH30W3_GRID_H];
static struct fdps_character_base_record ch30w3_char_base[
    CH30W3_CHAR_TABLE_ROWS];
static struct fdps_character_growth ch30w3_growth[CH30W3_CHAR_TABLE_ROWS];
static struct fdps_enemy_data ch30w3_enemy[CH30W3_ENEMY_TABLE_ROWS];
static struct fdps_item_effect ch30w3_items[CH30W3_ITEM_TABLE_ROWS];

static int ch30w3_files_checked = 0;
static int ch30w3_files_ready = 0;

/* ICON.CEL is read by fdps_cache_cel_sprite_group, which takes a fixed
   0x2970-byte bite out of the sheet's offset table, so a shorter file is one
   that reader runs off the end of rather than a smaller fixture.  FIELD.VFS
   cannot be stood in for at all: a missing container sends fdps_deploy_wave into
   fdps_wait_any_key and a missing member into exit(1). */
static void ch30w3_ensure_game_files(void)
{
    FILE *fp;
    long size;

    if (ch30w3_files_checked) {
        return;
    }
    ch30w3_files_checked = 1;

    fp = fopen("ICON.CEL", "rb");
    if (fp == NULL) {
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    if (size < (long) (15 + 0x2970)) {
        return;
    }

    fp = fopen("FIELD.VFS", "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);
    ch30w3_files_ready = 1;
}

static void ch30w3_zero_bytes(void *block, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        bytes[i] = 0;
    }
}

static struct fdps_char_spawn_record *ch30w3_spawn_at(int index)
{
    return (struct fdps_char_spawn_record *)
           (ch30w3_spawn_table + CH30W3_SPAWN_TABLE_RECORD_BASE) + index;
}

/* One deployment record with nothing equipped and nothing carried, tagged with
   the wave it belongs to. */
static void ch30w3_set_spawn(int index, int char_id, int wave_no)
{
    ch30w3_spawn_at(index)->char_id = (unsigned char) char_id;
    ch30w3_spawn_at(index)->level = 1;
    ch30w3_spawn_at(index)->side = 0;
    ch30w3_spawn_at(index)->equipped_item_0 = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->equipped_item_1 = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[0] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[1] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[2] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[3] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[4] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[5] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->wave_no = (unsigned char) wave_no;
}

static void ch30w3_set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch30w3_tile_attr + CH30W3_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

/* The cell of the tile map at (tile_x, tile_y), so one tile can be given an id
   whose attribute row says a search must not use it. */
static void ch30w3_set_tile_id(int tile_x, int tile_y, int tile_id)
{
    short *tile_ids;

    tile_ids = (short *) (ch30w3_tile_map + CH30W3_TILE_MAP_IDS_OFFSET);
    tile_ids[tile_y * CH30W3_GRID_W + tile_x] = (short) tile_id;
}

static struct fdps_unit_record *ch30w3_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH30W3_UNIT_STRIDE);
}

/* A blank walkable map with one unit already on it, the chapter global on map 0
   and the turn counter on the given turn, plus three deployment records tagged
   waves 2, 3 and 4 at table indices 0, 1 and 2.  Placement record and table
   index are the same number, so which record was deployed is readable twice
   over: off the character id and off the tile it landed on.

   The unit already on the map stands at (0, 0), clear of every placement record
   these cases read back. */
static void ch30w3_stage(int battle_turn)
{
    int i;

    ch30w3_zero_bytes(ch30w3_grid, (int) sizeof(ch30w3_grid));
    ch30w3_zero_bytes(ch30w3_spawn_table, (int) sizeof(ch30w3_spawn_table));
    ch30w3_zero_bytes(ch30w3_tile_map, (int) sizeof(ch30w3_tile_map));
    ch30w3_zero_bytes(ch30w3_tile_attr, (int) sizeof(ch30w3_tile_attr));
    ch30w3_zero_bytes(ch30w3_event_layer, (int) sizeof(ch30w3_event_layer));
    ch30w3_zero_bytes(ch30w3_char_base, (int) sizeof(ch30w3_char_base));
    ch30w3_zero_bytes(ch30w3_growth, (int) sizeof(ch30w3_growth));
    ch30w3_zero_bytes(ch30w3_enemy, (int) sizeof(ch30w3_enemy));
    ch30w3_zero_bytes(ch30w3_items, (int) sizeof(ch30w3_items));

    *(short *) ch30w3_grid = (short) CH30W3_GRID_W;
    *(short *) (ch30w3_grid + 2) = (short) CH30W3_GRID_H;

    *(short *) (ch30w3_tile_map + CH30W3_TILE_MAP_WIDTH_OFFSET) =
        (short) CH30W3_GRID_W;
    for (i = 0; i < CH30W3_TILE_ATTR_ROWS; i++) {
        ch30w3_set_terrain(i, CH30W3_TERRAIN_WALKABLE);
    }

    *(short *) (ch30w3_event_layer + CH30W3_EVENT_LAYER_WIDTH_OFFSET) =
        (short) CH30W3_GRID_W;

    data_fdps_battle_move_grid_ptr = ch30w3_grid;
    data_fdps_tile_event_data_table_ptr = ch30w3_spawn_table;
    data_fdps_scene_layer_tile_map_ptrs[0] = ch30w3_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ch30w3_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = ch30w3_event_layer;
    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) ch30w3_char_base;
    data_fdps_battle_character_growth_table_ptr =
        (unsigned char *) ch30w3_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch30w3_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch30w3_items;

    data_fdps_map_unit_array_ptr = (unsigned char *) malloc(CH30W3_UNIT_STRIDE);
    ch30w3_zero_bytes(data_fdps_map_unit_array_ptr, CH30W3_UNIT_STRIDE);
    data_fdps_map_unit_count = 1;

    ch30w3_spawn_table[CH30W3_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH30W3_WAVE2_CHAR_ID, 2);
    ch30w3_set_spawn(CH30W3_WAVE3_RECORD, CH30W3_WAVE3_CHAR_ID, 3);
    ch30w3_set_spawn(CH30W3_WAVE4_RECORD, CH30W3_WAVE4_CHAR_ID, 4);

    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_battle_turn_counter = battle_turn;
}

/* Move the wave-3 tag down onto table index 0, so the record the handler
   deploys is placement record 0 -- the one record of MAP01.COD these cases know
   the coordinates of. */
static void ch30w3_tag_first_record_as_the_wave(void)
{
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH30W3_WAVE2_CHAR_ID,
                     CH30W3_WAVE_NO);
    ch30w3_set_spawn(CH30W3_WAVE3_RECORD, CH30W3_WAVE3_CHAR_ID, 2);
}

/* The fields the cases below read back and the stride they are indexed by.
   Every one of them would agree with itself while addressing another byte if the
   layout were wrong. */
static void ch30w3_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH30W3_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, char_id), 8);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* The wave asked for is 3 and only the record tagged with it arrives: of the
   three staged records exactly one deploys, it is table index 1, and it lands on
   MAP00.COD record 1 at (22, 12) with the character id that record carries.  The
   records at waves 2 and 4 sit either side of it, so an off-by-one in either
   direction would put a different character on a different tile. */
static void ch30w3_deploys_the_wave_three_record(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w3_stage(0);

    fdps_chapter_30_event_deploy_wave_3(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, 12);
}

/* The 3 is a literal and not the battle turn counter, which is what every
   turn-scheduled handler of this family pushes instead: the counter is driven
   through the wave numbers the fixture carries and one well above them, and the
   same wave-3 record arrives every time.  A handler that read the counter would
   deploy the wave-2 record on turn 2, the wave-4 record on turn 4 and nothing at
   all on turn 0 or turn 11. */
static void ch30w3_wave_number_is_a_literal(void)
{
    static int turns[4] = {0, 2, 4, 11};
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < 4; i++) {
        ch30w3_stage(turns[i]);
        fdps_chapter_30_event_deploy_wave_3(0);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, 22);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, 12);
    }
}

/* It is the whole wave and not one unit: map29.dat tags a single record wave 3,
   so nothing in the shipped data would tell a wave deploy from a single-unit
   one, and the fixture tags all three records instead.  All three arrive, in
   table order, on their own placement records -- (18, 0), (22, 12) and
   (8, 10) -- and the count goes up by three. */
static void ch30w3_deploys_every_record_of_the_wave(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w3_stage(0);
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH30W3_WAVE2_CHAR_ID,
                     CH30W3_WAVE_NO);
    ch30w3_set_spawn(CH30W3_WAVE4_RECORD, CH30W3_WAVE4_CHAR_ID,
                     CH30W3_WAVE_NO);

    fdps_chapter_30_event_deploy_wave_3(0);

    CHECK_EQ(data_fdps_map_unit_count, 4);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, 0);
    CHECK_EQ((int) ch30w3_unit(2)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(2)->pos_x, 22);
    CHECK_EQ((int) ch30w3_unit(2)->pos_y, 12);
    CHECK_EQ((int) ch30w3_unit(3)->char_id, CH30W3_WAVE4_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(3)->pos_x, 8);
    CHECK_EQ((int) ch30w3_unit(3)->pos_y, 10);
}

/* The placement flag is 1 -- MOV EAX,0x1 / PUSH EAX at 000398d3 -- so the unit
   is put on the tile its placement record names and the terrain there is never
   consulted.  MAP00.COD record 1 names (22, 12); giving that one cell a tile id
   whose attribute row is terrain 5 is what takes a tile out of the search a flag
   of 0 runs, and the unit still lands on (22, 12).  The chapter 17 and 18
   handlers push 0 and their cases in tests/chevt3.c assert the same record
   landing on (22, 13) off the same fixture, so this is the assertion that
   separates the two flags. */
static void ch30w3_places_on_the_scripted_tile(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w3_stage(0);
    ch30w3_set_tile_id(22, 12, 1);
    ch30w3_set_terrain(1, CH30W3_TERRAIN_BLOCKED);

    fdps_chapter_30_event_deploy_wave_3(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, 12);
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the wave-3 record is moved onto table index 0 and deployed twice, once with
   that global on 0 and once on 1, and it lands on MAP00.COD's record 0 at
   (18, 0) and then on MAP01.COD's record 0 at (9, 4). */
static void ch30w3_map_number_comes_from_the_chapter_global(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w3_stage(0);
    ch30w3_tag_first_record_as_the_wave();

    fdps_chapter_30_event_deploy_wave_3(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, 0);

    ch30w3_stage(0);
    ch30w3_tag_first_record_as_the_wave();
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_30_event_deploy_wave_3(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, 9);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, 4);
}

/* The incoming argument slot is overwritten with 0 at 000398cc before anything
   else happens and never read back, so the index the dispatcher passes cannot
   reach the wave asked for, the map asked for or the placement flag.  In the
   shipped data this handler is reached from fdps_run_death_scripts, which passes
   the index of the unit that just died; the values put through here are the 0
   the turn-event runner would pass, an index that names the unit already on the
   map, one past the array, and -1 and 30000, which are the ones an
   argument-driven handler would betray itself on. */
static void ch30w3_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 2, -1, 30000};
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch30w3_stage(0);
        fdps_chapter_30_event_deploy_wave_3(arguments[i]);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, 22);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, 12);
    }
}

/* Nothing guards the call: there is no compare anywhere in the body and no latch
   is written, so a second firing appends a second copy of the wave rather than
   being refused.  The slot the one-shot handlers of this family latch is put up
   beforehand and the wave still arrives, and the slot is asserted unchanged
   because a handler that had grown a latch would have written it. */
static void ch30w3_has_no_one_shot_latch(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w3_stage(0);
    data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT] = 1;

    fdps_chapter_30_event_deploy_wave_3(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);

    fdps_chapter_30_event_deploy_wave_3(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch30w3_unit(2)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(2)->pos_x, 22);
    CHECK_EQ((int) ch30w3_unit(2)->pos_y, 12);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);
}

/* ---- fdps_chapter_28_event_deploy_wave_for_turn, 00039550 ----------------
 *
 * The handler is one deployment followed by a pan, and both halves are read off
 * the assembly at 00039550: the SAR EDX,0x1f / SUB EAX,EDX / SAR EAX,0x1 at
 * 0003956a..00039571 that halves the turn counter with a signed divide, the XOR
 * EAX,EAX / PUSH EAX at 0003955c that asks for the nearest-free-tile placement,
 * the PUSH 0x0 / PUSH 0x120 at 0003958a that names the pan target, the CMP dword
 * ptr [EBP+0x14],0xc / JL at 000395a0 that counts the hold, and the two literal
 * stores into the cursor draw mode at 00039580 and 000395b7.
 *
 * The cases run the deployment for real off the ch30w3_ fixture above -- the
 * same blank walkable map, the same three deployment records at table indices 0,
 * 1 and 2, the same MAP00.COD placement records -- because the wave the handler
 * asked for is only readable off the record that arrived.  The middle record
 * carries the wave under test and the other two carry one wave either side of
 * it, so a key that drifted in either direction deploys a different character id
 * onto a different tile, and a key that matches nothing deploys nothing at all.
 *
 * They also run the pan for real, which the ch30w3_ cases never do: the adapter
 * goes into mode 13h and a timer interrupt is installed for the length of every
 * call, because fdps_render_view_frame spins until the tick counter moves and
 * would otherwise never come back.  The cursor is parked one whole tile west of
 * the pan target, which is the shortest walk fdps_map_cursor_move_to accepts --
 * less than one tile on the dominant axis divides by zero inside it.
 *
 * The unit the fixture already has on the map is given portrait id 0x80, the id
 * fdps_draw_map_unit returns on before it reads anything else off a record, so
 * the twelve composed frames do not depend on what a sprite cache slot holds.
 * The units that arrive land on row 12, which is below the view, so they are
 * skipped by the compositor's own vertical range test rather than by anything
 * the fixture arranges.
 */

/* How long the view is held, CMP dword ptr [EBP+0x14],0xc / JL at 000395a0, and
   the least the tick counter can move across the call.  The bound is one-sided
   on purpose -- a slow machine spends more ticks than this, never fewer, and the
   frame the pan itself composes is on top of it -- so it cannot fail spuriously,
   while a rebuild that dropped the loop cannot reach it at any sane speed. */
#define CH28_HOLD_FRAMES 0xc
#define CH28_LEAST_HOLD_TICKS (CH28_HOLD_FRAMES - 1)

/* Where the pan ends, PUSH 0x0 / PUSH 0x120 at 0003958a, and where the cursor
   starts.  The start is one whole tile west of the target, so the walk runs and
   arrives on the target exactly: 0x120 is a whole multiple of the 24-pixel tile
   the walk steps by (mapcur.h). */
#define CH28_PAN_TARGET_X 0x120
#define CH28_PAN_TARGET_Y 0
#define CH28_PAN_START_X (CH28_PAN_TARGET_X - 0x18)
#define CH28_PAN_START_Y 0

/* The cursor mode the fixture parks before every run: neither of the two values
   the handler writes, so a run that left the global alone, one that hid the
   cursor and never put it back, and one that restored what it found are all told
   apart from the 1 the handler is required to leave behind. */
#define CH28_STAGED_CURSOR_MODE 4
#define CH28_CURSOR_MODE_NORMAL 1

/* Portrait id 0x80 is the one fdps_draw_map_unit returns on immediately, which
   is how the unit already on the map is kept out of the composed frames. */
#define CH28_PORTRAIT_NO_SPRITE 0x80

/* A value the frame latch cannot legitimately hold, so a run that composed
   nothing is distinguishable from one that did. */
#define CH28_FRAME_SENTINEL 0x5a5a5a5aU

/* Where MAP00.COD's placement record 1 puts a unit, which is the record table
   index 1 carries.  The tile is free and walkable in this fixture, so the
   nearest-free-tile search settles on the recorded tile itself. */
#define CH28_MAP00_RECORD1_X 22
#define CH28_MAP00_RECORD1_Y 12

/* The three staged records: the middle one carries the wave under test and the
   other two one wave either side of it.  The character ids are the ch30w3_
   fixture's own and only have to differ from each other. */
#define CH28_BELOW_CHAR_ID CH30W3_WAVE2_CHAR_ID
#define CH28_TAGGED_CHAR_ID CH30W3_WAVE3_CHAR_ID
#define CH28_ABOVE_CHAR_ID CH30W3_WAVE4_CHAR_ID

/* A wave tag no key these cases produce can select, used for the record below
   wave 0 and for the case where nothing matches at all. */
#define CH28_UNSELECTED_WAVE 0x30

#define CH28_MODE_TEXT 0x03
#define CH28_MODE_320X200X256 0x13
#define CH28_TIMER_VECTOR 8

/* The nine turns map27.dat schedules this slot on and the wave each of them
   halves down to.  Turn 7 comes out at 3 for the same reason turn 6 does, which
   is why wave 3 arrives twice in the shipped chapter and wave 4 never does. */
#define CH28_SCHEDULED_TURNS 9

static int ch28_schedule_turns[CH28_SCHEDULED_TURNS] = {
    2, 4, 6, 7, 10, 12, 14, 16, 18
};

static int ch28_schedule_waves[CH28_SCHEDULED_TURNS] = {
    1, 2, 3, 3, 5, 6, 7, 8, 9
};

static void (__interrupt __far *ch28_saved_timer)();
static unsigned int ch28_ticks_before;
static unsigned int ch28_ticks_after;

static void __interrupt __far ch28_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(ch28_saved_timer);
}

static void ch28_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* The three deployment records, tagged around the wave the case expects.  Below
   wave 0 there is no lower neighbour to lay down, and none is needed: the keys a
   broken halving would produce at turns 0 and 1 are all above 0, so the upper
   decoy catches them. */
static void ch28_tag_decoys(int wave_no)
{
    int wave_below;

    wave_below = wave_no - 1;
    if (wave_below < 0) {
        wave_below = CH28_UNSELECTED_WAVE;
    }
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH28_BELOW_CHAR_ID, wave_below);
    ch30w3_set_spawn(CH30W3_WAVE3_RECORD, CH28_TAGGED_CHAR_ID, wave_no);
    ch30w3_set_spawn(CH30W3_WAVE4_RECORD, CH28_ABOVE_CHAR_ID, wave_no + 1);
}

/* The ch30w3_ fixture on the given turn, with the decoys laid down around the
   wave under test and the compositor's own globals put where a frame can be
   composed against them. */
static void ch28_stage(int battle_turn, int wave_no)
{
    ch30w3_stage(battle_turn);
    ch28_tag_decoys(wave_no);

    ch30w3_unit(0)->portrait_id = CH28_PORTRAIT_NO_SPRITE;

    data_fdps_scene_layer_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_map_unit_walk_anim_counter = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = CH28_PAN_START_X;
    data_fdps_map_cursor_world_y = CH28_PAN_START_Y;
    data_fdps_view_frame_last_tick = CH28_FRAME_SENTINEL;
    data_fdps_map_cursor_draw_mode = CH28_STAGED_CURSOR_MODE;
}

/* One whole call with the adapter in the mode the game plays it in and a timer
   interrupt running, with the tick counter sampled either side so the length of
   the hold can be read back.  Text mode is back before anything is asserted, so
   a failure prints on a readable screen. */
static void ch28_run(int unit_index)
{
    ch28_set_mode(CH28_MODE_320X200X256);
    ch28_saved_timer = _dos_getvect(CH28_TIMER_VECTOR);
    _dos_setvect(CH28_TIMER_VECTOR, ch28_timer_isr);
    ch28_ticks_before = data_fdps_timer_tick_counter;
    fdps_chapter_28_event_deploy_wave_for_turn(unit_index);
    ch28_ticks_after = data_fdps_timer_tick_counter;
    _dos_setvect(CH28_TIMER_VECTOR, ch28_saved_timer);
    ch28_set_mode(CH28_MODE_TEXT);
}

/* Put the compositor's globals back the way a freshly started program has them,
   for the reason tests/anim.c gives: a later unit that expects an empty battle
   would otherwise inherit this fixture. */
static void ch28_unstage(void)
{
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_view_frame_last_tick = 0;
}

/* Each of the nine turns map27.dat schedules brings on the wave that turn halves
   to and no other: turn 2 wave 1, turn 4 wave 2, turns 6 and 7 wave 3, turn 10
   wave 5, and so on up to turn 18 and wave 9.  Every turn is staged against
   three records tagged one wave below, the wave itself and one above, so only
   the middle one may arrive; the count moves by exactly one and the character id
   and the tile both say which record it was.  Turn 7 is the case the halving is
   really about -- a rebuild that rounded it up, or that read the counter after
   the increment, would deploy the record tagged 4 instead. */
static void ch28_each_scheduled_turn_deploys_the_wave_it_halves_to(void)
{
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < CH28_SCHEDULED_TURNS; i++) {
        ch28_stage(ch28_schedule_turns[i], ch28_schedule_waves[i]);

        ch28_run(0);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH28_TAGGED_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH28_MAP00_RECORD1_X);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH28_MAP00_RECORD1_Y);
    }
    ch28_unstage();
}

/* The divide truncates toward zero, so consecutive turns pair onto one wave and
   an odd turn takes the wave below it: turns 0 and 1 both ask for wave 0 and
   turns 2 and 3 both ask for wave 1.  A key taken from the counter itself, or
   one rounded up, deploys the record tagged one above on every odd turn, and
   the fixture has that record laid down to catch it. */
static void ch28_odd_turns_truncate_down(void)
{
    static int turns[4] = {0, 1, 2, 3};
    static int waves[4] = {0, 0, 1, 1};
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < 4; i++) {
        ch28_stage(turns[i], waves[i]);

        ch28_run(0);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH28_TAGGED_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH28_MAP00_RECORD1_X);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH28_MAP00_RECORD1_Y);
    }
    ch28_unstage();
}

/* The pan goes to map pixel (0x120, 0) and the view is held there.  The cursor
   arrives on the target exactly, the draw mode afterwards is 1 -- not the 4 the
   fixture parked and not the 0 the pan ran under, so neither a run that saved
   and restored the mode nor one that left the blank behind passes -- the frame
   latch has moved off its sentinel, and the tick counter has moved by at least
   eleven, which no rebuild missing the hold loop can reach. */
static void ch28_pans_to_the_spawn_tile_and_holds(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch28_stage(ch28_schedule_turns[0], ch28_schedule_waves[0]);

    ch28_run(0);

    CHECK_EQ(data_fdps_map_cursor_world_x, CH28_PAN_TARGET_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH28_PAN_TARGET_Y);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH28_CURSOR_MODE_NORMAL);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH28_FRAME_SENTINEL, 0);
    CHECK_EQ(ch28_ticks_after - ch28_ticks_before
                 >= (unsigned int) CH28_LEAST_HOLD_TICKS,
             1);
    ch28_unstage();
}

/* Nothing in the body tests whether the deployment found anything, so a turn
   whose wave matches no record still blanks the cursor, walks the view to the
   top edge and holds it there.  All three records are tagged well away from the
   key this turn produces: the unit count does not move and the pan runs
   anyway. */
static void ch28_pans_even_when_no_record_matches(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch28_stage(40, CH28_UNSELECTED_WAVE);

    ch28_run(0);

    CHECK_EQ(data_fdps_map_unit_count, 1);
    CHECK_EQ(data_fdps_map_cursor_world_x, CH28_PAN_TARGET_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH28_PAN_TARGET_Y);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH28_CURSOR_MODE_NORMAL);
    CHECK_EQ(ch28_ticks_after - ch28_ticks_before
                 >= (unsigned int) CH28_LEAST_HOLD_TICKS,
             1);
    ch28_unstage();
}

/* The incoming argument slot is overwritten with 0 at 00039599 and is only the
   hold counter after that, so the index the dispatcher passes cannot reach the
   wave asked for, the map asked for or the pan.  The turn-event runner is the
   only path this slot is reached by in the shipped data and it pushes a literal
   0; the values put through here are that 0, an index that names the unit
   already on the map, one past the array, and -1 and 30000, which are the ones
   an argument-driven handler would betray itself on. */
static void ch28_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 2, -1, 30000};
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch28_stage(ch28_schedule_turns[2], ch28_schedule_waves[2]);

        ch28_run(arguments[i]);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH28_TAGGED_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH28_MAP00_RECORD1_X);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH28_MAP00_RECORD1_Y);
        CHECK_EQ(data_fdps_map_cursor_world_x, CH28_PAN_TARGET_X);
        CHECK_EQ(data_fdps_map_cursor_draw_mode, CH28_CURSOR_MODE_NORMAL);
    }
    ch28_unstage();
}

/* Nothing guards the call: there is no compare anywhere in the body and no latch
   is written, so a second firing on the same turn appends a second copy of the
   wave rather than being refused.  The slot the one-shot handlers of this family
   latch is put up beforehand and the wave still arrives, and the slot is
   asserted unchanged because a handler that had grown a latch would have written
   it.  The second arrival lands one tile south of the first, which is what the
   nearest-free-tile search does with a scripted tile that is now occupied. */
static void ch28_has_no_one_shot_latch(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch28_stage(ch28_schedule_turns[0], ch28_schedule_waves[0]);
    data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT] = 1;

    ch28_run(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);

    ch28_run(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch30w3_unit(2)->char_id, CH28_TAGGED_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(2)->pos_x, CH28_MAP00_RECORD1_X);
    CHECK_EQ((int) ch30w3_unit(2)->pos_y, CH28_MAP00_RECORD1_Y + 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);
    ch28_unstage();
}

/* ---- fdps_chapter_30_revive_wave_4_undead, 00010760 ----------------------
 *
 * Every expected value below is read off the assembly at 00010760: the CMP
 * EAX,0x55 at 000107a5 and CMP EAX,0x6a at 000107b5 that pick the two character
 * ids out of record byte +7; the CALL 0x000109b0 / TEST EAX,EAX / JNZ at
 * 000107be that makes the second half of the gate the retired predicate, which
 * is bit 0 of byte +5 alone; the CMP EAX,[0x00060150] / JL at 00010776 that
 * bounds the sweep; the four anchor literals at 0001080f..00010826; the AND
 * AL,0x40 at 00010883 that skips an occupied cell; the CMP EAX,[EBP-0x10] / JLE
 * at 000108b0 that keeps a candidate whose distance merely ties; the CMP EAX,0x5
 * / JGE at 000108ce that rejects terrain 5 and above; the CMP dword ptr
 * [EBP-0xc],0x0 / JGE at 00010932 that runs the flash down to a bias of 0
 * inclusive; and the MOV byte ptr [EAX+0x5],0x0 and MOV DX,[EAX+0x42] / MOV
 * [EAX+0x40],DX at 0001098c..0001099a that put the unit back in play.  None of
 * them is read off the emitted C.
 *
 * WHY THE SEARCH CASES DRIVE THE REAL HARDWARE.  The tile the search settles on
 * is written into the record and nowhere else, and between the search and that
 * write the function walks the cursor, uploads the palette 65 times and plays a
 * VFS clip.  None of those can be stood in for -- the container's name is a
 * literal inside fdps_play_vfs_animation_over_units and a member it cannot find
 * ends the process -- so the cases put the adapter in mode 13h, install an IRQ0
 * handler so the clip's frame waits can end, and skip themselves when MISC.VFS
 * is not staged next to the executable.
 *
 * WHY THE MAP IS EIGHT BY SIX.  Both anchors are off it -- (5, 12) and (15, 13)
 * against a map six rows deep -- which is what makes the search's answer a
 * single cell on the bottom row that can be worked out by hand, and it is also
 * the shape the real map puts the search in, because on map29 the anchor tile is
 * normally occupied.  Every cell of it is walkable unless a case says otherwise.
 *
 * WHY NOTHING IS EVER DRAWN.  The clip's own compositor repaints the scene on
 * every tick, and with no scene layers and no sprite cache staged the only thing
 * that could reach a sprite sheet is a map unit.  The unit being revived is
 * still retired while the clip plays -- the flag byte is cleared afterwards, at
 * 0001098c -- and fdps_draw_map_unit returns on a retired record; every other
 * staged unit carries portrait id 0x80, which that function returns on one line
 * earlier.  So no case here needs ICON.CEL.
 *
 * WHY THE CURSOR NEVER SCROLLS THE VIEW.  Every case starts the cursor at world
 * (120, 96) with the view at the map origin and the cursor switched off, and
 * every tile the search can settle on in these fixtures is world (96..168, 120):
 * the whole walk stays inside the 24..216 by 24..144 band fdps_map_cursor_move_to
 * holds it in, so no step moves the view and no frame is composed (tests/mapcur.c
 * stages the walk's own cases the same way).
 *
 * WHAT IS NOT COVERED.  Which pixels the flash and the clip put on the screen is
 * palette.c's and anim.c's business and is covered in their own files.  What is
 * asserted of the flash here is only its last step, read back off the DAC.
 * ------------------------------------------------------------------ */

/* The two character ids at record +7 that the revival accepts, and the four
   ids either side of them, which it must not. */
#define CH30REV_SKELETON_ID  0x55
#define CH30REV_WRAITH_ID    0x6a

/* The scripted spawn tiles the two types are measured from. */
#define CH30REV_SKELETON_ANCHOR_X 5
#define CH30REV_SKELETON_ANCHOR_Y 12
#define CH30REV_WRAITH_ANCHOR_X   15
#define CH30REV_WRAITH_ANCHOR_Y   13

/* The staged map.  Both anchors are below its bottom row on purpose. */
#define CH30REV_GRID_W 8
#define CH30REV_GRID_H 6

/* The scene-layer offsets fdps_map_load_tile_info reads through, the same ones
   the ch30w3_ fixture above uses. */
#define CH30REV_TILE_MAP_WIDTH_OFFSET   7
#define CH30REV_TILE_MAP_IDS_OFFSET     0xb
#define CH30REV_TILE_ATTR_ROWS_OFFSET   0x11
#define CH30REV_EVENT_LAYER_WIDTH_OFFSET 7
#define CH30REV_EVENT_LAYER_CELLS_OFFSET 0x10
#define CH30REV_TILE_ATTR_ROWS 16

/* Terrain codes either side of the CMP EAX,0x5 / JGE at 000108ce: 4 is the
   highest the search still accepts and 5 the lowest it rejects. */
#define CH30REV_TERRAIN_WALKABLE 1
#define CH30REV_TERRAIN_HIGHEST_ACCEPTED 4
#define CH30REV_TERRAIN_REJECTED 5

/* Tile ids the cases give one cell so that cell can carry its own terrain: id 0
   is the walkable one every other cell keeps. */
#define CH30REV_TILE_ID_ACCEPTED 2
#define CH30REV_TILE_ID_REJECTED 1

/* The movement grid's cell array starts after its 4-byte header and a cell is
   two bytes; bit 0x40 of byte 0 is "a unit stands here" and bit 0x80 is the
   zone-of-control mark.  fdps_map_grid_reset clears both and keeps the low six
   bits, so a blank grid reads 0 here. */
#define CH30REV_GRID_CELL_BASE 4
#define CH30REV_GRID_ZONE_BITS 0xc0

/* Record state the cases stage and read back.  The flag byte carries the
   retired bit 0 and the acted bit 0x80 together, so a revival that masked
   instead of assigning would leave 0x80 standing. */
#define CH30REV_FLAGS_DEAD_AND_ACTED 0x81
#define CH30REV_FLAGS_ACTED_ONLY     0x80
#define CH30REV_FLAGS_IN_PLAY        0x00
#define CH30REV_START_HP 0
#define CH30REV_MAX_HP   37

/* The portrait id fdps_draw_map_unit returns on before it touches a sprite
   sheet, which is what keeps the staged blockers out of the clip's repaint. */
#define CH30REV_PORTRAIT_NO_SPRITE 0x80

/* Where the cursor starts every case, and the tile pitch it walks in. */
#define CH30REV_CURSOR_START_X 120
#define CH30REV_CURSOR_START_Y 96
#define CH30REV_TILE_PIXELS 24

/* Where the dead unit is parked before the revival, well clear of both anchors
   and of every cell a case blocks. */
#define CH30REV_DEAD_START_X 2
#define CH30REV_DEAD_START_Y 3

/* The DAC entry the flash's last step is read back off, and the colour staged
   there.  All three components are below 63, so a bias of 0 uploads them
   unchanged and any bias above 0 does not. */
#define CH30REV_DAC_PROBE_ENTRY 200
#define CH30REV_DAC_PROBE_RED   10
#define CH30REV_DAC_PROBE_GREEN 20
#define CH30REV_DAC_PROBE_BLUE  30
#define CH30REV_DAC_READ_INDEX 0x3c7
#define CH30REV_DAC_DATA 0x3c9

/* IRQ0, and the two adapter modes.  DOS/4GW reflects a hardware interrupt taken
   in protected mode to the protected-mode vector, so the handler installed here
   is the one that runs while the clip spins on the tick counter. */
#define CH30REV_TIMER_VECTOR 8
#define CH30REV_MODE_TEXT 0x03
#define CH30REV_MODE_320X200X256 0x13

#define CH30REV_ARCHIVE "MISC.VFS"
#define CH30REV_STAGE_UNITS 4

static unsigned char ch30rev_grid[CH30REV_GRID_CELL_BASE +
                                  CH30REV_GRID_W * CH30REV_GRID_H * 2];
static unsigned char ch30rev_tile_map[CH30REV_TILE_MAP_IDS_OFFSET +
                                      CH30REV_GRID_W * CH30REV_GRID_H * 2];
static unsigned char ch30rev_tile_attr[CH30REV_TILE_ATTR_ROWS_OFFSET +
                                       CH30REV_TILE_ATTR_ROWS * 4];
static unsigned char ch30rev_event_layer[CH30REV_EVENT_LAYER_CELLS_OFFSET +
                                         CH30REV_GRID_W * CH30REV_GRID_H];
static struct fdps_unit_record ch30rev_units[CH30REV_STAGE_UNITS];
static struct fdps_palette_entry ch30rev_palette[256];

static int ch30rev_archive_checked = 0;
static int ch30rev_archive_ready = 0;

static void (__interrupt __far *ch30rev_saved_timer)();

/* What the DAC held for the probe entry when the call came back, sampled while
   the adapter is still in mode 13h. */
static int ch30rev_dac_red;
static int ch30rev_dac_green;
static int ch30rev_dac_blue;

static void __interrupt __far ch30rev_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(ch30rev_saved_timer);
}

/* Only the container's presence is tested: the member name is a literal inside
   fdps_play_vfs_animation_over_units, so there is nothing to point at a smaller
   file, and a member it cannot find ends the process rather than failing a
   check. */
static void ch30rev_ensure_archive(void)
{
    FILE *fp;

    if (ch30rev_archive_checked) {
        return;
    }
    ch30rev_archive_checked = 1;
    fp = fopen(CH30REV_ARCHIVE, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);
    ch30rev_archive_ready = 1;
}

static void ch30rev_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

static struct fdps_move_grid_cell *ch30rev_cell(int tile_x, int tile_y)
{
    return (struct fdps_move_grid_cell *)
           (ch30rev_grid + CH30REV_GRID_CELL_BASE) +
           (tile_y * CH30REV_GRID_W + tile_x);
}

static void ch30rev_set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch30rev_tile_attr + CH30REV_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

static void ch30rev_set_tile_id(int tile_x, int tile_y, int tile_id)
{
    short *tile_ids;

    tile_ids = (short *) (ch30rev_tile_map + CH30REV_TILE_MAP_IDS_OFFSET);
    tile_ids[tile_y * CH30REV_GRID_W + tile_x] = (short) tile_id;
}

/* A blank walkable eight by six map with the dead undead of the given character
   id at index 0 and nothing else on it, the cursor parked at its start position
   with the view at the map origin, and a palette whose probe entry carries a
   colour the DAC can hold unchanged.

   unit_count is what data_fdps_map_unit_count is left on, so a case can stage a
   record the sweep must not reach by putting it above the count. */
static void ch30rev_stage(int char_id, int unit_count)
{
    int i;

    memset(ch30rev_grid, 0, sizeof(ch30rev_grid));
    memset(ch30rev_tile_map, 0, sizeof(ch30rev_tile_map));
    memset(ch30rev_tile_attr, 0, sizeof(ch30rev_tile_attr));
    memset(ch30rev_event_layer, 0, sizeof(ch30rev_event_layer));
    memset(ch30rev_units, 0, sizeof(ch30rev_units));

    *(short *) ch30rev_grid = (short) CH30REV_GRID_W;
    *(short *) (ch30rev_grid + 2) = (short) CH30REV_GRID_H;

    *(short *) (ch30rev_tile_map + CH30REV_TILE_MAP_WIDTH_OFFSET) =
        (short) CH30REV_GRID_W;
    for (i = 0; i < CH30REV_TILE_ATTR_ROWS; i++) {
        ch30rev_set_terrain(i, CH30REV_TERRAIN_WALKABLE);
    }

    *(short *) (ch30rev_event_layer + CH30REV_EVENT_LAYER_WIDTH_OFFSET) =
        (short) CH30REV_GRID_W;

    for (i = 0; i < 256; i++) {
        ch30rev_palette[i].red = 0;
        ch30rev_palette[i].green = 0;
        ch30rev_palette[i].blue = 0;
    }
    ch30rev_palette[CH30REV_DAC_PROBE_ENTRY].red = CH30REV_DAC_PROBE_RED;
    ch30rev_palette[CH30REV_DAC_PROBE_ENTRY].green = CH30REV_DAC_PROBE_GREEN;
    ch30rev_palette[CH30REV_DAC_PROBE_ENTRY].blue = CH30REV_DAC_PROBE_BLUE;

    ch30rev_units[0].portrait_id = (unsigned char) char_id;
    ch30rev_units[0].flags = CH30REV_FLAGS_DEAD_AND_ACTED;
    ch30rev_units[0].pos_x = CH30REV_DEAD_START_X;
    ch30rev_units[0].pos_y = CH30REV_DEAD_START_Y;
    ch30rev_units[0].hp_current = CH30REV_START_HP;
    ch30rev_units[0].hp_max = CH30REV_MAX_HP;

    data_fdps_battle_move_grid_ptr = ch30rev_grid;
    data_fdps_scene_layer_tile_map_ptrs[0] = ch30rev_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ch30rev_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = ch30rev_event_layer;
    data_fdps_vga_main_palette_ptr = (unsigned char *) ch30rev_palette;
    data_fdps_map_unit_array_ptr = (unsigned char *) ch30rev_units;
    data_fdps_map_unit_count = unit_count;

    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_map_unit_walk_anim_counter = 0;
    data_fdps_map_cursor_world_x = CH30REV_CURSOR_START_X;
    data_fdps_map_cursor_world_y = CH30REV_CURSOR_START_Y;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
}

/* A unit still in play, which is what puts the 0x40 bit on its tile when the
   two marking passes run.  Its portrait id keeps it out of the clip's repaint,
   and its side decides which of the two passes marks it -- side 0 is marked by
   the pass with a non-zero selector and every other side by the pass with 0 --
   so a fixture that uses both sides needs both passes to have run. */
static void ch30rev_set_blocker(int index, int tile_x, int tile_y, int side)
{
    ch30rev_units[index].portrait_id = CH30REV_PORTRAIT_NO_SPRITE;
    ch30rev_units[index].flags = CH30REV_FLAGS_IN_PLAY;
    ch30rev_units[index].side = (unsigned char) side;
    ch30rev_units[index].pos_x = (unsigned char) tile_x;
    ch30rev_units[index].pos_y = (unsigned char) tile_y;
}

/* Put the staged globals back the way a freshly started program has them, for
   the reason tests/anim.c gives: a later unit that expects an empty battle would
   otherwise inherit this fixture through the array pointer. */
static void ch30rev_unstage(void)
{
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_vga_main_palette_ptr = NULL;
    data_fdps_scene_layer_tile_map_ptrs[0] = NULL;
    data_fdps_scene_layer_tile_attr_ptr[0] = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
}

/* One whole call with the adapter in the mode the game plays it in and a timer
   interrupt running, sampling the DAC's probe entry before the mode goes back so
   the flash's last upload is still on the hardware. */
static void ch30rev_run(void)
{
    ch30rev_set_mode(CH30REV_MODE_320X200X256);
    ch30rev_saved_timer = _dos_getvect(CH30REV_TIMER_VECTOR);
    _dos_setvect(CH30REV_TIMER_VECTOR, ch30rev_timer_isr);

    fdps_chapter_30_revive_wave_4_undead();

    outp(CH30REV_DAC_READ_INDEX, CH30REV_DAC_PROBE_ENTRY);
    ch30rev_dac_red = inp(CH30REV_DAC_DATA);
    ch30rev_dac_green = inp(CH30REV_DAC_DATA);
    ch30rev_dac_blue = inp(CH30REV_DAC_DATA);

    _dos_setvect(CH30REV_TIMER_VECTOR, ch30rev_saved_timer);
    ch30rev_set_mode(CH30REV_MODE_TEXT);
}

/* The fields the cases read back and the stride they are indexed by.  The
   character id the gate tests is record byte +7, which struct fdps_unit_record
   names portrait_id -- fdps_deploy_unit writes the deployment record's character
   id into both +7 and +8, so the two carry the same number and only +7 is what
   this function loads. */
static void ch30rev_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 7);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
    CHECK_EQ((int) sizeof(struct fdps_move_grid_cell), 2);
}

/* The two accepted ids are equality tests and not a range: the four ids either
   side of them are staged dead, one at a time, and none of them is touched.
   0x54 and 0x56 straddle 白骨戰士 and 0x69 and 0x6b straddle 死靈, so an
   off-by-one on either compare, or a range test written across the pair, would
   show here.  Nothing runs, so this case needs no game file. */
static void ch30rev_only_the_two_undead_ids_are_revived(void)
{
    static int rejected_ids[4] = {0x54, 0x56, 0x69, 0x6b};
    int i;

    for (i = 0; i < 4; i++) {
        ch30rev_stage(rejected_ids[i], 1);

        fdps_chapter_30_revive_wave_4_undead();

        CHECK_EQ((int) ch30rev_units[0].pos_x, CH30REV_DEAD_START_X);
        CHECK_EQ((int) ch30rev_units[0].pos_y, CH30REV_DEAD_START_Y);
        CHECK_EQ((int) ch30rev_units[0].flags, CH30REV_FLAGS_DEAD_AND_ACTED);
        CHECK_EQ((int) ch30rev_units[0].hp_current, CH30REV_START_HP);
    }
    ch30rev_unstage();
}

/* The second half of the gate is the retired predicate, which is bit 0 of the
   flag byte alone.  Both accepted ids are staged twice: once with a clear flag
   byte and once with only the acted bit 0x80 up, which is the value that would
   pass a gate written as "the flag byte is non-zero".  Neither is revived, so
   nothing runs and no game file is needed. */
static void ch30rev_a_living_undead_is_left_alone(void)
{
    static int accepted_ids[2] = {CH30REV_SKELETON_ID, CH30REV_WRAITH_ID};
    static int living_flags[2] = {CH30REV_FLAGS_IN_PLAY,
                                  CH30REV_FLAGS_ACTED_ONLY};
    int id;
    int flag;

    for (id = 0; id < 2; id++) {
        for (flag = 0; flag < 2; flag++) {
            ch30rev_stage(accepted_ids[id], 1);
            ch30rev_units[0].flags = (unsigned char) living_flags[flag];

            fdps_chapter_30_revive_wave_4_undead();

            CHECK_EQ((int) ch30rev_units[0].pos_x, CH30REV_DEAD_START_X);
            CHECK_EQ((int) ch30rev_units[0].pos_y, CH30REV_DEAD_START_Y);
            CHECK_EQ((int) ch30rev_units[0].flags, living_flags[flag]);
            CHECK_EQ((int) ch30rev_units[0].hp_current, CH30REV_START_HP);
        }
    }
    ch30rev_unstage();
}

/* The sweep is bounded by data_fdps_map_unit_count and by nothing else: a dead
   白骨戰士 is staged at index 1 with the count left on 1, so the record exists
   and qualifies but is one past the bound.  A walk that ran to the end of the array,
   or one that used <= against the count, would revive it. */
static void ch30rev_the_sweep_stops_at_the_unit_count(void)
{
    ch30rev_stage(CH30REV_SKELETON_ID, 1);
    ch30rev_units[0].portrait_id = CH30REV_PORTRAIT_NO_SPRITE;
    ch30rev_units[1].portrait_id = CH30REV_SKELETON_ID;
    ch30rev_units[1].flags = CH30REV_FLAGS_DEAD_AND_ACTED;
    ch30rev_units[1].pos_x = CH30REV_DEAD_START_X;
    ch30rev_units[1].pos_y = CH30REV_DEAD_START_Y;
    ch30rev_units[1].hp_max = CH30REV_MAX_HP;

    fdps_chapter_30_revive_wave_4_undead();

    CHECK_EQ((int) ch30rev_units[1].pos_x, CH30REV_DEAD_START_X);
    CHECK_EQ((int) ch30rev_units[1].pos_y, CH30REV_DEAD_START_Y);
    CHECK_EQ((int) ch30rev_units[1].flags, CH30REV_FLAGS_DEAD_AND_ACTED);
    CHECK_EQ((int) ch30rev_units[1].hp_current, CH30REV_START_HP);
    ch30rev_unstage();
}

/* One dead 白骨戰士 on an empty map.  Its anchor is (5, 12), the map is six rows
   deep and every cell is free and walkable, so the shortest Manhattan distance
   any cell can reach is 7 and exactly one cell reaches it: (5, 5).  That is the
   whole revival read back at once -- the tile, the cursor walked onto it, the
   flag byte assigned 0 rather than masked, max HP copied over current HP, and
   the grid left blank behind it.
   The cursor is the ordering assertion: it is walked to the unit AFTER the tile
   bytes are written, so a cursor that ended at the record's old tile (2, 3)
   would say the two had swapped.
   The DAC probe is the flash's last step: the fade runs the bias down to 0
   inclusive, so the final upload is the staged palette unbiased.  A loop that
   stopped at 1 would leave every component one higher. */
static void ch30rev_the_skeleton_lands_on_the_nearest_free_walkable_tile(void)
{
    ch30rev_ensure_archive();
    if (!ch30rev_archive_ready) {
        return;
    }

    ch30rev_stage(CH30REV_SKELETON_ID, 1);

    ch30rev_run();

    CHECK_EQ((int) ch30rev_units[0].pos_x, 5);
    CHECK_EQ((int) ch30rev_units[0].pos_y, 5);
    CHECK_EQ((int) ch30rev_units[0].flags, CH30REV_FLAGS_IN_PLAY);
    CHECK_EQ((int) ch30rev_units[0].hp_current, CH30REV_MAX_HP);
    CHECK_EQ((int) ch30rev_units[0].hp_max, CH30REV_MAX_HP);
    CHECK_EQ(data_fdps_map_cursor_world_x, 5 * CH30REV_TILE_PIXELS);
    CHECK_EQ(data_fdps_map_cursor_world_y, 5 * CH30REV_TILE_PIXELS);
    CHECK_EQ(data_fdps_battle_view_window_origin_x, 0);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 0);
    CHECK_EQ(ch30rev_cell(5, 5)->flags & CH30REV_GRID_ZONE_BITS, 0);
    CHECK_EQ(ch30rev_dac_red, CH30REV_DAC_PROBE_RED);
    CHECK_EQ(ch30rev_dac_green, CH30REV_DAC_PROBE_GREEN);
    CHECK_EQ(ch30rev_dac_blue, CH30REV_DAC_PROBE_BLUE);
    ch30rev_unstage();
}

/* The tie-break, and the one thing here that cannot be got right by writing the
   obvious comparison.  Two units still in play stand on (5, 5) and (5, 4), which
   the two marking passes turn into the 0x40 bit the search skips -- one is on
   the enemy side and one on the player side, so both passes have to have run for
   the pair to be blocked.  That leaves three cells tied at distance 8: (5, 3) is
   not one of them, but (4, 5) and (6, 5) are, and so is (4, 4) at distance 9
   which loses.  In row-major order (4, 5) comes before (6, 5), and the accepting
   compare is "no greater than", so (6, 5) is the tile.  A search written with
   "<" keeps (4, 5) instead. */
static void ch30rev_a_tie_at_the_shortest_distance_keeps_the_last_cell(void)
{
    ch30rev_ensure_archive();
    if (!ch30rev_archive_ready) {
        return;
    }

    ch30rev_stage(CH30REV_SKELETON_ID, 3);
    ch30rev_set_blocker(1, 5, 5, 0);
    ch30rev_set_blocker(2, 5, 4, 2);

    ch30rev_run();

    CHECK_EQ((int) ch30rev_units[0].pos_x, 6);
    CHECK_EQ((int) ch30rev_units[0].pos_y, 5);
    CHECK_EQ(data_fdps_map_cursor_world_x, 6 * CH30REV_TILE_PIXELS);
    CHECK_EQ(data_fdps_map_cursor_world_y, 5 * CH30REV_TILE_PIXELS);
    CHECK_EQ((int) ch30rev_units[1].pos_x, 5);
    CHECK_EQ((int) ch30rev_units[1].pos_y, 5);
    CHECK_EQ((int) ch30rev_units[2].pos_x, 5);
    CHECK_EQ((int) ch30rev_units[2].pos_y, 4);
    CHECK_EQ(ch30rev_cell(5, 5)->flags & CH30REV_GRID_ZONE_BITS, 0);
    CHECK_EQ(ch30rev_cell(4, 5)->flags & CH30REV_GRID_ZONE_BITS, 0);
    ch30rev_unstage();
}

/* The terrain bound is "below 5" and not "5 or below".  The fixture is the tie
   above, whose winner is (6, 5); giving that one cell a tile id whose attribute
   row says 5 takes it out of the search and the tile falls back to (4, 5), and
   giving the same cell 4 instead leaves it the winner.  The rejected run also
   shows that a rejected cell does not move the best-so-far distance: (7, 5) sits
   one further out at distance 9 and is still not taken. */
static void ch30rev_a_tile_over_the_terrain_limit_is_rejected(void)
{
    ch30rev_ensure_archive();
    if (!ch30rev_archive_ready) {
        return;
    }

    ch30rev_stage(CH30REV_SKELETON_ID, 3);
    ch30rev_set_blocker(1, 5, 5, 0);
    ch30rev_set_blocker(2, 5, 4, 2);
    ch30rev_set_tile_id(6, 5, CH30REV_TILE_ID_REJECTED);
    ch30rev_set_terrain(CH30REV_TILE_ID_REJECTED, CH30REV_TERRAIN_REJECTED);

    ch30rev_run();

    CHECK_EQ((int) ch30rev_units[0].pos_x, 4);
    CHECK_EQ((int) ch30rev_units[0].pos_y, 5);

    ch30rev_stage(CH30REV_SKELETON_ID, 3);
    ch30rev_set_blocker(1, 5, 5, 0);
    ch30rev_set_blocker(2, 5, 4, 2);
    ch30rev_set_tile_id(6, 5, CH30REV_TILE_ID_ACCEPTED);
    ch30rev_set_terrain(CH30REV_TILE_ID_ACCEPTED,
                        CH30REV_TERRAIN_HIGHEST_ACCEPTED);

    ch30rev_run();

    CHECK_EQ((int) ch30rev_units[0].pos_x, 6);
    CHECK_EQ((int) ch30rev_units[0].pos_y, 5);
    ch30rev_unstage();
}

/* The two types measure from two different anchors, and which one is used comes
   off the same record byte the gate tested.  A dead 死靈 on the same empty
   map is measured from (15, 13) rather than (5, 12), so the nearest free
   walkable cell is the bottom right corner (7, 5) and not (5, 5) -- the tile the
   白骨戰士 case above gets off the identical fixture. */
static void ch30rev_the_wraith_measures_from_its_own_anchor(void)
{
    ch30rev_ensure_archive();
    if (!ch30rev_archive_ready) {
        return;
    }

    ch30rev_stage(CH30REV_WRAITH_ID, 1);

    ch30rev_run();

    CHECK_EQ((int) ch30rev_units[0].pos_x, 7);
    CHECK_EQ((int) ch30rev_units[0].pos_y, 5);
    CHECK_EQ((int) ch30rev_units[0].flags, CH30REV_FLAGS_IN_PLAY);
    CHECK_EQ((int) ch30rev_units[0].hp_current, CH30REV_MAX_HP);
    CHECK_EQ(data_fdps_map_cursor_world_x, 7 * CH30REV_TILE_PIXELS);
    CHECK_EQ(data_fdps_map_cursor_world_y, 5 * CH30REV_TILE_PIXELS);
    ch30rev_unstage();
}

/* Nothing latches, and the only thing that stops a second pass is the gate
   itself.  The same fixture is run twice: the first call revives the unit, and
   the second finds a record that is no longer retired and leaves the whole map
   alone.  The cursor is parked somewhere the walk would have to move it away
   from before the second call, so a body that ran again would say so even if it
   settled on the same tile. */
static void ch30rev_a_revived_unit_is_not_revived_again(void)
{
    ch30rev_ensure_archive();
    if (!ch30rev_archive_ready) {
        return;
    }

    ch30rev_stage(CH30REV_SKELETON_ID, 1);

    ch30rev_run();

    CHECK_EQ((int) ch30rev_units[0].pos_x, 5);
    CHECK_EQ((int) ch30rev_units[0].pos_y, 5);
    CHECK_EQ((int) ch30rev_units[0].flags, CH30REV_FLAGS_IN_PLAY);

    ch30rev_units[0].hp_current = CH30REV_START_HP;
    data_fdps_map_cursor_world_x = CH30REV_CURSOR_START_X;
    data_fdps_map_cursor_world_y = CH30REV_CURSOR_START_Y;

    fdps_chapter_30_revive_wave_4_undead();

    CHECK_EQ((int) ch30rev_units[0].pos_x, 5);
    CHECK_EQ((int) ch30rev_units[0].pos_y, 5);
    CHECK_EQ((int) ch30rev_units[0].hp_current, CH30REV_START_HP);
    CHECK_EQ(data_fdps_map_cursor_world_x, CH30REV_CURSOR_START_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH30REV_CURSOR_START_Y);
    ch30rev_unstage();
}

/* ---- fdps_chapter_30_event_deploy_wave_4, 00039770 -----------------------
 *
 * Every expected value below is read off the assembly at 00039770: the CMP byte
 * ptr [0x000640e8],0x0 / JNZ at 0003977c that gates the whole body and the MOV
 * byte ptr [0x000640e8],0x1 at 00039830 that closes it; the XOR EAX,EAX / PUSH
 * EAX / PUSH 0x4 / PUSH dword ptr [0x00069cf4] at 00039789..0003978e that name
 * the wave, the placement and the map; the two literal stores into the cursor
 * draw mode at 0003979c and 00039803; the PUSH 0x150 / PUSH 0x60 at 000397a6 and
 * PUSH 0x150 / PUSH 0x198 at 000397d3 that name the two pan targets; the two CMP
 * dword ptr [EBP+0x14],0xc / JL at 000397bc and 000397ec that count the holds;
 * and the seven pushes at 0003980d..00039822 that hand text entry 8 to
 * fdps_draw_text at the aperture origin in the standard message colours.
 *
 * The cases reuse the ch30w3_ fixture above -- the same blank walkable map, the
 * same three deployment records at table indices 0, 1 and 2, the same MAP00.COD
 * placement records -- because the wave the handler asked for is only readable
 * off the record that arrived.  The middle record carries wave 4 and the other
 * two carry one wave either side of it, so a key that drifted in either
 * direction deploys a different character id onto a different tile.
 *
 * WHY THE CURSOR STARTS ON THE SECOND PAN TARGET.  Both pans end where the
 * assembly says and only the second one's target survives in the cursor
 * globals, so a final position of (0x198, 0x150) cannot on its own tell two
 * pans from one.  Starting the cursor exactly on that target is what makes the
 * difference show up somewhere else: fdps_map_cursor_move_to returns at once
 * for a target the cursor already stands on, so a handler that had lost the
 * first pan would move nothing at all and leave the view window on the zeroes
 * the fixture staged, while the real one walks thirteen tiles west, holds, and
 * walks thirteen back, dragging the window along behind it.  The two view
 * origin assertions are what pin that the first pan ran.  Neither the final
 * cursor position nor the tick bound covers it -- both survive its loss -- so
 * dropping those two assertions would leave the first pan unasserted.
 *
 * WHERE THE VIEW WINDOW ENDS UP.  Only the walks move it: fdps_render_view_frame
 * reads both origins and writes neither (src/mapdraw.c), so the holds contribute
 * nothing.  Each walk drags the window on the steps that push the cursor past
 * the window's far edge, leaving origin_x at cursor_x - 0xd8 and origin_y at
 * cursor_y - 0x90 (src/mapcur.c, CURSOR_VIEW_MAX_OFFSET_X and _Y); on this
 * fixture's 32 by 16 map neither of the two clamps against the map's extent
 * bites.  So a run that made both walks ends on (0x198 - 0xd8, 0x150 - 0x90) =
 * (0xc0, 0xc0), and a run that made neither ends on the staged (0, 0).
 *
 * WHAT THE TICK BOUND SEPARATES.  A run that kept both hold loops from one that
 * lost a loop, and nothing else.  The two holds are twenty-four calls into
 * fdps_render_view_frame and each of those waits for the timer tick to move, so
 * a correct run clears twenty-three and a run down to one hold falls to roughly
 * half of it.  Twenty-four hold frames clear the bound on their own, so it says
 * nothing about whether either pan ran.  It is one-sided on purpose -- the walks
 * compose frames of their own on every step that scrolls the view, so a correct
 * run always spends more than the two holds' ticks and can never fail here.
 *
 * WHY THE LINE IS READ BACK OFF THE SCREEN.  fdps_draw_text writes to the mode
 * 13h aperture and hands back a cursor this handler discards, so the only place
 * the draw is visible is video memory.  The staged text block points every entry
 * at a lone terminator except entry 8, which is one solid glyph, so a draw that
 * asked for another entry paints nothing and the aperture's first pixel does not
 * come out at the message foreground colour.  The pixel is sampled before the
 * adapter goes back to text mode, because that mode set clears it.
 *
 * The unit the fixture already has on the map is given portrait id 0x80, the id
 * fdps_draw_map_unit returns on before it reads anything else off a record, so
 * the composed frames do not depend on what a sprite cache slot holds.
 */

/* The slot of data_fdps_map_cell_event_triggered_flags this handler gates on
   and latches -- element 0x10, the same one CH29_LATCH_SLOT above names. */
#define CH30W4_LATCH_SLOT CH29_LATCH_SLOT

/* The wave the ambush asks for, PUSH 0x4 at 0003978c, and the two waves the
   decoy records either side of it carry. */
#define CH30W4_WAVE 4
#define CH30W4_WAVE_BELOW 3
#define CH30W4_WAVE_ABOVE 5

/* A wave tag no reading of this handler can select, used when a case wants the
   deployment to match nothing at all. */
#define CH30W4_UNSELECTED_WAVE 0x30

/* The two pan targets, PUSH 0x150 / PUSH 0x60 at 000397a6 and PUSH 0x150 /
   PUSH 0x198 at 000397d3.  Both are whole multiples of the 24-pixel tile the
   walk steps by, so the cursor arrives on them exactly (mapcur.h). */
#define CH30W4_PAN_LEFT_X 0x60
#define CH30W4_PAN_LEFT_Y 0x150
#define CH30W4_PAN_RIGHT_X 0x198
#define CH30W4_PAN_RIGHT_Y 0x150

/* How long each hold is, CMP dword ptr [EBP+0x14],0xc / JL at 000397bc and
   000397ec, and the least the tick counter can move across a firing call: two
   holds less one, so a slow machine cannot fail it and a rebuild that lost
   either loop cannot reach it. */
#define CH30W4_HOLD_FRAMES 0xc
#define CH30W4_LEAST_PAN_TICKS (2 * CH30W4_HOLD_FRAMES - 1)

/* How far the cursor may get from the view window's origin before a walk step
   drags the window after it, src/mapcur.c's CURSOR_VIEW_MAX_OFFSET_X and _Y,
   and where the two walks together therefore leave the window.  This is the one
   observable in this fixture that a lost first pan changes: with the cursor
   parked on the second target a handler missing the first walk moves nothing,
   so the window stays on the (0, 0) the fixture staged. */
#define CH30W4_VIEW_MAX_OFFSET_X 0xd8
#define CH30W4_VIEW_MAX_OFFSET_Y 0x90
#define CH30W4_END_VIEW_ORIGIN_X (CH30W4_PAN_RIGHT_X - CH30W4_VIEW_MAX_OFFSET_X)
#define CH30W4_END_VIEW_ORIGIN_Y (CH30W4_PAN_RIGHT_Y - CH30W4_VIEW_MAX_OFFSET_Y)

/* The cursor mode the fixture parks before every run: neither of the two values
   the handler writes, so a run that left the global alone, one that hid the
   cursor and never put it back, and one that restored what it found are all told
   apart from the 1 the handler is required to leave behind. */
#define CH30W4_STAGED_CURSOR_MODE 4
#define CH30W4_CURSOR_MODE_NORMAL 1

/* A cursor position that is neither pan target, so a blocked call is
   distinguishable from a firing one by where the cursor ends up. */
#define CH30W4_PARKED_CURSOR_X 0x18
#define CH30W4_PARKED_CURSOR_Y 0x18

/* The entry the line is spoken from, PUSH 0x8 at 00039820, and the block that
   holds it: entries all pointing at a lone terminator except that one, which
   points at a single solid glyph followed by a terminator. */
#define CH30W4_TEXT_ID 8
#define CH30W4_TEXT_ENTRIES 0x10
#define CH30W4_TEXT_TERMINATOR (-1)
#define CH30W4_TEXT_EMPTY_AT (CH30W4_TEXT_ENTRIES * 2)
#define CH30W4_TEXT_GLYPH_AT ((CH30W4_TEXT_ENTRIES + 1) * 2)
#define CH30W4_TEXT_SLOTS (CH30W4_TEXT_ENTRIES + 3)

/* The synthetic font: one 8 by 8 cell per glyph, one byte to the row and the
   most significant bit leftmost, with glyph 1 solid and glyph 0 blank.  With the
   outline flag clear and both shadow offsets zero, the shadow lands on the cell
   itself and the body is drawn over it, so a painted pixel comes out at the
   foreground colour the handler pushes. */
#define CH30W4_FONT_GLYPHS 2
#define CH30W4_FONT_W 8
#define CH30W4_FONT_H 8
#define CH30W4_FONT_STRIDE 8
#define CH30W4_FONT_SOLID_GLYPH 1

/* The colours pushed at 00039811, 0003980f and 0003980d, and the aperture the
   line is drawn at, PUSH 0xa0000 at 0003981b. */
#define CH30W4_TEXT_FG_COLOR 0xd0
#define CH30W4_VGA_ORIGIN 0x000a0000

/* A pixel value the glyph cannot leave behind, stamped on the aperture's first
   byte before a run so a call that painted nothing is visible as itself. */
#define CH30W4_SCREEN_SENTINEL 0x11

/* Where MAP00.COD's placement records 0 and 1 and MAP01.COD's record 0 put a
   unit -- the same coordinates the ch30w3_ and ch28_ cases above read back. */
#define CH30W4_MAP00_RECORD0_X 18
#define CH30W4_MAP00_RECORD0_Y 0
#define CH30W4_MAP00_RECORD1_X 22
#define CH30W4_MAP00_RECORD1_Y 12
#define CH30W4_MAP01_RECORD0_X 9
#define CH30W4_MAP01_RECORD0_Y 4

static short ch30w4_text[CH30W4_TEXT_SLOTS];
static unsigned char ch30w4_font[CH30W4_FONT_GLYPHS * CH30W4_FONT_STRIDE];
static unsigned int ch30w4_ticks_before;
static unsigned int ch30w4_ticks_after;
static unsigned char ch30w4_screen_pixel;

/* The chapter text block and the font under it.  Only entry 8 paints. */
static void ch30w4_stage_text(void)
{
    int entry;

    for (entry = 0; entry < CH30W4_TEXT_ENTRIES; entry++) {
        ch30w4_text[entry] = (short) CH30W4_TEXT_EMPTY_AT;
    }
    ch30w4_text[CH30W4_TEXT_ENTRIES] = (short) CH30W4_TEXT_TERMINATOR;
    ch30w4_text[CH30W4_TEXT_ENTRIES + 1] = (short) CH30W4_FONT_SOLID_GLYPH;
    ch30w4_text[CH30W4_TEXT_ENTRIES + 2] = (short) CH30W4_TEXT_TERMINATOR;
    ch30w4_text[CH30W4_TEXT_ID] = (short) CH30W4_TEXT_GLYPH_AT;

    memset(ch30w4_font, 0, sizeof(ch30w4_font));
    for (entry = 0; entry < CH30W4_FONT_H; entry++) {
        ch30w4_font[CH30W4_FONT_SOLID_GLYPH * CH30W4_FONT_STRIDE + entry] =
            0xff;
    }

    data_fdps_current_chapter_text_ptr = (unsigned char *) ch30w4_text;
    data_fdps_font_sheet_ptr = ch30w4_font;
    data_fdps_font_glyph_width = CH30W4_FONT_W;
    data_fdps_glyph_cell_height = CH30W4_FONT_H;
    data_fdps_font_glyph_stride_bytes = CH30W4_FONT_STRIDE;
    data_fdps_font_outline_enabled_flag = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_glyph_advance_x = CH30W4_FONT_W;
    data_fdps_font_line_height = CH30W4_FONT_H;
}

/* The three deployment records, tagged around the wave the handler must ask
   for, so a key that drifted either way deploys a different character id onto a
   different tile. */
static void ch30w4_tag_decoys(int wave_no)
{
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH30W3_WAVE2_CHAR_ID, wave_no - 1);
    ch30w3_set_spawn(CH30W3_WAVE3_RECORD, CH30W3_WAVE3_CHAR_ID, wave_no);
    ch30w3_set_spawn(CH30W3_WAVE4_RECORD, CH30W3_WAVE4_CHAR_ID, wave_no + 1);
}

/* The ch30w3_ fixture on the given turn with the decoys laid down around wave 4,
   the compositor's globals put where a frame can be composed against them, the
   cursor parked on the second pan target and the latch down. */
static void ch30w4_stage(int battle_turn)
{
    ch30w3_stage(battle_turn);
    ch30w4_tag_decoys(CH30W4_WAVE);
    ch30w4_stage_text();

    ch30w3_unit(0)->portrait_id = CH28_PORTRAIT_NO_SPRITE;

    data_fdps_scene_layer_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_map_unit_walk_anim_counter = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = CH30W4_PAN_RIGHT_X;
    data_fdps_map_cursor_world_y = CH30W4_PAN_RIGHT_Y;
    data_fdps_map_cursor_draw_mode = CH30W4_STAGED_CURSOR_MODE;
    data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT] = 0;
}

/* One whole call with the adapter in the mode the game plays it in and a timer
   interrupt running, with the tick counter sampled either side and the
   aperture's first pixel read back before the adapter leaves graphics mode. */
static void ch30w4_run(int unit_index)
{
    ch28_set_mode(CH28_MODE_320X200X256);
    *(unsigned char *) CH30W4_VGA_ORIGIN = CH30W4_SCREEN_SENTINEL;
    ch28_saved_timer = _dos_getvect(CH28_TIMER_VECTOR);
    _dos_setvect(CH28_TIMER_VECTOR, ch28_timer_isr);
    ch30w4_ticks_before = data_fdps_timer_tick_counter;
    fdps_chapter_30_event_deploy_wave_4(unit_index);
    ch30w4_ticks_after = data_fdps_timer_tick_counter;
    _dos_setvect(CH28_TIMER_VECTOR, ch28_saved_timer);
    ch30w4_screen_pixel = *(unsigned char *) CH30W4_VGA_ORIGIN;
    ch28_set_mode(CH28_MODE_TEXT);
}

/* Back to the state a freshly started program has these in, for the reason
   ch28_unstage gives: a later unit that expects an empty battle, an unloaded
   chapter and no font would otherwise inherit this fixture. */
static void ch30w4_unstage(void)
{
    ch28_unstage();
    data_fdps_current_chapter_text_ptr = NULL;
    data_fdps_font_sheet_ptr = NULL;
    data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT] = 0;
}

/* A latch that is already up sends the body straight to the epilogue: no
   deployment, no cursor mode store, no pan and no line.  The compare is against
   0 with a JNZ, so every non-zero value blocks it and not just the 1 the handler
   writes; 1, 2 and 0xff are put through and the slot is asserted to come back
   holding what it was given, because a handler that rewrote it would say so.
   None of these runs reaches the adapter, so no game file is needed. */
static void ch30w4_a_raised_latch_blocks_the_whole_body(void)
{
    static int latched[3] = {1, 2, 0xff};
    int i;

    for (i = 0; i < 3; i++) {
        ch30w4_stage(0);
        data_fdps_map_cursor_world_x = CH30W4_PARKED_CURSOR_X;
        data_fdps_map_cursor_world_y = CH30W4_PARKED_CURSOR_Y;
        data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT] =
            (unsigned char) latched[i];

        fdps_chapter_30_event_deploy_wave_4(0);

        CHECK_EQ(data_fdps_map_unit_count, 1);
        CHECK_EQ(data_fdps_map_cursor_world_x, CH30W4_PARKED_CURSOR_X);
        CHECK_EQ(data_fdps_map_cursor_world_y, CH30W4_PARKED_CURSOR_Y);
        CHECK_EQ(data_fdps_map_cursor_draw_mode, CH30W4_STAGED_CURSOR_MODE);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT],
                 latched[i]);
    }
    ch30w4_unstage();
}

/* The whole body with the latch down: the wave-4 record and no other arrives, on
   MAP00.COD's record 1 at (22, 12); the cursor ends on the second pan target
   with the draw mode on 1 -- not the 4 the fixture parked and not the 0 the pans
   ran under, so neither a run that saved and restored the mode nor one that left
   the blank behind passes; the view window ends at (0xc0, 0xc0), which only the
   west walk and the walk back can put it at and which a run that lost the first
   pan leaves on the staged (0, 0); the tick counter has moved by at least
   twenty-three, which no run missing a hold loop reaches; the aperture's first
   pixel is the message foreground colour, so entry 8 was drawn over the
   sentinel; and the latch is up. */
static void ch30w4_deploys_wave_four_pans_and_latches(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w4_stage(0);

    ch30w4_run(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W4_MAP00_RECORD1_X);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W4_MAP00_RECORD1_Y);
    CHECK_EQ(data_fdps_map_cursor_world_x, CH30W4_PAN_RIGHT_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH30W4_PAN_RIGHT_Y);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH30W4_CURSOR_MODE_NORMAL);
    CHECK_EQ(data_fdps_battle_view_window_origin_x, CH30W4_END_VIEW_ORIGIN_X);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, CH30W4_END_VIEW_ORIGIN_Y);
    CHECK_EQ(ch30w4_ticks_after - ch30w4_ticks_before
                 >= (unsigned int) CH30W4_LEAST_PAN_TICKS,
             1);
    CHECK_EQ(ch30w4_screen_pixel, CH30W4_TEXT_FG_COLOR);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT], 1);
    ch30w4_unstage();
}

/* The wave is the literal 4 and not the battle turn counter, which is what every
   turn-scheduled handler of this family pushes instead.  The counter is driven
   to three values that would each pick a decoy: read raw, turn 3 selects the
   record below and turn 5 the record above, and halved, turns 0, 3 and 5 select
   waves 0, 1 and 2 and would deploy nothing at all.  The same wave-4 record
   arrives on the same tile every time. */
static void ch30w4_wave_number_is_a_literal(void)
{
    static int turns[3] = {0, 3, 5};
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < 3; i++) {
        ch30w4_stage(turns[i]);

        ch30w4_run(0);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W4_MAP00_RECORD1_X);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W4_MAP00_RECORD1_Y);
    }
    ch30w4_unstage();
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the wave-4 tag is moved onto table index 0 and the handler run twice, once
   with that global on 0 and once on 1, and the record lands on MAP00.COD's
   record 0 at (18, 0) and then on MAP01.COD's record 0 at (9, 4). */
static void ch30w4_map_number_comes_from_the_chapter_global(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w4_stage(0);
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH30W3_WAVE2_CHAR_ID, CH30W4_WAVE);
    ch30w3_set_spawn(CH30W3_WAVE3_RECORD, CH30W3_WAVE3_CHAR_ID,
                     CH30W4_WAVE_BELOW);

    ch30w4_run(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W4_MAP00_RECORD0_X);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W4_MAP00_RECORD0_Y);

    ch30w4_stage(0);
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH30W3_WAVE2_CHAR_ID, CH30W4_WAVE);
    ch30w3_set_spawn(CH30W3_WAVE3_RECORD, CH30W3_WAVE3_CHAR_ID,
                     CH30W4_WAVE_BELOW);
    data_fdps_chapter_current_chapter_id = 1;

    ch30w4_run(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W4_MAP01_RECORD0_X);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W4_MAP01_RECORD0_Y);
    ch30w4_unstage();
}

/* Nothing in the body tests whether the deployment found anything: with all
   three records tagged well away from wave 4 the unit count does not move, and
   the cursor is still blanked, both pans still run -- the west walk shows in the
   view window ending at (0xc0, 0xc0) rather than the staged (0, 0), the holds in
   the tick bound -- the line is still spoken and the latch still goes up. */
static void ch30w4_pans_even_when_no_record_matches(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w4_stage(0);
    ch30w4_tag_decoys(CH30W4_UNSELECTED_WAVE);

    ch30w4_run(0);

    CHECK_EQ(data_fdps_map_unit_count, 1);
    CHECK_EQ(data_fdps_map_cursor_world_x, CH30W4_PAN_RIGHT_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH30W4_PAN_RIGHT_Y);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH30W4_CURSOR_MODE_NORMAL);
    CHECK_EQ(data_fdps_battle_view_window_origin_x, CH30W4_END_VIEW_ORIGIN_X);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, CH30W4_END_VIEW_ORIGIN_Y);
    CHECK_EQ(ch30w4_ticks_after - ch30w4_ticks_before
                 >= (unsigned int) CH30W4_LEAST_PAN_TICKS,
             1);
    CHECK_EQ(ch30w4_screen_pixel, CH30W4_TEXT_FG_COLOR);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT], 1);
    ch30w4_unstage();
}

/* The latch the firing call raises is what stops the second one: the handler is
   called again on the same fixture with the cursor parked somewhere neither pan
   target and the draw mode put back to the staged value, and nothing moves.  A
   handler that latched at the top of its body instead of the bottom would pass
   this too -- what this pins is that a second crossing of the trigger tile
   brings no second wave. */
static void ch30w4_fires_only_once(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w4_stage(0);

    ch30w4_run(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT], 1);

    data_fdps_map_cursor_world_x = CH30W4_PARKED_CURSOR_X;
    data_fdps_map_cursor_world_y = CH30W4_PARKED_CURSOR_Y;
    data_fdps_map_cursor_draw_mode = CH30W4_STAGED_CURSOR_MODE;

    fdps_chapter_30_event_deploy_wave_4(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cursor_world_x, CH30W4_PARKED_CURSOR_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH30W4_PARKED_CURSOR_Y);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH30W4_STAGED_CURSOR_MODE);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT], 1);
    ch30w4_unstage();
}

/* The incoming argument slot is overwritten with 0 at the head of each hold loop
   and is never read as an argument, so the index the dispatcher passes cannot
   reach the wave asked for, the map asked for, the placement flag or either pan.
   In the shipped data it is the index of the unit that stepped onto the trigger
   tile; the values put through here are 0, an index that names the unit already
   on the map, one past the array, and -1 and 30000, which are the ones an
   argument-driven handler would betray itself on.  There is no side gate in this
   handler either, so a record's contents cannot matter -- which is what
   separates it from the chapter 19 and 29 tile triggers. */
static void ch30w4_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 2, -1, 30000};
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch30w4_stage(0);

        ch30w4_run(arguments[i]);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W4_MAP00_RECORD1_X);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W4_MAP00_RECORD1_Y);
        CHECK_EQ(data_fdps_map_cursor_world_x, CH30W4_PAN_RIGHT_X);
        CHECK_EQ(data_fdps_map_cursor_draw_mode, CH30W4_CURSOR_MODE_NORMAL);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT],
                 1);
    }
    ch30w4_unstage();
}

/* ---- fdps_chapter_30_event_deploy_wave_2, 00039840 -----------------------
 *
 * Every expected value below is read off the assembly at 00039840: the two
 * seven-push draws at 00039853..0003986e and 00039898..000398b3 that name text
 * entries 0x0f and 0x10, the aperture origin, the row stride and the three
 * message colours; the MOV byte ptr [0x000640da],0x1 at 00039876 that marks map
 * cell event code 2 in data_fdps_map_cell_event_triggered_flags and the CALL to
 * fdps_map_apply_triggered_cell_changes at 0003987d that makes the mark visible;
 * and the MOV EAX,0x1 / PUSH EAX / PUSH 0x2 / PUSH dword ptr [0x00069cf4] at
 * 00039882..0003988a that name the placement, the wave and the map.  There is no
 * compare anywhere in the body, so nothing here is conditional.
 *
 * The deployment cases reuse the ch30w3_ fixture -- the same blank walkable map,
 * the same three deployment records at table indices 0, 1 and 2, the same
 * MAP00.COD and MAP01.COD placement records -- because the wave the handler
 * asked for is only readable off the record that arrived.  ch30w4_tag_decoys
 * lays a record at the wave under test with one wave either side of it, so a key
 * that drifted in either direction deploys a different character id onto a
 * different tile.  They run the real deployment and so need ICON.CEL and
 * FIELD.VFS next to the executable (tests/gamefile.lst) and skip themselves
 * without them.
 *
 * THE TERRAIN CASES ARE WHAT SEPARATE THE TRIGGER TABLE FROM A FLAG.  The store
 * at 00039876 is into element 2 of a shared table and not into a variable of its
 * own, and the difference is only visible through the call on the next line:
 * fdps_map_apply_triggered_cell_changes reads the table with each cell's event
 * code and bumps the tile id of every searchable cell whose code is marked.  So
 * the fixture puts a searchable cell carrying event code 2 on the map and reads
 * the tile id and the event-code byte back afterwards.  A rebuild that wrote a
 * private flag leaves both untouched, which is chapter 30's map keeping its old
 * tiles when the boss's first form dies.  The tile map's height word has to be
 * staged for these -- ch30w3_stage leaves it 0 and the sweep is a loop over it.
 *
 * The control cells are the other half of the same reading: one searchable cell
 * carrying event code 3 and one non-searchable cell carrying event code 2, both
 * of which must come out unchanged.  Between them they pin that the mark went
 * into element 2 and nowhere else and that the sweep's kind test is still there.
 *
 * THE TEXT BLOCK IS SILENT BY DEFAULT, the way tests/chevt1.c stages the same
 * family's: every entry points at a lone terminator, so fdps_draw_text walks it,
 * draws nothing and returns.  That is what lets every case but one run with the
 * adapter in text mode, where the aperture at 0xa0000 answers nothing and a draw
 * would be measuring memory the console does not show.
 *
 * WHY THE LINES ARE READ BACK OFF THE SCREEN.  fdps_draw_text writes to the
 * address the handler hands it, keeps no state and returns a cursor this handler
 * discards, so the only place a draw is visible is video memory.  The one case
 * that asserts the draws points entry 0x0f at a glyph whose only set row is row 0
 * and entry 0x10 at a glyph whose only set row is row 4, and puts the adapter in
 * mode 13h: the two draws land at the same origin without overwriting each other
 * -- the cell background fill is off at background colour 0 -- so each is
 * readable on its own row.  Row 2 is sampled as well and must still hold the
 * sentinel, which is what says nothing painted a solid cell.  Every other entry
 * stays a lone terminator, so a draw that asked for a neighbouring entry paints
 * nothing and its row comes back as the sentinel.  The pixels are read before the
 * adapter goes back to text mode, because that mode set clears them.
 */

/* The wave asked for, PUSH 0x2 at 00039888. */
#define CH30W2_WAVE 2

/* The cell event code marked at 00039876: 0x000640da is element 2 of the table
   based at 0x000640d8.  The elements around it are read back to say the write
   did not spill, and the table's declared length (gamedata.h) is what the
   fixture puts down and back up. */
#define CH30W2_TERRAIN_CELL_CODE 2
#define CH30W2_UNMARKED_CELL_CODE 3
#define CH30W2_TRIGGER_TABLE_SLOTS 32

/* The two text entries spoken, PUSH 0xf at 00039866 and PUSH 0x10 at
   000398ab. */
#define CH30W2_FALL_TEXT_ID 0x0f
#define CH30W2_ARRIVAL_TEXT_ID 0x10

/* The staged text block: 0x11 offset entries, then the lone terminator every
   entry points at by default, then the two one-glyph streams the painting case
   moves entries 0x0f and 0x10 onto. */
#define CH30W2_TEXT_ENTRIES 0x11
#define CH30W2_TEXT_TERMINATOR (-1)
#define CH30W2_TEXT_EMPTY_AT (CH30W2_TEXT_ENTRIES * 2)
#define CH30W2_TEXT_FALL_AT ((CH30W2_TEXT_ENTRIES + 1) * 2)
#define CH30W2_TEXT_ARRIVAL_AT ((CH30W2_TEXT_ENTRIES + 3) * 2)
#define CH30W2_TEXT_SLOTS (CH30W2_TEXT_ENTRIES + 5)

/* The synthetic font: one 8 by 8 cell per glyph, one byte to the row and the
   most significant bit leftmost.  Glyph 0 is blank, glyph 1 has row 0 solid and
   glyph 2 has row 4 solid, so which of the two entries was drawn is readable off
   which screen row came out at the foreground colour.  With the outline flag
   clear and both shadow offsets zero the shadow lands on the cell itself and the
   body is drawn over it. */
#define CH30W2_FONT_GLYPHS 3
#define CH30W2_FONT_W 8
#define CH30W2_FONT_H 8
#define CH30W2_FONT_STRIDE 8
#define CH30W2_FALL_GLYPH 1
#define CH30W2_ARRIVAL_GLYPH 2
#define CH30W2_FALL_GLYPH_ROW 0
#define CH30W2_ARRIVAL_GLYPH_ROW 4
#define CH30W2_BLANK_ROW 2

/* The aperture the lines are drawn at and its row stride, PUSH 0xa0000 and PUSH
   0x140 at 00039861 and 0003985c, and the foreground colour, PUSH 0xd0 at
   00039857. */
#define CH30W2_VGA_ORIGIN 0x000a0000
#define CH30W2_VGA_PITCH 0x140
#define CH30W2_TEXT_FG_COLOR 0xd0

/* A pixel value no glyph of this font can leave behind, stamped on each sampled
   row before the painting run so a row nothing painted is visible as itself. */
#define CH30W2_SCREEN_SENTINEL 0x11

/* Where the tile map's height word sits, which
   fdps_map_apply_triggered_cell_changes reads to bound its sweep
   (src/maptile.c).  ch30w3_stage stages the width and leaves this 0, which makes
   the sweep a loop over nothing. */
#define CH30W2_TILE_MAP_HEIGHT_OFFSET 9

/* The attribute-row flag value the sweep accepts as a searchable cell of kind
   0x20, and the tile ids the terrain cases hang off: one row given those flags
   and one left without them. */
#define CH30W2_TILE_KIND_SEARCHABLE 0x20
#define CH30W2_SEARCHABLE_TILE_ID 3
#define CH30W2_PLAIN_TILE_ID 5

/* The three cells the terrain cases stage, all clear of the placement records
   these cases read back: the one that must change and the two that must not. */
#define CH30W2_MARKED_CELL_X 4
#define CH30W2_MARKED_CELL_Y 5
#define CH30W2_OTHER_CODE_CELL_X 6
#define CH30W2_OTHER_CODE_CELL_Y 5
#define CH30W2_PLAIN_CELL_X 8
#define CH30W2_PLAIN_CELL_Y 5

/* Where MAP00.COD's placement records 0 and 1 and MAP01.COD's record 0 put a
   unit, the same coordinates the ch30w3_ and ch30w4_ cases read back. */
#define CH30W2_MAP00_RECORD0_X 18
#define CH30W2_MAP00_RECORD0_Y 0
#define CH30W2_MAP00_RECORD1_X 22
#define CH30W2_MAP00_RECORD1_Y 12
#define CH30W2_MAP01_RECORD0_X 9
#define CH30W2_MAP01_RECORD0_Y 4

static short ch30w2_text[CH30W2_TEXT_SLOTS];
static unsigned char ch30w2_font[CH30W2_FONT_GLYPHS * CH30W2_FONT_STRIDE];
static unsigned char ch30w2_fall_pixel;
static unsigned char ch30w2_blank_pixel;
static unsigned char ch30w2_arrival_pixel;

/* The chapter text block with every entry silent, and the font under it.  The
   two one-glyph streams are laid down in the tail here as well; nothing points
   at them until a case asks for it. */
static void ch30w2_stage_text(void)
{
    int entry;

    for (entry = 0; entry < CH30W2_TEXT_ENTRIES; entry++) {
        ch30w2_text[entry] = (short) CH30W2_TEXT_EMPTY_AT;
    }
    ch30w2_text[CH30W2_TEXT_ENTRIES] = (short) CH30W2_TEXT_TERMINATOR;
    ch30w2_text[CH30W2_TEXT_ENTRIES + 1] = (short) CH30W2_FALL_GLYPH;
    ch30w2_text[CH30W2_TEXT_ENTRIES + 2] = (short) CH30W2_TEXT_TERMINATOR;
    ch30w2_text[CH30W2_TEXT_ENTRIES + 3] = (short) CH30W2_ARRIVAL_GLYPH;
    ch30w2_text[CH30W2_TEXT_ENTRIES + 4] = (short) CH30W2_TEXT_TERMINATOR;

    memset(ch30w2_font, 0, sizeof(ch30w2_font));
    ch30w2_font[CH30W2_FALL_GLYPH * CH30W2_FONT_STRIDE +
                CH30W2_FALL_GLYPH_ROW] = 0xff;
    ch30w2_font[CH30W2_ARRIVAL_GLYPH * CH30W2_FONT_STRIDE +
                CH30W2_ARRIVAL_GLYPH_ROW] = 0xff;

    data_fdps_current_chapter_text_ptr = (unsigned char *) ch30w2_text;
    data_fdps_font_sheet_ptr = ch30w2_font;
    data_fdps_font_glyph_width = CH30W2_FONT_W;
    data_fdps_glyph_cell_height = CH30W2_FONT_H;
    data_fdps_font_glyph_stride_bytes = CH30W2_FONT_STRIDE;
    data_fdps_font_outline_enabled_flag = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_glyph_advance_x = CH30W2_FONT_W;
    data_fdps_font_line_height = CH30W2_FONT_H;
}

/* Point the two entries the handler speaks at their glyph streams, so a draw of
   either becomes a pixel.  Only the case that puts the adapter in a graphics
   mode calls this. */
static void ch30w2_make_the_lines_paint(void)
{
    ch30w2_text[CH30W2_FALL_TEXT_ID] = (short) CH30W2_TEXT_FALL_AT;
    ch30w2_text[CH30W2_ARRIVAL_TEXT_ID] = (short) CH30W2_TEXT_ARRIVAL_AT;
}

/* One attribute row's flag byte, which is what the sweep's kind test reads. */
static void ch30w2_set_tile_flags(int tile_id, int flags)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch30w3_tile_attr + CH30W3_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].flags = (unsigned char) flags;
}

static int ch30w2_tile_id_at(int tile_x, int tile_y)
{
    short *tile_ids;

    tile_ids = (short *) (ch30w3_tile_map + CH30W3_TILE_MAP_IDS_OFFSET);
    return (int) tile_ids[tile_y * CH30W3_GRID_W + tile_x];
}

static void ch30w2_set_cell_event_code(int tile_x, int tile_y, int code)
{
    ch30w3_event_layer[CH30W3_EVENT_LAYER_CELLS_OFFSET +
                       tile_y * CH30W3_GRID_W + tile_x] =
        (unsigned char) code;
}

static int ch30w2_cell_event_code_at(int tile_x, int tile_y)
{
    return (int) ch30w3_event_layer[CH30W3_EVENT_LAYER_CELLS_OFFSET +
                                    tile_y * CH30W3_GRID_W + tile_x];
}

/* The ch30w3_ fixture with the decoys laid down around wave 2, a silent text
   block and a font under it so both draws have something safe to walk, the tile
   map's height word filled in so the terrain sweep has rows to walk, and the
   whole trigger table down. */
static void ch30w2_stage(int battle_turn)
{
    int slot;

    ch30w3_stage(battle_turn);
    ch30w4_tag_decoys(CH30W2_WAVE);
    ch30w2_stage_text();

    *(short *) (ch30w3_tile_map + CH30W2_TILE_MAP_HEIGHT_OFFSET) =
        (short) CH30W3_GRID_H;

    for (slot = 0; slot < CH30W2_TRIGGER_TABLE_SLOTS; slot++) {
        data_fdps_map_cell_event_triggered_flags[slot] = 0;
    }
}

/* Move the wave-2 tag onto table index 0, so the record the handler deploys is
   placement record 0 -- the one record of MAP01.COD these cases know the
   coordinates of. */
static void ch30w2_tag_first_record_as_the_wave(void)
{
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH30W3_WAVE2_CHAR_ID, CH30W2_WAVE);
    ch30w3_set_spawn(CH30W3_WAVE3_RECORD, CH30W3_WAVE3_CHAR_ID,
                     CH30W2_WAVE + 1);
    ch30w3_set_spawn(CH30W3_WAVE4_RECORD, CH30W3_WAVE4_CHAR_ID,
                     CH30W2_WAVE + 2);
}

/* The three cells the terrain cases read back: a searchable cell carrying the
   marked code, a searchable cell carrying another code, and a cell of a kind the
   sweep rejects carrying the marked code. */
static void ch30w2_stage_terrain_cells(void)
{
    ch30w2_set_tile_flags(CH30W2_SEARCHABLE_TILE_ID,
                          CH30W2_TILE_KIND_SEARCHABLE);
    ch30w2_set_tile_flags(CH30W2_PLAIN_TILE_ID, 0);

    ch30w3_set_tile_id(CH30W2_MARKED_CELL_X, CH30W2_MARKED_CELL_Y,
                       CH30W2_SEARCHABLE_TILE_ID);
    ch30w2_set_cell_event_code(CH30W2_MARKED_CELL_X, CH30W2_MARKED_CELL_Y,
                               CH30W2_TERRAIN_CELL_CODE);

    ch30w3_set_tile_id(CH30W2_OTHER_CODE_CELL_X, CH30W2_OTHER_CODE_CELL_Y,
                       CH30W2_SEARCHABLE_TILE_ID);
    ch30w2_set_cell_event_code(CH30W2_OTHER_CODE_CELL_X,
                               CH30W2_OTHER_CODE_CELL_Y,
                               CH30W2_UNMARKED_CELL_CODE);

    ch30w3_set_tile_id(CH30W2_PLAIN_CELL_X, CH30W2_PLAIN_CELL_Y,
                       CH30W2_PLAIN_TILE_ID);
    ch30w2_set_cell_event_code(CH30W2_PLAIN_CELL_X, CH30W2_PLAIN_CELL_Y,
                               CH30W2_TERRAIN_CELL_CODE);
}

/* One whole call with the adapter in the mode the game plays it in, the three
   sampled rows stamped with the sentinel beforehand and read back before the
   adapter leaves graphics mode. */
static void ch30w2_run(int unit_index)
{
    ch28_set_mode(CH28_MODE_320X200X256);
    *(unsigned char *) (CH30W2_VGA_ORIGIN +
                        CH30W2_FALL_GLYPH_ROW * CH30W2_VGA_PITCH) =
        CH30W2_SCREEN_SENTINEL;
    *(unsigned char *) (CH30W2_VGA_ORIGIN +
                        CH30W2_BLANK_ROW * CH30W2_VGA_PITCH) =
        CH30W2_SCREEN_SENTINEL;
    *(unsigned char *) (CH30W2_VGA_ORIGIN +
                        CH30W2_ARRIVAL_GLYPH_ROW * CH30W2_VGA_PITCH) =
        CH30W2_SCREEN_SENTINEL;

    fdps_chapter_30_event_deploy_wave_2(unit_index);

    ch30w2_fall_pixel = *(unsigned char *)
        (CH30W2_VGA_ORIGIN + CH30W2_FALL_GLYPH_ROW * CH30W2_VGA_PITCH);
    ch30w2_blank_pixel = *(unsigned char *)
        (CH30W2_VGA_ORIGIN + CH30W2_BLANK_ROW * CH30W2_VGA_PITCH);
    ch30w2_arrival_pixel = *(unsigned char *)
        (CH30W2_VGA_ORIGIN + CH30W2_ARRIVAL_GLYPH_ROW * CH30W2_VGA_PITCH);
    ch28_set_mode(CH28_MODE_TEXT);
}

/* Back to the state a freshly started program has these in, for the reason
   ch28_unstage gives, plus the trigger table -- which this handler raises and
   never lowers, so a later unit would inherit a map cell code already marked. */
static void ch30w2_unstage(void)
{
    int slot;

    ch28_unstage();
    data_fdps_current_chapter_text_ptr = NULL;
    data_fdps_font_sheet_ptr = NULL;
    for (slot = 0; slot < CH30W2_TRIGGER_TABLE_SLOTS; slot++) {
        data_fdps_map_cell_event_triggered_flags[slot] = 0;
    }
}

/* The wave asked for is 2 and only the record tagged with it arrives: of the
   three staged records exactly one deploys, it is table index 1, and it lands on
   MAP00.COD record 1 at (22, 12) with the character id that record carries.  The
   records at waves 1 and 3 sit either side of it, so an off-by-one in either
   direction would put a different character on a different tile. */
static void ch30w2_deploys_the_wave_two_record(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w2_stage(0);

    fdps_chapter_30_event_deploy_wave_2(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W2_MAP00_RECORD1_X);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W2_MAP00_RECORD1_Y);
    ch30w2_unstage();
}

/* The wave is the literal 2 and not a number derived from the battle turn
   counter: the same record arrives on turn 0, turn 1, turn 4 -- which a handler
   halving the counter would turn into wave 2 by accident -- turn 7 and turn 18.
   This is the assertion that separates this handler from the turn-scheduled
   reinforcement handlers of chapters 17, 18, 23, 24 and 28. */
static void ch30w2_wave_number_is_a_literal(void)
{
    static int turns[5] = {0, 1, 4, 7, 18};
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch30w2_stage(turns[i]);

        fdps_chapter_30_event_deploy_wave_2(0);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W2_MAP00_RECORD1_X);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W2_MAP00_RECORD1_Y);
    }
    ch30w2_unstage();
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the wave-2 record is moved onto table index 0 and deployed twice, once with
   that global on 0 and once on 1, and it lands on MAP00.COD's record 0 at
   (18, 0) and then on MAP01.COD's record 0 at (9, 4). */
static void ch30w2_map_number_comes_from_the_chapter_global(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w2_stage(0);
    ch30w2_tag_first_record_as_the_wave();

    fdps_chapter_30_event_deploy_wave_2(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W2_MAP00_RECORD0_X);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W2_MAP00_RECORD0_Y);

    ch30w2_stage(0);
    ch30w2_tag_first_record_as_the_wave();
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_30_event_deploy_wave_2(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W2_MAP01_RECORD0_X);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W2_MAP01_RECORD0_Y);
    ch30w2_unstage();
}

/* The placement flag is 1, so the record's own tile is used verbatim: the tile
   MAP00.COD record 1 names is given a terrain the free-tile search rejects and
   the unit still lands on it.  A flag of 0 would send the search out to the
   nearest walkable tile instead, which the chapter 28 handler's cases in this
   file show landing on (22, 13) off the same fixture. */
static void ch30w2_places_on_the_scripted_tile(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w2_stage(0);
    ch30w3_set_tile_id(CH30W2_MAP00_RECORD1_X, CH30W2_MAP00_RECORD1_Y, 1);
    ch30w3_set_terrain(1, CH30W3_TERRAIN_BLOCKED);

    fdps_chapter_30_event_deploy_wave_2(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W2_MAP00_RECORD1_X);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W2_MAP00_RECORD1_Y);
    ch30w2_unstage();
}

/* The store at 00039876 is element 2 of the shared trigger table and the call
   after it turns that mark into the map's terrain change: the searchable cell
   carrying event code 2 comes back with its tile id bumped by one and its
   event-code byte cleared, and element 2 of the table is left raised.  A rebuild
   that wrote a flag of its own leaves the tile id and the event byte exactly as
   staged, which is the map keeping its old tiles. */
static void ch30w2_marks_cell_code_two_and_applies_the_terrain_change(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w2_stage(0);
    ch30w2_stage_terrain_cells();

    fdps_chapter_30_event_deploy_wave_2(0);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH30W2_TERRAIN_CELL_CODE], 1);
    CHECK_EQ(ch30w2_tile_id_at(CH30W2_MARKED_CELL_X, CH30W2_MARKED_CELL_Y),
             CH30W2_SEARCHABLE_TILE_ID + 1);
    CHECK_EQ(ch30w2_cell_event_code_at(CH30W2_MARKED_CELL_X,
                                       CH30W2_MARKED_CELL_Y), 0);
    ch30w2_unstage();
}

/* Only element 2 is marked and only cells matching it change.  The elements
   around it stay down, a searchable cell carrying event code 3 keeps both its
   tile id and its event byte, and a cell of a kind the sweep rejects keeps them
   too even though it carries the marked code.  Between them these say the write
   landed on element 2 rather than spilling into a neighbour, and that the sweep's
   kind test is still in front of the change. */
static void ch30w2_leaves_other_codes_and_other_kinds_alone(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w2_stage(0);
    ch30w2_stage_terrain_cells();

    fdps_chapter_30_event_deploy_wave_2(0);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[0], 0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[1], 0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[3], 0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[4], 0);

    CHECK_EQ(ch30w2_tile_id_at(CH30W2_OTHER_CODE_CELL_X,
                               CH30W2_OTHER_CODE_CELL_Y),
             CH30W2_SEARCHABLE_TILE_ID);
    CHECK_EQ(ch30w2_cell_event_code_at(CH30W2_OTHER_CODE_CELL_X,
                                       CH30W2_OTHER_CODE_CELL_Y),
             CH30W2_UNMARKED_CELL_CODE);

    CHECK_EQ(ch30w2_tile_id_at(CH30W2_PLAIN_CELL_X, CH30W2_PLAIN_CELL_Y),
             CH30W2_PLAIN_TILE_ID);
    CHECK_EQ(ch30w2_cell_event_code_at(CH30W2_PLAIN_CELL_X,
                                       CH30W2_PLAIN_CELL_Y),
             CH30W2_TERRAIN_CELL_CODE);
    ch30w2_unstage();
}

/* Both lines are spoken and they are entries 0x0f and 0x10.  Entry 0x0f paints
   screen row 0 and entry 0x10 paints screen row 4, so each draw is readable on
   its own row; row 2 still holds the sentinel, which says nothing else painted a
   solid cell. */
static void ch30w2_speaks_both_lines(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w2_stage(0);
    ch30w2_make_the_lines_paint();

    ch30w2_run(0);

    CHECK_EQ((int) ch30w2_fall_pixel, CH30W2_TEXT_FG_COLOR);
    CHECK_EQ((int) ch30w2_arrival_pixel, CH30W2_TEXT_FG_COLOR);
    CHECK_EQ((int) ch30w2_blank_pixel, CH30W2_SCREEN_SENTINEL);
    ch30w2_unstage();
}

/* The incoming argument slot is overwritten with 0 at 0003984c before anything
   else happens and never read back, so the index the dispatcher passes cannot
   reach the entries drawn, the cell code marked, the map asked for, the wave
   asked for or the placement flag.  In the shipped data this handler is reached
   from fdps_run_death_scripts, which passes the index of the unit that just
   died; the values put through here are the 0 the turn-event runner would pass,
   an index that names the unit already on the map, one past the array, and -1 and
   30000, which are the ones an argument-driven handler would betray itself on. */
static void ch30w2_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 2, -1, 30000};
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch30w2_stage(0);
        ch30w2_stage_terrain_cells();

        fdps_chapter_30_event_deploy_wave_2(arguments[i]);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, CH30W2_MAP00_RECORD1_X);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, CH30W2_MAP00_RECORD1_Y);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                     CH30W2_TERRAIN_CELL_CODE], 1);
        CHECK_EQ(ch30w2_tile_id_at(CH30W2_MARKED_CELL_X,
                                   CH30W2_MARKED_CELL_Y),
                 CH30W2_SEARCHABLE_TILE_ID + 1);
    }
    ch30w2_unstage();
}

/* Nothing guards the call: there is no compare anywhere in the body, and the
   byte this handler writes is element 2 of the trigger table rather than the
   one-shot slot its siblings gate on.  So the slot those siblings latch is put up
   beforehand and the wave still arrives, and a second call appends a second copy
   of it rather than being refused.  Element 2 being already raised does not
   refuse it either -- the second run marks it again and deploys again. */
static void ch30w2_has_no_one_shot_latch(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w2_stage(0);
    data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT] = 1;

    fdps_chapter_30_event_deploy_wave_2(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);

    fdps_chapter_30_event_deploy_wave_2(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch30w3_unit(2)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(2)->pos_x, CH30W2_MAP00_RECORD1_X);
    CHECK_EQ((int) ch30w3_unit(2)->pos_y, CH30W2_MAP00_RECORD1_Y);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH30W2_TERRAIN_CELL_CODE], 1);
    ch30w2_unstage();
}

void run_chevt6_tests(void)
{
    RUN_TEST(ch29_record_shape_matches_the_offsets);
    RUN_TEST(ch29_player_unit_clears_exactly_the_range);
    RUN_TEST(ch29_range_includes_the_last_index);
    RUN_TEST(ch29_leaves_the_guardian_dragons_holding);
    RUN_TEST(ch29_only_the_player_side_springs_the_ambush);
    RUN_TEST(ch29_gate_follows_the_argument_unit);
    RUN_TEST(ch29_keeps_the_high_nibble);
    RUN_TEST(ch29_touches_no_neighbouring_byte);
    RUN_TEST(ch29_has_no_one_shot_latch);
    RUN_TEST(ch29all_releases_the_whole_deployment);
    RUN_TEST(ch29all_both_ranges_are_inclusive);
    RUN_TEST(ch29all_dragons_get_the_item_then_attack_mode);
    RUN_TEST(ch29all_only_the_player_side_springs_the_ambush);
    RUN_TEST(ch29all_gate_follows_the_argument_unit);
    RUN_TEST(ch29all_keeps_the_high_nibble);
    RUN_TEST(ch29all_touches_no_neighbouring_byte);
    RUN_TEST(ch29all_has_no_one_shot_latch);
    RUN_TEST(ch30w3_record_shape_matches_the_offsets);
    RUN_TEST(ch30w3_deploys_the_wave_three_record);
    RUN_TEST(ch30w3_wave_number_is_a_literal);
    RUN_TEST(ch30w3_deploys_every_record_of_the_wave);
    RUN_TEST(ch30w3_places_on_the_scripted_tile);
    RUN_TEST(ch30w3_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch30w3_ignores_the_unit_index_argument);
    RUN_TEST(ch30w3_has_no_one_shot_latch);
    RUN_TEST(ch28_each_scheduled_turn_deploys_the_wave_it_halves_to);
    RUN_TEST(ch28_odd_turns_truncate_down);
    RUN_TEST(ch28_pans_to_the_spawn_tile_and_holds);
    RUN_TEST(ch28_pans_even_when_no_record_matches);
    RUN_TEST(ch28_ignores_the_unit_index_argument);
    RUN_TEST(ch28_has_no_one_shot_latch);
    RUN_TEST(ch30rev_record_shape_matches_the_offsets);
    RUN_TEST(ch30rev_only_the_two_undead_ids_are_revived);
    RUN_TEST(ch30rev_a_living_undead_is_left_alone);
    RUN_TEST(ch30rev_the_sweep_stops_at_the_unit_count);
    RUN_TEST(ch30rev_the_skeleton_lands_on_the_nearest_free_walkable_tile);
    RUN_TEST(ch30rev_a_tie_at_the_shortest_distance_keeps_the_last_cell);
    RUN_TEST(ch30rev_a_tile_over_the_terrain_limit_is_rejected);
    RUN_TEST(ch30rev_the_wraith_measures_from_its_own_anchor);
    RUN_TEST(ch30rev_a_revived_unit_is_not_revived_again);
    RUN_TEST(ch30w4_a_raised_latch_blocks_the_whole_body);
    RUN_TEST(ch30w4_deploys_wave_four_pans_and_latches);
    RUN_TEST(ch30w4_wave_number_is_a_literal);
    RUN_TEST(ch30w4_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch30w4_pans_even_when_no_record_matches);
    RUN_TEST(ch30w4_fires_only_once);
    RUN_TEST(ch30w4_ignores_the_unit_index_argument);
    RUN_TEST(ch30w2_deploys_the_wave_two_record);
    RUN_TEST(ch30w2_wave_number_is_a_literal);
    RUN_TEST(ch30w2_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch30w2_places_on_the_scripted_tile);
    RUN_TEST(ch30w2_marks_cell_code_two_and_applies_the_terrain_change);
    RUN_TEST(ch30w2_leaves_other_codes_and_other_kinds_alone);
    RUN_TEST(ch30w2_speaks_both_lines);
    RUN_TEST(ch30w2_ignores_the_unit_index_argument);
    RUN_TEST(ch30w2_has_no_one_shot_latch);
}
