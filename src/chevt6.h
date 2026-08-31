/* chevt6.h -- the scripted chapter-event handlers of chapters 28 to 30.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and most
 * handlers of the family ignore it -- these later chapters are where that stops
 * being true, because a tile trigger fires for whichever unit walked over the
 * tile and the handler has to decide whether that unit was one of the party's.
 *
 * chevt1.h holds the same family for chapters 2 to 7, chevt2.h for 8 to 14 and
 * chevt3.h for 15 to 19.  Nothing here owns state.
 */
#ifndef CHEVT6_H
#define CHEVT6_H

/* Chapter 29's mid-map ambush trigger: it takes unit indices 0x24 through 0x59
   inclusive off the hold-position behaviour the map deploys them in and puts
   them on the default one, which paths a unit toward the nearest opposing unit,
   so the enemy groups holding the right, the lower right and the upper middle
   of the map stop guarding their ground and start hunting the party.

   It runs only for a unit on the player side.  The record named by unit_index is
   fetched first and its side byte at record offset 6 compared for equality with
   2 -- the player side, against 0 for the enemy and 1 for the neutral one -- and
   anything else returns having changed nothing.  So an enemy or neutral unit
   crossing the same trigger tile does not spring the ambush.  This is the one
   place in the handler the argument reaches: it is not range checked and the
   record is resolved through fdps_get_unit_record, so an index outside the live
   array reads whatever lies at that stride.

   Like the rest of the family it rewrites only the low nibble -- the behaviour
   code -- of the ai_behavior byte at record offset 0x34 to 0 across the range,
   and leaves the high nibble alone, because bits 0x40 and 0x80 of it are
   independent AI flags fdps_map_actor_take_best_action and
   fdps_score_targets_for_item read on their own.  Both bounds are literals;
   nothing is range checked and data_fdps_map_unit_count is not consulted, and
   each record is resolved through fdps_get_unit_record per iteration, so the
   array base is re-read.

   The indices are only meaningful against chapter 29's own deployment.
   map28.dat declares 12 player slots and 80 scenario units, so unit indices
   0x00..0x0b are the party and 0x0c..0x5b are deployment records 0..79, and the
   range 0x24..0x59 is records 24..77.  Records 24..59 are the 36 units this
   event releases -- they carry behaviour code 2, hold position, at deployment
   record offset 0x11 -- and records 60..77 already carry 0 and have been
   advancing since the first turn, so the range covers them without changing
   anything.  The top bound stops one index short of the two level 30 Guardian
   Dragons at records 78 and 79, which also hold position and are left for the
   map's other trigger, slot 46, to release; records 0..23 hold position too and
   are equally untouched.

   There is no one-shot latch: nothing in the body guards the loop and nothing
   records that it ran, so calling it again runs it again.  The merge is
   idempotent, so a second firing changes nothing, but a unit the AI has since
   moved into another behaviour mode would be pushed back to mode 0.  What makes
   it fire once in play is the hand-off slot: fdps_map_set_pending_tile_event
   arms it and the turn driver resets it to 0xff before the next unit acts.

   Table slot 45, and chapter 29's map28.dat is the only shipped file that names
   it -- as tile-trigger entry 1, the two bytes at file offset 0x33, {slot 45,
   pass-mode 0}.  Pass-mode 0 is what the four single-tile movement steppers
   fdps_walk_step_down, fdps_animate_move_step_up, fdps_animate_move_step_left
   and fdps_animate_move_step_right report, while fdps_battle_unit_turn and
   fdps_map_actor_behavior_step report 1, so the trigger fires as a unit walks
   across the tile rather than when it comes to rest on it. */
extern void fdps_chapter_29_event_activate_enemy_groups(int unit_index);
#pragma aux fdps_chapter_29_event_activate_enemy_groups "*" parm caller [];

#endif
