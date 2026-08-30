/* tests/spell.c -- cover for src/spell.c.
 *
 * The file covers two functions and is in two halves: fdps_spell_damage_unit
 * first, then fdps_spell_heal_unit from the banner comment further down, which
 * carries its own account of where its figures come from.  The fixture, the
 * three staged tables and the helpers are shared.
 *
 * Every expected value in the damage half is read off the assembly of
 * fdps_spell_damage_unit at 00028320 -- the XOR EAX,EAX / MOV AL byte loads at 0002833d, 00028358 and
 * 000283e8 that zero extend the class code, the magic resistance complement
 * and the hit rate, the MOVSX word ptr [EAX] at 00028373 for the signed power,
 * the INC EAX at 00028349 that biases the class record index, the two
 * MOV EBX,0x64 / IDIV EBX pairs at 00028386 and 000283b4, the JL at 0002837d
 * that picks the formula, the MOVSX word ptr [EAX+0x48] at 000283ac and
 * MOVSX word ptr [EAX+0x4a] at 000283c6 for the two combat words, the JGE
 * clamp at 000283da, the CALL 0x00042cf8 at 000283ee that draws the hit roll,
 * the CMP EDX,[EBP-0x1c] / JGE at 00028401 that compares it, the two
 * CMP dword ptr [EBP+0x1c] tests at 00028406 and 0002840c for the spell ids,
 * and the TEST EAX,EAX / JNZ at 0002841e on the flying answer -- and from the
 * record layouts ticket 17 settled.  None of them is read off the emitted C.
 *
 * HOW THE RANDOMNESS IS TAKEN OUT.  Two rolls can run through a landing cast.
 *
 * The first is this function's own hit roll, rand() % 100 against the hit rate
 * byte.  rand() % 100 is 0..99, so a rate of 100 lands on every draw and a
 * rate of 0 lands on none: both ends are deterministic and every case below
 * uses one of them.
 *
 * The second is inside fdps_unit_apply_damage, which rolls
 * base * 9 / 10 + (rand() % 100) * base / 1000.  For any base of 10 or less
 * the second term's numerator is at most 990, which the signed divide by 1000
 * truncates to 0 whatever rand() returned, so the roll is exactly base * 9 / 10
 * -- 9 for a base of 10, 7 for 8, 5 for 6, 4 for 5.  Every damage figure the
 * fixture produces is inside that band.
 *
 * WHY THE TARGET IS ON SIDE 2.  fdps_unit_apply_damage credits battle
 * experience only when the target's side byte is 0, and that path resolves an
 * ENEMYDAT.DAT record through a table pointer these tests do not stage.  Side
 * 2, the player's, keeps the damage arithmetic on its own.
 *
 * THE PRNG ORDER IS ASSERTED, NOT ASSUMED.  The draw happens before the flying
 * test, so an immune target still consumes one value.  The last case reseeds
 * with srand and compares the next value out of the stream against the stream
 * advanced by hand, which pins how many draws each of the three exits takes:
 * one for a miss, one for the flying immunity, two for a landing hit.
 */
#include <stddef.h>
#include <stdlib.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "spell.h"

/* The strides the three accessors multiply by. */
#define UNIT_RECORD_STRIDE 0x50
#define CLASS_RECORD_STRIDE 0x0a
#define SPELL_RECORD_STRIDE 0x07

/* Four unit records are staged so that a walk which strayed into a neighbour
   would be visible.  The class table is staged out to 0x100 records because
   one case feeds a class code of 0x90 to prove the byte is zero extended, and
   the spell table to the 0x28 records MAGICDAT.DAT actually holds. */
#define STAGE_UNITS 4
#define STAGE_CLASSES 0x100
#define STAGE_SPELLS 0x28

#define CASTER 1
#define TARGET 2
#define BYSTANDER 3

/* Side 0 is the enemy side, which pays experience; 2 is the player's. */
#define PLAYER_SIDE 2

/* rand() % 100 is 0..99, so these two rates are the deterministic ends. */
#define ALWAYS_HITS 100
#define NEVER_HITS 0

/* An ordinary spell id, well away from the two the function tests for. */
#define PLAIN_SPELL 0x05
#define SPELL_QUAKE 0x0a       /* 裂地術 */
#define SPELL_GREAT_QUAKE 0x0b /* 封神裂震 */
#define OTHER_SPELL 0x0c

/* 0x1f 飛兵 is one of the five class codes fdps_unit_is_flying answers 1 for;
   0x00 is not. */
#define FLYING_CLASS 0x1f
#define GROUND_CLASS 0x00

/* Starting HP, chosen so nothing clamps unless a case asks for it. */
#define FULL_HP 100

/* A magic resistance complement of 100 is a target with no resistance at all,
   so the flat figure is the spell's power unchanged. */
#define NO_RESISTANCE 100

/* Any seed will do; the cases compare one stream against the same stream. */
#define PRNG_SEED 1

/* Twenty casts is enough that a hit rate mistaken for a probability rather
   than a floor would be seen. */
#define REPEATED_CASTS 20

static unsigned char unit_block[STAGE_UNITS * UNIT_RECORD_STRIDE];
static unsigned char class_block[STAGE_CLASSES * CLASS_RECORD_STRIDE];
static unsigned char spell_block[STAGE_SPELLS * SPELL_RECORD_STRIDE];

static struct fdps_unit_record *unit(int unit_index)
{
    return (struct fdps_unit_record *)
        (unit_block + unit_index * UNIT_RECORD_STRIDE);
}

static struct fdps_class_record *class_rec(int record_index)
{
    return (struct fdps_class_record *)
        (class_block + record_index * CLASS_RECORD_STRIDE);
}

static struct fdps_spell_effect *spell_rec(int spell_id)
{
    return (struct fdps_spell_effect *)
        (spell_block + spell_id * SPELL_RECORD_STRIDE);
}

/* Publishes the three table pointers and builds a plain target: a ground class
   on side 2 at full HP, whose class record leaves it no magic resistance.  The
   caster is a second record so that a formula reading the wrong one is
   visible. */
static void stage(void)
{
    int i;

    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_block[i] = 0;
    }
    for (i = 0; i < (int) sizeof(class_block); i++) {
        class_block[i] = 0;
    }
    for (i = 0; i < (int) sizeof(spell_block); i++) {
        spell_block[i] = 0;
    }

    data_fdps_map_unit_array_ptr = unit_block;
    data_fdps_class_table_ptr = class_block;
    data_fdps_battle_spell_effect_table_ptr = spell_block;

    unit(TARGET)->side = PLAYER_SIDE;
    unit(TARGET)->clazz = GROUND_CLASS;
    unit(TARGET)->hp_current = FULL_HP;
    unit(TARGET)->hp_max = FULL_HP;
    unit(CASTER)->side = PLAYER_SIDE;

    class_rec(GROUND_CLASS + 1)->magic_resist_complement = NO_RESISTANCE;
}

static void stage_spell(int spell_id, int power, int hit_rate)
{
    spell_rec(spell_id)->power = (short) power;
    spell_rec(spell_id)->hit_rate = (unsigned char) hit_rate;
}

static void the_record_layouts_match_the_offsets_read(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), UNIT_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, clazz), 0x20);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap), 0x48);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, dp), 0x4a);

    CHECK_EQ((int) sizeof(struct fdps_class_record), CLASS_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_class_record,
                            magic_resist_complement), 0x09);

    CHECK_EQ((int) sizeof(struct fdps_spell_effect), SPELL_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, power), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, hit_rate), 0x02);
}

/* power * complement / 100, then nine tenths of that into the HP. */
static void a_flat_power_spell_scales_by_the_magic_resist_complement(void)
{
    stage();
    stage_spell(PLAIN_SPELL, 10, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 9);
    CHECK_EQ(unit(TARGET)->hp_current, 91);

    stage();
    class_rec(GROUND_CLASS + 1)->magic_resist_complement = 50;
    stage_spell(PLAIN_SPELL, 10, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 4);
    CHECK_EQ(unit(TARGET)->hp_current, 96);
}

/* IDIV EBX at 00028390 with a 100 divisor: 10 * 25 / 100 is 2, not 3. */
static void the_flat_figure_truncates(void)
{
    stage();
    class_rec(GROUND_CLASS + 1)->magic_resist_complement = 25;
    stage_spell(PLAIN_SPELL, 10, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 1);
    CHECK_EQ(unit(TARGET)->hp_current, 99);
}

/* The caster's record is never resolved on the flat path and the target's
   defence word is never read, so neither can reach the figure. */
static void the_flat_path_ignores_the_caster_and_the_defence(void)
{
    stage();
    unit(CASTER)->ap = 999;
    unit(CASTER)->dp = 999;
    unit(TARGET)->ap = 999;
    unit(TARGET)->dp = 999;
    stage_spell(PLAIN_SPELL, 10, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 9);
    CHECK_EQ(unit(TARGET)->hp_current, 91);
}

/* XOR EAX,EAX before MOV AL,byte ptr [EDX + 0x9]: a complement of 0xc8 is 200
   and doubles the figure.  Read signed it would be -56 and the spell would
   heal. */
static void the_magic_resist_complement_is_read_unsigned(void)
{
    stage();
    class_rec(GROUND_CLASS + 1)->magic_resist_complement = 200;
    stage_spell(PLAIN_SPELL, 5, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 9);
    CHECK_EQ(unit(TARGET)->hp_current, 91);
}

/* INC EAX at 00028349: the class record read is the one AFTER the class
   code's own. */
static void the_class_record_is_the_class_code_plus_one(void)
{
    stage();
    unit(TARGET)->clazz = 0x11;
    class_rec(0x11)->magic_resist_complement = 0;
    class_rec(0x12)->magic_resist_complement = NO_RESISTANCE;
    stage_spell(PLAIN_SPELL, 10, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 9);

    stage();
    unit(TARGET)->clazz = 0x11;
    class_rec(0x11)->magic_resist_complement = NO_RESISTANCE;
    class_rec(0x12)->magic_resist_complement = 0;
    stage_spell(PLAIN_SPELL, 10, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 0);
    CHECK_EQ(unit(TARGET)->hp_current, FULL_HP);
}

/* XOR EAX,EAX before MOV AL,byte ptr [EDX + 0x20]: class code 0x90 indexes
   record 0x91.  Read signed it would index record -0x6f. */
static void the_class_code_is_read_unsigned(void)
{
    stage();
    unit(TARGET)->clazz = 0x90;
    class_rec(0x91)->magic_resist_complement = NO_RESISTANCE;
    stage_spell(PLAIN_SPELL, 10, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 9);
    CHECK_EQ(unit(TARGET)->hp_current, 91);
}

/* MOVSX word ptr [EAX] at 00028373.  A power of -100 read as an unsigned word
   would be 65436, take the flat formula and flatten the target. */
static void the_power_word_is_read_signed(void)
{
    stage();
    unit(CASTER)->ap = 10;
    unit(TARGET)->dp = 0;
    stage_spell(PLAIN_SPELL, -100, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 9);
    CHECK_EQ(unit(TARGET)->hp_current, 91);
}

/* attack * multiplier / 100 - defence, with the attack coming off the CASTER's
   record and the defence off the TARGET's.  The two records carry values that
   would give a different answer if either were read off the other. */
static void an_attack_spell_uses_the_casters_attack_and_targets_defence(void)
{
    stage();
    unit(CASTER)->ap = 4;
    unit(CASTER)->dp = 50;
    unit(TARGET)->ap = 100;
    unit(TARGET)->dp = 2;
    stage_spell(PLAIN_SPELL, -250, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 7);
    CHECK_EQ(unit(TARGET)->hp_current, 93);
}

/* IDIV at 000283be runs on the product alone: 9 * 101 / 100 is 9, and the
   defence comes off afterwards.  Dividing the difference instead would give
   (909 - 3) / 100, which is 9. */
static void the_multiplier_divides_before_the_defence_is_taken_off(void)
{
    stage();
    unit(CASTER)->ap = 9;
    unit(TARGET)->dp = 3;
    stage_spell(PLAIN_SPELL, -101, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 5);
    CHECK_EQ(unit(TARGET)->hp_current, 95);
}

/* JGE at 000283da.  Without the floor the negative figure would be handed to
   fdps_unit_apply_damage and heal the target. */
static void an_attack_spell_under_the_defence_deals_nothing(void)
{
    stage();
    unit(CASTER)->ap = 1;
    unit(TARGET)->dp = 50;
    stage_spell(PLAIN_SPELL, -100, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 0);
    CHECK_EQ(unit(TARGET)->hp_current, FULL_HP);
}

/* CMP EDX,[EBP-0x1c] / JGE at 00028401: the draw has to be BELOW the rate, so
   a rate of 0 never lands however many times it is drawn. */
static void a_hit_rate_of_zero_never_lands(void)
{
    int cast;
    int total;

    stage();
    stage_spell(PLAIN_SPELL, 10, NEVER_HITS);
    total = 0;
    for (cast = 0; cast < REPEATED_CASTS; cast++) {
        total += fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL);
    }
    CHECK_EQ(total, 0);
    CHECK_EQ(unit(TARGET)->hp_current, FULL_HP);
}

/* rand() % 100 is 0..99, so a rate of 100 lands on every draw: twenty casts of
   a figure that rolls 9 take exactly 180 HP. */
static void a_hit_rate_of_one_hundred_always_lands(void)
{
    int cast;

    stage();
    unit(TARGET)->hp_current = 1000;
    unit(TARGET)->hp_max = 1000;
    stage_spell(PLAIN_SPELL, 10, ALWAYS_HITS);
    for (cast = 0; cast < REPEATED_CASTS; cast++) {
        fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL);
    }
    CHECK_EQ(unit(TARGET)->hp_current, 1000 - 9 * REPEATED_CASTS);
}

/* XOR EAX,EAX before MOV AL,byte ptr [EDX + 0x2]: a rate of 0xc8 is 200 and
   lands on every draw.  Read signed it would be -56 and land on none. */
static void the_hit_rate_byte_is_read_unsigned(void)
{
    stage();
    stage_spell(PLAIN_SPELL, 10, 200);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 9);
    CHECK_EQ(unit(TARGET)->hp_current, 91);
}

/* CMP dword ptr [EBP + 0x1c],0xa and 0xb with the flying answer: both ids fail
   outright against an airborne target. */
static void the_two_quake_spells_miss_a_flying_target(void)
{
    stage();
    unit(TARGET)->clazz = FLYING_CLASS;
    class_rec(FLYING_CLASS + 1)->magic_resist_complement = NO_RESISTANCE;
    stage_spell(SPELL_QUAKE, 10, ALWAYS_HITS);
    stage_spell(SPELL_GREAT_QUAKE, 10, ALWAYS_HITS);

    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, SPELL_QUAKE), 0);
    CHECK_EQ(unit(TARGET)->hp_current, FULL_HP);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, SPELL_GREAT_QUAKE), 0);
    CHECK_EQ(unit(TARGET)->hp_current, FULL_HP);
}

/* The immunity is keyed to those two ids and to nothing else: the ids either
   side of them land on the same airborne target. */
static void a_flying_target_takes_every_other_spell(void)
{
    stage();
    unit(TARGET)->clazz = FLYING_CLASS;
    class_rec(FLYING_CLASS + 1)->magic_resist_complement = NO_RESISTANCE;
    stage_spell(0x09, 10, ALWAYS_HITS);
    stage_spell(OTHER_SPELL, 10, ALWAYS_HITS);

    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, 0x09), 9);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, OTHER_SPELL), 9);
    CHECK_EQ(unit(TARGET)->hp_current, FULL_HP - 18);
}

/* And it is keyed to the target being airborne, not to the spell alone. */
static void a_ground_target_takes_a_quake_spell(void)
{
    stage();
    stage_spell(SPELL_QUAKE, 10, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, SPELL_QUAKE), 9);
    CHECK_EQ(unit(TARGET)->hp_current, 91);
}

/* CALL 0x00042cf8 at 000283ee comes BEFORE the flying test at 00028416, so an
   immune target still consumes one value.  Each exit is compared against the
   same stream advanced by hand: a miss and the immunity take one draw, a
   landing hit takes two -- its own and fdps_unit_apply_damage's. */
static void each_exit_consumes_its_own_number_of_draws(void)
{
    int after_call;
    int by_hand;
    int first;
    int second;

    /* The stream has to advance for any of this to mean anything. */
    srand(PRNG_SEED);
    first = rand();
    second = rand();
    CHECK_EQ(first == second, 0);

    /* A failed hit roll: one draw. */
    stage();
    stage_spell(PLAIN_SPELL, 10, NEVER_HITS);
    srand(PRNG_SEED);
    fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL);
    after_call = rand();
    srand(PRNG_SEED);
    rand();
    by_hand = rand();
    CHECK_EQ(after_call, by_hand);

    /* A flying target under 裂地術: still one draw. */
    stage();
    unit(TARGET)->clazz = FLYING_CLASS;
    class_rec(FLYING_CLASS + 1)->magic_resist_complement = NO_RESISTANCE;
    stage_spell(SPELL_QUAKE, 10, ALWAYS_HITS);
    srand(PRNG_SEED);
    fdps_spell_damage_unit(CASTER, TARGET, SPELL_QUAKE);
    after_call = rand();
    srand(PRNG_SEED);
    rand();
    by_hand = rand();
    CHECK_EQ(after_call, by_hand);

    /* A landing hit: two, the second one inside fdps_unit_apply_damage. */
    stage();
    stage_spell(PLAIN_SPELL, 10, ALWAYS_HITS);
    srand(PRNG_SEED);
    fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL);
    after_call = rand();
    srand(PRNG_SEED);
    rand();
    rand();
    by_hand = rand();
    CHECK_EQ(after_call, by_hand);
}

/* The only write this function is responsible for is the one
   fdps_unit_apply_damage makes to the target's current HP. */
static void nothing_but_the_targets_hp_is_written(void)
{
    stage();
    unit(CASTER)->ap = 8;
    unit(CASTER)->hp_current = 55;
    unit(BYSTANDER)->hp_current = 77;
    unit(0)->hp_current = 88;
    unit(TARGET)->ap = 33;
    unit(TARGET)->dp = 0;
    stage_spell(PLAIN_SPELL, -100, ALWAYS_HITS);

    CHECK_EQ(fdps_spell_damage_unit(CASTER, TARGET, PLAIN_SPELL), 7);
    CHECK_EQ(unit(TARGET)->hp_current, 93);
    CHECK_EQ(unit(TARGET)->hp_max, FULL_HP);
    CHECK_EQ(unit(TARGET)->ap, 33);
    CHECK_EQ(unit(TARGET)->dp, 0);
    CHECK_EQ(unit(TARGET)->clazz, GROUND_CLASS);
    CHECK_EQ(unit(CASTER)->ap, 8);
    CHECK_EQ(unit(CASTER)->hp_current, 55);
    CHECK_EQ(unit(BYSTANDER)->hp_current, 77);
    CHECK_EQ(unit(0)->hp_current, 88);
    CHECK_EQ(spell_rec(PLAIN_SPELL)->power, -100);
    CHECK_EQ(class_rec(GROUND_CLASS + 1)->magic_resist_complement,
             NO_RESISTANCE);
}

/* Both indices select their own record: the damage lands on the unit the
   second argument names, and the attack comes from the one the first names. */
static void each_index_selects_its_own_record(void)
{
    stage();
    unit(BYSTANDER)->side = PLAYER_SIDE;
    unit(BYSTANDER)->clazz = GROUND_CLASS;
    unit(BYSTANDER)->hp_current = FULL_HP;
    unit(BYSTANDER)->hp_max = FULL_HP;
    stage_spell(PLAIN_SPELL, 10, ALWAYS_HITS);

    CHECK_EQ(fdps_spell_damage_unit(CASTER, BYSTANDER, PLAIN_SPELL), 9);
    CHECK_EQ(unit(BYSTANDER)->hp_current, 91);
    CHECK_EQ(unit(TARGET)->hp_current, FULL_HP);

    stage();
    unit(CASTER)->ap = 10;
    unit(BYSTANDER)->ap = 0;
    unit(TARGET)->ap = 10;
    unit(TARGET)->dp = 0;
    stage_spell(PLAIN_SPELL, -100, ALWAYS_HITS);
    CHECK_EQ(fdps_spell_damage_unit(BYSTANDER, TARGET, PLAIN_SPELL), 0);
    CHECK_EQ(unit(TARGET)->hp_current, FULL_HP);
}

/* ---- fdps_spell_heal_unit @ 00028570 -------------------------------------
 *
 * The function has no branches: fetch the record at 00018bd0, MOVSX its power
 * word at 0002858e, hand that to fdps_unit_apply_heal at 0002709c, return that
 * call's own EAX.  So every figure below is the roll fdps_unit_apply_heal
 * makes -- amount * 9 / 10 + (rand() % 100) * amount / 1000 -- and the cases
 * are about which value reaches it and what comes back.
 *
 * A power of 10 or -10 keeps that roll deterministic for the same reason the
 * damage cases above are deterministic: the spread term's numerator tops out
 * at 990, which the signed divide by 1000 truncates towards zero whatever
 * rand() returned.  So 10 rolls exactly 9 and -10 rolls exactly -9.
 *
 * Every heal case stages its spell with a hit rate of 0.  This function draws
 * no hit roll at all -- there is no PRNG call in its body -- so a rate that
 * would never land in fdps_spell_damage_unit changes nothing here, and using
 * it says so. */

/* A heal power that rolls to a whole number, and what it rolls to. */
#define HEAL_POWER 10
#define HEAL_ROLL 9

/* A level that pays a round experience figure at a maximum HP of 100:
   4 * 25 * 9 / 100 is 9. */
#define HEALED_LEVEL 4
#define EXPECTED_XP_CREDIT 9

/* Portrait ids below 0x3c are the roster, the only ones that pay experience. */
#define ROSTER_PORTRAIT_ID 0x00

/* Two adjacent spell ids, so a record index off by one would be seen. */
#define HEAL_SPELL 0x05
#define NEXT_SPELL 0x06

/* Half HP, so a heal has room to land without reaching the clamp. */
#define WOUNDED_HP 50

/* The power word goes to fdps_unit_apply_heal and the HP goes up by the nine
   tenths of it that rolls. */
static void a_heal_adds_the_rolled_power_to_the_units_hp(void)
{
    stage();
    unit(TARGET)->hp_current = WOUNDED_HP;
    stage_spell(HEAL_SPELL, HEAL_POWER, NEVER_HITS);

    CHECK_EQ(fdps_spell_heal_unit(TARGET, HEAL_SPELL), HEAL_ROLL);
    CHECK_EQ(unit(TARGET)->hp_current, WOUNDED_HP + HEAL_ROLL);
    CHECK_EQ(unit(TARGET)->hp_max, FULL_HP);
}

/* MOVSX word ptr [EAX] at 0002858e, and no sign test between there and the
   call.  A power of -10 takes HP off.  Read as an unsigned word it would be
   65526, roll tens of thousands and clamp the unit to its maximum instead. */
static void the_power_word_is_read_signed_on_the_heal_path(void)
{
    stage();
    unit(TARGET)->hp_current = WOUNDED_HP;
    stage_spell(HEAL_SPELL, -HEAL_POWER, NEVER_HITS);

    CHECK_EQ(fdps_spell_heal_unit(TARGET, HEAL_SPELL), -HEAL_ROLL);
    CHECK_EQ(unit(TARGET)->hp_current, WOUNDED_HP - HEAL_ROLL);
}

/* IMUL EAX,[EBP+0x14],0x7 at 00018bdc with no INC after it: the record is the
   spell id's own, unlike the class table's biased lookup.  The neighbouring
   record holds the negated power, so an index off by one in either direction
   shows up as a heal that ran backwards. */
static void the_spell_record_is_the_id_itself_with_no_bias(void)
{
    stage();
    unit(TARGET)->hp_current = WOUNDED_HP;
    stage_spell(HEAL_SPELL, HEAL_POWER, NEVER_HITS);
    stage_spell(NEXT_SPELL, -HEAL_POWER, NEVER_HITS);

    CHECK_EQ(fdps_spell_heal_unit(TARGET, HEAL_SPELL), HEAL_ROLL);
    CHECK_EQ(unit(TARGET)->hp_current, WOUNDED_HP + HEAL_ROLL);
    CHECK_EQ(fdps_spell_heal_unit(TARGET, NEXT_SPELL), -HEAL_ROLL);
    CHECK_EQ(unit(TARGET)->hp_current, WOUNDED_HP);
}

/* MOV dword ptr [EBP + -0x4],EAX at 000285a4 stores the call's own answer and
   the epilogue loads it straight back: the return is the roll
   fdps_unit_apply_heal made, not the HP the record gained.  A unit two short
   of its maximum gains two and still reports nine. */
static void the_return_is_the_roll_and_not_the_hp_restored(void)
{
    stage();
    unit(TARGET)->hp_current = FULL_HP - 2;
    stage_spell(HEAL_SPELL, HEAL_POWER, NEVER_HITS);

    CHECK_EQ(fdps_spell_heal_unit(TARGET, HEAL_SPELL), HEAL_ROLL);
    CHECK_EQ(unit(TARGET)->hp_current, FULL_HP);
}

/* The unit index is forwarded unchanged: the heal lands on the record it names
   and on no other. */
static void the_heal_lands_on_the_unit_the_index_names(void)
{
    stage();
    unit(TARGET)->hp_current = WOUNDED_HP;
    unit(BYSTANDER)->hp_current = WOUNDED_HP;
    unit(BYSTANDER)->hp_max = FULL_HP;
    unit(CASTER)->hp_current = WOUNDED_HP;
    unit(CASTER)->hp_max = FULL_HP;
    stage_spell(HEAL_SPELL, HEAL_POWER, NEVER_HITS);

    CHECK_EQ(fdps_spell_heal_unit(BYSTANDER, HEAL_SPELL), HEAL_ROLL);
    CHECK_EQ(unit(BYSTANDER)->hp_current, WOUNDED_HP + HEAL_ROLL);
    CHECK_EQ(unit(TARGET)->hp_current, WOUNDED_HP);
    CHECK_EQ(unit(CASTER)->hp_current, WOUNDED_HP);
}

/* The forward really is to fdps_unit_apply_heal and not to a bare HP add: that
   routine credits (level * 25 * HP restored) / maximum HP into the pending
   battle-experience accumulator for a roster portrait id, and the figure
   arrives.  4 * 25 * 9 / 100 is 9, from the IMUL,0x19 at 0002714b and the
   IDIV by the maximum HP at 00027158. */
static void the_heal_credits_battle_experience_through_the_shared_routine(void)
{
    stage();
    unit(TARGET)->hp_current = WOUNDED_HP;
    unit(TARGET)->level = HEALED_LEVEL;
    unit(TARGET)->portrait_id = ROSTER_PORTRAIT_ID;
    stage_spell(HEAL_SPELL, HEAL_POWER, NEVER_HITS);

    data_fdps_battle_pending_xp_credit = 0;
    CHECK_EQ(fdps_spell_heal_unit(TARGET, HEAL_SPELL), HEAL_ROLL);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, EXPECTED_XP_CREDIT);
}

/* Only the power word at record +0x00 is read.  The hit rate at +0x02, the
   cast range flags at +0x03, the area at +0x04, the MP cost at +0x05 and the
   target side at +0x06 all carry values that would block or divert the heal if
   any of them were consulted, and the heal lands unchanged with the caster's
   MP untouched. */
static void nothing_of_the_record_but_the_power_is_consulted(void)
{
    stage();
    unit(TARGET)->hp_current = WOUNDED_HP;
    unit(TARGET)->mp_current = 30;
    stage_spell(HEAL_SPELL, HEAL_POWER, NEVER_HITS);
    spell_rec(HEAL_SPELL)->cast_range_flags = 0xff;
    spell_rec(HEAL_SPELL)->area = 0xff;
    spell_rec(HEAL_SPELL)->mp_cost = 0xff;
    spell_rec(HEAL_SPELL)->target_side = 0xff;

    CHECK_EQ(fdps_spell_heal_unit(TARGET, HEAL_SPELL), HEAL_ROLL);
    CHECK_EQ(unit(TARGET)->hp_current, WOUNDED_HP + HEAL_ROLL);
    CHECK_EQ(unit(TARGET)->mp_current, 30);
    CHECK_EQ(spell_rec(HEAL_SPELL)->mp_cost, 0xff);
}

/* There is no PRNG call in this body, so the only draw a heal takes is the
   spread term inside fdps_unit_apply_heal: exactly one, against the two a
   landing fdps_spell_damage_unit takes. */
static void a_heal_consumes_exactly_one_draw(void)
{
    int after_call;
    int by_hand;

    stage();
    unit(TARGET)->hp_current = WOUNDED_HP;
    stage_spell(HEAL_SPELL, HEAL_POWER, NEVER_HITS);

    srand(PRNG_SEED);
    fdps_spell_heal_unit(TARGET, HEAL_SPELL);
    after_call = rand();

    srand(PRNG_SEED);
    rand();
    by_hand = rand();
    CHECK_EQ(after_call, by_hand);
}

void run_spell_tests(void)
{
    RUN_TEST(the_record_layouts_match_the_offsets_read);
    RUN_TEST(a_flat_power_spell_scales_by_the_magic_resist_complement);
    RUN_TEST(the_flat_figure_truncates);
    RUN_TEST(the_flat_path_ignores_the_caster_and_the_defence);
    RUN_TEST(the_magic_resist_complement_is_read_unsigned);
    RUN_TEST(the_class_record_is_the_class_code_plus_one);
    RUN_TEST(the_class_code_is_read_unsigned);
    RUN_TEST(the_power_word_is_read_signed);
    RUN_TEST(an_attack_spell_uses_the_casters_attack_and_targets_defence);
    RUN_TEST(the_multiplier_divides_before_the_defence_is_taken_off);
    RUN_TEST(an_attack_spell_under_the_defence_deals_nothing);
    RUN_TEST(a_hit_rate_of_zero_never_lands);
    RUN_TEST(a_hit_rate_of_one_hundred_always_lands);
    RUN_TEST(the_hit_rate_byte_is_read_unsigned);
    RUN_TEST(the_two_quake_spells_miss_a_flying_target);
    RUN_TEST(a_flying_target_takes_every_other_spell);
    RUN_TEST(a_ground_target_takes_a_quake_spell);
    RUN_TEST(each_exit_consumes_its_own_number_of_draws);
    RUN_TEST(nothing_but_the_targets_hp_is_written);
    RUN_TEST(each_index_selects_its_own_record);

    RUN_TEST(a_heal_adds_the_rolled_power_to_the_units_hp);
    RUN_TEST(the_power_word_is_read_signed_on_the_heal_path);
    RUN_TEST(the_spell_record_is_the_id_itself_with_no_bias);
    RUN_TEST(the_return_is_the_roll_and_not_the_hp_restored);
    RUN_TEST(the_heal_lands_on_the_unit_the_index_names);
    RUN_TEST(the_heal_credits_battle_experience_through_the_shared_routine);
    RUN_TEST(nothing_of_the_record_but_the_power_is_consulted);
    RUN_TEST(a_heal_consumes_exactly_one_draw);
}
