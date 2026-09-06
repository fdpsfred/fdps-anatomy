/* cmbspell.h -- the full-screen combat presentation of a spell.
 *
 * The counterpart of src/combat.c's attack exchange: where that one plays a
 * physical blow on the fight screen, this one plays a spell on it and drains
 * every target's HP gauge across the hit frames of the spell's own animation.
 * The map-side alternative is fdps_cast_spell_on_targets (src/spell.h), which
 * this function hands the spells that have no fight-screen presentation of
 * their own straight over to.
 */
#ifndef CMBSPELL_H
#define CMBSPELL_H

/* Plays one spell on the full-screen combat animation and applies its damage
   to every target in turn.

   THE SPELLS THAT HAVE NO COMBAT-SCREEN PRESENTATION ARE HANDED STRAIGHT ON.
   Ids 0x0a, 0x0b, 0x0e-0x16, 0x18 and 0x21 -- the two ground shocks, the three
   heals, the seal, the two ailments, the blessing, the teleport, the haste,
   the revival and the requiem -- go to fdps_cast_spell_on_targets with the
   same four arguments and nothing else happens here.  Every other id is set up
   and played on the fight screen.

   `caster_unit_index` is the casting unit's place in the map unit array; its
   side byte picks the clip-name prefix ("E" for the enemy side, "M" for the
   other two) and the sign of the knockback, so a struck target is always
   thrown away from its caster, and its portrait id names the Stand and Magic
   clips.  `spell_id` is 0..39: it selects the handoff above, supplies the
   %02d of every effect clip name and indexes the recolour-band table a landed
   hit tints the target through.  `target_count` is how many entries of
   `target_unit_indices` are played out, in array order; it is NOT checked
   against the 40 clip slots the function has room for.

   THE FUNCTION OWNS THE FIGHT-MODE ENVIRONMENT AND PUTS IT BACK, exactly as
   fdps_combat_play_attack_exchange does (src/combat.h): FMer1.tmp and
   FMer2.tmp are read over data_fdps_palette_shade_ramp_table and
   data_fdps_inverse_palette_cube on the way in and Mer1.tmp and Mer2.tmp on
   the way out, the fight palette goes up and the map palette comes back, the
   two combat gauge sheets are built and freed, and the mode 13h screen is
   cleared before the return.  None of the four fopen results is tested, so a
   missing file faults inside fread.

   THE HP IS DRAINED, NOT SNAPPED.  fdps_spell_damage_unit has already taken
   the damage off the record when it returns; this routine reads the result,
   writes the pre-damage HP back and re-drains it one step per hit frame, so
   the gauge empties across the animation (rebuild_info/pitfalls.md).

   A main clip with no hit frame at all prints "ERROR: No Hit Point !!!" and
   ends the process at exit(1).  data_fdps_timer_tick_counter (src/gamedata.h)
   paces every frame, so a caller running this with no timer interrupt
   installed stops on the first frame that waits.  Nothing is returned. */
extern void fdps_combat_play_spell_on_targets(int caster_unit_index,
                                              int spell_id, int target_count,
                                              unsigned char *
                                                  target_unit_indices);
#pragma aux fdps_combat_play_spell_on_targets "*" parm caller [];

#endif
