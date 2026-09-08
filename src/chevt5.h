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

/* Chapter 25's 風神弓 shop: 瑪麗安 stops on the merchant's tile with 30000
   gold and room in her bag, the merchant offers her 風神弓 (item id 0x4a), and
   if the player says yes the money is taken and the bow handed over.

   FOUR GATES, ALL FOUR REFUSING THE WHOLE BODY: the triggering unit's
   character id must be 5, which is 瑪麗安; the party purse must hold at least
   30000, the bow's list price (assets/items.md); the shared one-shot latch must
   still be clear; and the unit's inventory must not already hold eight entries,
   because fdps_unit_add_item (unititem.h) stores nothing into a full bag.  A
   unit that arrives short of money or with a full bag leaves the event armed
   and can come back later.

   THE OFFER IS SPENT WHETHER OR NOT THE BOW IS BOUGHT.  The latch is raised on
   the way out of both arms of the question, so declining -- and cancelling the
   prompt, which answers -1 and which the "not 0" test folds into declining --
   costs 瑪麗安 her only chance at the bow for the rest of the game.  Raising it
   only on the purchase is the obvious rewrite and it lets the player be asked
   again.

   THE FIRST LINE IS DRAWN TO THE SCREEN CORNER AND THE OTHER THREE INTO THE
   PANEL.  The opening exchange carries its own speaker tokens, which stand a
   speech panel up and move the pen themselves, so its destination argument is
   never used; the question is a bare glyph run and needs both the panel
   fdps_message_window_open puts up for merchant portrait 0x77 and the in-panel
   pen it is handed.  That is why the question, and only the question, is
   bracketed by an open and a close.

   unit_index is the handler table's shared parameter: the index of the unit
   that tripped the tile event, not range checked.  It is read three times --
   resolved through fdps_get_unit_record (unit.h) for the character id, and
   handed to fdps_unit_item_count and fdps_unit_add_item (unititem.h).

   Table slot 39 at 00060260. */
extern void fdps_chapter_25_event_marian_buys_wind_god_bow(int unit_index);
#pragma aux fdps_chapter_25_event_marian_buys_wind_god_bow "*" parm caller [];

/* Chapter 26's turn-4 event: one line of the chapter's own FDETXT26.TXT block
   is spoken, and then the two ten-unit holding groups of the tower garrison
   come off their holding behaviour and start advancing on the player.

   MAP25.DAT's only live turn-event record is (turn 4, this slot, side 0), so
   the event fires as the enemy phase of turn 4 opens -- immediately after the
   player's fourth turn ends.  What it releases is unit indices 0x1a..0x2d,
   deployment records 14..33 of the map's wave-0 block: two identical ten-unit
   groups, every record of which the map file deploys in the holding behaviour
   mode.  The commanders below them and the sixteen units above them keep
   holding until fdps_chapter_26_event_deploy_waves_2_and_3, the next slot of
   the same table, sweeps the whole wave-0 block.

   ONLY THE BEHAVIOUR NIBBLE IS WRITTEN.  Each record's AI byte is merged, not
   stored: the low nibble -- the mode fdps_map_actor_behavior_step dispatches
   on -- goes to 0, and the high nibble is carried across because two of its
   bits are per-unit flags the target scorers read on their own.

   THE RANGE IS INCLUSIVE.  The last unit released is 0x2d and not 0x2c, and
   the record above it is where the map's already-advancing units begin.

   unit_index is the handler table's shared parameter and this handler does not
   use it: the turn-event dispatcher that reaches this slot passes 0 and the
   body overwrites the slot before anything else happens.

   Table slot 40 at 00060264. */
extern void fdps_chapter_26_event_enemies_advance(int unit_index);
#pragma aux fdps_chapter_26_event_enemies_advance "*" parm caller [];

/* Chapter 26's mid-map ambush: the first time a unit on the player's side
   walks onto the map's trigger tile, the enemy wave and the allied relief wave
   both come onto the battlefield and the lower half of the deployment block
   stops holding position.

   TWO GATES, BOTH REFUSING THE WHOLE BODY: the shared one-shot latch must
   still be clear, and the triggering unit's side byte must be 2, the player's
   own roster.  An enemy or a guest crossing the tile changes nothing and
   leaves the ambush armed for the unit that comes next.  The latch is the same
   element fdps_chapter_25_event_deploy_wave_1 uses; the two events live on
   different maps and the block is cleared between chapters, so they cannot
   collide.

   THE ORDER IS ENEMY WAVE, LINE, PAN AND HOLD, LINE, ALLIED WAVE, LINE.  The
   enemy wave is already standing when its line is spoken; the allied wave is
   announced before it arrives.  The pan hides the cursor, walks the view to
   the bottom of the map, holds it there for twelve composed frames and then
   puts the cursor back -- as a literal 1, not as whatever it found.

   THE RELEASE RANGE IS TWO LITERALS, 0x0c..0x4f INCLUSIVE, and not the unit
   count: the twelve party slots below it and every unit from 0x50 upward keep
   the behaviour mode they already carry.  On MAP25.DAT the two literals cover
   the map's wave-0 block exactly -- 68 of its 80 deployment records are tagged
   wave 0, seven wave 2 and five wave 3 -- so what is released is the garrison
   alone, and neither the enemy wave nor the allied wave this handler has just
   brought on starts moving with it.

   ONLY THE BEHAVIOUR NIBBLE IS WRITTEN.  Each released record's AI byte is
   merged, not stored: the low nibble goes to 0 and the high nibble is carried
   across because two of its bits are per-unit flags the target scorers read on
   their own.

   unit_index is the handler table's shared parameter: the index of the unit
   that tripped the tile event, not range checked.  It is read once, resolved
   through fdps_get_unit_record (unit.h) for the side byte, and nothing else in
   the body looks at it.

   Table slot 41 at 00060268. */
extern void fdps_chapter_26_event_deploy_waves_2_and_3(int unit_index);
#pragma aux fdps_chapter_26_event_deploy_waves_2_and_3 "*" parm caller [];

/* Chapter 26's wave-2 wipe line: the death script the seven enemy
   reinforcements of the chapter's mid-map ambush all carry, which speaks one
   line of the chapter's own FDETXT26.TXT block once the last of the seven is
   gone and then latches itself off.

   The seven are the units at indices 0x50..0x56 --
   fdps_chapter_26_event_deploy_waves_2_and_3 above appends MAP25.DAT's seven
   wave-2 records there, on top of the twelve party slots and the map's 68
   wave-0 records, and brings the five wave-3 allies on behind them at
   0x57..0x5b.  Every one of the seven carries the same death script, so this
   handler runs once for each of them as it dies and does nothing until the
   last run finds all seven retired.

   TWO GATES, BOTH REFUSING THE WHOLE BODY: the one-shot latch must still be 0,
   and every one of the seven must answer fdps_unit_is_retired (unit.h) with a
   non-zero.  The latch is element 0x12 of
   data_fdps_map_cell_event_triggered_flags (gamedata.h) and NOT either of the
   elements the two handlers above use; the chapter 27 handler at 00039440
   latches the same element in the chapter that follows, which is safe because
   one chapter is loaded at a time and fdps_chapter_state_reset clears the
   whole block when a chapter starts.

   THE POLL IS NOT STOPPED BY THE FIRST SURVIVOR.  All seven are asked on every
   firing and the answers are gathered into one flag; a rewrite that broke out
   of the loop asks fewer of them.  fdps_unit_is_retired only reads a record, so
   nothing observes the difference beyond the call count.

   THE TEST DEPENDS ON THE CALLER'S ORDER.  fdps_map_actor_move_and_attack
   collects the death scripts of the killed, then marks them retired, and only
   then runs the scripts, so the seventh unit already reads as retired on the
   pass that kills it.  Running the scripts before the marking leaves the line
   permanently unspoken.

   Message 0x17 opens with the -0x11 speaker token carrying character id 0x0c,
   MAP25.DAT's single wave-3 ally, so the line is the guest hero's.

   unit_index is the handler table's shared parameter.  The dispatcher that
   reaches this slot, fdps_run_death_scripts, forwards the index of the unit
   that made the killing action rather than that of the dead unit whose script
   is running, and this handler reads neither: the incoming value is stored over
   on entry.

   Table slot 42 at 0006026c. */
extern void fdps_chapter_26_event_wave_2_defeated_line(int unit_index);
#pragma aux fdps_chapter_26_event_wave_2_defeated_line "*" parm caller [];

#endif
