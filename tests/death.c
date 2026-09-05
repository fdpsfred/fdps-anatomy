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
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
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

/* ---- fdps_play_death_animation_and_mark_dead, 0001d6c0 -------------------
 *
 * Expected values come from the assembly at 0001d6c0: the CMP
 * EAX,[0x00060150] / JL at 0001d6dd that bounds the sweep, AND AL,0x1 / TEST
 * EAX,EAX / JNZ at 0001d719 that rejects a unit on bit 0 of its flags byte
 * alone and CMP word ptr [EAX+0x40],0x0 / JZ at 0001d727 that takes only a
 * unit resting at EXACTLY zero; CMP dword ptr [EBP-0x24],0x0 / JZ at 0001d74c
 * that ends the call when nothing qualified; CMP dword ptr [EBP-0x1c],0xd / JL
 * at 0001d75d for thirteen spin frames with MOV EBX,0x4 / CDQ / IDIV EBX / MOV
 * byte ptr [EAX+0x3],DL at 0001d7a2 for the facing each one writes; MOV byte
 * ptr [EAX+0x5],0x1 at 0001d7f9 for the mark; PUSH 0x15180 / CALL malloc at
 * 0001d7ff with the pitch and rows at 0001d80f and 0001d816; MOV EAX,0x6176c /
 * PUSH [0x000643a8] / CALL at 0001d821 for the sheet and CALL 0x000144e0 at
 * 0001d84a for the frame count the loop runs to; MOV AL,byte ptr [EAX] / AND
 * EAX,0xff / IMUL EAX,EAX,0x18 / SUB EAX,[0x00069ce4] at 0001d8b5 and the same
 * with SUB EAX,[0x00069ce0] / SUB EAX,0x6 at 0001d8cb for the request origin;
 * CMP dword ptr [EBP-0x28],0x0 at 0001d8e5 for the sound flag; and PUSH 0xc0 /
 * PUSH 0x138 / PUSH 0x140 / PUSH 0xa0504 / PUSH 0x168 / page + 0x21d8 at
 * 0001d931 for the window.  None of them is read off the emitted C.
 *
 * HOW THE RUN IS WATCHED.  The routine composes on a page it allocates and
 * frees itself and blits that page's 312x192 window straight over the live
 * mode 13h screen, so the adapter is the only place its output can be read
 * back from, and the records it edits are the only other thing it leaves
 * behind.  Every case sets mode 13h, fills the frame with a border sentinel,
 * seeds the page through the heap, runs a real timer interrupt so the frame
 * waits end, calls, snapshots the 64,000 bytes and returns to text mode -- the
 * same way tests/anim.c watches fdps_play_attack_animation.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED.  Thirteen spin frames go through
 * fdps_render_view_frame, whose own wait ends only when
 * data_fdps_timer_tick_counter moves, and every explosion frame ends on the
 * same wait.  Nothing advances that counter in a test image, so the first spin
 * frame would never end.  Each run hooks IRQ0 for the duration of the call
 * with a handler that increments the counter and chains to the one that was
 * there.
 *
 * WHY THE PAGE IS SEEDED THROUGH THE HEAP AND WHY NOTHING ELSE PAINTS.  Every
 * staged unit carries portrait id 0x80, which fdps_draw_map_unit drops before
 * it reaches any sprite, and the scene layer count, the cursor mode and the
 * two HUD flags are all zero, so fdps_draw_scene_layers writes nothing into
 * the page and neither does fdps_render_view_frame's own compositor.  Whatever
 * malloc hands over is therefore what shows everywhere the explosion does not
 * reach, and each run frees a zeroed block of exactly the page's 0x15180 bytes
 * immediately before the call so every undrawn window pixel reads 0.  The
 * thirteen spin frames borrow and return that same block without writing it.
 * dth_the_page_comes_back_to_the_heap is that assumption stated as an
 * assertion.
 *
 * WHAT THE SNAPSHOT SHOWS IS EVERY EXPLOSION FRAME AT ONCE, not the last one.
 * Nothing clears the page between passes, so the sheet's three frames pile up
 * on it and the frame left on the adapter carries all three marks side by
 * side.  The fixture puts frame i's mark eight pixels right of frame i-1's, so
 * three marks in the right three places say the loop made one pass per frame
 * of the sheet AND that pass i drew frame i.
 *
 * THE SHEET IS SYNTHETIC AND SO IS THE CONTAINER IT IS LOOKED UP IN.  The
 * shipped Explo.Saf is 24-pixel artwork whose own shape would decide every
 * position below; the fixture's frames carry one 4x2 cell each so a mark can
 * be read as a position.  It is wrapped in a synthetic 26-byte-entry container
 * under the name EXPLO.SAF because the resident-image lookup is how the
 * routine reaches its sheet, and the entry's name is stored upper-case because
 * that lookup folds only the query.  The .SAF layout is resource_info/saf.md
 * and the container's is resource_info/vfs.md; neither was read off src/.
 *
 * WHAT IS NOT COVERED.  Which unit's pass asks for the frame's sound -- the
 * flag built at 0001d8e5 -- reaches fdps_sfx_play as the frame's own sound
 * number, and every frame of the fixture carries -1, which that function
 * rejects; what a non-zero play_sound does is fdps_draw_composite_sprite's own
 * behaviour, covered in tests/sprite.c.  The tick pacing and the retrace each
 * frame straddles are playtest contracts (rebuild_info/pitfalls.md).
 * ------------------------------------------------------------------ */

/* The adapter, the frame it presents and the two modes the cases switch
   between. */
#define DTH_VGA_BASE 0x000a0000
#define DTH_SCREEN_W 0x140
#define DTH_SCREEN_H 0xc8
#define DTH_SCREEN_BYTES (DTH_SCREEN_W * DTH_SCREEN_H)
#define DTH_MODE_TEXT 0x03
#define DTH_MODE_320X200X256 0x13

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the routine spins on the counter. */
#define DTH_TIMER_VECTOR 8

/* The window the routine copies out of its page: 312x192 taken from page byte
   0x21d8, which is page pixel (24,24), and landing at screen byte 0x504, which
   is screen pixel (4,4).  A page column is therefore 20 lower on screen. */
#define DTH_WINDOW_ROW 4
#define DTH_WINDOW_COL 4
#define DTH_WINDOW_W 0x138
#define DTH_WINDOW_H 0xc0
#define DTH_PAGE_BORDER 24
#define DTH_PAGE_BYTES 0x15180
#define DTH_TO_SCREEN (DTH_WINDOW_COL - DTH_PAGE_BORDER)

/* What a screen byte the routine did not write holds. */
#define DTH_BORDER_FILL 0xa5

/* A map tile and the six-pixel lift the request origin carries. */
#define DTH_TILE 0x18
#define DTH_LIFT 6

/* The portrait id fdps_draw_map_unit drops a unit on, so no staged unit needs
   a walk sprite. */
#define DTH_NO_MAP_SPRITE 0x80

/* Bit 7 of the flags byte, the acted-this-turn flag the mark wipes along with
   everything else in that byte. */
#define DTH_FLAG_ACTED 0x80

/* Six records, so a unit can be parked one past the count the sweep is given
   and two can die at once. */
#define DTH_UNITS 6

/* Where the dying units are put, and where their marks therefore land: page
   column tile * 24 and page row tile * 24 - 6, both carried out to the
   screen. */
#define DTH_TILE_X 3
#define DTH_TILE_Y 3
#define DTH_MARK_COL (DTH_TILE_X * DTH_TILE + DTH_TO_SCREEN)
#define DTH_MARK_ROW (DTH_TILE_Y * DTH_TILE - DTH_LIFT + DTH_TO_SCREEN)
#define DTH_TILE2_X 8
#define DTH_TILE2_Y 5
#define DTH_MARK2_COL (DTH_TILE2_X * DTH_TILE + DTH_TO_SCREEN)
#define DTH_MARK2_ROW (DTH_TILE2_Y * DTH_TILE - DTH_LIFT + DTH_TO_SCREEN)

/* A view scrolled off the map origin, and where the mark lands then.  Neither
   number is a multiple of the tile, so a run that scaled the scroll or applied
   it to the wrong axis misses. */
#define DTH_SCROLL_X 10
#define DTH_SCROLL_Y 7

/* A tile column and row past 127, with the view scrolled so that widening the
   two bytes WITHOUT sign puts the mark back where the unscrolled case above
   left it.  Read with sign, 200 is -56 and 130 is -126, and the sprite lands
   thousands of pixels off the page. */
#define DTH_HIGH_TILE_X 200
#define DTH_HIGH_TILE_Y 130
#define DTH_HIGH_SCROLL_X (DTH_HIGH_TILE_X * DTH_TILE - DTH_TILE_X * DTH_TILE)
#define DTH_HIGH_SCROLL_Y (DTH_HIGH_TILE_Y * DTH_TILE - DTH_TILE_Y * DTH_TILE)

/* The synthetic sheet: three frames of one 4x2 cell each, frame i's cell eight
   pixels right of frame i-1's and painted in its own colour.  Frames are 0x18
   bytes apart, which clears the 0x0a header and the 13-byte layer of the
   record before it.  Command 0x03 is a fill run of four pixels
   (resource_info/cel.md), so two of them make the two rows of the cell, and
   sound 0xffff is the -1 the mixer rejects. */
#define DTH_CELL_W 4
#define DTH_CELL_H 2
#define DTH_FRAMES 3
#define DTH_MARK_STEP 8

/* The .SAF header fields the readers address, resource_info/saf.md. */
#define DSAF_CELL_W_AT 0x07
#define DSAF_CELL_H_AT 0x09
#define DSAF_FRAME_COUNT_AT 0x0c
#define DSAF_FRAME_TABLE_PTR_AT 0x0e
#define DSAF_TILEMAP_COUNT_AT 0x16
#define DSAF_TILEMAP_TABLE_PTR_AT 0x18
#define DSAF_TILE_COUNT_AT 0x20
#define DSAF_TILE_TABLE_PTR_AT 0x22

/* Where the fixture puts each of the four sections. */
#define DSAF_TILE_TABLE_AT 0x34
#define DSAF_TILE0_STREAM_AT 0x40
#define DSAF_TILEMAP_TABLE_AT 0x4c
#define DSAF_TILEMAP0_AT 0x58
#define DSAF_FRAME_TABLE_AT 0x70
#define DSAF_FRAME0_AT 0x80
#define DSAF_TILE_STREAM_BYTES 4
#define DSAF_TILEMAP_BYTES 8
#define DSAF_FRAME_BYTES 0x18
#define DSAF_IMAGE_BYTES 0x100

/* One frame record and the one layer behind it. */
#define DSAF_FRAME_SOUND_AT 0x00
#define DSAF_FRAME_DURATION_AT 0x02
#define DSAF_FRAME_LAYERS_AT 0x08
#define DSAF_LAYER_AT 0x0a
#define DSAF_LAYER_TILEMAP_AT 0x00
#define DSAF_LAYER_X_AT 0x02
#define DSAF_LAYER_Y_AT 0x04
#define DSAF_LAYER_BLEND_AT 0x06
#define DSAF_NO_SOUND 0xffff

/* The container, resource_info/vfs.md: an 11-byte header, a 24-byte packer
   signature and then 26-byte directory entries from byte 35. */
#define DVFS_TABLE_AT 35
#define DVFS_ENTRY_BYTES 26
#define DVFS_ENTRY_SIZE_AT 0x0d
#define DVFS_ENTRY_SIZE2_AT 0x11
#define DVFS_ENTRY_START_AT 0x16
#define DVFS_MEMBER "EXPLO.SAF"
#define DVFS_MEMBER_AT (DVFS_TABLE_AT + DVFS_ENTRY_BYTES)
#define DVFS_IMAGE_BYTES (DVFS_MEMBER_AT + DSAF_IMAGE_BYTES)

static struct fdps_unit_record dth_units[DTH_UNITS];
static unsigned char dth_saf[DSAF_IMAGE_BYTES];
static unsigned char dth_vfs[DVFS_IMAGE_BYTES];

/* The snapshot is on the heap and not a static, for the reason tests/anim.c
   gives: a 64,000-byte frame is a lot of BSS to carry for one file. */
static unsigned char *dth_screen;
static void (__interrupt __far *dth_saved_timer)();
static int dth_blocks_before;
static int dth_blocks_after;

static void __interrupt __far dth_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(dth_saved_timer);
}

static void dth_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void dth_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* Which colour frame index carries.  None of them is 0, the drawer's
   transparency key, and none is the border sentinel. */
static int dth_mark_pixel(int frame_index)
{
    return 0x11 * (frame_index + 1);
}

/* The sheet: three tiles of their own colour, one single-cell tilemap each,
   and three frames whose layers step eight pixels apart. */
static void dth_stage_sheet(void)
{
    int frame_index;
    int tile_at;
    int tilemap_at;
    int frame_at;

    memset(dth_saf, 0, (size_t) DSAF_IMAGE_BYTES);
    dth_saf[0] = 'S';
    dth_saf[1] = 'A';
    dth_saf[2] = 'F';
    dth_u16(dth_saf, DSAF_CELL_W_AT, DTH_CELL_W);
    dth_u16(dth_saf, DSAF_CELL_H_AT, DTH_CELL_H);

    dth_u16(dth_saf, DSAF_TILE_COUNT_AT, DTH_FRAMES);
    dth_u32(dth_saf, DSAF_TILE_TABLE_PTR_AT,
            (unsigned long) DSAF_TILE_TABLE_AT);
    dth_u16(dth_saf, DSAF_TILEMAP_COUNT_AT, DTH_FRAMES);
    dth_u32(dth_saf, DSAF_TILEMAP_TABLE_PTR_AT,
            (unsigned long) DSAF_TILEMAP_TABLE_AT);
    dth_u16(dth_saf, DSAF_FRAME_COUNT_AT, DTH_FRAMES);
    dth_u32(dth_saf, DSAF_FRAME_TABLE_PTR_AT,
            (unsigned long) DSAF_FRAME_TABLE_AT);

    for (frame_index = 0; frame_index < DTH_FRAMES; frame_index++) {
        tile_at = DSAF_TILE0_STREAM_AT
                  + frame_index * DSAF_TILE_STREAM_BYTES;
        tilemap_at = DSAF_TILEMAP0_AT + frame_index * DSAF_TILEMAP_BYTES;
        frame_at = DSAF_FRAME0_AT + frame_index * DSAF_FRAME_BYTES;

        dth_u32(dth_saf, DSAF_TILE_TABLE_AT + frame_index * 4,
                (unsigned long) tile_at);
        dth_saf[tile_at] = 0x03;
        dth_saf[tile_at + 1] = (unsigned char) dth_mark_pixel(frame_index);
        dth_saf[tile_at + 2] = 0x03;
        dth_saf[tile_at + 3] = (unsigned char) dth_mark_pixel(frame_index);

        dth_u32(dth_saf, DSAF_TILEMAP_TABLE_AT + frame_index * 4,
                (unsigned long) tilemap_at);
        dth_u16(dth_saf, tilemap_at, 1);
        dth_u16(dth_saf, tilemap_at + 2, 1);
        dth_u16(dth_saf, tilemap_at + 4, (unsigned int) frame_index);

        dth_u32(dth_saf, DSAF_FRAME_TABLE_AT + frame_index * 4,
                (unsigned long) frame_at);
        dth_u16(dth_saf, frame_at + DSAF_FRAME_SOUND_AT, DSAF_NO_SOUND);
        dth_u16(dth_saf, frame_at + DSAF_FRAME_DURATION_AT, 1);
        dth_u16(dth_saf, frame_at + DSAF_FRAME_LAYERS_AT, 1);
        dth_u16(dth_saf, frame_at + DSAF_LAYER_AT + DSAF_LAYER_TILEMAP_AT,
                (unsigned int) frame_index);
        dth_u16(dth_saf, frame_at + DSAF_LAYER_AT + DSAF_LAYER_X_AT,
                (unsigned int) (frame_index * DTH_MARK_STEP));
        dth_u16(dth_saf, frame_at + DSAF_LAYER_AT + DSAF_LAYER_Y_AT, 0);
        dth_saf[frame_at + DSAF_LAYER_AT + DSAF_LAYER_BLEND_AT] = 0;
    }
}

/* The container that sheet is looked up in, published as the resident archive
   the routine reads. */
static void dth_stage_container(void)
{
    dth_stage_sheet();
    memset(dth_vfs, 0, (size_t) DVFS_IMAGE_BYTES);
    dth_vfs[0] = 'V';
    dth_vfs[1] = 'F';
    dth_vfs[2] = 'S';
    dth_u16(dth_vfs, 3, 1);
    dth_u16(dth_vfs, 5, DVFS_TABLE_AT);
    dth_u32(dth_vfs, 7, 1);
    strcpy((char *) dth_vfs + DVFS_TABLE_AT, DVFS_MEMBER);
    dth_u32(dth_vfs, DVFS_TABLE_AT + DVFS_ENTRY_SIZE_AT,
            (unsigned long) DSAF_IMAGE_BYTES);
    dth_u32(dth_vfs, DVFS_TABLE_AT + DVFS_ENTRY_SIZE2_AT,
            (unsigned long) DSAF_IMAGE_BYTES);
    dth_u32(dth_vfs, DVFS_TABLE_AT + DVFS_ENTRY_START_AT,
            (unsigned long) DVFS_MEMBER_AT);
    memmove(dth_vfs + DVFS_MEMBER_AT, dth_saf, (size_t) DSAF_IMAGE_BYTES);
}

/* Nothing on the map and nothing in the way -- no scene layers, no cursor
   overlay, no terrain panel and no unit the map drawer will look at -- so the
   only thing that reaches the page is the explosion. */
static void dth_stage(int live_unit_count)
{
    int offset;

    for (offset = 0; offset < (int) sizeof(dth_units); offset++) {
        ((unsigned char *) dth_units)[offset] = 0;
    }
    for (offset = 0; offset < DTH_UNITS; offset++) {
        dth_units[offset].portrait_id = DTH_NO_MAP_SPRITE;
        dth_units[offset].hp_current = 1;
    }
    dth_stage_container();
    data_fdps_map_unit_array_ptr = (unsigned char *) dth_units;
    data_fdps_map_unit_count = live_unit_count;
    data_fdps_animation_baseani_archive_ptr = dth_vfs;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
}

/* Put the staged globals back the way a freshly started program has them, for
   the reason tests/anim.c gives: both of these hold blocks the game's own
   loaders free, and leaving one pointing at a static here hands a later test a
   free() of storage that never came from the heap. */
static void dth_unstage(void)
{
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_animation_baseani_archive_ptr = NULL;
    free(dth_screen);
    dth_screen = NULL;
}

static void dth_set_unit(int unit_index, int tile_column, int tile_row,
                         int facing, int flags, int hp)
{
    dth_units[unit_index].pos_x = (unsigned char) tile_column;
    dth_units[unit_index].pos_y = (unsigned char) tile_row;
    dth_units[unit_index].facing = (unsigned char) facing;
    dth_units[unit_index].flags = (unsigned char) flags;
    dth_units[unit_index].hp_current = (short) hp;
}

static void dth_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Used entries currently in the heap, so a case can say the page came back. */
static int dth_used_heap_blocks(void)
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

/* Leave a zeroed block of exactly the page's size at the head of the free
   list. */
static void dth_seed_page(void)
{
    unsigned char *page;

    page = (unsigned char *) malloc((size_t) DTH_PAGE_BYTES);
    if (page != NULL) {
        memset(page, 0, (size_t) DTH_PAGE_BYTES);
        free(page);
    }
}

/* One whole run, leaving the frame in dth_screen[]. */
static void dth_run(void)
{
    dth_screen = (unsigned char *) malloc((size_t) DTH_SCREEN_BYTES);
    CHECK_EQ(dth_screen != NULL, 1);
    if (dth_screen == NULL) {
        return;
    }
    memset(dth_screen, DTH_BORDER_FILL, (size_t) DTH_SCREEN_BYTES);

    dth_blocks_before = dth_used_heap_blocks();
    dth_set_mode(DTH_MODE_320X200X256);
    memset((void *) DTH_VGA_BASE, DTH_BORDER_FILL, (size_t) DTH_SCREEN_BYTES);
    dth_seed_page();

    dth_saved_timer = _dos_getvect(DTH_TIMER_VECTOR);
    _dos_setvect(DTH_TIMER_VECTOR, dth_timer_isr);
    fdps_play_death_animation_and_mark_dead();
    _dos_setvect(DTH_TIMER_VECTOR, dth_saved_timer);

    memmove(dth_screen, (void *) DTH_VGA_BASE, (size_t) DTH_SCREEN_BYTES);
    dth_set_mode(DTH_MODE_TEXT);
    dth_blocks_after = dth_used_heap_blocks();
}

static int dth_pixel(int row, int col)
{
    return (int) dth_screen[row * DTH_SCREEN_W + col];
}

/* Every mark one dying unit left, read at both ends of its 4x2 cell, with the
   pixel just left of the first one and the row above it still the page seed so
   a mark one column or row wide of where it belongs fails here. */
static void dth_marks_are(int first_col, int row)
{
    int frame_index;
    int col;

    for (frame_index = 0; frame_index < DTH_FRAMES; frame_index++) {
        col = first_col + frame_index * DTH_MARK_STEP;
        CHECK_EQ(dth_pixel(row, col), dth_mark_pixel(frame_index));
        CHECK_EQ(dth_pixel(row + DTH_CELL_H - 1, col + DTH_CELL_W - 1),
                 dth_mark_pixel(frame_index));
    }
    CHECK_EQ(dth_pixel(row, first_col - 1), 0);
    CHECK_EQ(dth_pixel(row - 1, first_col), 0);
}

/* The five record bytes the routine addresses by literal displacement: +0 and
   +1 for the tile the explosion is placed over, +3 for the facing the spin
   writes, +5 for the flags byte it tests and then marks, and +0x40 for the
   hit-point word that decides who dies.  If the layout moved, every case below
   would still pass while reading the wrong bytes. */
static void dth_reads_the_measured_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, facing), 3);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
}

/* CMP dword ptr [EBP-0x24],0x0 / JZ at 0001d74c goes to the epilogue, so an
   action that killed nobody costs nothing: no record is touched, no page is
   taken from the heap and not one pixel of the window is written. */
static void dth_nothing_dying_leaves_everything_alone(void)
{
    dth_stage(3);
    dth_set_unit(0, DTH_TILE_X, DTH_TILE_Y, 3, DTH_FLAG_ACTED, 5);
    dth_set_unit(1, DTH_TILE_X, DTH_TILE_Y, 3, 0, 1);
    dth_set_unit(2, DTH_TILE_X, DTH_TILE_Y, 3, 0, -1);
    dth_run();
    CHECK_EQ(dth_units[0].flags, DTH_FLAG_ACTED);
    CHECK_EQ(dth_units[1].flags, 0);
    CHECK_EQ(dth_units[2].flags, 0);
    CHECK_EQ(dth_units[0].facing, 3);
    CHECK_EQ(dth_units[2].facing, 3);
    CHECK_EQ(dth_blocks_after, dth_blocks_before);
    CHECK_EQ(dth_pixel(DTH_MARK_ROW, DTH_MARK_COL), DTH_BORDER_FILL);
    CHECK_EQ(dth_pixel(DTH_WINDOW_ROW, DTH_WINDOW_COL), DTH_BORDER_FILL);
    dth_unstage();
}

/* The hit-point test is CMP word ptr [EAX+0x40],0x0 / JZ, an EQUALITY: a unit
   at 1 lives, and so does one the fight drove to -1, which the two collectors
   above would both have taken.  Bit 0 of the flags byte disqualifies a unit
   however dead it is, and the byte of the one it rejects comes back untouched
   rather than rewritten to 1. */
static void dth_only_a_unit_resting_at_exactly_zero_dies(void)
{
    dth_stage(4);
    dth_set_unit(0, DTH_TILE_X, DTH_TILE_Y, 3, 0, 0);
    dth_set_unit(1, DTH_TILE_X, DTH_TILE_Y, 3, 0, 1);
    dth_set_unit(2, DTH_TILE_X, DTH_TILE_Y, 3, 0, -1);
    dth_set_unit(3, DTH_TILE_X, DTH_TILE_Y, 3, DTH_FLAG_ACTED | 1, 0);
    dth_run();
    CHECK_EQ(dth_units[0].flags, 1);
    CHECK_EQ(dth_units[1].flags, 0);
    CHECK_EQ(dth_units[2].flags, 0);
    CHECK_EQ(dth_units[3].flags, DTH_FLAG_ACTED | 1);
    dth_unstage();
}

/* MOV byte ptr [EAX+0x5],0x1 writes the whole byte, so a unit that had already
   acted this turn comes out of the mark holding 1 and not 0x81.  Spelling the
   mark as a bit set leaves bit 7 standing and fails here. */
static void dth_the_mark_overwrites_the_whole_flags_byte(void)
{
    dth_stage(1);
    dth_set_unit(0, DTH_TILE_X, DTH_TILE_Y, 3, DTH_FLAG_ACTED, 0);
    dth_run();
    CHECK_EQ(dth_units[0].flags, 1);
    dth_unstage();
}

/* Thirteen spin frames numbered 0 to 12, each storing its number modulo 4, so
   the last one leaves every dying unit facing 0 -- down.  A unit that is not
   dying keeps the facing it had. */
static void dth_the_spin_leaves_every_dying_unit_facing_down(void)
{
    dth_stage(2);
    dth_set_unit(0, DTH_TILE_X, DTH_TILE_Y, 3, 0, 0);
    dth_set_unit(1, DTH_TILE2_X, DTH_TILE2_Y, 3, 0, 9);
    dth_run();
    CHECK_EQ(dth_units[0].facing, 0);
    CHECK_EQ(dth_units[1].facing, 3);
    dth_unstage();
}

/* The sweep is bounded by data_fdps_map_unit_count with a signed JL before the
   body, so the unit sitting at index == count is outside it although the array
   holds it and it qualifies on both tests. */
static void dth_the_sweep_stops_at_the_unit_count(void)
{
    dth_stage(2);
    dth_set_unit(0, DTH_TILE_X, DTH_TILE_Y, 3, 0, 0);
    dth_set_unit(1, DTH_TILE2_X, DTH_TILE2_Y, 3, 0, 0);
    dth_set_unit(2, DTH_TILE_X, DTH_TILE_Y, 3, 0, 0);
    dth_run();
    CHECK_EQ(dth_units[0].flags, 1);
    CHECK_EQ(dth_units[1].flags, 1);
    CHECK_EQ(dth_units[2].flags, 0);
    CHECK_EQ(dth_units[2].facing, 3);
    dth_unstage();
}

/* The explosion's origin is the unit's tile times 24, less the view scroll,
   less six rows, and the three marks piled up on the page say the loop made
   one pass per frame of the sheet and drew frame i on pass i. */
static void dth_the_explosion_lands_on_the_dying_units_tile(void)
{
    dth_stage(1);
    dth_set_unit(0, DTH_TILE_X, DTH_TILE_Y, 3, 0, 0);
    dth_run();
    dth_marks_are(DTH_MARK_COL, DTH_MARK_ROW);
    dth_unstage();
}

/* The same unit with the view scrolled: both origins are subtracted, each from
   its own axis, and neither is scaled by the tile. */
static void dth_the_view_scroll_origin_is_subtracted(void)
{
    dth_stage(1);
    dth_set_unit(0, DTH_TILE_X, DTH_TILE_Y, 3, 0, 0);
    data_fdps_battle_view_window_origin_x = DTH_SCROLL_X;
    data_fdps_battle_view_window_origin_y = DTH_SCROLL_Y;
    dth_run();
    dth_marks_are(DTH_MARK_COL - DTH_SCROLL_X, DTH_MARK_ROW - DTH_SCROLL_Y);
    dth_unstage();
}

/* MOV AL,byte ptr [EAX] / AND EAX,0xff widens both tile bytes WITHOUT sign, so
   a tile at column 200 and row 130 is far down and to the right.  The view is
   scrolled by exactly the difference, which puts the marks back where the
   unscrolled case left them; read with sign the sprite lands thousands of
   pixels off the page and nothing is drawn at all. */
static void dth_the_tile_bytes_are_widened_without_sign(void)
{
    dth_stage(1);
    dth_set_unit(0, DTH_HIGH_TILE_X, DTH_HIGH_TILE_Y, 3, 0, 0);
    data_fdps_battle_view_window_origin_x = DTH_HIGH_SCROLL_X;
    data_fdps_battle_view_window_origin_y = DTH_HIGH_SCROLL_Y;
    dth_run();
    dth_marks_are(DTH_MARK_COL, DTH_MARK_ROW);
    dth_unstage();
}

/* Every unit on the list gets the whole sheet drawn over its own tile in the
   same frame, so two units dying together leave two full sets of marks. */
static void dth_every_dying_unit_gets_its_own_explosion(void)
{
    dth_stage(3);
    dth_set_unit(0, DTH_TILE_X, DTH_TILE_Y, 3, 0, 0);
    dth_set_unit(1, DTH_TILE2_X, DTH_TILE2_Y, 3, 0, 4);
    dth_set_unit(2, DTH_TILE2_X, DTH_TILE2_Y, 3, 0, 0);
    dth_run();
    dth_marks_are(DTH_MARK_COL, DTH_MARK_ROW);
    dth_marks_are(DTH_MARK2_COL, DTH_MARK2_ROW);
    CHECK_EQ(dth_units[0].flags, 1);
    CHECK_EQ(dth_units[1].flags, 0);
    CHECK_EQ(dth_units[2].flags, 1);
    dth_unstage();
}

/* free at 0001d977 gives the page back, so a run leaves the heap holding
   exactly what it held before.  This is also what the page seeding above
   depends on: every undrawn window pixel reading 0 in the cases that read
   marks says the routine composed on the block that was seeded. */
static void dth_the_page_comes_back_to_the_heap(void)
{
    dth_stage(1);
    dth_set_unit(0, DTH_TILE_X, DTH_TILE_Y, 3, 0, 0);
    dth_run();
    CHECK_EQ(dth_blocks_after, dth_blocks_before);
    CHECK_EQ(dth_pixel(DTH_MARK_ROW, DTH_MARK_COL - 2), 0);
    dth_unstage();
}

/* The window presented is 312 x 192 at screen (4,4): the four-pixel border is
   never written, and the row and column just inside it are. */
static void dth_only_the_inset_window_is_presented(void)
{
    dth_stage(1);
    dth_set_unit(0, DTH_TILE_X, DTH_TILE_Y, 3, 0, 0);
    dth_run();
    CHECK_EQ(dth_pixel(0, 0), DTH_BORDER_FILL);
    CHECK_EQ(dth_pixel(DTH_WINDOW_ROW - 1, DTH_WINDOW_COL), DTH_BORDER_FILL);
    CHECK_EQ(dth_pixel(DTH_WINDOW_ROW, DTH_WINDOW_COL - 1), DTH_BORDER_FILL);
    CHECK_EQ(dth_pixel(DTH_WINDOW_ROW + DTH_WINDOW_H, DTH_WINDOW_COL),
             DTH_BORDER_FILL);
    CHECK_EQ(dth_pixel(DTH_WINDOW_ROW, DTH_WINDOW_COL + DTH_WINDOW_W),
             DTH_BORDER_FILL);
    CHECK_EQ(dth_pixel(DTH_WINDOW_ROW, DTH_WINDOW_COL), 0);
    CHECK_EQ(dth_pixel(DTH_WINDOW_ROW + DTH_WINDOW_H - 1,
                       DTH_WINDOW_COL + DTH_WINDOW_W - 1), 0);
    dth_unstage();
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
    RUN_TEST(dth_reads_the_measured_offsets);
    RUN_TEST(dth_nothing_dying_leaves_everything_alone);
    RUN_TEST(dth_only_a_unit_resting_at_exactly_zero_dies);
    RUN_TEST(dth_the_mark_overwrites_the_whole_flags_byte);
    RUN_TEST(dth_the_spin_leaves_every_dying_unit_facing_down);
    RUN_TEST(dth_the_sweep_stops_at_the_unit_count);
    RUN_TEST(dth_the_explosion_lands_on_the_dying_units_tile);
    RUN_TEST(dth_the_view_scroll_origin_is_subtracted);
    RUN_TEST(dth_the_tile_bytes_are_widened_without_sign);
    RUN_TEST(dth_every_dying_unit_gets_its_own_explosion);
    RUN_TEST(dth_the_page_comes_back_to_the_heap);
    RUN_TEST(dth_only_the_inset_window_is_presented);
}
