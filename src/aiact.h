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
   fdps_map_actor_score_best_item (aiscore.h) last scored.  Answers 0 on every
   path; both callers discard it.

   Nothing about the use is an argument.  The bag slot and the tile come out of
   that search's globals, and the item id out of the acting unit's own record
   at +0x0b + 2 * slot.

   THERE IS NO SCORE GATE HERE.  Unlike the cast above, this one carries the
   decision out whatever the search scored; both callers have already applied
   the threshold.

   THE ITEM'S SHAPE IS ITS use_distance BYTE AT +0x10.  Below 0x10 the item
   covers an area: the targets come from fdps_collect_targets_in_range
   (aitarget.h) with the use_radius byte at +0x12 as the reach and a min_dist
   of 0, and the presentation is the cursor walked to the aim tile carrying the
   radius-plus-two blast diamond.  From 0x10 up it covers a straight line: the
   targets come from fdps_collect_targets_in_line with the byte minus 0x10 as
   the length (a value compare and a subtraction, not a bit mask) and the
   actor's own tile as the origin, and the presentation is the
   whole beam -- the actor turned to face the first unit found, a white flash
   ramped back to the normal palette, the cursor swept out to the beam's far
   end in the overlay mode that leaves a highlight trail behind it, eight
   frames holding that trail, and the cursor brought back onto that first unit.

   WHAT THE SHIPPED TABLE ACTUALLY ASKS FOR.  Over the 251 records of ITEM.DAT
   (MISC.VFS member 91) use_radius is only ever 0, 1 or 2, so the blast diamond
   this action sets is only ever mode 2, 3 or 4 -- no real item reaches the
   mode 6 that clears grid markers, which is why the area arm's leaving the
   overlay up costs the grid nothing.  Three records take the line arm: 0x63
   and 0xc7 (use_distance 0x1e, a 14-tile beam) and 0xbe (0x16, six tiles), all
   three with use_target 5 and use_radius 0.  The other 248 cover an area.

   WHICH SIDE IS AIMED AT DEPENDS ON THE PHASE, exactly as it does for the
   cast.  side_select 0 -- the enemy phase, from fdps_battle_enemy_turn_phase
   -- turns the item's authored use_target byte at +0x11 into the filter
   `use_target == 0`, and side_select 1 -- the NPC phase, from
   fdps_battle_npc_turn_phase -- passes the byte through.  That is what lets a
   single authored healing item serve an enemy actor and a guest NPC alike.

   THE SWEPT-TO TILE IS CLAMPED AGAINST THE .MPL MAGIC AND NOT AGAINST THE MAP.
   The bounds are the signed words at +0 and +2 of layer 0's blob, which on a
   real .MPL are the first four bytes of its magic: 0x504d and 0x004c whatever
   map is loaded.  On x that bound of 20557 is out of reach, since the longest
   beam in the shipped table is 14 tiles and no shipped .MPL is larger than 64
   by 64, so only the clamp to 0 fires there; the y bound of 76 is reachable,
   from six rows of aim difference up, and pins the sweep at row 75.  Either
   way a beam aimed off the right or bottom edge sweeps the cursor past the
   map, marking cells of the following row.  It is the original's behaviour,
   and clamping against the real extents would not reproduce it
   (rebuild_info/pitfalls.md).

   THE SEARCH'S TWO COORDINATES ARE OVERWRITTEN ON THE LINE ARM.  They come out
   holding the beam's far end rather than the tile the search chose, so a
   caller that reads them after this returns is reading the sweep and not the
   decision.  The area arm leaves both alone.

   THE CURSOR OVERLAY IS LEFT UP ON THE AREA ARM.  That path ends with the
   blast diamond still set, and only one of the two callers takes it back down:
   fdps_map_actor_take_best_action clears it on its own shared exit, while
   fdps_map_actor_behavior_step does not -- the diamond survives its return and
   is cleared by whichever action runs next (fdps_map_actor_move_and_attack,
   fdps_map_actor_move_toward_nearest_reachable_opponent, fdps_unit_rest) or,
   failing all of those, by the phase loop itself.  The line arm ends at 0.

   THE ACTOR IS CREDITED NO EXPERIENCE.  data_fdps_battle_pending_xp_credit is
   zeroed at the end and paid to nobody, the same as the cast above.

   unit_index is the acting unit's place in the map unit array: it is whose bag
   the item comes out of, whose tile the beam starts from, and the actor the
   effect is applied on behalf of. */
extern int fdps_map_actor_use_item(int unit_index, int side_select);
#pragma aux fdps_map_actor_use_item "*" parm caller [];

#endif
