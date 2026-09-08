/* chevt3.h -- the scripted chapter-event handlers of chapters 15 to 19.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and a
 * handler that has no unit to work on ignores it -- the turn-event dispatcher
 * passes 0 when the event is turn-scheduled rather than unit-attached, while
 * the cell search and the death-script runner pass a real index.
 *
 * chevt1.h holds the same family for chapters 2 to 7 and chevt2.h for chapters
 * 8 to 14.  Nothing here owns state.
 */
#ifndef CHEVT3_H
#define CHEVT3_H

/* Chapter 15's death-triggered event: takes unit indices 0x1d through 0x25
   inclusive off the hold-position behaviour the map deploys them in and puts
   them on the default one, which paths a unit toward the nearest opposing
   unit, so the enemy group holding the bridgehead in the top-right corner of
   the map stops standing its ground and starts advancing on the party.

   It rewrites the low nibble -- the behaviour code -- of the ai_behavior byte
   at record offset 0x34 to 0 across that range and leaves the high nibble
   alone, because bits 0x40 and 0x80 of it are independent AI flags other code
   reads on their own.  The two bounds are literals; nothing is range checked
   and data_fdps_map_unit_count is not consulted, so they are only correct
   against chapter 15's own deployment, which puts 9 player records at indices
   0..8 and map14.dat's wave-0 records 1..43 at 9..0x33.  Each record is
   resolved through fdps_get_unit_record per iteration, so the array base is
   re-read.

   The nine indices are deployment records 21..29: three level 14 barbarian
   warriors at (23,3), (24,3) and (25,3), two level 19 ice mages at (23,4) and
   (25,4), three level 15 archers at (23,5), (24,5) and (25,5), and record 27,
   the level 16 beam turret at (16,10) that is this chapter's victory
   condition.  The turret has no movement allowance and both behaviour chains
   open with fdps_map_actor_take_best_action and close with fdps_unit_rest, so
   the mode change does not alter anything it does; it is inside the range
   because the range is contiguous, not because the handler is about it.

   There is no one-shot latch: nothing in the body guards the loop and nothing
   records that it ran, so calling it again runs it again.  The merge is
   idempotent, so a second firing changes nothing, but a unit the AI has since
   moved into another behaviour mode would be pushed back to mode 0.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before anything else and never read, so
   any index, in range or not, behaves the same.

   Table slot 20, and chapter 15's map14.dat is the only shipped file that
   names it -- as the death script of deployment record 8, the level 19 ice
   mage standing at tile (18,1) inside the stockade at the top of the map -- so
   the event fires when that unit is killed.  That file carries no turn events
   and no tile triggers at all, so the death script is the only route to this
   slot. */
extern void fdps_chapter_15_event_activate_enemy_group(int unit_index);
#pragma aux fdps_chapter_15_event_activate_enemy_group "*" parm caller [];

/* Chapter 15's boss-death event: the fortress cannon that is the chapter's
   victory condition has fallen, so the chapter is over -- except that when it
   fell on battle turn 25 or earlier with 裘娜 still on the map, a challenger is
   brought on first and offers her a duel.

   Every enemy still standing is destroyed first, unconditionally and before
   either gate is looked at, through fdps_battle_destroy_remaining_enemies
   (btlend.h).  That is why fdps_chapter_15_end, alone in its family, has no
   destroy call of its own, and why an over-25-turn clear must not be allowed to
   skip it.

   The two gates are the battle turn counter at 25 or below, a signed inclusive
   compare, and fdps_unit_is_retired(4) answering 0 -- unit 4 being 裘娜, the
   fifth of the nine roster records chapter 15 lays down at indices 0..8.
   Either one failing ends the chapter with the cleared code in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) and nothing else
   done.

   Both passing, the map's wave 2 is deployed -- map14.dat carries one record
   there, the level 17 opponent, appended as unit 0x35 -- and the offer is put
   to the player over the message panel under FACE.CEL record 3.

   ACCEPTING LEAVES THE BATTLE RUNNING.  The battle-end global is deliberately
   not written on that arm: instead every unit at indices 0..8 except 裘娜's,
   and unit 0x34 -- the archer 瑪麗安 the opening cutscene deployed -- has its
   flags byte STORED to 1, which retires it and clears the has-acted bit with
   it, and element 0x10 of data_fdps_map_cell_event_triggered_flags is raised.
   The duel then plays out between unit 4 and unit 0x35, and
   fdps_chapter_15_post_action, which branches on that same flag, settles it and
   awards the 妖刀村雨.

   Declining, and a cancel answers the same way, writes the refusal line and
   ends the chapter with the cleared code.

   There is no one-shot latch on the way in: the flag the accepted branch raises
   is written and never read here, so a second firing would run the whole scene
   again.  Nothing in the shipped data can produce one -- the only route in is
   one unit's death script.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with the prompt answer and then with the retire
   loop's counter, and it is never read before either store, so any index, in
   range or not, behaves the same.

   Table slot 21, and chapter 15's map14.dat is the only shipped file that names
   it -- as the death script of deployment record 27, the level 16 unit of enemy
   id 0x80 that is the chapter's victory condition.  That file carries no turn
   events and no tile triggers at all, so that death is the only route to this
   slot. */
extern void fdps_chapter_15_event_boss_defeat(int unit_index);
#pragma aux fdps_chapter_15_event_boss_defeat "*" parm caller [];

/* Chapter 16's wandering-smith event: Randis has ended his turn on the smith's
   tile, and the smith offers to reforge one of two named swords if he is still
   carrying it.  修佩魯 comes back as 灼烈之劍; 雷德 breaks in the forge and is
   paid off with 5000 gold and the player's choice of 神的聖印 or 金屬礦.

   Nothing happens at all unless three conditions hold together: the unit that
   ended its turn is battle unit 0, which is Randis; element 0x10 of
   data_fdps_map_cell_event_triggered_flags is still clear; and the battle turn
   counter is 20 or less, a signed inclusive bound, so turn 20 still triggers it
   and turn 21 does not.

   THE ONE-SHOT LATCH IS RAISED IMMEDIATELY AFTER THE GREETING, before the
   inventory is searched.  So a Randis who steps on the tile carrying neither
   sword spends the encounter on the greeting alone and can never come back with
   the right weapon -- the chapter's 灼烈之劍, and with it the 真炎龍劍 that
   chapter 25 makes out of it, is lost for that playthrough.  Raising the latch
   where the trade happens, which is what the sibling handlers do, would hand the
   player a second chance the original does not give.

   The sword is looked for as an else-chain: 修佩魯 first, and 雷德 only when
   the first is not carried, so a Randis holding both is treated as holding
   修佩魯.  Either way the slot found is the one the trade takes out of his bag,
   through fdps_unit_remove_item, before fdps_unit_add_item puts the new item in
   -- which is what keeps a full eight-entry inventory from losing the reward.
   fdps_unit_recompute_combat_stats then rebuilds the four derived combat stats
   on every accepted trade, the weapon he carries having changed.

   Both questions are answered by fdps_prompt_two_choice and both are tested for
   0, its affirmative: a cancel answers -1 and declines, exactly as the right
   option does (msgwin.h).  Declining the first question ends the scene on the
   parting line with the sword untouched -- and with the latch already spent.
   Declining the second, after 雷德 has broken, is not a refusal of the payment:
   the 5000 gold has already been added and the answer only picks 金屬礦 over
   神的聖印.

   The scene speaks ten lines out of the chapter's own FDETXT16.TXT under
   FACE.CEL portrait 129, each in a message window of its own; the two questions
   are asked with their window still standing.

   unit_index is the handler table's shared parameter, the battle unit that
   ended its turn on the trigger tile.  It is read once, by the first gate, and
   any index other than 0 leaves the handler doing nothing whatever.

   Table slot 22, and chapter 16's map15.dat is the only shipped file that names
   it: the first entry of its tile-event table, with occasion 1 -- a unit ending
   its turn on the cell -- and M15.DTL marks cell (2,12) with that event code. */
extern void fdps_chapter_16_event_wandering_smith_forge(int unit_index);
#pragma aux fdps_chapter_16_event_wandering_smith_forge "*" parm caller [];

/* Chapter 16's turn-scheduled release event: it takes one block of the map's
   enemies off the hold-position behaviour the map deployed them in and puts
   them on the default one, which paths a unit toward the nearest opposing
   unit, and which block it releases depends on the battle turn counter.

   The counter is tested for equality against 5 and there are only two
   outcomes: 5 releases unit indices 0x19 through 0x22 inclusive, and anything
   else releases 0x0a through 0x19 inclusive.  The second is the fall-through,
   not a test for a second turn number, so a firing on any turn other than 5
   lands there.  The two ranges overlap at index 0x19, which is released by
   both.

   Like the chapter 15 handler above it rewrites only the low nibble -- the
   behaviour code -- of the ai_behavior byte at record offset 0x34 to 0, and
   leaves the high nibble alone, because bits 0x40 and 0x80 of it are
   independent AI flags other code reads on their own.  The bounds are
   literals; nothing is range checked and data_fdps_map_unit_count is not
   consulted.  Each record is resolved through fdps_get_unit_record per
   iteration, so the array base is re-read.

   The indices are only meaningful against chapter 16's own deployment.
   map15.dat lays 10 player records down at 0..9 and its 25 enemy deployment
   records follow in file order at 0x0a..0x22, wave by wave: 0x0a..0x16 the
   thirteen wave-0 samurai, archers and knights, 0x17..0x18 the two wave-1 dark
   mages, 0x19..0x1c the four wave-2 flying units, 0x1d..0x20 the four wave-3
   samurai and 0x21..0x22 the two wave-4 dark mages.  So the turn-5 branch
   releases the whole second half of the deployment -- waves 2, 3 and 4, ten
   units, all of them deployed holding position -- and the other branch
   releases the units that opened the battle plus the first wave-2 flyer.

   Table slot 23, and the turn-event table of chapter 16's map15.dat is the
   only shipped thing that names it, with the two records {turn 5, slot 23,
   phase 0} and {turn 15, slot 23, phase 0}; no tile trigger and no death
   script in any MAP*.DAT reaches the slot.  The turn-event dispatcher fires a
   record while the counter still holds the turn whose phase has just ended, so
   the first firing sees 5 and the second sees 15: in the shipped data the two
   branches are the turn-5 release and the turn-15 release.

   There is no one-shot latch: nothing guards either loop and nothing records
   that it ran.  The merge is idempotent, so a repeat firing on the same turn
   changes nothing, but a unit the AI has since moved into another behaviour
   mode would be pushed back to mode 0.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before the counter is even read, and
   never read back, so any index behaves the same. */
extern void fdps_chapter_16_event_enemies_advance_for_turn(int unit_index);
#pragma aux fdps_chapter_16_event_enemies_advance_for_turn "*" parm caller [];

/* Chapter 17's turn-scheduled reinforcement event: it brings on the wave of
   the current map's deployment table that the turn just played is due, by
   handing fdps_deploy_wave the map the chapter is playing, the battle turn
   counter less seven, and a placement flag of 0.

   The wave key is the raw subtraction and nothing else: there is no test, no
   table and no bound on either side of it, so the wave asked for is whatever
   the counter happens to hold minus seven.  On the two turns the map schedules
   it that is wave 1 and wave 2; on any earlier turn it is a negative number,
   which matches no deployment record because a record's wave byte is
   unsigned, so the call opens its files, loads the placement table and deploys
   nothing.

   The map number is read from data_fdps_chapter_current_chapter_id, so it is
   whatever chapter is loaded rather than anything this handler holds, and the
   placement flag of 0 makes fdps_deploy_unit put each unit on the nearest free
   walkable tile to the coordinates its MAP%02d.COD record names rather than on
   those coordinates themselves.

   There is no one-shot latch and nothing records that the handler has run.
   Calling it twice on the same turn deploys the same wave twice, because the
   wave walk appends and never checks whether those records are already on the
   map; the map's own turn table naming a slot once per turn is what keeps that
   from happening.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before the counter is read, and never
   read back, so any index behaves the same.

   Table slot 24, and the turn-event table of chapter 17's map16.dat is the
   only shipped thing that names it, with the two records {turn 8, slot 24,
   phase 0} and {turn 9, slot 24, phase 0}; no tile trigger and no death script
   in any MAP*.DAT reaches the slot.  The turn-event dispatcher fires a record
   while the counter still holds the turn whose phase has just ended, so the
   first firing asks for wave 1 -- map16.dat's six level 15 records 23..28 --
   and the second for wave 2, its single level 14 record 29.  All seven have
   spawn points in the lower-left corner of the map, tiles (2..4, 22..24). */
extern void fdps_chapter_17_event_deploy_wave_for_turn(int unit_index);
#pragma aux fdps_chapter_17_event_deploy_wave_for_turn "*" parm caller [];

/* Chapter 18's turn-scheduled reinforcement event: it brings on the wave of
   the current map's deployment table whose number is the turn just played, by
   handing fdps_deploy_wave the map the chapter is playing, the battle turn
   counter unchanged, and a placement flag of 0.

   It is the chapter 17 handler with the subtraction taken out.  The wave key
   is the counter itself: no offset, no test, no table and no bound on either
   side of it, so the wave asked for is whatever the counter happens to hold,
   negative and unscheduled values included.  A wave number that matches no
   deployment record still opens the files and loads the placement table and
   simply deploys nothing.

   The map number is read from data_fdps_chapter_current_chapter_id, so it is
   whatever chapter is loaded rather than anything this handler holds, and the
   placement flag of 0 makes fdps_deploy_unit put each unit on the nearest free
   walkable tile to the coordinates its MAP%02d.COD record names rather than on
   those coordinates themselves.

   There is no one-shot latch and nothing records that the handler has run.
   Calling it twice on the same turn deploys the same wave twice, because the
   wave walk appends and never checks whether those records are already on the
   map; the map's own turn table naming a slot once per turn is what keeps that
   from happening.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before either global is read, and never
   read back, so any index behaves the same.

   Table slot 25, and chapter 18's map17.dat is the only shipped MAP*.DAT whose
   turn-event table names it, with the nine records {turn t, slot 25, phase 0}
   for t in 4, 5, 6, 7, 8, 9, 10, 11 and 13.  Its 65 deployment records carry
   four units tagged with each of waves 4 through 11 and fifteen tagged wave
   13, so wave number and scheduled turn are the same nine values and each
   firing brings on the group tagged with the turn it fires on -- four
   reinforcements a turn for eight turns, then fifteen at once on turn 13.
   (The map also carries fifteen records tagged wave 1 and one tagged wave 2,
   which nothing in the shipped data ever asks for: the chapter opens on wave 0
   and no turn 1 or 2 record names this slot.) */
extern void fdps_chapter_18_event_deploy_wave_for_turn(int unit_index);
#pragma aux fdps_chapter_18_event_deploy_wave_for_turn "*" parm caller [];

/* Chapter 19's turn-scheduled arrival event: the paladin 蘭斯洛特 joins the
   party in the middle of the battle.  It deploys wave 1 of the current map's
   deployment table -- in map18.dat one record, a player-side level 2 unit of
   character id 0x0b with items 0x29 and 0x6a equipped -- onto the nearest free
   walkable tile to the coordinates MAP%02d.COD names for it, and then speaks
   entry 10 of the chapter's own text block.

   It is the two reinforcement handlers above it with the wave key made a
   literal: the battle turn counter is not read anywhere in the body, so the
   wave asked for is 1 whatever turn the handler is reached on.  The map number
   is read from data_fdps_chapter_current_chapter_id at the call site, so it is
   whichever chapter is loaded.

   THE TWO CALLS ARE ORDERED AND THE ORDER IS LOAD-BEARING.  Entry 10 opens
   with the -0x11 speaker code, so the draw goes looking for the speaker among
   the units standing on the map and raises his portrait from the record it
   finds; the deployment is what puts him there to be found.  Speaking before
   deploying finds no unit.

   There is no one-shot latch and nothing records that the handler has run, so
   a second firing deploys wave 1 a second time; the map's turn table naming
   the slot once is what makes the arrival happen once.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before either call and never read back,
   so any index behaves the same.

   Table slot 26, and chapter 19's map18.dat is the only shipped file that
   names it, with the single turn-event record {turn 6, slot 26, phase 2} --
   the phase the turn runner is called for at the top of a player phase, just
   after the turn counter has been incremented.  No tile trigger and no death
   script in any MAP*.DAT reaches the slot. */
extern void fdps_chapter_19_event_lancelot_joins(int unit_index);
#pragma aux fdps_chapter_19_event_lancelot_joins "*" parm caller [];

/* Chapter 19's flank ambush: the first unit that is not on side 0 to finish a
   step onto the map's trigger tile brings on the twenty-two enemies tagged
   wave 6, the view is panned across the three places they arrive, and the
   chapter's line about them is spoken.

   Two gates, both jumping to the same exit and in this order: element 0x10 of
   data_fdps_map_cell_event_triggered_flags must still be 0, and only then is
   the acting unit's record fetched and its side byte tested for non-zero.  A
   latch that is already up means the record is never even looked up.  Side 0
   is the enemy's, so an enemy walking over the tile cannot spring the ambush;
   the player's units and the guests both can.  The side test is a plain
   equality against 0 over the whole byte, not a sign test and not a range.

   THE CURSOR MODE IS SET TWICE AND THE SECOND STORE IS A LITERAL 1, NOT A
   RESTORE.  0 goes into data_fdps_map_cursor_draw_mode before the deployment,
   which is a mode fdps_draw_map_cursor matches none of its cases for, so no
   cursor is painted for the whole sequence -- and it also makes
   fdps_map_cursor_move_to skip the frame on any step that did not scroll the
   view, so the three pans compose fewer frames than they take steps.  1 goes
   in after the last pan whatever the mode was on entry.  Writing the pair as
   the save/restore it looks like changes what the cursor wears afterwards.

   THE LATCH IS NOT PRIVATE TO THIS HANDLER.  It is element 0x10 of the
   chapter-event flag block fdps_chapter_state_reset clears at every chapter
   start and fdps_load_savegame restores from the save image, and a dozen
   handlers of other chapters latch the same byte.  A function-local static
   would fire the ambush once per process instead of once per chapter, so
   replaying chapter 19 after a Game Over, or loading a save made before the
   trigger, would silently skip the reinforcements.

   The deployment is fdps_deploy_wave (deploy.h) with wave 6 and place_exact 0,
   the map number read from data_fdps_chapter_current_chapter_id at the call
   site, so each arrival goes on the nearest free walkable tile to the
   coordinates its MAP%02d.COD record names.  The three pans are world pixels
   and not tiles -- (264, 0), (0, 528) and (768, 528), tiles (11, 0), (0, 22)
   and (32, 22) -- and each is followed by twelve calls to
   fdps_render_view_frame, which is a pause of twelve retrace-paced frames
   rather than a repaint count.  The line is entry 0x13 of the chapter's own
   text block.

   The order is load-bearing at one point: the deployment runs before the pans,
   so the reinforcements are standing on the map by the time the view reaches
   them.

   unit_index is the handler table's shared parameter and it is read exactly
   once, by the side gate.  The incoming slot is then overwritten with 0 and
   reused as the counter of all three frame loops, so nothing past the gate can
   see which unit fired the event.

   Table slot 27, and chapter 19's map18.dat is the only shipped file that
   names it: its tile-event table entry for cell event code 1, with occasion 0
   -- the occasion a unit reports as it finishes stepping onto a cell.  No turn
   event and no death script in any MAP*.DAT reaches the slot. */
extern void fdps_chapter_19_event_deploy_wave_6(int unit_index);
#pragma aux fdps_chapter_19_event_deploy_wave_6 "*" parm caller [];

#endif
