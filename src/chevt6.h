/* chevt6.h -- the scripted chapter-event handlers of chapters 28 to 30.
 *
 * Most entry points here are slots of the chapter-event handler table at
 * 000601c4, so they share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and most
 * handlers of the family ignore it -- these later chapters are where that stops
 * being true, because a tile trigger fires for whichever unit walked over the
 * tile and the handler has to decide whether that unit was one of the party's.
 *
 * The exception is fdps_chapter_30_revive_wave_4_undead at the bottom, which is
 * not in the table at all: it is called straight out of the AI's behaviour-mode
 * 11 branch and takes no argument.  It is here because it is chapter 30's
 * scripted behaviour and belongs with that chapter's other handlers.
 *
 * chevt1.h holds the same family for chapters 2 to 7, chevt2.h for 8 to 14 and
 * chevt3.h for 15 to 19.  Nothing here owns state.
 */
#ifndef CHEVT6_H
#define CHEVT6_H

/* Chapter 28's turn-scheduled reinforcement event: brings on the wave of the
   current map's deployment table that the turn just played is due, then pans the
   view up to the spawn point so the player sees the arrival.

   The wave asked for is the battle turn counter halved by a plain signed
   integer divide, so it truncates toward zero and two consecutive turns select
   the same wave.  The counter is read while it still names the turn whose
   player phase has just ended, because fdps_battle_run_turn_events dispatches
   the phase-0 events before fdps_battle_advance_turn raises it.

   Which turns reach this slot is map27.dat's turn-event table -- 2, 4, 6, 7, 10,
   12, 14, 16 and 18 -- so the waves it deploys are 1, 2, 3, 3, 5, 6, 7, 8 and 9:
   turn 7 halves down onto wave 3 again, so map27.dat's wave 3 arrives twice and
   its wave 4 never arrives at all.  The units are placed on the nearest free
   walkable tile to their scripted spawn point rather than on it exactly.

   The arrival is then shown: the map cursor's draw mode is parked at 0 so
   nothing of the cursor is painted, the view is walked to map pixel (0x120, 0)
   -- tile (12, 0), the rightmost of the three spawn tiles map27.cod names -- and
   held there for twelve frames, and the draw mode is left on 1.  That 1 is a
   fixed value and not the mode the call found, so whatever a caller had parked
   there is lost.

   unit_index is the handler table's shared argument.  This handler writes 0 over
   the incoming slot before anything else and reuses it as the frame counter, so
   nothing about the acting unit reaches the wave, the map or the pan; the only
   dispatcher that reaches this slot pushes a literal 0 anyway. */
extern void fdps_chapter_28_event_deploy_wave_for_turn(int unit_index);
#pragma aux fdps_chapter_28_event_deploy_wave_for_turn "*" parm caller [];

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

/* Chapter 29's final ambush trigger, and the companion of the handler above: it
   releases every unit map28.dat deploys, and it does not release them all the
   same way.  Unit indices 0x0c through 0x59 inclusive go on the behaviour code
   that paths a unit toward the nearest opposing unit from anywhere on the map,
   and unit indices 0x5a and 0x5b -- the two level 30 Guardian Dragons the
   handler above deliberately leaves out of its range -- go on behaviour code
   0x0a instead, which scores an item and uses it when the score reaches 6 and
   then still takes an attack in the same step, and which never reaches the
   raw-distance pathing, so a unit in it works from where it stands.  The two
   ranges are disjoint -- 0x0c..0x59 and 0x5a..0x5b share no index -- so every
   record is written exactly once and the order the two loops run in carries no
   meaning.

   Like the handler above it runs only for a unit on the player side.  The
   record named by unit_index is fetched first and its side byte at record
   offset 6 compared for equality with 2 -- the player side, against 0 for the
   enemy and 1 for the neutral one -- and anything else returns having changed
   nothing, so an enemy or neutral unit crossing the same trigger tile does not
   spring the ambush.  This is the only place the argument reaches: it is not
   range checked and the record is resolved through fdps_get_unit_record, so an
   index outside the live array reads whatever lies at that stride.

   Both ranges rewrite only the low nibble -- the behaviour code -- of the
   ai_behavior byte at record offset 0x34 and leave the high nibble alone,
   because bits 0x40 and 0x80 of it are independent AI flags
   fdps_map_actor_take_best_action and fdps_score_targets_for_item read on their
   own.  All four bounds are literals; nothing is range checked and
   data_fdps_map_unit_count is not consulted, and each record is resolved
   through fdps_get_unit_record per iteration, so the array base is re-read.

   The indices are only meaningful against chapter 29's own deployment.
   map28.dat declares 12 player slots and 80 scenario units, so unit indices
   0x00..0x0b are the party and 0x0c..0x5b are deployment records 0..79: the
   first range is records 0..77 and the second is records 78 and 79, so between
   them they cover all 80 with each record written exactly once.  Records
   0..59 and 78..79 carry behaviour code 2, hold position, at deployment record
   offset 0x11, and records 60..77 already carry 0 and have been advancing since
   the first turn, so the first range covers them without changing anything.
   Together
   with slot 45, which releases records 24..77 alone, this is what holds the 24
   units of records 0..23 and both dragons until a party unit reaches this
   handler's tile.

   There is no one-shot latch: nothing in the body guards the loops and nothing
   records that they ran, so calling it again runs them again.  Both merges are
   idempotent, so a second firing changes nothing, but a unit the AI has since
   moved into another behaviour mode would be pushed back.  What makes it fire
   once in play is the hand-off slot: fdps_map_set_pending_tile_event arms it and
   the turn driver resets it to 0xff before the next unit acts.

   Table slot 46, and chapter 29's map28.dat is the only shipped file that names
   it -- as tile-trigger entry 2, the two bytes at file offset 0x35, {slot 46,
   pass-mode 0}.  Pass-mode 0 is what the four single-tile movement steppers
   report, while fdps_battle_unit_turn and fdps_map_actor_behavior_step report 1,
   so the trigger fires as a unit walks across the tile rather than when it comes
   to rest on it. */
extern void fdps_chapter_29_event_activate_all_enemies(int unit_index);
#pragma aux fdps_chapter_29_event_activate_all_enemies "*" parm caller [];

/* Chapter 30's reinforcement ambush: the first unit to finish a step onto the
   map's trigger tile brings on the four enemies the chapter's map tags as wave
   4, the view is panned over both of the places they arrive, and the chapter's
   line about them is spoken.  It happens once per chapter and there is no side
   test in front of it -- any unit that walks over the tile springs it.

   The gate is the shared one-shot latch, element 0x10 of
   data_fdps_map_cell_event_triggered_flags (gamedata.h), and it has to still be
   0 for anything at all to happen.  The latch is raised last, after the line has
   been spoken.  It is not this handler's private flag: the whole 32-byte block
   is cleared at every chapter start and restored from a savegame, which is what
   makes the ambush fire once per chapter rather than once per process.

   The deployment is fdps_deploy_wave (deploy.h) with wave 4 and place_exact 0,
   so the arrivals settle on the nearest free walkable tile to their MAP%02d.COD
   placement records rather than on the records' own coordinates.  The map
   number is read from data_fdps_chapter_current_chapter_id (gamedata.h) at the
   call and is not anything this handler holds, and the wave number is a literal
   -- the battle turn counter is not read anywhere in the body, so the same four
   units arrive whenever the tile is crossed.

   The arrival is then shown twice.  data_fdps_map_cursor_draw_mode is parked at
   0 so no cursor is painted for the whole sequence, the view is walked to map
   pixel (0x60, 0x150) -- tile (4, 14), beside the lower-left arrival point --
   and held there for twelve frames, then to map pixel (0x198, 0x150) -- tile
   (17, 14), beside the lower-right one -- and held for twelve more.  Each held
   frame costs one timer tick inside fdps_render_view_frame (mapdraw.h), so the
   count is how long the view rests on each group.  The draw mode is then left
   on the literal 1, the plain cursor box, and not the mode the call found.

   Nothing in the body tests whether the deployment found anything, so a map
   with no wave-4 record still blanks the cursor, runs both pans and speaks the
   line.

   The line is text entry 8 of the loaded chapter's block, through
   fdps_draw_text (text.h) straight onto the mode 13h aperture in the standard
   message colours.  Its result is discarded.

   unit_index is the handler table's shared parameter and this handler ignores
   it: the slot is overwritten with 0 as each of the two hold loops starts and
   is never read as an argument.  In the shipped data it is the index of the
   unit that stepped onto the trigger tile.

   Table slot 47, and chapter 30's map29.dat is the only shipped file that names
   it -- as the tile-event entry for cell event code 1, {slot 47, occasion 0}.
   Occasion 0 is what fdps_map_set_pending_tile_event reports as a unit finishes
   stepping onto a tile during movement, which is why a unit teleported onto the
   tile does not spring it. */
extern void fdps_chapter_30_event_deploy_wave_4(int unit_index);
#pragma aux fdps_chapter_30_event_deploy_wave_4 "*" parm caller [];

/* Chapter 30's "the first form has fallen" event: it speaks a line, opens the
   map up, brings on the second form of 平衡之神 and speaks a second line.
   Nothing in it is conditional and nothing it does is guarded.

   The line before is entry 0x0f of the loaded chapter's text block and the line
   after is entry 0x10, both through fdps_draw_text (text.h) straight onto the
   mode 13h aperture in the standard message colours.  Both results are
   discarded, and because both are drawn at the same origin the second lands over
   the first.

   Between them is the terrain change, and it is two steps that only work
   together.  Map cell event code 2 is marked in
   data_fdps_map_cell_event_triggered_flags (gamedata.h) and
   fdps_map_apply_triggered_cell_changes (maptile.h) is then called, which walks
   every cell of the loaded map and, on each searchable cell whose event code is
   marked, bumps the cell's tile id by one and clears its byte in the event-code
   layer.  The mark is what persists -- it is saved and restored with the game
   and cleared at every chapter start -- and the call is what makes it visible.

   The deployment is wave 2 of the current map, fdps_deploy_wave (deploy.h) with
   the map number read out of data_fdps_chapter_current_chapter_id, the wave
   number as the literal 2 and the placement flag as 1.  The literal wave is what
   separates this handler from the turn-scheduled reinforcement handlers, which
   push the battle turn counter instead; the placement flag of 1 puts the unit on
   the tile its MAP%02d.COD record names exactly, with no search for a free
   walkable tile, which is how the second form appears where the first one stood.

   Nothing guards any of it: reaching the handler twice speaks the lines twice
   and appends a second copy of the wave.  What makes the form arrive once in
   play is that a unit's death script is collected once, before the unit is
   marked removed.

   unit_index is the handler table's shared parameter and this handler ignores
   it: the incoming slot is overwritten with 0 on entry and never read.  In the
   shipped data it is the index of the unit whose death ran the script.

   Table slot 48, and the only thing that names it in the shipped data is the
   death script of map29.dat's deployment record 0 -- the level 40 character id
   60 that is the first form of 平衡之神 -- whose wave 2 is the single record
   carrying its second form. */
extern void fdps_chapter_30_event_deploy_wave_2(int unit_index);
#pragma aux fdps_chapter_30_event_deploy_wave_2 "*" parm caller [];

/* Chapter 30's "the second form has fallen" event: one call and nothing else.
   It deploys wave 3 of the current map -- fdps_deploy_wave with the map number
   read out of data_fdps_chapter_current_chapter_id, the wave number as the
   literal 3 and the placement flag as 1 -- which brings the third and final form
   of the chapter's boss onto the battle map.

   The wave number being a literal is what separates this handler from the
   turn-scheduled reinforcement handlers of the same family: they push the battle
   turn counter, so the group they bring on depends on when they fire, and this
   one always brings the same group.

   The placement flag of 1 puts every unit of the wave on the tile its
   MAP%02d.COD placement record names, exactly -- no search for a free walkable
   tile and no test of what is standing there -- which is how a scripted arrival
   lands where the script put it.

   Nothing guards the call: it deploys the wave every time it is reached, and a
   second firing would append a second copy of it.  What makes the form arrive
   once in play is that a unit's death script is collected once, before the unit
   is marked removed.

   unit_index is the handler table's shared parameter and this handler ignores
   it: the incoming slot is overwritten with 0 on entry and never read.  In the
   shipped data the argument is the index of the unit whose death ran the script,
   because the only thing that names this slot is a death script -- map29.dat's
   deployment record 1, the second form of 平衡之神, whose wave 3 is the single
   record holding its third form. */
extern void fdps_chapter_30_event_deploy_wave_3(int unit_index);
#pragma aux fdps_chapter_30_event_deploy_wave_3 "*" parm caller [];

/* Chapter 30's undead top-up, and the reason that chapter's reinforcements can
   never be cleared out: every 死靈 (character id 0x55) and 白骨戰士
   (character id 0x6a) on the map that has been killed is put straight back on
   the battlefield at full health.

   It sweeps the whole live unit array and touches a record only when its
   character id is one of those two AND fdps_unit_is_retired says the unit is
   already dead, so a living one of either type is left alone and no other type
   is ever considered.

   Each revival gets its own tile.  The occupancy grid is rebuilt first -- a
   reset, then the two zone-of-control marking passes, which between them stamp
   the 0x40 "a unit stands here" bit on the tile of every unit still in play --
   and then the whole grid is scanned row by row for the free walkable cell
   closest, in Manhattan distance, to the type's scripted spawn point in
   map29.cod: (5, 12) for 死靈 and (15, 13) for 白骨戰士.  A cell carrying
   0x40 is skipped and a cell whose terrain movement cost is 5 or more is
   rejected.  Among cells that tie at the shortest distance THE LAST ONE IN
   ROW-MAJOR ORDER WINS, not the first (see the definition in chevt6.c).

   The revival is then played out once per unit: the cursor scrolls onto it, the
   screen snaps to white and fades back over 65 palette steps, and the Posion.saf
   effect from MISC.VFS plays over that one unit.  Only after that does the
   record change -- the whole flag byte at +5 goes to 0, which drops the retired
   bit and the per-turn redraw bit together, and max HP is copied over current HP
   -- and the grid is left blank again for the movement code.

   Nothing latches and nothing is deducted: run it again on the same map and
   every one of those units that has died since is revived again.

   The only caller is fdps_map_actor_behavior_step, out of its behaviour-mode 11
   branch and only for an acting unit whose character id is 0x3c, 0x3d or 0x3e --
   the three forms of 平衡之神 -- so in the shipped data this runs on every
   behaviour step the chapter 30 boss takes.  It is called with no argument at
   all: PUSH nothing, CALL, and no stack adjust after it. */
extern void fdps_chapter_30_revive_wave_4_undead(void);
#pragma aux fdps_chapter_30_revive_wave_4_undead "*" parm caller [];

#endif
