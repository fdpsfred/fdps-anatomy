/* unitstat.c -- what a battle effect does to one unit's own numbers: HP and
 * MP, the status effects running on it, and the experience those credit.
 *
 * See unitstat.h for what a caller has to know.  The file owns no state: it
 * reaches every unit record through fdps_get_unit_record (unit.h) and every
 * ENEMYDAT.DAT record through fdps_get_enemy_record (table.h), and the only
 * global it writes is data_fdps_battle_pending_xp_credit, which gamedata.h
 * declares and gamedata.c defines.
 *
 * rand comes from <stdlib.h>; it is a real call in the original -- CALL
 * 00042cf8 -- and not an inline expansion.
 */
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "table.h"
#include "unit.h"
#include "unitstat.h"

/* The heal is nine tenths of the request, LEA EDX,[EDX+EDX*8] followed by
   IDIV by 10. */
#define HEAL_NUMERATOR 9
#define HEAL_DENOMINATOR 10

/* On top of that, (rand() % 100) * amount / 1000 -- up to another 99/1000 of
   the request, so the roll spans 0.900*N to 0.999*N. */
#define PERCENT 100
#define HEAL_SPREAD_DIVISOR 1000

/* Each level of the healed unit is worth 25 before the share of its maximum
   HP is taken: IMUL EDX,dword ptr [EBP-0x8],0x19. */
#define EXP_PER_LEVEL 0x19

/* Portrait ids 0x0f..0x21 are the promoted character forms, whose level bytes
   restart at 1; 30 is added to the level they credit experience by so that a
   promotion does not cut the experience the unit pays.  The test is CMP
   EAX,0xf / JL then CMP EAX,0x22 / JL, so both ends are on the portrait id
   and neither is on the class.

   fdps_battle_spell_command tests portrait id > 8 for the same +30 at
   000280cf.  The two spans are different and the difference is what each
   function credits; they are not one rule written twice. */
#define FIRST_PROMOTED_PORTRAIT_ID 0x0f
#define LAST_PROMOTED_PORTRAIT_ID 0x21
#define PROMOTED_LEVEL_BONUS 0x1e

/* Portrait ids from 0x3c up are the enemy roster.  Healing one of those pays
   no experience at all -- the accumulator is not even touched, so the figure
   a previous effect left in it stands. */
#define FIRST_ENEMY_PORTRAIT_ID 0x3c

/* 00027070.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP, SUB ESP,0x20, with the two arguments read from [EBP+0x14] and
   [EBP+0x18] and all three callers doing ADD ESP,0x8 after the CALL.

   Two things about the shape are load-bearing and neither reads that way.

   The returned figure is the ROLL, recomputed at 00027167 from the same two
   locals the heal was built out of, and not the HP the record gained.  The
   difference is only visible on a target near full HP, because the clamp is
   what separates them, and fdps_apply_heal_to_targets prints the returned
   number over the target.  Writing the obvious `return hp_restored` shows a
   smaller number than the original does on exactly those heals.

   The HP the experience is scaled by, on the other hand, IS the restored HP:
   the store into the record at 00027106 happens after MOVSX EAX,word ptr
   [EAX+0x40] at 000270f4 reads the old value back out for the subtraction, so
   the record is the thing holding the pre-heal HP at that point and the local
   already holds the post-clamp total.  A heal on a unit already at full HP
   therefore credits nothing while still returning its roll.

   Every division is the signed IDIV with the dividend sign extended by SAR
   EDX,0x1f, so all three truncate towards zero rather than flooring.  That is
   observable on a negative amount: -7 gives a base heal of -6, not -7.

   There is no guard anywhere: the final IDIV divides by hp_max with the
   record's own word, so a roster unit whose maximum HP is 0 is a divide error
   in the original too, and neither the roll nor the HP is clamped at the
   bottom. */
int fdps_unit_apply_heal(int unit_index, int amount)
{
    struct fdps_unit_record *unit;
    int max_hp;
    int hp_after_heal;
    int base_heal;
    int random_bonus;
    int hp_restored;
    int effective_level;

    unit = fdps_get_unit_record(unit_index);

    /* Both MOVSX word loads: the two HP fields are signed 16-bit and widen
       into signed locals, and the clamp below is the signed JLE at 000270e9. */
    hp_after_heal = unit->hp_current;
    max_hp = unit->hp_max;

    base_heal = amount * HEAL_NUMERATOR / HEAL_DENOMINATOR;
    random_bonus = (rand() % PERCENT) * amount / HEAL_SPREAD_DIVISOR;

    hp_after_heal += base_heal + random_bonus;
    if (hp_after_heal > max_hp) {
        hp_after_heal = max_hp;
    }

    /* The record still holds the pre-heal HP here; the store is the next
       statement. */
    hp_restored = hp_after_heal - unit->hp_current;
    unit->hp_current = (short) hp_after_heal;

    /* XOR EAX,EAX / MOV AL,byte ptr [EDX+0x21]: the level is a zero-extended
       byte, and the +30 is added at int width, so a level 240 promoted form
       divides by 270 rather than wrapping. */
    effective_level = unit->level;
    if (unit->portrait_id >= FIRST_PROMOTED_PORTRAIT_ID &&
        unit->portrait_id <= LAST_PROMOTED_PORTRAIT_ID) {
        effective_level += PROMOTED_LEVEL_BONUS;
    }

    /* ADD dword ptr [0x00069cec],EAX -- an accumulate, not an assignment. */
    if (unit->portrait_id < FIRST_ENEMY_PORTRAIT_ID) {
        data_fdps_battle_pending_xp_credit +=
            effective_level * EXP_PER_LEVEL * hp_restored / max_hp;
    }

    return base_heal + random_bonus;
}

/* 000275a0.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP, SUB ESP,0x1c, the two arguments read from [EBP+0x14] and
   [EBP+0x18], and all four call sites -- 0001e6bc, 0001e746 and 0001e7d7 in
   fdps_battle_advance_turn, 00026a1b in fdps_apply_item_effect_to_targets --
   doing ADD ESP,0x8 after the CALL.

   The roll is built from the same three constants as the HP twin above and by
   the same three divides; what differs is everything around it.

   The returned figure is the ROLL and not the MP the record gained, exactly as
   in the twin: 00027648 rebuilds it from the two locals the restore was built
   out of instead of reading the clamped total back.  Every call site passes
   the returned value straight to fdps_show_number_indicator, so returning the
   real gain would float a smaller number than the original over a unit near
   full MP -- and a 0 where the original shows the full roll, because
   fdps_apply_item_effect_to_targets does not filter out a target that is
   already at full MP.

   Both MP fields are read ZERO-extended -- XOR EAX,EAX / MOV AX,word ptr
   [EDX+0x44] at 000275bd and again for +0x46, then MOV AX / AND EAX,0xffff at
   0002762d -- where the twin uses MOVSX on its two HP fields.  The casts below
   are that difference and it is observable: nothing clamps the MP at the
   bottom, so a drained unit can hold a negative word, and this function reads
   that word back as a number near 65535 and clamps it straight up to mp_max
   rather than climbing out of the negative.  Every other reader of the same
   field -- 0001346a, 00013075's neighbour in the item scorer, 0002826d,
   000285ed, 0002895b -- uses MOVSX, so the width is this function's own and
   not the record's type.

   The MP the restore actually applied is computed at 0002763b into a stack
   local that is never read again.  It is kept because it is precisely the
   difference between what the record gets and what the player is shown.

   Unlike the twin there is no divide by the maximum, so a unit whose maximum
   MP is 0 is clamped to 0 here rather than faulting, and no experience is
   credited at all: this function touches nothing but +0x44. */
int fdps_unit_restore_mp(int unit_index, int amount)
{
    struct fdps_unit_record *unit;
    int max_mp;
    int mp_after_restore;
    int base_restore;
    int random_bonus;
    int mp_gained;

    unit = fdps_get_unit_record(unit_index);

    mp_after_restore = (unsigned short) unit->mp_current;
    max_mp = (unsigned short) unit->mp_max;

    base_restore = amount * HEAL_NUMERATOR / HEAL_DENOMINATOR;
    random_bonus = (rand() % PERCENT) * amount / HEAL_SPREAD_DIVISOR;

    mp_after_restore += base_restore + random_bonus;
    if (mp_after_restore > max_mp) {
        mp_after_restore = max_mp;
    }

    /* Stored at 0002763b and never loaded again; the record still holds the
       pre-restore MP here, because the store is the next statement. */
    mp_gained = mp_after_restore - (unsigned short) unit->mp_current;
    unit->mp_current = (short) mp_after_restore;

    return base_restore + random_bonus;
}

/* CMP dword ptr [EBP-0xc],0x5: the outer walk is over the five bytes of
   struct fdps_unit_record's spells_known_bitmap, and the byte is reached as
   record + byte_index then byte ptr [that + 0x1a], which is the field's own
   offset. */
#define SPELL_BITMAP_BYTES 5

/* CMP dword ptr [EBP-0x14],0x8 for the inner walk, and SHL AL,0x3 for the id
   the outer index contributes -- the same 8, so the ids run 0..39 with no
   gap between one byte's block and the next. */
#define SPELL_BITMAP_BITS_PER_BYTE 8

/* 00027840.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP, SUB ESP,0x18, the two arguments read from [EBP+0x14] and
   [EBP+0x18], and all eight call sites -- 00013479, 00013a5a, 00015df5,
   00016c07, 00027704, 000278f4, 00027d87, 00028144 -- doing ADD ESP,0x8 after
   the CALL.

   Three things about the shape are worth stating.

   The counter is advanced only inside the `if`, at 000278c1, and is what both
   indexes out_ids and becomes the return value.  So the buffer is a packed
   list of the ids that are set and not a bitmap-shaped array: a unit knowing
   only spell 39 gets that id at out_ids[0], not at out_ids[39].

   The out_ids test is inside the bit test rather than around the whole walk --
   CMP dword ptr [EBP+0x18],0x0 at 000278aa, once per set bit -- so a NULL
   buffer still runs the full count.  That is the mode three of the call sites
   use, and it is the only reason the function is reachable with no buffer at
   all.

   The mask byte is loaded XOR EAX,EAX / MOV AL, so it widens UNSIGNED into an
   int, and the shift that tests it is the signed SAR at 000278a4.  The two
   agree here because a zero-extended byte is never negative; the widening is
   the half that matters, since a sign-extended 0x80 would make every bit above
   7 of the shifted value set and the `& 1` would still be reading bit 7 of the
   original byte -- the same answer, reached by accident.  The zero extension is
   what makes it not an accident. */
int fdps_unit_collect_known_spells(int unit_index, unsigned char *out_ids)
{
    struct fdps_unit_record *unit;
    int byte_index;
    int bit;
    int mask_byte;
    int spells_found;

    /* Zeroed at 0002784c, before the record lookup rather than after it. */
    spells_found = 0;
    unit = fdps_get_unit_record(unit_index);

    for (byte_index = 0; byte_index < SPELL_BITMAP_BYTES; byte_index++) {
        mask_byte = unit->spells_known_bitmap[byte_index];

        for (bit = 0; bit < SPELL_BITMAP_BITS_PER_BYTE; bit++) {
            if ((mask_byte >> bit) & 1) {
                if (out_ids != NULL) {
                    out_ids[spells_found] = (unsigned char)
                        (byte_index * SPELL_BITMAP_BITS_PER_BYTE + bit);
                }
                spells_found++;
            }
        }
    }

    return spells_found;
}

/* The damage roll is built from the same three literals as the heal at the top
   of this file, and they are written out again rather than shared because they
   are this function's own: LEA EDX,[EDX+EDX*8] with IDIV by 10 at 00028496,
   then IDIV by 100 / IMUL by the nominal figure / IDIV by 1000 at 000284a8.
   PERCENT above is the 100 of the modulo and is the one that is shared, being
   named for what it is rather than for either function. */
#define DAMAGE_NUMERATOR 9
#define DAMAGE_DENOMINATOR 10
#define DAMAGE_SPREAD_DIVISOR 1000

/* CMP byte ptr [EAX + 0x6],0x0 / JNZ at 000284f7 -- an equality on the side
   byte, not a truth test, so only side 0 takes the experience branch.  deploy.c
   places the player's own units on side 2, and side 0 is what an ENEMYDAT.DAT
   unit is deployed with, which is why the branch may assume the portrait id is
   an enemy one. */
#define ENEMY_SIDE 0

/* 00028460.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP,
   SUB ESP,0x20, the two arguments read from [EBP+0x14] and [EBP+0x18], and all
   three call sites -- 00026267 in fdps_apply_damage_to_targets, 00026aab in
   fdps_apply_item_effect_to_targets and 00028435 in fdps_spell_damage_unit --
   doing ADD ESP,0x8 after the CALL.

   Three things about the shape are load-bearing.

   The returned figure is the ROLL and not the HP the unit lost.  The clamp at
   000284e3 applies to the stored HP only, and the return at 00028551 reloads
   the roll from [EBP-0x8] rather than differencing the record.  All three
   callers float the returned number over the target, so an overkill reports
   more damage than the target had left; writing the obvious
   `return hp_before - hp_after` shows a smaller number on exactly those hits.

   Both HP fields are read ZERO-extended -- XOR EAX,EAX / MOV AX,word ptr
   [EDX+0x40] at 0002847d and again for +0x42 -- where fdps_unit_apply_heal
   above uses MOVSX on the same two fields.  The casts below are that difference
   and it is observable, because nothing in the heal clamps at the bottom: a
   unit whose hp_current has been driven negative reads back here as a number
   near 65535, so the clamp at zero cannot fire and the subtraction wraps back
   into a negative word instead.  The same zero extension puts hp_max in 0..65535
   for the divide, so a negative maximum divides by roughly 65532 and awards
   nothing rather than awarding a negative figure.

   The award is prorated ONLY while the target is still standing.  CMP dword ptr
   [EBP-0x18],0x0 / JZ at 00028530 jumps past the divide, so a target whose
   clamped HP is zero keeps the whole of exp_reward * level.  A kill therefore
   pays the full record value however little of the damage was needed, and the
   test is on the CLAMPED HP, so a unit that was already at 0 before the hit
   also pays in full.

   There is no guard on hp_max: an enemy record whose maximum HP is 0 and which
   survives the hit is a divide error in the original too. */
int fdps_unit_apply_damage(int unit_index, int base_damage)
{
    struct fdps_unit_record *unit;
    struct fdps_enemy_data *enemy_record;
    int hp_after_damage;
    int max_hp;
    int base_roll;
    int random_bonus;
    int damage_rolled;
    int exp_award;

    unit = fdps_get_unit_record(unit_index);

    hp_after_damage = (unsigned short) unit->hp_current;
    max_hp = (unsigned short) unit->hp_max;

    base_roll = base_damage * DAMAGE_NUMERATOR / DAMAGE_DENOMINATOR;
    random_bonus = (rand() % PERCENT) * base_damage / DAMAGE_SPREAD_DIVISOR;
    damage_rolled = base_roll + random_bonus;

    /* JGE at 000284e1 -- the signed test, and the clamp is on the stored HP
       alone. */
    hp_after_damage -= damage_rolled;
    if (hp_after_damage < 0) {
        hp_after_damage = 0;
    }
    unit->hp_current = (short) hp_after_damage;

    if (unit->side == ENEMY_SIDE) {
        /* MOV AL,byte ptr [EAX + 0x7] / AND EAX,0xff / SUB EAX,0x3c: the
           portrait id is zero extended before the bias, so the argument is the
           ENEMYDAT.DAT record index. */
        enemy_record = fdps_get_enemy_record(
            unit->portrait_id - FIRST_ENEMY_PORTRAIT_ID);

        /* XOR EDX,EDX / MOV DL,byte ptr [EAX + 0x9] for the record's experience
           multiplier and MOV AL,byte ptr [EAX + 0x21] / AND EAX,0xff for the
           level: both zero extended, then IMUL at 0002852a. */
        exp_award = enemy_record->exp_reward * unit->level;

        if (hp_after_damage != 0) {
            exp_award = exp_award * damage_rolled / max_hp;
        }

        /* ADD dword ptr [0x00069cec],EAX -- an accumulate, not an
           assignment. */
        data_fdps_battle_pending_xp_credit += exp_award;
    }

    return damage_rolled;
}

/* CMP dword ptr [EBP-0x4],0x3 at 00028f02: three passes, and the byte each one
   stores into is record + i + 0x25 -- MOV EBX,[EBP-0x8] / ADD EBX,[EBP-0x4] /
   MOV byte ptr [EBX+0x25],DL at 00028f52.  status_timers starts at record
   +0x22, so those are slots 3, 4 and 5: poison, paralysis and 封魔咒術. */
#define AILMENT_SLOT_COUNT 3
#define FIRST_AILMENT_TIMER_SLOT 3

/* CMP EDX,0x14 / JGE at 00028f25 -- a flat 20 out of the rand() % 100, written
   as a literal.  No MAGICDAT.DAT record is read anywhere in this function, so
   there is no hit rate for the spell to lower or raise. */
#define AILMENT_CHANCE 0x14

/* MOV EBX,0x2 / IDIV / ADD EDX,0x2 at 00028f43: two or three turns, the same
   spread the paralysis a weapon inflicts uses. */
#define AILMENT_TURNS_SPREAD 2
#define AILMENT_TURNS_BASE 2

/* 00028ee0.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP,
   SUB ESP,0x8, the single argument read from [EBP+0x14], and both call sites --
   0001aea5 in fdps_combat_play_spell_on_targets and 00028e90 in
   fdps_cast_spell_on_targets -- pushing one zero-extended byte and doing ADD
   ESP,0x4 after the CALL.  Nothing is left in EAX on the exit path and neither
   caller reads one.

   Three things about the shape are load-bearing and none of them reads that
   way.

   THE IMMUNITY TEST STAYS INSIDE THE LOOP AND STAYS SECOND.  It depends only on
   unit_index and on record fields this loop never writes, so it is
   loop-invariant and asks to be hoisted out or moved in front of the chance
   roll.  Either move skips rand() calls the original makes: the CALL at
   00028f12 runs on all three passes whatever the unit is, and the CALL at
   00028f2e is reached only once a pass has already rolled under 20.  rand() is
   one stream shared by every roll in the battle, so a call this function does
   not make is a value some later roll takes instead.

   THERE IS NO ALREADY-AFFLICTED TEST.  The store is unconditional once the two
   tests pass, so a slot that is already counting down is overwritten and its
   duration refreshed.  fdps_unit_apply_status_effect, which lands one named
   effect, does check -- the difference between the two is deliberate.

   NO GLOBAL IS TOUCHED.  data_fdps_battle_pending_xp_credit is not added to,
   which fdps_unit_apply_status_effect does do, so the ailments this seeds earn
   the party nothing. */
void fdps_unit_inflict_random_ailments(int unit_index)
{
    struct fdps_unit_record *unit;
    int slot;

    unit = fdps_get_unit_record(unit_index);

    for (slot = 0; slot < AILMENT_SLOT_COUNT; slot++) {
        /* Both divides are the signed IDIV with the dividend sign extended by
           SAR EDX,0x1f, and both remainders are taken from EDX. */
        if (rand() % PERCENT < AILMENT_CHANCE &&
            fdps_unit_is_ailment_immune(unit_index) == 0) {
            unit->status_timers[FIRST_AILMENT_TIMER_SLOT + slot] =
                (unsigned char) (rand() % AILMENT_TURNS_SPREAD +
                                 AILMENT_TURNS_BASE);
        }
    }
}

/* CMP 0x11 / MOV 0x27, CMP 0x12 / MOV 0x25, CMP 0x13 / MOV 0x26 at
   00028f92..00028fbd.  The three ids are the MAGICDAT.DAT indexes of the three
   ailment spells (assets/spells.md), and the offset each one selects is NOT the
   one its position in that run suggests: status_timers starts at record +0x22,
   so 0x27, 0x25 and 0x26 are slots 5, 3 and 4 -- 封魔咒術 first, then the
   poison and paralysis pair. */
#define SPELL_ID_SEAL 0x11
#define SPELL_ID_POISON 0x12
#define SPELL_ID_PARALYSIS 0x13
#define SEAL_TIMER_SLOT 5
#define POISON_TIMER_SLOT 3
#define PARALYSIS_TIMER_SLOT 4

/* MOV dword ptr [EBP+0x14],0x14 at 00028fc8: an id outside those three is
   REPLACED by 0x14, 神之祝福, before the hit rate is fetched and before the
   0x11..0x13 span that gates the immunity test is measured. */
#define SPELL_ID_BLESSING 0x14

/* IMUL EDX,EDX,0xa at 0002905d, on the zero-extended level byte. */
#define STATUS_EXP_PER_LEVEL 0xa

/* 00028f70.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP,
   SUB ESP,0x18, the two arguments read from [EBP+0x14] and [EBP+0x18], and all
   three call sites -- 00028cec, 00028d4c and 00028dc4, every one of them in
   fdps_cast_spell_on_targets -- pushing the unit id first and the effect id
   second, doing ADD ESP,0x8 after the CALL and then TEST EAX,EAX.

   Four things about the shape are load-bearing and none of them reads that way.

   THE ID IS REWRITTEN BEFORE THE HIT RATE IS LOOKED UP.  Only 0x11, 0x12 and
   0x13 reach fdps_get_spell_record as themselves; every other value is replaced
   by 0x14 at 00028fc8, so the three 神之祝福 slots the one caller passes as 0,
   1 and 2 all roll against spell 0x14's hit rate of 100 and land every time.
   Writing the obvious fdps_get_spell_record(effect_id) reads spells 0, 1 and 2
   -- 業火, 狂暴巨燄, 烈獄之火, hit rate 95 (assets/spells.md) -- and makes
   the blessing miss one time in twenty.

   THE OFFSET TABLE IS NOT SEQUENTIAL.  0x11 selects +0x27 and 0x12 selects
   +0x25, so the ailment ids and the timer slots run in different orders.

   THE IMMUNITY TEST IS SPANNED ON THE REWRITTEN ID.  CMP 0x11 / JL and CMP 0x13
   / JLE at 0002900e are read from [EBP+0x14] after the rewrite, which is why the
   blessing slots skip fdps_unit_is_ailment_immune entirely: 0x14 is past the top
   of the span.  A unit that is immune to poison still takes all three buffs.

   THE THREE TESTS ARE SHORT-CIRCUITED IN THIS ORDER and each one costs draws out
   of the shared rand() stream.  The hit roll at 00028fe9 always runs; the
   timer-is-clear test at 00029007 runs only after it passed; the immunity call at
   00029020 runs only after both; and the duration draw at 0002902c only after all
   three.  Hoisting the immunity call, or testing the timer first, changes how far
   the stream has advanced when the next roll in the battle takes its value.

   Unlike fdps_unit_inflict_random_ailments this one refuses a slot that is
   already counting down rather than refreshing it, and it credits experience:
   ADD dword ptr [0x00069cec],EDX at 00029060, ten times the target's level and
   an accumulate rather than an assignment. */
int fdps_unit_apply_status_effect(int effect_id, int unit_index)
{
    struct fdps_unit_record *unit;
    struct fdps_spell_effect *spell;
    int timer_slot;
    int hit_rate;
    int landed;

    /* Zeroed at 00028f7c, before the record lookup rather than after it. */
    landed = 0;
    unit = fdps_get_unit_record(unit_index);

    if (effect_id == SPELL_ID_SEAL) {
        timer_slot = SEAL_TIMER_SLOT;
    } else if (effect_id == SPELL_ID_POISON) {
        timer_slot = POISON_TIMER_SLOT;
    } else if (effect_id == SPELL_ID_PARALYSIS) {
        timer_slot = PARALYSIS_TIMER_SLOT;
    } else {
        /* ADD EAX,0x22 on the id itself, and status_timers is the field at
           +0x22, so the id arrives already being the slot index. */
        timer_slot = effect_id;
        effect_id = SPELL_ID_BLESSING;
    }

    spell = fdps_get_spell_record(effect_id);

    /* XOR EAX,EAX / MOV AL,byte ptr [EDX+0x2] at 00028fe3: the hit rate widens
       UNSIGNED into an int, and the comparison that follows is the signed JGE
       at 00028fff on the remainder.  A rate of 0 therefore refuses everything
       and no rate can be read as negative. */
    hit_rate = spell->hit_rate;

    /* Both divides are the signed IDIV with the dividend sign extended by SAR
       EDX,0x1f, and both remainders are taken from EDX. */
    if (rand() % PERCENT < hit_rate &&
        unit->status_timers[timer_slot] == 0 &&
        (effect_id < SPELL_ID_SEAL || effect_id > SPELL_ID_PARALYSIS ||
         fdps_unit_is_ailment_immune(unit_index) == 0)) {
        unit->status_timers[timer_slot] =
            (unsigned char) (rand() % AILMENT_TURNS_SPREAD +
                             AILMENT_TURNS_BASE);
        landed = 1;

        /* XOR EDX,EDX is absent here -- MOV DL,byte ptr [EDX+0x21] followed by
           AND EDX,0xff -- so the level is still a zero-extended byte. */
        data_fdps_battle_pending_xp_credit +=
            unit->level * STATUS_EXP_PER_LEVEL;
    }

    return landed;
}

/* CMP dword ptr [EBP-0xc],0x19 / JZ at 000290b1 -- an equality of its own,
   reached before either span is tried.  0x19 is 機兵 and 0x1a 魔神 is the
   class immediately above it, which this equality deliberately leaves out. */
#define IMMUNE_CLASS_MACHINE_SOLDIER 0x19

/* CMP 0x21 / JL then CMP 0x22 / JLE at 000290b7: 守護獸 and 將軍. */
#define FIRST_IMMUNE_CLASS_LOWER_SPAN 0x21
#define LAST_IMMUNE_CLASS_LOWER_SPAN 0x22

/* CMP 0x24 / JL then CMP 0x26 / JLE at 000290c7: ？？, 惡靈 and 活屍.  The
   span stops at 0x26 and PROMAP.DAT carries a further class 0x27 whose contents
   copy 0x24's, so that one is not immune either (assets/classes.md). */
#define FIRST_IMMUNE_CLASS_UPPER_SPAN 0x24
#define LAST_IMMUNE_CLASS_UPPER_SPAN 0x26

/* CMP 0x3c / JL then CMP 0x44 / JLE at 000290d7, and this pair is on the
   PORTRAIT ID at +0x7 rather than on the class at +0x20.  Portrait ids from
   0x3c up index ENEMYDAT.DAT as id - 0x3c, so the span is that table's first
   nine records. */
#define FIRST_IMMUNE_ENEMY_PORTRAIT_ID 0x3c
#define LAST_IMMUNE_ENEMY_PORTRAIT_ID 0x44

/* 00029080.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP,
   SUB ESP,0x10, the single argument read from [EBP+0x14], and all four call
   sites -- 0001a194 and 0001a1e2 in fdps_combat_compute_hit_outcome, 00028f2e in
   fdps_unit_inflict_random_ailments, 00029020 in fdps_unit_apply_status_effect
   -- doing ADD ESP,0x4 after the CALL and then TEST EAX,EAX.

   Two things about the shape are load-bearing.

   The accepted class ids are NOT one range.  The assembly tests 0x19 on its own,
   then 0x21..0x22, then 0x24..0x26, so 0x23 (傭兵) falls through all three and
   is not immune even though it sits between the last two.  Folding them into
   0x21..0x26, or into 0x19..0x26, hands mercenaries immunity to poison,
   paralysis and 封魔咒術 that the original does not give them.

   The fourth test is on a different field.  0x3c..0x44 is compared against the
   portrait id at +0x7, not against the class at +0x20, so it is an alternative
   route to immunity for the first nine ENEMYDAT.DAT records whatever class they
   carry -- and conversely a roster unit with an immune class is immune with a
   portrait id nowhere near that span.

   Both bytes are loaded XOR EAX,EAX / MOV AL and widened into int locals before
   any comparison runs, so both reads happen whichever way the tests go.  The
   comparisons that follow are the signed JL/JLE, but a zero-extended byte is
   never negative and every bound is positive, so nothing here can tell the two
   widenings apart; the zero extension is stated by the field types rather than
   demonstrated by behaviour. */
int fdps_unit_is_ailment_immune(int unit_index)
{
    struct fdps_unit_record *unit;
    int class_code;
    int portrait_id;
    int is_immune;

    unit = fdps_get_unit_record(unit_index);

    class_code = unit->clazz;
    portrait_id = unit->portrait_id;

    if (class_code == IMMUNE_CLASS_MACHINE_SOLDIER ||
        (class_code >= FIRST_IMMUNE_CLASS_LOWER_SPAN &&
         class_code <= LAST_IMMUNE_CLASS_LOWER_SPAN) ||
        (class_code >= FIRST_IMMUNE_CLASS_UPPER_SPAN &&
         class_code <= LAST_IMMUNE_CLASS_UPPER_SPAN) ||
        (portrait_id >= FIRST_IMMUNE_ENEMY_PORTRAIT_ID &&
         portrait_id <= LAST_IMMUNE_ENEMY_PORTRAIT_ID)) {
        is_immune = 1;
    } else {
        is_immune = 0;
    }

    return is_immune;
}
