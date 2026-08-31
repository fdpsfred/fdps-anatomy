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

/* ------------------------------------------------------------------
 * 00012230 fdps_map_actor_score_best_attack
 *
 * The search is driven end to end rather than in pieces: every one of its
 * callees is already emitted, so the only way to reach the ranking is to build
 * a battle for it.  Seven blocks have to be live -- the unit array, the item
 * table, the class table, the movement grid, the layer-0 tile map, that
 * layer's attribute table and the cell event-code layer -- because
 * fdps_move_grid_flood_fill_range reaches fdps_map_load_tile_info for every
 * neighbour it considers.  All seven are staged here rather than read from a
 * game file: the function takes its whole input from those globals and its two
 * arguments.  Nothing below asserts what any global holds on its own; ticket
 * 23 owns that.
 *
 * The map is 4x4 with every tile at terrain type 0, so a class movement cost
 * is one write and a step costs whatever that class's move_cost[0] says.  The
 * default class row 0 costs 1, row 1 costs 9 and row 2 costs 1, which is what
 * lets a case tell apart the row the search asks for from the row next to it.
 *
 * Expected values come from the assembly at 00012230 -- INC EAX before the
 * fdps_get_class_record push at 00012300, CMP dword ptr [EBP-0x30],0x2 / JLE
 * at 00012462, MOVSX word ptr [EAX+0x40] / CMP / JGE at 00012478, SHL dword
 * ptr [EBP-0x30],0x1 at 00012484, CMP EAX,0x1 / JNZ at 000124a2, CMP byte ptr
 * [EAX+0x8],0x0 at 000124b3, LEA EDX,[EDX+EDX*0x2] with the SAR 0x1f / SUB /
 * SAR 0x1 halving at 000124bc, and the JG / JNZ / JG ranking at 000124ce --
 * and by walking that algorithm over the fixture by hand.  None of them is
 * read off the emitted C.
 *
 * Every case leaves the score global at 0x12 and the other three at a sentinel
 * before the call, so a case that expects a tier-8 win also proves the entry
 * zeroing at 0001226d: without it 8 would not beat the 0x12 already standing.
 * ------------------------------------------------------------------ */

#define ATK_W 4
#define ATK_H 4
#define ATK_CELLS 32
#define ATK_TILES_AT 0x0b
#define ATK_ATTR_AT 0x11
#define ATK_ATTR_ROWS 32
#define ATK_EVENT_AT 0x10
#define ATK_UNITS 6
#define ATK_ITEMS 4
#define ATK_CLASSES 4
#define ATK_CLASS_STRIDE 0x0a

/* What the three unpublished globals hold going in.  Any value the search
   could not produce would do; this one is small enough to compare as an int
   and unlike every tile coordinate and unit index the fixture uses. */
#define ATK_SENTINEL 0x5a

/* The equipped bit of an inventory entry's flag byte, the same one
   fdps_unit_find_equipped_slot tests. */
#define ATK_EQUIPPED 0x40

/* An ITEM.DAT type inside the weapon span 1..0x15, so the equipped-slot search
   accepts the entry as a weapon. */
#define ATK_WEAPON_TYPE 1

static unsigned char atk_tilemap[ATK_TILES_AT + ATK_CELLS * 2];
static unsigned char atk_attr[ATK_ATTR_AT + ATK_ATTR_ROWS * 4];
static unsigned char atk_event[ATK_EVENT_AT + ATK_CELLS];
static unsigned char atk_grid[4 + ATK_CELLS * 2];
static unsigned char atk_classes[ATK_CLASSES * ATK_CLASS_STRIDE];
static struct fdps_unit_record atk_units[ATK_UNITS];
static struct fdps_item_effect atk_items[ATK_ITEMS];

static void atk_zero(unsigned char *block, int bytes)
{
    int i;

    for (i = 0; i < bytes; i++) {
        block[i] = 0;
    }
}

/* Give class row record_index the same movement cost on all eight terrain
   types, so a step costs that whatever tile it enters. */
static void atk_class(int record_index, int cost)
{
    int i;

    for (i = 0; i < 8; i++) {
        atk_classes[record_index * ATK_CLASS_STRIDE + i] =
            (unsigned char) cost;
    }
}

static void atk_item(int item_id, int type, int range_min, int range_max)
{
    atk_items[item_id].type = (unsigned char) type;
    atk_items[item_id].range_min = (unsigned char) range_min;
    atk_items[item_id].range_max = (unsigned char) range_max;
}

static void atk_unit(int index, int x, int y, int side, int ap, int dp,
                     int hp, int char_id)
{
    atk_units[index].pos_x = (unsigned char) x;
    atk_units[index].pos_y = (unsigned char) y;
    atk_units[index].side = (unsigned char) side;
    atk_units[index].ap = (short) ap;
    atk_units[index].dp = (short) dp;
    atk_units[index].hp_current = (short) hp;
    atk_units[index].char_id = (unsigned char) char_id;
}

/* Put a weapon in the unit's first inventory entry and equip it, which is what
   both fdps_unit_find_equipped_slot calls in this search look for -- the one
   on the actor and the one inside the counter-attack test. */
static void atk_equip(int unit_index, int item_id)
{
    atk_units[unit_index].inventory_slots[0] = ATK_EQUIPPED;
    atk_units[unit_index].inventory_slots[1] = (unsigned char) item_id;
}

/* Build a 4x4 battle: an empty map at terrain 0, a grid in the state
   fdps_map_grid_reset leaves it (flags clear, marker 0xff), item 1 a reach-1
   weapon, class rows 0 and 2 cheap with the expensive row 1 between them, and
   no units yet.  Cells past width*height carry 0xdd so a walk that ran long
   would be visible. */
static void atk_stage(void)
{
    int i;

    atk_zero(atk_tilemap, (int) sizeof(atk_tilemap));
    for (i = 0; i < ATK_TILES_AT; i++) {
        atk_tilemap[i] = 0xaa;
    }
    *(short *) (atk_tilemap + 7) = (short) ATK_W;
    for (i = 0; i < ATK_CELLS; i++) {
        *(short *) (atk_tilemap + ATK_TILES_AT + i * 2) = (short) i;
    }

    atk_zero(atk_attr, (int) sizeof(atk_attr));
    for (i = 0; i < ATK_ATTR_AT; i++) {
        atk_attr[i] = 0xaa;
    }

    atk_zero(atk_event, (int) sizeof(atk_event));
    for (i = 0; i < ATK_EVENT_AT; i++) {
        atk_event[i] = 0xaa;
    }
    *(short *) (atk_event + 7) = (short) ATK_W;

    atk_zero(atk_grid, (int) sizeof(atk_grid));
    *(short *) atk_grid = (short) ATK_W;
    *(short *) (atk_grid + 2) = (short) ATK_H;
    for (i = 0; i < ATK_CELLS; i++) {
        atk_grid[4 + i * 2] = 0x00;
        if (i < ATK_W * ATK_H) {
            atk_grid[4 + i * 2 + 1] = 0xff;
        } else {
            atk_grid[4 + i * 2 + 1] = 0xdd;
        }
    }

    atk_zero((unsigned char *) atk_units, (int) sizeof(atk_units));
    atk_zero((unsigned char *) atk_items, (int) sizeof(atk_items));
    atk_zero(atk_classes, (int) sizeof(atk_classes));
    atk_item(1, ATK_WEAPON_TYPE, 1, 1);
    atk_class(0, 1);
    atk_class(1, 9);
    atk_class(2, 1);
    atk_class(3, 1);

    data_fdps_scene_layer_tile_map_ptrs[0] = atk_tilemap;
    data_fdps_scene_layer_tile_attr_ptr[0] = atk_attr;
    data_fdps_battle_move_grid_ptr = atk_grid;
    data_fdps_map_cell_event_code_layer_ptr = atk_event;
    data_fdps_map_unit_array_ptr = (unsigned char *) atk_units;
    data_fdps_item_effect_table_ptr = (unsigned char *) atk_items;
    data_fdps_class_table_ptr = atk_classes;
    data_fdps_map_unit_count = 0;

    data_fdps_battle_ai_best_physical_score = 0x12;
    data_fdps_battle_ai_best_physical_target_idx = ATK_SENTINEL;
    data_fdps_battle_ai_best_physical_target_x = ATK_SENTINEL;
    data_fdps_battle_ai_best_attack_tile_y = ATK_SENTINEL;
}

/* Stage the actor at (0, 0) on side 0 with attack 10, defence 5, class 1 and
   the reach-1 weapon equipped, and no movement at all, so the candidate tile
   set is its own tile alone and every case that is about the ranking has one
   tile to think about.  Class 1 selects class row 2, which is cheap. */
static void atk_stage_still_actor(void)
{
    atk_stage();
    atk_unit(0, 0, 0, 0, 10, 5, 100, 5);
    atk_units[0].clazz = 1;
    atk_units[0].move = 0;
    atk_equip(0, 1);
}

/* The literal record offsets the search reads: attack and defence as signed
   words at +0x48 and +0x4a, current HP at +0x40, the character index tested as
   a byte at +0x08, the movement allowance at +0x3b, the class byte at +0x20
   and the tile at +0x00 and +0x01, plus the weapon's two reach bytes at +0x0b
   and +0x0c.  If any of them moved, the search would read a different field
   and every expected value below would be a coincidence. */
static void attack_search_reads_these_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 0x01);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, char_id), 0x08);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, clazz), 0x20);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, move), 0x3b);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap), 0x48);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, dp), 0x4a);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, range_min), 0x0b);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, range_max), 0x0c);
    CHECK_EQ((int) sizeof(struct fdps_class_record), ATK_CLASS_STRIDE);
}

/* CMP dword ptr [EBP-0x54],-0x1 / JNZ at 00012288 leaves by the early exit, so
   an actor with nothing equipped answers 0 with the score global freshly
   zeroed and the tile and target globals untouched -- the previous actor's
   decision is still standing in them, which is why the caller has to look at
   the score first.  A target is put in reach so the case fails if the search
   ran at all. */
static void no_equipped_weapon_returns_at_once(void)
{
    atk_stage_still_actor();
    atk_units[0].inventory_slots[0] = 0;
    atk_units[0].inventory_slots[1] = 0;
    atk_unit(1, 1, 0, 1, 0, 3, 100, 5);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, ATK_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_x, ATK_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_attack_tile_y, ATK_SENTINEL);
}

/* A weapon that reaches nobody runs the whole search and publishes nothing:
   the score comes back 0 because of the entry store and the other three keep
   their sentinels, because the only writes to them are inside the winning
   branch.  The target is parked at (3, 3), three steps and more away from the
   actor's single candidate tile. */
static void a_search_that_finds_nobody_publishes_nothing(void)
{
    atk_stage_still_actor();
    atk_unit(1, 3, 3, 1, 0, 3, 100, 5);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, ATK_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_x, ATK_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_attack_tile_y, ATK_SENTINEL);
}

/* One target adjacent to the actor's own tile: attack 10 against defence 3 is
   an estimate of 7, above the wound threshold and below the target's 100 HP,
   so the tier is 8.  All four globals are written together at
   000124f1-0001250c -- the tile x from byte 0 of the candidate pair, the tile
   y from byte 1, the unit index and the tier -- and the 0x12 standing in the
   score before the call is gone, which is the entry zeroing. */
static void a_reachable_target_publishes_tile_index_and_tier(void)
{
    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 3, 100, 5);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_x, 0);
    CHECK_EQ(data_fdps_battle_ai_best_attack_tile_y, 0);
}

/* CMP dword ptr [EBP-0x30],0x2 / JLE: the wound tier needs an estimate
   strictly above 2, so defence 7 against attack 10 is an estimate of 3 and
   scores 8 while defence 8 is an estimate of 2 and scores 0.  The tier-0 pair
   is still published, because the ranking's tie-break sees 0 == 0 and an
   estimate of 2 above the running best of 0.  Defence 10 makes that estimate 0,
   which does not beat the running 0, and nothing is published at all -- the
   tie-break is CMP / JG and so strict in both places. */
static void wound_tier_needs_an_estimate_above_two(void)
{
    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 7, 100, 5);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);

    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 8, 100, 5);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);

    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 10, 100, 5);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, ATK_SENTINEL);
}

/* MOVSX word ptr [EAX+0x40] / CMP / JGE: the lethal test is target HP strictly
   below the estimate, so a blow that exactly matches the remaining HP is a
   wound.  With attack 10 against defence 3 the estimate is 7: at 7 HP the tier
   is 8 and at 6 HP it is 0x12.  Writing the test as <= would make the first
   case a kill. */
static void lethal_test_is_strictly_less_than(void)
{
    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 3, 7, 5);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);

    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 3, 6, 5);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0x12);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);
}

/* INC EAX between the class byte and the fdps_get_class_record push: the row
   the movement flood spends is the class byte PLUS ONE.  Class 1 therefore
   spends row 2, which costs 1 a step, and two movement points carry the actor
   to (1, 0) where a reach-1 weapon touches the target waiting at (2, 0) -- so
   the published tile is (1, 0) and not the actor's own.  Class 0 spends row 1,
   which costs 9 a step, and the same actor cannot leave its tile at all: the
   target is two tiles away and nothing is published.  Without the +1 that
   second case would spend row 0, which costs 1, and would score. */
static void class_row_is_the_class_byte_plus_one(void)
{
    atk_stage();
    atk_unit(0, 0, 0, 0, 10, 5, 100, 5);
    atk_units[0].clazz = 1;
    atk_units[0].move = 2;
    atk_equip(0, 1);
    atk_unit(1, 2, 0, 1, 0, 3, 100, 5);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_x, 1);
    CHECK_EQ(data_fdps_battle_ai_best_attack_tile_y, 0);

    atk_stage();
    atk_unit(0, 0, 0, 0, 10, 5, 100, 5);
    atk_units[0].clazz = 0;
    atk_units[0].move = 2;
    atk_equip(0, 1);
    atk_unit(1, 2, 0, 1, 0, 3, 100, 5);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, ATK_SENTINEL);
}

/* CMP EAX,0x1 / JNZ: the counter-attack penalty is applied only when
   fdps_check_can_counter_attack_from_tile answers exactly 1, and it answers -1
   on every refusal, so a bare predicate would apply it to every target.

   Both targets have defence 3 and so an estimate of 7 and a tier of 8.  The
   one at (1, 0) carries no weapon and cannot strike back, so it keeps its 7;
   the one at (0, 1) is armed and would, so it takes the actor's defence 5 less
   its own attack 10 and drops to 2.  The first is found first and the tie-break
   is strict, so the winner is unit 1.  Treat -1 as "yes" and unit 1 would take
   5 less its attack of 50 and fall to -38, handing the win to unit 2; add the
   penalty the other way round -- target attack less actor defence -- and unit 2
   would rise to 12 and win too. */
static void counter_attack_penalty_needs_exactly_one(void)
{
    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 50, 3, 100, 5);
    atk_unit(2, 0, 1, 1, 10, 3, 100, 5);
    atk_equip(2, 1);
    data_fdps_map_unit_count = 3;

    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);
}

/* CMP byte ptr [EAX+0x8],0x0 then LEA EDX,[EDX+EDX*0x2] and the signed halving
   at 000124bc: a target whose character index is 0 -- the protagonist 蘭迪斯 --
   has its estimate multiplied by three and halved toward zero.

   Unit 1 is an ordinary target with defence 3 and an estimate of 7.  Unit 2 is
   the protagonist: with defence 4 its estimate of 6 becomes 9 and takes the
   win, where without the bonus 6 would have lost to 7.  With defence 5 its
   estimate of 5 becomes 7 and not 8, which does not beat the standing 7, so
   the win stays with unit 1 -- a divide that rounded up would move it. */
static void protagonist_estimate_is_tripled_and_halved(void)
{
    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 3, 100, 5);
    atk_unit(2, 0, 1, 1, 0, 4, 100, 0);
    data_fdps_map_unit_count = 3;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 2);

    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 3, 100, 5);
    atk_unit(2, 0, 1, 1, 0, 5, 100, 0);
    data_fdps_map_unit_count = 3;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);
}

/* SHL dword ptr [EBP-0x30],0x1 at 00012484: a lethal blow's estimate is
   doubled, and doubling only ever shows through the ranking when something
   additive lands on top of it.

   Both targets die.  Unit 1's estimate is 10 less its defence 6, that is 4,
   doubled to 8, and then it strikes back for the actor's defence 5 less its own
   attack 4, so 9.  Unit 2's is 10 less 5, that is 5, doubled to 10, with no
   counter-attack.  Unit 2 wins on 10 against 9.  Drop the doubling and the two
   are 5 and 5, the tie-break is strict, and unit 1 -- found first -- would keep
   the win instead. */
static void a_lethal_estimate_is_doubled(void)
{
    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 4, 6, 3, 5);
    atk_equip(1, 1);
    atk_unit(2, 0, 1, 1, 0, 5, 4, 5);
    data_fdps_map_unit_count = 3;

    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0x12);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 2);
}

/* MOV EAX,[EBP-0x24] / CMP EAX,[0x00063f78] / JG comes first and the estimate
   is only consulted on a tie, so a kill always outranks a wound however much
   larger the wound's estimate is.

   Unit 1 is unarmoured -- an estimate of 10 -- but survives, so it is tier 8.
   Unit 2 has defence 6 and 3 HP, an estimate of 4 doubled to 8, and is tier
   0x12.  Whichever of the two is met first, the kill is what ends up
   published; in the second arrangement the wound's estimate of 10 is the
   larger and still does not displace it. */
static void a_higher_tier_beats_a_bigger_estimate(void)
{
    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 0, 100, 5);
    atk_unit(2, 0, 1, 1, 0, 6, 3, 5);
    data_fdps_map_unit_count = 3;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0x12);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 2);

    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 6, 3, 5);
    atk_unit(2, 0, 1, 1, 0, 0, 100, 5);
    data_fdps_map_unit_count = 3;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0x12);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);
}

/* The two reach bytes are pushed in the order range_max then range_min at
   000123e3 and 000123df, so +0x0c is the reach fdps_collect_targets_in_range
   floods and +0x0b is the minimum distance it then sweeps back out.  A weapon
   of reach 2 to 3 therefore cannot hit the neighbour at (1, 0), however
   inviting its estimate of 10, and hits the one at (2, 0) for an estimate of 4
   instead.  Read the two bytes the other way round and the sweep would clear
   everything the flood of 2 had reached, leaving nothing published at all;
   ignore the minimum and the neighbour would win. */
static void the_weapon_reach_comes_from_its_two_bytes(void)
{
    atk_stage_still_actor();
    atk_item(2, ATK_WEAPON_TYPE, 2, 3);
    atk_equip(0, 2);
    atk_unit(1, 1, 0, 1, 0, 0, 100, 5);
    atk_unit(2, 2, 0, 1, 0, 6, 100, 5);
    data_fdps_map_unit_count = 3;

    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 2);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_x, 0);
    CHECK_EQ(data_fdps_battle_ai_best_attack_tile_y, 0);
}

/* The three stat words are all reached with MOVSX -- the actor's at 0001225c
   and 00012266, the target's at 00012448 and 00012452, its current HP at
   0001247b -- so every one of them is signed and a negative word is a small
   number and not a huge one.

   An actor whose attack word is -1 estimates -1 against an undefended target:
   tier 0, and an estimate that does not beat the running best of 0, so nothing
   is published.  Read the word unsigned and it would be 65535, a wound tier of
   8.  A target whose defence word is -1 lifts an attack of 0 to an estimate of
   1, which does beat 0 and is published at tier 0; read unsigned it would sink
   to -65535 and publish nothing.  A target whose HP word is -1 is below the
   estimate of 7 and dies; read unsigned it would be 65535 and merely be
   wounded. */
static void the_stat_words_are_read_signed(void)
{
    atk_stage_still_actor();
    atk_units[0].ap = (short) -1;
    atk_unit(1, 1, 0, 1, 0, 0, 100, 5);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, ATK_SENTINEL);

    atk_stage_still_actor();
    atk_units[0].ap = 0;
    atk_unit(1, 1, 0, 1, 0, -1, 100, 5);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);

    atk_stage_still_actor();
    atk_unit(1, 1, 0, 1, 0, 3, -1, 5);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_attack(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_physical_score, 0x12);
    CHECK_EQ(data_fdps_battle_ai_best_physical_target_idx, 1);
}

/* ------------------------------------------------------------------
 * 00013920 fdps_score_targets_for_spell
 *
 * The spell table and the unit array are both staged here: the function takes
 * its whole input from its three arguments and from the two records the
 * accessors resolve, so pointing those two globals at local blocks is the only
 * way to reach the branches.  Nothing below asserts what either global holds on
 * its own -- ticket 23 owns that.
 *
 * Expected values come from the assembly at 00013920: the CMP 0xe / JL, CMP
 * 0x10 / JLE and CMP 0x21 / JNZ range test at 0001394b and the five equality
 * tests after it, the XOR EAX,EAX / MOV AX widening at 00013996 and 000139a4
 * against the MOVSX at 00013bce, the IDIV EBX with EBX = 3 at 000139bc and the
 * SAR 0x1f / SUB / SAR 0x1 halving at 000139cf with their two JLE at 000139c1
 * and 000139dc, the AND AL,0x1 / SHL 0x1 at 000139f4, the PUSH 0x0 /
 * fdps_unit_collect_known_spells / TEST / CMP byte ptr [EAX+0x27] pair at
 * 00013a4a, the three-way ailment test at 00013ad9, the five PUSH pairs at
 * 00013a82, 00013b04, 00013b26, 00013b3d and 00013b54 with the MOV -- not ADD
 * -- that lands each answer, the CMP 0xb / JZ, CMP 0xa / JNZ flying guard at
 * 00013b8c, the CMP / JGE kill test at 00013bd2, and the FILD / FMUL double ptr
 * [0x00061571] / __CHP / FISTP at 00013bf0 over the 1.5 whose eight bytes read
 * 00 00 00 00 00 00 f8 3f.  None of them is read off the emitted C.
 * ------------------------------------------------------------------ */

/* MAGICDAT.DAT holds 40 records, ids 0x00-0x27 with no gap (assets/spells.md),
   so the table is staged full length and the highest id the dispatch names --
   0x21 鎮魂之歌 -- has a record of its own to be fetched from. */
#define SPL_SPELLS 0x28
#define SPL_UNITS 8

/* A character index that is not the protagonist's, so the 1.5 weighting cannot
   reach a case that is not about it. */
#define SPL_NOBODY 5

/* status_timers[] slots, as indices rather than as the record offsets the
   delegating branches push: [0] [1] [2] are 神之祝福's attack, defence and
   dexterity blessings, [3] poison, [4] paralysis, [5] the magic seal. */
#define SPL_SLOT_BLESS_AP  0
#define SPL_SLOT_BLESS_DP  1
#define SPL_SLOT_BLESS_DX  2
#define SPL_SLOT_POISON    3
#define SPL_SLOT_PARALYSIS 4
#define SPL_SLOT_SEAL      5

/* The ids the dispatch names, and two that it does not: 0x0d sits one below the
   healing span and 0x0c is an ordinary damaging spell. */
#define SPL_ID_QUAKE         0x0a
#define SPL_ID_GREAT_QUAKE   0x0b
#define SPL_ID_PLAIN_DAMAGE  0x0c
#define SPL_ID_BELOW_HEAL    0x0d
#define SPL_ID_HEAL_FIRST    0x0e
#define SPL_ID_HEAL_MID      0x0f
#define SPL_ID_HEAL_LAST     0x10
#define SPL_ID_SEAL_MAGIC    0x11
#define SPL_ID_POISON        0x12
#define SPL_ID_PARALYSE      0x13
#define SPL_ID_BLESSING      0x14
#define SPL_ID_CURE          0x18
#define SPL_ID_REQUIEM       0x21

/* Class codes fdps_unit_is_flying answers 1 and 0 for: 0x1f 飛兵 is in its
   five-code list and 0x19 機兵 is deliberately not. */
#define SPL_CLASS_FLYING     0x1f
#define SPL_CLASS_NOT_FLYING 0x19

static struct fdps_spell_effect spl_spells[SPL_SPELLS];
static struct fdps_unit_record spl_units[SPL_UNITS];
static unsigned char spl_targets[4];

/* Zero both tables, publish them, and give every unit a character index that is
   not the protagonist's so the weighting has to be asked for.  Every spell then
   has power 0 and every unit is an unhurt nobody who knows no spells and
   carries no timer. */
static void spl_stage(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) spl_spells;
    for (i = 0; i < (int) sizeof(spl_spells); i++) {
        bytes[i] = 0;
    }
    bytes = (unsigned char *) spl_units;
    for (i = 0; i < (int) sizeof(spl_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < SPL_UNITS; i++) {
        spl_units[i].char_id = SPL_NOBODY;
    }
    for (i = 0; i < 4; i++) {
        spl_targets[i] = (unsigned char) i;
    }
    data_fdps_battle_spell_effect_table_ptr = (unsigned char *) spl_spells;
    data_fdps_map_unit_array_ptr = (unsigned char *) spl_units;
}

static void spl_power(int spell_id, int power)
{
    spl_spells[spell_id].power = (short) power;
}

static void spl_unit(int unit_index, int hp_current, int hp_max)
{
    spl_units[unit_index].hp_current = (short) hp_current;
    spl_units[unit_index].hp_max = (short) hp_max;
}

static void spl_timer(int unit_index, int slot, int turns)
{
    spl_units[unit_index].status_timers[slot] = (unsigned char) turns;
}

/* Set one bit of the known-spell bitmap, which is what
   fdps_unit_collect_known_spells counts: byte spell_id / 8, bit spell_id % 8. */
static void spl_knows(int unit_index, int spell_id)
{
    spl_units[unit_index].spells_known_bitmap[spell_id / 8] |=
        (unsigned char) (1 << (spell_id % 8));
}

/* Write an HP word as a bit pattern, so a case about how that word is widened
   does not itself rest on how a short takes an out-of-range assignment. */
static void spl_hp_bits(int unit_index, unsigned int raw)
{
    unsigned char *word;

    word = (unsigned char *) &spl_units[unit_index].hp_current;
    word[0] = (unsigned char) (raw & 0xff);
    word[1] = (unsigned char) ((raw >> 8) & 0xff);
}

static void spl_hp_max_bits(int unit_index, unsigned int raw)
{
    unsigned char *word;

    word = (unsigned char *) &spl_units[unit_index].hp_max;
    word[0] = (unsigned char) (raw & 0xff);
    word[1] = (unsigned char) ((raw >> 8) & 0xff);
}

/* The literal offsets the scorer reads: the spell's power word at +0x00 of a
   seven-byte record, and the unit's current HP, maximum HP, behaviour byte,
   character index and seal timer at +0x40, +0x42, +0x34, +0x08 and +0x27.  The
   record stride matters as much as the offsets: fdps_get_spell_record multiplies
   the id by a literal 7, so an id past 0 would land somewhere else if the record
   measured anything different. */
static void spell_scorer_reads_these_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_spell_effect), 7);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, power), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, char_id), 0x08);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
    CHECK_EQ((int) (offsetof(struct fdps_unit_record, status_timers) +
                    SPL_SLOT_SEAL), 0x27);
}

/* CMP 0xe / JL then CMP 0x10 / JLE is a range and CMP 0x21 / JNZ is an
   equality, so 0x0e, 0x0f, 0x10 and 0x21 are the whole healing family and 0x0d
   just below the span is not in it.  An unhurt target scores 0 as a heal and 8
   as a damaging spell, so the two branches are told apart by the answer.  0x11
   is checked in the same case because it is the id immediately above the span
   and has a branch of its own: its target knows no spells, so it scores 0 for a
   reason that is not the healing rule. */
static void the_healing_span_is_a_range_and_stops_where_it_stops(void)
{
    spl_stage();
    spl_unit(0, 100, 100);

    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_MID, 1, spl_targets), 0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_LAST, 1, spl_targets), 0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_REQUIEM, 1, spl_targets), 0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_BELOW_HEAL, 1, spl_targets),
             8);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_SEAL_MAGIC, 1, spl_targets),
             0);
}

/* Both heal comparisons are JLE against the current HP, so the score changes
   strictly ABOVE the threshold: with hp_max 30 the thresholds are 10 and 15,
   a unit sitting on exactly 10 takes the 3 and one on exactly 15 takes the 0.
   That is the opposite boundary from the item scorer's two JL, which is why
   neither can be spelled from the other. */
static void heal_thresholds_change_above_the_boundary(void)
{
    spl_stage();

    spl_unit(0, 9, 30);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             8);
    spl_unit(0, 10, 30);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             3);
    spl_unit(0, 14, 30);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             3);
    spl_unit(0, 15, 30);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             0);
    spl_unit(0, 30, 30);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             0);
}

/* IDIV and the SAR-plus-sign-correction halving both truncate toward zero, so
   with hp_max 5 the thresholds are 1 and 2 and not 1.67 and 2.5.  A rounding
   divide would move the 1 case. */
static void heal_thresholds_truncate(void)
{
    spl_stage();

    spl_unit(0, 0, 5);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             8);
    spl_unit(0, 1, 5);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             3);
    spl_unit(0, 2, 5);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             0);
}

/* XOR EAX,EAX / MOV AX at 00013996 and 000139a4 widen both HP words WITHOUT
   their sign, which is the reverse of the damaging branch's MOVSX.  An HP word
   of 0xffff therefore reads as 65535, far above hp_max/2, and the target is not
   worth healing at all -- read signed it would be -1 and the most hurt thing on
   the map, scoring 8.  A maximum of 0xffff reads as 65535 and puts its third at
   21845, so an unhurt-looking target scores 8; read signed the maximum would be
   -1, both thresholds would truncate to 0 and the score would be 0. */
static void heal_reads_both_hp_words_unsigned(void)
{
    spl_stage();

    spl_unit(0, 0, 30);
    spl_hp_bits(0, 0xffff);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             0);

    spl_unit(0, 0, 0);
    spl_hp_max_bits(0, 0xffff);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             8);
}

/* AND AL,0x1 / TEST / SHL 0x1 doubles the per-target score and tests that one
   bit alone, so 0x80 -- the bit the item scorer's restorative walk reads out of
   the same byte -- leaves a heal score untouched, and doubling 0 is still 0. */
static void behavior_bit_zero_doubles_the_heal_score(void)
{
    spl_stage();

    spl_unit(0, 1, 30);
    spl_units[0].ai_behavior = 0x01;
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             16);
    spl_unit(0, 12, 30);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             6);
    spl_unit(0, 30, 30);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             0);

    spl_unit(0, 1, 30);
    spl_units[0].ai_behavior = 0x80;
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             8);
    spl_units[0].ai_behavior = 0xfe;
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             8);
    spl_units[0].ai_behavior = 0xff;
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             16);
}

/* TEST EAX,EAX / JZ on the spell count and CMP byte ptr [EAX+0x27],0x0 on the
   seal timer are both required and the pair is short circuited, so a target
   pays 6 only when it has magic to lose and has not already lost it.  The
   poison and paralysis timers are set in the same case to show the test is on
   the seal byte alone and not on "any status". */
static void seal_needs_a_spell_and_a_clear_seal_timer(void)
{
    spl_stage();

    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_SEAL_MAGIC, 1, spl_targets),
             0);
    spl_knows(0, 0x27);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_SEAL_MAGIC, 1, spl_targets),
             6);
    spl_timer(0, SPL_SLOT_POISON, 3);
    spl_timer(0, SPL_SLOT_PARALYSIS, 3);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_SEAL_MAGIC, 1, spl_targets),
             6);
    spl_timer(0, SPL_SLOT_SEAL, 1);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_SEAL_MAGIC, 1, spl_targets),
             0);

    spl_stage();
    spl_knows(0, 0);
    spl_knows(1, 5);
    spl_timer(1, SPL_SLOT_SEAL, 1);
    spl_knows(2, 9);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_SEAL_MAGIC, 3, spl_targets),
             12);
}

/* The three CMP byte ptr [EAX+0x25 / 0x26 / 0x27],0x0 are OR-ed and the 6 is
   added once, so a target carrying all three ailments is worth the same as one
   carrying a single ailment, and the three blessing timers next door do not
   count as ailments at all. */
static void cure_pays_once_for_any_ailment(void)
{
    spl_stage();

    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_CURE, 1, spl_targets), 0);
    spl_timer(0, SPL_SLOT_POISON, 1);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_CURE, 1, spl_targets), 6);
    spl_timer(0, SPL_SLOT_POISON, 0);
    spl_timer(0, SPL_SLOT_PARALYSIS, 1);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_CURE, 1, spl_targets), 6);
    spl_timer(0, SPL_SLOT_PARALYSIS, 0);
    spl_timer(0, SPL_SLOT_SEAL, 1);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_CURE, 1, spl_targets), 6);
    spl_timer(0, SPL_SLOT_POISON, 4);
    spl_timer(0, SPL_SLOT_PARALYSIS, 4);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_CURE, 1, spl_targets), 6);

    spl_stage();
    spl_timer(0, SPL_SLOT_BLESS_AP, 5);
    spl_timer(0, SPL_SLOT_BLESS_DP, 5);
    spl_timer(0, SPL_SLOT_BLESS_DX, 5);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_CURE, 1, spl_targets), 0);
}

/* PUSH 0xa / PUSH 0x25 for 腐毒術 and PUSH 0xa / PUSH 0x26 for 麻痺術: each
   ailment spell consults its own timer at 10 a clear target and ignores the
   other.  Two targets, one already poisoned, separates them -- 10 against 20. */
static void the_two_ailment_spells_read_their_own_timer(void)
{
    spl_stage();
    spl_timer(0, SPL_SLOT_POISON, 2);

    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_POISON, 2, spl_targets), 10);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PARALYSE, 2, spl_targets), 20);
    spl_timer(1, SPL_SLOT_PARALYSIS, 2);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_POISON, 2, spl_targets), 10);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PARALYSE, 2, spl_targets), 10);
}

/* MOV dword ptr [EBP-0x14],EAX at 00013b3a, 00013b51 and 00013b68: each of
   神之祝福's three passes STORES over the last, so only the dexterity pass at
   record +0x24 reaches the answer.

   Three targets, all clear of the attack and defence blessings and two of them
   already carrying the dexterity one: the attack and defence passes are each 12
   and the dexterity pass is 4, and 4 is the answer.  Written with += it would be
   28.  The second arrangement is the other way round -- all three carry the
   attack blessing and none the dexterity one -- so the discarded pass is 0 and
   the answer is the full 12; += would give 24 there. */
static void the_blessing_branch_keeps_only_the_last_pass(void)
{
    spl_stage();
    spl_timer(0, SPL_SLOT_BLESS_DX, 1);
    spl_timer(1, SPL_SLOT_BLESS_DX, 1);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_BLESSING, 3, spl_targets), 4);

    spl_stage();
    spl_timer(0, SPL_SLOT_BLESS_AP, 1);
    spl_timer(1, SPL_SLOT_BLESS_AP, 1);
    spl_timer(2, SPL_SLOT_BLESS_AP, 1);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_BLESSING, 3, spl_targets), 12);
}

/* MOVSX word ptr [EAX+0x40] / CMP / JGE at 00013bd2: the kill needs the
   target's current HP strictly BELOW the spell's power, so a cast that exactly
   matches the remaining HP scores the wound.  The HP word is signed here, the
   reverse of the healing branch, so 0xffff is -1 and dies to a power of 100;
   read unsigned it would be 65535 and merely be wounded.  火焰's power is not
   what is staged -- the numbers are chosen to sit on the boundary. */
static void the_kill_test_is_strict_and_reads_hp_signed(void)
{
    spl_stage();
    spl_power(SPL_ID_PLAIN_DAMAGE, 100);

    spl_unit(0, 101, 500);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             8);
    spl_unit(0, 100, 500);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             8);
    spl_unit(0, 99, 500);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             0x18);

    spl_unit(0, 0, 500);
    spl_hp_bits(0, 0xffff);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             0x18);
    spl_hp_bits(0, 0x8000);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             0x18);
}

/* MOVSX word ptr [EAX] at 00013945 takes the power word signed, and
 * MAGICDAT.DAT stores the attack-multiplier spells as a negated percentage
 * (assets/spells.md), so nothing can be below one of them: a target at 0 HP and
 * one whose HP word is 0xffff both score the flat wound.  Read the power
 * unsigned and -50 would be 65486 and every target of those spells would look
 * like a kill. */
static void a_negative_power_can_never_kill(void)
{
    spl_stage();
    spl_power(SPL_ID_PLAIN_DAMAGE, -50);

    spl_unit(0, 0, 500);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             8);
    spl_hp_bits(0, 0xffff);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             8);
}

/* IMUL EAX,[EBP+0x14],0x7 inside fdps_get_spell_record: the power comes from
   the record the id selects, so two ids with different powers rank the same
   target differently -- one kills it and the other does not. */
static void the_power_comes_from_the_id_s_own_record(void)
{
    spl_stage();
    spl_power(SPL_ID_PLAIN_DAMAGE, 100);
    spl_power(SPL_ID_BELOW_HEAL, 10);
    spl_unit(0, 50, 500);

    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             0x18);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_BELOW_HEAL, 1, spl_targets),
             8);
}

/* CMP byte ptr [EAX+0x8],0x0 then FILD / FMUL 1.5 / __CHP / FISTP: the
   protagonist's two possible scores are weighted to 12 and 36, and every other
   character index is left alone.  The multiply is over the whole per-target
   score and not over the total, so two protagonists are 24 and not one
   weighting applied twice. */
static void the_protagonist_is_weighted_by_one_and_a_half(void)
{
    spl_stage();
    spl_power(SPL_ID_PLAIN_DAMAGE, 100);
    spl_unit(0, 500, 500);
    spl_unit(1, 500, 500);

    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             8);
    spl_units[0].char_id = 0;
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             12);
    spl_unit(0, 50, 500);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             36);

    spl_unit(0, 500, 500);
    spl_units[1].char_id = 0;
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 2, spl_targets),
             24);
    spl_units[1].char_id = 1;
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 2, spl_targets),
             20);
}

/* CMP 0xb / JZ then CMP 0xa / JNZ at 00013b8c guards the flying test, and a
   flying target is skipped with the accumulator untouched rather than scored
   0 -- which is the same number here, so the case proves it by leaving a second
   target that does score.  Class 0x1f 飛兵 is in fdps_unit_is_flying's list and
   0x19 機兵 is not, and an ordinary damaging spell hits both. */
static void only_the_two_quake_spells_skip_flyers(void)
{
    spl_stage();
    spl_power(SPL_ID_QUAKE, 0);
    spl_power(SPL_ID_GREAT_QUAKE, 0);
    spl_power(SPL_ID_PLAIN_DAMAGE, 0);
    spl_units[0].clazz = SPL_CLASS_FLYING;
    spl_units[1].clazz = SPL_CLASS_NOT_FLYING;

    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_QUAKE, 2, spl_targets), 8);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_GREAT_QUAKE, 2, spl_targets),
             8);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 2, spl_targets),
             16);

    spl_units[1].clazz = SPL_CLASS_FLYING;
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_QUAKE, 2, spl_targets), 0);
    spl_units[0].clazz = 0;
    spl_units[1].clazz = 0;
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_QUAKE, 2, spl_targets), 16);
}

/* Every loop test is CMP / JL placed before its body, so a count of 0 or below
   scores 0 in every branch without reading the index list, and the delegating
   branches inherit the same shape from fdps_score_targets_without_status.  A
   do/while spelling would score the first entry, which is staged to be worth
   something in each branch. */
static void a_nonpositive_target_count_scores_zero_everywhere(void)
{
    spl_stage();
    spl_power(SPL_ID_PLAIN_DAMAGE, 100);
    spl_unit(0, 1, 30);
    spl_knows(0, 3);
    spl_timer(0, SPL_SLOT_SEAL, 0);

    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 0, spl_targets),
             0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, -1, spl_targets),
             0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_SEAL_MAGIC, 0, spl_targets),
             0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_SEAL_MAGIC, -1, spl_targets),
             0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_CURE, 0, spl_targets), 0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_POISON, 0, spl_targets), 0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_BLESSING, 0, spl_targets), 0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 0, spl_targets),
             0);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, -1,
                                          spl_targets),
             0);
}

/* MOV AL,byte ptr [EAX] / AND EAX,0xff reads the list one byte at a time in the
   order given and the bound is exclusive, so the entries past target_count are
   not visited however much they would pay.  Both walking branches are checked:
   the list starts at unit 3 while unit 0 is the one staged to score, so a walk
   that ignored the list and counted units from zero would answer differently. */
static void both_walks_follow_the_index_list(void)
{
    spl_stage();
    spl_power(SPL_ID_PLAIN_DAMAGE, 100);
    spl_targets[0] = 3;
    spl_targets[1] = 1;
    spl_targets[2] = 5;

    spl_unit(0, 1, 30);
    spl_unit(1, 30, 30);
    spl_unit(3, 1, 30);
    spl_unit(5, 1, 30);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 1, spl_targets),
             8);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 2, spl_targets),
             8);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_HEAL_FIRST, 3, spl_targets),
             16);

    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 1, spl_targets),
             0x18);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 2, spl_targets),
             0x18 + 0x18);
    CHECK_EQ(fdps_score_targets_for_spell(SPL_ID_PLAIN_DAMAGE, 3, spl_targets),
             0x18 * 3);
}

/* ------------------------------------------------------------------
 * fdps_map_actor_score_best_spell, 00013420.
 *
 * The spell search is built on the same 4x4 fixture the attack search above
 * uses -- atk_stage() puts up the seven blocks the flood fill needs and points
 * the unit, item and class globals at them -- with one block added, the
 * MAGICDAT.DAT spell table, because this search resolves a record per learned
 * spell.  Every class row costs 1 on all eight terrain types, so a cast
 * distance of d covers exactly the tiles within Manhattan distance d of the
 * caster and the candidate tiles come out of
 * fdps_map_grid_collect_marked_tiles row by row.
 *
 * Every spell below carries an id outside the ranges fdps_score_targets_for_spell
 * names, so its branch is the plain damaging one: 0x18 when the target's
 * current HP is strictly below the spell's power word and 8 otherwise, with no
 * flyer test and no protagonist weighting as long as char_id is not 0.  That
 * keeps a case about the search's own ranking from turning on the scorer's
 * arithmetic.
 *
 * Expected values come from the assembly at 00013420 -- PUSH 0x0 before the
 * fdps_get_class_record call at 00013436, the TEST/JZ pair at 00013484 and
 * CMP byte ptr [EAX+0x27],0x0 at 0001348d, the zero-extended MP cost against
 * MOVSX word ptr [EAX+0x44] with CMP / JG at 000134ee, the raw distance byte
 * pushed at 000134fa, the select_mode fork at 0001356a with its
 * CMP byte ptr [EAX+0x6],0x0 / MOV 1 / MOV 0, the PUSH 0x0 min_dist at
 * 0001359a, and the JG / JNZ / MOVSX word ptr [EAX] / JG ranking at
 * 000135ee-0001360d -- and by walking that algorithm over the fixture by hand.
 * None of them is read off the emitted C.
 *
 * Every case leaves the score global at 0x12 and the other three at a sentinel
 * before the call, so a case that expects a score of 8 also proves the entry
 * zeroing at 0001342c.
 * ------------------------------------------------------------------ */

#define BSP_SPELLS 12

/* What the four globals hold going in: a score no result below could reach by
   accident, and a value unlike every tile coordinate and spell id used here. */
#define BSP_SENTINEL_SCORE 0x12
#define BSP_SENTINEL 0x5a

/* Any char_id other than 0, so the scorer's protagonist weighting stays out of
   every expected value. */
#define BSP_NOBODY 5

static struct fdps_spell_effect bsp_spells[BSP_SPELLS];

/* The attack search's battle, plus an all-zero spell table and the four spell
   globals parked on their sentinels. */
static void bsp_stage(void)
{
    atk_stage();
    atk_zero((unsigned char *) bsp_spells, (int) sizeof(bsp_spells));
    data_fdps_battle_spell_effect_table_ptr = (unsigned char *) bsp_spells;
    data_fdps_battle_ai_best_spell_score = BSP_SENTINEL_SCORE;
    data_fdps_map_ai_best_spell_id = BSP_SENTINEL;
    data_fdps_battle_ai_best_spell_target_x = BSP_SENTINEL;
    data_fdps_battle_ai_best_spell_target_y = BSP_SENTINEL;
}

static void bsp_spell(int spell_id, int power, int cast_range, int area,
                      int mp_cost, int target_side)
{
    bsp_spells[spell_id].power = (short) power;
    bsp_spells[spell_id].cast_range_flags = (unsigned char) cast_range;
    bsp_spells[spell_id].area = (unsigned char) area;
    bsp_spells[spell_id].mp_cost = (unsigned char) mp_cost;
    bsp_spells[spell_id].target_side = (unsigned char) target_side;
}

/* Set one learned bit in the five-byte bitmap at record +0x1a, the way
   fdps_unit_collect_known_spells reads it: byte spell_id / 8, bit
   spell_id % 8. */
static void bsp_learns(int unit_index, int spell_id)
{
    atk_units[unit_index].spells_known_bitmap[spell_id / 8] |=
        (unsigned char) (1 << (spell_id % 8));
}

/* The caster: unit 0 at (0, 0) on side 0 with mp magic points and no spell
   learned yet. */
static void bsp_caster(int mp)
{
    atk_unit(0, 0, 0, 0, 10, 5, 100, BSP_NOBODY);
    atk_units[0].mp_current = (short) mp;
}

/* One enemy-side target at (x, y) with 100 HP, which every spell below wounds
   rather than kills unless its power says otherwise. */
static void bsp_target(int unit_index, int x, int y)
{
    atk_unit(unit_index, x, y, 1, 0, 0, 100, BSP_NOBODY);
}

/* The literal record offsets the search reads: the caster's tile at +0x00 and
   +0x01, its learned-spell bitmap at +0x1a, the seal timer at +0x27 -- element
   5 of status_timers, which starts at +0x22 -- and its current MP as a signed
   word at +0x44, plus the four spell record bytes at +0x03, +0x04, +0x05 and
   +0x06 and the power word at +0x00.  If any of them moved, the search would
   read a different field and every expected value below would be a
   coincidence. */
static void spell_search_reads_these_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 0x01);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, spells_known_bitmap),
             0x1a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_current), 0x44);
    CHECK_EQ((int) sizeof(struct fdps_spell_effect), 7);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, power), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, cast_range_flags), 0x03);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, area), 0x04);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, mp_cost), 0x05);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, target_side), 0x06);
}

/* CMP dword ptr [EBP-0x38],0x0 / JZ at 00013484: an actor whose bitmap is
   empty leaves at 00013493 with the score freshly zeroed and the other three
   globals untouched, so the previous actor's decision is still standing in
   them.  A target the spell would have caught is put in reach, so the case
   fails if the search ran at all. */
static void an_actor_that_knows_no_spell_returns_at_once(void)
{
    bsp_stage();
    bsp_caster(10);
    bsp_spell(1, 10, 3, 0, 0, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, BSP_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, BSP_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_y, BSP_SENTINEL);
}

/* CMP byte ptr [EAX+0x27],0x0 / JZ at 0001348d tests status_timers[5] alone,
   so a caster carrying the magic seal is skipped however much magic it knows,
   and one carrying the poison and paralysis timers instead is not.  The second
   half of the case shows the same fixture publishing, so the first half is
   about the seal byte and not about the search being unable to find
   anything. */
static void a_sealed_caster_is_skipped(void)
{
    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_spell(1, 10, 3, 0, 0, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;
    atk_units[0].status_timers[5] = 1;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, BSP_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, BSP_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_y, BSP_SENTINEL);

    atk_units[0].status_timers[5] = 0;
    atk_units[0].status_timers[3] = 4;
    atk_units[0].status_timers[4] = 4;
    data_fdps_battle_ai_best_spell_score = BSP_SENTINEL_SCORE;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, 1);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, 1);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_y, 0);
}

/* The MP cost byte is zero extended and compared against the signed MP word
   with CMP / JG at 000134ee, so a spell that costs exactly what is left is
   still cast and one that costs a point more is passed over entirely -- no
   flood fill, no candidate tile, nothing published.  Written as >= the first
   half would publish. */
static void an_unaffordable_spell_is_passed_over(void)
{
    bsp_stage();
    bsp_caster(4);
    bsp_learns(0, 1);
    bsp_spell(1, 10, 3, 0, 5, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, BSP_SENTINEL);

    bsp_stage();
    bsp_caster(5);
    bsp_learns(0, 1);
    bsp_spell(1, 10, 3, 0, 5, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, 1);
}

/* The select_mode fork at 0001356a.  With the caster on side 0 at (0, 0) and
   an enemy on side 1 at (1, 0), and a blast radius of 0 so a tile catches only
   what stands on it, the tile that wins names which side was collected.

   side_select 0 -- the enemy phase -- inverts the spell's target byte, so a
   byte of 0 becomes select_mode 1 and finds the non-zero side at (1, 0), while
   a byte of 1 becomes select_mode 0 and finds side 0, which is the caster's
   own tile.  side_select 1 forwards the byte, so the two answers swap.  A
   spelling that passed the byte through in both directions would give (1, 0)
   in three of the four. */
static void the_target_byte_is_inverted_for_an_enemy_caster(void)
{
    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_spell(1, 10, 3, 0, 0, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, 1);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_y, 0);

    data_fdps_battle_ai_best_spell_score = BSP_SENTINEL_SCORE;
    CHECK_EQ(fdps_map_actor_score_best_spell(0, 1), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_y, 0);

    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_spell(1, 10, 3, 0, 0, 1);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_y, 0);

    data_fdps_battle_ai_best_spell_score = BSP_SENTINEL_SCORE;
    CHECK_EQ(fdps_map_actor_score_best_spell(0, 1), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, 1);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_y, 0);
}

/* The distance byte at +0x03 is the flood fill's movement allowance over the
   all-ones default class row, so it bounds the tiles the cast may be centred
   on: with the target at (2, 0), a distance of 1 never reaches a tile that
   catches it and a distance of 2 does.  Both runs are otherwise identical, so
   the only thing separating them is which byte of the record the fill was
   handed. */
static void the_cast_distance_bounds_the_candidate_tiles(void)
{
    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_spell(1, 10, 1, 0, 0, 0);
    bsp_target(1, 2, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, BSP_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, BSP_SENTINEL);

    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_spell(1, 10, 2, 0, 0, 0);
    bsp_target(1, 2, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 8);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, 2);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_y, 0);
}

/* MOV AL,byte ptr [EAX+EBP*0x1-0x5c] / AND EAX,0xff at 00013629 publishes the
   MAGICDAT.DAT id out of the list fdps_unit_collect_known_spells filled, not
   the position in that list.  The caster learns spell 3 as well, priced out of
   reach so it cannot win, which puts spell 9 in slot 1: publishing the slot
   would leave a 1 behind. */
static void the_published_spell_id_is_the_learned_id(void)
{
    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 3);
    bsp_learns(0, 9);
    bsp_spell(3, 10, 3, 0, 99, 0);
    bsp_spell(9, 10, 3, 0, 0, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, 9);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, 1);
}

/* CMP EAX,[0x00063f88] / JG at 000135f1 ranks on the score before anything
   else, so a later spell with a far larger power word does not displace a
   better score and a later spell with a better score displaces whatever power
   the incumbent had.  With the target on 100 HP a power of 200 kills and
   scores 0x18 while a power of 10 wounds and scores 8. */
static void a_higher_score_beats_a_bigger_power(void)
{
    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_learns(0, 2);
    bsp_spell(1, 10, 3, 0, 0, 0);
    bsp_spell(2, 200, 3, 0, 0, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0x18);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, 2);

    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_learns(0, 2);
    bsp_spell(1, 200, 3, 0, 0, 0);
    bsp_spell(2, 10, 3, 0, 0, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0x18);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, 1);
}

/* MOVSX EAX,word ptr [EAX] / CMP EAX,[EBP-0x8] / JG at 00013607 is the
   tie-break, and it reads power SIGNED.  Both spells wound the 100 HP target
   for 8, so only the power separates them: a power of -1 does not displace an
   incumbent of 10, while an incumbent of -1 is displaced by 10.  Read
   unsigned, -1 would be 65535 and the first half would publish spell 2 --
   which is exactly what would happen to the eight 絕招, whose power word is a
   negative attack multiplier. */
static void the_tie_break_reads_power_signed(void)
{
    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_learns(0, 2);
    bsp_spell(1, 10, 3, 0, 0, 0);
    bsp_spell(2, -1, 3, 0, 0, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, 1);

    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_learns(0, 2);
    bsp_spell(1, -1, 3, 0, 0, 0);
    bsp_spell(2, 10, 3, 0, 0, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, 2);
}

/* The tie-break is JG and so strict: two spells that score the same and carry
   the same power leave the first one found standing, and the ids come out of
   the bitmap in ascending order, so that is the lower id. */
static void an_equal_power_does_not_displace_the_incumbent(void)
{
    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_learns(0, 2);
    bsp_spell(1, 10, 3, 0, 0, 0);
    bsp_spell(2, 10, 3, 0, 0, 0);
    bsp_target(1, 1, 0);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, 1);
}

/* CMP dword ptr [EBP-0x14],0x0 / JZ at 000135c4 skips the scoring call for a
   tile that caught nobody, so a caster alone on the map runs the whole search
   -- every candidate tile of a distance-3 range -- and publishes nothing but
   the zeroed score.  The three unpublished globals keep their sentinels, which
   is why a reader has to look at the score first. */
static void a_search_that_catches_nobody_publishes_nothing(void)
{
    bsp_stage();
    bsp_caster(10);
    bsp_learns(0, 1);
    bsp_spell(1, 10, 3, 0, 0, 0);
    data_fdps_map_unit_count = 1;

    CHECK_EQ(fdps_map_actor_score_best_spell(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_map_ai_best_spell_id, BSP_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_x, BSP_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_spell_target_y, BSP_SENTINEL);
}

/* ------------------------------------------------------------------
 * fdps_map_actor_score_best_item, 00013040.
 *
 * The item search is built on the same 4x4 fixture the attack search above
 * uses -- atk_stage() puts up the seven blocks the flood fill needs and points
 * the unit, item and class globals at them.  No block is added: the item table
 * is already one of the seven.  Every class row costs 1 on all eight terrain
 * types, so a reach of r covers exactly the tiles within Manhattan distance r
 * of the centre and the candidate tiles come out of
 * fdps_map_grid_collect_marked_tiles row by row, y outer and x inner.
 *
 * Every item below carries use_effect 0x1e, the line-damage code, because it
 * is the one code fdps_score_targets_for_item weighs without reading anything
 * but the target's current HP: 0x12 when that HP is at or below the item's
 * use_amount and 8 when it is above.  With use_amount 0 and a target on 100 HP
 * every catch is worth exactly 8, which keeps a case about the search's own
 * ranking from turning on the scorer's arithmetic.
 *
 * The two collectors read their side filter with OPPOSITE polarities and the
 * cases below depend on both: fdps_collect_targets_in_range's select_mode is
 * 0 = side 0, 1 = every non-zero side, 2 = side 1, 3 = side 2, while
 * fdps_collect_targets_in_line's select_enemy_side is 0 = every non-zero side
 * and non-zero = side 0.
 *
 * Expected values come from the assembly at 00013040 -- PUSH 0x0 before the
 * fdps_get_class_record call at 00013056, the CMP dword ptr [EBP-0x20],0x0 /
 * JNZ empty-bag exit at 000130b0, the MOV AL,byte ptr [EDX+0xb] entry read at
 * 000130e8 with the ADD EDX,EDX that makes the loop counter the entry index,
 * the CMP EAX,0xf / JLE at 0001310f with the MOV 0x1 / MOV 0x1 clamp at
 * 00013114, the CMP byte ptr [EAX+0xd],0x0 / JZ at 0001311f, the six PUSHes of
 * the aim search at 00013129, the select_mode fork at 0001319c, the reload of
 * the raw distance byte at 000131c8 with the second CMP EAX,0xf / JLE at
 * 000131d6, the SUB EAX,0x10 at 000131e4, and the CMP EAX,[0x00063f8c] / JLE
 * ranking at 00013257 -- and by walking that algorithm over the fixture by
 * hand.  None of them is read off the emitted C.
 *
 * Every case leaves the score global at 0x12 and the other three at a sentinel
 * before the call, so a case that expects a score of 8 also proves the entry
 * zeroing at 0001304c.
 * ------------------------------------------------------------------ */

/* What the four globals hold going in: a score no result below could reach by
   accident, and a value unlike every tile coordinate and bag slot used here. */
#define BIT_SENTINEL_SCORE 0x12
#define BIT_SENTINEL 0x5a

/* Any char_id other than 0, so nothing below can be reached by the
   protagonist's own weighting in a neighbouring scorer. */
#define BIT_NOBODY 5

/* Bit 0x80 of a bag entry's flag byte, the empty marker fdps_unit_item_count
   counts by.  A zeroed record therefore reads as eight FULL entries, which is
   why every actor below has its bag emptied explicitly. */
#define BIT_SLOT_EMPTY 0x80

/* The eight bag entries of a unit record's inventory_slots[16]. */
#define BIT_BAG_ENTRIES 8

/* ITEM.DAT use_distance values: 0x00 reaches the actor's own tile alone, 0x0f
   is the largest non-line reach, 0x10 is the shortest line -- length 0, which
   examines nothing -- and 0x11 is a line one tile long. */
#define BIT_DIST_SELF      0x00
#define BIT_DIST_WIDE      0x0f
#define BIT_DIST_LINE_ZERO 0x10
#define BIT_DIST_LINE_ONE  0x11

/* A use_amount above any HP the fixture gives a unit, so the damage branch
   answers 0x12 instead of 8. */
#define BIT_LETHAL_AMOUNT 200

/* A use_radius that covers the whole 4x4 map from any tile on it: the largest
   Manhattan distance between two of its tiles is 6. */
#define BIT_RADIUS_WHOLE_MAP 7

/* The attack search's battle with the four item globals parked on their
   sentinels.  atk_stage has already zeroed the item table and every unit. */
static void bit_stage(void)
{
    atk_stage();
    data_fdps_battle_ai_best_item_score = BIT_SENTINEL_SCORE;
    data_fdps_map_ai_best_item_bag_slot = BIT_SENTINEL;
    data_fdps_map_ai_best_item_target_x = BIT_SENTINEL;
    data_fdps_battle_ai_best_item_target_y = BIT_SENTINEL;
}

/* Mark all eight bag entries empty and clear their id bytes. */
static void bit_empty_bag(int unit_index)
{
    int slot;

    for (slot = 0; slot < BIT_BAG_ENTRIES; slot++) {
        atk_units[unit_index].inventory_slots[slot * 2] = BIT_SLOT_EMPTY;
        atk_units[unit_index].inventory_slots[slot * 2 + 1] = 0;
    }
}

/* Occupy bag entry `slot` with item_id: flag byte clear of the empty bit, id
   byte beside it. */
static void bit_carry(int unit_index, int slot, int item_id)
{
    atk_units[unit_index].inventory_slots[slot * 2] = 0;
    atk_units[unit_index].inventory_slots[slot * 2 + 1] =
        (unsigned char) item_id;
}

/* The five ITEM.DAT bytes this search and its scorer read. */
static void bit_item(int item_id, int use_effect, int use_amount,
                     int use_distance, int use_target, int use_radius)
{
    atk_items[item_id].use_effect = (unsigned char) use_effect;
    atk_items[item_id].use_amount = (short) use_amount;
    atk_items[item_id].use_distance = (unsigned char) use_distance;
    atk_items[item_id].use_target = (unsigned char) use_target;
    atk_items[item_id].use_radius = (unsigned char) use_radius;
}

/* The actor: unit 0 at (x, y) on side `side` with 100 HP and an empty bag. */
static void bit_actor(int x, int y, int side)
{
    atk_unit(0, x, y, side, 10, 5, 100, BIT_NOBODY);
    bit_empty_bag(0);
}

/* The literal record offsets the search reads: the actor's tile at +0x00 and
   +0x01, its bag at +0x0a -- the entry read is +0xb + slot*2, which is
   inventory_slots[slot * 2 + 1] -- and its current MP as a signed word at
   +0x44, plus the item's use_effect, use_distance, use_target and use_radius
   bytes at +0x0d, +0x10, +0x11 and +0x12.  The item stride matters as much as
   the offsets: fdps_get_item_record multiplies the id by a literal 0x17, so an
   id past 0 would land somewhere else if the record measured anything
   different. */
static void item_search_reads_these_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 0x01);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots), 0x0a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_current), 0x44);
    CHECK_EQ((int) sizeof(struct fdps_item_effect), 0x17);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_effect), 0x0d);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_distance), 0x10);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_target), 0x11);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_radius), 0x12);
}

/* CMP dword ptr [EBP-0x20],0x0 / JNZ at 000130b0: an actor carrying nothing
   leaves at 000130bd with the score freshly zeroed and the other three globals
   untouched, so the previous actor's decision is still standing in them.  A
   usable item is staged in the table and the actor is standing where its own
   radius would catch it, so the case fails if the walk ran at all. */
static void an_empty_bag_returns_at_once(void)
{
    bit_stage();
    bit_actor(1, 1, 0);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_SELF, 3, 0);
    data_fdps_map_unit_count = 1;

    CHECK_EQ(fdps_map_actor_score_best_item(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, BIT_SENTINEL);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, BIT_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, BIT_SENTINEL);
}

/* CMP byte ptr [EAX+0xd],0x0 / JZ at 0001311f skips to the next bag entry, so
   an item with no use effect costs the walk nothing and the entry after it is
   still examined.  Entry 0 holds the unusable item and entry 1 the usable one,
   and the published slot says which was scored. */
static void an_item_that_cannot_be_used_is_passed_over(void)
{
    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_carry(0, 1, 2);
    bit_item(1, 0, 0, BIT_DIST_SELF, 3, 0);
    bit_item(2, USE_EFFECT_DAMAGE, 0, BIT_DIST_SELF, 3, 0);
    data_fdps_map_unit_count = 1;

    CHECK_EQ(fdps_map_actor_score_best_item(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, 1);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, 1);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, 1);
}

/* MOV EDX,[EBP-0x1c] / ADD EDX,EDX / ADD EDX,[EBP-0x48] / MOV AL,[EDX+0xb] at
   000130de: the loop counter IS the entry index, and the flag byte
   fdps_unit_item_count counted with is never looked at again.  Here the only
   OCCUPIED entry is 7 and it holds an item with no use effect, while entry 0
   is marked empty and its id byte still names a usable item.  The count is
   therefore 1, the walk reads entry 0, and the empty entry's item is the one
   scored and published.  A loop that walked all eight entries and skipped the
   empty ones would find only the unusable item in entry 7 and publish nothing
   (rebuild_info/pitfalls.md). */
static void the_bag_walk_uses_its_counter_as_the_entry_index(void)
{
    bit_stage();
    bit_actor(1, 1, 0);
    atk_units[0].inventory_slots[1] = 1;
    bit_carry(0, 7, 2);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_SELF, 3, 0);
    bit_item(2, 0, 0, BIT_DIST_SELF, 3, 0);
    data_fdps_map_unit_count = 1;

    CHECK_EQ(fdps_map_actor_score_best_item(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, 0);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, 1);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, 1);
}

/* MOV EAX,[EBP-0x1c] / MOV [0x00063f7c],EAX at 00013277 publishes the loop
   counter, so the slot the winner is announced under is the entry it was read
   from.  Entry 0's item wounds for 8 and entry 1's kills for 0x12, so the
   later entry takes the decision. */
static void the_published_slot_is_the_winning_entry(void)
{
    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_carry(0, 1, 2);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_SELF, 3, 0);
    bit_item(2, USE_EFFECT_DAMAGE, BIT_LETHAL_AMOUNT, BIT_DIST_SELF, 3, 0);
    data_fdps_map_unit_count = 1;

    CHECK_EQ(fdps_map_actor_score_best_item(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0x12);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, 1);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, 1);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, 1);
}

/* CMP EAX,[0x00063f8c] / JLE at 00013257 is strictly greater, so an equal
   score leaves the incumbent standing and the earliest entry that reached it
   keeps the decision.  Both entries hold the same item, so both score 8 and
   only the first may be published.  The 8 also has to beat the 0x12 the
   fixture parked in the score global, which it can only do because the entry
   zeroing at 0001304c ran first. */
static void an_equal_score_does_not_displace_the_incumbent(void)
{
    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_carry(0, 1, 1);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_SELF, 3, 0);
    data_fdps_map_unit_count = 1;

    CHECK_EQ(fdps_map_actor_score_best_item(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, 0);
}

/* CMP dword ptr [EBP+0x18],0x0 / JNZ at 0001319c forks the side filter.  On
   the enemy phase the item's use_target byte is replaced by (byte == 0), so a
   byte of 0 becomes the collector's select_mode 1 -- every non-zero side --
   and a byte of 3 becomes select_mode 0, side 0.  On the NPC phase the byte
   goes through as it stands, so 0 selects side 0 and 3 selects side 2.
   Everything here reaches out of the actor's own tile with reach 0 and radius
   0, so the actor is the only unit any collector can see and it is on side 0:
   the two arrangements that select side 0 publish, the two that do not publish
   nothing.  That the actor's own tile is a candidate at all is the aim
   search's min_dist of 0, PUSH of [EBP-0x8] left at its 000130fd zero. */
static void the_use_target_byte_is_inverted_on_the_enemy_phase(void)
{
    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_SELF, 0, 0);
    data_fdps_map_unit_count = 1;
    CHECK_EQ(fdps_map_actor_score_best_item(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, BIT_SENTINEL);

    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_SELF, 3, 0);
    data_fdps_map_unit_count = 1;
    CHECK_EQ(fdps_map_actor_score_best_item(0, 0), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, 0);

    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_SELF, 0, 0);
    data_fdps_map_unit_count = 1;
    CHECK_EQ(fdps_map_actor_score_best_item(0, 1), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, 0);

    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_SELF, 3, 0);
    data_fdps_map_unit_count = 1;
    CHECK_EQ(fdps_map_actor_score_best_item(0, 1), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, BIT_SENTINEL);
}

/* MOV 0x1 into both [EBP-0x4] and [EBP-0x8] at 00013114, and SUB EAX,0x10 at
   000131e4.  A line item aims at its neighbours -- reach 1 with an exclusive
   minimum distance of 1 drops the actor's own tile -- and the line then sweeps
   from the actor toward the aim tile for use_distance - 0x10 tiles, the first
   tile examined being the one next to the actor.

   The actor stands at (1, 1) and a side-0 unit at (1, 0), one of the four
   neighbours.  select_enemy_side is the line collector's own polarity and a
   non-zero value there keeps side 0, so use_target 1 on the NPC phase catches
   it.  The candidate tiles arrive row by row, so (1, 0) is the first of the
   four and the one published.

   With use_distance 0x10 the same item sweeps 0 tiles and catches nothing: the
   subtraction is what makes 0x11 reach one tile, and a length taken from the
   raw byte would sweep sixteen and seventeen instead. */
static void a_line_item_aims_at_its_neighbours(void)
{
    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_LINE_ONE, 1, 0);
    atk_unit(1, 1, 0, 0, 0, 0, 100, BIT_NOBODY);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_item(0, 1), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, 0);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, 1);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, 0);

    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_LINE_ZERO, 1, 0);
    atk_unit(1, 1, 0, 0, 0, 0, 100, BIT_NOBODY);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_item(0, 1), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, BIT_SENTINEL);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, BIT_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, BIT_SENTINEL);
}

/* MOV EAX,[EBP-0x50] / MOV AL,byte ptr [EAX+0x10] at 000131c8 reloads the
   distance byte from the record before the shape test, so the value the clamp
   at 00013114 left behind is not the value that picks the branch.

   Both halves carry the same item but for its distance byte, both aim on the
   NPC phase with use_target 1, and the only other unit is on side 1 at (3, 3),
   the far corner.  The line collector reads a non-zero side filter as side 0,
   so it can never see that unit; the radius collector reads select_mode 1 as
   every non-zero side, and a radius of 7 covers the whole map from any tile on
   it, so it always can.

   With use_distance 0x11 the search must take the line branch and publish
   nothing.  A search that kept the clamped reach of 1 would compare 1 against
   0x10, take the radius branch instead and publish 8 -- which is exactly what
   the second half shows the radius branch doing when the distance byte really
   is below 0x10 (rebuild_info/pitfalls.md). */
static void the_shape_test_reloads_the_raw_distance_byte(void)
{
    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_LINE_ONE, 1,
             BIT_RADIUS_WHOLE_MAP);
    atk_unit(1, 3, 3, 1, 0, 0, 100, BIT_NOBODY);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_item(0, 1), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, BIT_SENTINEL);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, BIT_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, BIT_SENTINEL);

    bit_stage();
    bit_actor(1, 1, 0);
    bit_carry(0, 0, 1);
    bit_item(1, USE_EFFECT_DAMAGE, 0, BIT_DIST_WIDE, 1, BIT_RADIUS_WHOLE_MAP);
    atk_unit(1, 3, 3, 1, 0, 0, 100, BIT_NOBODY);
    data_fdps_map_unit_count = 2;

    CHECK_EQ(fdps_map_actor_score_best_item(0, 1), 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 8);
    CHECK_EQ(data_fdps_map_ai_best_item_bag_slot, 0);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, 0);
}

void run_aiscore_tests(void)
{
    RUN_TEST(attack_search_reads_these_offsets);
    RUN_TEST(no_equipped_weapon_returns_at_once);
    RUN_TEST(a_search_that_finds_nobody_publishes_nothing);
    RUN_TEST(a_reachable_target_publishes_tile_index_and_tier);
    RUN_TEST(wound_tier_needs_an_estimate_above_two);
    RUN_TEST(lethal_test_is_strictly_less_than);
    RUN_TEST(class_row_is_the_class_byte_plus_one);
    RUN_TEST(counter_attack_penalty_needs_exactly_one);
    RUN_TEST(protagonist_estimate_is_tripled_and_halved);
    RUN_TEST(a_lethal_estimate_is_doubled);
    RUN_TEST(a_higher_tier_beats_a_bigger_estimate);
    RUN_TEST(the_weapon_reach_comes_from_its_two_bytes);
    RUN_TEST(the_stat_words_are_read_signed);
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
    RUN_TEST(spell_scorer_reads_these_offsets);
    RUN_TEST(the_healing_span_is_a_range_and_stops_where_it_stops);
    RUN_TEST(heal_thresholds_change_above_the_boundary);
    RUN_TEST(heal_thresholds_truncate);
    RUN_TEST(heal_reads_both_hp_words_unsigned);
    RUN_TEST(behavior_bit_zero_doubles_the_heal_score);
    RUN_TEST(seal_needs_a_spell_and_a_clear_seal_timer);
    RUN_TEST(cure_pays_once_for_any_ailment);
    RUN_TEST(the_two_ailment_spells_read_their_own_timer);
    RUN_TEST(the_blessing_branch_keeps_only_the_last_pass);
    RUN_TEST(the_kill_test_is_strict_and_reads_hp_signed);
    RUN_TEST(a_negative_power_can_never_kill);
    RUN_TEST(the_power_comes_from_the_id_s_own_record);
    RUN_TEST(the_protagonist_is_weighted_by_one_and_a_half);
    RUN_TEST(only_the_two_quake_spells_skip_flyers);
    RUN_TEST(a_nonpositive_target_count_scores_zero_everywhere);
    RUN_TEST(both_walks_follow_the_index_list);
    RUN_TEST(spell_search_reads_these_offsets);
    RUN_TEST(an_actor_that_knows_no_spell_returns_at_once);
    RUN_TEST(a_sealed_caster_is_skipped);
    RUN_TEST(an_unaffordable_spell_is_passed_over);
    RUN_TEST(the_target_byte_is_inverted_for_an_enemy_caster);
    RUN_TEST(the_cast_distance_bounds_the_candidate_tiles);
    RUN_TEST(the_published_spell_id_is_the_learned_id);
    RUN_TEST(a_higher_score_beats_a_bigger_power);
    RUN_TEST(the_tie_break_reads_power_signed);
    RUN_TEST(an_equal_power_does_not_displace_the_incumbent);
    RUN_TEST(a_search_that_catches_nobody_publishes_nothing);
    RUN_TEST(item_search_reads_these_offsets);
    RUN_TEST(an_empty_bag_returns_at_once);
    RUN_TEST(an_item_that_cannot_be_used_is_passed_over);
    RUN_TEST(the_bag_walk_uses_its_counter_as_the_entry_index);
    RUN_TEST(the_published_slot_is_the_winning_entry);
    RUN_TEST(an_equal_score_does_not_displace_the_incumbent);
    RUN_TEST(the_use_target_byte_is_inverted_on_the_enemy_phase);
    RUN_TEST(a_line_item_aims_at_its_neighbours);
    RUN_TEST(the_shape_test_reloads_the_raw_distance_byte);
}
