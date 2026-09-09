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
#include "combat.h"
#include "death.h"
#include "gauge.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "movegrid.h"
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
