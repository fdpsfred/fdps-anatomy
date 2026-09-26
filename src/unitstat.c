/* unitstat.c -- what a battle effect does to one unit's own numbers: HP and
 * MP, the status effects running on it, and the experience those credit.
 *
 * See unitstat.h for what a caller has to know.  The file owns no state: it
 * reaches every unit record through fdps_get_unit_record (unit.h) and every
 * ENEMYDAT.DAT record through fdps_get_enemy_record (table.h), and every global
 * it writes -- data_fdps_battle_pending_xp_credit, the figure scratch
 * data_fdps_dialog_last_action_value_param and the cursor mode
 * data_fdps_map_cursor_draw_mode -- is one gamedata.h declares and gamedata.c
 * defines.
 *
 * rand comes from <stdlib.h>; it is a real call in the original -- CALL
 * 00042cf8 -- and not an inline expansion.  memset and the port read inp are
 * the only other library calls, both reached by fdps_unit_award_exp_and_level_up
 * near the bottom of the file.  That routine and fdps_battle_tick_status_effects
 * after it are the two here that draw, pace themselves on the timer and block;
 * the tick does all of it through callees and makes no library call itself.
 */
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "anim.h"
#include "audio.h"
#include "blit.h"
#include "chapter.h"
#include "death.h"
#include "indicat.h"
#include "keybd.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "sprite.h"
#include "table.h"
#include "text.h"
#include "unit.h"
#include "vfs.h"
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

/* PUSH 0xa4 at 0001e3c5.  164 is the row pitch of the level-up window the
   caller built, and the five dst offsets it forms confirm it: 0xad3, 0x1097,
   0x165b, 0x10ce and 0x1692 are 16*164+147, 25*164+147, 34*164+147, 26*164+38
   and 35*164+38, so every one of them is a whole number of rows plus one of two
   columns. */
#define LEVEL_UP_WINDOW_PITCH 0xa4

/* PUSH 0x0 at 0001e3bd: fdps_draw_number's digit_count, whose zero is the
   natural-width mode rather than a field of no digits (see text.h). */
#define GAIN_FIELD_NATURAL_WIDTH 0

/* XOR EAX,EAX / PUSH EAX at 0001e3ba: show_plus is 0, so a level-up gain is
   painted without the leading '+' the same routine can draw. */
#define GAIN_NO_LEADING_PLUS 0

/* 0001e370.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP,
   SUB ESP,0x8, the three arguments read from [EBP+0x14], [EBP+0x18] and
   [EBP+0x1c] behind the four saves and the return address, and a RET carrying no
   immediate.  fdps_unit_award_exp_and_level_up is the only caller and every one
   of its five call sites pushes three dwords right to left and does ADD ESP,0xc
   afterwards, so the caller cleans.  Nothing reads EAX after the CALL and the
   function returns nothing.

   Both bytes of the pair widen UNSIGNED.  The minimum is XOR EAX,EAX / MOV
   AL,byte ptr [EDX] at 0001e37c and the bound is MOV AL,byte ptr [EAX+0x1] /
   AND EAX,0xff at 0001e389, so a byte of 0xc8 is 200 and never -56.  The
   subtraction that follows and the divide are both signed -- SUB EAX,dword ptr
   [EBP-0x8] then IDIV -- which is what an inverted pair would show, but the two
   widenings are what decide the ordinary numbers.

   THE ZERO TEST IN FRONT OF THE DIVIDE IS NOT DEFENSIVE PADDING.  CMP dword ptr
   [EBP-0x4],0x0 / JZ at 0001e397 skips the CALL to rand() as well as the IDIV,
   so a pair whose two bytes are equal gains exactly the minimum AND DRAWS NO
   NUMBER OUT OF THE SHARED rand() STREAM.  Of the 175 pairs in the 35
   FRILEVUP.DAT rows that hold any growth, 26 are equal -- 15 of the 155 pairs
   of the forms the party levels through, 00-0b and 0f-21, mostly the DX pair --
   so both halves of that are ordinary: dropping the test divides by zero,
   and turning it into a rand() the result of which is thrown away shifts every
   later roll in the battle by one draw.

   The bound is EXCLUSIVE.  The remainder of a non-negative rand() is 0 through
   range - 1, so when the two bytes differ the largest gain a pair can give is
   growth_pair[1] - 1; an equal pair gains exactly that figure.  The growth
   table in assets/characters.md transcribes the file's two bytes as they
   stand, so for a pair whose bytes differ the upper figure printed there is
   one MORE than any level-up can actually roll -- 蘭迪斯's AP row of 4-6 gains
   4 or 5 and never 6 (rebuild_info/pitfalls.md).

   The figure travels through the global rather than through a register: MOV
   [0x00064038],EAX, then PUSH dword ptr [0x00064038] as fdps_draw_number's value
   argument, then MOV DX,word ptr [0x00064038] for the addition.  Folding it into
   a local would be invisible here -- nothing between the store and the reload
   writes it -- but the store is what the original leaves behind for whatever
   reads that global next, so it stays a global (see gamedata.h).

   The last step is sixteen bits wide: ADD word ptr [EAX],DX at 0001e3e0 with DX
   the LOW WORD of the dword global, so a stat field that overflows wraps inside
   its own two bytes and nothing carries into the field above it.  Nothing clamps
   the sum and nothing checks a cap. */
void fdps_level_up_apply_stat_gain(short *stat, unsigned char *growth_pair,
                                   unsigned char *dst)
{
    int min_gain;
    int gain_range;
    int gain_offset;

    min_gain = growth_pair[0];
    gain_range = (int) growth_pair[1] - min_gain;

    /* The slot the range was computed into is the slot the offset is left in,
       so a range of 0 reaches the addition below as an offset of 0. */
    gain_offset = 0;
    if (gain_range != 0) {
        gain_offset = rand() % gain_range;
    }

    data_fdps_dialog_last_action_value_param = min_gain + gain_offset;

    fdps_draw_number(dst, LEVEL_UP_WINDOW_PITCH,
                     data_fdps_dialog_last_action_value_param,
                     GAIN_FIELD_NATURAL_WIDTH, GAIN_NO_LEADING_PLUS);

    *stat = (short) (*stat +
                     (short) data_fdps_dialog_last_action_value_param);
}
/* CMP dword ptr [EBP-0x20],0x9 at 0001dd7f and again at 0001e2f4: portrait id 9
   is the machine soldier 蓋亞, the one character the routine lets climb past
   40.  CMP dword ptr [EBP-0x24],0x63 and CMP dword ptr [EBP-0x24],0x28 are the
   two caps those two branches compare the level against. */
#define MACHINE_SOLDIER_PORTRAIT_ID 9
#define MACHINE_SOLDIER_LEVEL_CAP 0x63
#define ORDINARY_LEVEL_CAP 0x28

/* CMP dword ptr [0x00069cec],0x63 / JLE at 0001ddb1: the award is written back
   down to 99 before anything is paid with it, so one call can never carry a
   unit through more than one level.  CMP dword ptr [EBP-0x10],0x64 / JL at
   0001dfb9 and ADD dword ptr [EBP-0x10],-0x64 at 0001e0cf are the level's
   price and what is taken off for it, both signed. */
#define XP_CREDIT_CAP 0x63
#define XP_PER_LEVEL 0x64

/* The map tile in pixels, IMUL EAX,EAX,0x18 at 0001de0a and 0001de24, and the
   offset the label is parked at inside the unit's cell -- ADD EAX,0x18 on the
   column and ADD EAX,0x20 on the row, which puts the figure half a tile right
   of the unit's left edge and one tile plus eight scanlines below its top. */
#define MAP_TILE_PIXELS 0x18
#define LABEL_ORIGIN_X 0x18
#define LABEL_ORIGIN_Y 0x20

/* The floating label's own surface: 40 x 8 at pitch 0x28, the 0x140 bytes
   PUSH 0x140 / CALL malloc at 0001de33 buys and memset clears.  Sprite 0x23 of
   the command sheet is the plate it is drawn on (PUSH 0x23 at 0001de64) and
   the figure goes in at byte 0x12, which is column 18 of row 0. */
#define LABEL_PITCH 0x28
#define LABEL_ROWS 8
#define LABEL_BYTES 0x140
#define LABEL_PLATE_SPRITE 0x23
#define LABEL_NUMBER_AT 0x12

/* The ramp the label rises on: MOV dword ptr [EBP-0x8],0x6 at 0001de90, CMP
   ...,0x24 / JL at 0001de97 and ADD ...,0x2 at 0001dea2 -- 6, 8 ... 0x22, so
   fifteen frames -- with CMP dword ptr [EBP-0x4],0x18 / JLE at 0001deae
   holding the last five of them at 24 pixels.  The blend weight is the held
   rise over 3 plus 8 (MOV EBX,0x3 / IDIV / ADD EAX,0x8 at 0001ded7), which
   runs 10, 10, 11 ... 16: every frame is past the blender's own fold at 8, so
   the figure is drawn over the scene rather than under it. */
#define LABEL_RISE_FIRST 6
#define LABEL_RISE_LIMIT 0x24
#define LABEL_RISE_STEP 2
#define LABEL_RISE_HELD_AT 0x18
#define LABEL_ALPHA_DIVISOR 3
#define LABEL_ALPHA_BASE 8

/* The offscreen page a frame is composed on, PUSH 0x15180 / CALL malloc at
   0001debb and 0001e1bb: 360 x 240 at pitch 0x168, with a 24-pixel apron on
   all four sides.  It is not cleared and malloc's answer is not tested. */
#define SCENE_PAGE_PITCH 0x168
#define SCENE_PAGE_BYTES 0x15180

/* The window handed to the adapter and where it lands: 312 x 192 taken from
   page byte 0x21d8, page pixel (24,24), and put down at screen byte 0x504,
   screen pixel (4,4).  Both are literals in the original -- PUSH 0xa0504 at
   0001df6e and 0001e2ac -- and stay literals here, because 0xa0000 is where
   the display adapter answers and not the address of anything the linker
   places. */
#define SCENE_PAGE_WINDOW_AT 0x21d8
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VIEW_WINDOW_AT 0x504
#define VIEW_WINDOW_W 0x138
#define VIEW_WINDOW_H 0xc0

/* VGA input status register 1.  Bit 3 is set while the vertical retrace runs,
   and every frame straddles one whole retrace: spin until it starts, spin
   until it ends. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* Levup.wav out of MISC.VFS, MOV EAX,0x61778 / MOV EAX,0x60128 at 0001dfc3.
   Both have to be plain writable literals: the container lookup upper-cases the
   caller's own storage in place (vfs.h), so these spellings are permanently
   folded after the first level-up of the run, exactly as the original's copies
   at 0x60128 and 0x61778 are (rebuild_info/pitfalls.md).

   PUSH -0x1 / PUSH -0x1 / PUSH 0x1 at 0001dfda: played once, at the rate its
   own .WAV header states and at the sound path's default volume. */
#define LEVEL_UP_ARCHIVE "MISC.VFS"
#define LEVEL_UP_SOUND "Levup.wav"
#define LEVEL_UP_SOUND_LOOPS 1
#define LEVEL_UP_RATE_FROM_HEADER (-1)
#define LEVEL_UP_VOLUME_FROM_DEFAULT (-1)

/* The level-up window: sprite 0 of the level-up sheet painted into a 164 x 66
   buffer of 0x2a48 bytes (PUSH 0x2a48 at 0001dffa), and the seven places inside
   it the routine writes.  Every one is a byte displacement added to the buffer
   in the original -- 0xa66 at 0001e100 for the new level, 0x1d1c at 0001e0a0
   for a learned spell's name, and 0xad3, 0x1097, 0x165b, 0x10ce and 0x1692 for
   the five stat gains, in the order AP, DP, DX, HP, MP. */
#define LEVEL_UP_WINDOW_ROWS 0x42
#define LEVEL_UP_WINDOW_BYTES 0x2a48
#define LEVEL_UP_WINDOW_SPRITE 0
#define LEVEL_UP_LEVEL_AT 0xa66
#define LEVEL_UP_SPELL_NAME_AT 0x1d1c
#define LEVEL_UP_AP_AT 0xad3
#define LEVEL_UP_DP_AT 0x1097
#define LEVEL_UP_DX_AT 0x165b
#define LEVEL_UP_HP_AT 0x10ce
#define LEVEL_UP_MP_AT 0x1692

/* Where the window is blitted into the composed page, PUSH page + 0x7c22 at
   0001e1e6: byte 0x7c22 of a 360-pitch page is page pixel (98, 88). */
#define LEVEL_UP_WINDOW_ON_PAGE_AT 0x7c22

/* And where the unit's own walk sprite goes, page byte 0x8eb0 at 0001e269:
   page pixel (168, 101), a 24 x 24 cell drawn opaque. */
#define LEVEL_UP_WALK_SPRITE_AT 0x8eb0
#define WALK_SPRITE_SIZE 0x18
#define BLIT_MODE_OPAQUE 0

/* The walk cycle the sprite is animated on: the tick counter modulo 16 divided
   by 4 (MOV EBX,0x10 / IDIV then the SAR/SHL/SBB/SAR divide by 4 at 0001e1fd),
   with a 3 folded back to 1 at 0001e21d so the four quarters run 0, 1, 2, 1.
   Both divides are SIGNED, which is what makes the tick local an int.

   The cache entry is the unit's own slot: MOV AL,byte ptr [EAX+0x2] /
   IMUL EAX,EAX,0x30 at 0001e22a puts the walk group 0x30 bytes per slot into
   the table data_fdps_cel_sprite_cache_ptr heads, and the frame picks one of
   its dwords.  That table starts at the base rather than at a .CEL file's
   +0x0f, because the cache is a table fdps_cache_cel_sprite_group builds and
   not a loaded sheet (mapdraw.c says the same). */
#define WALK_CYCLE_TICKS 0x10
#define WALK_TICKS_PER_FRAME 4
#define WALK_FRAME_FOLDED 3
#define WALK_FRAME_FOLDS_TO 1
#define UNIT_SPRITE_CACHE_SLOT_BYTES 0x30
#define CEL_SUB_IMAGE_ENTRY_BYTES 4

/* MOV dword ptr [EBP-0x4],0xfa at 0001e1a1: the window is held for up to 250
   frames.  AND EAX,0xff / CMP EAX,0x7f / JLE at 0001e1ad ends it early on the
   first make code -- anything 0x7f or below -- out of the keyboard ring. */
#define LEVEL_UP_HOLD_FRAMES 0xfa
#define KEY_MAKE_CODE_MAX 0x7f

/* The six {level, spell id} pairs of a GETMGTAB.DAT record and the two bytes
   each takes; CMP dword ptr [EBP-0x2c],0xff / JZ at 0001e036 is the growth
   record's "this character learns nothing" mark. */
#define SPELL_LEARN_PAIRS 6
#define SPELL_LEARN_PAIR_BYTES 2
#define SPELL_LEARN_NONE 0xff

/* A spell's name is text entry spell id + 0x1be of the block
   data_fdps_all_game_text_ptr heads (ADD EAX,0x1be at 0001e0a9), drawn in the
   three colours PUSH 0x6d / PUSH 0x0 / PUSH 0xd0 at 0001e08f. */
#define SPELL_NAME_TEXT_BASE 0x1be
#define SPELL_NAME_FG_COLOR 0xd0
#define SPELL_NAME_BG_COLOR 0
#define SPELL_NAME_OUTLINE_COLOR 0x6d

/* The two draw modes the routine leaves in data_fdps_map_cursor_draw_mode: 0
   while the animation runs, so fdps_draw_map_cursor paints nothing over it,
   and 1 -- the plain cursor -- on the way out. */
#define MAP_CURSOR_HIDDEN 0
#define MAP_CURSOR_PLAIN 1

/* 0001dd30.  One stack argument at [EBP+0x14], behind the four saves of the
   -4s prologue (PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x48) and the
   return address, and a RET carrying no immediate.  All four call sites --
   00013022, 00015fdf, 000280f2 and 00021d6a -- push one dword, CALL, then
   ADD ESP,0x4, so the caller cleans; none of them reads EAX afterwards and the
   function returns nothing.

   EVERY BYTE THIS READS OUT OF A RECORD WIDENS UNSIGNED.  The level and the
   portrait id come in as XOR EAX,EAX / MOV AL,byte ptr at 0001dd4b and
   0001dd56, the leftover experience and the two tile coordinates as
   MOV AL / AND EAX,0xff at 0001dded, 0001de03 and 0001de1c, and the walk group
   the same way at 0001e22d.  A tile column of 200 is therefore 200 and never
   -56.  What the routine then does with those ints is signed throughout: JLE
   on the award clamp, JL on the level price, and the two IDIVs of the walk
   cycle.

   THE TICK LOCAL IS DELIBERATELY LEFT UNINITIALISED, exactly as the original
   leaves [EBP-0xc].  The first spin of each of the two frame loops therefore
   falls through on whatever the frame held, and only the frames after it are
   actually paced.  data_fdps_timer_tick_counter is volatile (gamedata.h) or
   the spin has no exit at all.

   THE THREE ENTRY GATES DO NOT CLEAR THE ACCUMULATOR and the post-level-up
   test does not agree with the entry cap -- see unitstat.h and
   rebuild_info/pitfalls.md.  Both are the original's behaviour and both are
   left as they stand.

   The label buffer and the level-up window share the original's [EBP-0x34]
   slot, and the rise and the hold counter share [EBP-0x4]; they are separate
   locals here because they hold separate things, and which slot a local lands
   in is register allocation (ADR-0001). */
void fdps_unit_award_exp_and_level_up(int unit_index)
{
    /* The awarded unit's record, resolved twice: once before the gates and
       again after the cursor has been parked, exactly as the original does. */
    struct fdps_unit_record *unit_record;
    /* Its level and portrait id as they stood on entry; the level slot is
       re-read after the increment and carries the NEW level from there on. */
    int unit_level;
    int unit_portrait_id;
    /* The unit's FRILEVUP.DAT growth record. */
    struct fdps_character_growth *growth;
    /* The award plus the leftover the record already carried, which is what
       buys the level and what is stored back afterwards. */
    int running_exp;
    /* Where the floating figure sits on the composed page. */
    int label_x;
    int label_y;
    /* The 40x8 surface the figure is built on. */
    unsigned char *label_buf;
    /* How far the figure has climbed this frame, and that height held at 24. */
    int rise;
    int rise_held;
    /* The page one frame is composed on. */
    unsigned char *scene_page;
    /* The tick each frame loop waits to see change.  Not initialised, for the
       reason above. */
    int last_tick;
    /* The 164x66 level-up window, the loaded Levup.wav behind it, and the
       sample slot fdps_audio_start_wav answered with. */
    unsigned char *window_buf;
    void *level_up_sound;
    int level_up_sample;
    /* The growth record's GETMGTAB.DAT index, that record, and one pair of
       it. */
    int spell_learn_index;
    struct fdps_spell_learning_record *spell_learn_record;
    unsigned char *learn_pair;
    int pair_index;
    int spell_id;
    /* How many frames the window still has to stand. */
    int hold_frames;
    /* Which quarter of the walk cycle is showing, and the stream it selects. */
    int walk_frame;
    unsigned char *walk_sprite_stream;

    unit_record = fdps_get_unit_record(unit_index);
    unit_level = unit_record->level;
    unit_portrait_id = unit_record->portrait_id;

    if (data_fdps_battle_pending_xp_credit == 0
        || fdps_unit_is_retired(unit_index) != 0) {
        return;
    }
    if (unit_portrait_id == MACHINE_SOLDIER_PORTRAIT_ID) {
        if (unit_level == MACHINE_SOLDIER_LEVEL_CAP) {
            return;
        }
    } else if (unit_level == ORDINARY_LEVEL_CAP) {
        return;
    }

    data_fdps_map_cursor_draw_mode = MAP_CURSOR_HIDDEN;
    fdps_map_cursor_move_to_unit(unit_index);
    if (data_fdps_battle_pending_xp_credit > XP_CREDIT_CAP) {
        data_fdps_battle_pending_xp_credit = XP_CREDIT_CAP;
    }

    unit_record = fdps_get_unit_record(unit_index);
    growth = fdps_get_growth_record(unit_record->portrait_id);
    running_exp = data_fdps_battle_pending_xp_credit + unit_record->exp_carry;

    label_x = (int) unit_record->pos_x * MAP_TILE_PIXELS
              - data_fdps_battle_view_window_origin_x + LABEL_ORIGIN_X;
    label_y = (int) unit_record->pos_y * MAP_TILE_PIXELS
              - data_fdps_battle_view_window_origin_y + LABEL_ORIGIN_Y;

    label_buf = (unsigned char *) malloc((size_t) LABEL_BYTES);
    memset(label_buf, 0, (size_t) LABEL_BYTES);
    fdps_cel_blit_sprite(data_fdps_command_sprite_sheet_ptr,
                         LABEL_PLATE_SPRITE, label_buf, LABEL_PITCH, 0, 0, 0,
                         BLIT_MODE_OPAQUE);
    fdps_draw_number(label_buf + LABEL_NUMBER_AT, LABEL_PITCH,
                     data_fdps_battle_pending_xp_credit,
                     GAIN_FIELD_NATURAL_WIDTH, GAIN_NO_LEADING_PLUS);

    for (rise = LABEL_RISE_FIRST; rise < LABEL_RISE_LIMIT;
         rise = rise + LABEL_RISE_STEP) {
        rise_held = rise;
        if (rise_held > LABEL_RISE_HELD_AT) {
            rise_held = LABEL_RISE_HELD_AT;
        }

        scene_page = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);
        fdps_draw_scene_layers(scene_page);
        fdps_blit_blend_transparent_rect(
            label_buf, LABEL_PITCH,
            scene_page + (label_y - rise_held) * SCENE_PAGE_PITCH + label_x,
            SCENE_PAGE_PITCH,
            scene_page + (label_y - rise_held) * SCENE_PAGE_PITCH + label_x,
            SCENE_PAGE_PITCH, LABEL_PITCH, LABEL_ROWS,
            data_fdps_palette_shade_ramp_table, data_fdps_inverse_palette_cube,
            rise_held / LABEL_ALPHA_DIVISOR + LABEL_ALPHA_BASE);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the copy starts clear of it. */
        }
        fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                       SCENE_PAGE_PITCH,
                       (void *) (VGA_SCREEN_BASE + VIEW_WINDOW_AT),
                       VGA_SCREEN_PITCH, VIEW_WINDOW_W, VIEW_WINDOW_H);
        while (last_tick == (int) data_fdps_timer_tick_counter) {
        }
        last_tick = (int) data_fdps_timer_tick_counter;
        free(scene_page);
    }
    free(label_buf);

    if (running_exp >= XP_PER_LEVEL) {
        level_up_sound = fdps_vfs_load_entry(LEVEL_UP_ARCHIVE,
                                             LEVEL_UP_SOUND);
        level_up_sample = fdps_audio_start_wav(level_up_sound,
                                               LEVEL_UP_SOUND_LOOPS,
                                               LEVEL_UP_RATE_FROM_HEADER,
                                               LEVEL_UP_VOLUME_FROM_DEFAULT);
        fdps_flush_keyboard_queue();
        unit_record->level++;

        window_buf = (unsigned char *) malloc((size_t) LEVEL_UP_WINDOW_BYTES);
        fdps_cel_blit_sprite(data_fdps_level_up_window_sheet_ptr,
                             LEVEL_UP_WINDOW_SPRITE, window_buf,
                             LEVEL_UP_WINDOW_PITCH, 0, 0, 0, BLIT_MODE_OPAQUE);

        spell_learn_index = growth->spell_learning_idx;
        if (spell_learn_index != SPELL_LEARN_NONE) {
            spell_learn_record =
                fdps_get_spell_learn_record(spell_learn_index);
            for (pair_index = 0; pair_index < SPELL_LEARN_PAIRS;
                 pair_index++) {
                learn_pair = (unsigned char *) spell_learn_record
                             + pair_index * SPELL_LEARN_PAIR_BYTES;
                if (unit_record->level == learn_pair[0]) {
                    spell_id = learn_pair[1];
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   spell_id + SPELL_NAME_TEXT_BASE,
                                   window_buf + LEVEL_UP_SPELL_NAME_AT,
                                   LEVEL_UP_WINDOW_PITCH, SPELL_NAME_FG_COLOR,
                                   SPELL_NAME_BG_COLOR,
                                   SPELL_NAME_OUTLINE_COLOR);
                    fdps_set_flag_bit(unit_index, spell_id);
                }
            }
        }

        running_exp = running_exp - XP_PER_LEVEL;
        unit_level = unit_record->level;
        fdps_draw_number(window_buf + LEVEL_UP_LEVEL_AT, LEVEL_UP_WINDOW_PITCH,
                         unit_level, GAIN_FIELD_NATURAL_WIDTH,
                         GAIN_NO_LEADING_PLUS);
        fdps_level_up_apply_stat_gain(&unit_record->ap_base, &growth->ap_min,
                                      window_buf + LEVEL_UP_AP_AT);
        fdps_level_up_apply_stat_gain(&unit_record->dp_base, &growth->dp_min,
                                      window_buf + LEVEL_UP_DP_AT);
        fdps_level_up_apply_stat_gain(&unit_record->dx_base, &growth->dx_min,
                                      window_buf + LEVEL_UP_DX_AT);
        fdps_level_up_apply_stat_gain(&unit_record->hp_max, &growth->hp_min,
                                      window_buf + LEVEL_UP_HP_AT);
        fdps_level_up_apply_stat_gain(&unit_record->mp_max, &growth->mp_min,
                                      window_buf + LEVEL_UP_MP_AT);
        fdps_unit_recompute_combat_stats(unit_index);

        hold_frames = LEVEL_UP_HOLD_FRAMES;
        do {
            if (fdps_read_keyboard_queue() <= KEY_MAKE_CODE_MAX) {
                break;
            }

            scene_page = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);
            fdps_draw_scene_layers(scene_page);
            fdps_blit_rect((unsigned int) window_buf, LEVEL_UP_WINDOW_PITCH,
                           scene_page + LEVEL_UP_WINDOW_ON_PAGE_AT,
                           SCENE_PAGE_PITCH, LEVEL_UP_WINDOW_PITCH,
                           LEVEL_UP_WINDOW_ROWS);

            walk_frame = last_tick % WALK_CYCLE_TICKS / WALK_TICKS_PER_FRAME;
            if (walk_frame == WALK_FRAME_FOLDED) {
                walk_frame = WALK_FRAME_FOLDS_TO;
            }
            walk_sprite_stream = data_fdps_cel_sprite_cache_ptr
                + *(int *) (data_fdps_cel_sprite_cache_ptr
                            + unit_record->sprite_cache_slot
                              * UNIT_SPRITE_CACHE_SLOT_BYTES
                            + walk_frame * CEL_SUB_IMAGE_ENTRY_BYTES);
            fdps_blit_dispatch(walk_sprite_stream,
                               scene_page + LEVEL_UP_WALK_SPRITE_AT,
                               WALK_SPRITE_SIZE, WALK_SPRITE_SIZE,
                               SCENE_PAGE_PITCH, 0, BLIT_MODE_OPAQUE);

            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   == 0) {
                /* Spin until the retrace begins. */
            }
            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   != 0) {
                /* And until it ends. */
            }
            fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                           SCENE_PAGE_PITCH,
                           (void *) (VGA_SCREEN_BASE + VIEW_WINDOW_AT),
                           VGA_SCREEN_PITCH, VIEW_WINDOW_W, VIEW_WINDOW_H);
            while (last_tick == (int) data_fdps_timer_tick_counter) {
            }
            last_tick = (int) data_fdps_timer_tick_counter;
            free(scene_page);
            hold_frames--;
        } while (hold_frames != 0);

        if (unit_portrait_id == MACHINE_SOLDIER_PORTRAIT_ID
            && unit_level == MACHINE_SOLDIER_LEVEL_CAP) {
            running_exp = 0;
        } else if (unit_level == ORDINARY_LEVEL_CAP) {
            running_exp = 0;
        }

        free(window_buf);
        while (fdps_audio_sample_is_playing(level_up_sample) != 0) {
        }
        free(level_up_sound);
    }

    unit_record->exp_carry = (unsigned char) running_exp;
    data_fdps_battle_pending_xp_credit = 0;
    data_fdps_map_cursor_draw_mode = MAP_CURSOR_PLAIN;
    fdps_flush_keyboard_queue();
}

/* The six status timers, record +0x22 .. +0x27: the three 神之祝福 buff slots
   fdps_unit_recompute_combat_stats reads, then poison, paralysis and 封魔咒術.
   CMP dword ptr [EBP-0x18],0x6 / JL at 0001fb8b bounds the countdown. */
#define STATUS_TIMER_SLOTS 6

/* Bit 0 of the flags byte at record +0x05, the one fdps_unit_is_retired
   tests: AND AL,0x1 at 0001fa8b and again at 0001fbb1, inlined here rather
   than called. */
#define UNIT_FLAG_RETIRED 0x01

/* Poison takes a tenth of the maximum HP: MOV EBX,0xa / IDIV EBX at
   0001fab3. */
#define POISON_DAMAGE_DIVISOR 10

/* The clip played over a poisoned unit, MOV EDX,0x617dc at 0001fb03, and the
   one unit it is played over, PUSH 0x1 at 0001fb0d.  The literal is handed
   straight to fdps_play_vfs_animation_over_units, which lets the VFS lookup
   upper-case it in place, exactly as the item and chapter callers' clip
   literals are. */
#define POISON_EFFECT_CLIP "PosEff.saf"
#define POISON_EFFECT_UNITS 1

/* The damage digit set in Number.cel, PUSH 0x0 at 0001fb1b -- the same glyph
   base every damage popup uses (indicat.h). */
#define POISON_GLYPH_BASE 0

/* 0001fa30.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP, SUB ESP,0x18, the one argument read from [EBP+0x14] and a bare RET.
   All three call sites in fdps_battle_advance_turn -- 0001e580, 0001e5c6 and
   0001e63c -- push one dword, CALL, then ADD ESP,0x4, so the caller cleans;
   none of them reads EAX, and nothing here sets it.

   Every CALL but one returns nothing the body reads.  The exception is
   fdps_get_unit_record at 0001fa5f and 0001fb79, whose EAX is stored straight
   into [EBP-0x14] and is the record pointer for the rest of that pass.

   THE FIRST SWEEP READS BOTH HP WORDS UNSIGNED.  XOR EAX,EAX / MOV AX,word ptr
   [EBX+0x40] and the same for +0x42, so a word driven negative by an earlier
   drain is a number near 65535 here: the floor at 0 (CMP / JGE at 0001fad0,
   the signed test on the 32-bit difference) never fires for it, and the
   subtraction leaves a smaller negative word standing.  The damage is
   published in data_fdps_dialog_last_action_value_param and READ BACK from it
   for both the subtraction and the popup (MOV EAX,[0x00064038] at 0001fac8,
   PUSH dword ptr [0x00064038] at 0001fb1d).

   THE CURSOR MODE IS NOT PUT BACK BY THE COUNTDOWN.  The poison arm sets
   data_fdps_map_cursor_draw_mode to 0 and back to 1 around its presentation,
   but the expiry arm at 0001fbdb stores 0 and nothing after it stores
   anything, so once any timer of the side runs out the map cursor stays
   undrawn until some later routine sets the mode.  The store looks pointless
   -- fdps_unit_recompute_combat_stats draws nothing -- and it is what the
   next phase looks like.

   THE COUNTDOWN TESTS THE UNIT ONCE PER TIMER, NOT ONCE PER UNIT.  The side
   and retired checks sit inside the six-slot loop at 0001fb9b, so they are
   re-read for every slot; the C keeps them there.  The poison timer is one of
   the six, so it is decremented AFTER its damage has been dealt in the same
   call, and a poison of one turn still hits once.

   Between the sweeps come the death settlement and the chapter's win/lose
   test, unconditionally: CALL 0x0001d6c0 at 0001fb3f, then CALL dword ptr
   [EAX + 0x6028c] indexed by the chapter id at 0001fb50.  No range check on
   the chapter id. */
void fdps_battle_tick_status_effects(int side)
{
    /* The record of the unit the sweep is on, fdps_get_unit_record's answer. */
    struct fdps_unit_record *unit;
    int unit_index;
    /* The poisoned unit's HP after the damage, before the floor at 0. */
    int remaining_hp;
    int max_hp;
    int timer_slot;
    /* The one-entry unit list the poison clip is played over. */
    unsigned char poisoned_unit_id;

    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if (unit->status_timers[POISON_TIMER_SLOT] != 0
            && (int) unit->side == side
            && (unit->flags & UNIT_FLAG_RETIRED) == 0) {
            remaining_hp = (int) (unsigned short) unit->hp_current;
            max_hp = (int) (unsigned short) unit->hp_max;
            data_fdps_dialog_last_action_value_param =
                max_hp / POISON_DAMAGE_DIVISOR;
            remaining_hp -= data_fdps_dialog_last_action_value_param;
            if (remaining_hp < 0) {
                remaining_hp = 0;
            }
            unit->hp_current = (short) remaining_hp;

            data_fdps_map_cursor_draw_mode = MAP_CURSOR_HIDDEN;
            fdps_map_cursor_move_to_unit(unit_index);
            poisoned_unit_id = (unsigned char) unit_index;
            fdps_play_vfs_animation_over_units(POISON_EFFECT_UNITS,
                                               &poisoned_unit_id,
                                               POISON_EFFECT_CLIP);
            fdps_show_number_indicator(
                data_fdps_dialog_last_action_value_param,
                POISON_GLYPH_BASE, unit_index);
            fdps_play_indicator_queue();
            data_fdps_map_cursor_draw_mode = MAP_CURSOR_PLAIN;
        }
    }

    fdps_play_death_animation_and_mark_dead();
    data_fdps_chapter_post_action_handler_table
        [data_fdps_chapter_current_chapter_id]();

    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        for (timer_slot = 0; timer_slot < STATUS_TIMER_SLOTS; timer_slot++) {
            if ((int) unit->side == side
                && (unit->flags & UNIT_FLAG_RETIRED) == 0
                && unit->status_timers[timer_slot] != 0) {
                unit->status_timers[timer_slot]--;
                if (unit->status_timers[timer_slot] == 0) {
                    data_fdps_map_cursor_draw_mode = MAP_CURSOR_HIDDEN;
                    fdps_unit_recompute_combat_stats(unit_index);
                }
            }
        }
    }
}
