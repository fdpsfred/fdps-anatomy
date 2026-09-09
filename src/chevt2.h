/* chevt2.h -- the scripted chapter-event handlers of chapter 8.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and a
 * handler that has no unit to work on ignores it -- the turn-event dispatcher
 * passes 0 when the event is turn-scheduled rather than unit-attached, while
 * the cell search and the death-script runner pass a real index.
 *
 * chevt1.h holds the same family for chapters 2 to 7 and chevt2b.h for
 * chapters 9 to 14.  Nothing here owns state.
 */
#ifndef CHEVT2_H
#define CHEVT2_H

/* Chapter 8's turn-scheduled event: the one handler slot map07.dat names for
   all five of the chapter's turn events, running whichever of them is due for
   the turn the player has just finished.

   The body is an if/else ladder of five equality tests on the battle turn
   counter with no default arm, so a turn the ladder does not name does
   nothing at all.  Turn 1 paints the chapter's opening orders; turn 3 plays
   the cut-scene that brings the guest mage onto the map; turn 4 plays the
   cut-scene at the cell block and then re-aims the guard left standing there;
   turns 10 and 12 bring on the two cavalry waves and speak a line for each.

   NOTHING IN IT IS GUARDED.  There is no one-shot latch, no liveness test and
   no check against the live unit count: turn 4 plays its scene and rewrites
   unit 0x0e's behaviour byte whether or not the player has already killed the
   two guards, and the check that looks obviously missing would suppress a
   message the original still paints.

   The turn-4 rewrite keeps the high nibble of the byte at record offset 0x34,
   because the four bits it holds are AI flags other code reads on their own.
   The record is resolved through fdps_get_unit_record (unit.h).

   unit_index is the handler table's shared parameter.  This handler zeroes it
   before anything reads it, so nothing a dispatcher passes can change what it
   does; the store cannot be seen by the caller either, because the slot
   belongs to the caller's outgoing argument area.  The turn-event runner is
   the only dispatcher that reaches this slot and it passes 0. */
extern void fdps_chapter_08_event_for_turn(int event_arg);
#pragma aux fdps_chapter_08_event_for_turn "*" parm caller [];

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

#endif
