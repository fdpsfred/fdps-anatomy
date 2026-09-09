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
   cast fdps_map_actor_score_best_spell (aiscore.h) last scored.  Answers 1 when
   the spell was cast and 0 when it was not; both call sites discard it.

   Nothing about the cast is an argument.  The spell id, the tile and the score
   all come out of that search's four globals, and none of them is written
   here.

   THE SCORE IS THE ONLY GATE.  Below 6 the function does nothing whatever --
   no cursor move, no target collection, no cast, no clearing of the pending
   experience -- and returns 0.  Both callers have already applied the same
   test, so in the shipped image that answer is never seen.

   The sequence above the gate is: put the cursor on the actor; collect the
   units the blast covers from the target tile with fdps_collect_targets_in_range
   (aitarget.h), the spell record's reach byte as range_code and a min_dist of
   0, into a 32-byte array; put the movement grid back with fdps_map_grid_reset
   (movegrid.h); show the blast outline by setting
   data_fdps_map_cursor_draw_mode to that reach plus 2 while
   fdps_map_cursor_move_to (mapcur.h) walks the cursor to the tile scaled by 24
   pixels; hold for a fifth of a second; clear the overlay and paint one frame;
   play the spell; collect, animate and run the deaths it caused; and discard
   the pending battle experience.

   WHICH SIDE IS AIMED AT DEPENDS ON THE PHASE.  side_select is 0 on the enemy
   phase and 1 on the NPC phase, and its only use here is the target-side
   filter: on the enemy phase the filter is `spell record byte +6 == 0` and on
   the NPC phase it is that byte itself.  So an enemy caster turns a
   player-facing spell into one that hits every non-zero side and collapses any
   other authored value to 0, while an NPC -- already on the player's side --
   aims where the record says.

   HOW THE SPELL IS PLAYED IS data_fdps_ui_battle_animation_enabled's DECISION,
   and the test is an equality against 1.  Exactly 1 hands the cast to
   fdps_combat_play_spell_on_targets (cmbspell.h) on the full-screen fight
   presentation; every other value, 0 included, plays it on the map through
   fdps_cast_spell_on_targets (spell.h).  Both are called with the same four
   arguments.

   THE PENDING EXPERIENCE IS THROWN AWAY.  data_fdps_battle_pending_xp_credit is
   zeroed at the end and paid to nobody, where the attack action above scales it
   by 15/10 and awards it: a spell earns no experience for the unit it hits.

   THE CURSOR IS LEFT WITH NO OVERLAY.  The last store is 0, over the 1 that
   fdps_cast_spell_on_targets leaves behind, so a caller that had any other mode
   up does not get it back.

   unit_index is the acting unit's place in the map unit array; it reaches
   fdps_map_cursor_move_to_unit and is the caster id handed to whichever of the
   two play routines runs. */
extern int fdps_map_actor_cast_chosen_spell(int unit_index, int side_select);
#pragma aux fdps_map_actor_cast_chosen_spell "*" parm caller [];

/* 00027180.  Uses the actor's bag entry data_fdps_map_ai_best_item_bag_slot
   names on the tile data_fdps_map_ai_best_item_target_x /
   data_fdps_battle_ai_best_item_target_y name (gamedata.h), which is the use
   fdps_map_actor_score_best_item last scored.  Not emitted yet. */
extern int fdps_map_actor_use_item(int unit_index, int side_select);
#pragma aux fdps_map_actor_use_item "*" parm caller [];

#endif
