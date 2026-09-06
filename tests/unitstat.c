/* tests/unitstat.c -- cover for src/unitstat.c.
 *
 * Every expected value below is read off the assembly of fdps_unit_apply_heal
 * at 00027070 -- LEA EDX,[EDX+EDX*8] with IDIV by 10 for the base heal, IDIV
 * by 100 then IMUL by the amount then IDIV by 1000 for the random bonus, the
 * two MOVSX word loads at 0002708e and 00027098 for which HP field is which,
 * the JLE clamp at 000270e9, the MOVSX re-read at 000270f4 that runs BEFORE
 * the store at 00027106, XOR EAX,EAX / MOV AL,byte ptr [EDX+0x21] for the
 * level, the CMP EAX,0xf / CMP EAX,0x22 pair that bounds the promotion bonus,
 * ADD dword ptr [EBP-0x8],0x1e for the bonus itself, CMP EAX,0x3c for the
 * enemy cut-off, IMUL by 0x19 then by the restored HP then IDIV by the
 * maximum HP for the award, and ADD dword ptr [0x00069cec],EAX for the way it
 * lands -- and from the record layout ticket 17 settled.  None of them is
 * read off the emitted C.
 *
 * HOW THE RANDOMNESS IS TAKEN OUT.  One roll runs through this function,
 * (rand() % 100) * amount / 1000, and rand() % 100 is 0..99.  So for any
 * amount of 10 or less the largest numerator the roll can form is 990, which
 * the signed divide by 1000 truncates to 0 whatever rand() returned -- the
 * bonus is identically zero and the heal is exactly amount * 9 / 10.  Every
 * figure asserted below except one comes from an amount inside that band.
 *
 * The exception is the band test itself, which uses an amount of 100 so the
 * bonus is (rand() % 100) / 10 and therefore 0..9, and asserts the roll
 * against the interval rather than a value.
 *
 * The fdps_unit_restore_mp cases below are read off 000275a0 the same way:
 * the same LEA/IDIV roll, the XOR EAX,EAX / MOV AX,word ptr [EDX+0x44] and
 * MOV AX / AND EAX,0xffff loads at 000275bd and 0002762d that make both MP
 * reads UNSIGNED where the heal's are MOVSX, the JLE clamp at 00027622, the
 * 16-bit store at 00027644, and the return rebuilt from the two roll locals
 * at 00027648.  That function has no divide by the maximum and no ADD into
 * the experience accumulator at all, and both absences are asserted.
 *
 * The fdps_unit_apply_damage cases are read off 00028460 the same way: the
 * same LEA/IDIV roll at 00028496 and 000284a8, the XOR EAX,EAX / MOV AX word
 * loads at 0002847d and 00028489 that make BOTH HP reads unsigned where the
 * heal's are MOVSX, the JGE clamp at 000284e1 that pins the stored HP at 0, the
 * 16-bit store at 000284f0, the return rebuilt from [EBP-0x8] at 00028551, the
 * CMP byte ptr [EAX+0x6],0x0 side equality at 000284f7, the SUB EAX,0x3c
 * portrait bias at 00028508, byte +0x9 of the enemy record for the experience
 * multiplier, the IMUL by the level byte at 0002852a, the CMP dword ptr
 * [EBP-0x18],0x0 / JZ at 00028534 that skips the proration for a dead target,
 * and ADD dword ptr [0x00069cec],EAX at 0002854b.
 *
 * WHY THE EXPERIENCE ACCUMULATOR IS SEEDED WITH 1000.  The function ADDS to
 * data_fdps_battle_pending_xp_credit, so a seed of 0 could not tell an award
 * of 0 apart from an assignment of 0.  Seeding it with a value no award here
 * could produce makes both the accumulate and the untouched cases visible.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "unitstat.h"

/* The stride fdps_get_unit_record multiplies by. */
#define UNIT_RECORD_STRIDE 0x50

/* Four records are staged so that a walk which strayed into a neighbour would
   be visible.  The heal is applied to record 1 in every test but the last. */
#define STAGE_UNITS 4
#define PATIENT 1

/* CMP EAX,0xf and CMP EAX,0x22: the promoted character forms, and the two
   portrait ids either side of that span. */
#define FIRST_PROMOTED_PORTRAIT 0x0f
#define LAST_PROMOTED_PORTRAIT 0x21
#define BELOW_PROMOTED_PORTRAIT 0x0e
#define ABOVE_PROMOTED_PORTRAIT 0x22

/* CMP EAX,0x3c: the first enemy portrait id, and the last roster one. */
#define FIRST_ENEMY_PORTRAIT 0x3c
#define LAST_ROSTER_PORTRAIT 0x3b

/* An ordinary roster portrait, well inside 0..0x0e, so the standard fixture
   takes neither the promotion bonus nor the enemy cut-off. */
#define PLAIN_PORTRAIT 5

/* A seed no award the fixture can produce could be mistaken for. */
#define XP_SEED 1000

/* Amounts inside the band where the random bonus is identically zero. */
#define AMOUNT_TEN 10
#define AMOUNT_SEVEN 7
#define AMOUNT_THREE 3

/* Outside it: the bonus is (rand() % 100) / 10, so the roll is 90..99. */
#define AMOUNT_HUNDRED 100
#define HUNDRED_ROLL_LOW 90
#define HUNDRED_ROLL_HIGH 99
#define BAND_DRAWS 20

static unsigned char unit_block[STAGE_UNITS * UNIT_RECORD_STRIDE];

/* Only ever read for its field sizes. */
static struct fdps_unit_record layout_probe;

static struct fdps_unit_record *unit(int unit_index)
{
    return (struct fdps_unit_record *)
        (unit_block + unit_index * UNIT_RECORD_STRIDE);
}

/* Builds the standard patient and publishes every global the function reads.
 *
 * Record 1 is a level 4 roster character on 10 of 100 HP: the level and the
 * maximum are chosen so that the award, level * 25 * restored / max_hp, comes
 * out as a whole number for the restored figures the tests use, and the
 * headroom is wide enough that nothing clamps unless a test asks for it. */
static void stage(void)
{
    int i;

    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_block[i] = 0;
    }

    data_fdps_map_unit_array_ptr = unit_block;
    data_fdps_battle_pending_xp_credit = XP_SEED;

    unit(PATIENT)->portrait_id = PLAIN_PORTRAIT;
    unit(PATIENT)->level = 4;
    unit(PATIENT)->hp_current = 10;
    unit(PATIENT)->hp_max = 100;
}

static void the_record_layout_matches_the_offsets_read(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), UNIT_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 0x07);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, level), 0x21);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
    CHECK_EQ((int) sizeof(layout_probe.hp_current), 2);
    CHECK_EQ((int) sizeof(layout_probe.hp_max), 2);
    CHECK_EQ((int) sizeof(layout_probe.level), 1);
    CHECK_EQ((int) sizeof(layout_probe.portrait_id), 1);
}

/* amount * 9 / 10 with the bonus pinned to zero: 10 gives exactly 9. */
static void a_heal_of_ten_rolls_nine_every_time(void)
{
    stage();
    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 19);
}

/* 63 / 10 and 27 / 10: the base heal truncates, it does not round. */
static void the_base_heal_truncates(void)
{
    stage();
    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_SEVEN), 6);
    CHECK_EQ(unit(PATIENT)->hp_current, 16);

    stage();
    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_THREE), 2);
    CHECK_EQ(unit(PATIENT)->hp_current, 12);
}

/* IDIV on a sign-extended dividend truncates towards zero, so -7 gives -6 and
   not -7.  Nothing clamps the HP at the bottom either: a drain past zero is
   written back as a negative word.  The award follows it down, because the
   restored figure is negative and the accumulator is signed. */
static void a_negative_amount_truncates_towards_zero_and_drains(void)
{
    stage();
    unit(PATIENT)->hp_current = 2;

    CHECK_EQ(fdps_unit_apply_heal(PATIENT, -AMOUNT_SEVEN), -6);
    CHECK_EQ(unit(PATIENT)->hp_current, -4);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED - 6);
}

/* JLE at 000270e9: the total is clamped down to the maximum, and the clamp is
   a separate test rather than a min(), so a total already at the maximum is
   left where it is. */
static void the_total_is_clamped_to_the_maximum_hp(void)
{
    stage();
    unit(PATIENT)->hp_current = 38;
    unit(PATIENT)->hp_max = 40;

    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 40);
}

/* The whole point of the function's return value: the clamped heal gave the
   unit 2 HP and the figure handed back is still the 9 that was rolled. */
static void the_return_is_the_roll_and_not_the_hp_restored(void)
{
    int rolled;

    stage();
    unit(PATIENT)->hp_current = 38;
    unit(PATIENT)->hp_max = 40;

    rolled = fdps_unit_apply_heal(PATIENT, AMOUNT_TEN);
    CHECK_EQ(rolled, 9);
    CHECK_EQ(unit(PATIENT)->hp_current - 38, 2);
    CHECK_EQ(rolled != unit(PATIENT)->hp_current - 38, 1);
}

/* At an amount of 100 the bonus is (rand() % 100) / 10, so the roll is 90..99
   and the HP moves by exactly the roll.  Twenty draws, each checked against
   the interval and against the record.  The maximum is lifted to 200 so that
   the top of the band cannot reach the clamp and hide itself. */
static void the_roll_spans_nine_tenths_to_the_full_amount(void)
{
    int draw;
    int rolled;
    int inside;
    int matched;

    inside = 0;
    matched = 0;

    for (draw = 0; draw < BAND_DRAWS; draw++) {
        stage();
        unit(PATIENT)->hp_max = 200;
        rolled = fdps_unit_apply_heal(PATIENT, AMOUNT_HUNDRED);
        if (rolled >= HUNDRED_ROLL_LOW && rolled <= HUNDRED_ROLL_HIGH) {
            inside++;
        }
        if (unit(PATIENT)->hp_current == 10 + rolled) {
            matched++;
        }
    }

    CHECK_EQ(inside, BAND_DRAWS);
    CHECK_EQ(matched, BAND_DRAWS);
}

/* The award is scaled by the HP the record actually gained, which the clamp
   cut to 5, and not by the 9 that was rolled: 4 * 25 * 5 / 100 is 5, whereas
   the rolled figure would have given 9. */
static void the_award_is_scaled_by_the_hp_actually_restored(void)
{
    stage();
    unit(PATIENT)->hp_current = 95;

    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 100);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 5);
}

/* A unit already at full HP is restored nothing, so it credits nothing --
   and still returns its roll. */
static void a_full_unit_credits_nothing_but_still_returns_its_roll(void)
{
    stage();
    unit(PATIENT)->hp_current = 100;

    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 100);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);
}

/* ADD dword ptr [0x00069cec],EAX: two heals in a row leave the sum of both
   awards, not the second one.  Each heal here restores the full 9. */
static void the_award_accumulates_rather_than_replacing(void)
{
    stage();
    unit(PATIENT)->hp_current = 0;

    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 9);

    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 18);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 18);
}

/* The divide by the maximum HP is the LAST step, applied to the whole
   product.  A level 1 unit restored 9 of 100 gives 1 * 25 * 9 / 100 = 2; had
   the divide come before the multiply it would have been 0. */
static void the_award_divides_the_whole_product_last(void)
{
    stage();
    unit(PATIENT)->level = 1;
    unit(PATIENT)->hp_current = 0;

    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 2);
}

/* Portrait 0x0f is a promoted form: the level it credits by is 4 + 30, so the
   award is 34 * 25 * 9 / 100 = 76 instead of 9. */
static void a_promoted_portrait_credits_thirty_extra_levels(void)
{
    stage();
    unit(PATIENT)->hp_current = 0;
    unit(PATIENT)->portrait_id = FIRST_PROMOTED_PORTRAIT;

    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 76);
}

/* Both ends of that span, and the portrait id either side of each. */
static void the_promotion_span_is_0x0f_through_0x21(void)
{
    stage();
    unit(PATIENT)->hp_current = 0;
    unit(PATIENT)->portrait_id = BELOW_PROMOTED_PORTRAIT;
    fdps_unit_apply_heal(PATIENT, AMOUNT_TEN);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 9);

    stage();
    unit(PATIENT)->hp_current = 0;
    unit(PATIENT)->portrait_id = LAST_PROMOTED_PORTRAIT;
    fdps_unit_apply_heal(PATIENT, AMOUNT_TEN);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 76);

    stage();
    unit(PATIENT)->hp_current = 0;
    unit(PATIENT)->portrait_id = ABOVE_PROMOTED_PORTRAIT;
    fdps_unit_apply_heal(PATIENT, AMOUNT_TEN);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 9);
}

/* Portrait 0x3c and up is an EnemyDat entry: the HP is still restored, the
   roll is still returned, and the accumulator is not touched at all -- so the
   seed stands rather than being cleared. */
static void an_enemy_portrait_is_healed_but_credits_nothing(void)
{
    stage();
    unit(PATIENT)->hp_current = 0;
    unit(PATIENT)->portrait_id = FIRST_ENEMY_PORTRAIT;

    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 9);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);

    stage();
    unit(PATIENT)->hp_current = 0;
    unit(PATIENT)->portrait_id = LAST_ROSTER_PORTRAIT;

    CHECK_EQ(fdps_unit_apply_heal(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 9);
}

/* The index picks the record: unit 2 is healed and its three neighbours are
   left exactly where they were. */
static void each_index_selects_its_own_record(void)
{
    stage();
    unit(2)->portrait_id = PLAIN_PORTRAIT;
    unit(2)->level = 4;
    unit(2)->hp_current = 30;
    unit(2)->hp_max = 100;

    CHECK_EQ(fdps_unit_apply_heal(2, AMOUNT_TEN), 9);
    CHECK_EQ(unit(2)->hp_current, 39);
    CHECK_EQ(unit(0)->hp_current, 0);
    CHECK_EQ(unit(PATIENT)->hp_current, 10);
    CHECK_EQ(unit(3)->hp_current, 0);
}

/* The MP twin's patient: the same record 1, on 10 of 100 MP, with the HP
   fields stage() set left alone so that a stray write into them shows up. */
static void stage_mp(void)
{
    stage();
    unit(PATIENT)->mp_current = 10;
    unit(PATIENT)->mp_max = 100;
}

static void the_mp_fields_sit_where_the_word_loads_read(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_current), 0x44);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_max), 0x46);
    CHECK_EQ((int) sizeof(layout_probe.mp_current), 2);
    CHECK_EQ((int) sizeof(layout_probe.mp_max), 2);
}

/* The same roll as the heal: 10 gives exactly 9 with the bonus pinned to 0. */
static void a_restore_of_ten_rolls_nine_every_time(void)
{
    stage_mp();
    CHECK_EQ(fdps_unit_restore_mp(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->mp_current, 19);
}

/* 63 / 10 and 27 / 10 again: the base restore truncates. */
static void the_base_restore_truncates(void)
{
    stage_mp();
    CHECK_EQ(fdps_unit_restore_mp(PATIENT, AMOUNT_SEVEN), 6);
    CHECK_EQ(unit(PATIENT)->mp_current, 16);

    stage_mp();
    CHECK_EQ(fdps_unit_restore_mp(PATIENT, AMOUNT_THREE), 2);
    CHECK_EQ(unit(PATIENT)->mp_current, 12);
}

/* JLE at 00027622, and the return the whole function exists to get wrong: the
   clamped record gained 2 and the figure handed back is still the 9 rolled. */
static void the_total_is_clamped_and_the_roll_is_still_returned(void)
{
    int rolled;

    stage_mp();
    unit(PATIENT)->mp_current = 38;
    unit(PATIENT)->mp_max = 40;

    rolled = fdps_unit_restore_mp(PATIENT, AMOUNT_TEN);
    CHECK_EQ(rolled, 9);
    CHECK_EQ(unit(PATIENT)->mp_current, 40);
    CHECK_EQ(unit(PATIENT)->mp_current - 38, 2);
    CHECK_EQ(rolled != unit(PATIENT)->mp_current - 38, 1);
}

/* A unit already at full MP gains nothing and still hands back its roll --
   the case fdps_apply_item_effect_to_targets does not filter out. */
static void a_full_unit_gains_nothing_but_still_returns_its_roll(void)
{
    stage_mp();
    unit(PATIENT)->mp_current = 100;

    CHECK_EQ(fdps_unit_restore_mp(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->mp_current, 100);
}

/* No divide by the maximum anywhere in this function: a maximum of 0 pins the
   MP at 0 instead of faulting the way the heal's award would. */
static void a_maximum_of_zero_pins_the_mp_rather_than_faulting(void)
{
    stage_mp();
    unit(PATIENT)->mp_current = 0;
    unit(PATIENT)->mp_max = 0;

    CHECK_EQ(fdps_unit_restore_mp(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->mp_current, 0);
}

/* IDIV on a sign-extended dividend truncates towards zero, so -7 gives -6, and
   nothing clamps the MP at the bottom: the drained total is written back as a
   negative word. */
static void a_negative_amount_drains_mp_with_no_bottom_clamp(void)
{
    stage_mp();
    unit(PATIENT)->mp_current = 2;

    CHECK_EQ(fdps_unit_restore_mp(PATIENT, -AMOUNT_SEVEN), -6);
    CHECK_EQ(unit(PATIENT)->mp_current, -4);
}

/* The load at 000275bd is XOR EAX,EAX / MOV AX and not MOVSX, so the negative
   word the test above can leave behind reads back as 65532 rather than -4.
   The restore therefore lands far above the maximum and is clamped to it in
   one step; a signed read would have given 5. */
static void the_mp_fields_are_read_unsigned(void)
{
    stage_mp();
    unit(PATIENT)->mp_current = -4;

    CHECK_EQ(fdps_unit_restore_mp(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->mp_current, 100);
    CHECK_EQ(unit(PATIENT)->mp_current != 5, 1);
}

/* At an amount of 100 the bonus is (rand() % 100) / 10, so the roll is 90..99
   and the MP moves by exactly the roll.  Twenty draws against the interval and
   against the record, with the maximum lifted clear of the clamp. */
static void the_restore_spans_nine_tenths_to_the_full_amount(void)
{
    int draw;
    int rolled;
    int inside;
    int matched;

    inside = 0;
    matched = 0;

    for (draw = 0; draw < BAND_DRAWS; draw++) {
        stage_mp();
        unit(PATIENT)->mp_max = 200;
        rolled = fdps_unit_restore_mp(PATIENT, AMOUNT_HUNDRED);
        if (rolled >= HUNDRED_ROLL_LOW && rolled <= HUNDRED_ROLL_HIGH) {
            inside++;
        }
        if (unit(PATIENT)->mp_current == 10 + rolled) {
            matched++;
        }
    }

    CHECK_EQ(inside, BAND_DRAWS);
    CHECK_EQ(matched, BAND_DRAWS);
}

/* +0x44 is the only field the function writes: the HP pair the heal works on
   is untouched, and the experience accumulator -- which the heal ADDs to on
   exactly this fixture -- keeps its seed, because no ADD into it exists here
   at all. */
static void nothing_but_the_current_mp_is_touched(void)
{
    stage_mp();

    CHECK_EQ(fdps_unit_restore_mp(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->mp_current, 19);
    CHECK_EQ(unit(PATIENT)->mp_max, 100);
    CHECK_EQ(unit(PATIENT)->hp_current, 10);
    CHECK_EQ(unit(PATIENT)->hp_max, 100);
    CHECK_EQ(unit(PATIENT)->level, 4);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);
}

/* The index picks the record: unit 2 is restored and its three neighbours are
   left where they were. */
static void each_index_selects_its_own_record_for_mp(void)
{
    stage_mp();
    unit(2)->mp_current = 30;
    unit(2)->mp_max = 100;

    CHECK_EQ(fdps_unit_restore_mp(2, AMOUNT_TEN), 9);
    CHECK_EQ(unit(2)->mp_current, 39);
    CHECK_EQ(unit(0)->mp_current, 0);
    CHECK_EQ(unit(PATIENT)->mp_current, 10);
    CHECK_EQ(unit(3)->mp_current, 0);
}

/* The fdps_unit_collect_known_spells cases below are read off 00027840: CMP
   dword ptr [EBP-0xc],0x5 for the five bytes walked, byte ptr [EDX+0x1a] for
   where they are, CMP dword ptr [EBP-0x14],0x8 with SAR EAX,CL / TEST AL,0x1
   for the bit order inside each byte, SHL AL,0x3 / ADD AL for the id a set bit
   stands for, the counter at [EBP-0x10] that is advanced only inside the bit
   test and indexes the buffer as well as being returned, and CMP dword ptr
   [EBP+0x18],0x0 at 000278aa for the NULL buffer mode.  The five bytes and
   nothing on either side of them is what the outer walk covers, and both
   neighbours are staged with 0xff below to prove it. */

/* CMP dword ptr [EBP-0xc],0x5 -- and 5 * 8 ids, so the span is 0..39. */
#define SPELL_BITMAP_BYTES 5
#define SPELL_ID_COUNT 40

/* The buffer is given more room than the 40 the function can write so that a
   write past the count is visible; every byte starts as a value no spell id
   can be. */
#define OUT_ROOM 48
#define OUT_SENTINEL 0xee

static unsigned char spell_ids[OUT_ROOM];

static void stage_spells(void)
{
    int i;

    stage();
    for (i = 0; i < OUT_ROOM; i++) {
        spell_ids[i] = OUT_SENTINEL;
    }
}

static void the_spell_bitmap_sits_where_the_byte_loads_read(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, spells_known_bitmap),
             0x1a);
    CHECK_EQ((int) sizeof(layout_probe.spells_known_bitmap),
             SPELL_BITMAP_BYTES);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, race), 0x1f);
}

/* A unit that knows nothing counts nothing and writes nothing. */
static void an_empty_bitmap_collects_no_ids(void)
{
    stage_spells();

    CHECK_EQ(fdps_unit_collect_known_spells(PATIENT, spell_ids), 0);
    CHECK_EQ(spell_ids[0], OUT_SENTINEL);
}

/* Bit 0 of byte 0 is id 0 and bit 7 of byte 0 is id 7: the walk inside a byte
   runs from the least significant bit upwards, so the id is the bit number and
   not 7 minus it. */
static void the_bits_of_a_byte_are_walked_least_significant_first(void)
{
    stage_spells();
    unit(PATIENT)->spells_known_bitmap[0] = 0x01;
    CHECK_EQ(fdps_unit_collect_known_spells(PATIENT, spell_ids), 1);
    CHECK_EQ(spell_ids[0], 0);

    stage_spells();
    unit(PATIENT)->spells_known_bitmap[0] = 0x80;
    CHECK_EQ(fdps_unit_collect_known_spells(PATIENT, spell_ids), 1);
    CHECK_EQ(spell_ids[0], 7);
}

/* SHL AL,0x3: each byte contributes 8 to the id, so bit 0 of byte 1 is id 8
   and bit 7 of byte 4 is id 39 -- the top of the whole span. */
static void each_byte_contributes_eight_to_the_id(void)
{
    stage_spells();
    unit(PATIENT)->spells_known_bitmap[1] = 0x01;
    CHECK_EQ(fdps_unit_collect_known_spells(PATIENT, spell_ids), 1);
    CHECK_EQ(spell_ids[0], 8);

    stage_spells();
    unit(PATIENT)->spells_known_bitmap[4] = 0x80;
    CHECK_EQ(fdps_unit_collect_known_spells(PATIENT, spell_ids), 1);
    CHECK_EQ(spell_ids[0], SPELL_ID_COUNT - 1);
}

/* The counter advances only on a set bit, so the ids land packed from element
   0 in ascending order -- the buffer is a list, not an array indexed by spell
   id.  Bits 1 and 3 of byte 0 and bit 0 of byte 2 give 1, 3 and 16 in the
   first three elements, and the fourth is still the sentinel. */
static void the_ids_are_packed_in_ascending_order(void)
{
    stage_spells();
    unit(PATIENT)->spells_known_bitmap[0] = 0x0a;
    unit(PATIENT)->spells_known_bitmap[2] = 0x01;

    CHECK_EQ(fdps_unit_collect_known_spells(PATIENT, spell_ids), 3);
    CHECK_EQ(spell_ids[0], 1);
    CHECK_EQ(spell_ids[1], 3);
    CHECK_EQ(spell_ids[2], 16);
    CHECK_EQ(spell_ids[3], OUT_SENTINEL);
}

/* All forty bits set: forty ids, each equal to its own position, and nothing
   written at element 40. */
static void every_bit_set_yields_all_forty_ids_in_order(void)
{
    int i;
    int in_order;

    stage_spells();
    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        unit(PATIENT)->spells_known_bitmap[i] = 0xff;
    }

    CHECK_EQ(fdps_unit_collect_known_spells(PATIENT, spell_ids),
             SPELL_ID_COUNT);

    in_order = 0;
    for (i = 0; i < SPELL_ID_COUNT; i++) {
        if (spell_ids[i] == (unsigned char) i) {
            in_order++;
        }
    }
    CHECK_EQ(in_order, SPELL_ID_COUNT);
    CHECK_EQ(spell_ids[SPELL_ID_COUNT], OUT_SENTINEL);
}

/* CMP dword ptr [EBP+0x18],0x0 sits inside the bit test, not around the walk:
   a NULL buffer counts every set bit exactly as a real one does and writes
   nothing at all. */
static void a_null_buffer_still_counts_every_set_bit(void)
{
    stage_spells();
    unit(PATIENT)->spells_known_bitmap[0] = 0x0a;
    unit(PATIENT)->spells_known_bitmap[2] = 0x01;
    unit(PATIENT)->spells_known_bitmap[4] = 0x80;

    CHECK_EQ(fdps_unit_collect_known_spells(PATIENT, (unsigned char *) 0), 4);
    CHECK_EQ(spell_ids[0], OUT_SENTINEL);
    CHECK_EQ(spell_ids[1], OUT_SENTINEL);
}

/* The walk covers record offsets 0x1a through 0x1e and neither neighbour:
   inventory_slots[15] at 0x19 and race at 0x1f are both filled with 0xff, and
   the answer is still the one bit that is inside the bitmap. */
static void neither_neighbouring_field_is_walked(void)
{
    stage_spells();
    unit(PATIENT)->inventory_slots[15] = 0xff;
    unit(PATIENT)->race = 0xff;
    unit(PATIENT)->spells_known_bitmap[0] = 0x04;

    CHECK_EQ(fdps_unit_collect_known_spells(PATIENT, spell_ids), 1);
    CHECK_EQ(spell_ids[0], 2);
    CHECK_EQ(spell_ids[1], OUT_SENTINEL);
}

/* Nothing in the record is written and no global is touched: the bitmap reads
   back unchanged and the experience accumulator, which the heal on this same
   fixture ADDs to, keeps its seed. */
static void the_record_is_only_read(void)
{
    stage_spells();
    unit(PATIENT)->spells_known_bitmap[0] = 0x0a;
    unit(PATIENT)->spells_known_bitmap[3] = 0x40;

    CHECK_EQ(fdps_unit_collect_known_spells(PATIENT, spell_ids), 3);
    CHECK_EQ(unit(PATIENT)->spells_known_bitmap[0], 0x0a);
    CHECK_EQ(unit(PATIENT)->spells_known_bitmap[3], 0x40);
    CHECK_EQ(unit(PATIENT)->hp_current, 10);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);
}

/* The index picks the record: unit 2's bitmap is the one read, and unit 1's,
   which holds a different pattern, is not. */
static void each_index_selects_its_own_record_for_spells(void)
{
    stage_spells();
    unit(PATIENT)->spells_known_bitmap[0] = 0xff;
    unit(2)->spells_known_bitmap[1] = 0x03;

    CHECK_EQ(fdps_unit_collect_known_spells(2, spell_ids), 2);
    CHECK_EQ(spell_ids[0], 8);
    CHECK_EQ(spell_ids[1], 9);
    CHECK_EQ(spell_ids[2], OUT_SENTINEL);
}

/* The fdps_unit_apply_damage fixture.
 *
 * IMUL EAX,dword ptr [EBP+0x14],0xa in fdps_get_enemy_record: ENEMYDAT.DAT
 * records are ten bytes.  Four are staged so that a wrong portrait bias lands
 * on a neighbour that is visibly different rather than on nothing. */
#define ENEMY_RECORD_STRIDE 0x0a
#define STAGE_ENEMIES 4

/* The victim's portrait id is 0x3c + 1, so the record the bias has to reach is
   the second one and the three around it are wrong answers. */
#define VICTIM_ENEMY_INDEX 1

/* An exp_reward and a level whose product, 100, is the same as the fixture's
   maximum HP, so a prorated award is exactly the damage rolled and an
   unprorated one is exactly 100.  The two are impossible to confuse. */
#define VICTIM_EXP_REWARD 20
#define VICTIM_LEVEL 5
#define VICTIM_AWARD_UNPRORATED 100

/* What the neighbouring enemy records are filled with: a value that could not
   be mistaken for VICTIM_EXP_REWARD in any award. */
#define WRONG_EXP_REWARD 99

/* CMP byte ptr [EAX+0x6],0x0: side 0 is the ENEMYDAT side that pays, and
   deploy.c's PLAYER_SIDE is 2.  Side 1 is tested as well because the branch is
   an equality on 0 and not a truth test of "player or not". */
#define ENEMY_SIDE 0
#define PLAYER_SIDE 2
#define OTHER_NON_ENEMY_SIDE 1

static unsigned char enemy_block[STAGE_ENEMIES * ENEMY_RECORD_STRIDE];

/* Only ever read for its field offsets. */
static struct fdps_enemy_data enemy_layout_probe;

static struct fdps_enemy_data *enemy(int enemy_index)
{
    return (struct fdps_enemy_data *)
        (enemy_block + enemy_index * ENEMY_RECORD_STRIDE);
}

/* A player-side target on 50 of 100 HP: far enough from both ends that neither
   the clamp nor a wrap can fire unless a test asks for it, and on a side that
   credits no experience so the accumulator stays at its seed. */
static void stage_damage(void)
{
    int i;

    stage();
    for (i = 0; i < (int) sizeof(enemy_block); i++) {
        enemy_block[i] = 0;
    }
    data_fdps_battle_enemy_data_table_ptr = enemy_block;

    unit(PATIENT)->side = PLAYER_SIDE;
    unit(PATIENT)->hp_current = 50;
    unit(PATIENT)->hp_max = 100;
}

/* The same target moved to side 0 and given the enemy portrait id, level and
   record the award is built out of, on full HP. */
static void stage_damage_enemy(void)
{
    stage_damage();

    unit(PATIENT)->side = ENEMY_SIDE;
    unit(PATIENT)->portrait_id =
        (unsigned char) (FIRST_ENEMY_PORTRAIT + VICTIM_ENEMY_INDEX);
    unit(PATIENT)->level = VICTIM_LEVEL;
    unit(PATIENT)->hp_current = 100;

    enemy(VICTIM_ENEMY_INDEX)->exp_reward = VICTIM_EXP_REWARD;
}

static void the_enemy_record_layout_matches_the_offsets_read(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 0x06);
    CHECK_EQ((int) sizeof(enemy_layout_probe), ENEMY_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_enemy_data, exp_reward), 0x09);
    CHECK_EQ((int) sizeof(enemy_layout_probe.exp_reward), 1);
}

/* base_damage * 9 / 10 with the bonus pinned to zero: 10 takes off exactly 9,
   and a player-side target credits nothing. */
static void a_hit_of_ten_rolls_nine_every_time(void)
{
    stage_damage();

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 41);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);
}

/* 63 / 10 and 27 / 10: the base damage truncates, it does not round. */
static void the_base_damage_truncates(void)
{
    stage_damage();
    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_SEVEN), 6);
    CHECK_EQ(unit(PATIENT)->hp_current, 44);

    stage_damage();
    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_THREE), 2);
    CHECK_EQ(unit(PATIENT)->hp_current, 48);
}

/* IDIV on a sign-extended dividend truncates towards zero, so -7 gives -6 and
   not -7, and the bonus term is still identically 0 because 99 * 7 is under a
   thousand however the signs fall.  A negative nominal figure therefore RAISES
   the HP, with no clamp at hp_max on the way up. */
static void a_negative_nominal_figure_truncates_towards_zero_and_heals(void)
{
    stage_damage();

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, -AMOUNT_SEVEN), -6);
    CHECK_EQ(unit(PATIENT)->hp_current, 56);
}

/* JGE at 000284e1: the stored HP is clamped up to 0, and the figure handed back
   is still the whole 9 that was rolled.  This is the case every caller floats
   over the target, so an overkill on a unit with 5 HP left shows a 9. */
static void an_overkill_clamps_the_hp_but_returns_the_whole_roll(void)
{
    int rolled;

    stage_damage();
    unit(PATIENT)->hp_current = 5;

    rolled = fdps_unit_apply_damage(PATIENT, AMOUNT_TEN);
    CHECK_EQ(rolled, 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 0);
    CHECK_EQ(5 - unit(PATIENT)->hp_current, 5);
    CHECK_EQ(rolled != 5 - unit(PATIENT)->hp_current, 1);
}

/* At a nominal 100 the bonus is (rand() % 100) / 10, so the roll is 90..99 and
   the HP moves down by exactly the roll.  Twenty draws, each checked against
   the interval and against the record, with the starting HP lifted clear of the
   clamp so the top of the band cannot hide itself. */
static void the_damage_roll_spans_nine_tenths_to_the_full_figure(void)
{
    int draw;
    int rolled;
    int inside;
    int matched;

    inside = 0;
    matched = 0;

    for (draw = 0; draw < BAND_DRAWS; draw++) {
        stage_damage();
        unit(PATIENT)->hp_current = 200;
        unit(PATIENT)->hp_max = 200;
        rolled = fdps_unit_apply_damage(PATIENT, AMOUNT_HUNDRED);
        if (rolled >= HUNDRED_ROLL_LOW && rolled <= HUNDRED_ROLL_HIGH) {
            inside++;
        }
        if (unit(PATIENT)->hp_current == 200 - rolled) {
            matched++;
        }
    }

    CHECK_EQ(inside, BAND_DRAWS);
    CHECK_EQ(matched, BAND_DRAWS);
}

/* The two word loads are XOR EAX,EAX / MOV AX and not MOVSX, so a unit whose
   hp_current has been driven negative reads back as 65532 rather than -4.  The
   clamp at zero therefore never fires and the subtraction is written back as
   the negative word -13; a signed read would have clamped it to 0. */
static void the_hp_fields_are_read_unsigned(void)
{
    stage_damage();
    unit(PATIENT)->hp_current = -4;

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, -13);
    CHECK_EQ(unit(PATIENT)->hp_current != 0, 1);
}

/* The same zero extension on hp_max: a negative maximum is 65532 in the divide,
   so the award truncates to 0 rather than coming out as the -225 a signed read
   would have produced.  The target survives, so the divide really does run. */
static void a_negative_maximum_hp_divides_unsigned(void)
{
    stage_damage_enemy();
    unit(PATIENT)->hp_max = -4;

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 91);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);
}

/* A surviving enemy prorates: exp_reward 20 times level 5 is 100, scaled by the
   9 rolled over the maximum of 100, so the award is 9. */
static void a_surviving_enemy_prorates_the_award_by_the_damage(void)
{
    stage_damage_enemy();

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 91);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 9);
}

/* JZ at 00028534 jumps past the divide: a target the hit clamped to 0 keeps the
   whole of exp_reward * level, however little of the roll was needed.  Five HP
   taken off by a rolled 9 pays the full 100 rather than the 9 the proration
   would have given. */
static void a_killed_enemy_keeps_the_whole_award(void)
{
    stage_damage_enemy();
    unit(PATIENT)->hp_current = 5;

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit,
             XP_SEED + VICTIM_AWARD_UNPRORATED);
}

/* The test is on the CLAMPED HP and not on whether the hit did the killing, so
   an enemy already lying at 0 pays in full again on every further hit. */
static void an_enemy_already_at_zero_pays_in_full_again(void)
{
    stage_damage_enemy();
    unit(PATIENT)->hp_current = 0;

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit,
             XP_SEED + VICTIM_AWARD_UNPRORATED);
}

/* The divide is the LAST step, applied to the whole product.  exp_reward 3
   times level 5 is 15, scaled by 9 over 100, which is 1; had the divide come
   before the multiply it would have been 0. */
static void the_award_divides_the_whole_product_last_for_damage(void)
{
    stage_damage_enemy();
    enemy(VICTIM_ENEMY_INDEX)->exp_reward = 3;

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 1);
}

/* SUB EAX,0x3c: the record is picked by the portrait id minus 0x3c, so a
   portrait of 0x3d reaches record 1 and not record 0 or 61.  Every other staged
   record carries a reward that would be visible if the bias were wrong. */
static void the_enemy_record_is_the_portrait_id_less_0x3c(void)
{
    int i;

    stage_damage_enemy();
    for (i = 0; i < STAGE_ENEMIES; i++) {
        if (i != VICTIM_ENEMY_INDEX) {
            enemy(i)->exp_reward = WRONG_EXP_REWARD;
        }
    }

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 9);
}

/* ADD dword ptr [0x00069cec],EAX: two hits in a row leave the sum of both
   awards, not the second one.  Each hit here takes 9 off a full 100, so each
   prorated award is 9. */
static void the_damage_award_accumulates_rather_than_replacing(void)
{
    stage_damage_enemy();

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 9);

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 82);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 18);
}

/* The side test is an equality on 0, so neither the player's side 2 nor side 1
   credits anything -- the accumulator keeps its seed rather than being cleared,
   and the enemy record is never reached even though the portrait id would have
   indexed one. */
static void only_side_zero_credits_experience(void)
{
    stage_damage_enemy();
    unit(PATIENT)->side = PLAYER_SIDE;

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 91);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);

    stage_damage_enemy();
    unit(PATIENT)->side = OTHER_NON_ENEMY_SIDE;

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);
}

/* +0x40 is the only field the function writes: the maximum, the level, the
   portrait id and the side all read back as they were staged. */
static void nothing_but_the_current_hp_is_written(void)
{
    stage_damage_enemy();

    CHECK_EQ(fdps_unit_apply_damage(PATIENT, AMOUNT_TEN), 9);
    CHECK_EQ(unit(PATIENT)->hp_current, 91);
    CHECK_EQ(unit(PATIENT)->hp_max, 100);
    CHECK_EQ(unit(PATIENT)->level, VICTIM_LEVEL);
    CHECK_EQ(unit(PATIENT)->portrait_id,
             FIRST_ENEMY_PORTRAIT + VICTIM_ENEMY_INDEX);
    CHECK_EQ(unit(PATIENT)->side, ENEMY_SIDE);
    CHECK_EQ(enemy(VICTIM_ENEMY_INDEX)->exp_reward, VICTIM_EXP_REWARD);
}

/* The index picks the record: unit 2 is hit and its three neighbours are left
   exactly where they were. */
static void each_index_selects_its_own_record_for_damage(void)
{
    stage_damage();
    unit(2)->side = PLAYER_SIDE;
    unit(2)->hp_current = 30;
    unit(2)->hp_max = 100;

    CHECK_EQ(fdps_unit_apply_damage(2, AMOUNT_TEN), 9);
    CHECK_EQ(unit(2)->hp_current, 21);
    CHECK_EQ(unit(0)->hp_current, 0);
    CHECK_EQ(unit(PATIENT)->hp_current, 50);
    CHECK_EQ(unit(3)->hp_current, 0);
}

/* The class ids fdps_unit_is_ailment_immune accepts, transcribed one at a time
   off the four comparisons at 000290b1..000290d1 -- CMP 0x19 / JZ, CMP 0x21 /
   JL with CMP 0x22 / JLE, CMP 0x24 / JL with CMP 0x26 / JLE -- and not derived
   from the emitted C.  The names come from the PROMAP.DAT class table in
   assets/classes.md. */
#define CLASS_MACHINE_SOLDIER 0x19  /* 機兵 */
#define CLASS_GUARDIAN_BEAST 0x21   /* 守護獸 */
#define CLASS_GENERAL 0x22          /* 將軍 */
#define CLASS_UNNAMED 0x24          /* ？？ */
#define CLASS_EVIL_SPIRIT 0x25      /* 惡靈 */
#define CLASS_LIVING_CORPSE 0x26    /* 活屍 */

/* The class ids either side of each accepted run, which is where a range test
   written one id too wide would show. */
#define CLASS_MACHINE_COUNT_BELOW 0x18 /* 機械大師 */
#define CLASS_DEMON_GOD 0x1a           /* 魔神, immediately above the 0x19 */
#define CLASS_MONSTER 0x20             /* 妖魔, immediately below 0x21 */
#define CLASS_MERCENARY 0x23           /* 傭兵, the hole between the two runs */
#define CLASS_PAST_UPPER_SPAN 0x27     /* the unnamed 40th PROMAP.DAT class */

/* PROMAP.DAT holds 0x28 classes; the sweep runs a little past the end of the
   table so that a test written against the data rather than the code would
   still be caught. */
#define CLASS_SWEEP_END 0x30

/* A class no test accepts, for the cases that are about the portrait id. */
#define PLAIN_CLASS 0x00 /* 劍士 */

/* CMP 0x3c / JL and CMP 0x44 / JLE at 000290d7, on the portrait id. */
#define LAST_IMMUNE_PORTRAIT 0x44
#define MID_IMMUNE_PORTRAIT 0x40
#define FIRST_PORTRAIT_PAST_IMMUNE 0x45

/* Sentinels in the three ailment bytes the callers write, status_timers[3..5]
   at record offsets 0x25, 0x26 and 0x27. */
#define POISON_TIMER 3
#define PARALYSIS_TIMER 4
#define SEAL_TIMER 5
#define TIMER_SENTINEL 0x77

static const unsigned char immune_class_ids[] = {
    CLASS_MACHINE_SOLDIER,
    CLASS_GUARDIAN_BEAST,
    CLASS_GENERAL,
    CLASS_UNNAMED,
    CLASS_EVIL_SPIRIT,
    CLASS_LIVING_CORPSE
};

#define IMMUNE_CLASS_COUNT 6

static int class_is_in_the_transcribed_set(int class_code)
{
    int i;

    for (i = 0; i < IMMUNE_CLASS_COUNT; i++) {
        if ((int) immune_class_ids[i] == class_code) {
            return 1;
        }
    }

    return 0;
}

/* Record 1 as an ordinary roster unit: class 0 and portrait 5 are outside every
   accepted run, so the fixture answers 0 until a test moves one of them. */
static void stage_immunity(void)
{
    stage();

    unit(PATIENT)->clazz = PLAIN_CLASS;
    unit(PATIENT)->portrait_id = PLAIN_PORTRAIT;
}

/* MOV AL,byte ptr [EDX+0x20] for the class and MOV AL,byte ptr [EDX+0x7] for
   the portrait id: two separate byte fields, and the whole point of the
   function is that they are not the same one. */
static void the_two_fields_sit_where_the_byte_loads_read(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, clazz), 0x20);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 0x07);
    CHECK_EQ((int) sizeof(layout_probe.clazz), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
}

static void an_ordinary_roster_unit_is_not_immune(void)
{
    stage_immunity();

    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);
}

/* The 0x19 is an equality on its own rather than the bottom of a run, so both
   neighbours have to answer 0. */
static void the_machine_soldier_class_is_immune_on_its_own(void)
{
    stage_immunity();
    unit(PATIENT)->clazz = CLASS_MACHINE_COUNT_BELOW;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);

    unit(PATIENT)->clazz = CLASS_MACHINE_SOLDIER;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    unit(PATIENT)->clazz = CLASS_DEMON_GOD;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);
}

/* CMP 0x21 / JL and CMP 0x22 / JLE: both ends are inclusive and 妖魔 below is
   out. */
static void the_lower_immune_class_run_is_0x21_through_0x22(void)
{
    stage_immunity();
    unit(PATIENT)->clazz = CLASS_MONSTER;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);

    unit(PATIENT)->clazz = CLASS_GUARDIAN_BEAST;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    unit(PATIENT)->clazz = CLASS_GENERAL;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);
}

/* CMP 0x24 / JL and CMP 0x26 / JLE: both ends inclusive, and the class the data
   file carries past the end of the run is out. */
static void the_upper_immune_class_run_is_0x24_through_0x26(void)
{
    stage_immunity();
    unit(PATIENT)->clazz = CLASS_UNNAMED;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    unit(PATIENT)->clazz = CLASS_EVIL_SPIRIT;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    unit(PATIENT)->clazz = CLASS_LIVING_CORPSE;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    unit(PATIENT)->clazz = CLASS_PAST_UPPER_SPAN;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);
}

/* The hole.  0x23 傭兵 sits between the two runs and the assembly's two
   separate range tests step straight over it; a single 0x21..0x26 test would
   answer 1 here. */
static void the_mercenary_class_between_the_two_runs_is_not_immune(void)
{
    stage_immunity();
    unit(PATIENT)->clazz = CLASS_MERCENARY;

    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);
}

/* Every class id from 0 to 0x2f against the set transcribed above, so no
   accepted id is missing and no rejected one has crept in. */
static void every_class_id_matches_the_transcribed_set(void)
{
    int class_code;

    for (class_code = 0; class_code < CLASS_SWEEP_END; class_code++) {
        stage_immunity();
        unit(PATIENT)->clazz = (unsigned char) class_code;

        CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT),
                 class_is_in_the_transcribed_set(class_code));
    }
}

/* CMP 0x3c / JL and CMP 0x44 / JLE on the portrait id: both ends inclusive,
   which is ENEMYDAT.DAT records 0 through 8. */
static void the_enemy_portrait_run_is_0x3c_through_0x44(void)
{
    stage_immunity();
    unit(PATIENT)->portrait_id = LAST_ROSTER_PORTRAIT;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);

    unit(PATIENT)->portrait_id = FIRST_ENEMY_PORTRAIT;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    unit(PATIENT)->portrait_id = MID_IMMUNE_PORTRAIT;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    unit(PATIENT)->portrait_id = LAST_IMMUNE_PORTRAIT;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    unit(PATIENT)->portrait_id = FIRST_PORTRAIT_PAST_IMMUNE;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);
}

/* The two halves read different fields and neither value means anything in the
   other's: a class byte holding 0x3c is not immune, and a portrait id holding
   0x21 is not either. */
static void neither_run_is_read_from_the_other_field(void)
{
    stage_immunity();
    unit(PATIENT)->clazz = FIRST_ENEMY_PORTRAIT;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);

    stage_immunity();
    unit(PATIENT)->portrait_id = CLASS_GUARDIAN_BEAST;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);
}

/* The four tests are ORed, so an immune class with a roster portrait and a
   roster class with an enemy portrait both answer 1. */
static void either_test_alone_makes_the_unit_immune(void)
{
    stage_immunity();
    unit(PATIENT)->clazz = CLASS_MACHINE_SOLDIER;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    stage_immunity();
    unit(PATIENT)->portrait_id = FIRST_ENEMY_PORTRAIT;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    stage_immunity();
    unit(PATIENT)->clazz = CLASS_MACHINE_SOLDIER;
    unit(PATIENT)->portrait_id = FIRST_ENEMY_PORTRAIT;
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);
}

/* The function reads two bytes and writes nothing -- least of all the three
   ailment bytes at +0x25..+0x27 that its callers write once it has answered. */
static void the_immunity_test_writes_nothing(void)
{
    stage_immunity();
    unit(PATIENT)->clazz = CLASS_LIVING_CORPSE;
    unit(PATIENT)->status_timers[POISON_TIMER] = TIMER_SENTINEL;
    unit(PATIENT)->status_timers[PARALYSIS_TIMER] = TIMER_SENTINEL;
    unit(PATIENT)->status_timers[SEAL_TIMER] = TIMER_SENTINEL;

    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 1);

    CHECK_EQ(unit(PATIENT)->clazz, CLASS_LIVING_CORPSE);
    CHECK_EQ(unit(PATIENT)->portrait_id, PLAIN_PORTRAIT);
    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], TIMER_SENTINEL);
    CHECK_EQ(unit(PATIENT)->status_timers[PARALYSIS_TIMER], TIMER_SENTINEL);
    CHECK_EQ(unit(PATIENT)->status_timers[SEAL_TIMER], TIMER_SENTINEL);
    CHECK_EQ(unit(PATIENT)->hp_current, 10);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);
}

/* The index picks the record: only unit 2 carries an immune class and only
   unit 2 answers 1. */
static void each_index_selects_its_own_record_for_immunity(void)
{
    stage_immunity();
    unit(2)->clazz = CLASS_GUARDIAN_BEAST;

    CHECK_EQ(fdps_unit_is_ailment_immune(2), 1);
    CHECK_EQ(fdps_unit_is_ailment_immune(0), 0);
    CHECK_EQ(fdps_unit_is_ailment_immune(PATIENT), 0);
    CHECK_EQ(fdps_unit_is_ailment_immune(3), 0);
}

/* fdps_unit_inflict_random_ailments at 00028ee0, read off CMP dword ptr
   [EBP-0x4],0x3 for the three passes, MOV byte ptr [EBX+0x25],DL with EBX =
   record + i for the slot each pass writes, CMP EDX,0x14 / JGE for the chance,
   the CALL 0x00029080 that sits after that JGE rather than in front of it, and
   MOV EBX,0x2 / IDIV / ADD EDX,0x2 for the duration.  None of it is read off
   the emitted C.

   HOW THE RANDOMNESS IS TAKEN OUT.  Nothing here divides the roll away the way
   the heal band does, so the cases below pin the function three other ways.
   Two of them need no seed at all: an immune unit can never be written to
   whatever comes out of the stream, and a duration that is written is 2 or 3
   however the stream fell.  The third reseeds with srand and either walks the
   seeds until one produces the exact remainder a boundary needs, or advances
   the same stream by hand and compares -- which is how the draw count, and so
   the position of the immunity call, is pinned. */
#define AILMENT_SLOTS 3

/* CMP EDX,0x14: the remainder that just misses and the one that just lands. */
#define AILMENT_CHANCE 0x14
#define REMAINDER_THAT_LANDS 0x13
#define REMAINDER_THAT_MISSES 0x14

/* ADD EDX,0x2 onto rand() % 2. */
#define TURNS_LOW 2
#define TURNS_HIGH 3

/* Enough calls that a 20% roll landing on none of 3 * 200 chances is not a
   thing that happens. */
#define AILMENT_TRIALS 200

/* Seeds walked when a case needs one whose first draw has a chosen remainder,
   and the arbitrary seed the hand-advanced comparisons start from. */
#define SEED_SEARCH_LIMIT 20000
#define AILMENT_SEED 4177

/* Seeds compared against the hand-built model. */
#define MODEL_SEEDS 20

static unsigned char ailment_snapshot[STAGE_UNITS * UNIT_RECORD_STRIDE];

/* Record 1 with an ordinary class and portrait, so nothing about it is immune,
   and all six timers clear. */
static void stage_ailment(void)
{
    stage_immunity();
    unit(PATIENT)->status_timers[POISON_TIMER] = 0;
    unit(PATIENT)->status_timers[PARALYSIS_TIMER] = 0;
    unit(PATIENT)->status_timers[SEAL_TIMER] = 0;
}

/* The first seed at or after 1 whose first draw leaves the wanted remainder
   modulo 100, or 0 when the walk found none. */
static int seed_whose_first_draw_leaves(int remainder)
{
    int seed;

    for (seed = 1; seed < SEED_SEARCH_LIMIT; seed++) {
        srand(seed);
        if (rand() % 100 == remainder) {
            return seed;
        }
    }

    return 0;
}

/* MOV EBX,[EBP-0x8] / ADD EBX,[EBP-0x4] / MOV byte ptr [EBX+0x25],DL: the
   three bytes the loop stores into are record +0x25, +0x26 and +0x27, and
   status_timers starts at +0x22. */
static void the_three_ailment_slots_sit_where_the_stores_land(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers) +
             POISON_TIMER, 0x25);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers) +
             PARALYSIS_TIMER, 0x26);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers) +
             SEAL_TIMER, 0x27);
    CHECK_EQ((int) sizeof(layout_probe.status_timers), 6);
}

/* TEST EAX,EAX / JZ at 00028f36: the store is reached only on a 0 from
   fdps_unit_is_ailment_immune.  Either route into immunity blocks all three
   slots, for as many calls as are made. */
static void an_immune_unit_is_never_given_an_ailment(void)
{
    int trial;

    stage_ailment();
    unit(PATIENT)->clazz = CLASS_MACHINE_SOLDIER;
    unit(PATIENT)->status_timers[POISON_TIMER] = TIMER_SENTINEL;
    unit(PATIENT)->status_timers[PARALYSIS_TIMER] = TIMER_SENTINEL;
    unit(PATIENT)->status_timers[SEAL_TIMER] = TIMER_SENTINEL;

    for (trial = 0; trial < AILMENT_TRIALS; trial++) {
        fdps_unit_inflict_random_ailments(PATIENT);
    }

    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], TIMER_SENTINEL);
    CHECK_EQ(unit(PATIENT)->status_timers[PARALYSIS_TIMER], TIMER_SENTINEL);
    CHECK_EQ(unit(PATIENT)->status_timers[SEAL_TIMER], TIMER_SENTINEL);

    stage_ailment();
    unit(PATIENT)->portrait_id = FIRST_ENEMY_PORTRAIT;

    for (trial = 0; trial < AILMENT_TRIALS; trial++) {
        fdps_unit_inflict_random_ailments(PATIENT);
    }

    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], 0);
    CHECK_EQ(unit(PATIENT)->status_timers[PARALYSIS_TIMER], 0);
    CHECK_EQ(unit(PATIENT)->status_timers[SEAL_TIMER], 0);
}

/* The CALL to rand at 00028f12 is at the top of every pass and the CALL to
   fdps_unit_is_ailment_immune at 00028f2e is behind the JGE, so an immune unit
   takes exactly three draws and never a fourth: three because the chance roll
   is made whatever the unit is, and never four because the duration draw is
   past the immunity test.  Hoisting the immunity test out of the loop would
   take none, and putting it in front of the chance roll would take none
   either. */
static void an_immune_unit_still_draws_three_numbers(void)
{
    int after_call;
    int by_hand;
    int first;
    int second;

    /* The stream has to advance for any of this to mean anything. */
    srand(AILMENT_SEED);
    first = rand();
    second = rand();
    CHECK_EQ(first == second, 0);

    stage_ailment();
    unit(PATIENT)->clazz = CLASS_MACHINE_SOLDIER;

    srand(AILMENT_SEED);
    fdps_unit_inflict_random_ailments(PATIENT);
    after_call = rand();

    srand(AILMENT_SEED);
    rand();
    rand();
    rand();
    by_hand = rand();

    CHECK_EQ(after_call, by_hand);
}

/* CMP EDX,0x14 / JGE skips the slot, so remainder 19 lands and remainder 20
   does not.  The seeds are walked for rather than assumed, so the case says
   which remainder it is testing and not which number the CRT happened to
   produce. */
static void the_chance_is_the_remainder_under_twenty(void)
{
    int seed_that_lands;
    int seed_that_misses;

    seed_that_lands = seed_whose_first_draw_leaves(REMAINDER_THAT_LANDS);
    seed_that_misses = seed_whose_first_draw_leaves(REMAINDER_THAT_MISSES);

    CHECK_EQ(seed_that_lands != 0, 1);
    CHECK_EQ(seed_that_misses != 0, 1);

    stage_ailment();
    srand(seed_that_lands);
    fdps_unit_inflict_random_ailments(PATIENT);
    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER] >= TURNS_LOW, 1);

    stage_ailment();
    srand(seed_that_misses);
    fdps_unit_inflict_random_ailments(PATIENT);
    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], 0);

    /* Remainder 0 is inside the run as well, so the test is a < and not a
       window. */
    seed_that_lands = seed_whose_first_draw_leaves(0);
    CHECK_EQ(seed_that_lands != 0, 1);

    stage_ailment();
    srand(seed_that_lands);
    fdps_unit_inflict_random_ailments(PATIENT);
    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER] >= TURNS_LOW, 1);
}

/* rand() % 2 + 2: a slot is left at 0 or holds 2 or 3, and never 1 and never a
   count above 3.  Two hundred calls, each on a cleared record, and the writes
   are counted so the case cannot pass by never landing a roll. */
static void every_landed_roll_writes_two_or_three_turns(void)
{
    int trial;
    int slot;
    int value;
    int writes;
    int out_of_range;

    writes = 0;
    out_of_range = 0;

    for (trial = 0; trial < AILMENT_TRIALS; trial++) {
        stage_ailment();
        fdps_unit_inflict_random_ailments(PATIENT);

        for (slot = POISON_TIMER; slot <= SEAL_TIMER; slot++) {
            value = unit(PATIENT)->status_timers[slot];
            if (value != 0) {
                writes++;
                if (value != TURNS_LOW && value != TURNS_HIGH) {
                    out_of_range++;
                }
            }
        }
    }

    CHECK_EQ(out_of_range, 0);
    CHECK_EQ(writes > 0, 1);
}

/* The whole loop, rebuilt from the assembly and run against the same seeded
   stream: one draw per pass, a second draw only when that pass rolled under
   0x14, the remainder of that second draw plus 2 stored into slot 3 + i, and a
   pass that missed leaving its slot alone.  Twenty seeds, and the number of
   draws each one should have consumed is checked as well by comparing the next
   value out of the stream against the same stream advanced by hand. */
static void the_record_matches_the_loop_read_off_the_assembly(void)
{
    unsigned char expected[AILMENT_SLOTS];
    int seed;
    int slot;
    int draws;
    int step;
    int after_call;
    int by_hand;
    int mismatches;
    int modelled_writes;

    mismatches = 0;
    modelled_writes = 0;

    for (seed = 1; seed <= MODEL_SEEDS; seed++) {
        srand(seed);
        draws = 0;

        for (slot = 0; slot < AILMENT_SLOTS; slot++) {
            expected[slot] = 0;
        }

        for (slot = 0; slot < AILMENT_SLOTS; slot++) {
            draws++;
            if (rand() % 100 < AILMENT_CHANCE) {
                draws++;
                expected[slot] = (unsigned char) (rand() % 2 + 2);
                modelled_writes++;
            }
        }

        stage_ailment();
        srand(seed);
        fdps_unit_inflict_random_ailments(PATIENT);
        after_call = rand();

        for (slot = 0; slot < AILMENT_SLOTS; slot++) {
            if (unit(PATIENT)->status_timers[POISON_TIMER + slot] !=
                expected[slot]) {
                mismatches++;
            }
        }

        srand(seed);
        for (step = 0; step < draws; step++) {
            rand();
        }
        by_hand = rand();

        if (after_call != by_hand) {
            mismatches++;
        }
    }

    CHECK_EQ(mismatches, 0);
    CHECK_EQ(modelled_writes > 0, 1);
}

/* There is no test for a timer that is already running, so a landed roll
   overwrites the count that was there.  Every trial re-seeds all three slots
   with a sentinel no duration can produce, so a slot holding anything else
   afterwards was written over an occupied slot -- and the value it holds is
   still the plain 2 or 3, never a 0 and never an extension of the sentinel. */
static void an_ailment_already_running_is_overwritten(void)
{
    int trial;
    int slot;
    int value;
    int overwrites;
    int wrong_value;

    overwrites = 0;
    wrong_value = 0;

    for (trial = 0; trial < AILMENT_TRIALS; trial++) {
        stage_ailment();
        for (slot = POISON_TIMER; slot <= SEAL_TIMER; slot++) {
            unit(PATIENT)->status_timers[slot] = TIMER_SENTINEL;
        }

        fdps_unit_inflict_random_ailments(PATIENT);

        for (slot = POISON_TIMER; slot <= SEAL_TIMER; slot++) {
            value = unit(PATIENT)->status_timers[slot];
            if (value != TIMER_SENTINEL) {
                overwrites++;
                if (value != TURNS_LOW && value != TURNS_HIGH) {
                    wrong_value++;
                }
            }
        }
    }

    CHECK_EQ(wrong_value, 0);
    CHECK_EQ(overwrites > 0, 1);
}

/* The function reads one record and writes three bytes of it.  The whole
   staged block is compared byte for byte against a snapshot taken before the
   calls, with only those three offsets of record 1 exempt, so a stray write
   anywhere -- another field, another record, the experience accumulator --
   would show. */
static void nothing_outside_the_three_ailment_bytes_is_written(void)
{
    int trial;
    int index;
    int first_exempt;
    int last_exempt;
    int differences;

    stage_ailment();
    unit(PATIENT)->status_timers[0] = TIMER_SENTINEL;
    unit(PATIENT)->status_timers[1] = TIMER_SENTINEL;
    unit(PATIENT)->status_timers[2] = TIMER_SENTINEL;
    unit(PATIENT)->mp_current = 33;
    unit(PATIENT)->mp_max = 44;
    unit(0)->hp_current = 88;
    unit(2)->hp_current = 77;
    unit(3)->level = 9;

    for (index = 0; index < (int) sizeof(unit_block); index++) {
        ailment_snapshot[index] = unit_block[index];
    }

    for (trial = 0; trial < AILMENT_TRIALS; trial++) {
        fdps_unit_inflict_random_ailments(PATIENT);
    }

    first_exempt = PATIENT * UNIT_RECORD_STRIDE + 0x25;
    last_exempt = PATIENT * UNIT_RECORD_STRIDE + 0x27;
    differences = 0;

    for (index = 0; index < (int) sizeof(unit_block); index++) {
        if (index >= first_exempt && index <= last_exempt) {
            continue;
        }
        if (unit_block[index] != ailment_snapshot[index]) {
            differences++;
        }
    }

    CHECK_EQ(differences, 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);
}

/* The index picks the record: two hundred calls naming unit 2 write inside
   unit 2 and nowhere else. */
static void each_index_selects_its_own_record_for_ailments(void)
{
    int trial;
    int slot;
    int writes;

    stage_ailment();

    for (trial = 0; trial < AILMENT_TRIALS; trial++) {
        fdps_unit_inflict_random_ailments(2);
    }

    writes = 0;
    for (slot = POISON_TIMER; slot <= SEAL_TIMER; slot++) {
        if (unit(2)->status_timers[slot] != 0) {
            writes++;
        }
        CHECK_EQ(unit(0)->status_timers[slot], 0);
        CHECK_EQ(unit(PATIENT)->status_timers[slot], 0);
        CHECK_EQ(unit(3)->status_timers[slot], 0);
    }

    CHECK_EQ(writes > 0, 1);
}

/* ------------------------------------------------------------------ *
 * fdps_unit_apply_status_effect @ 00028f70
 *
 * The expected values below are read off its assembly: the CMP 0x11 / MOV 0x27,
 * CMP 0x12 / MOV 0x25, CMP 0x13 / MOV 0x26 chain at 00028f92..00028fbd for
 * which timer each id picks, ADD EAX,0x22 at 00028fc2 for the slot a blessing
 * index picks, MOV dword ptr [EBP+0x14],0x14 at 00028fc8 for the id rewrite,
 * XOR EAX,EAX / MOV AL,byte ptr [EDX+0x2] at 00028fe3 for the hit rate, the
 * IDIV by 0x64 and signed JGE at 00028ffa..00028fff for the roll, CMP byte ptr
 * [EAX],0x0 / JZ at 00029007 for the already-running test, the CMP 0x11 / JL and
 * CMP 0x13 / JLE pair at 0002900e that spans the immunity call, the IDIV by 2
 * with ADD EDX,0x2 at 00029033..0002903f for the duration, and MOV DL,byte ptr
 * [EDX+0x21] / AND EDX,0xff / IMUL EDX,EDX,0xa / ADD dword ptr [0x00069cec],EDX
 * at 00029051..00029060 for the award.  The hit rates the real spells carry come
 * from assets/spells.md.
 *
 * HOW THE RANDOMNESS IS TAKEN OUT.  The hit rate is a staged byte, so a rate of
 * 100 makes the roll pass whatever rand() returned and a rate of 0 makes it fail
 * whatever it returned; every case about the branch structure is written at one
 * of those two ends and needs no seed.  The two that are about the roll itself
 * walk the seeds for a chosen remainder, and the four that are about how far the
 * stream advanced re-seed and compare against the same stream advanced by hand.
 * ------------------------------------------------------------------ */

/* IMUL EAX,[EBP+0x14],0x7 inside fdps_get_spell_record, and byte +2 of the
   record is the hit rate (src/fdpstype.h, assets/tables/spells.md). */
#define SPELL_STRIDE 0x07
#define SPELL_HIT_RATE_BYTE 0x02

/* MAGICDAT.DAT holds ids 0x00..0x27; one spare record past the end so a staged
   write for the top id has room. */
#define SPELL_TABLE_RECORDS 41

/* The three ailment spell ids and the blessing the other ids are rewritten
   into. */
#define EFFECT_SEAL 0x11
#define EFFECT_POISON 0x12
#define EFFECT_PARALYSIS 0x13
#define EFFECT_BLESSING 0x14

/* The three 神之祝福 slots fdps_cast_spell_on_targets passes at 00028dc4,
   which are also the status_timers slots +0x22 reaches. */
#define BLESSING_SLOTS 3

/* struct fdps_unit_record's status_timers is six bytes. */
#define STATUS_TIMER_SLOTS 6

/* Rates that decide the roll without a seed, and 封魔咒術's real rate of 50
   for the boundary case. */
#define RATE_ALWAYS 100
#define RATE_NEVER 0
#define SEAL_REAL_RATE 50

/* stage() puts the patient on level 4, and the award is ten times that. */
#define PATIENT_LEVEL 4
#define STATUS_AWARD 40

/* Enough calls that a duration outside 2..3 would have shown. */
#define STATUS_TRIALS 200

/* An arbitrary seed for the cases that count draws by hand. */
#define STATUS_SEED 271

static unsigned char spell_table[SPELL_TABLE_RECORDS * SPELL_STRIDE];
static unsigned char status_snapshot[STAGE_UNITS * UNIT_RECORD_STRIDE];

static void set_spell_hit_rate(int spell_id, int rate)
{
    spell_table[spell_id * SPELL_STRIDE + SPELL_HIT_RATE_BYTE] =
        (unsigned char) rate;
}

/* The immunity fixture, plus an all-zero spell table published as the base
   fdps_get_spell_record reads.  Every rate therefore starts at 0 -- refusing
   everything -- and a case opens exactly the ids it means to test. */
static void stage_status(void)
{
    int i;

    stage_immunity();

    for (i = 0; i < (int) sizeof(spell_table); i++) {
        spell_table[i] = 0;
    }
    data_fdps_battle_spell_effect_table_ptr = spell_table;
}

/* The value the seeded stream yields after exactly `draws` calls have been
   taken out of it. */
static int stream_value_after(int seed, int draws)
{
    int step;

    srand(seed);
    for (step = 0; step < draws; step++) {
        rand();
    }

    return rand();
}

/* MOV 0x27 for id 0x11, MOV 0x25 for 0x12, MOV 0x26 for 0x13, and ADD EAX,0x22
   for everything else: the four offsets the assembly names, measured against
   the field ticket 17 settled. */
static void the_status_timer_offsets_match_the_assembly(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
    CHECK_EQ((int) sizeof(layout_probe.status_timers), STATUS_TIMER_SLOTS);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers) +
             SEAL_TIMER, 0x27);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers) +
             POISON_TIMER, 0x25);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers) +
             PARALYSIS_TIMER, 0x26);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers) +
             BLESSING_SLOTS - 1, 0x24);
}

/* Each of the three ailment ids lands in the slot its own CMP/MOV pair names
   and in no other, which is the case a sequential 0x11->+0x25 mapping fails on
   two ids out of three. */
static void each_ailment_id_writes_the_timer_the_assembly_names(void)
{
    stage_status();
    set_spell_hit_rate(EFFECT_SEAL, RATE_ALWAYS);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_SEAL, PATIENT), 1);
    CHECK_EQ(unit(PATIENT)->status_timers[SEAL_TIMER] >= TURNS_LOW, 1);
    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], 0);
    CHECK_EQ(unit(PATIENT)->status_timers[PARALYSIS_TIMER], 0);

    stage_status();
    set_spell_hit_rate(EFFECT_POISON, RATE_ALWAYS);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT), 1);
    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER] >= TURNS_LOW, 1);
    CHECK_EQ(unit(PATIENT)->status_timers[SEAL_TIMER], 0);
    CHECK_EQ(unit(PATIENT)->status_timers[PARALYSIS_TIMER], 0);

    stage_status();
    set_spell_hit_rate(EFFECT_PARALYSIS, RATE_ALWAYS);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_PARALYSIS, PATIENT), 1);
    CHECK_EQ(unit(PATIENT)->status_timers[PARALYSIS_TIMER] >= TURNS_LOW, 1);
    CHECK_EQ(unit(PATIENT)->status_timers[SEAL_TIMER], 0);
    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], 0);
}

/* ADD EAX,0x22: a blessing index of 0, 1 or 2 lands in status_timers slot 0, 1
   or 2 -- the three fdps_unit_recompute_combat_stats reads -- and never in one
   of the three ailment slots above them. */
static void a_blessing_index_writes_its_own_slot(void)
{
    int slot;

    for (slot = 0; slot < BLESSING_SLOTS; slot++) {
        stage_status();
        set_spell_hit_rate(EFFECT_BLESSING, RATE_ALWAYS);

        CHECK_EQ(fdps_unit_apply_status_effect(slot, PATIENT), 1);
        CHECK_EQ(unit(PATIENT)->status_timers[slot] >= TURNS_LOW, 1);
        CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], 0);
        CHECK_EQ(unit(PATIENT)->status_timers[PARALYSIS_TIMER], 0);
        CHECK_EQ(unit(PATIENT)->status_timers[SEAL_TIMER], 0);
    }
}

/* MOV dword ptr [EBP+0x14],0x14 before the fdps_get_spell_record call: the rate
   a blessing rolls against is spell 0x14's and never spell 0's, 1's or 2's.
   Both directions are asserted, so neither table can be the one being read by
   accident: spells 0..2 wide open with 0x14 shut refuses every blessing, and
   0x14 open with 0..2 shut lands every one. */
static void the_blessing_rolls_against_spell_fourteen(void)
{
    int slot;

    stage_status();
    for (slot = 0; slot < BLESSING_SLOTS; slot++) {
        set_spell_hit_rate(slot, RATE_ALWAYS);
    }
    set_spell_hit_rate(EFFECT_BLESSING, RATE_NEVER);

    for (slot = 0; slot < BLESSING_SLOTS; slot++) {
        CHECK_EQ(fdps_unit_apply_status_effect(slot, PATIENT), 0);
        CHECK_EQ(unit(PATIENT)->status_timers[slot], 0);
    }

    stage_status();
    for (slot = 0; slot < BLESSING_SLOTS; slot++) {
        set_spell_hit_rate(slot, RATE_NEVER);
    }
    set_spell_hit_rate(EFFECT_BLESSING, RATE_ALWAYS);

    for (slot = 0; slot < BLESSING_SLOTS; slot++) {
        CHECK_EQ(fdps_unit_apply_status_effect(slot, PATIENT), 1);
        CHECK_EQ(unit(PATIENT)->status_timers[slot] >= TURNS_LOW, 1);
    }
}

/* The signed JGE at 00028fff is a strict `remainder < rate`, so a rate of 0
   refuses every draw and a rate of 100 accepts every one.  Two hundred calls at
   each end, and the accumulator is checked so a refusal cannot be crediting
   anything. */
static void the_rate_bounds_the_roll_at_both_ends(void)
{
    int trial;
    int lands;

    stage_status();
    set_spell_hit_rate(EFFECT_POISON, RATE_NEVER);
    lands = 0;
    for (trial = 0; trial < STATUS_TRIALS; trial++) {
        lands += fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT);
    }
    CHECK_EQ(lands, 0);
    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);

    lands = 0;
    for (trial = 0; trial < STATUS_TRIALS; trial++) {
        stage_status();
        set_spell_hit_rate(EFFECT_POISON, RATE_ALWAYS);
        lands += fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT);
    }
    CHECK_EQ(lands, STATUS_TRIALS);
}

/* The boundary itself, on 封魔咒術's real rate of 50: remainder 49 lands and
   remainder 50 does not.  The seeds are walked for rather than assumed. */
static void the_roll_lands_strictly_under_the_rate(void)
{
    int seed_that_lands;
    int seed_that_misses;

    seed_that_lands = seed_whose_first_draw_leaves(SEAL_REAL_RATE - 1);
    seed_that_misses = seed_whose_first_draw_leaves(SEAL_REAL_RATE);

    CHECK_EQ(seed_that_lands != 0, 1);
    CHECK_EQ(seed_that_misses != 0, 1);

    stage_status();
    set_spell_hit_rate(EFFECT_SEAL, SEAL_REAL_RATE);
    srand(seed_that_lands);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_SEAL, PATIENT), 1);

    stage_status();
    set_spell_hit_rate(EFFECT_SEAL, SEAL_REAL_RATE);
    srand(seed_that_misses);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_SEAL, PATIENT), 0);
    CHECK_EQ(unit(PATIENT)->status_timers[SEAL_TIMER], 0);
}

/* rand() % 2 + 2 -- two or three turns and never 1, never 0 on a landed roll,
   and never a count above 3. */
static void a_landed_effect_runs_two_or_three_turns(void)
{
    int trial;
    int value;
    int out_of_range;

    out_of_range = 0;

    for (trial = 0; trial < STATUS_TRIALS; trial++) {
        stage_status();
        set_spell_hit_rate(EFFECT_PARALYSIS, RATE_ALWAYS);
        CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_PARALYSIS, PATIENT), 1);

        value = unit(PATIENT)->status_timers[PARALYSIS_TIMER];
        if (value != TURNS_LOW && value != TURNS_HIGH) {
            out_of_range++;
        }
    }

    CHECK_EQ(out_of_range, 0);
}

/* CMP byte ptr [EAX],0x0 / JZ: a timer already counting down refuses the effect
   outright, and the count it holds is left exactly as it was -- not refreshed,
   not extended -- with nothing credited.  This is the whole of what separates
   the accounting here from fdps_unit_inflict_random_ailments, which has no such
   test. */
static void an_effect_already_running_is_refused_and_not_refreshed(void)
{
    int trial;

    stage_status();
    set_spell_hit_rate(EFFECT_POISON, RATE_ALWAYS);
    unit(PATIENT)->status_timers[POISON_TIMER] = TIMER_SENTINEL;

    for (trial = 0; trial < STATUS_TRIALS; trial++) {
        CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT), 0);
    }

    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], TIMER_SENTINEL);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);
}

/* CMP 0x11 / JL and CMP 0x13 / JLE on the REWRITTEN id: the three ailments
   consult fdps_unit_is_ailment_immune and a blessing does not.  The same immune
   unit therefore refuses all three ailments and takes all three blessings. */
static void immunity_gates_the_ailments_and_not_the_blessings(void)
{
    int slot;

    stage_status();
    unit(PATIENT)->clazz = CLASS_MACHINE_SOLDIER;
    set_spell_hit_rate(EFFECT_SEAL, RATE_ALWAYS);
    set_spell_hit_rate(EFFECT_POISON, RATE_ALWAYS);
    set_spell_hit_rate(EFFECT_PARALYSIS, RATE_ALWAYS);
    set_spell_hit_rate(EFFECT_BLESSING, RATE_ALWAYS);

    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_SEAL, PATIENT), 0);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT), 0);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_PARALYSIS, PATIENT), 0);
    CHECK_EQ(unit(PATIENT)->status_timers[SEAL_TIMER], 0);
    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], 0);
    CHECK_EQ(unit(PATIENT)->status_timers[PARALYSIS_TIMER], 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED);

    for (slot = 0; slot < BLESSING_SLOTS; slot++) {
        CHECK_EQ(fdps_unit_apply_status_effect(slot, PATIENT), 1);
        CHECK_EQ(unit(PATIENT)->status_timers[slot] >= TURNS_LOW, 1);
    }
}

/* The immune portrait run reaches the same gate through the other field, so an
   ordinary class carrying an ENEMYDAT portrait id refuses an ailment too. */
static void an_immune_portrait_id_also_refuses_an_ailment(void)
{
    stage_status();
    unit(PATIENT)->portrait_id = MID_IMMUNE_PORTRAIT;
    set_spell_hit_rate(EFFECT_POISON, RATE_ALWAYS);

    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT), 0);
    CHECK_EQ(unit(PATIENT)->status_timers[POISON_TIMER], 0);
}

/* IMUL EDX,EDX,0xa on the zero-extended level byte, ADDED to the accumulator:
   a level 4 target is worth 40 and three landings are worth 120.  Neither the
   side byte nor the portrait id is looked at, so a landing on a roster unit
   credits exactly as one on an enemy does. */
static void a_landed_effect_credits_ten_times_the_level(void)
{
    stage_status();
    set_spell_hit_rate(EFFECT_SEAL, RATE_ALWAYS);
    set_spell_hit_rate(EFFECT_POISON, RATE_ALWAYS);
    set_spell_hit_rate(EFFECT_PARALYSIS, RATE_ALWAYS);

    CHECK_EQ(unit(PATIENT)->level, PATIENT_LEVEL);

    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_SEAL, PATIENT), 1);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + STATUS_AWARD);

    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT), 1);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + STATUS_AWARD * 2);

    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_PARALYSIS, PATIENT), 1);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + STATUS_AWARD * 3);

    /* A level of 0x80 is above the signed byte's top: AND EDX,0xff makes it
       128 and the award 1280, not a negative figure. */
    stage_status();
    set_spell_hit_rate(EFFECT_POISON, RATE_ALWAYS);
    unit(PATIENT)->level = 0x80;
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT), 1);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 0x80 * 10);
}

/* THE ORDER OF THE THREE TESTS, measured by how far the shared rand() stream
   has moved.  The hit roll always draws; the duration draws only after all
   three tests passed; and neither the already-running test nor the immunity
   call draws anything of its own.  So a landing is two draws and each of the
   three ways of failing is exactly one -- which is what pins the immunity call
   to its place BEHIND the roll and the timer test rather than in front of
   them. */
static void the_stream_advances_by_one_draw_per_failure_and_two_on_a_landing(void)
{
    int after_call;

    stage_status();
    set_spell_hit_rate(EFFECT_POISON, RATE_ALWAYS);
    srand(STATUS_SEED);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT), 1);
    after_call = rand();
    CHECK_EQ(after_call, stream_value_after(STATUS_SEED, 2));

    stage_status();
    set_spell_hit_rate(EFFECT_POISON, RATE_NEVER);
    srand(STATUS_SEED);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT), 0);
    after_call = rand();
    CHECK_EQ(after_call, stream_value_after(STATUS_SEED, 1));

    stage_status();
    set_spell_hit_rate(EFFECT_POISON, RATE_ALWAYS);
    unit(PATIENT)->status_timers[POISON_TIMER] = TIMER_SENTINEL;
    srand(STATUS_SEED);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT), 0);
    after_call = rand();
    CHECK_EQ(after_call, stream_value_after(STATUS_SEED, 1));

    stage_status();
    set_spell_hit_rate(EFFECT_POISON, RATE_ALWAYS);
    unit(PATIENT)->clazz = CLASS_MACHINE_SOLDIER;
    srand(STATUS_SEED);
    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_POISON, PATIENT), 0);
    after_call = rand();
    CHECK_EQ(after_call, stream_value_after(STATUS_SEED, 1));
}

/* The function reads one record and writes one byte of it.  The whole staged
   block is compared byte for byte against a snapshot, with only that one offset
   exempt, so a stray write anywhere -- a neighbouring timer, another field,
   another record -- would show. */
static void nothing_outside_the_one_timer_byte_is_written(void)
{
    int index;
    int exempt;
    int differences;

    stage_status();
    set_spell_hit_rate(EFFECT_SEAL, RATE_ALWAYS);
    unit(PATIENT)->mp_current = 33;
    unit(PATIENT)->mp_max = 44;
    unit(PATIENT)->status_timers[0] = TIMER_SENTINEL;
    unit(PATIENT)->status_timers[POISON_TIMER] = TIMER_SENTINEL;
    unit(0)->hp_current = 88;
    unit(2)->hp_current = 77;
    unit(3)->level = 9;

    for (index = 0; index < (int) sizeof(unit_block); index++) {
        status_snapshot[index] = unit_block[index];
    }

    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_SEAL, PATIENT), 1);

    exempt = PATIENT * UNIT_RECORD_STRIDE + 0x27;
    differences = 0;
    for (index = 0; index < (int) sizeof(unit_block); index++) {
        if (index == exempt) {
            continue;
        }
        if (unit_block[index] != status_snapshot[index]) {
            differences++;
        }
    }

    CHECK_EQ(differences, 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + STATUS_AWARD);
}

/* The index picks the record: naming unit 2 writes inside unit 2 and nowhere
   else, and the award follows unit 2's own level. */
static void each_index_selects_its_own_record_for_status(void)
{
    stage_status();
    set_spell_hit_rate(EFFECT_PARALYSIS, RATE_ALWAYS);
    unit(2)->level = 7;

    CHECK_EQ(fdps_unit_apply_status_effect(EFFECT_PARALYSIS, 2), 1);

    CHECK_EQ(unit(2)->status_timers[PARALYSIS_TIMER] >= TURNS_LOW, 1);
    CHECK_EQ(unit(0)->status_timers[PARALYSIS_TIMER], 0);
    CHECK_EQ(unit(PATIENT)->status_timers[PARALYSIS_TIMER], 0);
    CHECK_EQ(unit(3)->status_timers[PARALYSIS_TIMER], 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SEED + 70);
}

/* ---------------------------------------------------------------------------
   fdps_level_up_apply_stat_gain at 0001e370.

   Every expected value below is read off the assembly at the addresses quoted
   on each test: the XOR EAX,EAX / MOV AL,byte ptr [EDX] minimum at 0001e37c and
   the MOV AL,byte ptr [EAX+0x1] / AND EAX,0xff bound at 0001e389 that make both
   pair bytes unsigned, the SUB at 0001e391 that forms the range, the CMP dword
   ptr [EBP-0x4],0x0 / JZ at 0001e397 that guards BOTH the CALL to rand() and
   the IDIV, the IDIV at 0001e3a9 whose remainder is the offset, the MOV
   [0x00064038],EAX store at 0001e3b5, the five pushes at 0001e3ba-0001e3cd that
   make the drawing call fdps_draw_number(dst, 0xa4, figure, 0, 0), and the MOV
   DX,word ptr [0x00064038] / ADD word ptr [EAX],DX pair at 0001e3d6 that adds
   only the LOW WORD and only sixteen bits wide.  The five field offsets and the
   five pair offsets are the caller's, ADD EAX,0x37/0x39/0x3e/0x42/0x46 and ADD
   EAX,0x2/0x4/0x6/0x8 at 0001e10d through 0001e189.  None of them is read off
   the emitted C.

   HOW THE RANDOMNESS IS TAKEN OUT.  A pair whose two bytes are equal has a
   range of 0 and takes the JZ, so it draws nothing at all and gains exactly the
   minimum -- most of the assertions below use such a pair and are therefore
   exact.  The cases that must exercise the divide either seed the stream with
   srand and compute the same remainder by hand, or sweep many seeds and assert
   an interval.

   HOW THE DRAWN FIGURE IS OBSERVED.  The same staged sheet tests/text.c uses:
   a real .CEL-shaped block whose sprite n is a flat 6x8 fill of colour n + 1,
   so the window spells out which sprite index the figure selected and where it
   landed.  That runs the whole path through fdps_draw_number and
   fdps_blit_dispatch and is what pins the pitch of 164 and the natural-width
   format, neither of which is visible in the stat field. */

/* The sheet's shape, from resource_info/cel.md: the sprite offset table sits at
   a fixed +0x0f and holds one dword per sprite.  Sixty-five sprites, five
   colour rows of the thirteen glyphs '0'-'9', '+', '-', '?'. */
#define GAIN_TABLE_AT     0x0f
#define GAIN_SPRITES      65
#define GAIN_STREAM_AT    (GAIN_TABLE_AT + GAIN_SPRITES * 4)
#define GAIN_STREAM_BYTES 16
#define GAIN_SHEET_BYTES  (GAIN_STREAM_AT + GAIN_SPRITES * GAIN_STREAM_BYTES)

/* PUSH 0xa4 at 0001e3c5.  The window is 164 by 66; twelve rows is all any test
   here draws into, and one row past the 8-row cell is enough to catch a blit
   that stepped by the wrong stride. */
#define GAIN_PITCH  0xa4
#define GAIN_ROWS   12
#define GAIN_BYTES  (GAIN_PITCH * GAIN_ROWS)

/* Not a colour any sprite paints, so an untouched pixel is unmistakable. */
#define GAIN_BACKGROUND 0xaa

/* A figure no gain in these tests can produce, so the store at 0001e3b5 is
   visible even when the gain is 0. */
#define GAIN_SCRATCH_SEED 12345

/* FRILEVUP.DAT records are eleven bytes and the caller indexes pairs at +0, +2,
   +4, +6 and +8. */
#define GROWTH_RECORD_BYTES 11
#define GROWTH_DX_PAIR_AT 4

/* Arbitrary but fixed: the tests that need a known draw reseed with these. */
#define GAIN_SEED       4919
#define GAIN_SEED_SWEEP 120

static unsigned char gain_sheet[GAIN_SHEET_BYTES];
static unsigned char gain_window[GAIN_BYTES];
static unsigned char growth_record[GROWTH_RECORD_BYTES];

/* Only ever read for its field sizes and offsets. */
static struct fdps_character_growth growth_probe;

static int gain_sprite_color(int sprite_index)
{
    return sprite_index + 1;
}

/* The colour standing at the top-left pixel of digit cell `cell`, counting from
   the cell the figure was drawn at. */
static int gain_cell(int cell)
{
    return gain_window[cell * 6];
}

/* The colour standing `row` rows below the figure's first cell.  This is what
   separates a pitch of 164 from any other: a row lands here only if the blit
   stepped by exactly 164. */
static int gain_row_pixel(int row)
{
    return gain_window[row * GAIN_PITCH];
}

/* Zeroes the unit block and the growth record, repaints the window, and hands
   fdps_draw_number a sheet whose sprite n is a flat fill of colour n + 1. */
static void stage_gain(void)
{
    int sprite;
    int row;
    int stream_at;
    int i;

    for (i = 0; i < GAIN_SHEET_BYTES; i++) {
        gain_sheet[i] = 0;
    }

    for (sprite = 0; sprite < GAIN_SPRITES; sprite++) {
        stream_at = GAIN_STREAM_AT + sprite * GAIN_STREAM_BYTES;
        *(int *) (gain_sheet + GAIN_TABLE_AT + sprite * 4) = stream_at;
        for (row = 0; row < 8; row++) {
            gain_sheet[stream_at + row * 2] = 0x05;
            gain_sheet[stream_at + row * 2 + 1] =
                (unsigned char) gain_sprite_color(sprite);
        }
    }

    for (i = 0; i < GAIN_BYTES; i++) {
        gain_window[i] = GAIN_BACKGROUND;
    }

    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_block[i] = 0;
    }

    for (i = 0; i < GROWTH_RECORD_BYTES; i++) {
        growth_record[i] = 0;
    }

    data_fdps_map_unit_array_ptr = unit_block;
    data_fdps_number_glyph_sheet_ptr = gain_sheet;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_dialog_last_action_value_param = GAIN_SCRATCH_SEED;
}

/* Puts one {min, max} pair at the head of the growth record. */
static void pair(int min_gain, int exclusive_max)
{
    growth_record[0] = (unsigned char) min_gain;
    growth_record[1] = (unsigned char) exclusive_max;
}

/* The five fields the caller aims at and the five pairs it aims them with.  ADD
   EAX,0x37/0x39/0x3e/0x42/0x46 on the record at 0001e10d, 0001e12c, 0001e14b,
   0001e16a and 0001e189, and ADD EAX,0x2/0x4/0x6/0x8 on the growth record at
   0001e125, 0001e144, 0001e163 and 0001e182 -- so the pair order is ap, dp, dx,
   hp, mp and every field it reaches is two bytes wide. */
static void the_growth_pair_layout_matches_the_offsets_read(void)
{
    CHECK_EQ((int) sizeof(struct fdps_character_growth), GROWTH_RECORD_BYTES);
    CHECK_EQ((int) offsetof(struct fdps_character_growth, ap_min), 0);
    CHECK_EQ((int) offsetof(struct fdps_character_growth, dp_min), 2);
    CHECK_EQ((int) offsetof(struct fdps_character_growth, dx_min), 4);
    CHECK_EQ((int) offsetof(struct fdps_character_growth, hp_min), 6);
    CHECK_EQ((int) offsetof(struct fdps_character_growth, mp_min), 8);
    CHECK_EQ((int) sizeof(growth_probe.ap_min), 1);
    CHECK_EQ((int) sizeof(growth_probe.ap_max), 1);

    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap_base), 0x37);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, dp_base), 0x39);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, dx_base), 0x3e);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_max), 0x46);
    CHECK_EQ((int) sizeof(layout_probe.ap_base), 2);
    CHECK_EQ((int) sizeof(layout_probe.dp_base), 2);
    CHECK_EQ((int) sizeof(layout_probe.dx_base), 2);
    CHECK_EQ((int) sizeof(layout_probe.mp_max), 2);
}

/* The JZ at 0001e39b: a range of 0 skips everything and the gain is the
   minimum, the same figure every time.  Three calls in a row also show the ADD
   word ptr [EAX],DX at 0001e3e0 accumulating rather than assigning. */
static void an_equal_pair_gains_exactly_the_minimum(void)
{
    stage_gain();
    pair(3, 3);

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);
    CHECK_EQ(unit(PATIENT)->hp_max, 3);

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);
    CHECK_EQ(unit(PATIENT)->hp_max, 6);

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);
    CHECK_EQ(unit(PATIENT)->hp_max, 9);
}

/* The zero test guards the CALL at 0001e39d as well as the IDIV, so an equal
   pair leaves the shared stream exactly where it found it.  Half of
   FRILEVUP.DAT's pairs are equal, so this is the ordinary path and every later
   roll of the battle depends on it. */
static void an_equal_pair_draws_no_random_number(void)
{
    int untouched;
    int after_call;

    srand(GAIN_SEED);
    untouched = rand();

    stage_gain();
    pair(3, 3);

    srand(GAIN_SEED);
    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);
    after_call = rand();

    CHECK_EQ(after_call, untouched);
}

/* A range that is not zero takes exactly one draw and no more. */
static void a_wider_pair_draws_exactly_one_random_number(void)
{
    int second;
    int after_call;

    srand(GAIN_SEED);
    rand();
    second = rand();

    stage_gain();
    pair(0, 5);

    srand(GAIN_SEED);
    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);
    after_call = rand();

    CHECK_EQ(after_call, second);
}

/* ADD EAX,dword ptr [EBP-0x4] at 0001e3b2: the figure is the minimum plus the
   remainder, not the remainder alone and not a remainder taken over the whole
   bound. */
static void the_gain_is_the_minimum_plus_the_remainder(void)
{
    int roll;

    srand(GAIN_SEED);
    roll = rand();

    stage_gain();
    pair(7, 10);

    srand(GAIN_SEED);
    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);

    CHECK_EQ(unit(PATIENT)->hp_max, 7 + roll % 3);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, 7 + roll % 3);
}

/* IDIV by growth_pair[1] - growth_pair[0]: the remainder of a non-negative
   dividend is 0 through range - 1, so the bound is exclusive and a pair of
   {0, 5} can never gain 5.  The top of the band has to actually appear, or an
   off-by-one at the other end would pass this unnoticed. */
static void the_upper_bound_of_the_pair_is_exclusive(void)
{
    int seed;
    int gain;
    int inside;
    int saw_top;
    int reached_bound;

    inside = 0;
    saw_top = 0;
    reached_bound = 0;

    for (seed = 1; seed <= GAIN_SEED_SWEEP; seed++) {
        stage_gain();
        pair(0, 5);
        srand(seed);
        fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                      gain_window);
        gain = unit(PATIENT)->hp_max;
        if (gain >= 0 && gain <= 4) {
            inside++;
        }
        if (gain == 4) {
            saw_top++;
        }
        if (gain >= 5) {
            reached_bound++;
        }
    }

    CHECK_EQ(inside, GAIN_SEED_SWEEP);
    CHECK_EQ(saw_top > 0, 1);
    CHECK_EQ(reached_bound, 0);
}

/* XOR EAX,EAX / MOV AL at 0001e37c: a minimum byte of 0x80 is 128, not -128. */
static void the_minimum_byte_widens_unsigned(void)
{
    stage_gain();
    pair(0x80, 0x80);

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);

    CHECK_EQ(unit(PATIENT)->hp_max, 128);
}

/* MOV AL,byte ptr [EAX+0x1] / AND EAX,0xff at 0001e389: a bound byte of 0xc0 is
   192, so {0x00, 0xc0} spans 0..191.  Read signed it would be -64 and the range
   -64, which the IDIV would still turn into a non-negative remainder -- but one
   that could never reach 64.  Both halves are asserted: every gain matches the
   remainder over 192, and at least one of them is out of reach of the signed
   reading. */
static void the_bound_byte_widens_unsigned(void)
{
    int seed;
    int roll;
    int matched;
    int above_signed_reach;

    matched = 0;
    above_signed_reach = 0;

    for (seed = 1; seed <= GAIN_SEED_SWEEP; seed++) {
        srand(seed);
        roll = rand();

        stage_gain();
        pair(0x00, 0xc0);
        srand(seed);
        fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                      gain_window);

        if (unit(PATIENT)->hp_max == roll % 192) {
            matched++;
        }
        if (unit(PATIENT)->hp_max >= 64) {
            above_signed_reach++;
        }
    }

    CHECK_EQ(matched, GAIN_SEED_SWEEP);
    CHECK_EQ(above_signed_reach > 0, 1);
}

/* growth_pair is a pointer into the middle of an eleven-byte record and only
   the two bytes at it are read: the caller aims it at +4 for DX, and the ten
   0xff bytes around the pair would be impossible to miss if either load
   strayed.  The record is not written either. */
static void only_the_two_bytes_at_the_pointer_are_read(void)
{
    int i;
    int untouched;

    stage_gain();
    for (i = 0; i < GROWTH_RECORD_BYTES; i++) {
        growth_record[i] = 0xff;
    }
    growth_record[GROWTH_DX_PAIR_AT] = 2;
    growth_record[GROWTH_DX_PAIR_AT + 1] = 2;

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->dx_base,
                                  growth_record + GROWTH_DX_PAIR_AT,
                                  gain_window);

    CHECK_EQ(unit(PATIENT)->dx_base, 2);

    untouched = 0;
    for (i = 0; i < GROWTH_RECORD_BYTES; i++) {
        if (i == GROWTH_DX_PAIR_AT || i == GROWTH_DX_PAIR_AT + 1) {
            if (growth_record[i] == 2) {
                untouched++;
            }
        } else if (growth_record[i] == 0xff) {
            untouched++;
        }
    }
    CHECK_EQ(untouched, GROWTH_RECORD_BYTES);
}

/* ADD word ptr [EAX],DX at 0001e3e0 is sixteen bits wide and DX is the LOW WORD
   of the dword global.  A field at 0xffff therefore wraps to 0 without carrying
   into the field two bytes above it, and a field at 32767 wraps to -32768
   rather than being clamped anywhere. */
static void the_addition_is_sixteen_bits_wide(void)
{
    stage_gain();
    pair(1, 1);
    unit(PATIENT)->hp_max = -1;
    unit(PATIENT)->mp_current = 0x1234;

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);

    CHECK_EQ(unit(PATIENT)->hp_max, 0);
    CHECK_EQ(unit(PATIENT)->mp_current, 0x1234);

    stage_gain();
    pair(1, 1);
    unit(PATIENT)->hp_max = 32767;

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);

    CHECK_EQ(unit(PATIENT)->hp_max, -32768);
}

/* The only store into the record is the one word the pointer names.  Four
   records are staged zeroed, so a walk that strayed into a neighbouring field
   or a neighbouring record would leave a mark. */
static void nothing_but_the_named_field_is_written(void)
{
    int i;
    int clean;

    stage_gain();
    pair(5, 5);

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);

    CHECK_EQ(unit(PATIENT)->hp_max, 5);

    clean = 0;
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        if (i == PATIENT * UNIT_RECORD_STRIDE + 0x42) {
            if (unit_block[i] == 5) {
                clean++;
            }
        } else if (unit_block[i] == 0) {
            clean++;
        }
    }
    CHECK_EQ(clean, (int) sizeof(unit_block));
}

/* MOV [0x00064038],EAX at 0001e3b5 and MOV DX,word ptr [0x00064038] at
   0001e3d6: the figure travels through the global, and what is left in it after
   the call is the gain rather than whatever the caller had put there. */
static void the_scratch_global_carries_the_figure(void)
{
    stage_gain();
    pair(6, 6);

    CHECK_EQ(data_fdps_dialog_last_action_value_param, GAIN_SCRATCH_SEED);

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);

    CHECK_EQ(data_fdps_dialog_last_action_value_param, 6);
    CHECK_EQ(unit(PATIENT)->hp_max, 6);
    CHECK_EQ(gain_cell(0), gain_sprite_color(6));
}

/* PUSH 0x0 at 0001e3bd is fdps_draw_number's digit_count, whose zero is the
   natural-width mode: 12 draws two cells and the third is never touched.  A
   three-digit field would have drawn a '0' sprite first. */
static void the_figure_is_drawn_at_its_natural_width(void)
{
    stage_gain();
    pair(12, 12);

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);

    CHECK_EQ(unit(PATIENT)->hp_max, 12);
    CHECK_EQ(gain_cell(0), gain_sprite_color(1));
    CHECK_EQ(gain_cell(1), gain_sprite_color(2));
    CHECK_EQ(gain_cell(2), GAIN_BACKGROUND);
}

/* XOR EAX,EAX / PUSH EAX at 0001e3ba is show_plus, and it is 0: a level-up gain
   is drawn as a bare figure.  Sprite 10 is the '+' glyph and never appears. */
static void no_leading_plus_is_drawn(void)
{
    stage_gain();
    pair(5, 5);

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);

    CHECK_EQ(gain_cell(0), gain_sprite_color(5));
    CHECK_EQ(gain_cell(1), GAIN_BACKGROUND);
}

/* PUSH 0xa4 at 0001e3c5.  The eight rows of the 6x8 cell land 164 bytes apart,
   which is the only stride that puts row 7 at offset 1148; the ninth row is
   past the cell and stays background. */
static void the_figure_is_drawn_at_the_window_pitch(void)
{
    stage_gain();
    pair(5, 5);

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);

    CHECK_EQ(gain_row_pixel(0), gain_sprite_color(5));
    CHECK_EQ(gain_row_pixel(7), gain_sprite_color(5));
    CHECK_EQ(gain_row_pixel(8), GAIN_BACKGROUND);
    CHECK_EQ(gain_window[7 * GAIN_PITCH + 5], gain_sprite_color(5));
    CHECK_EQ(gain_window[7 * GAIN_PITCH + 6], GAIN_BACKGROUND);
}

/* dst is handed to fdps_draw_number untouched, so the figure starts exactly
   where the caller aimed it -- 0x10ce is 26 * 164 + 38, one of the two columns
   fdps_unit_award_exp_and_level_up uses. */
static void the_figure_starts_where_dst_points(void)
{
    stage_gain();
    pair(5, 5);

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window + 3 * GAIN_PITCH + 38);

    CHECK_EQ(gain_window[3 * GAIN_PITCH + 38], gain_sprite_color(5));
    CHECK_EQ(gain_window[4 * GAIN_PITCH + 38], gain_sprite_color(5));
    CHECK_EQ(gain_window[0], GAIN_BACKGROUND);
    CHECK_EQ(gain_window[3 * GAIN_PITCH + 37], GAIN_BACKGROUND);
}

/* The colour row is not an argument and this function neither sets it nor puts
   it back: with the global on 2 the digit comes out of the third colour row,
   sprite 2 * 13 + 4, and the global is still 2 afterwards. */
static void the_colour_row_is_left_to_the_caller(void)
{
    stage_gain();
    pair(4, 4);
    data_fdps_number_glyph_color_row = 2;

    fdps_level_up_apply_stat_gain(&unit(PATIENT)->hp_max, growth_record,
                                  gain_window);

    CHECK_EQ(gain_cell(0), gain_sprite_color(2 * 13 + 4));
    CHECK_EQ(data_fdps_number_glyph_color_row, 2);
}

/* The staged sheet is this file's own and nothing outside it should inherit the
   pointer. */
static void the_staged_sheet_is_put_back(void)
{
    data_fdps_number_glyph_sheet_ptr = (unsigned char *) 0;
    data_fdps_number_glyph_color_row = 0;

    CHECK_EQ(data_fdps_number_glyph_sheet_ptr == (unsigned char *) 0, 1);
    CHECK_EQ(data_fdps_number_glyph_color_row, 0);
}
/* ------------------------------------------------------------------
 * fdps_unit_award_exp_and_level_up @ 0001dd30
 *
 * Expected values come from the assembly -- CMP dword ptr [0x00069cec],0x0 /
 * JZ at 0001dd61 and CALL 0x000109b0 / TEST EAX,EAX / JZ at 0001dd6e for the
 * first two gates; CMP dword ptr [EBP-0x20],0x9 / CMP dword ptr
 * [EBP-0x24],0x63 / CMP dword ptr [EBP-0x24],0x28 at 0001dd7f for the third;
 * MOV dword ptr [0x00069cd0],0x0 at 0001dd9b and MOV dword ptr
 * [0x00069cd0],0x1 at 0001e353 for the two cursor modes; CMP dword ptr
 * [0x00069cec],0x63 / JLE / MOV ...,0x63 at 0001ddb1 for the clamp; MOV AL,byte
 * ptr [EAX+0x3c] / AND EAX,0xff / ADD EDX,EAX at 0001dded for the running
 * total; IMUL EAX,EAX,0x18 / SUB EAX,[0x00069ce4] / ADD EAX,0x18 at 0001de00
 * and the same with [0x00069ce0] / ADD EAX,0x20 at 0001de19 for where the
 * label sits; MOV dword ptr [EBP-0x8],0x6 / CMP ...,0x24 / ADD ...,0x2 at
 * 0001de90 and CMP dword ptr [EBP-0x4],0x18 / JLE at 0001deae for the rise
 * ramp; PUSH 0x8 / PUSH 0x28 at 0001def7 for the label's extent; CMP dword ptr
 * [EBP-0x10],0x64 / JL at 0001dfb9 for the level's price; and MOV AL,byte ptr
 * [EBP-0x10] / MOV byte ptr [EDX+0x3c],AL / MOV dword ptr [0x00069cec],0x0 at
 * 0001e340 for what is left behind.  None of them is read off the emitted C.
 *
 * WHAT IS NOT COVERED, AND WHY.  The level-up half -- everything behind
 * CMP dword ptr [EBP-0x10],0x64 at 0001dfb9 -- loads Levup.wav and hands the
 * sample slot fdps_audio_start_wav answers with straight back to
 * fdps_audio_sample_is_playing in a spin.  With no AIL driver initialised
 * fdps_audio_start_wav refuses and answers -1, and the spin then asks the
 * vendor library for the status of a handle one entry before the table.  No
 * assertion can reach that path without a running sound driver, so every case
 * here keeps the running total under 100 and the cases that would otherwise
 * level a unit instead prove that the gate LET THE UNIT THROUGH, by the
 * accumulator having been cleared.  The stat rolls, the spell learning and the
 * window layout behind that gate are unreached here; fdps_level_up_apply_stat_gain,
 * which does the rolling, is covered above.
 *
 * HOW THE RUN IS WATCHED.  The routine composes each frame on a page it
 * allocates and frees itself and blits that page's 312x192 window over the live
 * mode 13h screen, so the adapter is the only place the floating label can be
 * read back from.  Each animated case sets mode 13h, fills the frame with a
 * sentinel, seeds the heap, runs a real timer interrupt so the frame waits end,
 * calls, snapshots the 64,000 bytes and returns to text mode -- the same way
 * tests/death.c watches fdps_play_death_animation_and_mark_dead.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED.  Every one of the fifteen float frames
 * ends by spinning until data_fdps_timer_tick_counter moves.  Nothing advances
 * that counter in a test image, so the second frame would never end.  Each run
 * hooks IRQ0 for the duration of the call with a handler that increments the
 * counter and chains to the one that was there.
 *
 * WHY THE BLEND PAINTS ONE COLOUR.  fdps_blit_blend_transparent_rect ends every
 * pixel it touches as inverse_palette_cube[index], and the index it forms is
 * always inside that 4096-entry table.  Filling the whole cube with one
 * sentinel therefore makes every pixel the label reached that sentinel, whatever
 * the shade ramp holds -- which matters because ticket 23 has not written either
 * table yet and neither may be assumed to be zero.  The cube is saved and put
 * back around every run.
 *
 * WHY THE HEAP IS SEEDED, AND WITH WHAT.  The page is uncleared malloc storage
 * and nothing in the routine paints the parts of it the label does not reach:
 * the scene layer count, the unit count and the cursor mode are all zero, so
 * fdps_draw_scene_layers writes nothing.  Whatever the heap left behind is
 * therefore what shows everywhere else.  Each run first allocates the same two
 * blocks the routine will, in the same order and the same sizes -- 0x140 then
 * 0x15180 -- zeroes both and frees them, so a first-fit allocator hands the
 * routine back the same two zeroed regions.  Every undrawn window pixel then
 * reads 0 and the label's extent can be read off the frame.
 *
 * WHAT THE SNAPSHOT SHOWS IS THE WHOLE CLIMB AT ONCE, not the last frame.  The
 * page is freed and taken again at the same size every frame with the label
 * buffer still held, so it is the same block and nothing clears it between
 * frames: the label's trail piles up.  The band it leaves runs from the top of
 * the highest frame, label_y - 24, to the bottom of the lowest, label_y - 6 + 7,
 * and that band's four edges are what pin the rise ramp and the 40x8 extent.
 * ------------------------------------------------------------------ */

/* Three records, so the neighbour either side of the awarded unit would show a
   walk that strayed out of it. */
#define AWD_UNITS 3
#define AWD_PATIENT 1

/* A leftover byte no case here can produce, parked in both neighbours. */
#define AWD_NEIGHBOUR_MARK 0x77

/* CMP dword ptr [EBP-0x20],0x9: portrait id 9 is the machine soldier, the one
   character allowed past 40, and 5 is an ordinary roster portrait.  0x63 and
   0x28 are the two caps the gate compares the level against. */
#define AWD_MACHINE_PORTRAIT 9
#define AWD_PLAIN_PORTRAIT 5
#define AWD_MACHINE_CAP 0x63
#define AWD_ORDINARY_CAP 0x28

/* Well below either cap, so the standard fixture takes neither gate. */
#define AWD_START_LEVEL 4

/* A cursor mode neither 0 nor 1, so "left at the plain cursor" and "not
   touched at all" are different answers. */
#define AWD_CURSOR_SEED 7

/* The tile the awarded unit stands on, and the map the movement grid states --
   40 by 30 tiles, wide enough that no view clamp bites. */
#define AWD_TILE 0x18
#define AWD_TILE_X 3
#define AWD_TILE_Y 3
#define AWD_MAP_TILES_X 40
#define AWD_MAP_TILES_Y 30

/* The label: 40 x 8, parked half a tile right of the unit's cell and one tile
   plus eight scanlines below its top, climbing 6 pixels on the first frame and
   held at 24 on the last five. */
#define AWD_LABEL_W 0x28
#define AWD_LABEL_H 8
#define AWD_LABEL_ORIGIN_X 0x18
#define AWD_LABEL_ORIGIN_Y 0x20
#define AWD_RISE_FIRST 6
#define AWD_RISE_HELD 0x18
#define AWD_LABEL_BYTES 0x140

/* The page and where its window lands on the adapter: 312 x 192 taken from page
   pixel (24,24) and put down at screen pixel (4,4), so a page column is 20
   lower on screen. */
#define AWD_PAGE_PITCH 0x168
#define AWD_PAGE_BYTES 0x15180
#define AWD_PAGE_BORDER 24
#define AWD_WINDOW_INSET 4
#define AWD_TO_SCREEN (AWD_WINDOW_INSET - AWD_PAGE_BORDER)

/* The adapter, the frame it presents and the two modes the cases switch
   between. */
#define AWD_VGA_BASE 0x000a0000
#define AWD_SCREEN_W 0x140
#define AWD_SCREEN_H 0xc8
#define AWD_SCREEN_BYTES (AWD_SCREEN_W * AWD_SCREEN_H)
#define AWD_MODE_TEXT 0x03
#define AWD_MODE_320X200X256 0x13

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the routine spins on the counter. */
#define AWD_TIMER_VECTOR 8

/* What a screen byte outside the presented window keeps, and the colour every
   blended pixel comes out as. */
#define AWD_BORDER_FILL 0xa5
#define AWD_BLEND_SENTINEL 0x5c

/* The .CEL sheets the routine draws the label out of, shaped as
   resource_info/cel.md describes them: the sprite offset table sits at a fixed
   +0x0f and holds one dword per sprite, and every stored offset is measured
   from the start of the file.  The plate sheet states 40 x 8 in its header,
   because fdps_cel_blit_sprite takes the size from the SHEET and not from the
   sprite, and carries 36 entries so that sprite 0x23 exists.  A row of it is one
   fill run: command 0x27 is op 0 with a length of 39 + 1. */
#define AWD_CEL_TABLE_AT 0x0f
#define AWD_CEL_WIDTH_AT 0x07
#define AWD_CEL_HEIGHT_AT 0x09
#define AWD_PLATE_SPRITES 0x24
#define AWD_PLATE_ROW_BYTES 2
#define AWD_PLATE_STREAM_AT (AWD_CEL_TABLE_AT + AWD_PLATE_SPRITES * 4)
#define AWD_PLATE_STREAM_BYTES (AWD_LABEL_H * AWD_PLATE_ROW_BYTES)
#define AWD_PLATE_SHEET_BYTES \
    (AWD_PLATE_STREAM_AT + AWD_PLATE_SPRITES * AWD_PLATE_STREAM_BYTES)
#define AWD_PLATE_FILL_RUN 0x27
#define AWD_PLATE_COLOR 0x21

/* The digit sheet fdps_draw_number reaches through
   data_fdps_number_glyph_sheet_ptr: thirteen 6 x 8 glyphs for colour row 0,
   each a flat fill so that no digit pixel is ever transparent and the whole
   label stays a solid mask. */
#define AWD_GLYPH_W 6
#define AWD_GLYPH_H 8
#define AWD_GLYPHS 13
#define AWD_GLYPH_STREAM_AT (AWD_CEL_TABLE_AT + AWD_GLYPHS * 4)
#define AWD_GLYPH_STREAM_BYTES (AWD_GLYPH_H * 2)
#define AWD_GLYPH_SHEET_BYTES \
    (AWD_GLYPH_STREAM_AT + AWD_GLYPHS * AWD_GLYPH_STREAM_BYTES)
#define AWD_GLYPH_FILL_RUN (AWD_GLYPH_W - 1)
#define AWD_GLYPH_COLOR 0x31

static unsigned char awd_block[AWD_UNITS * UNIT_RECORD_STRIDE];
static unsigned char awd_plate_sheet[AWD_PLATE_SHEET_BYTES];
static unsigned char awd_glyph_sheet[AWD_GLYPH_SHEET_BYTES];
static short awd_grid_header[2];
static unsigned char awd_growth_table[16 * 11];
static unsigned char *awd_screen;
static unsigned char *awd_saved_cube;
static void (__interrupt __far *awd_saved_timer)();
static int awd_seed_ok;

static void __interrupt __far awd_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(awd_saved_timer);
}

static struct fdps_unit_record *awd_unit(int unit_index)
{
    return (struct fdps_unit_record *)
        (awd_block + unit_index * UNIT_RECORD_STRIDE);
}

static void awd_put_u16(unsigned char *image, int at, int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

/* Zeroes the records, builds both sheets and publishes every global the routine
   reads.  The cursor is parked on the awarded unit already, so
   fdps_map_cursor_move_to has nothing to walk and leaves the view origins where
   the case put them; the one case that wants the walk moves the cursor away
   itself. */
static void awd_stage(void)
{
    int i;
    int sprite;
    int stream_at;
    int row;

    for (i = 0; i < (int) sizeof(awd_block); i++) {
        awd_block[i] = 0;
    }
    for (i = 0; i < (int) sizeof(awd_growth_table); i++) {
        awd_growth_table[i] = 0;
    }
    awd_unit(0)->exp_carry = AWD_NEIGHBOUR_MARK;
    awd_unit(AWD_UNITS - 1)->exp_carry = AWD_NEIGHBOUR_MARK;

    awd_unit(AWD_PATIENT)->pos_x = (unsigned char) AWD_TILE_X;
    awd_unit(AWD_PATIENT)->pos_y = (unsigned char) AWD_TILE_Y;
    awd_unit(AWD_PATIENT)->portrait_id = (unsigned char) AWD_PLAIN_PORTRAIT;
    awd_unit(AWD_PATIENT)->level = (unsigned char) AWD_START_LEVEL;

    for (i = 0; i < AWD_PLATE_SHEET_BYTES; i++) {
        awd_plate_sheet[i] = 0;
    }
    awd_put_u16(awd_plate_sheet, AWD_CEL_WIDTH_AT, AWD_LABEL_W);
    awd_put_u16(awd_plate_sheet, AWD_CEL_HEIGHT_AT, AWD_LABEL_H);
    for (sprite = 0; sprite < AWD_PLATE_SPRITES; sprite++) {
        stream_at = AWD_PLATE_STREAM_AT + sprite * AWD_PLATE_STREAM_BYTES;
        *(int *) (awd_plate_sheet + AWD_CEL_TABLE_AT + sprite * 4) = stream_at;
        for (row = 0; row < AWD_LABEL_H; row++) {
            awd_plate_sheet[stream_at + row * 2] = AWD_PLATE_FILL_RUN;
            awd_plate_sheet[stream_at + row * 2 + 1] = AWD_PLATE_COLOR;
        }
    }

    for (i = 0; i < AWD_GLYPH_SHEET_BYTES; i++) {
        awd_glyph_sheet[i] = 0;
    }
    for (sprite = 0; sprite < AWD_GLYPHS; sprite++) {
        stream_at = AWD_GLYPH_STREAM_AT + sprite * AWD_GLYPH_STREAM_BYTES;
        *(int *) (awd_glyph_sheet + AWD_CEL_TABLE_AT + sprite * 4) = stream_at;
        for (row = 0; row < AWD_GLYPH_H; row++) {
            awd_glyph_sheet[stream_at + row * 2] = AWD_GLYPH_FILL_RUN;
            awd_glyph_sheet[stream_at + row * 2 + 1] = AWD_GLYPH_COLOR;
        }
    }

    awd_grid_header[0] = (short) AWD_MAP_TILES_X;
    awd_grid_header[1] = (short) AWD_MAP_TILES_Y;

    data_fdps_map_unit_array_ptr = awd_block;
    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_battle_move_grid_ptr = (unsigned char *) awd_grid_header;
    data_fdps_battle_character_growth_table_ptr = awd_growth_table;
    data_fdps_command_sprite_sheet_ptr = awd_plate_sheet;
    data_fdps_number_glyph_sheet_ptr = awd_glyph_sheet;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = AWD_TILE_X * AWD_TILE;
    data_fdps_map_cursor_world_y = AWD_TILE_Y * AWD_TILE;
    data_fdps_map_cursor_draw_mode = AWD_CURSOR_SEED;
    data_fdps_battle_pending_xp_credit = 0;
}

/* Put the staged pointers back the way a freshly started program has them, for
   the reason tests/anim.c gives: these hold blocks the game's own loaders free,
   and leaving one pointing at a static here hands a later test a free() of
   storage that never came from the heap. */
static void awd_unstage(void)
{
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_battle_character_growth_table_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_number_glyph_sheet_ptr = NULL;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_battle_pending_xp_credit = 0;
}

static void awd_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* The two blocks the routine will ask for, in its own order and at its own
   sizes, zeroed and handed straight back. */
static void awd_seed_heap(void)
{
    unsigned char *label;
    unsigned char *page;

    label = (unsigned char *) malloc((size_t) AWD_LABEL_BYTES);
    page = (unsigned char *) malloc((size_t) AWD_PAGE_BYTES);
    awd_seed_ok = (label != NULL && page != NULL);
    if (label != NULL) {
        memset(label, 0, (size_t) AWD_LABEL_BYTES);
    }
    if (page != NULL) {
        memset(page, 0, (size_t) AWD_PAGE_BYTES);
    }
    free(page);
    free(label);
}

/* One whole run, leaving the frame in awd_screen[]. */
static void awd_run(void)
{
    size_t cube_bytes;

    cube_bytes = sizeof(data_fdps_inverse_palette_cube);
    awd_screen = (unsigned char *) malloc((size_t) AWD_SCREEN_BYTES);
    awd_saved_cube = (unsigned char *) malloc(cube_bytes);
    CHECK_EQ(awd_screen != NULL && awd_saved_cube != NULL, 1);
    if (awd_screen == NULL || awd_saved_cube == NULL) {
        free(awd_screen);
        free(awd_saved_cube);
        awd_screen = NULL;
        awd_saved_cube = NULL;
        return;
    }
    memset(awd_screen, AWD_BORDER_FILL, (size_t) AWD_SCREEN_BYTES);
    memmove(awd_saved_cube, data_fdps_inverse_palette_cube, cube_bytes);
    memset(data_fdps_inverse_palette_cube, AWD_BLEND_SENTINEL, cube_bytes);

    awd_set_mode(AWD_MODE_320X200X256);
    memset((void *) AWD_VGA_BASE, AWD_BORDER_FILL, (size_t) AWD_SCREEN_BYTES);
    awd_seed_heap();

    awd_saved_timer = _dos_getvect(AWD_TIMER_VECTOR);
    _dos_setvect(AWD_TIMER_VECTOR, awd_timer_isr);
    fdps_unit_award_exp_and_level_up(AWD_PATIENT);
    _dos_setvect(AWD_TIMER_VECTOR, awd_saved_timer);

    memmove(awd_screen, (void *) AWD_VGA_BASE, (size_t) AWD_SCREEN_BYTES);
    awd_set_mode(AWD_MODE_TEXT);

    memmove(data_fdps_inverse_palette_cube, awd_saved_cube, cube_bytes);
    free(awd_saved_cube);
    awd_saved_cube = NULL;
}

static void awd_drop_screen(void)
{
    free(awd_screen);
    awd_screen = NULL;
}

static int awd_pixel(int row, int col)
{
    return (int) awd_screen[row * AWD_SCREEN_W + col];
}

/* The four edges of the trail the climb leaves, in screen coordinates: the
   label's own left column and the row the highest frame's top lands on, and the
   column and row one past the widest and lowest frame.  Between them they say
   the figure was 40 wide and 8 tall, that the first frame rose 6 and that the
   last five were held at 24. */
static void awd_trail_is(int label_x, int label_y)
{
    int left;
    int top;
    int right;
    int bottom;

    left = label_x + AWD_TO_SCREEN;
    right = left + AWD_LABEL_W - 1;
    top = label_y - AWD_RISE_HELD + AWD_TO_SCREEN;
    bottom = label_y - AWD_RISE_FIRST + AWD_LABEL_H - 1 + AWD_TO_SCREEN;

    CHECK_EQ(awd_pixel(top, left), AWD_BLEND_SENTINEL);
    CHECK_EQ(awd_pixel(bottom, right), AWD_BLEND_SENTINEL);
    CHECK_EQ(awd_pixel(top - 1, left), 0);
    CHECK_EQ(awd_pixel(bottom + 1, right), 0);
    CHECK_EQ(awd_pixel(top, left - 1), 0);
    CHECK_EQ(awd_pixel(top, right + 1), 0);
}

/* The eight record bytes the routine addresses by literal displacement, and the
   stride fdps_get_unit_record multiplies by.  If any of them moved, every case
   below would still pass while reading the wrong byte. */
static void awd_the_record_fields_sit_where_the_loads_read(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), UNIT_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 0x01);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, sprite_cache_slot), 0x02);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 0x05);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 0x07);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, level), 0x21);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, exp_carry), 0x3c);
    CHECK_EQ((int) sizeof(layout_probe.exp_carry), 1);
}

/* CMP dword ptr [0x00069cec],0x0 / JZ at 0001dd61 jumps to the RET, so nothing
   is paid, nothing is drawn and the cursor mode is not touched. */
static void awd_a_zero_accumulator_pays_nothing(void)
{
    awd_stage();
    awd_unit(AWD_PATIENT)->exp_carry = 12;
    data_fdps_battle_pending_xp_credit = 0;

    fdps_unit_award_exp_and_level_up(AWD_PATIENT);

    CHECK_EQ(awd_unit(AWD_PATIENT)->exp_carry, 12);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AWD_CURSOR_SEED);
    awd_unstage();
}

/* CALL fdps_unit_is_retired / TEST EAX,EAX / JZ at 0001dd6e takes the same exit,
   and that exit does NOT clear the accumulator: the credit is still standing
   afterwards, ready for whichever unit is awarded next. */
static void awd_a_retired_unit_keeps_the_accumulator(void)
{
    awd_stage();
    awd_unit(AWD_PATIENT)->flags = 1;
    awd_unit(AWD_PATIENT)->exp_carry = 12;
    data_fdps_battle_pending_xp_credit = 30;

    fdps_unit_award_exp_and_level_up(AWD_PATIENT);

    CHECK_EQ(data_fdps_battle_pending_xp_credit, 30);
    CHECK_EQ(awd_unit(AWD_PATIENT)->exp_carry, 12);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AWD_CURSOR_SEED);
    awd_unstage();
}

/* CMP dword ptr [EBP-0x20],0x9 / CMP dword ptr [EBP-0x24],0x63 / JZ at
   0001dd85: portrait id 9 at level 99 is capped, and the exit leaves the
   accumulator alone as well. */
static void awd_the_machine_soldier_stops_at_ninety_nine(void)
{
    awd_stage();
    awd_unit(AWD_PATIENT)->portrait_id = (unsigned char) AWD_MACHINE_PORTRAIT;
    awd_unit(AWD_PATIENT)->level = (unsigned char) AWD_MACHINE_CAP;
    data_fdps_battle_pending_xp_credit = 30;

    fdps_unit_award_exp_and_level_up(AWD_PATIENT);

    CHECK_EQ(data_fdps_battle_pending_xp_credit, 30);
    CHECK_EQ(awd_unit(AWD_PATIENT)->exp_carry, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AWD_CURSOR_SEED);
    awd_unstage();
}

/* CMP dword ptr [EBP-0x24],0x28 / JZ at 0001dd91: every other portrait id is
   capped at 40 instead. */
static void awd_every_other_character_stops_at_forty(void)
{
    awd_stage();
    awd_unit(AWD_PATIENT)->level = (unsigned char) AWD_ORDINARY_CAP;
    data_fdps_battle_pending_xp_credit = 30;

    fdps_unit_award_exp_and_level_up(AWD_PATIENT);

    CHECK_EQ(data_fdps_battle_pending_xp_credit, 30);
    CHECK_EQ(awd_unit(AWD_PATIENT)->exp_carry, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AWD_CURSOR_SEED);
    awd_unstage();
}

/* The two gates are exclusive: JNZ at 0001dd83 sends portrait id 9 to the 99
   test and never to the 40 one, so the machine soldier at level 40 is paid.
   The cleared accumulator is what says the gate let it through. */
static void awd_the_machine_soldier_is_not_stopped_at_forty(void)
{
    awd_stage();
    awd_unit(AWD_PATIENT)->portrait_id = (unsigned char) AWD_MACHINE_PORTRAIT;
    awd_unit(AWD_PATIENT)->level = (unsigned char) AWD_ORDINARY_CAP;
    data_fdps_battle_pending_xp_credit = 30;

    awd_run();

    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    CHECK_EQ(awd_unit(AWD_PATIENT)->exp_carry, 30);
    CHECK_EQ(awd_unit(AWD_PATIENT)->level, AWD_ORDINARY_CAP);
    awd_drop_screen();
    awd_unstage();
}

/* And the other way round: an ordinary portrait id is tested against 40 only,
   so one standing at 99 is paid rather than refused. */
static void awd_an_ordinary_character_is_not_stopped_at_ninety_nine(void)
{
    awd_stage();
    awd_unit(AWD_PATIENT)->level = (unsigned char) AWD_MACHINE_CAP;
    data_fdps_battle_pending_xp_credit = 30;

    awd_run();

    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    CHECK_EQ(awd_unit(AWD_PATIENT)->exp_carry, 30);
    awd_drop_screen();
    awd_unstage();
}

/* MOV AL,byte ptr [EAX+0x3c] / AND EAX,0xff / ADD EDX,EAX at 0001dded forms the
   running total, and MOV AL,byte ptr [EBP-0x10] / MOV byte ptr [EDX+0x3c],AL at
   0001e340 stores it back.  Under 100 nothing is spent on a level, so the whole
   total stays in the byte.  The accumulator is cleared, the cursor mode is set
   to the plain cursor rather than put back to what it was, and neither
   neighbouring record is touched. */
static void awd_the_award_lands_in_the_leftover_byte(void)
{
    awd_stage();
    awd_unit(AWD_PATIENT)->exp_carry = 12;
    data_fdps_battle_pending_xp_credit = 30;

    awd_run();

    CHECK_EQ(awd_unit(AWD_PATIENT)->exp_carry, 42);
    CHECK_EQ(awd_unit(AWD_PATIENT)->level, AWD_START_LEVEL);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
    CHECK_EQ(awd_unit(0)->exp_carry, AWD_NEIGHBOUR_MARK);
    CHECK_EQ(awd_unit(AWD_UNITS - 1)->exp_carry, AWD_NEIGHBOUR_MARK);
    awd_drop_screen();
    awd_unstage();
}

/* CMP dword ptr [0x00069cec],0x63 / JLE / MOV dword ptr [0x00069cec],0x63 at
   0001ddb1: the award is written back down to 99 before it is added, so the
   surplus is discarded rather than carried, and a single award can never buy
   more than one level. */
static void awd_an_award_above_ninety_nine_is_clamped(void)
{
    awd_stage();
    awd_unit(AWD_PATIENT)->exp_carry = 0;
    data_fdps_battle_pending_xp_credit = 250;

    awd_run();

    CHECK_EQ(awd_unit(AWD_PATIENT)->exp_carry, 99);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    awd_drop_screen();
    awd_unstage();
}

/* CALL fdps_map_cursor_move_to_unit at 0001dda9, with the cursor started at the
   map origin: it ends on the awarded unit's own tile, 24 pixels to the tile.
   The walk is short enough that the view does not scroll, which is what lets
   the label cases below start from a known origin. */
static void awd_the_cursor_is_parked_on_the_unit(void)
{
    awd_stage();
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_battle_pending_xp_credit = 30;

    awd_run();

    CHECK_EQ(data_fdps_map_cursor_world_x, AWD_TILE_X * AWD_TILE);
    CHECK_EQ(data_fdps_map_cursor_world_y, AWD_TILE_Y * AWD_TILE);
    CHECK_EQ(data_fdps_battle_view_window_origin_x, 0);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 0);
    awd_drop_screen();
    awd_unstage();
}

/* IMUL EAX,EAX,0x18 / SUB EAX,[0x00069ce4] / ADD EAX,0x18 at 0001de00 and the
   same on the other axis with ADD EAX,0x20 at 0001de19: with the view at the
   map origin the label sits at tile * 24 plus (24, 32). */
static void awd_the_label_floats_over_the_unit(void)
{
    awd_stage();
    data_fdps_battle_pending_xp_credit = 30;

    awd_run();

    CHECK_EQ(awd_seed_ok, 1);
    awd_trail_is(AWD_TILE_X * AWD_TILE + AWD_LABEL_ORIGIN_X,
                 AWD_TILE_Y * AWD_TILE + AWD_LABEL_ORIGIN_Y);
    awd_drop_screen();
    awd_unstage();
}

/* The two SUBs are the view scroll, so a scrolled view moves the label by
   exactly what it scrolled.  The cursor is parked on the unit already, so
   fdps_map_cursor_move_to returns before it can move the origins itself. */
static void awd_the_label_follows_the_view_scroll(void)
{
    awd_stage();
    data_fdps_battle_view_window_origin_x = 24;
    data_fdps_battle_view_window_origin_y = 48;
    data_fdps_battle_pending_xp_credit = 30;

    awd_run();

    CHECK_EQ(awd_seed_ok, 1);
    awd_trail_is(AWD_TILE_X * AWD_TILE - 24 + AWD_LABEL_ORIGIN_X,
                 AWD_TILE_Y * AWD_TILE - 48 + AWD_LABEL_ORIGIN_Y);
    awd_drop_screen();
    awd_unstage();
}

void run_unitstat_tests(void)
{
    RUN_TEST(the_record_layout_matches_the_offsets_read);
    RUN_TEST(a_heal_of_ten_rolls_nine_every_time);
    RUN_TEST(the_base_heal_truncates);
    RUN_TEST(a_negative_amount_truncates_towards_zero_and_drains);
    RUN_TEST(the_total_is_clamped_to_the_maximum_hp);
    RUN_TEST(the_return_is_the_roll_and_not_the_hp_restored);
    RUN_TEST(the_roll_spans_nine_tenths_to_the_full_amount);
    RUN_TEST(the_award_is_scaled_by_the_hp_actually_restored);
    RUN_TEST(a_full_unit_credits_nothing_but_still_returns_its_roll);
    RUN_TEST(the_award_accumulates_rather_than_replacing);
    RUN_TEST(the_award_divides_the_whole_product_last);
    RUN_TEST(a_promoted_portrait_credits_thirty_extra_levels);
    RUN_TEST(the_promotion_span_is_0x0f_through_0x21);
    RUN_TEST(an_enemy_portrait_is_healed_but_credits_nothing);
    RUN_TEST(each_index_selects_its_own_record);

    RUN_TEST(the_mp_fields_sit_where_the_word_loads_read);
    RUN_TEST(a_restore_of_ten_rolls_nine_every_time);
    RUN_TEST(the_base_restore_truncates);
    RUN_TEST(the_total_is_clamped_and_the_roll_is_still_returned);
    RUN_TEST(a_full_unit_gains_nothing_but_still_returns_its_roll);
    RUN_TEST(a_maximum_of_zero_pins_the_mp_rather_than_faulting);
    RUN_TEST(a_negative_amount_drains_mp_with_no_bottom_clamp);
    RUN_TEST(the_mp_fields_are_read_unsigned);
    RUN_TEST(the_restore_spans_nine_tenths_to_the_full_amount);
    RUN_TEST(nothing_but_the_current_mp_is_touched);
    RUN_TEST(each_index_selects_its_own_record_for_mp);

    RUN_TEST(the_spell_bitmap_sits_where_the_byte_loads_read);
    RUN_TEST(an_empty_bitmap_collects_no_ids);
    RUN_TEST(the_bits_of_a_byte_are_walked_least_significant_first);
    RUN_TEST(each_byte_contributes_eight_to_the_id);
    RUN_TEST(the_ids_are_packed_in_ascending_order);
    RUN_TEST(every_bit_set_yields_all_forty_ids_in_order);
    RUN_TEST(a_null_buffer_still_counts_every_set_bit);
    RUN_TEST(neither_neighbouring_field_is_walked);
    RUN_TEST(the_record_is_only_read);
    RUN_TEST(each_index_selects_its_own_record_for_spells);

    RUN_TEST(the_enemy_record_layout_matches_the_offsets_read);
    RUN_TEST(a_hit_of_ten_rolls_nine_every_time);
    RUN_TEST(the_base_damage_truncates);
    RUN_TEST(a_negative_nominal_figure_truncates_towards_zero_and_heals);
    RUN_TEST(an_overkill_clamps_the_hp_but_returns_the_whole_roll);
    RUN_TEST(the_damage_roll_spans_nine_tenths_to_the_full_figure);
    RUN_TEST(the_hp_fields_are_read_unsigned);
    RUN_TEST(a_negative_maximum_hp_divides_unsigned);
    RUN_TEST(a_surviving_enemy_prorates_the_award_by_the_damage);
    RUN_TEST(a_killed_enemy_keeps_the_whole_award);
    RUN_TEST(an_enemy_already_at_zero_pays_in_full_again);
    RUN_TEST(the_award_divides_the_whole_product_last_for_damage);
    RUN_TEST(the_enemy_record_is_the_portrait_id_less_0x3c);
    RUN_TEST(the_damage_award_accumulates_rather_than_replacing);
    RUN_TEST(only_side_zero_credits_experience);
    RUN_TEST(nothing_but_the_current_hp_is_written);
    RUN_TEST(each_index_selects_its_own_record_for_damage);

    RUN_TEST(the_two_fields_sit_where_the_byte_loads_read);
    RUN_TEST(an_ordinary_roster_unit_is_not_immune);
    RUN_TEST(the_machine_soldier_class_is_immune_on_its_own);
    RUN_TEST(the_lower_immune_class_run_is_0x21_through_0x22);
    RUN_TEST(the_upper_immune_class_run_is_0x24_through_0x26);
    RUN_TEST(the_mercenary_class_between_the_two_runs_is_not_immune);
    RUN_TEST(every_class_id_matches_the_transcribed_set);
    RUN_TEST(the_enemy_portrait_run_is_0x3c_through_0x44);
    RUN_TEST(neither_run_is_read_from_the_other_field);
    RUN_TEST(either_test_alone_makes_the_unit_immune);
    RUN_TEST(the_immunity_test_writes_nothing);
    RUN_TEST(each_index_selects_its_own_record_for_immunity);

    RUN_TEST(the_three_ailment_slots_sit_where_the_stores_land);
    RUN_TEST(an_immune_unit_is_never_given_an_ailment);
    RUN_TEST(an_immune_unit_still_draws_three_numbers);
    RUN_TEST(the_chance_is_the_remainder_under_twenty);
    RUN_TEST(every_landed_roll_writes_two_or_three_turns);
    RUN_TEST(the_record_matches_the_loop_read_off_the_assembly);
    RUN_TEST(an_ailment_already_running_is_overwritten);
    RUN_TEST(nothing_outside_the_three_ailment_bytes_is_written);
    RUN_TEST(each_index_selects_its_own_record_for_ailments);

    RUN_TEST(the_status_timer_offsets_match_the_assembly);
    RUN_TEST(each_ailment_id_writes_the_timer_the_assembly_names);
    RUN_TEST(a_blessing_index_writes_its_own_slot);
    RUN_TEST(the_blessing_rolls_against_spell_fourteen);
    RUN_TEST(the_rate_bounds_the_roll_at_both_ends);
    RUN_TEST(the_roll_lands_strictly_under_the_rate);
    RUN_TEST(a_landed_effect_runs_two_or_three_turns);
    RUN_TEST(an_effect_already_running_is_refused_and_not_refreshed);
    RUN_TEST(immunity_gates_the_ailments_and_not_the_blessings);
    RUN_TEST(an_immune_portrait_id_also_refuses_an_ailment);
    RUN_TEST(a_landed_effect_credits_ten_times_the_level);
    RUN_TEST(the_stream_advances_by_one_draw_per_failure_and_two_on_a_landing);
    RUN_TEST(nothing_outside_the_one_timer_byte_is_written);
    RUN_TEST(each_index_selects_its_own_record_for_status);

    RUN_TEST(the_growth_pair_layout_matches_the_offsets_read);
    RUN_TEST(an_equal_pair_gains_exactly_the_minimum);
    RUN_TEST(an_equal_pair_draws_no_random_number);
    RUN_TEST(a_wider_pair_draws_exactly_one_random_number);
    RUN_TEST(the_gain_is_the_minimum_plus_the_remainder);
    RUN_TEST(the_upper_bound_of_the_pair_is_exclusive);
    RUN_TEST(the_minimum_byte_widens_unsigned);
    RUN_TEST(the_bound_byte_widens_unsigned);
    RUN_TEST(only_the_two_bytes_at_the_pointer_are_read);
    RUN_TEST(the_addition_is_sixteen_bits_wide);
    RUN_TEST(nothing_but_the_named_field_is_written);
    RUN_TEST(the_scratch_global_carries_the_figure);
    RUN_TEST(the_figure_is_drawn_at_its_natural_width);
    RUN_TEST(no_leading_plus_is_drawn);
    RUN_TEST(the_figure_is_drawn_at_the_window_pitch);
    RUN_TEST(the_figure_starts_where_dst_points);
    RUN_TEST(the_colour_row_is_left_to_the_caller);
    RUN_TEST(the_staged_sheet_is_put_back);
    RUN_TEST(awd_the_record_fields_sit_where_the_loads_read);
    RUN_TEST(awd_a_zero_accumulator_pays_nothing);
    RUN_TEST(awd_a_retired_unit_keeps_the_accumulator);
    RUN_TEST(awd_the_machine_soldier_stops_at_ninety_nine);
    RUN_TEST(awd_every_other_character_stops_at_forty);
    RUN_TEST(awd_the_machine_soldier_is_not_stopped_at_forty);
    RUN_TEST(awd_an_ordinary_character_is_not_stopped_at_ninety_nine);
    RUN_TEST(awd_the_award_lands_in_the_leftover_byte);
    RUN_TEST(awd_an_award_above_ninety_nine_is_clamped);
    RUN_TEST(awd_the_cursor_is_parked_on_the_unit);
    RUN_TEST(awd_the_label_floats_over_the_unit);
    RUN_TEST(awd_the_label_follows_the_view_scroll);
}
