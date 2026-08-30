/* tests/death.c -- cover for src/death.c.
 *
 * Expected values come from the assembly at 00026180: the CMP EAX,[0x00060150]
 * / JL at 0002619d that bounds the walk with a signed compare before the body
 * runs, the AND AL,0x1 / TEST EAX,EAX / JNZ at 000261c7 that rejects a unit on
 * bit 0 of its flags byte alone, the MOV AL,byte ptr [EAX+0x31] / AND EAX,0xff
 * / CMP EAX,0xff at 000261d5 that rejects the sentinel opcode after widening
 * the byte without sign, the CMP word ptr [EAX+0x40],0x0 / JLE at 000261e9
 * that accepts a hit-point word of zero or less as a signed 16-bit compare,
 * the PUSH 0x3 / LEA EAX,[EAX+EAX*0x2] / ADD EAX,[EBP+0x14] at 000261f2 that
 * copies three raw bytes to out_scripts + written * 3, and the INC dword ptr
 * [EBP-0xc] at 00026210 that is the returned count.  None of them is read off
 * the emitted C.
 *
 * The unit array is staged here rather than read from a game file: the
 * function takes its whole input from its argument, from the unit count global
 * and from the records the accessor resolves, so pointing the array global at
 * a local block is the only way to reach the walk.  Nothing below asserts what
 * either global holds on its own -- ticket 23 owns that.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "death.h"

/* Eight slots, so a unit can be parked one past the count the walk is given
   and a stray step past the bound would be visible. */
#define STAGE_UNITS 8

/* Room for one record per staged unit plus a tail of untouched sentinel, so an
   append that used the unit index instead of the output counter, or that wrote
   more than three bytes, lands somewhere the assertions look. */
#define OUT_BYTES 32

/* The byte the output buffer is filled with before each call.  Nothing the
   function writes can be this value in the cases below, so a byte that still
   reads as this was not written. */
#define OUT_UNWRITTEN 0xaa

/* Bit 0 of the flags byte is the retirement flag; bit 7 is the separate
   acted-this-turn flag the collector does not reject on. */
#define FLAG_RETIRED 0x01
#define FLAG_ACTED   0x80

/* The "no script" opcode fdps_build_map_unit_array stamps into every roster
   slot. */
#define SCRIPT_NONE 0xff

static struct fdps_unit_record stage_units[STAGE_UNITS];
static unsigned char out_scripts[OUT_BYTES];

/* Zero every slot, fill the output buffer with the sentinel and publish the
   block.  Every unit is then a live, unretired unit at zero HP carrying script
   opcode 0, which is the state each case edits only the fields it is about. */
static void stage(int live_unit_count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < OUT_BYTES; i++) {
        out_scripts[i] = OUT_UNWRITTEN;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
    data_fdps_map_unit_count = live_unit_count;
}

static void stage_unit(int unit_index, int flags, int opcode, int operand,
                       int hp)
{
    stage_units[unit_index].flags = (unsigned char) flags;
    stage_units[unit_index].death_script_opcode = (unsigned char) opcode;
    stage_units[unit_index].death_script_operand = (short) operand;
    stage_units[unit_index].hp_current = (short) hp;
}

/* The record offsets the walk addresses as literals: +0x5 for the flags byte,
   +0x31 for the opcode, +0x32 for the operand it copies along with it and
   +0x40 for the hit-point word, plus the 0x50 stride the accessor multiplies
   by.  If the record measured anything else every lookup past index 0 would
   read a different unit, and the three-byte copy would take the wrong pair. */
static void record_layout_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, death_script_opcode),
             0x31);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, death_script_operand),
             0x32);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
}

/* CMP EAX,[0x00060150] / JL guards the body, so the count is a bound tested
   first.  A count of 0 collects nothing and writes nothing, and because the
   compare is the signed JL a negative count does too rather than running away
   as an unsigned one would. */
static void an_empty_battle_collects_nothing(void)
{
    stage(0);
    stage_unit(0, 0, 1, 0x1234, 0);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 0);
    CHECK_EQ(out_scripts[0], OUT_UNWRITTEN);
    stage(-1);
    stage_unit(0, 0, 1, 0x1234, 0);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 0);
    CHECK_EQ(out_scripts[0], OUT_UNWRITTEN);
}

/* The bound is exclusive: the unit sitting at index == count is outside the
   walk even though the array holds it and it qualifies on all three tests. */
static void the_bound_is_exclusive(void)
{
    stage(2);
    stage_unit(0, 0, 1, 0x1111, 0);
    stage_unit(1, 0, 2, 0x2222, 0);
    stage_unit(2, 0, 3, 0x3333, 0);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 2);
    CHECK_EQ(out_scripts[0], 1);
    CHECK_EQ(out_scripts[3], 2);
    CHECK_EQ(out_scripts[6], OUT_UNWRITTEN);
}

/* One qualifying unit writes exactly three bytes: the opcode, then the operand
   word behind it, low byte first.  The fourth byte is untouched, so the copy
   is three bytes wide and not four. */
static void a_record_is_the_opcode_then_the_operand_word(void)
{
    stage(1);
    stage_unit(0, 0, 1, 0x1234, 0);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 1);
    CHECK_EQ(out_scripts[0], 1);
    CHECK_EQ(out_scripts[1], 0x34);
    CHECK_EQ(out_scripts[2], 0x12);
    CHECK_EQ(out_scripts[3], OUT_UNWRITTEN);
}

/* The operand is copied as raw bytes, so a negative one arrives whole rather
   than clamped or widened: -2 is 0xfffe and both its bytes land. */
static void a_negative_operand_is_copied_whole(void)
{
    stage(1);
    stage_unit(0, 0, 3, -2, 0);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 1);
    CHECK_EQ(out_scripts[0], 3);
    CHECK_EQ(out_scripts[1], 0xfe);
    CHECK_EQ(out_scripts[2], 0xff);
}

/* AND AL,0x1 / TEST EAX,EAX / JNZ: a unit already taken off the map is passed
   over even though it is at zero HP with a real script.  This is the rejection
   that makes the order against fdps_play_death_animation_and_mark_dead
   load-bearing -- that routine sets this bit for every unit at zero HP. */
static void a_retired_unit_is_passed_over(void)
{
    stage(3);
    stage_unit(0, FLAG_RETIRED, 1, 0x1111, 0);
    stage_unit(1, 0, 2, 0x2222, 0);
    stage_unit(2, FLAG_RETIRED, 3, 0x3333, 0);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 1);
    CHECK_EQ(out_scripts[0], 2);
    CHECK_EQ(out_scripts[3], OUT_UNWRITTEN);
}

/* Only bit 0 rejects.  Bit 7 is the acted-this-turn flag, and a unit that has
   already moved this turn still owes its script, so a test written against the
   whole flags byte rather than against bit 0 would lose it. */
static void the_acted_flag_does_not_reject_a_unit(void)
{
    stage(3);
    stage_unit(0, FLAG_ACTED, 1, 0x1111, 0);
    stage_unit(1, 0xfe, 2, 0x2222, 0);
    stage_unit(2, FLAG_ACTED | FLAG_RETIRED, 3, 0x3333, 0);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 2);
    CHECK_EQ(out_scripts[0], 1);
    CHECK_EQ(out_scripts[3], 2);
    CHECK_EQ(out_scripts[6], OUT_UNWRITTEN);
}

/* CMP EAX,0xff is an equality against the sentinel, not a range: 0xff is the
   only opcode rejected, and the neighbouring 0xfe is collected.  0x80 is
   collected too, which is what the AND EAX,0xff before the compare buys -- a
   signed read of the byte would make it negative and a "< 0 means no script"
   spelling would drop it. */
static void only_the_sentinel_opcode_is_rejected(void)
{
    stage(3);
    stage_unit(0, 0, SCRIPT_NONE, 0x1111, 0);
    stage_unit(1, 0, 0xfe, 0x2222, 0);
    stage_unit(2, 0, 0x80, 0x3333, 0);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 2);
    CHECK_EQ(out_scripts[0], 0xfe);
    CHECK_EQ(out_scripts[3], 0x80);
    CHECK_EQ(out_scripts[6], OUT_UNWRITTEN);
}

/* JLE, not JL: a unit resting at exactly zero HP is dead as far as this
   collector is concerned, and one still holding a single hit point is not. */
static void zero_hit_points_qualifies_and_one_does_not(void)
{
    stage(3);
    stage_unit(0, 0, 1, 0x1111, 0);
    stage_unit(1, 0, 2, 0x2222, 1);
    stage_unit(2, 0, 3, 0x3333, 0x7fff);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 1);
    CHECK_EQ(out_scripts[0], 1);
    CHECK_EQ(out_scripts[3], OUT_UNWRITTEN);
}

/* The hit-point compare is the signed one over a signed word.  An overkill
   leaves the count below zero, and reading hp_current unsigned turns -1 into
   65535 and drops every unit the fight took past zero. */
static void an_overkilled_unit_qualifies(void)
{
    stage(3);
    stage_unit(0, 0, 1, 0x1111, -1);
    stage_unit(1, 0, 2, 0x2222, -300);
    stage_unit(2, 0, 3, 0x3333, -32768);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 3);
    CHECK_EQ(out_scripts[0], 1);
    CHECK_EQ(out_scripts[3], 2);
    CHECK_EQ(out_scripts[6], 3);
}

/* The append scales the OUTPUT counter by three, not the unit index, so a
   scattered set of qualifying units still lands packed from the front in unit
   order with no gaps.  Indices 1 and 4 qualify out of six: their records are
   at 0 and 3, and byte 3 would still read as the sentinel if the index had
   been scaled instead. */
static void the_records_are_packed_from_the_front(void)
{
    stage(6);
    stage_unit(0, FLAG_RETIRED, 1, 0x1111, 0);
    stage_unit(1, 0, 2, 0x0102, 0);
    stage_unit(2, 0, SCRIPT_NONE, 0x2222, 0);
    stage_unit(3, 0, 4, 0x3333, 5);
    stage_unit(4, 0, 5, 0x0405, -7);
    stage_unit(5, 0, 6, 0x4444, 20);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 2);
    CHECK_EQ(out_scripts[0], 2);
    CHECK_EQ(out_scripts[1], 0x02);
    CHECK_EQ(out_scripts[2], 0x01);
    CHECK_EQ(out_scripts[3], 5);
    CHECK_EQ(out_scripts[4], 0x05);
    CHECK_EQ(out_scripts[5], 0x04);
    CHECK_EQ(out_scripts[6], OUT_UNWRITTEN);
}

/* The walk resolves each record through the accessor with the loop index, so
   the units it reads are the ones at 0..count-1 of the published block and the
   0x50 stride lands on each in turn.  Moving the block moves the answer, which
   is what re-resolving on every iteration buys. */
static void the_walk_follows_the_published_array(void)
{
    stage(4);
    stage_unit(0, 0, 1, 0x1111, 0);
    stage_unit(1, 0, 2, 0x2222, 0);
    stage_unit(2, 0, 3, 0x3333, 0);
    stage_unit(3, 0, 4, 0x4444, 0);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 4);
    CHECK_EQ(out_scripts[0], 1);
    data_fdps_map_unit_array_ptr = (unsigned char *) &stage_units[2];
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 2);
    CHECK_EQ(out_scripts[0], 3);
    CHECK_EQ(out_scripts[3], 4);
}

/* ---- fdps_collect_death_script_events, 000274e0 -------------------------
 *
 * Expected values come from the assembly at 000274e0: the CMP
 * EAX,[0x00060150] / JL at 000274fd that bounds the walk with a signed compare
 * before the body runs, the AND AL,0x1 / TEST EAX,EAX / JNZ at 00027527 that
 * rejects on bit 0 of the flags byte alone, the CMP word ptr [EAX+0x40],0x0 /
 * JLE at 00027535 that accepts a hit-point word of zero or less as a signed
 * 16-bit compare, the pair CMP EAX,0x2 / JGE at 00027549 and CMP EAX,0x5 / JLE
 * at 0002755b that admit only opcodes 2..5 after AND EAX,0xff has widened the
 * byte without sign, and the PUSH 0x3 / LEA EAX,[EAX+EAX*0x2] / ADD
 * EAX,[EBP+0x14] at 00027562 that copies three raw bytes to out_events +
 * written * 3.  The count returned is the INC dword ptr [EBP-0xc] at 00027580.
 *
 * The staging helpers above are shared: the same unit array and the same
 * output buffer serve both collectors, since both take their whole input from
 * the argument, the unit count global and the records the accessor resolves.
 */

/* The bound is tested before the body, so an empty battle collects nothing,
   and because the compare is the signed JL a negative count does too rather
   than running away as an unsigned one would. */
static void an_empty_battle_collects_no_events(void)
{
    stage(0);
    stage_unit(0, 0, 3, 0x1234, 0);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 0);
    CHECK_EQ(out_scripts[0], OUT_UNWRITTEN);
    stage(-1);
    stage_unit(0, 0, 3, 0x1234, 0);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 0);
    CHECK_EQ(out_scripts[0], OUT_UNWRITTEN);
}

/* The bound is exclusive: the unit at index == count is outside the walk even
   though the array holds it and it passes all three tests. */
static void the_event_bound_is_exclusive(void)
{
    stage(2);
    stage_unit(0, 0, 2, 0x1111, 0);
    stage_unit(1, 0, 3, 0x2222, 0);
    stage_unit(2, 0, 4, 0x3333, 0);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 2);
    CHECK_EQ(out_scripts[0], 2);
    CHECK_EQ(out_scripts[3], 3);
    CHECK_EQ(out_scripts[6], OUT_UNWRITTEN);
}

/* One qualifying unit writes exactly three bytes: the opcode, then the operand
   word behind it, low byte first.  The fourth byte is untouched, so the copy
   is three bytes wide and not four. */
static void an_event_record_is_the_opcode_then_the_operand_word(void)
{
    stage(1);
    stage_unit(0, 0, 3, 0x1234, 0);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 1);
    CHECK_EQ(out_scripts[0], 3);
    CHECK_EQ(out_scripts[1], 0x34);
    CHECK_EQ(out_scripts[2], 0x12);
    CHECK_EQ(out_scripts[3], OUT_UNWRITTEN);
}

/* The operand is copied as raw bytes, so a negative one arrives whole rather
   than clamped or widened: -2 is 0xfffe and both its bytes land. */
static void a_negative_event_operand_is_copied_whole(void)
{
    stage(1);
    stage_unit(0, 0, 5, -2, 0);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 1);
    CHECK_EQ(out_scripts[0], 5);
    CHECK_EQ(out_scripts[1], 0xfe);
    CHECK_EQ(out_scripts[2], 0xff);
}

/* The window is 2..5 inclusive and closed at both ends.  The eight staged
   units carry opcodes 0, 1, 2, 3, 4, 5, 6 and the 0xff sentinel, so the item
   opcode 0 and the gold opcode 1 below the window, the unused 6 above it and
   the sentinel above that are all dropped, and exactly the four in between
   survive -- in unit order, each with its own operand.  This is the whole
   difference from fdps_collect_death_scripts: a test written as "not the
   sentinel" would collect seven of these eight. */
static void only_opcodes_two_through_five_are_events(void)
{
    stage(8);
    stage_unit(0, 0, 0, 0, 0);
    stage_unit(1, 0, 1, 1, 0);
    stage_unit(2, 0, 2, 2, 0);
    stage_unit(3, 0, 3, 3, 0);
    stage_unit(4, 0, 4, 4, 0);
    stage_unit(5, 0, 5, 5, 0);
    stage_unit(6, 0, 6, 6, 0);
    stage_unit(7, 0, SCRIPT_NONE, 7, 0);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 4);
    CHECK_EQ(out_scripts[0], 2);
    CHECK_EQ(out_scripts[1], 2);
    CHECK_EQ(out_scripts[3], 3);
    CHECK_EQ(out_scripts[4], 3);
    CHECK_EQ(out_scripts[6], 4);
    CHECK_EQ(out_scripts[7], 4);
    CHECK_EQ(out_scripts[9], 5);
    CHECK_EQ(out_scripts[10], 5);
    CHECK_EQ(out_scripts[12], OUT_UNWRITTEN);
}

/* AND EAX,0xff before both compares: the opcode is widened without sign, so
   the high half of the byte range sits above the window rather than below it.
   0x80 and 0xfe are dropped exactly as 6 and 0xff are. */
static void a_high_opcode_byte_is_above_the_window(void)
{
    stage(3);
    stage_unit(0, 0, 0x80, 0x1111, 0);
    stage_unit(1, 0, 0xfe, 0x2222, 0);
    stage_unit(2, 0, 4, 0x3333, 0);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 1);
    CHECK_EQ(out_scripts[0], 4);
    CHECK_EQ(out_scripts[3], OUT_UNWRITTEN);
}

/* A unit already taken off the map is passed over even at zero HP with an
   in-window opcode.  This is the rejection that makes the order against
   fdps_play_death_animation_and_mark_dead load-bearing. */
static void a_retired_unit_owes_no_event(void)
{
    stage(3);
    stage_unit(0, FLAG_RETIRED, 2, 0x1111, 0);
    stage_unit(1, 0, 3, 0x2222, 0);
    stage_unit(2, FLAG_RETIRED, 4, 0x3333, 0);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 1);
    CHECK_EQ(out_scripts[0], 3);
    CHECK_EQ(out_scripts[3], OUT_UNWRITTEN);
}

/* Only bit 0 rejects.  Bit 7 is the acted-this-turn flag, and a unit that has
   already moved this turn still owes its event, so a test against the whole
   flags byte would lose it. */
static void the_acted_flag_does_not_reject_an_event(void)
{
    stage(3);
    stage_unit(0, FLAG_ACTED, 2, 0x1111, 0);
    stage_unit(1, 0xfe, 3, 0x2222, 0);
    stage_unit(2, FLAG_ACTED | FLAG_RETIRED, 4, 0x3333, 0);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 2);
    CHECK_EQ(out_scripts[0], 2);
    CHECK_EQ(out_scripts[3], 3);
    CHECK_EQ(out_scripts[6], OUT_UNWRITTEN);
}

/* JLE, not JL: a unit resting at exactly zero HP owes its event, and one still
   holding a single hit point does not. */
static void zero_hit_points_owes_an_event_and_one_does_not(void)
{
    stage(3);
    stage_unit(0, 0, 2, 0x1111, 0);
    stage_unit(1, 0, 3, 0x2222, 1);
    stage_unit(2, 0, 4, 0x3333, 0x7fff);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 1);
    CHECK_EQ(out_scripts[0], 2);
    CHECK_EQ(out_scripts[3], OUT_UNWRITTEN);
}

/* The hit-point compare is the signed one over a signed word, so an overkill
   still owes its event; reading hp_current unsigned turns -1 into 65535 and
   drops every unit the spell took past zero. */
static void an_overkilled_unit_owes_its_event(void)
{
    stage(3);
    stage_unit(0, 0, 2, 0x1111, -1);
    stage_unit(1, 0, 3, 0x2222, -300);
    stage_unit(2, 0, 4, 0x3333, -32768);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 3);
    CHECK_EQ(out_scripts[0], 2);
    CHECK_EQ(out_scripts[3], 3);
    CHECK_EQ(out_scripts[6], 4);
}

/* The append scales the OUTPUT counter by three, not the unit index, so a
   scattered set of qualifying units lands packed from the front in unit order
   with no gaps.  Indices 1 and 4 qualify out of six -- index 0 is retired,
   index 2 carries the item opcode, index 3 is alive and index 5 carries the
   gold opcode -- and their records sit at 0 and 3; byte 3 would still read as
   the sentinel if the unit index had been scaled instead. */
static void the_event_records_are_packed_from_the_front(void)
{
    stage(6);
    stage_unit(0, FLAG_RETIRED, 2, 0x1111, 0);
    stage_unit(1, 0, 3, 0x0102, 0);
    stage_unit(2, 0, 0, 0x2222, 0);
    stage_unit(3, 0, 4, 0x3333, 5);
    stage_unit(4, 0, 5, 0x0405, -7);
    stage_unit(5, 0, 1, 0x4444, -20);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 2);
    CHECK_EQ(out_scripts[0], 3);
    CHECK_EQ(out_scripts[1], 0x02);
    CHECK_EQ(out_scripts[2], 0x01);
    CHECK_EQ(out_scripts[3], 5);
    CHECK_EQ(out_scripts[4], 0x05);
    CHECK_EQ(out_scripts[5], 0x04);
    CHECK_EQ(out_scripts[6], OUT_UNWRITTEN);
}

/* The two collectors are not the same function.  Handed one battle in which
   every opcode appears once, the sibling keeps everything but the sentinel and
   this one keeps only the middle four; if either were folded into the other
   these two counts would agree. */
static void the_two_collectors_disagree_on_the_reward_opcodes(void)
{
    stage(8);
    stage_unit(0, 0, 0, 0, 0);
    stage_unit(1, 0, 1, 1, 0);
    stage_unit(2, 0, 2, 2, 0);
    stage_unit(3, 0, 3, 3, 0);
    stage_unit(4, 0, 4, 4, 0);
    stage_unit(5, 0, 5, 5, 0);
    stage_unit(6, 0, 6, 6, 0);
    stage_unit(7, 0, SCRIPT_NONE, 7, 0);
    CHECK_EQ(fdps_collect_death_scripts(out_scripts), 7);
    CHECK_EQ(fdps_collect_death_script_events(out_scripts), 4);
}

void run_death_tests(void)
{
    RUN_TEST(record_layout_matches_the_offsets);
    RUN_TEST(an_empty_battle_collects_nothing);
    RUN_TEST(the_bound_is_exclusive);
    RUN_TEST(a_record_is_the_opcode_then_the_operand_word);
    RUN_TEST(a_negative_operand_is_copied_whole);
    RUN_TEST(a_retired_unit_is_passed_over);
    RUN_TEST(the_acted_flag_does_not_reject_a_unit);
    RUN_TEST(only_the_sentinel_opcode_is_rejected);
    RUN_TEST(zero_hit_points_qualifies_and_one_does_not);
    RUN_TEST(an_overkilled_unit_qualifies);
    RUN_TEST(the_records_are_packed_from_the_front);
    RUN_TEST(the_walk_follows_the_published_array);
    RUN_TEST(an_empty_battle_collects_no_events);
    RUN_TEST(the_event_bound_is_exclusive);
    RUN_TEST(an_event_record_is_the_opcode_then_the_operand_word);
    RUN_TEST(a_negative_event_operand_is_copied_whole);
    RUN_TEST(only_opcodes_two_through_five_are_events);
    RUN_TEST(a_high_opcode_byte_is_above_the_window);
    RUN_TEST(a_retired_unit_owes_no_event);
    RUN_TEST(the_acted_flag_does_not_reject_an_event);
    RUN_TEST(zero_hit_points_owes_an_event_and_one_does_not);
    RUN_TEST(an_overkilled_unit_owes_its_event);
    RUN_TEST(the_event_records_are_packed_from_the_front);
    RUN_TEST(the_two_collectors_disagree_on_the_reward_opcodes);
}
