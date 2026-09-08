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

#endif
