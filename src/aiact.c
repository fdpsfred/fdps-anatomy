/* aiact.c -- the map AI's action layer: the routines that carry out the choice
 * src/aiscore.c has already scored, once the behaviour dispatcher in
 * src/mapai.c has decided that choice is worth taking.
 *
 * Nothing here decides anything.  The tile, the target, the spell and the bag
 * slot all arrive in the globals the matching search published (gamedata.h),
 * so an action run without its search first is carrying out a stale decision.
 *
 * See aiact.h for what each entry point promises.
 */
#include <i86.h>
#include "gamedata.h"
#include "aitarget.h"
#include "cmbspell.h"
#include "combat.h"
#include "death.h"
#include "gauge.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "movegrid.h"
#include "spell.h"
#include "table.h"
#include "unit.h"
#include "unitatk.h"
#include "unitstat.h"
#include "aiact.h"

/* The two values this file stores into data_fdps_map_cursor_draw_mode
   (gamedata.h): 0 at 00012e5c and 00012ed9 so nothing is painted over the walk
   or over the frame that opens the fight, and 1 at 00012e8e and 00012ee8 for
   the plain cursor box.  Both stores of 1 are plain stores and not restores --
   MOV dword ptr [0x00069cd0],0x1 with the entry value never read -- so a
   caller that was in any other mode comes back in the box mode. */
#define CURSOR_DRAW_MODE_HIDDEN 0
#define CURSOR_DRAW_MODE_BOX 1

/* PUSH 0x64 at 00012eb8: the tenth of a second the actor is held facing its
   target before the fight starts. */
#define FACE_TARGET_HOLD_MS 100

/* CMP EAX,0x1 at 00012f04 and 00012f6c.  fdps_check_can_counter_attack answers
   1 or -1 and never 0 (aitarget.h), so this is an equality against 1 and not a
   truth test: every defender that may NOT strike back would pass a truth
   test. */
#define COUNTER_ATTACK_ALLOWED 1

/* ADD EAX,0x8 at 00012fb0 on the int pointer the gauge call returned, which is
   two ints along: pair 0 of data_fdps_battle_combat_gauge_pos_pairs is where
   the actor's blow paints its bar and pair 1 where the counterblow paints
   its (gauge.h). */
#define COUNTER_GAUGE_PAIR_AT 2

/* The stack buffer fdps_collect_death_scripts fills, 0x0c bytes at [EBP-0x18]:
   four 3-byte records.  That collector is told no capacity and checks none, so
   the size is a contract with it and not a local choice (death.h). */
#define DEATH_SCRIPT_BUFFER_BYTES 12

/* IMUL ...,0xf then IDIV by 10 at 00013004, both signed. */
#define XP_CREDIT_NUMERATOR 15
#define XP_CREDIT_DENOMINATOR 10

/* CMP dword ptr [0x00063f88],0x6 / JL at 00013cd9: the tier the spell search's
   score has to reach before the cast is carried out.  Signed, and the same
   threshold the three searches are compared against elsewhere (gamedata.h). */
#define SPELL_ACTION_SCORE_FLOOR 6

/* The stack array fdps_collect_targets_in_range fills, 0x20 bytes at
   [EBP-0x40]: one unit index per byte.  That collector is told no capacity and
   checks none, so the size is a contract with it (aitarget.h). */
#define SPELL_TARGET_BUFFER_BYTES 32

/* PUSH 0x0 at 00013cf6: the min_dist argument.  Nothing inside the blast is
   excluded, so the whole diamond the reach byte spreads is collected. */
#define SPELL_TARGET_MIN_DISTANCE 0

/* The stack buffer fdps_collect_death_script_events fills, 0x0c bytes at
   [EBP-0x20]: four 3-byte records, and the same uncapped contract as the
   attack path's buffer above (death.h). */
#define DEATH_EVENT_BUFFER_BYTES 12

/* ADD EAX,0x2 at 00013d2f on the spell record's reach byte.  Reaches 1, 2 and
   3 land on fdps_draw_map_cursor's radius-1, radius-2 and radius-3 diamonds --
   overlay modes 3, 4 and 5 -- so the area of effect is shown while the cursor
   travels to the tile (mapcur.h).  MAGICDAT.DAT's forty records carry reaches
   0, 1, 2, 3, 6 and 8 and no other (assets/spells.md), so the modes this store
   can produce are 2, 3, 4, 5, 8 and 10: reach 0 shows the plain cursor and the
   two ground shocks show nothing at all, and the mode-6 arm -- the one that
   clears the movement grid's marker byte under the cursor as a side effect of
   drawing -- is out of reach from here, because no record has a reach of 4. */
#define CURSOR_BLAST_MODE_BIAS 2

/* IMUL by 0x18 at 00013d37 and 00013d3f: a map tile is 24 pixels square, so
   the two tile coordinates are scaled into the pixel coordinates
   fdps_map_cursor_move_to scrolls to. */
#define MAP_TILE_SIZE 0x18

/* PUSH 0xc8 at 00013d4f: the fifth of a second the blast outline is held over
   the target tile before the spell is played. */
#define CAST_AIM_HOLD_MS 200

/* CMP EAX,0x1 at 00013d72, on the animation flag widened out of its byte.
   This is an equality against 1 and not a truth test: any other value takes
   the on-map route, so a byte of 2 would go to the full-screen presentation
   under `if (flag)` and does not here (gamedata.h). */
#define BATTLE_ANIMATION_ON 1

/* CMP byte ptr [EAX + 0x6],0x0 at 00013cb6: the authored target side the
   enemy phase inverts against. */
#define SPELL_TARGET_SIDE_PLAYER 0

/* Walks the actor onto the tile the attack search picked and fights the unit
   it picked there.  aiact.h carries what the whole sequence promises; what is
   worth saying beside the code is why three of these lines are spelled the way
   they are.

   THE TWO COUNTER-ATTACK TESTS ARE EQUALITIES AGAINST 1.  See
   COUNTER_ATTACK_ALLOWED above: the natural truth test turns every defender
   that may not strike back into one that does.

   THE SECOND TEST IS A BITWISE AND AND NOT A SHORT-CIRCUIT.  Both operands are
   evaluated -- the swing's answer at 00012f46 lands in [EBP-0x20] and the
   second fdps_check_can_counter_attack is called at 00012f64 whatever that
   answer was, its own 0/1 landing in [EBP-0x1c], and only then are the two
   ANDed at 00012f84.  Writing && would skip the second call on a dead target.

   THE EXPERIENCE GOES TO THE TARGET.  PUSH dword ptr [0x00063f74] at 0001301c,
   the unit that was attacked, not the actor at [EBP+0x14]: this is how a player
   unit earns experience for surviving an enemy turn. */
int fdps_map_actor_move_and_attack(int unit_index, int side_select)
{
    /* Where the two HP bars were put, as the gauge call's own array of two
       {x, y} pairs: pair 0 is the bar the actor's blow drains and pair 1 the
       bar the counterblow drains (gauge.h). */
    int *gauge_positions;
    /* What the actor's swing left the target on, 0 when the target died. */
    int target_hp_left;
    /* The 3-byte on-death records of every unit the exchange killed. */
    unsigned char death_scripts[DEATH_SCRIPT_BUFFER_BYTES];
    /* How many of those records were collected. */
    int death_script_count;

    data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
    fdps_map_cursor_move_to_unit(unit_index);
    fdps_battle_move_unit_toward(data_fdps_battle_ai_best_physical_target_x,
                                 data_fdps_battle_ai_best_attack_tile_y,
                                 unit_index, side_select);

    data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_BOX;
    fdps_map_cursor_move_to_unit(data_fdps_battle_ai_best_physical_target_idx);
    fdps_unit_face_target(unit_index,
                          data_fdps_battle_ai_best_physical_target_idx);
    delay((unsigned int) FACE_TARGET_HOLD_MS);

    data_fdps_battle_pending_xp_credit = 0;

    if (data_fdps_ui_battle_animation_enabled == 0) {
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_render_view_frame();
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_BOX;

        if (fdps_check_can_counter_attack(
                unit_index, data_fdps_battle_ai_best_physical_target_idx)
                == COUNTER_ATTACK_ALLOWED) {
            fdps_unit_face_target(data_fdps_battle_ai_best_physical_target_idx,
                                  unit_index);
        }

        gauge_positions = fdps_battle_show_combat_gauges(
            unit_index, data_fdps_battle_ai_best_physical_target_idx);
        target_hp_left = fdps_unit_attack_target(
            unit_index, data_fdps_battle_ai_best_physical_target_idx,
            gauge_positions);

        if ((target_hp_left != 0)
                & (fdps_check_can_counter_attack(
                       unit_index,
                       data_fdps_battle_ai_best_physical_target_idx)
                   == COUNTER_ATTACK_ALLOWED)) {
            fdps_unit_face_target(
                unit_index, data_fdps_battle_ai_best_physical_target_idx);
            fdps_unit_face_target(data_fdps_battle_ai_best_physical_target_idx,
                                  unit_index);
            fdps_unit_attack_target(
                data_fdps_battle_ai_best_physical_target_idx, unit_index,
                gauge_positions + COUNTER_GAUGE_PAIR_AT);
        }
    } else {
        fdps_combat_play_attack_exchange(
            unit_index, data_fdps_battle_ai_best_physical_target_idx);
    }

    death_script_count = fdps_collect_death_scripts(death_scripts);
    fdps_play_death_animation_and_mark_dead();
    fdps_run_death_scripts(data_fdps_battle_ai_best_physical_target_idx,
                           death_script_count, death_scripts);

    data_fdps_battle_pending_xp_credit =
        data_fdps_battle_pending_xp_credit * XP_CREDIT_NUMERATOR
        / XP_CREDIT_DENOMINATOR;
    fdps_unit_award_exp_and_level_up(
        data_fdps_battle_ai_best_physical_target_idx);

    return 1;
}

/* Casts the spell the map AI's spell search chose, at the tile it chose.
   aiact.h carries what the whole sequence promises; what is worth saying
   beside the code is why four of these lines are spelled the way they are.

   THE AUTHORED TARGET SIDE IS INVERTED ON THE ENEMY PHASE.  With side_select 0
   the filter handed to fdps_collect_targets_in_range is `record byte +6 == 0`
   and not the byte itself, so a spell authored to hit side 0 becomes one that
   hits every non-zero side and every other authored value collapses to 0: an
   enemy caster aims a player-facing spell at the player and a friendly-facing
   one at its own side.  With side_select non-zero -- the NPC phase, whose
   units already stand on the player's side -- the byte is used unchanged.
   Writing the byte through on both arms aims the enemy phase's offensive
   spells at the enemy's own ranks.

   THE ANIMATION FLAG IS TESTED FOR EQUALITY WITH 1.  See BATTLE_ANIMATION_ON
   above: the natural truth test sends every value but 0 to the full-screen
   presentation, where the original sends every value but 1 to the map.

   THE GRID IS RESET AFTER THE COLLECT AND NOT BEFORE.
   fdps_collect_targets_in_range wants the grid arriving at the 0xff sentinel
   and leaves it dirty, and putting it back is the caller's half of that
   contract (aitarget.h, movegrid.h).

   THE DEATH SCRIPTS ARE RUN FOR data_fdps_battle_ai_best_physical_target_idx.
   That global is the melee target the ATTACK search picked (gamedata.h) --
   neither the caster nor any unit this spell killed, and on the
   fdps_map_actor_behavior_step path a leftover from an earlier actor, because
   the attack search has not run this turn.  It reaches the chapter event
   handler table as the unit index of an opcode-2 record, so the obvious
   reading -- the caster, which is what fdps_battle_spell_command passes at its
   own matching call at 000280b1 -- hands those handlers a different unit.
   PUSH dword ptr [0x00063f74] at 00013dce is what the original does. */
int fdps_map_actor_cast_chosen_spell(int unit_index, int side_select)
{
    /* The chosen spell's MAGICDAT.DAT record, read for the blast radius at
       +0x04 and the authored target side at +0x06. */
    struct fdps_spell_effect *spell;
    /* The unit indices the blast covers, one per byte, as the collector
       appends them. */
    unsigned char targets[SPELL_TARGET_BUFFER_BYTES];
    /* How many of those slots it filled. */
    int target_count;
    /* The 3-byte on-death event records of every unit the spell killed. */
    unsigned char death_events[DEATH_EVENT_BUFFER_BYTES];
    /* How many of those records were collected. */
    int death_event_count;
    /* The select_mode the collector is asked for: the record's target side,
       inverted on the enemy phase. */
    int target_side_filter;
    /* 1 once the spell has been cast, 0 when the score gate refused it. */
    int cast_done;

    spell = fdps_get_spell_record(data_fdps_map_ai_best_spell_id);
    if (side_select == 0) {
        target_side_filter = (spell->target_side == SPELL_TARGET_SIDE_PLAYER);
    } else {
        target_side_filter = (int) spell->target_side;
    }

    if (data_fdps_battle_ai_best_spell_score < SPELL_ACTION_SCORE_FLOOR) {
        cast_done = 0;
    } else {
        fdps_map_cursor_move_to_unit(unit_index);

        target_count = fdps_collect_targets_in_range(
            (int) data_fdps_battle_ai_best_spell_target_x,
            (int) data_fdps_battle_ai_best_spell_target_y, targets,
            (int) spell->area, SPELL_TARGET_MIN_DISTANCE, target_side_filter);
        fdps_map_grid_reset();

        data_fdps_map_cursor_draw_mode =
            (int) spell->area + CURSOR_BLAST_MODE_BIAS;
        fdps_map_cursor_move_to(
            (int) (data_fdps_battle_ai_best_spell_target_x * MAP_TILE_SIZE),
            (int) (data_fdps_battle_ai_best_spell_target_y * MAP_TILE_SIZE));
        delay((unsigned int) CAST_AIM_HOLD_MS);
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_render_view_frame();

        if (data_fdps_ui_battle_animation_enabled == BATTLE_ANIMATION_ON) {
            fdps_combat_play_spell_on_targets(unit_index,
                                              data_fdps_map_ai_best_spell_id,
                                              target_count, targets);
        } else {
            fdps_cast_spell_on_targets(unit_index,
                                       data_fdps_map_ai_best_spell_id,
                                       target_count, targets);
        }

        death_event_count = fdps_collect_death_script_events(death_events);
        fdps_render_view_frame();
        fdps_play_death_animation_and_mark_dead();
        fdps_run_death_scripts(data_fdps_battle_ai_best_physical_target_idx,
                               death_event_count, death_events);

        data_fdps_battle_pending_xp_credit = 0;
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        cast_done = 1;
    }

    return cast_done;
}
