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
#include "unitstat.h"
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

/* The two scratch areas fdps_map_actor_score_best_item searches through: PUSH
   0x190 at 00013091 for the candidate aim tiles, and the 0x20 bytes between
   [EBP-0x70] and the next local for the unit indices a collector reports.  The
   tile block is 200 (x, y) byte pairs and the index block 32 indices, and
   nothing compares either against what actually arrives. */
#define ITEM_SEARCH_TILE_BYTES 0x190
#define ITEM_SEARCH_TARGET_BYTES 32

/* PUSH 0x0 before the fdps_get_class_record call at 00013056: PROMAP.DAT's
   leading default row, the row whose eight terrain costs are all 1.  The
   record it answers with is never read here -- see the note on the function
   below. */
#define ITEM_SEARCH_DEFAULT_CLASS_ROW 0

/* MOV byte ptr [EBP-0x4],0x1 / MOV byte ptr [EBP-0x8],0x1 at 00013114: the
   reach and the minimum distance a line item's aim search runs with.  A
   minimum distance of 1 is exclusive and drops the actor's own tile alone, so
   the candidate aim tiles of a line item are its passable neighbours. */
#define ITEM_SEARCH_LINE_AIM_REACH 1
#define ITEM_SEARCH_LINE_AIM_MIN_DIST 1

/* CMP EAX,0xf / JLE at 0001310f and again at 000131d6.  The ITEM.DAT
   use_distance byte is compared as a number, not masked: below 0x10 it is the
   reach, from 0x10 up the item covers a straight line whose length is the
   byte less 0x10. */
#define ITEM_USE_DISTANCE_LINE_BASE 0x10

/* 00013040.  Picks the best item use the actor could make this turn -- which
   bag entry to spend and which tile to aim it at -- and publishes the choice
   in the four AI decision globals.  The returned value is 0 down both paths
   and no caller looks at it.

   The score global is zeroed at 0001304c, before anything else, so the
   empty-bag return leaves a fresh 0 there; the other three are not touched on
   that path and keep whatever the previous actor left in them.

   The class record and the actor's current MP are fetched and never read
   again.  They are the opening of fdps_map_actor_score_best_spell below, which
   uses both -- the class-0 record as the all-costs-1 movement table it hands
   to the flood fill, the MP word as what each spell's cost is weighed against.
   Items cost no MP and this search gets its candidate tiles from a target
   sweep rather than a flood fill of its own, so neither has any work left to do
   here.  Both are kept because both are calls or reads the original makes.

   The bag walk uses its loop counter directly as the entry index -- item id at
   unit record +0xb + slot*2, which is inventory_slots[slot * 2 + 1] -- and
   never re-tests the flag byte at +0xa that fdps_unit_item_count counted with.
   The obvious defensive loop, walk all eight entries and skip the empty ones,
   would pick a different item set and publish a different bag slot if a bag
   ever had a hole in it.  None does: fdps_unit_remove_item shifts the entries
   above the removed one down and fdps_unit_add_item fills the first empty
   entry, so every bag stays packed and the two loops agree on shipped data.

   use_distance is read twice, and the two reads are not the same value.  The
   aim search at 00013101 takes the byte and clamps a line item down to reach 1
   with minimum distance 1; the shape test at 000131c8 reloads the byte RAW
   from the record.  Keeping the clamped value in one variable sends every line
   item down the radius branch.

   The side filter is worked out before that reload, once per aim tile, from
   the item's use_target byte: CMP dword ptr [EBP+0x18],0x0 / JNZ at 0001319c
   forwards the byte unchanged on the NPC phase and replaces it with
   (byte == 0) on the enemy phase.  The inversion is a boolean one, so a
   use_target of 3 comes out as 0 for an enemy actor, the same value a byte of
   1 gives.

   The ranking is CMP EAX,[0x00063f8c] / JLE at 0001325d, so it is strictly
   greater and signed: on an equal score the incumbent stands, and the first
   bag entry and the first aim tile that reach a score keep it.

   Nothing is bounds checked: not the candidate tiles against the 200 pairs,
   not the targets against the 32 indices, and not the malloc against null.
   The block is also leaked on the empty-bag path, because the early return at
   000130bd sits between the malloc at 00013096 and the free at 0001328d. */
int fdps_map_actor_score_best_item(int unit_index, int side_select)
{
    struct fdps_unit_record *actor;
    struct fdps_item_effect *item;
    struct fdps_class_record *default_class_move_cost;
    unsigned char *tile_coords;
    unsigned char target_indices[ITEM_SEARCH_TARGET_BYTES];
    int actor_x;
    int actor_y;
    int actor_mp;
    int bag_item_count;
    int bag_slot;
    int item_id;
    int aim_reach;
    int aim_min_dist;
    int use_distance;
    int tile_count;
    int tile_slot;
    int aim_x;
    int aim_y;
    int target_select_mode;
    int target_count;
    int score_tier;

    data_fdps_battle_ai_best_item_score = 0;
    default_class_move_cost =
        fdps_get_class_record(ITEM_SEARCH_DEFAULT_CLASS_ROW);
    actor = fdps_get_unit_record(unit_index);
    actor_mp = actor->mp_current;
    actor_x = (int) actor->pos_x;
    actor_y = (int) actor->pos_y;

    tile_coords = malloc(ITEM_SEARCH_TILE_BYTES);
    bag_item_count = fdps_unit_item_count(unit_index);
    if (bag_item_count == 0) {
        return 0;
    }

    for (bag_slot = 0; bag_slot < bag_item_count; bag_slot++) {
        item_id = (int) actor->inventory_slots[bag_slot * 2 + 1];
        item = fdps_get_item_record(item_id);

        aim_min_dist = 0;
        aim_reach = (int) item->use_distance;
        if (aim_reach >= ITEM_USE_DISTANCE_LINE_BASE) {
            aim_reach = ITEM_SEARCH_LINE_AIM_REACH;
            aim_min_dist = ITEM_SEARCH_LINE_AIM_MIN_DIST;
        }
        if (item->use_effect == 0) {
            continue;
        }

        fdps_collect_targets_in_range(actor_x, actor_y, NULL, aim_reach,
                                      aim_min_dist, 0);
        tile_count = fdps_map_grid_collect_marked_tiles(tile_coords);
        fdps_map_grid_reset();

        for (tile_slot = 0; tile_slot < tile_count; tile_slot++) {
            aim_x = (int) tile_coords[tile_slot * 2];
            aim_y = (int) tile_coords[tile_slot * 2 + 1];
            if (side_select == 0) {
                target_select_mode = (item->use_target == 0);
            } else {
                target_select_mode = (int) item->use_target;
            }

            use_distance = (int) item->use_distance;
            if (use_distance < ITEM_USE_DISTANCE_LINE_BASE) {
                target_count =
                    fdps_collect_targets_in_range(aim_x, aim_y,
                                                  target_indices,
                                                  (int) item->use_radius, 0,
                                                  target_select_mode);
            } else {
                target_count =
                    fdps_collect_targets_in_line(aim_x, aim_y, target_indices,
                                                 actor_x, actor_y,
                                                 use_distance -
                                                 ITEM_USE_DISTANCE_LINE_BASE,
                                                 target_select_mode);
            }
            fdps_map_grid_reset();
            if (target_count == 0) {
                continue;
            }

            score_tier = fdps_score_targets_for_item(item_id, target_count,
                                                     target_indices);
            if (score_tier > data_fdps_battle_ai_best_item_score) {
                data_fdps_battle_ai_best_item_score = score_tier;
                data_fdps_map_ai_best_item_target_x = aim_x;
                data_fdps_battle_ai_best_item_target_y = aim_y;
                data_fdps_map_ai_best_item_bag_slot = bag_slot;
            }
        }
    }

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

/* The three ailment timers as status_timers[] indices, for the branches that
   read them through the field rather than through an offset argument.  [5] at
   record +0x27 is the magic seal 封魔咒術 leaves. */
#define POISON_TIMER_SLOT    3
#define PARALYSIS_TIMER_SLOT 4
#define SEAL_TIMER_SLOT      5

/* The one heap block fdps_map_actor_score_best_spell takes for its search:
   PUSH 0x190 at 0001349f, room for 200 (x, y) byte pairs.  The two stack
   arrays are the frame slots at [EBP-0x5c] and [EBP-0x7c]: 20 bytes for the
   spell-id list fdps_unit_collect_known_spells fills -- which is half the 40
   ids that function can write -- and 32 bytes for the unit indices
   fdps_collect_targets_in_range appends.  None of the three is bounds checked
   against what is written into it. */
#define SPELL_SEARCH_TILE_BYTES 0x190
#define SPELL_SEARCH_KNOWN_SPELL_IDS 20
#define SPELL_SEARCH_TARGET_BYTES 32

/* PROMAP.DAT row 0, the default class row whose eight terrain costs are all 1
   -- PUSH 0x0 at 00013436.  The cast range is therefore counted in walkable
   tiles rather than in the acting class's own movement costs. */
#define SPELL_SEARCH_DEFAULT_CLASS_ROW 0

/* 00013420.  Picks the best spell the actor at unit_index could cast this turn
   together with the tile to centre it on, and publishes that choice in the AI
   decision globals.  Always returns 0; all three callers discard it and read
   the globals instead.

   The score global is zeroed at 0001342c, before anything else, so a caller
   reads a fresh 0 even down the two early returns; the spell id and the two
   tile coordinates are only written inside the winning branch and keep the
   previous actor's decision otherwise.

   The class record is fetched once, for row 0 rather than the actor's own row,
   so the flood fill that spreads the cast range treats every walkable tile as
   costing 1.  Both early returns are at 00013493: an actor that knows no
   spells, and an actor whose status_timers[5] -- record +0x27, the timer
   封魔咒術 leaves -- is still running.

   Per known spell, the MP cost byte at record +0x05 is zero extended and
   compared against the actor's current MP with JG at 000134f1, so a spell that
   costs exactly what is left is still affordable.  The cast distance byte at
   +0x03 goes into fdps_move_grid_flood_fill_range RAW, straight-line flag
   0x10 and all (assets/tables/spells.md): the player's targeting path decodes
   that byte and this one does not.

   The select_mode handed to fdps_collect_targets_in_range comes from the
   spell's target byte at +0x06 and from side_select, at 0001356a: forwarded
   unchanged when side_select is non-zero, and inverted to (byte == 0) when it
   is 0, which is what turns a spell authored from the player's side around for
   an enemy caster.  The inversion is a boolean one, so the target byte of 3
   that 0x16 神行術 carries comes out of it as 0.

   The ranking at 000135ee is JG on the score, then, on an exact tie, JG on the
   spell's signed power word at record +0x00.  Power is signed and the eight
   絕招 store it as a negative attack multiplier, so on a tie an ordinary spell
   always outranks one of those.

   best_power is initialised here and is NOT in the original: [EBP-0x8] is only
   written inside the winning branch, so the tie-break at 0001360a reads an
   unwritten frame slot.  It cannot be observed.  The score global starts at 0
   and never falls, the tie branch leaves it alone, and so the slot is only
   ever consulted while the best score is still 0; the first candidate that
   scores above 0 goes through the JG path, which writes the slot without
   reading it.  Every consumer of the three published globals is gated on the
   score reaching 6.

   Nothing is bounds checked: not the tile count against the 200 pairs, not the
   target count against the 32 indices, not the spell count against the 20-byte
   list, and not the malloc against null. */
int fdps_map_actor_score_best_spell(int unit_index, int side_select)
{
    struct fdps_unit_record *actor;
    struct fdps_spell_effect *spell;
    struct fdps_class_record *default_class_move_cost;
    unsigned char *tile_coords;
    unsigned char known_spell_ids[SPELL_SEARCH_KNOWN_SPELL_IDS];
    unsigned char target_indices[SPELL_SEARCH_TARGET_BYTES];
    int known_spell_count;
    int spell_slot;
    int spell_id;
    int actor_x;
    int actor_y;
    int actor_mp;
    int tile_count;
    int tile_slot;
    int tile_x;
    int tile_y;
    int target_select_mode;
    int target_count;
    int score_tier;
    int best_power;

    data_fdps_battle_ai_best_spell_score = 0;
    default_class_move_cost =
        fdps_get_class_record(SPELL_SEARCH_DEFAULT_CLASS_ROW);
    actor = fdps_get_unit_record(unit_index);
    actor_x = (int) actor->pos_x;
    actor_y = (int) actor->pos_y;
    actor_mp = actor->mp_current;

    known_spell_count = fdps_unit_collect_known_spells(unit_index,
                                                       known_spell_ids);
    if (known_spell_count == 0 ||
        actor->status_timers[SEAL_TIMER_SLOT] != 0) {
        return 0;
    }

    best_power = 0;
    tile_coords = malloc(SPELL_SEARCH_TILE_BYTES);

    for (spell_slot = 0; spell_slot < known_spell_count; spell_slot++) {
        spell_id = (int) known_spell_ids[spell_slot];
        spell = fdps_get_spell_record(spell_id);
        if ((int) spell->mp_cost > actor_mp) {
            continue;
        }

        fdps_move_grid_flood_fill_range(default_class_move_cost,
                                        actor_x, actor_y,
                                        (int) spell->cast_range_flags);
        tile_count = fdps_map_grid_collect_marked_tiles(tile_coords);
        fdps_map_grid_reset();

        for (tile_slot = 0; tile_slot < tile_count; tile_slot++) {
            tile_x = (int) tile_coords[tile_slot * 2];
            tile_y = (int) tile_coords[tile_slot * 2 + 1];
            if (side_select == 0) {
                target_select_mode = (spell->target_side == 0);
            } else {
                target_select_mode = (int) spell->target_side;
            }
            target_count = fdps_collect_targets_in_range(tile_x, tile_y,
                                                         target_indices,
                                                         (int) spell->area,
                                                         0,
                                                         target_select_mode);
            fdps_map_grid_reset();
            if (target_count == 0) {
                continue;
            }

            score_tier = fdps_score_targets_for_spell(spell_id, target_count,
                                                      target_indices);
            if (score_tier > data_fdps_battle_ai_best_spell_score ||
                (score_tier == data_fdps_battle_ai_best_spell_score &&
                 spell->power > best_power)) {
                data_fdps_battle_ai_best_spell_score = score_tier;
                data_fdps_battle_ai_best_spell_target_x =
                    (unsigned int) tile_x;
                data_fdps_battle_ai_best_spell_target_y =
                    (unsigned int) tile_y;
                data_fdps_map_ai_best_spell_id = spell_id;
                best_power = spell->power;
            }
        }
    }

    free(tile_coords);
    return 0;
}

/* The MAGICDAT.DAT spell ids fdps_score_targets_for_spell weighs by name.  The
   healing span is a range test -- CMP 0xe / JL then CMP 0x10 / JLE at
   0001394b -- and every other id is an equality, so 0x0d and 0x11 are outside
   the span and only the exact ids below take their own branch.  Names from
   assets/spells.md. */
#define SPELL_QUAKE            0x0a  /* 裂地術 */
#define SPELL_GREAT_QUAKE      0x0b  /* 封神裂震 */
#define SPELL_HEAL_FIRST       0x0e  /* 恢復之光 */
#define SPELL_HEAL_LAST        0x10  /* 痊癒之泉 */
#define SPELL_SEAL_MAGIC       0x11  /* 封魔咒術 */
#define SPELL_POISON           0x12  /* 腐毒術 */
#define SPELL_PARALYSE         0x13  /* 麻痺術 */
#define SPELL_BLESSING         0x14  /* 神之祝福 */
#define SPELL_CURE_AILMENTS    0x18  /* 甦癒術 */
#define SPELL_REQUIEM          0x21  /* 鎮魂之歌 */

/* The record offsets fdps_score_targets_for_spell hands
   fdps_score_targets_without_status, which takes the timer as an offset and not
   as a field.  All five are inside struct fdps_unit_record's status_timers[]:
   [0] scales ap at 00024e38, [1] scales dp at 00024e52 and [2] adds 15 to the
   dx figure at 00024dac, all three inside fdps_unit_recompute_combat_stats, so
   they are 神之祝福's attack, defence and dexterity blessings in that order;
   [3] and [4] are the poison and paralysis counters unitatk.c writes. */
#define STATUS_OFFSET_BLESS_AP  0x22
#define STATUS_OFFSET_BLESS_DP  0x23
#define STATUS_OFFSET_BLESS_DX  0x24
#define STATUS_OFFSET_POISON    0x25
#define STATUS_OFFSET_PARALYSIS 0x26

/* The per-target scores the spell branches pay, as the PUSHes and the ADD
   immediates spell them: 8 and 3 for the two heal tiers at 000139c3 and
   000139de, 6 at 00013a71 and 00013af3, 0x0a and 4 in the PUSH pairs at
   00013a82 and 00013b26, and 0x18 against 8 for the kill and the wound at
   00013bd7 and 00013be0. */
#define SPELL_SCORE_BADLY_HURT   8
#define SPELL_SCORE_HURT         3
#define SPELL_SCORE_STATUS       6
#define SPELL_SCORE_AILMENT      0x0a
#define SPELL_SCORE_BLESSING     4
#define SPELL_SCORE_KILL         0x18
#define SPELL_SCORE_WOUND        8

/* Bit 0 of the behaviour byte, which is what the heal branch tests with
   AND AL,0x1 at 000139f4.

   That byte is packed and each reader takes its own piece of it: AND AL,0xf at
   00010065 in fdps_map_actor_behavior_step takes the low nibble as an AI mode
   code, AND AL,0x40 at 00012c72 in fdps_map_actor_take_best_action and
   AND AL,0x80 at 00013380 in fdps_score_targets_for_item take single flags, and
   fdps_map_actor_behavior_step assigns the whole byte the literal 7 at 0001039a.
   So this bit is the bottom bit of the mode nibble and not a flag of its own,
   and what the mode it belongs to means is not settled here -- the mask is named
   for the bit it is rather than for a meaning that has not been established. */
#define AI_BEHAVIOR_MODE_BIT_0 0x01

/* FMUL double ptr [0x00061571] at 00013bf3, whose eight bytes are
   00 00 00 00 00 00 f8 3f -- 1.5 exactly. */
#define PROTAGONIST_WEIGHT 1.5

/* The character index of 蘭迪斯, tested as a byte at 00013bea. */
#define PROTAGONIST_CHAR_ID 0

/* 00013920.  Scores casting one spell over one candidate target list, so
   fdps_map_actor_score_best_spell can rank that spell against the others the
   caster knows.  Each branch sums a per-target score and the sum is returned.

   The spell record is fetched and its power word kept before the dispatch,
   MOVSX word ptr [EAX] at 00013945, so the figure is signed: MAGICDAT.DAT holds
   the eight attack-multiplier spells as a negated percentage (assets/spells.md)
   and a negative power can never be above a target's current HP, so every
   target of one of those scores the flat wound value.  The fetch happens even
   for the branches that never look at the power.

   The dispatch is an if / else-if chain in the id order 0x0e-0x10 and 0x21,
   then 0x11, 0x12, 0x18, 0x13, 0x14, then everything else -- the compare chain
   at 0001394b, 00013a12, 00013a7c, 00013a9e, 00013afe and 00013b20 in that
   order.  Only the first test is a range; the rest are equalities.

   The current-HP word at +0x40 is read with a DIFFERENT signedness in the two
   branches that read it, and that is behaviour and not spelling: XOR EAX,EAX /
   MOV AX at 00013996 in the healing branch against MOVSX at 00013bce in the
   damaging one.  A unit whose HP word had gone negative is therefore the most
   hurt thing on the map to a healer -- 0xffff is far above hp_max/2 unsigned,
   so it would score 0 read the other way -- and is already dead to an attacker.
   hp_max at +0x42 is zero extended alongside it.

   Both healing thresholds are signed divisions compared with JLE at 000139c1
   and 000139dc, so they are strictly greater-than against the current HP: a
   unit sitting on exactly a third of its maximum takes the 3 and not the 8.
   IDIV EBX with EBX = 3 and the SAR 0x1f / SUB / SAR 0x1 halving both truncate
   toward zero.

   The 0x11 branch resolves the record BEFORE it asks the count, and asks with a
   NULL buffer, which is fdps_unit_collect_known_spells' count-only mode; the
   test is that count against zero AND the seal timer against zero, short
   circuited at 00013a64, so a sealed target is worth nothing however many
   spells it knows.

   The 0x14 branch calls fdps_score_targets_without_status three times and
   STORES each answer over the last -- MOV dword ptr [EBP-0x14],EAX at 00013b3a,
   00013b51 and 00013b68, never ADD -- so the attack and defence blessing passes
   are computed and discarded and only the dexterity pass reaches the total.
   Writing the obvious += trebles 神之祝福's score and changes which spell the
   map AI picks.  The three plain assignments are the point.

   The flying test guards only the two ground-shock spells and it skips the
   target outright, accumulator untouched: CMP 0xb / JZ then CMP 0xa / JNZ at
   00013b8c short circuits to the body for every other id, and a nonzero answer
   jumps straight to the loop increment at 00013bb0.

   The protagonist weighting is FILD / FMUL 1.5 / CALL __CHP / FISTP, so it is a
   double multiply truncated back to an int and not the integer 3/2 the physical
   attack scorer uses at 000124bc.  The two agree on every value the branch can
   produce -- 8 becomes 12 and 0x18 becomes 0x24 either way -- but the operation
   is the one written here.

   Nothing is bounds checked: not spell_id against MAGICDAT.DAT's 40 records,
   not the index bytes against the unit array, and the record pointer is
   re-resolved on every pass rather than held across the loop. */
int fdps_score_targets_for_spell(int spell_id, int target_count,
                                 unsigned char *target_unit_indices)
{
    struct fdps_spell_effect *spell;
    struct fdps_unit_record *target;
    int spell_power;
    int target_slot;
    int hp_current;
    int hp_max;
    int score;
    int total;

    total = 0;
    spell = fdps_get_spell_record(spell_id);
    spell_power = spell->power;

    if ((spell_id >= SPELL_HEAL_FIRST && spell_id <= SPELL_HEAL_LAST) ||
        spell_id == SPELL_REQUIEM) {
        for (target_slot = 0; target_slot < target_count; target_slot++) {
            target = fdps_get_unit_record(target_unit_indices[target_slot]);
            hp_current = (int) (unsigned short) target->hp_current;
            hp_max = (int) (unsigned short) target->hp_max;
            if (hp_max / 3 > hp_current) {
                score = SPELL_SCORE_BADLY_HURT;
            } else if (hp_max / 2 > hp_current) {
                score = SPELL_SCORE_HURT;
            } else {
                score = 0;
            }
            if ((target->ai_behavior & AI_BEHAVIOR_MODE_BIT_0) != 0) {
                score = score * 2;
            }
            total = total + score;
        }
    } else if (spell_id == SPELL_SEAL_MAGIC) {
        for (target_slot = 0; target_slot < target_count; target_slot++) {
            target = fdps_get_unit_record(target_unit_indices[target_slot]);
            if (fdps_unit_collect_known_spells(
                    target_unit_indices[target_slot], NULL) != 0 &&
                target->status_timers[SEAL_TIMER_SLOT] == 0) {
                total = total + SPELL_SCORE_STATUS;
            }
        }
    } else if (spell_id == SPELL_POISON) {
        total = fdps_score_targets_without_status(target_count,
                                                  target_unit_indices,
                                                  STATUS_OFFSET_POISON,
                                                  SPELL_SCORE_AILMENT);
    } else if (spell_id == SPELL_CURE_AILMENTS) {
        for (target_slot = 0; target_slot < target_count; target_slot++) {
            target = fdps_get_unit_record(target_unit_indices[target_slot]);
            if (target->status_timers[POISON_TIMER_SLOT] != 0 ||
                target->status_timers[PARALYSIS_TIMER_SLOT] != 0 ||
                target->status_timers[SEAL_TIMER_SLOT] != 0) {
                total = total + SPELL_SCORE_STATUS;
            }
        }
    } else if (spell_id == SPELL_PARALYSE) {
        total = fdps_score_targets_without_status(target_count,
                                                  target_unit_indices,
                                                  STATUS_OFFSET_PARALYSIS,
                                                  SPELL_SCORE_AILMENT);
    } else if (spell_id == SPELL_BLESSING) {
        total = fdps_score_targets_without_status(target_count,
                                                  target_unit_indices,
                                                  STATUS_OFFSET_BLESS_AP,
                                                  SPELL_SCORE_BLESSING);
        total = fdps_score_targets_without_status(target_count,
                                                  target_unit_indices,
                                                  STATUS_OFFSET_BLESS_DP,
                                                  SPELL_SCORE_BLESSING);
        total = fdps_score_targets_without_status(target_count,
                                                  target_unit_indices,
                                                  STATUS_OFFSET_BLESS_DX,
                                                  SPELL_SCORE_BLESSING);
    } else {
        for (target_slot = 0; target_slot < target_count; target_slot++) {
            if ((spell_id == SPELL_GREAT_QUAKE ||
                 spell_id == SPELL_QUAKE) &&
                fdps_unit_is_flying(target_unit_indices[target_slot]) != 0) {
                continue;
            }
            target = fdps_get_unit_record(target_unit_indices[target_slot]);
            if (target->hp_current < spell_power) {
                score = SPELL_SCORE_KILL;
            } else {
                score = SPELL_SCORE_WOUND;
            }
            if (target->char_id == PROTAGONIST_CHAR_ID) {
                score = (int) (score * PROTAGONIST_WEIGHT);
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
