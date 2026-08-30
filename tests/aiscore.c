/* tests/aiscore.c -- cover for src/aiscore.c.
 *
 * Expected values come from the assembly at 000132b0: the use_effect dispatch
 * CMP dword ptr [EBP-0x8],0xb / 0x1e at 000132e7 and 000133a1, the IDIV EBX
 * with EBX = 3 at 00013348 and the SAR-halving at 00013363 with their two JL
 * at 0001334d and 00013368, the AND AL,0x80 / LEA EAX,[EAX+EAX*2] tripling at
 * 00013380, the MOVSX word ptr [EAX+0x40] at 00013329 against the MOV AX +
 * AND EAX,0xffff at 000133dc, and the CMP / JLE at 000133e5 that decides 8
 * against 0x12.  The loop bound is CMP EAX,[EBP+0x18] / JL at 000132fb, so it
 * is exclusive.  None of them is read off the emitted C.
 *
 * For 00013c20 they come from the CMP EAX,[EBP+0x14] / JL at 00013c3d that
 * bounds the walk before the body runs, the MOV AL / AND EAX,0xff at 00013c52
 * that zero extends the index byte, the ADD EAX,[EBP+0x1c] / CMP byte ptr
 * [EAX],0x0 at 00013c68 that picks the timer byte out of the record and tests
 * it against zero alone, and the ADD dword ptr [EBP-0xc],EAX at 00013c73 that
 * adds score_per_target once per clear target.  The five offsets and the two
 * score values the only caller passes are the PUSH pairs at 00013a82,
 * 00013b04, 00013b26, 00013b3d and 00013b54.
 *
 * Both tables are staged here rather than read from a game file: the function
 * takes its whole input from its three arguments and from the two records the
 * accessors resolve, so pointing the table globals at local blocks is the only
 * way to reach the walks.  Nothing below asserts what either global holds on
 * its own -- ticket 23 owns that.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "aiscore.h"

/* Four item records and six units, so an id or an index other than 0 has
   somewhere to land and a lookup that strayed into the neighbouring record
   would be visible. */
#define STAGE_ITEMS 4
#define STAGE_UNITS 6

/* ITEM.DAT's use_effect codes the scorer weighs: the HP restoratives and the
   line-shaped damage items.  Every other code falls through both branches. */
#define USE_EFFECT_HEAL   0x0b
#define USE_EFFECT_DAMAGE 0x1e

/* The status-effect timer offsets fdps_score_targets_for_spell pushes, with
   the per-target score it pairs each with: 4 for the three 神之祝福 buff slots
   and 0x0a for the two ailments. */
#define STATUS_OFF_BLESS_AP  0x22
#define STATUS_OFF_BLESS_DP  0x23
#define STATUS_OFF_POISON    0x25
#define STATUS_OFF_PARALYSIS 0x26
#define SCORE_BLESS   4
#define SCORE_AILMENT 0x0a

static struct fdps_item_effect stage_items[STAGE_ITEMS];

static struct fdps_unit_record stage_units[STAGE_UNITS];

/* Zero both tables and publish them.  Every item then carries use_effect 0 and
   every unit is a fully healthy nobody, which is the state each case edits
   only the fields it is about. */
static void stage(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage_items;
    for (i = 0; i < (int) sizeof(stage_items); i++) {
        bytes[i] = 0;
    }
    bytes = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_item_effect_table_ptr = (unsigned char *) stage_items;
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
}

static void stage_item(int item_id, int use_effect, int use_amount)
{
    stage_items[item_id].use_effect = (unsigned char) use_effect;
    stage_items[item_id].use_amount = (short) use_amount;
}

static void stage_unit(int unit_index, int hp_current, int hp_max,
                       int ai_behavior)
{
    stage_units[unit_index].hp_current = (short) hp_current;
    stage_units[unit_index].hp_max = (short) hp_max;
    stage_units[unit_index].ai_behavior = (unsigned char) ai_behavior;
}

/* Sets one status-effect timer on a staged unit, addressed the way the scorer
   addresses it -- record base plus a byte offset -- so a case about which byte
   is read is not routed through the field name it is trying to pin down. */
static void stage_status(int unit_index, int status_offset, int turns)
{
    unsigned char *record;

    record = (unsigned char *) &stage_units[unit_index];
    record[status_offset] = (unsigned char) turns;
}

/* Writes the current-HP word as a bit pattern rather than as a number, so a
   case about how that word is widened is not itself relying on how a short
   takes an out-of-range assignment. */
static void stage_hp_bits(int unit_index, unsigned int raw)
{
    unsigned char *word;

    word = (unsigned char *) &stage_units[unit_index].hp_current;
    word[0] = (unsigned char) (raw & 0xff);
    word[1] = (unsigned char) ((raw >> 8) & 0xff);
}

/* One target list every case can point at; the entries each case cares about
   are set before the call. */
static unsigned char targets[4];

/* The record offsets the function addresses as literals -- +0x0d and +0x0e in
   the item record, +0x34, +0x40 and +0x42 in the unit record -- plus the two
   strides the accessors multiply by.  If either record measured anything else
   every lookup past index 0 would read a different item or unit. */
static void record_layout_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_item_effect), 0x17);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_effect), 0x0d);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_amount), 0x0e);
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
}

/* CMP dword ptr [EBP-0x8],0xb / JNZ then CMP ...,0x1e / JNZ jump straight to
   the epilogue, so every other code returns 0 no matter how inviting the
   targets are.  The neighbours of both codes are checked because the dispatch
   is over equalities, not ranges: a > / < spelling would score 0x0c and 0x1f
   as though they were weighed. */
static void unweighed_use_effect_codes_score_zero(void)
{
    stage();
    stage_unit(0, 1, 100, 0);
    stage_unit(1, 1, 100, 0);
    targets[0] = 0;
    targets[1] = 1;

    stage_item(0, 0x00, 300);
    CHECK_EQ(fdps_score_targets_for_item(0, 2, targets), 0);
    stage_item(0, 0x0a, 300);
    CHECK_EQ(fdps_score_targets_for_item(0, 2, targets), 0);
    stage_item(0, 0x0c, 300);
    CHECK_EQ(fdps_score_targets_for_item(0, 2, targets), 0);
    stage_item(0, 0x1d, 300);
    CHECK_EQ(fdps_score_targets_for_item(0, 2, targets), 0);
    stage_item(0, 0x1f, 300);
    CHECK_EQ(fdps_score_targets_for_item(0, 2, targets), 0);
}

/* The loop test runs before the body at 000132f8 and 000133ae, so a count of
   0 enters neither walk and the untouched accumulator at [EBP-0x18] comes
   back.  A do/while spelling would score the first array entry regardless. */
static void empty_target_list_scores_zero(void)
{
    stage();
    stage_item(0, USE_EFFECT_HEAL, 0);
    stage_item(1, USE_EFFECT_DAMAGE, 300);
    stage_unit(0, 1, 100, 0);
    targets[0] = 0;

    CHECK_EQ(fdps_score_targets_for_item(0, 0, targets), 0);
    CHECK_EQ(fdps_score_targets_for_item(1, 0, targets), 0);
}

/* Both heal comparisons are JL against the current HP, so both thresholds are
   strictly less-than: with hp_max 30 the boundaries are 10 and 15, and a unit
   sitting exactly on one takes the better-paying side.  Spelling either as <=
   would move 10 to 3 and 15 to 0. */
static void heal_thresholds_are_strictly_less_than(void)
{
    stage();
    stage_item(0, USE_EFFECT_HEAL, 0);
    targets[0] = 0;

    stage_unit(0, 9, 30, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 8);
    stage_unit(0, 10, 30, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 8);
    stage_unit(0, 11, 30, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 3);
    stage_unit(0, 15, 30, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 3);
    stage_unit(0, 16, 30, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 0);
    stage_unit(0, 30, 30, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 0);
}

/* IDIV and the SAR-plus-sign-correction halving both truncate toward zero, so
   with hp_max 5 the thresholds are 1 and 2 and not 1.67 and 2.5.  A rounding
   or a floating divide would put the 2 case on the other side. */
static void heal_thresholds_truncate_toward_zero(void)
{
    stage();
    stage_item(0, USE_EFFECT_HEAL, 0);
    targets[0] = 0;

    stage_unit(0, 1, 5, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 8);
    stage_unit(0, 2, 5, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 3);
    stage_unit(0, 3, 5, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 0);
}

/* MOVSX word ptr [EAX+0x40] at 00013329 widens the current HP with its sign,
   so a unit whose HP word has gone negative is the most hurt thing on the map
   and scores 8.  Read the same word unsigned -- which is what the damage walk
   does -- and 0xffff would be far above hp_max/2 and score 0. */
static void heal_current_hp_is_signed(void)
{
    stage();
    stage_item(0, USE_EFFECT_HEAL, 0);
    targets[0] = 0;
    stage_unit(0, 0, 30, 0);
    stage_hp_bits(0, 0xffff);

    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 8);
}

/* AND AL,0x80 / TEST / LEA EAX,[EAX+EAX*2] triples the per-target score, and
   it tests that one bit alone: a behaviour byte of 0x7f leaves the score
   as it was, and 0 triples to 0. */
static void behavior_bit_triples_the_heal_score(void)
{
    stage();
    stage_item(0, USE_EFFECT_HEAL, 0);
    targets[0] = 0;

    stage_unit(0, 1, 30, 0x80);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 24);
    stage_unit(0, 11, 30, 0x80);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 9);
    stage_unit(0, 30, 30, 0x80);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 0);
    stage_unit(0, 1, 30, 0x7f);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 8);
    stage_unit(0, 1, 30, 0xff);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 24);
}

/* use_amount is fetched at 000132d5 but the restorative walk never reads it,
   so the amount an item would actually restore changes nothing: 藥草's small
   heal and 神聖之水's large one score the same on the same target.  The two
   items are staged at ids 1 and 2 so the fetch also has to pick the right
   record out of the table. */
static void heal_ignores_use_amount(void)
{
    stage();
    stage_item(1, USE_EFFECT_HEAL, 30);
    stage_item(2, USE_EFFECT_HEAL, 9999);
    stage_unit(0, 5, 30, 0);
    targets[0] = 0;

    CHECK_EQ(fdps_score_targets_for_item(1, 1, targets), 8);
    CHECK_EQ(fdps_score_targets_for_item(2, 1, targets), 8);
}

/* The accumulator at [EBP-0x18] takes every target's score, the walk reads the
   index list as bytes in the order given, and the bound is exclusive: with
   target_count 2 the third entry is not visited even though it would score 8.
   Indices 3 and 1 are used so a walk that ignored the list and counted units
   from 0 would land on different records. */
static void heal_sums_over_the_index_list(void)
{
    stage();
    stage_item(0, USE_EFFECT_HEAL, 0);
    stage_unit(1, 20, 30, 0);   /* above half: 0 */
    stage_unit(3, 5, 30, 0);    /* at or below a third: 8 */
    stage_unit(4, 12, 30, 0);   /* at or below half: 3 */
    stage_unit(5, 1, 30, 0);    /* would add 8 if the bound were inclusive */
    targets[0] = 3;
    targets[1] = 1;
    targets[2] = 5;

    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 8);
    CHECK_EQ(fdps_score_targets_for_item(0, 2, targets), 8);
    targets[1] = 4;
    CHECK_EQ(fdps_score_targets_for_item(0, 2, targets), 11);
}

/* CMP EAX,[EBP-0x24] / JLE at 000133e5: current HP at or below use_amount is
   the kill and scores 0x12, above it is the wound and scores 8.  The boundary
   is the JLE, so exactly use_amount is a kill; a JL spelling would call it a
   wound.  火焰's use_amount of 300 is the one staged. */
static void damage_scores_the_kill_above_the_wound(void)
{
    stage();
    stage_item(0, USE_EFFECT_DAMAGE, 300);
    targets[0] = 0;

    stage_unit(0, 301, 1000, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 8);
    stage_unit(0, 300, 1000, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 0x12);
    stage_unit(0, 299, 1000, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 0x12);
    stage_unit(0, 0, 1000, 0);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 0x12);
}

/* MOV AX,word ptr [EAX+0x40] / AND EAX,0xffff at 000133dc widens the current
   HP without its sign while use_amount stays sign-extended, so an HP word of
   0x8000 compares as 32768 and is above use_amount: the wound score, 8.  Read
   as the signed short the layout declares it would be -32768, at or below
   use_amount, and the same target would score 0x12 instead. */
static void damage_current_hp_is_unsigned(void)
{
    stage();
    stage_item(0, USE_EFFECT_DAMAGE, 300);
    targets[0] = 0;
    stage_unit(0, 0, 1000, 0);

    stage_hp_bits(0, 0x8000);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 8);
    stage_hp_bits(0, 0xffff);
    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 8);
}

/* The damage walk has no behaviour test at all -- there is no AND 0x80
   anywhere between 000133c0 and 00013400 -- so the bit that triples a heal
   leaves a damage score alone.  The sum over two targets is asserted in the
   same case: one kill and one wound is 0x12 + 8. */
static void damage_ignores_the_behavior_bit_and_sums(void)
{
    stage();
    stage_item(0, USE_EFFECT_DAMAGE, 300);
    stage_unit(0, 100, 1000, 0x80);
    stage_unit(1, 900, 1000, 0x80);
    targets[0] = 0;
    targets[1] = 1;

    CHECK_EQ(fdps_score_targets_for_item(0, 1, targets), 0x12);
    CHECK_EQ(fdps_score_targets_for_item(0, 2, targets), 0x12 + 8);
}

/* A unit array long enough to hold an index with the top bit of its byte set,
   so the case about how that byte is widened has a real record to land on
   instead of reading in front of the array. */
#define WIDE_UNITS 0x82

static struct fdps_unit_record wide_units[WIDE_UNITS];

static void stage_wide(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) wide_units;
    for (i = 0; i < (int) sizeof(wide_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) wide_units;
}

/* The scorer indexes the record by the literal offsets its caller pushes --
   0x22, 0x23, 0x24, 0x25 and 0x26 -- so all five have to fall inside
   status_timers, which starts at 0x22 and runs six bytes.  If the field moved,
   every one of those calls would read some other unit field instead. */
static void status_timers_sit_where_the_caller_indexes(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, gap_028), 0x28);
}

/* CMP EAX,[EBP+0x14] / JL at 00013c3d is a signed test placed before the body,
   so 0 and a negative count both return the accumulator untouched and neither
   reads target_ids.  An unsigned compare would make -1 a huge count and walk
   the array; a do/while spelling would score the first entry either way.  The
   third check shows that entry would have scored had the walk run. */
static void nonpositive_target_count_scores_zero(void)
{
    stage();
    targets[0] = 0;

    CHECK_EQ(fdps_score_targets_without_status(0, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), 0);
    CHECK_EQ(fdps_score_targets_without_status(-1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), 0);
    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), SCORE_AILMENT);
}

/* CMP byte ptr [EAX],0x0 / JNZ at 00013c6b tests the whole byte against zero,
   so the timer's remaining turn count is not compared with anything and any
   nonzero value suppresses the score alike -- 1, 2 and 0xff included.  A test
   of a single bit, or one of "more than one turn left", would let some of
   these through. */
static void a_running_timer_scores_nothing(void)
{
    stage();
    targets[0] = 0;

    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), SCORE_AILMENT);
    stage_status(0, STATUS_OFF_POISON, 1);
    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), 0);
    stage_status(0, STATUS_OFF_POISON, 2);
    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), 0);
    stage_status(0, STATUS_OFF_POISON, 0xff);
    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), 0);
    stage_status(0, STATUS_OFF_POISON, 0);
    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), SCORE_AILMENT);
}

/* ADD EAX,[EBP+0x1c] at 00013c68 adds the argument to the record base, so the
   offset selects which one of the six timers is consulted and the neighbouring
   five are not: a unit carrying the attack buff is still a full-value target
   for the defence buff, for poison and for paralysis.  A scorer that tested a
   fixed field, or that OR-ed the timers together, would score 0 for all of
   them. */
static void status_offset_picks_one_timer(void)
{
    stage();
    targets[0] = 0;
    stage_status(0, STATUS_OFF_BLESS_AP, 3);

    CHECK_EQ(fdps_score_targets_without_status(1, targets,
                                               STATUS_OFF_BLESS_AP,
                                               SCORE_BLESS), 0);
    CHECK_EQ(fdps_score_targets_without_status(1, targets,
                                               STATUS_OFF_BLESS_DP,
                                               SCORE_BLESS), SCORE_BLESS);
    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), SCORE_AILMENT);
    stage_status(0, STATUS_OFF_PARALYSIS, 1);
    CHECK_EQ(fdps_score_targets_without_status(1, targets,
                                               STATUS_OFF_PARALYSIS,
                                               SCORE_AILMENT), 0);
    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), SCORE_AILMENT);
}

/* ADD dword ptr [EBP-0xc],EAX at 00013c73 adds the argument once per clear
   target, so the answer is score_per_target times the number of clear targets
   and nothing else scales it: three clear targets are 30 at the ailment rate
   and 12 at the blessing rate, a score of 0 stays 0 however many targets there
   are, and one target that already carries the effect drops exactly its own
   share. */
static void score_per_target_multiplies_the_clear_targets(void)
{
    stage();
    targets[0] = 0;
    targets[1] = 1;
    targets[2] = 2;

    CHECK_EQ(fdps_score_targets_without_status(3, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), 30);
    CHECK_EQ(fdps_score_targets_without_status(3, targets, STATUS_OFF_POISON,
                                               SCORE_BLESS), 12);
    CHECK_EQ(fdps_score_targets_without_status(3, targets, STATUS_OFF_POISON,
                                               0), 0);
    stage_status(1, STATUS_OFF_POISON, 1);
    CHECK_EQ(fdps_score_targets_without_status(3, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), 20);
}

/* MOV EAX,[EBP+0x18] / ADD EAX,[EBP-0x10] / MOV AL,byte ptr [EAX] at 00013c4c
   reads the list one byte at a time in the order given, and the bound is
   exclusive.  Unit 0 is made to carry the effect while the list starts at unit
   3, so a walk that ignored the list and counted units from zero would score 0
   where this one scores 10; entries past target_count are not visited even
   though unit 5 would pay. */
static void the_walk_follows_the_index_list(void)
{
    stage();
    targets[0] = 3;
    targets[1] = 1;
    targets[2] = 5;
    stage_status(0, STATUS_OFF_POISON, 1);
    stage_status(1, STATUS_OFF_POISON, 1);

    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), SCORE_AILMENT);
    CHECK_EQ(fdps_score_targets_without_status(2, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), SCORE_AILMENT);
    CHECK_EQ(fdps_score_targets_without_status(3, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT),
             SCORE_AILMENT * 2);
}

/* MOV AL,byte ptr [EAX] / AND EAX,0xff at 00013c52 widens the index byte
   without a sign, so 0x81 is unit 129.  Unit 0 is staged carrying the effect
   so a truncated or sign-extended index cannot score by accident, and unit 129
   is then given the effect to prove the record actually being read is that
   one. */
static void the_index_byte_is_zero_extended(void)
{
    unsigned char *record;

    stage_wide();
    targets[0] = 0x81;
    ((unsigned char *) &wide_units[0])[STATUS_OFF_POISON] = 1;

    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), SCORE_AILMENT);
    record = (unsigned char *) &wide_units[0x81];
    record[STATUS_OFF_POISON] = 1;
    CHECK_EQ(fdps_score_targets_without_status(1, targets, STATUS_OFF_POISON,
                                               SCORE_AILMENT), 0);
}

void run_aiscore_tests(void)
{
    RUN_TEST(record_layout_matches_the_offsets);
    RUN_TEST(unweighed_use_effect_codes_score_zero);
    RUN_TEST(empty_target_list_scores_zero);
    RUN_TEST(heal_thresholds_are_strictly_less_than);
    RUN_TEST(heal_thresholds_truncate_toward_zero);
    RUN_TEST(heal_current_hp_is_signed);
    RUN_TEST(behavior_bit_triples_the_heal_score);
    RUN_TEST(heal_ignores_use_amount);
    RUN_TEST(heal_sums_over_the_index_list);
    RUN_TEST(damage_scores_the_kill_above_the_wound);
    RUN_TEST(damage_current_hp_is_unsigned);
    RUN_TEST(damage_ignores_the_behavior_bit_and_sums);
    RUN_TEST(status_timers_sit_where_the_caller_indexes);
    RUN_TEST(nonpositive_target_count_scores_zero);
    RUN_TEST(a_running_timer_scores_nothing);
    RUN_TEST(status_offset_picks_one_timer);
    RUN_TEST(score_per_target_multiplies_the_clear_targets);
    RUN_TEST(the_walk_follows_the_index_list);
    RUN_TEST(the_index_byte_is_zero_extended);
}
