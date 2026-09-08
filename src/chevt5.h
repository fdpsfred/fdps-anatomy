/* chevt5.h -- the scripted chapter-event handlers of chapters 24 to 27.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and a
 * handler that has no unit to work on ignores it -- the turn-event dispatcher
 * passes 0 when the event is turn-scheduled rather than unit-attached, while
 * the cell search and the death-script runner pass a real index.
 *
 * chevt1.h holds the same family for chapters 2 to 7, chevt2.h for 8 to 14,
 * chevt3.h for 15 to 19, chevt4.h for 20 to 23 and chevt6.h for 28 to 30.
 * Nothing here owns state.
 */
#ifndef CHEVT5_H
#define CHEVT5_H

/* Chapter 24's turn-scheduled event handler: the one table slot MAP23.DAT
   names for all six of the chapter's turn events, running whichever of them is
   due for the turn that has just been finished.  Each of the six speaks one
   line of the chapter's own FDETXT24.TXT block, and five of the six also bring
   on one wave of the map's deployment table.

     turn 5   line 0x10, then wave 2
     turn 8   line 0x11, and nothing deployed
     turn 7   wave 6, then line 0x0e
     turn 10  wave 4, then line 0x12
     turn 13  line 0x13, then wave 3
     turn 15  line 0x14, then wave 5

   Wave 6 is the map's one player-side deployment record, 珊 the 法師 at
   character id 0x0a, who joins the party as a guest on turn 7; waves 2, 3, 4
   and 5 are enemy reinforcements.  Nothing here gates on the chapter's boss
   being alive or on any latch: a reinforcement turn fires on the turn counter
   alone.

   THE CALL ORDER IS NOT THE SAME IN EVERY CASE.  Turns 5, 13 and 15 speak
   first and deploy second; turns 7 and 10 deploy first and speak second.  What
   the player sees is a line over a map the wave has not arrived on yet, or a
   line over a map it has, and folding the six cases into one table of (turn,
   text id, wave) with one fixed call order changes that on three of the six
   turns.  The order is preserved case by case in the emitted body.

   The turn tests themselves are in the map file's own order -- 5, 8, 7, 10,
   13, 15, which is exactly the order MAP23.DAT's turn-event table lists its
   six live records in -- and not in numeric order.  They are mutually
   exclusive equalities in an if / else-if chain, so re-ordering the tests is
   harmless; only the call order inside a case is not.

   The map number handed to the deployment is read out of
   data_fdps_chapter_current_chapter_id (gamedata.h) at each call site and is
   not a literal, so it is whichever chapter is loaded -- 23 for this one,
   which is the only map whose turn-event table names this slot.

   The incoming argument is stored over with 0 on entry and never read.  The
   only dispatcher that reaches this slot, fdps_battle_run_turn_events, pushes
   a literal 0 anyway.

   Table slot 36 at 00060254. */
extern void fdps_chapter_24_event_deploy_wave_for_turn(int event_arg);
#pragma aux fdps_chapter_24_event_deploy_wave_for_turn "*" parm caller [];

#endif
