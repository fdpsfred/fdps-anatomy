/* aiscore.c -- scoring the map AI's candidate attacks, items and spells.
 *
 * See aiscore.h for what each scorer is asked and what its number means.
 * Nothing here owns state: every scorer reads the data tables through the
 * accessors in table.h and the battle units through unit.h, and reports.
 */
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "aitarget.h"
#include "movegrid.h"
#include "table.h"
#include "unit.h"
#include "unititem.h"
#include "aiscore.h"

/* The three heap blocks fdps_map_actor_score_best_attack takes for the search:
   PUSH 0x64 at 00012310, PUSH 0x1900 at 0001231d and PUSH 0x64 at 00012385.
   The tile buffer is 3200 (x, y) byte pairs, which is the whole map and more;
   the index buffer has room for 100 unit indices and nothing compares that
   against the number of targets a tile turns up.  The first 0x64 block is
   allocated, never read and freed again -- the only trace of it in the
   original is the malloc/free pair. */
#define ATTACK_SEARCH_UNUSED_BYTES 100
#define ATTACK_SEARCH_TILE_BYTES 0x1900
#define ATTACK_SEARCH_TARGET_BYTES 100

/* 00012230.  Picks the best physical attack the actor could make this turn --
   which tile to move to and which unit to hit -- and publishes the choice in
   the four AI decision globals.  The returned value is 0 down both paths;
   fdps_map_actor_behavior_step still tests it (TEST EAX,EAX / JNZ at 0001014c)
   and so always takes the fall-through.

   The score global is zeroed at 0001226d, before the equipped-weapon test, so
   the early return leaves a fresh 0 there; the other three globals are not
   touched on that path and keep whatever the previous actor left in them.

   fdps_get_class_record is asked for the actor's class byte PLUS ONE at
   000122fd -- INC EAX between the load and the PUSH -- because PROMAP.DAT's
   row 0 is the default terrain-cost row that fdps_collect_targets_in_range
   uses for its own reach flood.  Dropping the +1 hands the fill the wrong
   class's costs and changes which tiles the actor is judged able to reach.

   Every branch in the ranking is signed and each one is behaviour:

     - the wound tier is CMP dword ptr [EBP-0x30],0x2 / JLE at 00012462, so an
       estimate of exactly 2 scores tier 0 and 3 scores tier 8;
     - the lethal test is MOVSX word ptr [EAX+0x40] / CMP / JGE at 00012478, so
       it is strictly target HP < estimate: a blow that exactly matches the
       target's remaining HP is a wound, not a kill;
     - the counter-attack penalty is gated on CMP EAX,0x1 / JNZ at 000124a2.
       fdps_check_can_counter_attack_from_tile answers 1 or -1 and never 0, so
       using its result as a bare predicate would apply the penalty to every
       target;
     - the protagonist bonus is LEA EDX,[EDX+EDX*2] then the SAR 0x1f /
       SUB / SAR 0x1 halving at 000124bc, which is the signed divide that
       truncates toward zero: an estimate of 5 becomes 7, not 8.

   What is published as the score is the coarse tier -- 0, 8 or 0x12 -- and not
   the estimate the search actually ranks on; the estimate never leaves the
   frame.  fdps_map_actor_take_best_action weighs that number against the spell
   and item scores on the same 8 / 18 scale, so publishing the estimate instead
   would make the AI attack in preference to everything else it could do.

   The tile and the target are published together at 000124f1-0001250c: x from
   byte 0 of the candidate pair, y from byte 1, then the target's unit index
   and the tier.  The estimate copies at [EBP-0x14] and [EBP-0x18] that the
   original refreshes from the actor's two stats at the top of every tile pass
   are not reproduced -- neither is ever written again, so they hold the values
   read once at 0001225c and 00012266.

   Nothing is bounds checked: not the tile count against the 3200 pairs the
   buffer holds, not the target count against the 100 indices, and neither
   malloc result against null. */
int fdps_map_actor_score_best_attack(int unit_index, int side_select)
{
    struct fdps_unit_record *actor;
    struct fdps_unit_record *target;
    struct fdps_item_effect *weapon;
    struct fdps_class_record *class_move_cost;
    void *unused_block;
    unsigned char *tile_coords;
    unsigned char *target_indices;
    int actor_attack;
    int actor_defence;
    int actor_x;
    int actor_y;
    int actor_class;
    int move_points;
    int weapon_slot;
    int weapon_item_id;
    int weapon_range_min;
    int weapon_range_max;
    int target_select_mode;
    int tile_count;
    int tile_slot;
    int tile_x;
    int tile_y;
    int target_count;
    int target_slot;
    int target_index;
    int target_attack;
    int target_defence;
    int damage_estimate;
    int score_tier;
    int best_estimate;

    best_estimate = 0;
    target_select_mode = 0;

    actor = fdps_get_unit_record(unit_index);
    actor_attack = actor->ap;
    actor_defence = actor->dp;
    data_fdps_battle_ai_best_physical_score = 0;

    weapon_slot = fdps_unit_find_equipped_slot(unit_index, 0);
    if (weapon_slot == -1) {
        return 0;
    }

    weapon_item_id = fdps_unit_get_item_id(unit_index, weapon_slot);
    weapon = fdps_get_item_record(weapon_item_id);
    weapon_range_min = weapon->range_min;
    weapon_range_max = weapon->range_max;
    move_points = actor->move;
    actor_x = actor->pos_x;
    actor_y = actor->pos_y;
    actor_class = actor->clazz;
    class_move_cost = fdps_get_class_record(actor_class + 1);

    unused_block = malloc(ATTACK_SEARCH_UNUSED_BYTES);
    tile_coords = malloc(ATTACK_SEARCH_TILE_BYTES);
    if (side_select == 0) {
        target_select_mode = 1;
    }

    fdps_move_grid_mark_opposing_zones_of_control(side_select);
    fdps_move_grid_flood_fill_range(class_move_cost, actor_x, actor_y,
                                    move_points);
    fdps_move_grid_block_occupied_tiles(unit_index, side_select);
    tile_count = fdps_map_grid_collect_marked_tiles(tile_coords);
    fdps_map_grid_reset();

    target_indices = malloc(ATTACK_SEARCH_TARGET_BYTES);

    for (tile_slot = 0; tile_slot < tile_count; tile_slot++) {
        tile_x = (int) tile_coords[tile_slot * 2];
        tile_y = (int) tile_coords[tile_slot * 2 + 1];
        target_count = fdps_collect_targets_in_range(tile_x, tile_y,
                                                     target_indices,
                                                     weapon_range_max,
                                                     weapon_range_min,
                                                     target_select_mode);
        fdps_map_grid_reset();
        if (target_count != 0) {
            for (target_slot = 0;
                 target_slot < target_count;
                 target_slot++) {
                target_index = (int) target_indices[target_slot];
                target = fdps_get_unit_record(target_index);
                target_attack = target->ap;
                target_defence = target->dp;
                damage_estimate = actor_attack - target_defence;
                if (damage_estimate > 2) {
                    score_tier = 8;
                } else {
                    score_tier = 0;
                }
                if (target->hp_current < damage_estimate) {
                    damage_estimate = damage_estimate * 2;
                    score_tier = 0x12;
                }
                if (fdps_check_can_counter_attack_from_tile(target_index,
                                                           tile_x,
                                                           tile_y) == 1) {
                    damage_estimate = damage_estimate +
                                      (actor_defence - target_attack);
                }
                if (target->char_id == 0) {
                    damage_estimate = damage_estimate * 3 / 2;
                }
                if (data_fdps_battle_ai_best_physical_score < score_tier ||
                    (score_tier ==
                         data_fdps_battle_ai_best_physical_score &&
                     best_estimate < damage_estimate)) {
                    best_estimate = damage_estimate;
                    data_fdps_battle_ai_best_physical_target_x = tile_x;
                    data_fdps_battle_ai_best_attack_tile_y = tile_y;
                    data_fdps_battle_ai_best_physical_target_idx =
                        target_index;
                    data_fdps_battle_ai_best_physical_score = score_tier;
                }
            }
        }
    }

    free(target_indices);
    free(unused_block);
    free(tile_coords);
    return 0;
}

/* 000132b0.  Fetches the ITEM.DAT record once, keeps use_amount and the
   use_effect code, and runs one of two target walks or neither.

   The two walks read the same current-HP word at unit record +0x40 with
   different signedness and that is behaviour, not spelling: MOVSX at 00013329
   in the restorative walk against MOV AX + AND 0xffff at 000133dc in the
   damage walk, while the item's use_amount at +0x0e is sign-extended at
   000132d5 either way.  Reading hp_current as the signed short the layout
   declares in both places reproduces the first walk and breaks the second: a
   unit whose HP word had gone negative would score 0x12, "this finishes it",
   where the original scores 8, "still standing".  Hence the cast in the
   damage walk and none in the restorative one.

   The two thresholds are signed divisions of the maximum -- IDIV EBX with
   EBX = 3 at 00013348, and SAR-based halving at 00013363 -- and both compare
   with JL, so they are strictly less-than against the current HP: at exactly
   a third the score is 8, not 3.  Writing either as <= moves the boundary of
   every heal decision the AI makes.

   use_effect is loaded zero-extended (XOR EAX,EAX / MOV AL at 000132dc), so
   the dispatch is over 0x00..0xff and 0x0b and 0x1e are the only codes with a
   walk; the caller filters out 0 before calling but this function does not
   depend on that.

   use_amount is read before the dispatch and the restorative walk never looks
   at it, which is why all four HP items score alike however much they
   restore. */
int fdps_score_targets_for_item(int item_id, int target_count,
                                unsigned char *target_unit_indices)
{
    struct fdps_item_effect *item;
    struct fdps_unit_record *target;
    int use_amount;
    int use_effect;
    int target_index;
    int hp_current;
    int hp_max;
    int score;
    int total;

    total = 0;
    item = fdps_get_item_record(item_id);
    use_amount = item->use_amount;
    use_effect = item->use_effect;
    if (use_effect == 0x0b) {
        for (target_index = 0; target_index < target_count; target_index++) {
            target = fdps_get_unit_record(target_unit_indices[target_index]);
            hp_current = target->hp_current;
            hp_max = target->hp_max;
            if (hp_max / 3 < hp_current) {
                if (hp_max / 2 < hp_current) {
                    score = 0;
                } else {
                    score = 3;
                }
            } else {
                score = 8;
            }
            if ((target->ai_behavior & 0x80) != 0) {
                score = score * 3;
            }
            total = total + score;
        }
    } else if (use_effect == 0x1e) {
        for (target_index = 0; target_index < target_count; target_index++) {
            target = fdps_get_unit_record(target_unit_indices[target_index]);
            if (use_amount < (int) (unsigned short) target->hp_current) {
                score = 8;
            } else {
                score = 0x12;
            }
            total = total + score;
        }
    }
    return total;
}

/* 00013c20.  One walk over the target list, one byte tested per target.

   The status byte is reached as record + status_offset rather than through a
   named field because the offset is an argument: MOV EAX,[EBP-0x8] / ADD
   EAX,[EBP+0x1c] / CMP byte ptr [EAX],0x0 at 00013c65.  The five offsets the
   caller passes -- 0x22, 0x23 and 0x24 for 神之祝福's three buff slots, 0x25
   for 腐毒術 and 0x26 for 麻痺術 -- are all inside struct fdps_unit_record's
   status_timers, and each byte is a count of turns the effect still has to
   run, so any nonzero value means the effect is already on the unit and that
   target contributes nothing.

   The loop test is CMP EAX,[EBP+0x14] / JL at 00013c3d: it runs before the
   body and it is signed, so a target_count of 0 or below returns the untouched
   accumulator without reading target_ids at all.

   The index is loaded MOV AL / AND EAX,0xff at 00013c52, so it is zero
   extended -- an index byte of 0x81 is unit 129.  Neither that index nor the
   record pointer is range checked, because fdps_get_unit_record checks
   neither, and the record is re-resolved on every pass rather than held.

   fdps_score_targets_for_spell stores the result of all five of its calls with
   MOV, not ADD -- 00013a96, 00013b18, 00013b3a, 00013b51 and 00013b68 all
   write the same accumulator at [EBP-0x14] -- so in the 神之祝福 branch the
   0x22 and 0x23 totals are computed and then overwritten by the 0x24 one. */
int fdps_score_targets_without_status(int target_count,
                                      unsigned char *target_ids,
                                      int status_offset, int score_per_target)
{
    struct fdps_unit_record *target;
    int target_index;
    int total;

    total = 0;
    for (target_index = 0; target_index < target_count; target_index++) {
        target = fdps_get_unit_record(target_ids[target_index]);
        if (*((unsigned char *) target + status_offset) == 0) {
            total = total + score_per_target;
        }
    }
    return total;
}
