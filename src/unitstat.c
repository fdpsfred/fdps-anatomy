/* unitstat.c -- what a battle effect does to one unit's own numbers: HP and
 * MP, the status effects running on it, and the experience those credit.
 *
 * See unitstat.h for what a caller has to know.  The file owns no state: it
 * reaches every record through fdps_get_unit_record (unit.h) and the only
 * global it writes is data_fdps_battle_pending_xp_credit, which gamedata.h
 * declares and gamedata.c defines.
 *
 * rand comes from <stdlib.h>; it is a real call in the original -- CALL
 * 00042cf8 -- and not an inline expansion.
 */
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
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
