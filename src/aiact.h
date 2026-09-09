/* aiact.h -- the map AI's action layer: the three routines that carry out the
 * choice src/aiscore.c has already scored, once the behaviour dispatcher has
 * decided that choice is worth taking.
 *
 * Each of the three reads the winning search's globals back out of gamedata.h
 * -- the target, the tile and the bag slot or spell id -- so a caller that has
 * not first checked that search's score is handing them a stale decision.
 */
#ifndef AIACT_H
#define AIACT_H

/* 00012e50.  Walks the actor onto the tile the attack search picked and fights
   the unit it picked there: the blow, the counterblow, the deaths those caused
   and the experience they earned.  Always answers 1, and every call site
   discards it.

   Nothing about the fight is an argument.  The destination tile comes from
   data_fdps_battle_ai_best_physical_target_x and
   data_fdps_battle_ai_best_attack_tile_y and the unit to fight from
   data_fdps_battle_ai_best_physical_target_idx (gamedata.h), all three written
   by fdps_map_actor_score_best_attack (aiscore.h) and none of them touched
   here.

   The sequence is: hide the cursor, put it on the actor and walk the actor
   toward that tile with fdps_battle_move_unit_toward (movegrid.h); switch the
   cursor to the plain box, move it onto the target, turn the actor to face it
   and hold for a tenth of a second; clear the pending experience credit; play
   the fight; collect and run the death scripts of everyone it killed; and pay
   the experience.

   HOW THE FIGHT IS PLAYED IS data_fdps_ui_battle_animation_enabled's DECISION,
   read here as a byte against 0.  Clear, and the exchange happens on the map:
   one plain frame, the two HP gauges through fdps_battle_show_combat_gauges
   (gauge.h), the actor's swing, and -- only when the target survived it AND
   fdps_check_can_counter_attack (aitarget.h) answers exactly 1 -- both units
   turned to face each other and the counterblow played into the gauge call's
   second position pair.  Set, and the whole exchange is handed to
   fdps_combat_play_attack_exchange (combat.h), which runs its own
   counter-attack test, so no counterblow is driven from here.

   THE EXPERIENCE IS PAID TO THE TARGET AND NOT TO THE ACTOR.  The credit the
   blows accumulated in data_fdps_battle_pending_xp_credit is scaled by 15/10,
   signed and truncating, and handed to fdps_unit_award_exp_and_level_up
   (unitstat.h) for data_fdps_battle_ai_best_physical_target_idx.  That is how
   a player unit earns experience for surviving an enemy turn; an actor's own
   blow on a player unit earns nothing at all, because the credit is only
   written when a player unit strikes an enemy record.

   THE CURSOR IS LEFT IN THE BOX MODE ON PURPOSE.  Nothing here puts back the
   mode the caller had; fdps_map_actor_take_best_action clears it to 0 at
   00012e26 after this returns, and fdps_map_actor_behavior_step does not.

   unit_index is the acting unit's place in the map unit array.  side_select is
   that unit's side as a truth value and is only passed through, into
   fdps_battle_move_unit_toward, where it decides whose zones of control block
   the walk. */
extern int fdps_map_actor_move_and_attack(int unit_index, int side_select);
#pragma aux fdps_map_actor_move_and_attack "*" parm caller [];

/* 00013c90.  Casts data_fdps_map_ai_best_spell_id at the tile
   data_fdps_battle_ai_best_spell_target_x / _y name (gamedata.h), which is the
   cast fdps_map_actor_score_best_spell last scored.  Not emitted yet. */
extern int fdps_map_actor_cast_chosen_spell(int unit_index, int side_select);
#pragma aux fdps_map_actor_cast_chosen_spell "*" parm caller [];

/* 00027180.  Uses the actor's bag entry data_fdps_map_ai_best_item_bag_slot
   names on the tile data_fdps_map_ai_best_item_target_x /
   data_fdps_battle_ai_best_item_target_y name (gamedata.h), which is the use
   fdps_map_actor_score_best_item last scored.  Not emitted yet. */
extern int fdps_map_actor_use_item(int unit_index, int side_select);
#pragma aux fdps_map_actor_use_item "*" parm caller [];

#endif
