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

/* Chapter 25's ambush: the first time a unit on the player's side finishes a
   step onto the map's trigger tile, the map's wave-1 reinforcements arrive, the
   chapter speaks two lines while the view is panned over the two corners they
   land in, and every unit on the map beyond the twelve party slots switches to
   the fullest map-AI mode so the whole garrison attacks at once.

   In order: entry 0x12 of the chapter's own FDETXT25.TXT block is drawn, wave 1
   of the resident MAP%02d.DAT deployment table is brought on, the map cursor is
   hidden, the view is walked to tile (0, 0) and held for twelve frames and then
   to tile (20, 11) and held for twelve more, the cursor comes back, entry 0x13
   is drawn, and finally the behaviour sweep runs and the latch is raised.

   TWO GATES, BOTH REFUSING THE WHOLE BODY.  The one-shot latch --
   data_fdps_map_cell_event_triggered_flags element 0x10 (gamedata.h) -- must
   still be 0, and the record the argument names must be on side 2.  A unit on
   any other side leaves the ambush armed rather than spending it, so an enemy
   crossing the tile first does not consume the event.  The latch lives in the
   chapter's own flag array and not in a private static, which is what makes it
   survive a save and be cleared by a chapter restart.

   THE WAVE ARRIVES BEFORE THE SWEEP AND IS SWEPT WITH IT.  The sweep's last
   index is data_fdps_map_unit_count - 1 read after the deployment has appended
   the wave, so the arrivals get behaviour mode 0x0b too.  Deploying after the
   sweep, or reading the count before it, leaves the arriving wave in the mode
   the map file authored.

   THE SWEEP STARTS AT UNIT 12 AND IS INCLUSIVE.  Indices 0 to 11 are the party
   slots and are never touched; every index from 12 to the last live unit gets
   the low nibble of its AI byte replaced with 0x0b while the high nibble, which
   carries flags the target scorers read, is preserved.  A map with fewer than
   thirteen units runs the sweep zero times.

   THE CURSOR MODE IS OVERWRITTEN, NOT RESTORED.  Whatever
   data_fdps_map_cursor_draw_mode held on entry is replaced by 1 at the end of
   the pan.

   The map number handed to the deployment is read out of
   data_fdps_chapter_current_chapter_id (gamedata.h) at the call site and is not
   a literal, so it is whichever chapter is loaded -- 24 for this one, which is
   the only map whose tile-event table names this slot.

   unit_index is the handler table's shared parameter: the index of the unit
   that tripped the tile event, not range checked and resolved through
   fdps_get_unit_record (unit.h) before either gate.  Only the side byte is read
   from it.

   Table slot 37 at 00060258. */
extern void fdps_chapter_25_event_deploy_wave_1(int unit_index);
#pragma aux fdps_chapter_25_event_deploy_wave_1 "*" parm caller [];

/* Chapter 25's fire-god exchange: Randis finishes a step onto the shrine tile
   still carrying 火光之劍, the chapter speaks one line, and the sword becomes
   真炎龍劍.  In order: entry 0x14 of the chapter's own FDETXT25.TXT block is
   drawn, the entry holding 火光之劍 (item id 0xa1) is taken out of the bag,
   真炎龍劍 (0xa2) is added, and the unit's derived combat stats are rebuilt so
   the new weapon's attack power counts at once.

   It is the last link of a three-sword chain: the chapter 16 smith forges
   灼烈之劍 (0xa0), fdps_chapter_20_event_upgrade_randis_sword (chevt4.h) trades
   that for 火光之劍 on or before turn 20 of chapter 20, and this trades that
   for 真炎龍劍.  A player who missed the chapter 20 deadline arrives here
   carrying the wrong sword and this event does nothing for him.

   TWO GATES, BOTH REFUSING THE WHOLE BODY: the index must be 0, battle unit 0
   being Randis, and the search for 火光之劍 must have found an entry.  Any
   other unit, and a Randis who is not carrying the sword, returns having done
   nothing.

   THE SEARCH IS MADE BEFORE EITHER GATE, so every firing costs one walk of the
   triggering unit's inventory whether or not the body then runs.

   NO TURN DEADLINE AND NO ONE-SHOT LATCH.  Unlike the chapter 20 link there is
   no turn test, and nothing is written to
   data_fdps_map_cell_event_triggered_flags.  The exchange cannot fire twice
   because the item it looks for is gone afterwards, and a unit that is not
   Randis crossing the tile leaves the event armed rather than spending it.

   THE OLD SWORD LEAVES THE BAG BEFORE THE NEW ONE IS PUT IN.  fdps_unit_add_item
   (unititem.h) stores nothing when all eight entries are occupied, so the
   natural "give the new sword, then take the old one" order loses 真炎龍劍
   whenever Randis is carrying a full bag.

   unit_index is the handler table's shared parameter: the index of the unit
   that tripped the tile event, not range checked, handed straight to
   fdps_unit_find_item_slot, fdps_unit_remove_item, fdps_unit_add_item and
   fdps_unit_recompute_combat_stats.  No unit record is resolved in this body
   itself.

   Table slot 38 at 0006025c. */
extern void fdps_chapter_25_event_upgrade_randis_sword(int unit_index);
#pragma aux fdps_chapter_25_event_upgrade_randis_sword "*" parm caller [];

#endif
