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
#include "keybd.h"
#include "chapter.h"
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


/* ------------------------------------------------------------------
 * fdps_run_death_scripts, 0001d990.
 *
 * Expected values come from the assembly: the CMP dword ptr [EBP+0x18],0x0 /
 * JZ 0x0001dd21 at 0001d99c that leaves a count of zero past both flushes,
 * the XOR EAX,EAX / MOV AL at 0001d9f1 and the MOVSX at 0001d9df that make the
 * opcode an unsigned byte and the operand a signed word three bytes apart, the
 * two JMP 0x0001dd21 at 0001da22 and 0001dc32 that leave the WHOLE function
 * when the actor is not a live player unit, the LEA EDX,[EDX*0x4] / CALL dword
 * ptr [EDX+0x601c4] / ADD ESP,0x4 at 0001dca3 that dispatches one argument
 * through the chapter-event table, the CMP dword ptr [EBP-0x18],0xff at
 * 0001dcbf and CMP dword ptr [EBP-0x18],-0x1 at 0001dcc8 that suppress a line
 * on either sentinel, the PUSH 0xa0000 at 0001dcde that puts the line at the
 * screen origin, and the MOV dword ptr [0x00069da0],0x2 at 0001dcfb and the
 * same store of 1 at 0001dd0d.  None of them was read off the emitted C.
 *
 * THE KEYBOARD FLUSH IS THE INSTRUMENT.  fdps_flush_keyboard_queue is three
 * instructions that make the ring's write index equal its read index
 * (keybd.h), so staging the two apart makes each flush visible, and a
 * chapter-event handler of the fixture's own that pushes them apart again
 * makes the CLOSING flush visible on its own.  That is what tells the guard's
 * return apart from a continue: the return abandons the closing flush as well
 * as the records behind it.
 *
 * WHAT IS NOT COVERED.  Opcodes 0 and 1, the item drop and the gold, past
 * their guard: both open a modal message window on a portrait, draw through
 * the window and wait on the keyboard ring, and the bag-full arm additionally
 * runs the two-choice prompt and the item-select window.  A unit runner has
 * neither a keyboard nor the loaded resources those need, so the payout
 * arithmetic -- the item name's id + 0xc9, the gold added from
 * data_fdps_dialog_last_action_value_param rather than from the operand -- is
 * left to the manual playtest (ADR-0003).  What the cases below do reach of
 * those two opcodes is the guard that stands in front of them.
 * ------------------------------------------------------------------ */

/* Four staged units, of which index 2 is the actor every case passes, so an
   argument that reached a handler as 0 or as the record index would be
   visible.  Its portrait id is the one the reward path would open a window
   on; nothing in these cases opens one. */
#define RDS_UNITS 4
#define RDS_ACTOR 2
#define RDS_PORTRAIT 7

/* The three sides the record's byte at offset 6 can hold. */
#define RDS_SIDE_ENEMY 0
#define RDS_SIDE_NPC 1
#define RDS_SIDE_PLAYER 2

/* Bit 0 of the flags byte, the retirement flag fdps_unit_is_retired answers
   from. */
#define RDS_FLAG_RETIRED 0x01

/* The scancode ring's two indices, staged apart so a flush is visible.  A
   flush stores the read index over the write index, so "flushed" is
   write == RDS_QUEUE_HEAD and "not flushed" is write == RDS_QUEUE_WRITE. */
#define RDS_QUEUE_HEAD 5
#define RDS_QUEUE_WRITE 9

/* Which chapter-event slot the fixture's handler is installed in.  Not 0, so a
   dispatch that ignored the operand and took slot 0 would call the other
   handler instead and be visible. */
#define RDS_EVENT_SLOT 7
#define RDS_OTHER_EVENT_SLOT 0

/* The two verdicts and the "battle still running" value the fixture starts
   from. */
#define RDS_END_RUNNING 0
#define RDS_END_CLEARED 2
#define RDS_END_DEFEAT 1

/* The operand that means "this record carries no line", and the other one. */
#define RDS_NO_LINE 0xff

/* The line's own fixture: one 8x1 glyph cell per glyph, one byte of it, whose
   glyph 0 sets the bit for column 0.  So the screen byte the line lands on
   says both that the draw happened and where its pen was. */
#define RDS_CELL_WIDTH 8
#define RDS_CELL_ROWS 1
#define RDS_GLYPH_STRIDE 1
#define RDS_ADVANCE 8
#define RDS_LINE_HEIGHT 1

/* The adapter and the byte the cases fill it with, the same arrangement the
   destruction-sequence cases above use: mode 13h so the aperture at 0xa0000 is
   decoded, and text mode again afterwards. */
#define RDS_VGA_BASE 0x000a0000
#define RDS_SCREEN_BYTES (0x140 * 0xc8)
#define RDS_SCREEN_FILL 0xa5
#define RDS_MODE_TEXT 0x03
#define RDS_MODE_320X200X256 0x13

/* The colours the draw asks for, PUSH 0x6d / PUSH 0x0 / PUSH 0xd0. */
#define RDS_TEXT_FG 0xd0

static struct fdps_unit_record rds_units[RDS_UNITS];
static unsigned char rds_scripts[16];
static unsigned char rds_font[RDS_GLYPH_STRIDE * 4];
static unsigned char rds_text_block[32];
static unsigned char *rds_screen;
static int rds_handler_calls;
static int rds_handler_arg;
static int rds_other_handler_calls;

/* The handler the event cases install: it records that it ran and with what,
   and pushes the ring's two indices apart again so the closing flush has
   something to close. */
static void rds_event_handler(int unit_index)
{
    rds_handler_calls++;
    rds_handler_arg = unit_index;
    data_fdps_input_scancode_queue_write_index = RDS_QUEUE_WRITE;
}

static void rds_other_event_handler(int unit_index)
{
    rds_other_handler_calls++;
    rds_handler_arg = unit_index;
}

static void rds_stage(int actor_side, int actor_flags)
{
    int offset;

    for (offset = 0; offset < (int) sizeof(rds_units); offset++) {
        ((unsigned char *) rds_units)[offset] = 0;
    }
    for (offset = 0; offset < (int) sizeof(rds_scripts); offset++) {
        rds_scripts[offset] = 0;
    }
    rds_units[RDS_ACTOR].side = (unsigned char) actor_side;
    rds_units[RDS_ACTOR].flags = (unsigned char) actor_flags;
    rds_units[RDS_ACTOR].portrait_id = (unsigned char) RDS_PORTRAIT;

    data_fdps_map_unit_array_ptr = (unsigned char *) rds_units;
    data_fdps_map_unit_count = RDS_UNITS;
    data_fdps_chapter_event_or_battle_end_code =
        (unsigned int) RDS_END_RUNNING;
    data_fdps_input_scancode_queue_head = RDS_QUEUE_HEAD;
    data_fdps_input_scancode_queue_write_index = RDS_QUEUE_WRITE;

    data_fdps_chapter_event_handler_table[RDS_EVENT_SLOT] = rds_event_handler;
    data_fdps_chapter_event_handler_table[RDS_OTHER_EVENT_SLOT] =
        rds_other_event_handler;
    rds_handler_calls = 0;
    rds_other_handler_calls = 0;
    rds_handler_arg = -1;
}

/* Put back what a freshly started program holds, for the reason tests/anim.c
   gives: these globals name blocks the game's own loaders free, and leaving
   one pointing at a static here hands a later test a free() of storage that
   never came from the heap.  The two table slots go back to null for the same
   reason -- a code pointer into this file must not outlive the case. */
static void rds_unstage(void)
{
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_current_chapter_text_ptr = NULL;
    data_fdps_font_sheet_ptr = NULL;
    data_fdps_font_glyph_width = (unsigned char) 0;
    data_fdps_glyph_cell_height = (unsigned char) 0;
    data_fdps_font_glyph_stride_bytes = 0;
    data_fdps_glyph_advance_x = 0;
    data_fdps_font_line_height = 0;
    data_fdps_chapter_event_handler_table[RDS_EVENT_SLOT] = NULL;
    data_fdps_chapter_event_handler_table[RDS_OTHER_EVENT_SLOT] = NULL;
}

/* One record: the opcode byte and the signed 16-bit operand behind it, three
   bytes apart. */
static void rds_put(int record_index, int opcode, int operand)
{
    rds_scripts[record_index * 3] = (unsigned char) opcode;
    *(short *) (rds_scripts + record_index * 3 + 1) = (short) operand;
}

/* CMP dword ptr [EBP+0x18],0x0 / JZ 0x0001dd21: a count of zero jumps to the
   epilogue, so the record in the buffer is never read, no verdict is recorded
   and NEITHER flush runs -- the ring's two indices are still apart. */
static void rds_a_count_of_zero_does_nothing_at_all(void)
{
    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_put(0, 4, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 0, rds_scripts);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_RUNNING);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, RDS_QUEUE_WRITE);
    rds_unstage();
}

/* MOV dword ptr [0x00069da0],0x2 at 0001dcfb and the same store of 1 at
   0001dd0d: opcode 4 is the chapter cleared and opcode 5 the defeat.  Both
   carry the no-line operand, so the verdict is recorded whether or not the
   record had a line to draw. */
static void rds_the_two_verdict_opcodes_set_the_end_code(void)
{
    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_put(0, 4, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 1, rds_scripts);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_CLEARED);

    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_put(0, 5, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 1, rds_scripts);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code, RDS_END_DEFEAT);
    rds_unstage();
}

/* CMP dword ptr [EBP-0x14],0x4 / JNZ and CMP 0x5 / JNZ: only those two write
   the code.  Opcode 3 is a line and nothing else, and an opcode above the
   window -- 6 here, and 0x80, which is only above it because the byte is
   widened WITHOUT sign -- stops at the line as well. */
static void rds_the_other_line_opcodes_leave_the_end_code_alone(void)
{
    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_put(0, 3, RDS_NO_LINE);
    rds_put(1, 6, RDS_NO_LINE);
    rds_put(2, 0x80, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 3, rds_scripts);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_RUNNING);
    rds_unstage();
}

/* The walk is front to back and every record runs, so the LAST verdict in the
   array is the one that stands.  Both orders are tried, because a walk that
   ran backwards would pass one of them by accident. */
static void rds_the_records_run_front_to_back(void)
{
    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_put(0, 5, RDS_NO_LINE);
    rds_put(1, 4, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 2, rds_scripts);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_CLEARED);

    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_put(0, 4, RDS_NO_LINE);
    rds_put(1, 5, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 2, rds_scripts);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code, RDS_END_DEFEAT);
    rds_unstage();
}

/* The count is the bound: a record sitting one past it is not run even though
   the buffer holds it. */
static void rds_the_count_bounds_the_walk(void)
{
    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_put(0, 4, RDS_NO_LINE);
    rds_put(1, 5, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 1, rds_scripts);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_CLEARED);
    rds_unstage();
}

/* LEA EDX,[EDX*0x4] / CALL dword ptr [EDX+0x601c4] / ADD ESP,0x4: the operand
   is the slot, scaled by the four bytes a pointer takes, and the single
   argument is the actor index this call was handed -- not the record index and
   not 0.  The handler in slot 0 stays untouched, so a dispatch that ignored
   the operand would be visible. */
static void rds_the_event_opcode_calls_its_table_slot(void)
{
    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_put(0, 2, RDS_EVENT_SLOT);
    fdps_run_death_scripts(RDS_ACTOR, 1, rds_scripts);
    CHECK_EQ(rds_handler_calls, 1);
    CHECK_EQ(rds_other_handler_calls, 0);
    CHECK_EQ(rds_handler_arg, RDS_ACTOR);

    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_put(0, 2, RDS_OTHER_EVENT_SLOT);
    fdps_run_death_scripts(RDS_ACTOR, 1, rds_scripts);
    CHECK_EQ(rds_handler_calls, 0);
    CHECK_EQ(rds_other_handler_calls, 1);
    rds_unstage();
}

/* Both flushes: the one at 0001d9b5 before the walk and the one at 0001dd1c
   after it.  The handler pushes the ring's indices apart again in the middle
   of the run, so the equal pair at the end is the CLOSING flush and not the
   opening one. */
static void rds_the_run_flushes_the_queue_at_both_ends(void)
{
    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_put(0, 2, RDS_EVENT_SLOT);
    rds_put(1, 4, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 2, rds_scripts);
    CHECK_EQ(rds_handler_calls, 1);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, RDS_QUEUE_HEAD);
    rds_unstage();
}

/* CMP EAX,0x2 / JNZ 0x0001da22 -> JMP 0x0001dd21: an item record with an actor
   that is not on the player side leaves the whole function.  The verdict
   record behind it is dropped and the closing flush does not happen -- the
   ring's indices are still where the handler in front of the item record put
   them.  A continue in place of that return would set the end code and flush.
   Both the enemy side and the NPC side are tried, because the test is an
   equality against 2 and not a "not the enemy" one. */
static void rds_an_item_record_from_a_non_player_actor_ends_the_run(void)
{
    rds_stage(RDS_SIDE_ENEMY, 0);
    rds_put(0, 2, RDS_EVENT_SLOT);
    rds_put(1, 0, 3);
    rds_put(2, 4, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 3, rds_scripts);
    CHECK_EQ(rds_handler_calls, 1);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_RUNNING);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, RDS_QUEUE_WRITE);

    rds_stage(RDS_SIDE_NPC, 0);
    rds_put(0, 0, 3);
    rds_put(1, 4, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 2, rds_scripts);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_RUNNING);
    rds_unstage();
}

/* The same guard on the gold record, CMP EAX,0x2 / JNZ 0x0001dc32: the run
   ends there, the party purse is not touched and the verdict behind it is
   dropped.  The event record in front is what pushes the ring's indices apart
   again after the opening flush, so the pair still being apart at the end is
   the closing flush not happening. */
static void rds_a_gold_record_from_a_non_player_actor_ends_the_run(void)
{
    int purse;

    rds_stage(RDS_SIDE_ENEMY, 0);
    purse = data_fdps_shared_party_total_gold;
    rds_put(0, 2, RDS_EVENT_SLOT);
    rds_put(1, 1, 100);
    rds_put(2, 4, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 3, rds_scripts);
    CHECK_EQ(rds_handler_calls, 1);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_RUNNING);
    CHECK_EQ(data_fdps_shared_party_total_gold, purse);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, RDS_QUEUE_WRITE);
    rds_unstage();
}

/* TEST EAX,EAX / JZ after CALL fdps_unit_is_retired: the second half of the
   guard.  A player-side actor that has been taken off the map ends the run in
   exactly the same way, for the item record and for the gold one. */
static void rds_a_retired_player_actor_ends_the_run(void)
{
    rds_stage(RDS_SIDE_PLAYER, RDS_FLAG_RETIRED);
    rds_put(0, 2, RDS_EVENT_SLOT);
    rds_put(1, 0, 3);
    rds_put(2, 4, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 3, rds_scripts);
    CHECK_EQ(rds_handler_calls, 1);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_RUNNING);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, RDS_QUEUE_WRITE);

    rds_stage(RDS_SIDE_PLAYER, RDS_FLAG_RETIRED);
    rds_put(0, 1, 100);
    rds_put(1, 4, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 2, rds_scripts);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_RUNNING);
    rds_unstage();
}

/* The guard stands in front of the reward opcodes ONLY: an event record and a
   verdict record run for an enemy-side actor, which is what the map-AI path
   relies on -- it hands this function the index of the unit its own actor
   attacked. */
static void rds_a_non_player_actor_still_runs_the_event_opcodes(void)
{
    rds_stage(RDS_SIDE_ENEMY, RDS_FLAG_RETIRED);
    rds_put(0, 2, RDS_EVENT_SLOT);
    rds_put(1, 4, RDS_NO_LINE);
    fdps_run_death_scripts(RDS_ACTOR, 2, rds_scripts);
    CHECK_EQ(rds_handler_calls, 1);
    CHECK_EQ(rds_handler_arg, RDS_ACTOR);
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_CLEARED);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, RDS_QUEUE_HEAD);
    rds_unstage();
}

/* The glyph sheet and the one-entry text block the line cases draw from, and
   the font metrics fdps_draw_glyph reads out of the globals. */
static void rds_stage_text(void)
{
    int i;

    for (i = 0; i < (int) sizeof(rds_font); i++) {
        rds_font[i] = 0;
    }
    rds_font[0] = 0x80;
    for (i = 0; i < (int) sizeof(rds_text_block); i++) {
        rds_text_block[i] = 0;
    }
    /* Entry 0 is at byte 8 of the block: one glyph-0 token and the
       terminator. */
    *(short *) (rds_text_block + 0) = (short) 8;
    *(short *) (rds_text_block + 8) = (short) 0;
    *(short *) (rds_text_block + 10) = (short) -1;

    data_fdps_font_glyph_width = (unsigned char) RDS_CELL_WIDTH;
    data_fdps_glyph_cell_height = (unsigned char) RDS_CELL_ROWS;
    data_fdps_font_glyph_stride_bytes = RDS_GLYPH_STRIDE;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = RDS_ADVANCE;
    data_fdps_font_line_height = RDS_LINE_HEIGHT;
    data_fdps_font_sheet_ptr = rds_font;
    data_fdps_current_chapter_text_ptr = rds_text_block;
}

/* One run with the adapter in mode 13h, leaving the frame in rds_screen[]. */
static void rds_run_on_screen(int record_count)
{
    union REGS regs;

    rds_screen = (unsigned char *) malloc((size_t) RDS_SCREEN_BYTES);
    CHECK_EQ(rds_screen != NULL, 1);
    if (rds_screen == NULL) {
        return;
    }
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) RDS_MODE_320X200X256;
    int386(0x10, &regs, &regs);
    memset((void *) RDS_VGA_BASE, RDS_SCREEN_FILL, (size_t) RDS_SCREEN_BYTES);

    fdps_run_death_scripts(RDS_ACTOR, record_count, rds_scripts);

    memmove(rds_screen, (void *) RDS_VGA_BASE, (size_t) RDS_SCREEN_BYTES);
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) RDS_MODE_TEXT;
    int386(0x10, &regs, &regs);
}

static void rds_free_screen(void)
{
    free(rds_screen);
    rds_screen = NULL;
}

/* PUSH 0xa0000 / PUSH 0x140 at 0001dcde: the line goes to the screen ORIGIN at
   the visible pitch, out of data_fdps_current_chapter_text_ptr and not out of
   the global text block, and it is the operand that names the entry.  Glyph 0
   of the fixture paints column 0 of row 0, so screen byte 0 carries the pen
   colour and its neighbour does not. */
static void rds_a_line_is_drawn_at_the_screen_origin(void)
{
    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_stage_text();
    rds_put(0, 3, 0);
    rds_run_on_screen(1);
    if (rds_screen != NULL) {
        CHECK_EQ((int) rds_screen[0], RDS_TEXT_FG);
        CHECK_EQ((int) rds_screen[1], RDS_SCREEN_FILL);
    }
    rds_free_screen();
    rds_unstage();
}

/* CMP dword ptr [EBP-0x18],0xff / JZ and CMP dword ptr [EBP-0x18],-0x1 / JNZ:
   BOTH sentinels suppress the draw.  0x00ff is not -1 once the operand has
   been sign-extended, so a test against -1 alone would print entry 255 here;
   the screen staying filled is that not happening. */
static void rds_both_no_line_operands_suppress_the_draw(void)
{
    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_stage_text();
    rds_put(0, 3, RDS_NO_LINE);
    rds_run_on_screen(1);
    if (rds_screen != NULL) {
        CHECK_EQ((int) rds_screen[0], RDS_SCREEN_FILL);
    }
    rds_free_screen();

    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_stage_text();
    rds_put(0, 3, -1);
    rds_run_on_screen(1);
    if (rds_screen != NULL) {
        CHECK_EQ((int) rds_screen[0], RDS_SCREEN_FILL);
    }
    rds_free_screen();
    rds_unstage();
}

/* The suppression is of the LINE only: opcode 4 with a no-line operand still
   records the verdict, and opcode 4 with a real one draws and records both. */
static void rds_a_verdict_records_whether_or_not_it_has_a_line(void)
{
    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_stage_text();
    rds_put(0, 4, 0);
    rds_run_on_screen(1);
    if (rds_screen != NULL) {
        CHECK_EQ((int) rds_screen[0], RDS_TEXT_FG);
    }
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code,
             RDS_END_CLEARED);
    rds_free_screen();

    rds_stage(RDS_SIDE_PLAYER, 0);
    rds_stage_text();
    rds_put(0, 5, RDS_NO_LINE);
    rds_run_on_screen(1);
    if (rds_screen != NULL) {
        CHECK_EQ((int) rds_screen[0], RDS_SCREEN_FILL);
    }
    CHECK_EQ((int) data_fdps_chapter_event_or_battle_end_code, RDS_END_DEFEAT);
    rds_free_screen();
    rds_unstage();
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
    RUN_TEST(rds_a_count_of_zero_does_nothing_at_all);
    RUN_TEST(rds_the_two_verdict_opcodes_set_the_end_code);
    RUN_TEST(rds_the_other_line_opcodes_leave_the_end_code_alone);
    RUN_TEST(rds_the_records_run_front_to_back);
    RUN_TEST(rds_the_count_bounds_the_walk);
    RUN_TEST(rds_the_event_opcode_calls_its_table_slot);
    RUN_TEST(rds_the_run_flushes_the_queue_at_both_ends);
    RUN_TEST(rds_an_item_record_from_a_non_player_actor_ends_the_run);
    RUN_TEST(rds_a_gold_record_from_a_non_player_actor_ends_the_run);
    RUN_TEST(rds_a_retired_player_actor_ends_the_run);
    RUN_TEST(rds_a_non_player_actor_still_runs_the_event_opcodes);
    RUN_TEST(rds_a_line_is_drawn_at_the_screen_origin);
    RUN_TEST(rds_both_no_line_operands_suppress_the_draw);
    RUN_TEST(rds_a_verdict_records_whether_or_not_it_has_a_line);
}
