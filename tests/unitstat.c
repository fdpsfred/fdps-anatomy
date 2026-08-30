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
}
