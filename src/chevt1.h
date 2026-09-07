/* chevt1.h -- the scripted chapter-event handlers of chapters 2 to 7.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and a
 * handler that has no unit to work on ignores it -- the dispatchers pass 0
 * when the event is turn-scheduled rather than unit-attached.
 *
 * Nothing here owns state.  The battle-end code the handlers write is
 * gamedata.h's, and it is what the caller of the battle loop reads to decide
 * whether the chapter ended in defeat, in a clear, or not at all.
 */
#ifndef CHEVT1_H
#define CHEVT1_H

/* Chapter 3's turn-scheduled reinforcement event: brings on the wave of the
   resident map's deployment table that is due for the turn just played, and on
   the first and the last of the scheduled turns speaks a line of the chapter's
   own text block around it.

   The wave asked for is the battle turn counter less one, with no compare, no
   table and no bound on either side of the subtraction, so a counter of 0 asks
   for a negative wave that matches nothing.  map02.dat schedules this slot for
   the enemy phase of turns 3 through 14, which reach waves 2 through 13: 25
   石巨神 in twelve arrivals.

   The placement file is the literal "map02.cod" and not the chapter global the
   chapter 10, 17 and 18 handlers read, so the arriving units take map 2's
   coordinates whatever chapter is loaded.  They are placed with the flag that
   searches for the nearest free walkable tile rather than the flag that takes
   the placement record's own tile as given.  Which units arrive is not map 2's
   to say: the deployment records come from whichever MAP%02d.DAT is resident.

   On turn 3 it draws text entry 0x13 before the deployment and entry 0x14 after
   it, and on turn 14 entry 0x15 before it; the two turn tests are mutually
   exclusive.  All three go straight onto the mode 13h screen in the standard
   message colours, and whether a portrait panel opens around a line is decided
   by that entry's own token stream rather than here.

   Nothing guards the deployment and nothing records that it ran, so calling it
   again deploys the same wave again; what makes each wave arrive once is the
   map's turn table naming the slot once per turn.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before the counter is read and never read
   back, so any index, in range or not, behaves the same.

   Table slot 0, and map02.dat's turn-event table is the only shipped file that
   names it -- twelve records, {3, 0, 0} through {14, 0, 0}. */
extern void fdps_chapter_03_event_deploy_wave_for_turn(int unit_index);
#pragma aux fdps_chapter_03_event_deploy_wave_for_turn "*" parm caller [];

/* Chapter 3's ambush: the first unit to step onto the trigger region in the
   middle of map 2 sets off one line of the chapter's own text block and brings
   the resident map's wave 1 onto the field.

   It fires at most once per chapter.  Element 0x10 of
   data_fdps_map_cell_event_triggered_flags (gamedata.h) is the latch: the
   handler returns without touching anything when it is already non-zero, and
   sets it to 1 before it draws or deploys.  The chapter state reset clears that
   array, so a later chapter's handler starts from a clean latch; the save image
   carries it, so a chapter reloaded after the ambush fired does not fire it
   again.  It is the same slot chapter 5's ambush latches, which is safe only
   because one chapter is loaded at a time.

   The line is entry 0x12, drawn straight onto the mode 13h screen in the
   standard message colours before the deployment, so it is on screen by the
   time the enemies appear; whether a portrait panel opens around it is decided
   by that entry's own token stream rather than here.

   The wave asked for is the literal 1 -- nothing here reads the turn counter --
   and the placement file is the literal "map02.cod" and not the chapter global
   the chapter 10, 17 and 18 handlers read, so the arriving units take map 2's
   coordinates whatever chapter is loaded.  Which units arrive is not map 2's to
   say: the deployment records come from whichever MAP%02d.DAT is resident, and
   in chapter 3 the wave-1 records are five level-5 units of enemy id 0x52.
   They are placed with the flag that searches for the nearest free walkable
   tile rather than the flag that takes the placement record's own tile as
   given.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before the latch is even tested and never
   read back, so any index, in range or not, behaves the same.

   Table slot 1, and chapter 3's map02.dat is the only shipped file that names
   it -- one tile trigger, occasion 0, over the 24 cells of M02.DTL that carry
   event code 1, so the event trips on the step onto any of them. */
extern void fdps_chapter_03_event_deploy_wave_1(int unit_index);
#pragma aux fdps_chapter_03_event_deploy_wave_1 "*" parm caller [];

/* Scripted defeat: ends the current battle as a loss.  Stores 1 -- the defeat
   code -- into data_fdps_chapter_event_or_battle_end_code (gamedata.h) and
   returns, with no test of any kind in front of the store, so calling it ends
   the battle unconditionally and overwrites whatever code was already there,
   including the 2 that means the chapter was cleared.

   The battle does not stop inside this call.  The code is only a flag; the
   phase loop reads it at the top of its next iteration and returns, and the
   caller of the loop is what runs the game-over sequence.

   unit_index is the handler table's shared parameter and is ignored: this
   handler reads no unit record, so any index, in range or not, behaves the
   same.

   Table slot 2, and no shipped map file selects it -- scripted defeat is
   written in the data as a death-script opcode instead, which the death-script
   runner handles inline.  It is emitted anyway because the table is indexed
   unchecked by a byte out of the map file and the slot has to keep its
   number. */
extern void fdps_chapter_event_set_game_over(int unit_index);
#pragma aux fdps_chapter_event_set_game_over "*" parm caller [];

/* Chapter 2's turn-10 event: takes seven of the cave's enemies off the
   hold-position behaviour the map deploys them in and puts them on the default
   one, which paths a unit toward the nearest opposing unit.

   It rewrites the low nibble -- the behaviour code -- of the ai_behavior byte
   at record offset 0x34 to 0 for unit indices 8, 9, 0x0d, 0x11, 0x13, 0x14 and
   0x15, and leaves the high nibble of that byte alone, because bits 0x40 and
   0x80 of it are independent AI flags other code reads on their own.  The
   seven indices are literals; nothing is range checked and
   data_fdps_map_unit_count is not consulted, so the indices are only correct
   against chapter 2's own deployment.  Each record is resolved through
   fdps_get_unit_record per iteration, so the array base is re-read.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before anything else and never read, so
   any index, in range or not, behaves the same.

   Table slot 3, and chapter 2's map01.dat is the only shipped file that names
   it -- one turn-event record, turn 10, so the event fires once per playthrough
   of that chapter. */
extern void fdps_chapter_02_event_enemies_advance(int unit_index);
#pragma aux fdps_chapter_02_event_enemies_advance "*" parm caller [];

/* Chapter 3's tile-triggered reinforcement: brings on the four enemies map 2
   tags as wave 14, once, and speaks a line of the chapter's own text as they
   arrive.

   It fires only while both of two conditions hold: its one-shot latch, element
   0x11 of data_fdps_map_cell_event_triggered_flags (gamedata.h), is still 0,
   and the record unit_index names is not on side 0.  The side test is a plain
   test against 0 over the whole unsigned byte, and side 0 is the enemy, 1 the
   guest and 2 the player's roster -- so what it keeps out is an enemy stopping
   on the trigger tile, while the player's units and the guests spring it
   alike.  The record is resolved through fdps_get_unit_record (unit.h) before
   either test and is not range checked, so an index outside the live unit array
   reads whatever lies at that stride; the latch is tested first, so a refused
   call never dereferences it.

   When it fires it puts the latch up first, then draws text entry 0x16 through
   fdps_draw_text (text.h) straight onto the mode 13h aperture, then calls
   fdps_deploy_wave (deploy.h) with map number 2, wave 14 and place_exact 0 --
   so the line is on screen before the enemies appear, and they land on the
   nearest free walkable tile to their placement records rather than on the
   records' own coordinates.  The map number is the literal 2 and not the
   chapter global the chapter 10, 17 and 18 handlers read there.

   The latch slot is 0x11 and not the 0x10 the rest of the family shares,
   because map02.dat names two handlers at once -- cell event code 1 reaches the
   ambush above and code 2 reaches this one -- so the two are live together and
   one shared byte would let whichever fired first suppress the other.  Being
   inside the flag array is what makes the latch survive a save and what gets it
   cleared when the next chapter starts; neither survives rewriting it as a
   function-local static.

   Table slot 4, named by MAP02.DAT's cell event code 2 with trigger kind 0 and
   by no other shipped map, which is what makes this chapter 3.  Kind 0 is the
   occasion a unit finishes stepping onto the cell during movement
   (maptile.h), so the dispatchers that reach it are the ones that pass a real
   unit index; the turn-event runner, which passes a constant 0, does not name
   this slot in the shipped data. */
extern void fdps_chapter_03_event_deploy_wave_14(int unit_index);
#pragma aux fdps_chapter_03_event_deploy_wave_14 "*" parm caller [];

/* Chapter 5's ambush: takes unit indices 6 through 0x22 inclusive off the
   hold-position behaviour the map deploys them in and puts them on the default
   one, which paths a unit toward the nearest opposing unit, so the whole
   imperial army starts advancing at once.

   It rewrites the low nibble -- the behaviour code -- of the ai_behavior byte
   at record offset 0x34 to 0 across that range and leaves the high nibble
   alone, because bits 0x40 and 0x80 of it are independent AI flags other code
   reads on their own.  The two bounds are literals; nothing is range checked
   and data_fdps_map_unit_count is not consulted, so they are only correct
   against chapter 5's own deployment, which puts 5 party records at indices
   0..4, the guest hero at 5 and everything the opening script deploys at
   6..0x22.  Each record is resolved through fdps_get_unit_record per
   iteration, so the array base is re-read.

   It fires at most once per chapter.  Element 0x10 of
   data_fdps_map_cell_event_triggered_flags (gamedata.h) is the latch: the
   handler returns without touching anything when it is already non-zero, and
   sets it to 1 before running the loop.  The chapter state reset clears that
   array, so a later chapter's handler starts from a clean latch; the save
   image carries it, so a chapter reloaded after the ambush fired does not fire
   it again.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before the latch is even tested and is
   never read, so any index, in range or not, behaves the same.

   Table slot 7, and chapter 5's map04.dat is the only shipped file that names
   it -- one tile trigger, phase 0, so the event fires the moment a unit walks
   onto that tile. */
extern void fdps_chapter_05_event_enemies_advance(int unit_index);
#pragma aux fdps_chapter_05_event_enemies_advance "*" parm caller [];

/* Chapter 7's turn-2 event: takes unit indices 4 through 8 inclusive off the
   hold-position behaviour and puts them on the default one, which paths a unit
   toward the nearest opposing unit, so the arena's champion stops holding her
   ground.

   It rewrites the low nibble -- the behaviour code -- of the ai_behavior byte
   at record offset 0x34 to 0 across that range and leaves the high nibble
   alone, because bits 0x40 and 0x80 of it are independent AI flags other code
   reads on their own.  The two bounds are literals; nothing is range checked
   and data_fdps_map_unit_count is not consulted, so they are only correct
   against chapter 7's own deployment, which puts 4 party records at indices
   0..3 and the five enemies at 4..8.  Each record is resolved through
   fdps_get_unit_record per iteration, so the array base is re-read.

   Four of the five are deployed in behaviour code 0 already and the loop is a
   no-op for them; index 8, the champion, is the one deployed holding position
   and the one the event is for.  The range covers the whole enemy force because
   that is how the shared helper is called, not because the other four need it.

   There is no one-shot latch, unlike the chapter 5 handler: nothing in the body
   guards the loop and nothing records that it ran, so calling it again runs it
   again.  The merge is idempotent, so a second firing changes nothing, but a
   unit the AI has since moved into another behaviour mode would be pushed back
   to mode 0.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before anything else and never read, so
   any index, in range or not, behaves the same.

   Table slot 9, and chapter 7's map06.dat is the only shipped file that names
   it -- one turn-event record, turn 2, phase 0, so the event fires once per
   playthrough of that chapter. */
extern void fdps_chapter_07_event_enemies_advance(int unit_index);
#pragma aux fdps_chapter_07_event_enemies_advance "*" parm caller [];

#endif
