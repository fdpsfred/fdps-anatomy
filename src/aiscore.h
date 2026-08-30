/* aiscore.h -- how the map AI weighs the candidate actions it has found.
 *
 * A scorer here is handed one thing the acting unit could do -- an item, a
 * spell, an attack -- together with the units a target collector (aitarget.h)
 * says that action would reach, and answers how desirable doing it would be as
 * a single number.  Nothing here changes the battle state or picks anything:
 * the caller compares the numbers and keeps the largest.
 *
 * The scales are per-scorer and are not comparable across them beyond the one
 * threshold fdps_map_actor_take_best_action applies to the winning total.
 */
#ifndef AISCORE_H
#define AISCORE_H

/* Which physical attack should the actor at unit_index make this turn?  Sweeps
   every tile the actor can move to, collects the units its equipped weapon
   would reach from each, scores each (tile, target) pair and publishes the best
   one in the four AI decision globals gamedata.h declares:
   data_fdps_battle_ai_best_physical_score, ..._target_idx, ..._target_x and
   data_fdps_battle_ai_best_attack_tile_y.  Always returns 0.

   side_select says which side the actor is on and is passed on unchanged to
   fdps_move_grid_mark_opposing_zones_of_control and
   fdps_move_grid_block_occupied_tiles, which read it with opposite polarities;
   it becomes fdps_collect_targets_in_range's select_mode as (side_select == 0),
   so 0 -- the enemy phase -- looks for every non-zero side and any other value
   looks for side 0.

   The score is a tier and not a damage figure: 0 when the actor's attack stat
   exceeds the target's defence by 2 or less, 8 when it exceeds it by more, and
   0x12 when the estimated blow is strictly greater than the target's remaining
   HP.  It shares that scale with the spell and item scorers below, and
   fdps_map_actor_take_best_action compares the three directly.  Within a tier
   the pair with the larger damage estimate wins, ties going to the one found
   first, and that estimate is never published.

   The estimate the ranking uses is the actor's attack stat minus the target's
   defence stat, doubled when the blow is lethal, then plus the actor's defence
   stat minus the target's attack stat when
   fdps_check_can_counter_attack_from_tile (aitarget.h) answers exactly 1, then
   multiplied by 3/2 when the target's char_id is 0 -- the protagonist 蘭迪斯,
   whom the AI is made to prefer.

   The score global is zeroed before anything else, so a caller reads a fresh 0
   even when the actor has no weapon equipped and the search never runs; the
   other three globals are left alone on that path.  The grid has to arrive as
   fdps_map_grid_reset (movegrid.h) leaves it and is reset again on the way
   out.  Nothing is bounds checked: neither the candidate tiles against the
   3200-pair buffer, nor the targets against the 100-index buffer, nor either
   malloc against null. */
extern int fdps_map_actor_score_best_attack(int unit_index, int side_select);
#pragma aux fdps_map_actor_score_best_attack "*" parm caller [];

/* How much is using item_id on these targets worth?  target_unit_indices is
   target_count battle unit indices, one byte each; the per-target scores are
   summed and the sum returned.

   Only two ITEM.DAT use_effect codes are weighed at all.  0x0b, the HP
   restoratives, scores each target on how hurt it is -- 8 when its current HP
   is at or below a third of its maximum, 3 when at or below half, 0 otherwise
   -- and triples that when bit 0x80 of the target's ai_behavior is set.  The
   item's own use_amount is not consulted there, so every restorative scores
   alike.  0x1e, the line-shaped damage items, scores 0x12 when the target's
   current HP is at or below use_amount and 8 when it is above, so a killing
   hit outweighs a wounding one.  Every other use_effect code, including 0,
   scores 0 for every target and the function returns 0.

   item_id is not range checked and the record is resolved through
   fdps_get_item_record (table.h) on entry; each target index is resolved
   through fdps_get_unit_record (unit.h) as the walk reaches it, so a record
   pointer is never held across the loop. */
extern int fdps_score_targets_for_item(int item_id, int target_count,
                                       unsigned char *target_unit_indices);
#pragma aux fdps_score_targets_for_item "*" parm caller [];

/* How much is putting a status effect on these targets worth?  target_ids is
   target_count battle unit indices, one byte each; status_offset is the byte
   offset of the effect's timer inside struct fdps_unit_record -- 0x22, 0x23 and
   0x24 for 神之祝福's three buff slots, 0x25 for 腐毒術, 0x26 for 麻痺術, all
   of them within status_timers -- and the answer is score_per_target for every
   target whose timer byte is zero, summed.

   A nonzero timer is turns still to run, so a target already carrying the
   effect is worth nothing and casting on a group that all carry it scores 0.
   target_count is compared signed and the test precedes the body, so a count of
   0 or below scores 0 without reading target_ids.  Index bytes are zero
   extended and are not range checked; each is resolved through
   fdps_get_unit_record (unit.h) as the walk reaches it. */
extern int fdps_score_targets_without_status(int target_count,
                                             unsigned char *target_ids,
                                             int status_offset,
                                             int score_per_target);
#pragma aux fdps_score_targets_without_status "*" parm caller [];

#endif
