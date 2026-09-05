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

/* Charges the MP cost of one action to the unit that performed it: subtracts
   the MP cost byte of MAGICDAT.DAT record spell_id from struct
   fdps_unit_record's mp_current at record offset 0x44 of unit unit_index, and
   writes the difference back as a word.  Nothing is returned and no other field
   of either record is read or written -- in particular the maximum MP at
   offset 0x46 is not.

   The cost byte is unsigned, so the whole 0..255 range is a cost, and the
   current MP is signed.  There is no affordability test and no floor: a cost
   larger than the unit's MP leaves the field negative, and a difference outside
   a signed word wraps into it rather than saturating.  A caller that wants a
   cast refused for want of MP has to refuse it before calling.

   The caller does this once per action, after the action's animation loop has
   finished.  Every action that gets here is a cast: the only caller is
   fdps_combat_play_spell_on_targets, which is itself reached only from
   fdps_battle_spell_command and fdps_map_actor_cast_chosen_spell, and no
   plain-attack path leads into it.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record (unit.h), so a
   call after the array has moved writes into the new block.  spell_id is a
   MAGICDAT.DAT record index, 0..0x27, and is not checked either. */
extern void fdps_spell_deduct_mp_cost(int unit_index, int spell_id);
#pragma aux fdps_spell_deduct_mp_cost "*" parm caller [];

/* Plays spell 11 封神裂震's full-screen cutscene and hands the screen back to
   the battle map through a white flash.  Takes nothing, returns nothing, and
   the only caller is fdps_cast_spell_on_targets, which reaches it when the
   spell id is 0x0b.

   The clip is MISC.VFS's Mag11.saf, loaded here and freed here, composed on a
   368 x 248 page this function also owns, and presented to the mode 13h
   aperture one frame per timer tick with each present straddling the vertical
   retrace.  The adapter must already be in that mode: nothing here sets it.

   The presentation is 44 ticks long and runs in two phases.  For 31 ticks the
   page's visible window is refilled from the picture that was on the adapter
   when the call was made and the clip is drawn over it translucently, at a
   blend level that steps 15 down to 0 -- two ticks to a level -- so the
   animation comes up out of the frozen screen.  For the remaining 13 the page
   is cleared to 0 first, so the rest of the clip plays on black, and the phase
   ends when the clip does.  The playback cursor is NOT reset between them: the
   clip's 22 single-tick frames are outrun by the fade-in, which wraps once and
   leaves the cursor on frame 9, so the whole clip is played through exactly
   twice with the seam inside the fade-in.

   Neither of the two sound effects Mag11.saf carries is played -- the flag
   handed to the composite drawer is 0 on every frame -- so a caller that wants
   this spell to be heard starts the sample itself.

   The screen is not left holding the animation.  On the way out the whole DAC
   is re-uploaded from data_fdps_vga_main_palette_ptr at the maximum bias,
   which clamps every entry to white, the aperture is blanked, and six frames
   of the live battle view are then rendered at biases 50, 40, 30, 20, 10 and
   0.  So the caller inherits a screen showing the map at its true palette, and
   six more timer ticks have gone by.

   The function reads data_fdps_palette_shade_ramp_table,
   data_fdps_inverse_palette_cube, data_fdps_vga_main_palette_ptr and
   data_fdps_timer_tick_counter (gamedata.h) and writes none of them. */
extern void fdps_play_spell_11_cutscene(void);
#pragma aux fdps_play_spell_11_cutscene "*" parm caller [];

#endif
