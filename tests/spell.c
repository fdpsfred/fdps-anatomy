/* tests/spell.c -- cover for src/spell.c.
 *
 * The file covers four functions and is in four parts, each starting at its
 * own banner: fdps_spell_damage_unit first, then fdps_spell_heal_unit, then
 * fdps_spell_deduct_mp_cost, then fdps_play_spell_11_cutscene.  Each banner
 * carries its own account of where that part's figures come from.  The
 * fixture, the three staged tables and the helpers are shared by the first
 * three parts; the cutscene part stages the adapter, the two blend tables and
 * a palette of its own and shares nothing with them.
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
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
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

/* ---- fdps_spell_deduct_mp_cost @ 000285c0 --------------------------------
 *
 * The function has no branches either: fetch the spell record at 000285d0,
 * fetch the unit record at 000285df, MOVSX the current MP word at 000285ed,
 * take the cost byte zero-extended with XOR EDX,EDX / MOV DL,byte ptr [EAX+0x5]
 * at 000285f7, SUB and store the low 16 bits back with MOV word ptr
 * [EAX+0x44],BX at 00028604.  Every figure below is that subtraction, and the
 * cases are about which value reaches it, how each is widened, and what is left
 * alone.
 *
 * No PRNG is involved on this path -- there is no call in the body but the two
 * accessors -- so no case here has anything to take the randomness out of, and
 * the staged records carry hit rates that would never land in
 * fdps_spell_damage_unit to say that this function does not roll.
 *
 * The MOVSX at 000285ed is not separately observable: only the low 16 bits of
 * the difference are stored, and a sign-extended and a zero-extended MP agree
 * modulo 65536, so the same word comes out either way.  What the cases below
 * do pin is the half that is observable -- that the cost byte is unsigned, that
 * the field takes a negative value rather than being floored, and that the
 * store is a word. */

/* The MP the acting unit starts a case with, and a cost that leaves a round
   remainder. */
#define MP_START 20
#define MP_COST 5

/* A cost byte of 0xff: 255 unsigned, -1 read signed. */
#define MP_COST_MAX_BYTE 0xff

/* The most negative signed word, for the wrap case. */
#define MP_MOST_NEGATIVE (-32768)

/* Two adjacent ids again, so a record index off by one is visible. */
#define COST_SPELL 0x07
#define COST_NEXT_SPELL 0x08

static void stage_cost(int spell_id, int mp_cost)
{
    spell_rec(spell_id)->mp_cost = (unsigned char) mp_cost;
}

/* The two offsets the deduction addresses, +0x44 in the unit record and +0x05
   in the spell record, against the layouts ticket 17 settled. */
static void the_mp_offsets_the_deduction_reaches_are_where_ticket_17_puts_them(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_current), 0x44);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_max), 0x46);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, mp_cost), 0x05);
}

/* SUB EBX,EDX at 00028602: the cost comes straight off the current MP. */
static void a_cast_takes_the_records_cost_off_the_casters_mp(void)
{
    stage();
    unit(CASTER)->mp_current = MP_START;
    unit(CASTER)->mp_max = MP_START;
    stage_spell(COST_SPELL, 10, NEVER_HITS);
    stage_cost(COST_SPELL, MP_COST);

    fdps_spell_deduct_mp_cost(CASTER, COST_SPELL);
    CHECK_EQ(unit(CASTER)->mp_current, MP_START - MP_COST);
    CHECK_EQ(unit(CASTER)->mp_max, MP_START);
}

/* There is no CMP anywhere in the body, so the SUB at 00028602 is
   unconditional: a cost byte of zero still runs it and takes nothing off,
   rather than the lookup being skipped.  No MAGICDAT.DAT record actually
   carries a zero cost -- the smallest is 4 (assets/spells.md) -- so what this
   pins is the shape of the arithmetic, not a case the game reaches. */
static void a_zero_cost_record_leaves_the_mp_where_it_was(void)
{
    stage();
    unit(CASTER)->mp_current = MP_START;
    stage_spell(COST_SPELL, 10, NEVER_HITS);
    stage_cost(COST_SPELL, 0);

    fdps_spell_deduct_mp_cost(CASTER, COST_SPELL);
    CHECK_EQ(unit(CASTER)->mp_current, MP_START);
}

/* XOR EDX,EDX before MOV DL,byte ptr [EAX + 0x5]: a cost byte of 0xff is 255
   and takes 255 off.  Read signed it would be -1 and ADD one MP instead. */
static void the_mp_cost_byte_is_read_unsigned(void)
{
    stage();
    unit(CASTER)->mp_current = 10;
    stage_spell(COST_SPELL, 10, NEVER_HITS);
    stage_cost(COST_SPELL, MP_COST_MAX_BYTE);

    fdps_spell_deduct_mp_cost(CASTER, COST_SPELL);
    CHECK_EQ(unit(CASTER)->mp_current, 10 - 255);
}

/* There is no test of the current MP against the cost and no clamp after the
   subtraction: a cost above the MP leaves the field negative. */
static void a_cost_above_the_current_mp_leaves_the_field_negative(void)
{
    stage();
    unit(CASTER)->mp_current = 3;
    stage_spell(COST_SPELL, 10, NEVER_HITS);
    stage_cost(COST_SPELL, 10);

    fdps_spell_deduct_mp_cost(CASTER, COST_SPELL);
    CHECK_EQ(unit(CASTER)->mp_current, -7);

    /* And a second charge takes it further down rather than stopping. */
    fdps_spell_deduct_mp_cost(CASTER, COST_SPELL);
    CHECK_EQ(unit(CASTER)->mp_current, -17);
}

/* MOV word ptr [EAX + 0x44],BX stores the low 16 bits of an int-width
   difference: one MP off the most negative word wraps to the most positive one
   rather than saturating. */
static void the_difference_is_stored_as_a_word_and_wraps(void)
{
    stage();
    unit(CASTER)->mp_current = (short) MP_MOST_NEGATIVE;
    stage_spell(COST_SPELL, 10, NEVER_HITS);
    stage_cost(COST_SPELL, 1);

    fdps_spell_deduct_mp_cost(CASTER, COST_SPELL);
    CHECK_EQ(unit(CASTER)->mp_current, 32767);
}

/* Nothing reads record +0x46, so an MP already above the unit's maximum is not
   pulled down to it and the maximum itself is not touched. */
static void the_maximum_mp_is_not_consulted(void)
{
    stage();
    unit(CASTER)->mp_current = 40;
    unit(CASTER)->mp_max = 10;
    stage_spell(COST_SPELL, 10, NEVER_HITS);
    stage_cost(COST_SPELL, MP_COST);

    fdps_spell_deduct_mp_cost(CASTER, COST_SPELL);
    CHECK_EQ(unit(CASTER)->mp_current, 35);
    CHECK_EQ(unit(CASTER)->mp_max, 10);
}

/* The current MP word is the only thing written: the caster's other fields, the
   neighbouring unit records and the spell record all come through unchanged. */
static void nothing_but_the_casters_mp_word_is_written(void)
{
    stage();
    unit(CASTER)->mp_current = MP_START;
    unit(CASTER)->mp_max = MP_START;
    unit(CASTER)->hp_current = FULL_HP;
    unit(CASTER)->ap = 33;
    unit(CASTER)->level = 7;
    unit(TARGET)->mp_current = MP_START;
    unit(BYSTANDER)->mp_current = MP_START;
    unit(0)->mp_current = MP_START;
    stage_spell(COST_SPELL, 10, NEVER_HITS);
    stage_cost(COST_SPELL, MP_COST);
    spell_rec(COST_SPELL)->target_side = 0xff;

    fdps_spell_deduct_mp_cost(CASTER, COST_SPELL);
    CHECK_EQ(unit(CASTER)->mp_current, MP_START - MP_COST);
    CHECK_EQ(unit(CASTER)->mp_max, MP_START);
    CHECK_EQ(unit(CASTER)->hp_current, FULL_HP);
    CHECK_EQ(unit(CASTER)->ap, 33);
    CHECK_EQ(unit(CASTER)->level, 7);
    CHECK_EQ(unit(TARGET)->mp_current, MP_START);
    CHECK_EQ(unit(BYSTANDER)->mp_current, MP_START);
    CHECK_EQ(unit(0)->mp_current, MP_START);
    CHECK_EQ(spell_rec(COST_SPELL)->mp_cost, MP_COST);
    CHECK_EQ(spell_rec(COST_SPELL)->power, 10);
    CHECK_EQ(spell_rec(COST_SPELL)->target_side, 0xff);
}

/* The first argument selects the unit and the second the spell, in that order:
   the call site at 0001ad6c pushes the spell id first and the unit index
   second, so the unit index is parameter one.  Swapping them would charge the
   wrong record with the wrong cost, and the fixture makes both halves of that
   visible -- the two units carry different MP and the two adjacent spell
   records different costs. */
static void each_argument_selects_its_own_record(void)
{
    stage();
    unit(CASTER)->mp_current = MP_START;
    unit(BYSTANDER)->mp_current = MP_START;
    stage_spell(COST_SPELL, 10, NEVER_HITS);
    stage_spell(COST_NEXT_SPELL, 10, NEVER_HITS);
    stage_cost(COST_SPELL, MP_COST);
    stage_cost(COST_NEXT_SPELL, MP_COST * 2);

    fdps_spell_deduct_mp_cost(BYSTANDER, COST_SPELL);
    CHECK_EQ(unit(BYSTANDER)->mp_current, MP_START - MP_COST);
    CHECK_EQ(unit(CASTER)->mp_current, MP_START);

    fdps_spell_deduct_mp_cost(BYSTANDER, COST_NEXT_SPELL);
    CHECK_EQ(unit(BYSTANDER)->mp_current, MP_START - MP_COST - MP_COST * 2);
    CHECK_EQ(unit(CASTER)->mp_current, MP_START);
}

/* IMUL EAX,[EBP+0x14],0x7 in fdps_get_spell_record with no INC after it: the
   record is the spell id's own, not the biased lookup the class table uses.
   The records either side hold costs that would show an index off by one in
   either direction. */
static void the_cost_record_is_the_spell_id_itself_with_no_bias(void)
{
    stage();
    unit(CASTER)->mp_current = MP_START;
    stage_cost(COST_SPELL - 1, 1);
    stage_cost(COST_SPELL, MP_COST);
    stage_cost(COST_SPELL + 1, 100);

    fdps_spell_deduct_mp_cost(CASTER, COST_SPELL);
    CHECK_EQ(unit(CASTER)->mp_current, MP_START - MP_COST);
}

/* ------------------------------------------------------------------ *
 * 00028610  fdps_play_spell_11_cutscene                              *
 * ------------------------------------------------------------------ *
 *
 * WHAT THE RUN IS OBSERVED THROUGH.  The routine takes nothing, returns
 * nothing and composes on a page it allocates and frees inside the call, so
 * everything it does is seen either on the mode 13h aperture or in the DAC.
 * The case below puts the adapter into the mode the game plays in, fills the
 * frame with a sentinel and the whole DAC with another, runs the whole
 * presentation once, and reads the aperture and the DAC back afterwards.  The
 * frames themselves are gone by then -- the teardown blanks the aperture -- so
 * they are watched from inside the timer interrupt instead.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED.  Every one of the 44 animation ticks and
 * every one of the six frames the teardown renders ends waiting for
 * data_fdps_timer_tick_counter to change, and in the game that counter is
 * advanced by fdps_timer_tick_handler off AIL's timer.  Nothing advances it in
 * a test image, so the run hooks IRQ0 for the duration of the call with a
 * handler that increments the counter, samples the aperture and chains to the
 * one that was there.  The wait ends only when the counter moves, so every
 * presented frame is on the adapter across at least one of those samples.
 *
 * HOW A FRAME NAMES THE BLEND LEVEL IT WAS DRAWN AT.  fdps_rle_blit_translucent
 * folds a weighted source and a weighted destination into a 4-bit g:r:b triple
 * and resolves that through the inverse colour cube (rleblend.c).  For a blend
 * level above 8 the destination is weighted through shade-ramp row 25 - level
 * and the source through row 16 - level; at 8 and below the two rows are
 * level + 9 and level.  So the fixture leaves the whole ramp at zero except for
 * ONE entry in each of rows 10 to 16 -- the destination rows of levels 15 down
 * to 9 -- namely the entry the frozen screen's own byte indexes, which it sets
 * to level << 12.  A frame drawn at one of those seven levels over the frozen
 * screen therefore folds to cube index level << 8 whatever its source pixels
 * are, and the cube answers CUT_MARK_BASE + level there.  Every other fold in
 * the run lands on cube index 0, which answers CUT_BLACK_MARK.
 *
 * Rows 10 to 16 are also the SOURCE rows of levels 1 to 7, and that is why only
 * the sentinel's entry is staged rather than the whole row: the value the fold
 * looks up in a source row is an artwork pixel, and 0x0a is one of the 129 byte
 * values Mag11.saf's 1,817 tiles never carry, so no frame at a low level can
 * reach a staged entry.  That was checked by counting every pixel byte the 4-op
 * RLE of resource_info/cel.md produces out of the shipped member.  All of this
 * is a fixture for observing the function under test, not an assertion about
 * what the two tables hold in the game.
 *
 * WHY EVERY FRAME IS ONE FLAT COLOUR.  Mag11.saf's 22 frames each carry one
 * layer whose tilemap is 14 x 9 cells of 24 x 24 -- 336 x 216 laid at the page
 * margin, so it covers the whole 320 x 200 window -- and not one of the 17
 * tilemaps leaves a single visible pixel transparent.  Every visible byte of
 * every presented frame therefore goes through the fold above, which is what
 * makes "the whole frame is one value" a usable reading.  That was checked
 * against the shipped member by walking all 17 tilemaps' cells through the
 * 4-op RLE of resource_info/cel.md.
 *
 * WHERE THE EXPECTED VALUES COME FROM.  MOV dword ptr [EBP-0x10],0x1f at
 * 000286c5 with CMP against 0 / JG for the fade-in's 31 ticks; MOV EDX,[EBP-
 * 0x10] / SAR EDX,0x1f / SUB EAX,EDX / SAR EAX,1 at 000286e2 for the level
 * being its signed half, so the level runs 15, 15, 14, 14 down to 0; the six
 * pushes at 000286ef -- 0xc8, 0x140, 0x170, page + 0x2298, 0x140, snapshot --
 * for the refill that puts the frozen screen under every fade-in frame; PUSH
 * 0xfa00 / PUSH 0x0 / PUSH 0xa0000 / CALL memset at 00028887 for the blanked
 * aperture; and PUSH 0xff / PUSH 0x0 with IMUL EAX,[EBP-0x10],0xa at 000288b2
 * and the counter running 5 down to 0 for the last palette upload being the
 * whole DAC at bias 0.  None of them is read off the emitted C.
 *
 * WHAT IS NOT ASSERTED.  The white flash -- the 0x3f bias at 0002886c and the
 * five steps of 50, 40, 30, 20 and 10 that walk out of it -- is overwritten by
 * the bias-0 upload before the call returns, and the DAC cannot be read from
 * the interrupt handler without cutting into the very write sequences it would
 * be reading.  Its arithmetic is a playtest contract
 * (rebuild_info/pitfalls.md).  So is the main phase's memset of the page: the
 * clip covers every visible pixel opaquely and that phase runs at level 0,
 * where the fold ignores the destination entirely, so clearing the page and
 * leaving it alone put the same bytes on the adapter for THIS clip.  The two
 * retrace spins are in the same position, and so is the uninitialised frame
 * latch, whose only effect is one tick of pacing.
 */

#define CUT_VGA_BASE 0x000a0000
#define CUT_SCREEN_BYTES 64000
#define CUT_MODE_320X200X256 0x13
#define CUT_MODE_TEXT 0x03
#define CUT_TIMER_VECTOR 8

#define CUT_DAC_READ_INDEX 0x3c7
#define CUT_DAC_WRITE_INDEX 0x3c8
#define CUT_DAC_DATA 0x3c9
#define CUT_DAC_ENTRIES 256
#define CUT_READING_SLOTS 6

/* The shade ramp's shape, restated from the fold at 00057648 rather than taken
   from src/: 0x100 entries to a row, and the destination row of a level above
   8 is 25 - level. */
#define CUT_RAMP_ROW_ENTRIES 0x100
#define CUT_HIGH_LEVEL_DEST_ROW(level) (25 - (level))

/* The levels whose destination row the fixture stages, which are the only ones
   a presented frame can name.  The mask has one bit per level. */
#define CUT_TOP_LEVEL 15
#define CUT_BOTTOM_LEVEL 9
#define CUT_ALL_LEVELS_MASK 0xfe00

/* The pre-call picture, one flat byte.  Only its low nibble reaches the cube
   index, and 0x0a is disjoint from both marks below. */
#define CUT_SENTINEL 0x0a
/* What the cube answers for a frame drawn over the frozen screen at level L,
   and for every fold that lands on cube index 0. */
#define CUT_MARK_BASE 0x80
#define CUT_BLACK_MARK 0x3b
/* What the whole DAC is filled with before the call, so that a component still
   holding it afterwards says no upload reached that entry. */
#define CUT_DAC_SENTINEL 0x2a

/* A chapter with no arm in fdps_cycle_scene_palette's dispatch, so the six
   rendered frames the teardown runs cannot write the DAC behind the last
   upload. */
#define CUT_QUIET_CHAPTER 1

/* How many bytes apart the interrupt handler probes the aperture, and how many
   ticks it keeps a reading for.  73 is coprime to the 320-byte row, so the
   probe walks every column; 128 covers the 44 animation ticks, the six
   rendered frames and the ticks the member load costs. */
#define CUT_PROBE_STRIDE 73
#define CUT_SAMPLES 128

static struct fdps_palette_entry cut_palette[CUT_DAC_ENTRIES];

static volatile int cut_sample[CUT_SAMPLES];
static volatile int cut_sample_count;

static int cut_return_dac[CUT_READING_SLOTS];
static int cut_first_screen_byte;
static int cut_last_screen_byte;
static int cut_first_mark;
static int cut_level_mask;
static int cut_levels_descend;
static int cut_black_seen;
static int cut_mark_after_black;
static int cut_baseline_done;

static void (__interrupt __far *cut_saved_timer)();

/* One tick: advance the counter the frame waits on, then decide whether the
   aperture is holding a single value and keep it if it is.  A reading taken
   while a present blit is half done is not one value and is dropped. */
static void __interrupt __far cut_timer_isr(void)
{
    unsigned char *aperture;
    int probe;
    int first_byte;
    int uniform;

    ++data_fdps_timer_tick_counter;

    if (cut_sample_count < CUT_SAMPLES) {
        aperture = (unsigned char *) CUT_VGA_BASE;
        first_byte = (int) aperture[0];
        uniform = 1;
        if ((int) aperture[CUT_SCREEN_BYTES - 1] != first_byte) {
            uniform = 0;
        }
        for (probe = CUT_PROBE_STRIDE;
             uniform != 0 && probe < CUT_SCREEN_BYTES;
             probe += CUT_PROBE_STRIDE) {
            if ((int) aperture[probe] != first_byte) {
                uniform = 0;
            }
        }
        if (uniform != 0) {
            cut_sample[cut_sample_count] = first_byte;
        } else {
            cut_sample[cut_sample_count] = -1;
        }
        cut_sample_count++;
    }

    _chain_intr(cut_saved_timer);
}

static void cut_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

static void cut_read_dac(int *into)
{
    outp(CUT_DAC_READ_INDEX, 0);
    into[0] = (int) inp(CUT_DAC_DATA);
    into[1] = (int) inp(CUT_DAC_DATA);
    into[2] = (int) inp(CUT_DAC_DATA);
    outp(CUT_DAC_READ_INDEX, CUT_DAC_ENTRIES - 1);
    into[3] = (int) inp(CUT_DAC_DATA);
    into[4] = (int) inp(CUT_DAC_DATA);
    into[5] = (int) inp(CUT_DAC_DATA);
}

/* The two blend tables, the map palette, and everything a rendered frame would
   otherwise paint switched off so the six frames the teardown runs cost a tick
   each and nothing else. */
static void cut_stage(void)
{
    int level;
    int entry;

    memset(data_fdps_palette_shade_ramp_table, 0,
           sizeof(data_fdps_palette_shade_ramp_table));
    memset(data_fdps_inverse_palette_cube, 0,
           sizeof(data_fdps_inverse_palette_cube));

    for (level = CUT_BOTTOM_LEVEL; level <= CUT_TOP_LEVEL; level++) {
        data_fdps_palette_shade_ramp_table[
            CUT_HIGH_LEVEL_DEST_ROW(level) * CUT_RAMP_ROW_ENTRIES
            + CUT_SENTINEL] = (unsigned int) (level << 12);
        data_fdps_inverse_palette_cube[level << 8] =
            (unsigned char) (CUT_MARK_BASE + level);
    }
    data_fdps_inverse_palette_cube[0] = CUT_BLACK_MARK;

    for (entry = 0; entry < CUT_DAC_ENTRIES; entry++) {
        cut_palette[entry].red = (unsigned char) ((entry * 3) % 64);
        cut_palette[entry].green = (unsigned char) ((entry * 5 + 7) % 64);
        cut_palette[entry].blue = (unsigned char) ((entry * 7 + 13) % 64);
    }
    data_fdps_vga_main_palette_ptr = (unsigned char *) cut_palette;

    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_chapter_current_chapter_id = CUT_QUIET_CHAPTER;
}

/* Reduce the per-tick readings to the five things the cases ask about: the
   first frame that was presented at all, which levels named themselves, whether
   they only ever went down, and where the marks stop. */
static void cut_reduce(void)
{
    int index;
    int value;
    int level;
    int previous_level;

    cut_first_mark = -1;
    cut_level_mask = 0;
    cut_levels_descend = 1;
    cut_black_seen = 0;
    cut_mark_after_black = 0;
    previous_level = CUT_TOP_LEVEL + 1;

    for (index = 0; index < cut_sample_count; index++) {
        value = cut_sample[index];
        if (value < 0 || value == CUT_SENTINEL) {
            continue;
        }
        if (cut_first_mark < 0) {
            cut_first_mark = value;
        }
        if (value >= CUT_MARK_BASE + CUT_BOTTOM_LEVEL
            && value <= CUT_MARK_BASE + CUT_TOP_LEVEL) {
            level = value - CUT_MARK_BASE;
            if (level > previous_level) {
                cut_levels_descend = 0;
            }
            previous_level = level;
            cut_level_mask |= 1 << level;
            if (cut_black_seen != 0) {
                cut_mark_after_black = 1;
            }
        } else if (value == CUT_BLACK_MARK) {
            cut_black_seen = 1;
        }
    }
}

/* One whole cutscene, with everything a case can read afterwards copied out
   before the mode change can move it.  It costs some fifty timer ticks and
   loads a 722,133-byte member, so it is run once and shared. */
static void cut_baseline(void)
{
    int index;

    if (cut_baseline_done != 0) {
        return;
    }

    cut_stage();

    cut_sample_count = 0;
    for (index = 0; index < CUT_SAMPLES; index++) {
        cut_sample[index] = -1;
    }
    for (index = 0; index < CUT_READING_SLOTS; index++) {
        cut_return_dac[index] = -1;
    }

    cut_set_mode(CUT_MODE_320X200X256);
    memset((void *) CUT_VGA_BASE, CUT_SENTINEL, (size_t) CUT_SCREEN_BYTES);
    for (index = 0; index < CUT_DAC_ENTRIES; index++) {
        outp(CUT_DAC_WRITE_INDEX, index);
        outp(CUT_DAC_DATA, CUT_DAC_SENTINEL);
        outp(CUT_DAC_DATA, CUT_DAC_SENTINEL);
        outp(CUT_DAC_DATA, CUT_DAC_SENTINEL);
    }

    cut_saved_timer = _dos_getvect(CUT_TIMER_VECTOR);
    _dos_setvect(CUT_TIMER_VECTOR, cut_timer_isr);
    fdps_play_spell_11_cutscene();
    _dos_setvect(CUT_TIMER_VECTOR, cut_saved_timer);

    cut_read_dac(cut_return_dac);
    cut_first_screen_byte = (int) ((unsigned char *) CUT_VGA_BASE)[0];
    cut_last_screen_byte =
        (int) ((unsigned char *) CUT_VGA_BASE)[CUT_SCREEN_BYTES - 1];
    cut_set_mode(CUT_MODE_TEXT);

    cut_reduce();
    cut_baseline_done = 1;
}

/* Something was presented, and the first thing presented was the whole frame
   drawn over the frozen screen at level 15.  Only level 15's destination row is
   ramp row 10, so no other opening level can produce this byte: a counter that
   started anywhere but 0x1f or 0x1e, or a level that was the counter itself
   rather than its half, lands on a row the fixture leaves at zero and the frame
   comes out CUT_BLACK_MARK instead.  It also says the frozen screen reached the
   page -- the memmove of the aperture and the refill blit into the page window
   at 0x2298 -- since the mark is what the SENTINEL folds to, and that the
   request carries blit mode 9 with the descriptor the two staged tables are
   named in. */
static void the_fade_in_opens_over_the_frozen_screen_at_level_fifteen(void)
{
    cut_baseline();

    CHECK_EQ(cut_sample_count > 0, 1);
    CHECK_EQ(cut_first_mark, CUT_MARK_BASE + CUT_TOP_LEVEL);
}

/* Every level from 15 down to 9 names itself once, and none of them ever
   follows a lower one.  The seven together are what say the level is the
   counter's half and steps by one: a divisor of 4 would skip every other level
   and leave four of the seven bits clear, and a level that did not move at all
   would leave six of them clear. */
static void the_blend_level_walks_down_one_step_at_a_time(void)
{
    cut_baseline();

    CHECK_EQ(cut_level_mask, CUT_ALL_LEVELS_MASK);
    CHECK_EQ(cut_levels_descend, 1);
}

/* The fade-in ends and nothing puts the frozen screen back.  Once a frame has
   come out CUT_BLACK_MARK -- which is every frame from level 8 down, the fold
   landing on cube index 0 -- no later frame names a level again.  A body that
   reset the blend level for the main phase, or ran the two phases the other way
   round, would put a level-15 frame after the black ones. */
static void the_frozen_screen_never_returns_once_the_fade_is_done(void)
{
    cut_baseline();

    CHECK_EQ(cut_black_seen, 1);
    CHECK_EQ(cut_mark_after_black, 0);
}

/* The teardown blanks all 64000 bytes of the aperture, and the six frames it
   renders afterwards put back only the 312 x 192 window inset four pixels, so
   the first and last bytes of the frame are the memset's and nothing else's.
   Without that memset both would still hold the last animation frame, which is
   CUT_BLACK_MARK; a memset shorter than 0xfa00 would leave the last byte
   holding it. */
static void the_aperture_is_blanked_before_the_map_comes_back(void)
{
    cut_baseline();

    CHECK_EQ(cut_first_screen_byte, 0);
    CHECK_EQ(cut_last_screen_byte, 0);
}

/* The last thing the routine does to the DAC is upload the whole map palette
   at no bias.  Entry 0 and entry 255 are both read, so a range that stopped
   short of 0xff -- the bound is inclusive -- shows as the second entry still
   holding the pre-call sentinel, and a final step at any bias but 0 shows as
   every component being 10 too high. */
static void the_call_ends_with_the_map_palette_at_no_bias(void)
{
    cut_baseline();

    CHECK_EQ(cut_return_dac[0], (int) cut_palette[0].red);
    CHECK_EQ(cut_return_dac[1], (int) cut_palette[0].green);
    CHECK_EQ(cut_return_dac[2], (int) cut_palette[0].blue);
    CHECK_EQ(cut_return_dac[3],
             (int) cut_palette[CUT_DAC_ENTRIES - 1].red);
    CHECK_EQ(cut_return_dac[4],
             (int) cut_palette[CUT_DAC_ENTRIES - 1].green);
    CHECK_EQ(cut_return_dac[5],
             (int) cut_palette[CUT_DAC_ENTRIES - 1].blue);
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

    RUN_TEST(the_mp_offsets_the_deduction_reaches_are_where_ticket_17_puts_them);
    RUN_TEST(a_cast_takes_the_records_cost_off_the_casters_mp);
    RUN_TEST(a_zero_cost_record_leaves_the_mp_where_it_was);
    RUN_TEST(the_mp_cost_byte_is_read_unsigned);
    RUN_TEST(a_cost_above_the_current_mp_leaves_the_field_negative);
    RUN_TEST(the_difference_is_stored_as_a_word_and_wraps);
    RUN_TEST(the_maximum_mp_is_not_consulted);
    RUN_TEST(nothing_but_the_casters_mp_word_is_written);
    RUN_TEST(each_argument_selects_its_own_record);
    RUN_TEST(the_cost_record_is_the_spell_id_itself_with_no_bias);

    RUN_TEST(the_fade_in_opens_over_the_frozen_screen_at_level_fifteen);
    RUN_TEST(the_blend_level_walks_down_one_step_at_a_time);
    RUN_TEST(the_frozen_screen_never_returns_once_the_fade_is_done);
    RUN_TEST(the_aperture_is_blanked_before_the_map_comes_back);
    RUN_TEST(the_call_ends_with_the_map_palette_at_no_bias);
}
