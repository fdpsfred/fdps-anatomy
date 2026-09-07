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

/* Chapter 3's turn-limit defeat: speaks the chapter's last line, brings on the
   resident map's wave 15 and marks the battle lost, in that order and with
   nothing between them.

   It has no guard at all -- no one-shot latch, no turn test and no test of the
   battle-end code it is about to write -- so every call draws, deploys and
   stores again.  What makes it fire once is map02.dat's turn-event table, which
   names this slot in a single record, turn 0x16 on side 0, and nothing else in
   the shipped data names slot 5.

   The store is the literal 1, the defeat code of
   data_fdps_chapter_event_or_battle_end_code (gamedata.h), written
   unconditionally: a chapter already marked cleared becomes a defeat.  It is
   only a flag, so the handler returns normally and the phase loop it returns
   into is what stops.

   The deployment is fdps_deploy_wave (deploy.h) with map number 2, wave 15 and
   place_exact 0 -- the literal 2 and not the chapter global the chapter 10, 17
   and 18 handlers read there, so the arrivals take map 2's coordinates whatever
   chapter is loaded, and they land on the nearest free walkable tile to their
   placement records rather than on the records' own coordinates.  Map 2 tags 44
   records with wave 15, which is the largest arrival the family asks for.

   THE HANDLER NEVER SHOWS WHAT IT DEPLOYED.  The store to the battle-end code
   is the instruction after the deployment and the handler then returns, so on
   the code alone the 44 units enter the array and the battle ends; whatever the
   player sees at that point comes from the battle-end path.  The sibling that
   does want its arrivals seen -- the chapter 4 turn handler -- spells the cursor
   move and the render frames out, so a redraw added here would be a frame the
   original does not draw.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before anything else and never read back,
   so any index, in range or not, behaves the same.

   Table slot 5, and chapter 3's map02.dat is the only shipped file that names
   it -- one turn-event record, turn 0x16, side 0, so the event fires on the
   enemy pass of turn 22, the turn the chapter's stated deadline expires on. */
extern void fdps_chapter_03_event_turn_limit_game_over(int unit_index);
#pragma aux fdps_chapter_03_event_turn_limit_game_over "*" parm caller [];

/* Chapter 4's turn-scheduled event, and the only handler of the family that
   does two different things on two different turns.

   On turn 3 it draws one entry of the chapter's own text block and returns,
   deploying nothing and touching no unit.  On every other turn -- turn 5 is the
   only other one map03.dat schedules it for -- it brings the resident map's
   wave-5 records onto the battlefield, walks the map cursor onto unit 0x1f so
   the view sits over the arrivals, holds it there for twelve composed frames,
   draws the announcement entry, and then puts unit indices 0x0f through 0x12
   inclusive and unit index 0x0d onto the advancing behaviour.

   THE ONLY TURN IT COMPARES AGAINST IS 3.  There is no test for 5 and no
   one-shot latch, so any turn other than 3 runs the whole reinforcement path;
   what limits it to two firings is the map file naming the slot twice.

   The map number handed to the deployment is
   data_fdps_chapter_current_chapter_id and not a literal, unlike the four
   chapter 3 handlers above, so the arrivals take the loaded chapter's own
   MAP%02d.COD coordinates.  They are placed with the flag that searches for the
   nearest free walkable tile rather than the one that takes the record's tile
   as given.

   The behaviour rewrite is a merge and not an assignment: the low nibble of the
   ai_behavior byte at record offset 0x34 goes to 0 and the high nibble is
   carried across, because bits 0x40 and 0x80 of that byte are independent AI
   flags other code reads on their own.  Both ranges are inclusive of their last
   index.  Nothing is range checked and data_fdps_map_unit_count is not
   consulted, so the five indices are only correct against map03's own
   deployment; each record is resolved through fdps_get_unit_record per
   iteration, so the array base is re-read.

   The cursor unit, 0x1f, is one of the seven records the deployment on the line
   before it has just appended, so it only names an arrival when the call that
   precedes it actually deployed.

   unit_index is the handler table's shared parameter and is ignored, but for a
   different reason from the rest of the family: on the reinforcement path the
   incoming slot is overwritten with 0 and used as the twelve-frame counter, and
   on the speaking path it is never touched at all.  Either way nothing reads
   what came in, and the turn-event runner pushes a literal 0.

   Table slot 6, and chapter 4's map03.dat is the only shipped file that names
   it -- two turn-event records, turns 3 and 5, both phase 0, so both firings
   are on the enemy pass, immediately after the player's third and fifth turns
   end. */
extern void fdps_chapter_04_event_for_turn(int unit_index);
#pragma aux fdps_chapter_04_event_for_turn "*" parm caller [];

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

/* Chapter 6's cavalry-death ambush: the map's second wave of enemy
   reinforcements marches in at the lower left, the chapter's guest hero speaks
   over it, and every unit already on the field is released from hold-position
   into the all-out attack.

   It runs in four steps and the order is what the player sees: the wave-2
   records of the resident MAP%02d.DAT are appended through fdps_deploy_wave
   (deploy.h), the map cursor and with it the view is walked onto unit 0x1e --
   the second of the seven arrivals, so the index is only correct after that
   deployment -- the view is held there for twelve composed frames, and only
   then is text entry 0x0d drawn through fdps_draw_text (text.h) straight onto
   the mode 13h aperture in the standard message colours.  Entry 0x0d opens with
   the portrait code -0x11 and character id 0x0c, map05.dat's one side-1 record,
   so the guest hero is the speaker.

   The map number handed to the deployment is
   data_fdps_chapter_current_chapter_id and not a literal, as it is for the
   chapter 4 handler and unlike the four chapter 3 ones, so the arrivals take
   the loaded chapter's own MAP%02d.COD coordinates.  They are placed with the
   flag that searches for the nearest free walkable tile rather than the one
   that takes the record's tile as given, which matters here because the guest
   hero already stands inside the block those seven records name.

   The behaviour rewrite is unit indices 4 through 0x22 inclusive, and it is a
   merge and not an assignment: the low nibble of the ai_behavior byte at record
   offset 0x34 goes to 0 and the high nibble is carried across, because bits
   0x40 and 0x80 of that byte are independent AI flags other code reads on their
   own.  Nothing is range checked and data_fdps_map_unit_count is not consulted,
   so the two bounds are only correct against chapter 6's own deployment, which
   puts 4 party records at indices 0..3 and everything else at 4..0x23; each
   record is resolved through fdps_get_unit_record per iteration, so the array
   base is re-read.  THE LOW BOUND IS 4 AND NOT 6: the range takes in the guest
   hero at index 5 and pulls him out of the behaviour mode he deployed in, which
   is the one place this handler differs from chapter 5's copy of the same loop.

   There is no guard of any kind -- no one-shot latch, no turn test -- so a
   second call deploys the wave a second time.  What makes it fire once is the
   map file naming the slot on one unit's death script.

   unit_index is the handler table's shared parameter and is ignored: it is
   overwritten with 0 after the deployment and the cursor move and then used as
   the twelve-frame counter, so nothing reads what came in.

   Table slot 8, named by map05.dat's deployment record 7 as the {opcode 2,
   operand 8} death script of the level-8 cavalryman it puts at unit index 0x0c
   on tile (23, 8) in the lower right, and by nothing else in the shipped data.
   The death-script runner is the only dispatcher that reaches it. */
extern void fdps_chapter_06_event_deploy_wave_2(int unit_index);
#pragma aux fdps_chapter_06_event_deploy_wave_2 "*" parm caller [];

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
