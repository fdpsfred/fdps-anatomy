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

/* Which spell should the actor at unit_index cast this turn, and where?  Walks
   the spells the actor has learned, spreads each one's cast distance out of the
   actor's own tile as a walkable range, collects the units the spell would
   catch from every tile in that range, scores each (spell, tile) pair with
   fdps_score_targets_for_spell below and publishes the best in the four AI
   decision globals gamedata.h declares:
   data_fdps_battle_ai_best_spell_score, data_fdps_map_ai_best_spell_id,
   data_fdps_battle_ai_best_spell_target_x and ..._target_y.  Always returns 0.

   Two early exits leave the score at a fresh 0 and the other three globals
   untouched: an actor that has learned no spell, and an actor whose
   status_timers[5] -- the timer 封魔咒術 leaves -- is still running.  A spell
   whose MP cost exceeds the actor's current MP is passed over, an exact match
   being affordable.

   side_select says which side the actor is on and decides how the spell's own
   target byte becomes fdps_collect_targets_in_range's select_mode: non-zero
   forwards the byte unchanged, 0 -- the enemy phase -- replaces it with
   (byte == 0).  That inversion is boolean, so the target byte of 3 that 0x16
   神行術 alone carries comes out as 0 for an enemy caster, the same value a
   byte of 1 gives.  Only 0, 1 and 3 occur in MAGICDAT.DAT, so this caller never
   produces that collector's select_mode 2.

   The cast distance is handed to the flood fill RAW, straight-line bit 0x10
   included, which the player's own targeting path decodes and this one does not
   (assets/tables/spells.md).  The range is spread over PROMAP.DAT row 0, whose
   eight terrain costs are all 1, so it counts walkable tiles and walls cut it
   short; a distance of 0 leaves the actor's own tile as the only candidate.

   The score is on the same tier scale as the attack and item scorers, and the
   ranking's tie-break is the spell's signed power word: on an equal score the
   larger power wins, so an ordinary spell always outranks one of the eight
   絕招, which store power as a negative attack multiplier.

   The grid has to arrive as fdps_map_grid_reset (movegrid.h) leaves it and is
   reset again after every fill.  Nothing is bounds checked: not the candidate
   tiles against the 200-pair buffer, not the targets against the 32-index
   buffer, not the learned spells against the 20-byte list, and not the malloc
   against null. */
extern int fdps_map_actor_score_best_spell(int unit_index, int side_select);
#pragma aux fdps_map_actor_score_best_spell "*" parm caller [];

/* Which item in the actor's bag should it use this turn, and where?  Walks the
   entries fdps_unit_item_count says the actor is carrying, spreads each usable
   item's reach out of the actor's own tile as a set of candidate aim tiles,
   collects the units the item would catch from every one of them, scores each
   (item, aim tile) pair with fdps_score_targets_for_item below and publishes
   the best in the four AI decision globals gamedata.h declares:
   data_fdps_battle_ai_best_item_score, data_fdps_map_ai_best_item_bag_slot,
   data_fdps_map_ai_best_item_target_x and ..._target_y.  Always returns 0.

   An actor with an empty bag returns at once with the score at a fresh 0 and
   the other three globals untouched, so the previous actor's decision is still
   standing in them.  An item whose ITEM.DAT use_effect byte is 0 cannot be
   used and is passed over.

   The bag walk uses its counter directly as the entry index and never re-tests
   the flag byte fdps_unit_item_count counted with, so it reads entries
   0..count-1 and assumes the eight are packed (rebuild_info/pitfalls.md).

   The item's use_distance byte carries the reach in its low nibble and the
   straight-line shape in bit 0x10, and the two halves of the search read it
   differently.  The aim search takes the byte as the reach with a minimum
   distance of 0, which keeps the actor's own tile among the candidates; a line
   item instead searches with reach 1 and minimum distance 1, so its aim tiles
   are its passable orthogonal neighbours.  The shape test then reads the byte
   again RAW: below 0x10 the units the item catches come from
   fdps_collect_targets_in_range over the item's use_radius centred on the aim
   tile, at 0x10 and above from fdps_collect_targets_in_line swept from the
   actor's own tile toward the aim tile for use_distance - 0x10 tiles.

   side_select says which side the actor is on and decides how the item's
   use_target byte becomes the collector's side filter: non-zero -- the NPC
   phase -- forwards the byte unchanged, 0 -- the enemy phase -- replaces it
   with (byte == 0).  fdps_map_actor_use_item repeats the same inversion when
   it carries the choice out.

   The reach is spread over PROMAP.DAT row 0, whose eight terrain costs are all
   1, so it counts walkable tiles and walls cut it short.  The grid has to
   arrive as fdps_map_grid_reset (movegrid.h) leaves it and is reset again after
   every collection.  Nothing is bounds checked: not the candidate tiles against
   the 200-pair buffer, not the targets against the 32-index buffer, and not the
   malloc against null; that buffer is also leaked on the empty-bag path. */
extern int fdps_map_actor_score_best_item(int unit_index, int side_select);
#pragma aux fdps_map_actor_score_best_item "*" parm caller [];

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

/* How much is casting spell_id on these targets worth?  target_unit_indices is
   target_count battle unit indices, one byte each; the per-target scores are
   summed and the sum returned.  Higher is better and 0 means there is nothing
   there worth hitting.

   One branch per spell family, by MAGICDAT.DAT id (assets/spells.md):

     - 0x0e-0x10 恢復之光 / 治癒之風 / 痊癒之泉 and 0x21 鎮魂之歌, the heals:
       8 for a target below a third of its maximum HP, 3 for one below half, 0
       otherwise, doubled when bit 0 of the target's ai_behavior is set.  Both
       thresholds are strict and both divisions truncate toward zero.  Current
       HP and maximum HP are read UNSIGNED here, so a target whose HP word has
       gone negative reads as the most hurt unit on the map.
     - 0x11 封魔咒術: 6 for every target that knows at least one spell and
       whose seal timer, status_timers[5], is clear.
     - 0x12 腐毒術 and 0x13 麻痺術: 10 for every target not already carrying
       that ailment, through fdps_score_targets_without_status.
     - 0x14 神之祝福: three fdps_score_targets_without_status passes at 4 a
       target over the three blessing timers, of which only the last -- the
       dexterity one at record +0x24 -- reaches the total, because the original
       stores each answer over the previous rather than adding it.
     - 0x18 甦癒術: 6 for every target already carrying poison, paralysis or
       the magic seal.
     - every other id, the damaging spells: 0x18 when the target's current HP
       is strictly below the spell's power word, 8 otherwise, multiplied by 1.5
       as a double and truncated back to an int when the target's char_id is 0,
       the protagonist 蘭迪斯.  Current HP is read SIGNED here.  The power word
       is signed too and is negative for the attack-multiplier spells, so every
       target of one of those scores the flat 8.  Targets of 0x0a 裂地術 and
       0x0b 封神裂震 that fdps_unit_is_flying (unit.h) calls airborne are
       skipped and contribute nothing.

   The spell record is resolved through fdps_get_spell_record (table.h) on entry
   whether or not the branch taken reads it, and each target index through
   fdps_get_unit_record (unit.h) as the walk reaches it.  Nothing is range
   checked: not spell_id, not the index bytes.  A target_count of 0 or below
   scores 0 without reading the list, every loop test being signed and placed
   before its body. */
extern int fdps_score_targets_for_spell(int spell_id, int target_count,
                                        unsigned char *target_unit_indices);
#pragma aux fdps_score_targets_for_spell "*" parm caller [];

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
