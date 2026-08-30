/* combat.h -- the full-screen animated attack exchange.
 *
 * This is the cut-scene side of a physical attack: the screen the game brings
 * up when two units trade blows, the slide-in of the two combatants and the
 * arithmetic that decides what each blow of that exchange did.  The map-side
 * resolver that runs with the battle map still on screen is a different
 * function, fdps_unit_resolve_attack_hit in src/unitatk.c, and neither calls
 * the other.
 *
 * Both units are named by their index in the map unit array reached through
 * data_fdps_map_unit_array_ptr (src/gamedata.h); the record layout is struct
 * fdps_unit_record in src/fdpstype.h.
 */
#ifndef COMBAT_H
#define COMBAT_H

/* Works out what one blow of the animated exchange does and writes the
   numbers into the caller's outcome block.  Nothing is returned and the
   defender's HP is NOT touched -- that is the difference from the map-side
   resolver, which writes the new HP back into the record.  The animation
   drains the bar itself from the figures below.

   `outcome` is a block of six ints, all six of which this function writes
   before it does anything else:

     outcome[0]  non-zero when the blow missed.  Set to 1 on entry and
                 cleared only when the accuracy roll lands.
     outcome[1]  non-zero when the blow was critical.
     outcome[2]  written as 0 and read by nothing in the program.
     outcome[3]  written as 0 and read by nothing in the program.
     outcome[4]  written as 0 and read by nothing in the program.
     outcome[5]  the damage.  0 on a miss, and 0 on a landed blow whose
                 stat gap was too small to produce any.

   The only caller, fdps_combat_play_blow at 000196d0, passes a block of its
   own stack frame and discards the returned register.

   Two side effects reach outside that block.  The defender's poison or
   paralysis timer is written when the attacker's weapon carries that hit
   effect and the roll lands -- and that roll happens BEFORE the accuracy
   roll, so a blow that then misses can still inflict the ailment.  And a
   player-side unit striking a side-0 unit rewrites
   data_fdps_battle_pending_xp_credit with what the blow earned; any other
   pairing of sides leaves the previous action's figure standing.

   Neither index is range checked and neither is the enemy record index the
   experience path derives from the defender's portrait id. */
extern void fdps_combat_compute_hit_outcome(int attacker_unit_index,
                                            int defender_unit_index,
                                            int *outcome);
#pragma aux fdps_combat_compute_hit_outcome "*" parm caller [];

#endif
