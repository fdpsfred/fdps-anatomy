/* tests/spell.c -- cover for src/spell.c.
 *
 * The file covers five functions and is in five parts, each starting at its
 * own banner: fdps_spell_damage_unit first, then fdps_spell_heal_unit, then
 * fdps_spell_deduct_mp_cost, then fdps_play_spell_11_cutscene, then
 * fdps_play_spell_palette_flash.  Each banner carries its own account of where
 * that part's figures come from.  The fixture, the three staged tables and the
 * helpers are shared by the first three parts; the cutscene part and the
 * palette-flash part each stage the adapter and a palette of their own and
 * share nothing with them or with each other.
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
#include "indicat.h"
#include "mapdraw.h"
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

/* ---------------------------------------------------------------------------
 * fdps_play_spell_palette_flash
 *
 * WHERE THE EXPECTED VALUES COME FROM.  The three 40-byte planes restated below
 * are the read-only blocks at 00027674, 0002769c and 000276c4 read back byte
 * for byte -- the three REP MOVSD sources at 00029114, 00029123 and 00029132 --
 * and not a copy of the emitted initialisers.  The pass count is
 * MOV dword ptr [EBP-0x4],0x0 at 00029139 with CMP against 0x4 / JL, the two
 * frames a pass presents are the CALLs to fdps_render_view_frame at 000291b0
 * and 000291f1, the single DAC entry is PUSH 0x0 / PUSH 0x3c8 at 00029153 and
 * 000291b5, and the three components per burst are the three PUSH 0x3c9 pairs
 * that follow each of them.
 *
 * HOW THE COLOUR IS OBSERVED, AND WHY IT IS SAFE TO READ THE DAC HERE.  The
 * routine leaves entry 0 black, so the colour it flashed is gone by the time
 * the call returns and the only way to see it is from inside the call.  A
 * handler that simply read the DAC every tick would sooner or later cut into
 * one of the routine's own write bursts, because 0x3c7 and 0x3c8 load the same
 * internal address register -- which is why the cutscene part above does not
 * read the DAC from its handler at all.
 *
 * What makes it safe here is that the handler owns the frame clock.  Every one
 * of the eight frames ends in fdps_render_view_frame's spin on
 * data_fdps_timer_tick_counter (mapdraw.h), which only this handler advances,
 * so the routine cannot leave a frame until the handler lets it.  The handler
 * therefore advances the counter on every SECOND tick and reads the DAC
 * immediately before it does: at that instant the routine has been parked in
 * the spin for a whole tick period, its write bursts are long finished, and no
 * further one can start until the counter moves.  data_fdps_view_frame_last_tick
 * is seeded to the counter before the call so that the first frame waits too --
 * without that the first reading would be taken during the black half.
 *
 * The eight readings of the sampled run are therefore the eight frames in
 * order.  Every other run is stopped after one reading, which is the first
 * frame's colour, and then paced freely, so a whole-table sweep costs nine
 * ticks a spell rather than sixteen.
 *
 * WHAT IS NOT ASSERTED.  How long a flash lasts in wall time is
 * fdps_render_view_frame's contract and not this routine's, and the picture the
 * strobe is seen against is whatever the scene drew in palette index 0 -- both
 * are playtest contracts (rebuild_info/pitfalls.md).
 */

#define FLASH_SPELL_COUNT 40
#define FLASH_PASSES 4
#define FLASH_FRAMES (FLASH_PASSES * 2)
/* Two more readings than the routine can produce, so a body that presented more
   frames than it should would show up as a ninth reading. */
#define FLASH_SAMPLE_SLOTS (FLASH_FRAMES + 2)

#define FLASH_TIMER_VECTOR 8
#define FLASH_DAC_READ_INDEX 0x3c7
#define FLASH_DAC_WRITE_INDEX 0x3c8
#define FLASH_DAC_DATA 0x3c9
#define FLASH_DAC_ENTRIES 256
/* The entry the routine is supposed to move, and the one after it, which it is
   supposed to leave alone.  A burst of four components instead of three would
   spill into the guard entry's red. */
#define FLASH_DAC_ENTRY 0
#define FLASH_GUARD_ENTRY 1
/* What both entries hold before a run, so that a component still holding it
   afterwards says nothing was written there. */
#define FLASH_DAC_SENTINEL 0x2a

#define FLASH_VGA_BASE 0x000a0000
#define FLASH_SCREEN_BYTES 64000
#define FLASH_MODE_320X200X256 0x13
#define FLASH_MODE_TEXT 0x03

/* A chapter with no arm in fdps_cycle_scene_palette's dispatch, so the frames
   the routine presents cannot write the DAC behind the readings. */
#define FLASH_QUIET_CHAPTER 1

/* The spell whose run is sampled frame by frame.  裂地術's colour has three
   different components, so a run that swapped two planes could not produce it. */
#define FLASH_SAMPLED_SPELL 0x0a

/* Ids whose colour the cases name outright: the first fire spell, the two
   ground shocks' brown, 甦癒術's green, 審判之雷's red-orange, and the two ends
   of the white run that surrounds them. */
#define FLASH_SPELL_FIRE 0x00
#define FLASH_SPELL_QUAKE 0x0a
#define FLASH_SPELL_REVIVE 0x18
#define FLASH_SPELL_JUDGEMENT 0x20
#define FLASH_SPELL_FIRST_WHITE 0x03
#define FLASH_SPELL_LAST 0x27

static const unsigned char flash_expect_red[FLASH_SPELL_COUNT] = {
    63, 63, 63, 63, 63, 63, 63, 63,
    63, 63, 57, 57, 63, 63, 63, 63,
    63, 63, 63, 63, 63, 63, 63, 63,
     0, 63, 63, 63, 63, 63, 63, 63,
    63, 63, 63, 63, 63, 63, 63, 63
};
static const unsigned char flash_expect_green[FLASH_SPELL_COUNT] = {
     0,  0,  0, 63, 63, 63, 63, 63,
    63, 63, 42, 42,  0, 63, 63, 63,
    63, 63, 63, 63,  0, 63, 63, 63,
    63, 63, 63, 63, 63, 63,  0,  0,
     9, 63, 63, 63, 63, 63, 63, 63
};
static const unsigned char flash_expect_blue[FLASH_SPELL_COUNT] = {
     0,  0,  0, 63, 63, 63, 63, 63,
    63, 63, 25, 25,  0, 63, 63, 63,
    63, 63, 63, 63,  0, 63, 63, 63,
     0, 63, 63, 63, 63, 63,  0,  0,
     0, 63, 63, 63, 63, 63, 63, 63
};

static struct fdps_palette_entry flash_palette[FLASH_DAC_ENTRIES];

static volatile int flash_gate;
static volatile int flash_want;
static volatile int flash_taken;
static volatile int flash_sample_red[FLASH_SAMPLE_SLOTS];
static volatile int flash_sample_green[FLASH_SAMPLE_SLOTS];
static volatile int flash_sample_blue[FLASH_SAMPLE_SLOTS];

static void (__interrupt __far *flash_saved_timer)();

/* What one whole sweep left behind. */
static int flash_seen_red[FLASH_SPELL_COUNT];
static int flash_seen_green[FLASH_SPELL_COUNT];
static int flash_seen_blue[FLASH_SPELL_COUNT];
static int flash_red_wrong;
static int flash_green_wrong;
static int flash_blue_wrong;
static int flash_frames_presented;
static int flash_colour_frames;
static int flash_black_frames;
static int flash_alternates;
static int flash_end_entry[3];
static int flash_guard_entry[3];
static int flash_baseline_done;

/* One tick.  On every second one the routine is parked in the frame spin, which
   is when the DAC may be read and when the counter is advanced to release it. */
static void __interrupt __far flash_timer_isr(void)
{
    if (flash_want > 0) {
        flash_gate = flash_gate ^ 1;
        if (flash_gate == 0) {
            outp(FLASH_DAC_READ_INDEX, FLASH_DAC_ENTRY);
            flash_sample_red[flash_taken] = (int) inp(FLASH_DAC_DATA);
            flash_sample_green[flash_taken] = (int) inp(FLASH_DAC_DATA);
            flash_sample_blue[flash_taken] = (int) inp(FLASH_DAC_DATA);
            flash_taken++;
            flash_want--;
            ++data_fdps_timer_tick_counter;
        }
    } else {
        ++data_fdps_timer_tick_counter;
    }

    _chain_intr(flash_saved_timer);
}

/* Everything a presented frame would otherwise paint switched off, so the eight
   frames a run costs are eight ticks and nothing else. */
static void flash_stage(void)
{
    int entry;

    for (entry = 0; entry < FLASH_DAC_ENTRIES; entry++) {
        flash_palette[entry].red = (unsigned char) (entry % 64);
        flash_palette[entry].green = (unsigned char) ((entry + 21) % 64);
        flash_palette[entry].blue = (unsigned char) ((entry + 42) % 64);
    }
    data_fdps_vga_main_palette_ptr = (unsigned char *) flash_palette;

    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_chapter_current_chapter_id = FLASH_QUIET_CHAPTER;
}

static void flash_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

static void flash_write_entry(int entry, int level)
{
    outp(FLASH_DAC_WRITE_INDEX, entry);
    outp(FLASH_DAC_DATA, level);
    outp(FLASH_DAC_DATA, level);
    outp(FLASH_DAC_DATA, level);
}

static void flash_read_entry(int entry, int *into)
{
    outp(FLASH_DAC_READ_INDEX, entry);
    into[0] = (int) inp(FLASH_DAC_DATA);
    into[1] = (int) inp(FLASH_DAC_DATA);
    into[2] = (int) inp(FLASH_DAC_DATA);
}

/* One whole flash, with the first `samples` frames read back out of the DAC. */
static void flash_run(int spell_id, int samples)
{
    int index;

    for (index = 0; index < FLASH_SAMPLE_SLOTS; index++) {
        flash_sample_red[index] = -1;
        flash_sample_green[index] = -1;
        flash_sample_blue[index] = -1;
    }
    flash_taken = 0;
    flash_gate = 0;
    flash_want = samples;

    flash_write_entry(FLASH_DAC_ENTRY, FLASH_DAC_SENTINEL);
    flash_write_entry(FLASH_GUARD_ENTRY, FLASH_DAC_SENTINEL);

    flash_saved_timer = _dos_getvect(FLASH_TIMER_VECTOR);
    _dos_setvect(FLASH_TIMER_VECTOR, flash_timer_isr);
    data_fdps_view_frame_last_tick = data_fdps_timer_tick_counter;
    fdps_play_spell_palette_flash(spell_id);
    _dos_setvect(FLASH_TIMER_VECTOR, flash_saved_timer);
}

/* The sampled run and then the whole table, once.  It costs some four hundred
   timer ticks, so it is run once and shared. */
static void flash_baseline(void)
{
    int index;
    int is_colour;
    int is_black;

    if (flash_baseline_done != 0) {
        return;
    }

    flash_stage();
    flash_set_mode(FLASH_MODE_320X200X256);
    memset((void *) FLASH_VGA_BASE, 0, (size_t) FLASH_SCREEN_BYTES);

    flash_run(FLASH_SAMPLED_SPELL, FLASH_SAMPLE_SLOTS);
    flash_frames_presented = flash_taken;
    flash_read_entry(FLASH_DAC_ENTRY, flash_end_entry);
    flash_read_entry(FLASH_GUARD_ENTRY, flash_guard_entry);

    flash_colour_frames = 0;
    flash_black_frames = 0;
    flash_alternates = 1;
    for (index = 0; index < flash_frames_presented; index++) {
        is_colour = 0;
        if (flash_sample_red[index]
                == (int) flash_expect_red[FLASH_SAMPLED_SPELL]
            && flash_sample_green[index]
                == (int) flash_expect_green[FLASH_SAMPLED_SPELL]
            && flash_sample_blue[index]
                == (int) flash_expect_blue[FLASH_SAMPLED_SPELL]) {
            is_colour = 1;
        }
        is_black = 0;
        if (flash_sample_red[index] == 0 && flash_sample_green[index] == 0
            && flash_sample_blue[index] == 0) {
            is_black = 1;
        }
        if (is_colour != 0) {
            flash_colour_frames++;
        }
        if (is_black != 0) {
            flash_black_frames++;
        }
        if ((index % 2) == 0) {
            if (is_colour == 0) {
                flash_alternates = 0;
            }
        } else {
            if (is_black == 0) {
                flash_alternates = 0;
            }
        }
    }

    flash_red_wrong = 0;
    flash_green_wrong = 0;
    flash_blue_wrong = 0;
    for (index = 0; index < FLASH_SPELL_COUNT; index++) {
        flash_run(index, 1);
        flash_seen_red[index] = flash_sample_red[0];
        flash_seen_green[index] = flash_sample_green[0];
        flash_seen_blue[index] = flash_sample_blue[0];
        if (flash_seen_red[index] != (int) flash_expect_red[index]) {
            flash_red_wrong++;
        }
        if (flash_seen_green[index] != (int) flash_expect_green[index]) {
            flash_green_wrong++;
        }
        if (flash_seen_blue[index] != (int) flash_expect_blue[index]) {
            flash_blue_wrong++;
        }
    }

    flash_set_mode(FLASH_MODE_TEXT);
    flash_baseline_done = 1;
}

/* Every one of the forty spell ids flashes the colour its own three planes
   hold.  This is what says the id indexes the planes directly -- a bias of one
   either way moves the two brown entries and the green one off the ids that own
   them, and every id in a white run keeps reading 63 -- and that the three
   planes are 0x28 bytes apart rather than interleaved. */
static void every_spell_id_flashes_the_colour_its_planes_hold(void)
{
    flash_baseline();

    CHECK_EQ(flash_red_wrong, 0);
    CHECK_EQ(flash_green_wrong, 0);
    CHECK_EQ(flash_blue_wrong, 0);
}

/* The four colours that are not white, named outright and in channel order.
   業火 is pure red, 裂地術 the earth brown whose three components all differ,
   甦癒術 pure green and 審判之雷 the red-orange with the 9 in the green plane.
   Two planes handed to outp in the wrong order would keep the white ids white
   and show up only here. */
static void the_element_colours_reach_the_dac_in_channel_order(void)
{
    flash_baseline();

    CHECK_EQ(flash_seen_red[FLASH_SPELL_FIRE], 63);
    CHECK_EQ(flash_seen_green[FLASH_SPELL_FIRE], 0);
    CHECK_EQ(flash_seen_blue[FLASH_SPELL_FIRE], 0);

    CHECK_EQ(flash_seen_red[FLASH_SPELL_QUAKE], 57);
    CHECK_EQ(flash_seen_green[FLASH_SPELL_QUAKE], 42);
    CHECK_EQ(flash_seen_blue[FLASH_SPELL_QUAKE], 25);

    CHECK_EQ(flash_seen_red[FLASH_SPELL_REVIVE], 0);
    CHECK_EQ(flash_seen_green[FLASH_SPELL_REVIVE], 63);
    CHECK_EQ(flash_seen_blue[FLASH_SPELL_REVIVE], 0);

    CHECK_EQ(flash_seen_red[FLASH_SPELL_JUDGEMENT], 63);
    CHECK_EQ(flash_seen_green[FLASH_SPELL_JUDGEMENT], 9);
    CHECK_EQ(flash_seen_blue[FLASH_SPELL_JUDGEMENT], 0);
}

/* The two ends of the white majority: the first id past the fire run and the
   last record MAGICDAT.DAT holds.  The last one is where an index that ran off
   the top of a table would show, since 0x27 is the final entry of all three. */
static void the_spells_with_no_element_colour_flash_white(void)
{
    flash_baseline();

    CHECK_EQ(flash_seen_red[FLASH_SPELL_FIRST_WHITE], 63);
    CHECK_EQ(flash_seen_green[FLASH_SPELL_FIRST_WHITE], 63);
    CHECK_EQ(flash_seen_blue[FLASH_SPELL_FIRST_WHITE], 63);

    CHECK_EQ(flash_seen_red[FLASH_SPELL_LAST], 63);
    CHECK_EQ(flash_seen_green[FLASH_SPELL_LAST], 63);
    CHECK_EQ(flash_seen_blue[FLASH_SPELL_LAST], 63);
}

/* Eight frames, no more and no fewer, and they run colour, black, colour, black
   from the first one.  The handler was willing to read ten, so a fifth pass or
   a third frame in a pass would have been read; a loop that ran three times
   would have stopped at six.  The alternation is what says the black burst
   comes after the colour's frame and not before it. */
static void the_flash_presents_four_passes_alternating_with_black(void)
{
    flash_baseline();

    CHECK_EQ(flash_frames_presented, FLASH_FRAMES);
    CHECK_EQ(flash_colour_frames, FLASH_PASSES);
    CHECK_EQ(flash_black_frames, FLASH_PASSES);
    CHECK_EQ(flash_alternates, 1);
}

/* The entry is black when the call returns, and it is the routine that made it
   so: both entries went in holding the sentinel.  A body that put back what
   entry 0 held before the flash would leave the sentinel here. */
static void the_flash_leaves_dac_entry_zero_black(void)
{
    flash_baseline();

    CHECK_EQ(flash_end_entry[0], 0);
    CHECK_EQ(flash_end_entry[1], 0);
    CHECK_EQ(flash_end_entry[2], 0);
}

/* And it moves nothing else.  Entry 1 still holds the sentinel it was given, so
   the write index was 0 on every burst and each burst was three components
   long: a fourth write would have run on into this entry's red. */
static void nothing_but_dac_entry_zero_is_written(void)
{
    flash_baseline();

    CHECK_EQ(flash_guard_entry[0], FLASH_DAC_SENTINEL);
    CHECK_EQ(flash_guard_entry[1], FLASH_DAC_SENTINEL);
    CHECK_EQ(flash_guard_entry[2], FLASH_DAC_SENTINEL);
}

/* ------------------------------------------------------------------ *
 * 000288f0  fdps_cast_spell_on_targets                               *
 * ------------------------------------------------------------------ *
 *
 * WHAT THE RUN IS OBSERVED THROUGH.  The routine takes four arguments, returns
 * nothing and spends most of its time presenting, so what it can be held to is
 * the state it leaves behind: the unit records the target indices name, the
 * floating-popup queue, the two view origins and the map cursor mode.  Every
 * case below stages that state, runs one whole cast, copies the four unit
 * records and the head of the queue into a capture, and asserts against the
 * copy.  Each cast is run once and shared by the cases that read it.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED, and why the scene is emptied first: for
 * the reasons the palette-flash part above gives.  Every frame the flash, the
 * ground shock, the map animation and the popup playback present ends waiting
 * for data_fdps_timer_tick_counter to change, and nothing advances it in a test
 * image, so the run hooks IRQ0 with a handler that advances it and chains on.
 * flash_stage's empty scene is reused unchanged, so a presented frame paints
 * nothing and costs one tick.
 *
 * THE QUEUE SURVIVES ITS OWN PLAYBACK.  fdps_play_indicator_queue resets the
 * cell count on the way out but leaves the cells themselves where they are
 * (indicat.c), so the glyph ids and the unit each cell belongs to can be read
 * back after the call.  That is how the popups are pinned here without going
 * to the adapter for them.  Cells past the count are never drawn, so the whole
 * array is filled with 0xee before a run and a cell still holding that value is
 * one nothing queued.
 *
 * THE GLYPH SHEET IS STAGED, not borrowed from the game files: the playback
 * blits every non-blank cell through data_fdps_number_glyph_sheet_ptr, and the
 * ids reached here run up to 0x40, so a synthetic .CEL of 0x41 uniform entries
 * is published for the duration.  Nothing is asserted about the pixels it
 * paints; it is there so the playback has a sheet to read.
 *
 * HOW THE SHAKE IS WATCHED.  The 25 shake frames each write both view origins
 * and then park in fdps_render_view_frame's tick wait, so the handler sees one
 * sample per shake frame with that frame's offsets still in place.  It records
 * how many samples were off centre and the extreme offset on each axis.  The
 * bound that matters is the upper one: rand() % 4 - 2 gives -2..+1, so an
 * offset of +2 can never be sampled, while the symmetric jitter the code
 * invites -- rand() % 5 - 2 -- would show one within a few frames.
 *
 * HOW THE THREE 神之祝福 GROUPS ARE SEPARATED.  The blessing plays its queue out
 * once per slot, so only the last slot's cells are still in the queue when the
 * call returns.  The handler therefore also samples the head of the queue while
 * it is being played and records each distinct triple, which recovers all three
 * groups from one run and pins the order they are played in.
 *
 * WHERE THE EXPECTED VALUES COME FROM.  The 12 bytes at 00027668 for the three
 * buff groups; MOVSX word ptr [EAX+0x44] at 0002895b with XOR EDX,EDX /
 * MOV DL,byte ptr [EAX+0x5] at 00028967 and MOV word ptr [EAX+0x44],BX at
 * 00028972 for the MP charge; AND byte ptr [EAX+0x5],0x7f at 00028bfe for the
 * acted bit; the two MOV byte stores at 00028b9c and 00028ba7 for the teleport
 * writing the FIRST entry of the array only; PUSH 0x3 / PUSH 0x0 with
 * ADD EAX,0x25 at 00028c7b for the three ailment slots the cure clears;
 * PUSH 0x27 at 00028b4a and PUSH 0x0 at 00028e6e for the two glyph bases;
 * MOV EBX,0x4 / IDIV EBX / SUB EDX,0x2 at 00028a54 and 00028a6d with
 * CMP dword ptr [EBP-0x20],0x19 at 00028a3d for the shake; and
 * CMP dword ptr [EBP-0x18],0x3 at 00028d86 for the three blessing passes.  The
 * cure, MISS and digit glyph ids are indicat.c's, the ailment slot the
 * paralysis id selects is unitstat.c's, and none of them is read off the
 * emitted C.
 *
 * WHAT IS NOT ASSERTED.  鬼動死靈陣's extra roll.  That arm calls
 * fdps_unit_inflict_random_ailments on a target it damaged, and that routine
 * lands each of the three ailments on a flat one draw in five, so a single cast
 * leaves the record untouched about half the time and no number of casts a test
 * image can afford makes it certain.  Nothing here can pin it; what would is a
 * count of the draws the cast takes out of the shared rand() stream, which the
 * image has no way to observe.
 *
 * Nor is the presentation itself: which clip is played, that the map cursor is
 * off for the whole of it rather than only at the ends, that 封神裂震 shares
 * 裂地術's Emg10.saf and that 鎮魂之歌 plays its extra clip.
 * The first two are playtest contracts.  The Emg10.saf sharing is settled the
 * only way a test could settle it -- the container has EMG10.SAF and no
 * EMG11.SAF, so the id that formats itself minus one is the only id that can
 * load at all -- but running spell 0x0b here would drag its whole full-screen
 * cutscene and that cutscene's own fixture in behind it, and the cutscene part
 * above already covers that.
 */

/* The four staged records.  The caster is a fifth party to every cast and is
   the witness that a per-target write did not stray. */
#define CAST_CASTER 1
#define CAST_A 0
#define CAST_B 2
#define CAST_C 3

/* Tiles well inside the popup cull window, which with both origins at 0 is
   columns 0 to 12 and rows 0 to 8 (indicat.c). */
#define CAST_A_TILE_X 2
#define CAST_A_TILE_Y 2
#define CAST_B_TILE_X 4
#define CAST_B_TILE_Y 3
#define CAST_C_TILE_X 6
#define CAST_C_TILE_Y 4
#define CAST_CASTER_TILE_X 8
#define CAST_CASTER_TILE_Y 5

/* The spell ids the cases cast, each with its own arm in the dispatch. */
#define CAST_HASTE 0x16     /* 神行術 */
#define CAST_TELEPORT 0x15  /* 傳送術 */
#define CAST_CURE 0x18      /* 甦癒術 */
#define CAST_HEAL 0x0e      /* 恢復之光 */
#define CAST_QUAKE 0x0a     /* 裂地術 */
#define CAST_PARALYSIS 0x13 /* 麻痺術 */
#define CAST_BLESSING 0x14  /* 神之祝福 */

/* What every cast is charged, and what the caster starts with.  One case
   raises the cost to 0xff to prove the byte widens unsigned. */
#define CAST_MP_START 50
#define CAST_MP_COST 4
#define CAST_HASTE_COST 7

/* A heal of 10 is inside the band where fdps_unit_apply_heal's second term
   truncates to nothing whatever rand() returned -- (0..99) * 10 / 1000 is 0 --
   so the roll is exactly 10 * 9 / 10, and the same band makes a damage of 10
   land as exactly 9 (unitstat.c, and the damage part above). */
#define CAST_POWER 10
#define CAST_ROLLED 9
#define CAST_HP_START 50
#define CAST_HP_MAX 100
#define CAST_FULL_HP 100

/* rand() % 100 is 0..99, so these are the deterministic ends of a hit rate. */
#define CAST_ALWAYS 100

/* The glyph bases and the fixed words, from indicat.c. */
#define CAST_HEAL_GLYPH_BASE 0x27
#define CAST_DAMAGE_GLYPH_BASE 0
#define CAST_BLANK_GLYPH 0xff
#define CAST_MISS_M 0x34
#define CAST_MISS_I 0x35
#define CAST_MISS_S 0x36
#define CAST_CURE_C 0x37
#define CAST_CURE_U 0x38
#define CAST_CURE_R 0x39
#define CAST_CURE_E 0x3a

/* The three four-byte groups at 00027668, which the function copies onto its
   own frame before anything else. */
#define CAST_BUFF0_A 0x3b
#define CAST_BUFF0_B 0x3c
#define CAST_BUFF0_C 0x3c
#define CAST_BUFF1_A 0x3d
#define CAST_BUFF1_B 0x3e
#define CAST_BUFF1_C 0x3f
#define CAST_BUFF2_A 0x3d
#define CAST_BUFF2_B 0x3e
#define CAST_BUFF2_C 0x40

/* status_timers slots: 0 to 2 are 神之祝福's three buffs and 3 to 5 are the
   ailments, of which 麻痺術 is slot 4 (unitstat.c). */
#define CAST_BUFF_SLOTS 3
#define CAST_AILMENT_FIRST 3
#define CAST_PARALYSIS_SLOT 4

/* A timer value no roll can produce -- fdps_unit_apply_status_effect writes
   rand() % 2 + 2, which is 2 or 3 -- so a slot still holding it was refused and
   one holding 2 or 3 was written by the roll. */
#define CAST_TIMER_OCCUPIED 5
#define CAST_TIMER_MIN 2
#define CAST_TIMER_MAX 3

/* The value every derived combat word is pre-loaded with, so that a word still
   holding it says fdps_unit_recompute_combat_stats was not called on that
   record.  The base stats are all left at 0, which is what makes the recomputed
   attack and defence 0 whatever the 1.15 buff multiplier does with them; the
   evasion bonus is the integer 15 that unit.c adds for a live slot 2. */
#define CAST_STAT_SENTINEL 77
#define CAST_DEX_BONUS 15

/* The mode the cursor global is loaded with before a run: neither the 0 the
   routine clears it to nor the 1 it leaves, so the value found afterwards can
   only have been written by the routine. */
#define CAST_CURSOR_SENTINEL 2
#define CAST_CURSOR_PLAIN 1

/* What an unqueued cell holds.  It is not a glyph the playback can reach,
   because only cells below the queue count are ever drawn. */
#define CAST_QUEUE_SENTINEL 0xee
/* Cells kept in the capture: two targets' worth of a four-cell popup with room
   to see that a third was not appended. */
#define CAST_QUEUE_WATCH 12

/* 25 shake frames -- CMP dword ptr [EBP-0x20],0x19 at 00028a3d -- of which the
   one in sixteen that draws (0, 0) leaves the view centred, so the count of
   off-centre samples runs around 23 and cannot exceed 25.  The floor is set
   where a correct run cannot fall through it: 14 or fewer needs eleven of the
   25 frames to draw dead centre, which is one run in four million.

   The upper offset is asserted as a bound rather than as a value reached,
   because a correct run is not obliged to draw +1 at all -- it is one draw in
   four per axis, so demanding it would fail about one run in thirteen hundred.
   As a bound it still separates the two spellings: the symmetric jitter the
   shape invites, rand() % 5 - 2, draws +2 one frame in five and would be
   caught in 25 frames all but four times in a thousand.  The lower offset is
   the other way round -- half of all draws are below zero, so a run that never
   reached -1 is one in thirty million. */
#define CAST_SHAKE_FRAMES 0x19
#define CAST_SHAKE_MIN_SAMPLES 15
#define CAST_SHAKE_MAX_OFFSET 1
#define CAST_SHAKE_MIN_OFFSET (-1)

/* One distinct queue head per blessing pass, with room for a fourth so a run
   that played its queue out more often than it should would be visible. */
#define CAST_TRIPLES_MAX 6

/* The synthetic glyph sheet: 0x41 uniform 6x8 entries in the .CEL shape
   resource_info/cel.md describes, which is one more than the highest id any
   popup here reaches. */
#define CG_GLYPH_W 6
#define CG_GLYPH_H 8
#define CG_SHEET_ENTRIES 0x41
#define CG_VERSION_FIELD_AT 0x03
#define CG_TABLE_FIELD_AT 0x05
#define CG_WIDTH_FIELD_AT 0x07
#define CG_HEIGHT_FIELD_AT 0x09
#define CG_COUNT_FIELD_AT 0x0b
#define CG_ENCODING_FIELD_AT 0x0d
#define CG_TABLE_AT 0x0f
#define CG_FILL_RUN 0x05
#define CG_STREAM_BYTES (CG_GLYPH_H * 2)
#define CG_STREAM0_AT (CG_TABLE_AT + CG_SHEET_ENTRIES * 4)
#define CG_SHEET_BYTES (CG_STREAM0_AT + CG_SHEET_ENTRIES * CG_STREAM_BYTES)
#define CG_ART_PIXEL 0x30

/* The movement grid fdps_map_cursor_move_to reads the map's extent out of: a
   four-byte header of two signed 16-bit tile dimensions (movegrid.h).  Only
   the header is read on the way through, so the cells are not staged.  A map
   this size leaves the destination tile below well inside the view, so the
   scroll clamps have nothing to do and the cursor walk is one step long. */
#define CAST_MAP_TILES_W 20
#define CAST_MAP_TILES_H 15
#define CAST_GRID_HEADER_BYTES 4

/* Where the teleport is aimed, and where the cursor is standing when the cast
   begins: one tile to its left, so the walk is a single step whose end
   position says which pixel coordinates the scroll was asked for. */
#define CAST_DEST_TILE_X 5
#define CAST_DEST_TILE_Y 5
#define CAST_TILE_PIXELS 0x18

/* Everything one whole cast left behind. */
struct cast_capture {
    unsigned char units[STAGE_UNITS * UNIT_RECORD_STRIDE];
    unsigned char glyphs[CAST_QUEUE_WATCH];
    unsigned char owners[CAST_QUEUE_WATCH];
    int cursor_mode;
    int cursor_world_x;
    int cursor_world_y;
    int origin_x;
    int origin_y;
    int min_dx;
    int max_dx;
    int min_dy;
    int max_dy;
    int offset_samples;
    int triples;
    unsigned char triple_a[CAST_TRIPLES_MAX];
    unsigned char triple_b[CAST_TRIPLES_MAX];
    unsigned char triple_c[CAST_TRIPLES_MAX];
};

static unsigned char cast_sheet[CG_SHEET_BYTES];
static unsigned char cast_targets[CAST_BUFF_SLOTS];
static unsigned char cast_grid[CAST_GRID_HEADER_BYTES];

static void (__interrupt __far *cast_saved_timer)();

static volatile int cast_base_origin_x;
static volatile int cast_base_origin_y;
static volatile int cast_min_dx;
static volatile int cast_max_dx;
static volatile int cast_min_dy;
static volatile int cast_max_dy;
static volatile int cast_offset_samples;
static volatile int cast_triples;
static volatile unsigned char cast_last_a;
static volatile unsigned char cast_last_b;
static volatile unsigned char cast_last_c;
static volatile unsigned char cast_triple_a[CAST_TRIPLES_MAX];
static volatile unsigned char cast_triple_b[CAST_TRIPLES_MAX];
static volatile unsigned char cast_triple_c[CAST_TRIPLES_MAX];

/* One tick: advance the counter every frame is waiting on, and while it is
   here, sample the two view origins and the head of the popup queue. */
static void __interrupt __far cast_timer_isr(void)
{
    int offset_x;
    int offset_y;
    unsigned char head_a;
    unsigned char head_b;
    unsigned char head_c;

    offset_x = data_fdps_battle_view_window_origin_x - cast_base_origin_x;
    offset_y = data_fdps_battle_view_window_origin_y - cast_base_origin_y;
    if (offset_x != 0 || offset_y != 0) {
        cast_offset_samples++;
    }
    if (offset_x < cast_min_dx) {
        cast_min_dx = offset_x;
    }
    if (offset_x > cast_max_dx) {
        cast_max_dx = offset_x;
    }
    if (offset_y < cast_min_dy) {
        cast_min_dy = offset_y;
    }
    if (offset_y > cast_max_dy) {
        cast_max_dy = offset_y;
    }

    if (data_fdps_indicator_queue_count != 0) {
        head_a = data_fdps_indicator_queue_glyph_ids[0];
        head_b = data_fdps_indicator_queue_glyph_ids[1];
        head_c = data_fdps_indicator_queue_glyph_ids[2];
        if (cast_triples == 0 || head_a != cast_last_a
            || head_b != cast_last_b || head_c != cast_last_c) {
            if (cast_triples < CAST_TRIPLES_MAX) {
                cast_triple_a[cast_triples] = head_a;
                cast_triple_b[cast_triples] = head_b;
                cast_triple_c[cast_triples] = head_c;
            }
            cast_triples++;
            cast_last_a = head_a;
            cast_last_b = head_b;
            cast_last_c = head_c;
        }
    }

    ++data_fdps_timer_tick_counter;

    _chain_intr(cast_saved_timer);
}

static void cast_sheet_u16(int at, int value)
{
    cast_sheet[at] = (unsigned char) (value & 0xff);
    cast_sheet[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void cast_sheet_u32(int at, unsigned long value)
{
    cast_sheet[at] = (unsigned char) (value & 0xff);
    cast_sheet[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    cast_sheet[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    cast_sheet[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* 0x41 entries of one flat colour each, so the playback has a sheet to read. */
static void cast_stage_sheet(void)
{
    int entry;
    int stream_at;
    int row;

    memset(cast_sheet, 0, sizeof(cast_sheet));
    cast_sheet[0] = 'C';
    cast_sheet[1] = 'E';
    cast_sheet[2] = 'L';
    cast_sheet_u16(CG_VERSION_FIELD_AT, 1);
    cast_sheet_u16(CG_TABLE_FIELD_AT, 0);
    cast_sheet_u16(CG_WIDTH_FIELD_AT, CG_GLYPH_W);
    cast_sheet_u16(CG_HEIGHT_FIELD_AT, CG_GLYPH_H);
    cast_sheet_u16(CG_COUNT_FIELD_AT, CG_SHEET_ENTRIES);
    cast_sheet_u16(CG_ENCODING_FIELD_AT, 2);

    for (entry = 0; entry < CG_SHEET_ENTRIES; entry++) {
        stream_at = CG_STREAM0_AT + entry * CG_STREAM_BYTES;
        cast_sheet_u32(CG_TABLE_AT + entry * 4, (unsigned long) stream_at);
        for (row = 0; row < CG_GLYPH_H; row++) {
            cast_sheet[stream_at + row * 2] = CG_FILL_RUN;
            cast_sheet[stream_at + row * 2 + 1] = (unsigned char) CG_ART_PIXEL;
        }
    }
}

/* The three staged tables and the empty scene, plus the four records placed on
   the map and the target list every cast is handed. */
static void cast_stage(void)
{
    int index;

    stage();
    flash_stage();
    cast_stage_sheet();
    data_fdps_number_glyph_sheet_ptr = cast_sheet;

    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_battle_teleport_dest_tile_x = 0;
    data_fdps_teleport_destination_tile_y = 0;

    /* The map extent the cursor scroll clamps against.  Null here would be a
       read through address zero rather than a caught error: the reader does
       not test the pointer (gamedata.h). */
    cast_grid[0] = (unsigned char) CAST_MAP_TILES_W;
    cast_grid[1] = 0;
    cast_grid[2] = (unsigned char) CAST_MAP_TILES_H;
    cast_grid[3] = 0;
    data_fdps_battle_move_grid_ptr = cast_grid;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;

    unit(CAST_A)->pos_x = CAST_A_TILE_X;
    unit(CAST_A)->pos_y = CAST_A_TILE_Y;
    unit(CAST_B)->pos_x = CAST_B_TILE_X;
    unit(CAST_B)->pos_y = CAST_B_TILE_Y;
    unit(CAST_C)->pos_x = CAST_C_TILE_X;
    unit(CAST_C)->pos_y = CAST_C_TILE_Y;
    unit(CAST_CASTER)->pos_x = CAST_CASTER_TILE_X;
    unit(CAST_CASTER)->pos_y = CAST_CASTER_TILE_Y;
    unit(CAST_CASTER)->mp_current = CAST_MP_START;

    /* Side 2 is the player's.  A record left on side 0 would send the damage
       path into an ENEMYDAT.DAT lookup through a table pointer nothing here
       stages, for the reason the damage part above gives, and the maximum HP
       is what the heal's experience credit divides by. */
    for (index = 0; index < STAGE_UNITS; index++) {
        unit(index)->side = PLAYER_SIDE;
        unit(index)->hp_max = CAST_HP_MAX;
    }

    cast_targets[0] = CAST_A;
    cast_targets[1] = CAST_B;
    cast_targets[2] = CAST_C;
}

static void cast_capture_into(struct cast_capture *into)
{
    int cell;

    memcpy(into->units, unit_block, sizeof(into->units));
    for (cell = 0; cell < CAST_QUEUE_WATCH; cell++) {
        into->glyphs[cell] = data_fdps_indicator_queue_glyph_ids[cell];
        into->owners[cell] = data_fdps_battle_indicator_queue_unit_idx[cell];
    }
    into->cursor_mode = data_fdps_map_cursor_draw_mode;
    into->cursor_world_x = data_fdps_map_cursor_world_x;
    into->cursor_world_y = data_fdps_map_cursor_world_y;
    into->origin_x = data_fdps_battle_view_window_origin_x;
    into->origin_y = data_fdps_battle_view_window_origin_y;
    into->min_dx = cast_min_dx;
    into->max_dx = cast_max_dx;
    into->min_dy = cast_min_dy;
    into->max_dy = cast_max_dy;
    into->offset_samples = cast_offset_samples;
    into->triples = cast_triples;
    for (cell = 0; cell < CAST_TRIPLES_MAX; cell++) {
        into->triple_a[cell] = cast_triple_a[cell];
        into->triple_b[cell] = cast_triple_b[cell];
        into->triple_c[cell] = cast_triple_c[cell];
    }
}

/* One whole cast, with the adapter in the mode the game plays in and the
   handler installed for the duration. */
static void cast_run(int spell_id, int target_count,
                     struct cast_capture *into)
{
    int cell;

    for (cell = 0; cell < INDICATOR_QUEUE_CELLS; cell++) {
        data_fdps_indicator_queue_glyph_ids[cell] = CAST_QUEUE_SENTINEL;
        data_fdps_battle_indicator_queue_unit_idx[cell] = CAST_QUEUE_SENTINEL;
        data_fdps_indicator_queue_cell_x_offset[cell] = CAST_QUEUE_SENTINEL;
    }
    data_fdps_indicator_queue_count = 0;

    cast_base_origin_x = data_fdps_battle_view_window_origin_x;
    cast_base_origin_y = data_fdps_battle_view_window_origin_y;
    cast_min_dx = 0;
    cast_max_dx = 0;
    cast_min_dy = 0;
    cast_max_dy = 0;
    cast_offset_samples = 0;
    cast_triples = 0;
    cast_last_a = 0;
    cast_last_b = 0;
    cast_last_c = 0;

    data_fdps_map_cursor_draw_mode = CAST_CURSOR_SENTINEL;

    flash_set_mode(FLASH_MODE_320X200X256);
    memset((void *) FLASH_VGA_BASE, 0, (size_t) FLASH_SCREEN_BYTES);

    cast_saved_timer = _dos_getvect(FLASH_TIMER_VECTOR);
    _dos_setvect(FLASH_TIMER_VECTOR, cast_timer_isr);
    data_fdps_view_frame_last_tick = data_fdps_timer_tick_counter;
    fdps_cast_spell_on_targets(CAST_CASTER, spell_id, target_count,
                               cast_targets);
    _dos_setvect(FLASH_TIMER_VECTOR, cast_saved_timer);

    flash_set_mode(FLASH_MODE_TEXT);

    cast_capture_into(into);
}

static struct fdps_unit_record *cast_unit(struct cast_capture *cap, int index)
{
    return (struct fdps_unit_record *)
        (cap->units + index * UNIT_RECORD_STRIDE);
}

static struct cast_capture cast_haste_cap;
static int cast_haste_done;

/* 神行術 over two of the three placed units, with every acted bit set going in
   and the third unit left out of the list as the witness. */
static void cast_haste_baseline(void)
{
    if (cast_haste_done != 0) {
        return;
    }

    cast_stage();
    stage_cost(CAST_HASTE, CAST_HASTE_COST);
    unit(CAST_A)->flags = 0xff;
    unit(CAST_B)->flags = 0xff;
    unit(CAST_C)->flags = 0xff;

    cast_run(CAST_HASTE, 2, &cast_haste_cap);
    cast_haste_done = 1;
}

/* The charge is the record's cost byte off the caster's current MP, and it is
   made whatever the spell goes on to do. */
static void a_cast_charges_the_casters_mp(void)
{
    cast_haste_baseline();

    CHECK_EQ(cast_unit(&cast_haste_cap, CAST_CASTER)->mp_current,
             CAST_MP_START - CAST_HASTE_COST);
    CHECK_EQ(cast_unit(&cast_haste_cap, CAST_CASTER)->mp_max, 0);
}

/* Bit 7 comes off every listed target and off nothing else.  The mask is 0x7f,
   so the other seven bits of the flags byte have to survive. */
static void haste_clears_the_acted_bit_of_every_target(void)
{
    cast_haste_baseline();

    CHECK_EQ(cast_unit(&cast_haste_cap, CAST_A)->flags, 0x7f);
    CHECK_EQ(cast_unit(&cast_haste_cap, CAST_B)->flags, 0x7f);
    CHECK_EQ(cast_unit(&cast_haste_cap, CAST_C)->flags, 0xff);
}

/* Nothing floats over a hasted unit: the arm has no indicator call in it. */
static void haste_queues_no_popup(void)
{
    cast_haste_baseline();

    CHECK_EQ(cast_haste_cap.glyphs[0], CAST_QUEUE_SENTINEL);
    CHECK_EQ(cast_haste_cap.glyphs[1], CAST_QUEUE_SENTINEL);
}

/* The cursor mode is 1 when the call returns whatever it was before, so the
   caller gets the plain cursor back and not the one it had. */
static void the_cast_leaves_the_map_cursor_in_plain_mode(void)
{
    cast_haste_baseline();

    CHECK_EQ(cast_haste_cap.cursor_mode, CAST_CURSOR_PLAIN);
}

static struct cast_capture cast_teleport_cap;
static int cast_teleport_done;

/* 傳送術 over two targets, with a destination tile that is neither unit's own
   and a cost that fills the whole byte. */
static void cast_teleport_baseline(void)
{
    if (cast_teleport_done != 0) {
        return;
    }

    cast_stage();
    stage_cost(CAST_TELEPORT, 0xff);
    data_fdps_battle_teleport_dest_tile_x = CAST_DEST_TILE_X;
    data_fdps_teleport_destination_tile_y = CAST_DEST_TILE_Y;
    data_fdps_map_cursor_world_x =
        (CAST_DEST_TILE_X - 1) * CAST_TILE_PIXELS;
    data_fdps_map_cursor_world_y = CAST_DEST_TILE_Y * CAST_TILE_PIXELS;

    cast_run(CAST_TELEPORT, 2, &cast_teleport_cap);
    cast_teleport_done = 1;
}

/* The first entry of the array is moved to the destination tile and the second
   one is not moved at all, however many targets were listed. */
static void only_the_first_target_is_teleported(void)
{
    cast_teleport_baseline();

    CHECK_EQ(cast_unit(&cast_teleport_cap, CAST_A)->pos_x, CAST_DEST_TILE_X);
    CHECK_EQ(cast_unit(&cast_teleport_cap, CAST_A)->pos_y, CAST_DEST_TILE_Y);
    CHECK_EQ(cast_unit(&cast_teleport_cap, CAST_B)->pos_x, CAST_B_TILE_X);
    CHECK_EQ(cast_unit(&cast_teleport_cap, CAST_B)->pos_y, CAST_B_TILE_Y);
}

/* The scroll is asked for in pixels and the record is written in tiles: the
   cursor walk ends on the destination tile times the 24-pixel tile size, and
   the same two globals go into the record as plain bytes.  A rebuild that
   scaled the record write, or handed the scroll the unscaled tile numbers,
   would move one of the two and not the other. */
static void the_teleport_scrolls_to_the_destination_in_pixels(void)
{
    cast_teleport_baseline();

    CHECK_EQ(cast_teleport_cap.cursor_world_x,
             CAST_DEST_TILE_X * CAST_TILE_PIXELS);
    CHECK_EQ(cast_teleport_cap.cursor_world_y,
             CAST_DEST_TILE_Y * CAST_TILE_PIXELS);
}

/* A cost byte of 0xff is 255 and not -1, and the difference is stored back
   through a signed word, so 50 - 255 comes out as -205 rather than 51 or a
   floor at zero. */
static void the_charge_reads_the_cost_byte_unsigned(void)
{
    cast_teleport_baseline();

    CHECK_EQ(cast_unit(&cast_teleport_cap, CAST_CASTER)->mp_current,
             CAST_MP_START - 0xff);
}

/* The arrival is drawn with the animation, not with a popup. */
static void teleport_queues_no_popup(void)
{
    cast_teleport_baseline();

    CHECK_EQ(cast_teleport_cap.glyphs[0], CAST_QUEUE_SENTINEL);
}

static struct cast_capture cast_cure_cap;
static int cast_cure_done;

/* 甦癒術 over one target carrying all three ailments and one carrying none,
   both of them with all three buff slots running. */
static void cast_cure_baseline(void)
{
    if (cast_cure_done != 0) {
        return;
    }

    cast_stage();
    stage_cost(CAST_CURE, CAST_MP_COST);

    unit(CAST_A)->status_timers[0] = 7;
    unit(CAST_A)->status_timers[1] = 8;
    unit(CAST_A)->status_timers[2] = 9;
    unit(CAST_A)->status_timers[CAST_AILMENT_FIRST] = 1;
    unit(CAST_A)->status_timers[CAST_AILMENT_FIRST + 1] = 2;
    unit(CAST_A)->status_timers[CAST_AILMENT_FIRST + 2] = 3;
    unit(CAST_B)->status_timers[0] = 7;

    cast_run(CAST_CURE, 2, &cast_cure_cap);
    cast_cure_done = 1;
}

/* The clear is three bytes at slot 3, so the three buff slots below it have to
   come through untouched, and it runs on the clean target as well. */
static void the_cure_clears_the_three_ailment_slots_and_no_others(void)
{
    cast_cure_baseline();

    CHECK_EQ(cast_unit(&cast_cure_cap, CAST_A)->
                 status_timers[CAST_AILMENT_FIRST], 0);
    CHECK_EQ(cast_unit(&cast_cure_cap, CAST_A)->
                 status_timers[CAST_AILMENT_FIRST + 1], 0);
    CHECK_EQ(cast_unit(&cast_cure_cap, CAST_A)->
                 status_timers[CAST_AILMENT_FIRST + 2], 0);
    CHECK_EQ(cast_unit(&cast_cure_cap, CAST_A)->status_timers[0], 7);
    CHECK_EQ(cast_unit(&cast_cure_cap, CAST_A)->status_timers[1], 8);
    CHECK_EQ(cast_unit(&cast_cure_cap, CAST_A)->status_timers[2], 9);
    CHECK_EQ(cast_unit(&cast_cure_cap, CAST_B)->status_timers[0], 7);
}

/* Only the target that had something to cure gets the popup, and it is four
   cells of the fixed word charged to that target's own index. */
static void the_cure_popup_goes_to_the_ailing_target_alone(void)
{
    cast_cure_baseline();

    CHECK_EQ(cast_cure_cap.glyphs[0], CAST_CURE_C);
    CHECK_EQ(cast_cure_cap.glyphs[1], CAST_CURE_U);
    CHECK_EQ(cast_cure_cap.glyphs[2], CAST_CURE_R);
    CHECK_EQ(cast_cure_cap.glyphs[3], CAST_CURE_E);
    CHECK_EQ(cast_cure_cap.owners[0], CAST_A);
    CHECK_EQ(cast_cure_cap.glyphs[4], CAST_QUEUE_SENTINEL);
}

static struct cast_capture cast_heal_cap;
static int cast_heal_done;

/* 恢復之光 over two wounded targets, with a power inside the band where the
   heal's spread term truncates away. */
static void cast_heal_baseline(void)
{
    if (cast_heal_done != 0) {
        return;
    }

    cast_stage();
    stage_spell(CAST_HEAL, CAST_POWER, CAST_ALWAYS);
    stage_cost(CAST_HEAL, CAST_MP_COST);
    unit(CAST_A)->hp_current = CAST_HP_START;
    unit(CAST_A)->hp_max = CAST_HP_MAX;
    unit(CAST_B)->hp_current = CAST_HP_START;
    unit(CAST_B)->hp_max = CAST_HP_MAX;

    cast_run(CAST_HEAL, 2, &cast_heal_cap);
    cast_heal_done = 1;
}

/* Every listed target is healed, by the record's power word and not by
   anything of the caster's. */
static void a_healing_spell_heals_every_target(void)
{
    cast_heal_baseline();

    CHECK_EQ(cast_unit(&cast_heal_cap, CAST_A)->hp_current,
             CAST_HP_START + CAST_ROLLED);
    CHECK_EQ(cast_unit(&cast_heal_cap, CAST_B)->hp_current,
             CAST_HP_START + CAST_ROLLED);
    CHECK_EQ(cast_unit(&cast_heal_cap, CAST_CASTER)->hp_current, 0);
    CHECK_EQ(cast_unit(&cast_heal_cap, CAST_CASTER)->mp_current,
             CAST_MP_START - CAST_MP_COST);
}

/* The figure floats over each target as its own four-cell popup, right
   aligned, drawn from glyph base 0x27.  Two targets append eight cells and no
   more. */
static void the_healed_figure_floats_from_the_heal_glyph_base(void)
{
    cast_heal_baseline();

    CHECK_EQ(cast_heal_cap.glyphs[0], CAST_BLANK_GLYPH);
    CHECK_EQ(cast_heal_cap.glyphs[1], CAST_BLANK_GLYPH);
    CHECK_EQ(cast_heal_cap.glyphs[2], CAST_BLANK_GLYPH);
    CHECK_EQ(cast_heal_cap.glyphs[3], CAST_HEAL_GLYPH_BASE + CAST_ROLLED);
    CHECK_EQ(cast_heal_cap.owners[3], CAST_A);
    CHECK_EQ(cast_heal_cap.glyphs[7], CAST_HEAL_GLYPH_BASE + CAST_ROLLED);
    CHECK_EQ(cast_heal_cap.owners[7], CAST_B);
    CHECK_EQ(cast_heal_cap.glyphs[8], CAST_QUEUE_SENTINEL);
}

static struct cast_capture cast_quake_cap;
static int cast_quake_done;

/* 裂地術 over two targets at full HP: the only arm that shakes the view, and
   one that falls through to the damage path afterwards.  The second target is
   given the flying class, which fdps_spell_damage_unit answers 0 for on a
   ground-shock spell however the hit roll went, and whose staged class record
   leaves it no magic resistance either -- so its figure is 0 by construction
   and one run drives both sides of the test on the figure that came back. */
static void cast_quake_baseline(void)
{
    if (cast_quake_done != 0) {
        return;
    }

    cast_stage();
    stage_spell(SPELL_QUAKE, CAST_POWER, CAST_ALWAYS);
    stage_cost(SPELL_QUAKE, CAST_MP_COST);
    unit(CAST_A)->hp_current = CAST_FULL_HP;
    unit(CAST_A)->hp_max = CAST_FULL_HP;
    unit(CAST_A)->clazz = GROUND_CLASS;
    unit(CAST_B)->hp_current = CAST_FULL_HP;
    unit(CAST_B)->hp_max = CAST_FULL_HP;
    unit(CAST_B)->clazz = FLYING_CLASS;

    cast_run(SPELL_QUAKE, 2, &cast_quake_cap);
    cast_quake_done = 1;
}

/* The view is moved off centre on most of the 25 shake frames and is back
   where it started when the call returns.  The offsets never reach +2 on
   either axis, which is what says the range is rand() % 4 - 2 and not the
   symmetric jitter the shape invites, and they do reach the negative side. */
static void the_ground_shock_jitters_the_view_and_puts_it_back(void)
{
    cast_quake_baseline();

    CHECK_EQ(cast_quake_cap.origin_x, 0);
    CHECK_EQ(cast_quake_cap.origin_y, 0);
    CHECK_EQ(cast_quake_cap.offset_samples >= CAST_SHAKE_MIN_SAMPLES, 1);
    CHECK_EQ(cast_quake_cap.offset_samples <= CAST_SHAKE_FRAMES, 1);
    CHECK_EQ(cast_quake_cap.max_dx <= CAST_SHAKE_MAX_OFFSET, 1);
    CHECK_EQ(cast_quake_cap.max_dy <= CAST_SHAKE_MAX_OFFSET, 1);
    CHECK_EQ(cast_quake_cap.min_dx <= CAST_SHAKE_MIN_OFFSET, 1);
    CHECK_EQ(cast_quake_cap.min_dy <= CAST_SHAKE_MIN_OFFSET, 1);
}

/* A target that took damage loses that HP and the figure floats over it from
   glyph base 0, which is the other of the two bases and not the heal's. */
static void the_damage_figure_floats_from_glyph_base_zero(void)
{
    cast_quake_baseline();

    CHECK_EQ(cast_unit(&cast_quake_cap, CAST_A)->hp_current,
             CAST_FULL_HP - CAST_ROLLED);
    CHECK_EQ(cast_quake_cap.glyphs[0], CAST_BLANK_GLYPH);
    CHECK_EQ(cast_quake_cap.glyphs[3], CAST_DAMAGE_GLYPH_BASE + CAST_ROLLED);
    CHECK_EQ(cast_quake_cap.owners[3], CAST_A);
}

/* A figure of zero shows MISS instead, and the two outcomes are decided by the
   value the damage call came back with and by nothing else: both targets were
   in the same list, on the same spell, at a hit rate that always lands. */
static void a_damage_figure_of_zero_shows_miss_instead(void)
{
    cast_quake_baseline();

    CHECK_EQ(cast_unit(&cast_quake_cap, CAST_B)->hp_current, CAST_FULL_HP);
    CHECK_EQ(cast_quake_cap.glyphs[4], CAST_MISS_M);
    CHECK_EQ(cast_quake_cap.glyphs[5], CAST_MISS_I);
    CHECK_EQ(cast_quake_cap.glyphs[6], CAST_MISS_S);
    CHECK_EQ(cast_quake_cap.glyphs[7], CAST_MISS_S);
    CHECK_EQ(cast_quake_cap.owners[4], CAST_B);
    CHECK_EQ(cast_quake_cap.glyphs[8], CAST_QUEUE_SENTINEL);
}

static struct cast_capture cast_status_cap;
static int cast_status_done;

/* 麻痺術 at a hit rate that always lands, over one target whose paralysis slot
   is already counting down -- which is the one thing the roll refuses on -- and
   one whose is clear. */
static void cast_status_baseline(void)
{
    if (cast_status_done != 0) {
        return;
    }

    cast_stage();
    stage_spell(CAST_PARALYSIS, 0, CAST_ALWAYS);
    stage_cost(CAST_PARALYSIS, CAST_MP_COST);
    unit(CAST_A)->status_timers[CAST_PARALYSIS_SLOT] = CAST_TIMER_OCCUPIED;

    cast_run(CAST_PARALYSIS, 2, &cast_status_cap);
    cast_status_done = 1;
}

/* The id reaches the roll as itself, so the timer that moves is the one 0x13
   selects and neither of its neighbours. */
static void the_ailment_lands_in_the_slot_its_own_id_names(void)
{
    cast_status_baseline();

    CHECK_EQ(cast_unit(&cast_status_cap, CAST_B)->
                 status_timers[CAST_PARALYSIS_SLOT] >= CAST_TIMER_MIN, 1);
    CHECK_EQ(cast_unit(&cast_status_cap, CAST_B)->
                 status_timers[CAST_PARALYSIS_SLOT] <= CAST_TIMER_MAX, 1);
    CHECK_EQ(cast_unit(&cast_status_cap, CAST_B)->
                 status_timers[CAST_PARALYSIS_SLOT - 1], 0);
    CHECK_EQ(cast_unit(&cast_status_cap, CAST_B)->
                 status_timers[CAST_PARALYSIS_SLOT + 1], 0);
}

/* A roll that comes back zero shows MISS over that target and leaves its timer
   as it was; the target it landed on gets no popup at all. */
static void a_refused_roll_shows_miss_over_that_target_alone(void)
{
    cast_status_baseline();

    CHECK_EQ(cast_unit(&cast_status_cap, CAST_A)->
                 status_timers[CAST_PARALYSIS_SLOT], CAST_TIMER_OCCUPIED);
    CHECK_EQ(cast_status_cap.glyphs[0], CAST_MISS_M);
    CHECK_EQ(cast_status_cap.glyphs[1], CAST_MISS_I);
    CHECK_EQ(cast_status_cap.glyphs[2], CAST_MISS_S);
    CHECK_EQ(cast_status_cap.glyphs[3], CAST_MISS_S);
    CHECK_EQ(cast_status_cap.owners[0], CAST_A);
    CHECK_EQ(cast_status_cap.glyphs[4], CAST_QUEUE_SENTINEL);
}

static struct cast_capture cast_blessing_cap;
static int cast_blessing_done;

/* 神之祝福 over three targets, each with exactly one of the three buff slots
   free, so every pass lands on one target and only one.  Every derived combat
   word starts at a value no recompute can produce. */
static void cast_blessing_baseline(void)
{
    if (cast_blessing_done != 0) {
        return;
    }

    cast_stage();
    stage_spell(CAST_BLESSING, 0, CAST_ALWAYS);
    stage_cost(CAST_BLESSING, CAST_MP_COST);

    unit(CAST_A)->status_timers[1] = CAST_TIMER_OCCUPIED;
    unit(CAST_A)->status_timers[2] = CAST_TIMER_OCCUPIED;
    unit(CAST_B)->status_timers[0] = CAST_TIMER_OCCUPIED;
    unit(CAST_B)->status_timers[2] = CAST_TIMER_OCCUPIED;
    unit(CAST_C)->status_timers[0] = CAST_TIMER_OCCUPIED;
    unit(CAST_C)->status_timers[1] = CAST_TIMER_OCCUPIED;

    unit(CAST_A)->ap = CAST_STAT_SENTINEL;
    unit(CAST_A)->ev = CAST_STAT_SENTINEL;
    unit(CAST_B)->ap = CAST_STAT_SENTINEL;
    unit(CAST_B)->ev = CAST_STAT_SENTINEL;
    unit(CAST_C)->ap = CAST_STAT_SENTINEL;
    unit(CAST_C)->ev = CAST_STAT_SENTINEL;
    unit(CAST_CASTER)->ap = CAST_STAT_SENTINEL;
    unit(CAST_CASTER)->ev = CAST_STAT_SENTINEL;

    cast_run(CAST_BLESSING, CAST_BUFF_SLOTS, &cast_blessing_cap);
    cast_blessing_done = 1;
}

/* Three passes, and the pass number is what is handed to the roll as the
   effect id, which is also the timer slot it lands in.  Each target's two
   occupied slots are still holding the value that made them refuse. */
static void each_blessing_pass_lands_in_its_own_slot(void)
{
    cast_blessing_baseline();

    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_A)->status_timers[0]
                 >= CAST_TIMER_MIN, 1);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_A)->status_timers[0]
                 <= CAST_TIMER_MAX, 1);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_A)->status_timers[1],
             CAST_TIMER_OCCUPIED);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_B)->status_timers[1]
                 >= CAST_TIMER_MIN, 1);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_B)->status_timers[1]
                 <= CAST_TIMER_MAX, 1);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_C)->status_timers[2]
                 >= CAST_TIMER_MIN, 1);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_C)->status_timers[2]
                 <= CAST_TIMER_MAX, 1);
}

/* A landing buff recomputes that target's combat words and nothing recomputes
   the caster's.  With every base stat at zero the recomputed attack is zero
   and the evasion is the flat bonus a live third slot adds, so both say the
   recompute ran rather than merely that something moved. */
static void a_landing_buff_recomputes_that_targets_combat_stats(void)
{
    cast_blessing_baseline();

    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_A)->ap, 0);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_B)->ap, 0);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_C)->ap, 0);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_C)->ev, CAST_DEX_BONUS);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_CASTER)->ap,
             CAST_STAT_SENTINEL);
    CHECK_EQ(cast_unit(&cast_blessing_cap, CAST_CASTER)->ev,
             CAST_STAT_SENTINEL);
}

/* The queue is played out between passes, so the three groups reach the screen
   one after another and in slot order.  The groups themselves are the twelve
   bytes the function copies onto its frame before anything else. */
static void the_three_buff_groups_are_played_one_pass_at_a_time(void)
{
    cast_blessing_baseline();

    CHECK_EQ(cast_blessing_cap.triples, CAST_BUFF_SLOTS);
    CHECK_EQ(cast_blessing_cap.triple_a[0], CAST_BUFF0_A);
    CHECK_EQ(cast_blessing_cap.triple_b[0], CAST_BUFF0_B);
    CHECK_EQ(cast_blessing_cap.triple_c[0], CAST_BUFF0_C);
    CHECK_EQ(cast_blessing_cap.triple_a[1], CAST_BUFF1_A);
    CHECK_EQ(cast_blessing_cap.triple_b[1], CAST_BUFF1_B);
    CHECK_EQ(cast_blessing_cap.triple_c[1], CAST_BUFF1_C);
    CHECK_EQ(cast_blessing_cap.triple_a[2], CAST_BUFF2_A);
    CHECK_EQ(cast_blessing_cap.triple_b[2], CAST_BUFF2_B);
    CHECK_EQ(cast_blessing_cap.triple_c[2], CAST_BUFF2_C);
}

/* The fourth byte of a group is a terminator and not a sprite: three cells are
   appended per landing buff, so the fourth cell of the queue is still holding
   what the run put there. */
static void the_fourth_byte_of_a_buff_group_queues_nothing(void)
{
    cast_blessing_baseline();

    CHECK_EQ(cast_blessing_cap.glyphs[0], CAST_BUFF2_A);
    CHECK_EQ(cast_blessing_cap.glyphs[1], CAST_BUFF2_B);
    CHECK_EQ(cast_blessing_cap.glyphs[2], CAST_BUFF2_C);
    CHECK_EQ(cast_blessing_cap.glyphs[3], CAST_QUEUE_SENTINEL);
}

/* The record offsets this function addresses by hand, against the layouts
   ticket 17 settled: the tile position the teleport writes, the flags byte the
   haste masks, the ailment triple the cure clears at slot 3, and the MP word
   the charge reads and writes. */
static void the_offsets_the_cast_reaches_are_where_ticket_17_puts_them(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 0x01);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 0x05);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers)
                 + CAST_AILMENT_FIRST, 0x25);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_current), 0x44);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, power), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, mp_cost), 0x05);
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

    RUN_TEST(every_spell_id_flashes_the_colour_its_planes_hold);
    RUN_TEST(the_element_colours_reach_the_dac_in_channel_order);
    RUN_TEST(the_spells_with_no_element_colour_flash_white);
    RUN_TEST(the_flash_presents_four_passes_alternating_with_black);
    RUN_TEST(the_flash_leaves_dac_entry_zero_black);
    RUN_TEST(nothing_but_dac_entry_zero_is_written);

    RUN_TEST(the_offsets_the_cast_reaches_are_where_ticket_17_puts_them);
    RUN_TEST(a_cast_charges_the_casters_mp);
    RUN_TEST(haste_clears_the_acted_bit_of_every_target);
    RUN_TEST(haste_queues_no_popup);
    RUN_TEST(the_cast_leaves_the_map_cursor_in_plain_mode);
    RUN_TEST(only_the_first_target_is_teleported);
    RUN_TEST(the_teleport_scrolls_to_the_destination_in_pixels);
    RUN_TEST(the_charge_reads_the_cost_byte_unsigned);
    RUN_TEST(teleport_queues_no_popup);
    RUN_TEST(the_cure_clears_the_three_ailment_slots_and_no_others);
    RUN_TEST(the_cure_popup_goes_to_the_ailing_target_alone);
    RUN_TEST(a_healing_spell_heals_every_target);
    RUN_TEST(the_healed_figure_floats_from_the_heal_glyph_base);
    RUN_TEST(the_ground_shock_jitters_the_view_and_puts_it_back);
    RUN_TEST(the_damage_figure_floats_from_glyph_base_zero);
    RUN_TEST(a_damage_figure_of_zero_shows_miss_instead);
    RUN_TEST(the_ailment_lands_in_the_slot_its_own_id_names);
    RUN_TEST(a_refused_roll_shows_miss_over_that_target_alone);
    RUN_TEST(each_blessing_pass_lands_in_its_own_slot);
    RUN_TEST(a_landing_buff_recomputes_that_targets_combat_stats);
    RUN_TEST(the_three_buff_groups_are_played_one_pass_at_a_time);
    RUN_TEST(the_fourth_byte_of_a_buff_group_queues_nothing);
}
