/* aiact.h -- the map AI's action layer: the three routines that carry out the
 * choice src/aiscore.c has already scored, once the behaviour dispatcher has
 * decided that choice is worth taking.
 *
 * Each of the three reads the winning search's globals back out of gamedata.h
 * -- the target, the tile and the bag slot or spell id -- so a caller that has
 * not first checked that search's score is handing them a stale decision.
 *
 * None of the three is emitted yet.  What is declared here is only what the
 * behaviour dispatcher needs to call them: the file that defines them owns
 * this header and fills in the rest.
 */
#ifndef AIACT_H
#define AIACT_H

/* 00012e50.  Walks the actor onto the tile
   data_fdps_battle_ai_best_physical_target_x / _y name and attacks the unit at
   data_fdps_battle_ai_best_physical_target_idx (gamedata.h), which is the
   attack fdps_map_actor_score_best_attack last scored. */
extern int fdps_map_actor_move_and_attack(int unit_index, int side_select);
#pragma aux fdps_map_actor_move_and_attack "*" parm caller [];

/* 00013c90.  Casts data_fdps_map_ai_best_spell_id at the tile
   data_fdps_battle_ai_best_spell_target_x / _y name (gamedata.h), which is the
   cast fdps_map_actor_score_best_spell last scored. */
extern int fdps_map_actor_cast_chosen_spell(int unit_index, int side_select);
#pragma aux fdps_map_actor_cast_chosen_spell "*" parm caller [];

/* 00027180.  Uses the actor's bag entry data_fdps_map_ai_best_item_bag_slot
   names on the tile data_fdps_map_ai_best_item_target_x /
   data_fdps_battle_ai_best_item_target_y name (gamedata.h), which is the use
   fdps_map_actor_score_best_item last scored. */
extern int fdps_map_actor_use_item(int unit_index, int side_select);
#pragma aux fdps_map_actor_use_item "*" parm caller [];

#endif
