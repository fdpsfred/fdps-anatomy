/* spell.c -- applying a cast spell's effect, and the cast presentation.
 *
 * See spell.h for what a caller has to know.  Nothing in this file owns state:
 * every record it works on is resolved on the spot through the accessors in
 * unit.h and table.h, so a call made after the unit array has moved sees the
 * array as it is at that moment.
 *
 * rand comes from <stdlib.h>.  It is a real call in the original -- CALL
 * 00042cf8 -- and not an inline expansion.
 */
#include <stdlib.h>
#include "fdpstype.h"
#include "unit.h"
#include "table.h"
#include "unitstat.h"
#include "spell.h"

/* Every ratio in here is a percentage: the magic resistance complement, the
   attack multiplier and the hit rate are all divided or drawn against 100.
   The literal is MOV EBX,0x64 before each IDIV. */
#define PERCENT 100

/* PROMAP.DAT row 0 is a default row, so the class record for a unit is looked
   up at its class code PLUS ONE.  Every caller of fdps_get_class_record in the
   image applies this bias (table.h); dropping it reads the previous class's
   magic resistance. */
#define CLASS_RECORD_BIAS 1

/* The two ground-shock spells, the only ids this function tests for.  Names
   from assets/spells.md, matching the defines in aiscore.c. */
#define SPELL_QUAKE 0x0a       /* 裂地術 */
#define SPELL_GREAT_QUAKE 0x0b /* 封神裂震 */

/* 00028320.  One spell landing on one unit.
 *
 * The three arguments arrive on the stack at [EBP+0x14], [EBP+0x18] and
 * [EBP+0x1c] and both call sites -- 0001ae5f and 00028e4f -- follow the CALL
 * with ADD ESP,0xc, so this is the stack convention with the caller cleaning
 * up, and the answer comes back in EAX (TEST EAX,EAX at 0001ae67).
 *
 * The two damage formulas are picked by JL at 0002837d on the spell record's
 * signed power word, MOVSX word ptr [EAX] at 00028373.  Both divides are
 * IDIV EBX with EBX 0x64 and EDX sign extended by SAR EDX,0x1f, so both are
 * signed, and the clamp on the second is JGE at 000283da -- signed as well.
 *
 * Three of the four record fields are byte loads preceded by XOR EAX,EAX, so
 * the class code at unit +0x20, the magic resistance complement at class
 * record +0x09 and the hit rate at spell record +0x02 are all zero extended
 * (0002833d, 00028358, 000283e8).  The two combat words are MOVSX: the
 * caster's attack at +0x48 (000283ac) and the target's defence at +0x4a
 * (000283c6).  struct fdps_unit_record and struct fdps_class_record carry
 * those signednesses, so the field types are the whole of it.
 *
 * The target's record pointer is taken once at 00028338 and the defence word
 * is read back off that same pointer at 000283c3, after the second
 * fdps_get_unit_record call for the caster -- the target is not re-resolved.
 *
 * Rebuild note: rand() is drawn at 000283ee, BEFORE the flying test at
 * 00028416, so a target that is immune to 裂地術 or 封神裂震 still consumes
 * one value from the PRNG.  Hoisting the immunity into a guard clause at the
 * top of the function -- the obvious shape for an immunity -- leaves that draw
 * untaken and shifts every later damage roll of the battle. */
int fdps_spell_damage_unit(int caster_unit_index, int target_unit_index,
                           int spell_id)
{
    struct fdps_unit_record *target;
    struct fdps_unit_record *caster;
    struct fdps_class_record *target_class;
    struct fdps_spell_effect *spell;
    int target_class_code;
    int magic_resist_complement;
    int spell_power;
    int attack_share;
    int target_defense;
    int damage;
    int hit_rate;

    target = fdps_get_unit_record(target_unit_index);
    target_class_code = target->clazz;
    target_class = fdps_get_class_record(target_class_code + CLASS_RECORD_BIAS);
    magic_resist_complement = target_class->magic_resist_complement;

    spell = fdps_get_spell_record(spell_id);
    spell_power = spell->power;

    if (spell_power >= 0) {
        /* A flat magic figure: the caster's record is never even resolved on
           this path. */
        damage = spell_power * magic_resist_complement / PERCENT;
    } else {
        /* NEG dword ptr [EBP + -0x10] at 00028397: the power is negated in
           place and the result is the multiplier in percent. */
        spell_power = -spell_power;
        caster = fdps_get_unit_record(caster_unit_index);
        attack_share = caster->ap * spell_power / PERCENT;
        target_defense = target->dp;
        damage = attack_share - target_defense;
        if (damage < 0) {
            damage = 0;
        }
    }

    hit_rate = spell->hit_rate;
    if (rand() % PERCENT < hit_rate) {
        if ((spell_id == SPELL_QUAKE || spell_id == SPELL_GREAT_QUAKE) &&
            fdps_unit_is_flying(target_unit_index) != 0) {
            return 0;
        }
        return fdps_unit_apply_damage(target_unit_index, damage);
    }

    return 0;
}
