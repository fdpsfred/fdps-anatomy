/* spell.h -- applying a cast spell's effect, and the cast presentation.
 *
 * This is the effect side of casting: the caster, the target and the spell id
 * are already decided elsewhere, and what is left is to work out what the
 * spell does to one unit and to make it happen.  The presentation routines
 * that go with a cast live in the same file.
 *
 * spell.c owns no state.  Everything it works on it resolves on the spot --
 * the unit records through fdps_get_unit_record (unit.h), the class record
 * through fdps_get_class_record (table.h) and the spell record through
 * fdps_get_spell_record (table.h) -- so a call made after the unit array has
 * moved sees the array as it is at that moment.
 */
#ifndef SPELL_H
#define SPELL_H

/* Damages one battle unit with one spell: rolls the spell's hit rate and, when
   it lands, works out what that spell does to that target and takes it off the
   target's HP.  Returns the damage that was rolled, or 0.

   The signed power word of the spell record picks between two formulas.  A
   power of zero or more is a flat magic figure: damage is power * (100 minus
   the target class's magic resistance) / 100, so neither the caster's stats
   nor the target's defence enters it and the same spell hits for the same
   amount whoever casts it.  A negative power is the negative of an attack
   multiplier in percent -- -250 meaning 2.50x -- and damage is then the
   caster's derived attack times that multiplier, less the target's derived
   defence, floored at 0.

   The hit rate is a percentage drawn against rand() % 100.  A draw that does
   not land returns 0 with the target untouched.  On a draw that lands, the two
   ground-shock spells 裂地術 and 封神裂震 also return 0 against a target
   fdps_unit_is_flying (unit.h) calls airborne; every other case pays the
   figure through fdps_unit_apply_damage (unitstat.h), which rolls the final
   90.0%-99.9% of it into the target's HP word and credits the battle
   experience.  So a 0 return covers three different things -- a failed hit
   roll, a flying target under one of those two spells, and a computed damage
   floored at 0 -- and a caller that distinguishes a hit from a miss by the
   return value is reading all three as a miss.

   The HP write is made here and is real.  fdps_combat_play_spell_on_targets
   takes the target's HP word before the call and puts the old value back
   afterwards so its animation can interpolate the drop; the write has to
   happen for that to have two endpoints to work between.

   caster_unit_index is read only on the negative-power path, and only for the
   attack word.  Neither unit index is range checked at either end and the
   bound is the caller's; spell_id is a MAGICDAT.DAT record index, 0..0x27. */
extern int fdps_spell_damage_unit(int caster_unit_index, int target_unit_index,
                                  int spell_id);
#pragma aux fdps_spell_damage_unit "*" parm caller [];

/* Heals one battle unit by the power word of one spell record, and returns
   what fdps_unit_apply_heal (unitstat.h) returned: the heal it rolled, which
   is 90.0%-99.9% of the power and is NOT the HP actually restored -- a unit
   near its maximum gains less than the number that comes back.

   The whole of the function is that forward.  There is no hit roll against the
   record's hit rate, no MP charged for the cast, and no check of which side
   the record says the spell is aimed at, so a caller that wants any of those
   has to do them itself.  The heal is unconditional and lands on whatever unit
   index it is given.

   The power word is signed and is passed through unchanged.  A spell whose
   power is negative -- the eight special attacks store an attack-power
   multiplier there as a negative percentage -- therefore takes HP off the unit
   rather than putting it on, and fdps_unit_apply_heal has no lower clamp, so
   the HP can go through zero.

   unit_index is not range checked at either end and the bound is the caller's;
   spell_id is a MAGICDAT.DAT record index, 0..0x27.

   Nothing in the shipped image calls this.  Both live heal paths --
   fdps_cast_spell_on_targets for a cast and fdps_apply_heal_to_targets for an
   item -- call fdps_unit_apply_heal directly instead. */
extern int fdps_spell_heal_unit(int unit_index, int spell_id);
#pragma aux fdps_spell_heal_unit "*" parm caller [];

#endif
