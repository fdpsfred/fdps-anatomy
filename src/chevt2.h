/* chevt2.h -- the scripted chapter-event handlers of chapters 8 to 14.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and a
 * handler that has no unit to work on ignores it -- the turn-event dispatcher
 * passes 0 when the event is turn-scheduled rather than unit-attached, while
 * the cell search and the death-script runner pass a real index.
 *
 * chevt1.h holds the same family for chapters 2 to 7.  Nothing here owns
 * state.
 */
#ifndef CHEVT2_H
#define CHEVT2_H

/* Chapter 8's guard-death event: sends the chapter's guest mage walking to the
   cell block and paints the line that goes with it.

   It re-aims exactly one unit, battle index 0x13 -- both bounds of its inline
   range walk hold that literal, and the compare between them is signed and
   inclusive, so the walk runs once.  Index 0x13 is the guest mage 費塔加:
   map07.dat's deployment record 19 is the file's only wave-1 record, so the
   turn-3 cutscene brings him on as the first index past the 19 units the map
   opens with.

   It rewrites the low nibble -- the behaviour code -- of that record's
   ai_behavior byte at offset 0x34 to 4 and leaves the high nibble alone,
   because bits 0x40 and 0x80 of it are independent AI flags other code reads
   on their own.  Mode 4 moves the map cursor to the unit and then walks it
   toward the destination tile held in its own record, so the mage stops
   fighting and heads for the cage.  The record is resolved through
   fdps_get_unit_record (unit.h).

   NOTHING IS RANGE CHECKED AND THERE IS NO LATCH.  data_fdps_map_unit_count is
   not consulted, so a call made before the cutscene has deployed unit 0x13
   writes one byte past the live array; that is what the original does, and
   adding the guard would take the write away rather than change what the
   player sees -- the mage is not on the map to walk either way, which is the
   guide's "clear the enemies before 費塔加 appears and he never goes to open
   the cell".  And nothing records that the handler ran, so calling it again
   runs it again; the merge is idempotent, but a unit the AI has since moved
   into another behaviour mode would be pushed back to mode 4.

   The re-aim happens first and the draw second: text entry 0x0f of the loaded
   chapter's block, through fdps_draw_text (text.h) straight onto the mode 13h
   aperture at 0xa0000, pitch 0x140, in the standard message colours.  The
   cursor it returns is discarded.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before anything else and never read, so
   any index, in range or not, behaves the same.

   Table slot 11, named by the opcode-2 unit script of map07.dat's deployment
   record 18 -- the soldier that becomes battle unit 14 -- so the event fires
   when that soldier is killed.  fdps_chapter_08_event_villagers_leave_cells
   re-aims the same unit when the cage is opened. */
extern void fdps_chapter_08_event_send_guest_mage_to_cells(int unit_index);
#pragma aux fdps_chapter_08_event_send_guest_mage_to_cells "*" parm caller [];

/* Chapter 8's villager-escape event: one of the four captive villagers walks
   off the battlefield and speaks its line, and when the last of the four goes
   the chapter's reward item lands in the guest mage's bag, scaled to how many
   villagers got out alive.

   It acts only for unit indices 0xf through 0x12 inclusive, chapter 8's four
   captives -- two 村民 at 0xf and 0x10, two 村婦 at 0x11 and 0x12 -- and both
   bounds are signed compares, so every other index, negative ones included,
   returns without touching anything.

   Escapes are counted in element 0x11 of data_fdps_map_cell_event_triggered_
   flags (gamedata.h), the same 32-byte block the one-shot latch at element
   0x10 lives in: it is bumped by one on every escape and is what
   fdps_chapter_08_post_action reads to tell the chapter's win from its loss --
   all four villagers retired with the count non-zero is a win, all four
   retired with it still zero is a rout.  A dozen other chapters keep an
   unrelated flag in the same element; that is safe because one chapter is
   loaded at a time and fdps_chapter_state_reset clears the whole block on
   chapter entry, and it is what makes the count survive a save, because the
   save image carries all 0x20 bytes.

   The escaping villager's own line is text entry unit_index + 0xd, so 0x1c
   through 0x1f, one per villager, drawn through fdps_draw_text (text.h)
   straight onto the mode 13h aperture in the standard message colours.  The
   cursor it returns is discarded.

   THE REWARD IS DECIDED BEFORE THIS VILLAGER IS MARKED RETIRED.  The handler
   asks fdps_unit_is_retired (unit.h) about each of 0xf..0x12 and requires the
   answer to be exactly 3, which is how it recognises that the villager it was
   called for is the last one still in the battle; only then, and only when the
   escape count has passed 1, does it draw the closing line -- 0x20 when the
   last one out is one of the two 村民, 0x21 when it is one of the two 村婦 --
   and hand fdps_unit_add_item (unititem.h) the reward for unit 0x13, the guest
   mage 費塔加: 0xc8 炎之寶石 for two escapes, 0xda 速度藥水 for three, 0xdd
   風精之羽 for all four, which is the guide's 若四個村民全被救出，結束後會得
   到風精之羽（在費塔加身上）.  Nothing reads what add_item answers, so a full
   bag loses the reward in silence.

   Retiring is the whole-byte store record->flags = 1, not a bit set, so it
   also drops the per-turn flag bit 7 that may be standing in the same byte.
   It happens on every in-range call, whether or not the reward fired.

   Nothing is range checked beyond the 0xf..0x12 test itself and there is no
   latch, so calling it twice for the same villager counts two escapes.

   Table slot 13, the entry at 000601f8. */
extern void fdps_chapter_08_event_villager_escapes(int unit_index);
#pragma aux fdps_chapter_08_event_villager_escapes "*" parm caller [];

/* Chapter 9's scheduled reinforcement wave: brings the map's six wave-1
   enemies onto the battlefield and paints the line that announces them.

   The body is two calls and a return, with no branch, no loop and no compare
   in it at all.  fdps_deploy_wave (deploy.h) is asked for wave 1 with
   place_exact 0, so the arrivals land on the nearest free walkable tile to
   their MAP%02d.COD placement records rather than on the records' own
   coordinates; map08.dat carries six records tagged wave 1 -- character ids
   0x4c, 0x56 and 0x5d, two of each at level 13 -- against the 24 wave-0
   records the map opens with.  The map number is read from
   data_fdps_chapter_current_chapter_id (gamedata.h) at the call, not from
   anything the handler holds.

   Then text entry 0x17 of the loaded chapter's block, through fdps_draw_text
   (text.h) straight onto the mode 13h aperture at 0xa0000, pitch 0x140, in the
   standard message colours.  It is the last of the 24 entries of
   fdetxt09.txt and it opens with the portrait code -0x11 naming character
   0x4c, one of the units the deployment just brought on.  The cursor the draw
   returns is discarded.

   THE DEPLOY COMES FIRST AND THE DRAW SECOND, the opposite of chapter 3's
   ambush handler: the enemies are on the map before the portrait names one of
   them.

   THERE IS NO LATCH AND NO TURN TEST.  Nothing in the body guards either call
   and nothing records that it ran, so a second call appends the same six
   records again.  What makes it happen once is the data: map08.dat's turn-event
   table, the sixteen 3-byte entries fdps_battle_run_turn_events walks from
   offset 3 of the chapter script, names this slot in one entry only -- turn
   0x0f, handler 0x0e, side 0 -- so the wave arrives at the top of turn 15's
   enemy phase and the fifteen remaining entries are the unreachable
   00 ff ff filler.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before either call and never read, so
   any index, in range or not, behaves the same.  The turn-event dispatcher is
   the only path that reaches this slot in the shipped data and it passes a
   literal 0.

   Table slot 14, the entry at 000601fc. */
extern void fdps_chapter_09_event_deploy_wave_1(int unit_index);
#pragma aux fdps_chapter_09_event_deploy_wave_1 "*" parm caller [];

/* Chapter 10's stairway ambush: brings on the ten enemy reinforcements the
   chapter's map tags as wave 10, once.

   It fires only while both of two conditions hold: the shared one-shot latch,
   element 0x10 of data_fdps_map_cell_event_triggered_flags (gamedata.h), is
   still 0, and the record unit_index names is not on side 0.  The side test is
   unsigned and is only a test against 0, and side 0 is the enemy, 1 the
   guest/neutral one and 2 the player's roster -- so what it keeps out is an
   enemy unit stopping on the trigger tile, and the player's units and the
   guests spring it alike.  The
   record is resolved through fdps_get_unit_record (unit.h) and is not range
   checked, so an index outside the live unit array reads whatever lies at that
   stride.

   When it fires it puts the latch up first and then calls fdps_deploy_wave
   (deploy.h) with wave 10 and place_exact 0, so the reinforcements land on the
   nearest free walkable tile to their placement records rather than on the
   records' own coordinates.  The map number it deploys under is read from
   data_fdps_chapter_current_chapter_id at the call, not from anything the
   handler holds.

   The latch is one byte shared with a dozen other chapters' handlers and with
   the chapter 14, 15 and 26 post-action checks.  fdps_chapter_state_reset
   clears the whole array when a chapter starts, so a later chapter's handler
   begins from a clean latch; the save-state block carries it, so a chapter
   reloaded after its event fired does not fire it again.  Both halves are what
   make one shared slot safe, and neither survives rewriting this as a
   function-local static.

   Table slot 16, named by MAP09.DAT's tile-event entry 1 and by no other
   shipped map, which is what makes this chapter 10.  Chapter 10's other
   handler, slot 15, covers the same map's turn-scheduled waves. */
extern void fdps_chapter_10_event_deploy_wave_10(int unit_index);
#pragma aux fdps_chapter_10_event_deploy_wave_10 "*" parm caller [];

/* Chapter 13's death-triggered event: takes unit indices 9 through 0x2c
   inclusive off the hold-position behaviour the map deploys them in and puts
   them on the default one, which paths a unit toward the nearest opposing
   unit, so the map's enemies stop waiting to be lured out one at a time and
   start advancing.

   It rewrites the low nibble -- the behaviour code -- of the ai_behavior byte
   at record offset 0x34 to 0 across that range and leaves the high nibble
   alone, because bits 0x40 and 0x80 of it are independent AI flags other code
   reads on their own.  The two bounds are literals; nothing is range checked
   and data_fdps_map_unit_count is not consulted, so they are only correct
   against chapter 13's own deployment, which puts 9 player records at indices
   0..8 and map12.dat's 37 records at 9..0x2d.  Each record is resolved through
   fdps_get_unit_record per iteration, so the array base is re-read.

   The range stops one short of that last deployed unit: 0x2c is index 36 of
   the map's 37 records, so unit index 0x2d -- a level-16 unit at tile (15,9)
   -- keeps the mode the map gave it and goes on holding position.  That is the
   original's behaviour and not an oversight to correct.

   There is no one-shot latch: nothing in the body guards the loop and nothing
   records that it ran, so calling it again runs it again.  The merge is
   idempotent, so a second firing changes nothing, but a unit the AI has since
   moved into another behaviour mode would be pushed back to mode 0.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before anything else and never read, so
   any index, in range or not, behaves the same.

   Table slot 18, and chapter 13's map12.dat is the only shipped file that
   names it -- as the death script of deployment record 28, the dark mage
   standing at tile (14,9) that becomes unit index 0x25 -- so the event fires
   when that unit is killed. */
extern void fdps_chapter_13_event_enemies_advance(int unit_index);
#pragma aux fdps_chapter_13_event_enemies_advance "*" parm caller [];

#endif
