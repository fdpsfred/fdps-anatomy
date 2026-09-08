/* chevt4.h -- the scripted chapter-event handlers of chapters 20 to 23.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and a
 * handler that has no unit to work on ignores it -- the turn-event dispatcher
 * passes 0 when the event is turn-scheduled rather than unit-attached, while
 * the cell search and the death-script runner pass a real index.
 *
 * chevt1.h holds the same family for chapters 2 to 7, chevt2.h for 8 to 14 and
 * chevt3.h for 15 to 19.  Nothing here owns state.
 */
#ifndef CHEVT4_H
#define CHEVT4_H

/* Chapter 20's sword-upgrade event: Randis steps onto the map's trigger tile
   still carrying 灼烈之劍 and the fire god takes it off him, leaving 火光之劍
   in its place.

   Three things have to hold, and the handler does nothing at all unless all
   three do: the unit that stepped on the tile is battle unit 0, which is
   Randis, who is always the first unit deployed; the battle is no more than
   twenty turns old; and the sword is really in the bag.  The search for the
   sword runs before any of the three are tested, so it costs a walk of the
   inventory on every firing including the ones that go no further.

   THE DEADLINE IS INCLUSIVE.  Turn 20 still upgrades the sword and turn 21 does
   not, and the counter this is read off starts the battle at 1.  Nothing else
   ever closes the event -- see below.

   The line spoken is entry 0x13 of the chapter's own text block, which is the
   last of FDETXT20.TXT's twenty strings, and it is spoken before the swap.  The
   sword leaves the bag through the slot the search returned and 火光之劍 goes
   into the first empty entry, which is the one the removal's compaction just
   opened, so the upgraded sword ends up where the old one was.  The four
   derived combat stats are rebuilt afterwards because the weapon the unit is
   wearing has changed.

   THERE IS NO ONE-SHOT LATCH AND THE CELL IS NEVER MARKED CONSUMED.  The item
   held is the flag: once 灼烈之劍 has become 火光之劍 the slot search misses
   and the event cannot fire again.  That is not the same thing as a latch, and
   adding one would break the event in a way nothing else would show -- a unit
   that is not Randis has to be able to walk over the tile and leave it armed
   for him.  Equally, the handler must not be given a private static: the event
   is per chapter, not per process.

   Its twin is fdps_chapter_25_event_upgrade_randis_sword, the same body without
   the turn gate, which swaps 火光之劍 for 真炎龍劍.  Whether the player ever
   sees that one is decided here, on turn 20 of chapter 20.

   Table slot 28, and chapter 20's map19.dat is the only shipped file that names
   it: the first entry of its tile-event table, with occasion 0 -- the occasion
   a unit reports as it finishes stepping onto a cell. */
extern void fdps_chapter_20_event_upgrade_randis_sword(int unit_index);
#pragma aux fdps_chapter_20_event_upgrade_randis_sword "*" parm caller [];

/* Chapter 21's first ambush: a unit of the player's own side finishes a step
   onto the map's trigger tile, the sixteen enemies MAP20.DAT tags wave 1 come
   on, and the chapter's line about them is spoken.

   Two things have to hold and the handler does nothing at all unless both do:
   the shared one-shot latch is still down, and the unit that stepped on the
   tile is on side 2.  THE SIDE TEST IS AN EQUALITY AND NOT THE FAMILY'S "NOT
   0": side 1, the guest side, is refused here where the chapter 10 and chapter
   19 ambushes admit it, so a guest walking over the tile leaves it armed.  The
   record is fetched before either test is made, so a firing that goes no
   further still costs one lookup in the unit array.

   The sixteen are placed on the nearest free walkable tile to each record's
   scripted coordinates rather than on those coordinates verbatim, and they are
   appended to the unit array, so whatever is already on the map stays on it.
   The map they are read from is whichever chapter is loaded, not a number this
   handler holds.  The line is entry 0x14 of the chapter's own text block and it
   is spoken after the deployment, so the enemies are already standing on the
   map behind the message.

   The latch is raised last, after the line.  It is the shared byte the
   chapter reset clears and the save file carries, NOT a flag private to this
   handler -- a static in its place would leave the ambush spent across a
   chapter restart and across a reload.  Any non-zero value in the slot blocks
   the body, and the handler writes exactly 1.

   Table slot 29, and chapter 21's map20.dat is the only shipped file that names
   it: tile-event entry 1, with occasion 0 -- the occasion a unit reports as it
   finishes stepping onto a cell.  The same map's entry 2 routes to slot 30,
   fdps_chapter_21_event_deploy_wave_2. */
extern void fdps_chapter_21_event_deploy_wave_1(int unit_index);
#pragma aux fdps_chapter_21_event_deploy_wave_1 "*" parm caller [];

/* Chapter 21's second ambush: a unit of the player's own side finishes a step
   onto the map's second trigger tile, the sixteen enemies MAP20.DAT tags wave 2
   come on, the chapter's line about them is spoken, and then the whole garrison
   is put back on the default advance.

   Two things have to hold and the handler does nothing at all unless both do:
   its own one-shot latch is still down, and the unit that stepped on the tile
   is on side 2.  THE SIDE TEST IS AN EQUALITY AND NOT THE FAMILY'S "NOT 0", so
   a guest walking over the tile leaves it armed.  The record is fetched before
   either test is made, so a firing that goes no further still costs one lookup
   in the unit array.

   THE LATCH IS NOT THE ONE fdps_chapter_21_event_deploy_wave_1 USES.  It is the
   next element of the same shared array, and it has to be: the same map names
   both handlers, so the two tiles are armed at the same time and one byte
   between them would let whichever was tripped first disarm the other.  Like
   its neighbour it is the shared byte the chapter reset clears and the save
   file carries, NOT a flag private to this handler.  Any non-zero value in the
   slot blocks the body, and the handler writes exactly 1.

   The sixteen are placed on the nearest free walkable tile to each record's
   scripted coordinates rather than on those coordinates verbatim, and they are
   appended to the unit array.  The map they are read from is whichever chapter
   is loaded, not a number this handler holds.  The line is entry 0x15 of the
   chapter's own text block -- the entry after the wave-1 line -- and it is
   spoken after the deployment.

   THE ADVANCE IS THE LAST THING IT DOES AND IT IS INCLUSIVE AT BOTH ENDS.  Unit
   indices 0x0b through 0x50, which is every unit on the map that is not one of
   the eleven party members, have the low nibble of their behaviour byte cleared
   to mode 0, the default chain that paths a unit toward the nearest opponent;
   MAP20.DAT authors those units in mode 2, which holds position.  0x50 is
   written and is not one past the end, so the half-open spelling leaves the
   map's last unit standing still.  The high nibble is a read-modify-write and
   is preserved, because bits 0x40 and 0x80 of the same byte are independent AI
   flags.  Neither bound is read from a unit count and nothing bounds them.

   Table slot 30, and chapter 21's map20.dat is the only shipped file that names
   it: tile-event entry 2, with occasion 0 -- the occasion a unit reports as it
   finishes stepping onto a cell.  The same map's entry 1 routes to slot 29,
   fdps_chapter_21_event_deploy_wave_1. */
extern void fdps_chapter_21_event_deploy_wave_2(int unit_index);
#pragma aux fdps_chapter_21_event_deploy_wave_2 "*" parm caller [];

/* Chapter 22's turn-scheduled event: the one handler MAP21.DAT names for all
   five of the chapter's turn events, running whichever of them is due for the
   turn the player has just finished.

   It is an if/else ladder of five equality tests on the battle turn counter and
   it has no default branch, so on any other turn it does nothing at all.  The
   incoming argument is never read: the dispatcher pushes 0 for a turn-scheduled
   event and two of the branches write over the slot and count their hold frames
   in it.

   turn 1 -- speaks the chapter's line 0x0c and brings on wave 1, the sixteen
   reinforcements that arrive in the three corridors and behind the boss.
   turn 3 -- line 0x0d and wave 2, eight units beside the boss.
   turn 5 -- line 0x0e and wave 3, four units; pans the view to the map's north
   west and north east and holds twelve frames at each; then line 0x0f and wave
   5, four more; the same pair of pans again; then line 0x10.
   turn 8 -- line 0x11 and wave 4, twenty-two units, then three pans: north
   west, north east and south east.
   turn 9 -- no line and no deployment.  The boss at unit index 0x0b is moved to
   behaviour mode 0x0b, its own scripted chase, and every unit from 0x0c to 0x41
   -- all 54 the five waves have put on the map -- is handed back to mode 0, the
   default advance, so the whole map starts moving at once.

   THE TURN-TO-WAVE MAPPING IS NOT THE IDENTITY.  Turn 5 deploys wave 3 and then
   wave 5, and turn 8 deploys wave 4.  Ordering the deployments by wave number
   puts wave 4's twenty-two units on the map three turns early and holds wave 5's
   four back to turn 8.

   THE BEHAVIOUR WRITES ARE READ-MODIFY-WRITE ON THE LOW NIBBLE.  Byte 0x34 of a
   unit record is packed: the low nibble is the behaviour mode and the high
   nibble carries AI flags other code tests, so assigning the mode whole clears
   them.  Both index ranges are inclusive at both ends and neither is bounded
   against the map's unit count.

   THE PANS BLANK THE MAP CURSOR AND PUT IT BACK.  Turn 5 blanks it for each of
   its two pan pairs and restores it between them, so the line it speaks in the
   middle is spoken with the cursor on the map; turn 8 blanks it once across all
   three of its pans.  Both branches leave the cursor mode on 1, which is what a
   chapter runs in.  Each hold is twelve frames and each frame waits for the
   timer tick, so the count is the dwell the player reads the map in.

   Table slot 31, and chapter 22's map21.dat is the only shipped file that names
   it: all five live entries of its turn-event table route to this slot, with
   phase 0 -- the pass the turn advance runs as the player's phase ends. */
extern void fdps_chapter_22_event_for_turn(int event_arg);
#pragma aux fdps_chapter_22_event_for_turn "*" parm caller [];

/* Chapter 22's boss-death event: the chapter is won, and if 法蓮娜 struck the
   killing blow with room in her bag the dying 巫湯婆婆 speaks her line and
   死神契約 changes hands.

   The two gates are the acting unit's character id -- 1, 法蓮娜 -- and her bag
   count, which must not be 8; either failing skips the line, the gift and the
   store into the boss's record.  What is NOT skipped is the last thing the
   handler does: the battle end code is set to the chapter-cleared value on
   every path, and that store is chapter 22's whole victory condition, because
   the chapter's post-action handler only tests for defeat.

   A full bag loses the item silently, and that is the original behaviour: the
   reward chapter 23 hands out for another killing blow by 法蓮娜 is given only
   to a 法蓮娜 carrying 死神契約, and that reward is what opens the hidden
   chapter.

   The boss is unit index 0x0b and her whole flags byte is cleared before the
   line is drawn.  It reads like a poke at a corpse and is not: it takes back
   the removed bit the death sequence has just set, and the line's portrait
   token looks its speaker up through a search that skips retired units, so
   without the store the dying line is spoken by nobody.

   Table slot 32, reached only through the table: the boss's deployment record
   in MAP21.DAT carries the death script that names it, and the runner passes
   the index of the unit that was acting when she died. */
extern void fdps_chapter_22_event_boss_defeat(int unit_index);
#pragma aux fdps_chapter_22_event_boss_defeat "*" parm caller [];

/* Chapter 23's boss-death event: the chapter is won and the dying 死神 speaks
   its line, and if 法蓮娜 struck the killing blow carrying 死神契約 the
   contract is taken off her and 反禁制器 put in its place.

   What happens on every firing is the dying line and the victory: the boss's
   record is un-retired, its line is drawn, and the battle end code is set to
   the chapter-cleared value.  Killing the 死神 is chapter 23's whole victory
   condition.  Only the exchange is gated, on two tests -- the acting unit's
   character id must be 1, 法蓮娜, and 死神契約 must really be in her bag --
   and the search for the contract runs whether or not the id matched.

   The boss is unit index 0x20 and its whole flags byte is cleared before the
   line is drawn.  It reads like a poke at a corpse and is not: it takes back
   the removed bit the death sequence has just set, and the line's portrait
   token looks its speaker up through a search that skips retired units, so
   without the store the dying line is spoken by nobody.

   反禁制器 is handed over by removing the contract and adding the new item, so
   it lands in the first entry left empty by the repack rather than in the slot
   the contract vacated -- the same slot only when the bag has no other hole.

   Table slot 33, reached only through the table: the boss's deployment record
   in MAP22.DAT carries the death script that names it, and the runner passes
   the index of the unit that was acting when it died. */
extern void fdps_chapter_23_event_boss_defeat(int unit_index);
#pragma aux fdps_chapter_23_event_boss_defeat "*" parm caller [];

#endif
