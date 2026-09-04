/* item.h -- applying an item or spell effect to a list of battle-map targets,
 * and the in-battle item menu.
 *
 * A target list is an array of BYTE unit indices into the map unit array
 * reached through data_fdps_map_unit_array_ptr (src/gamedata.h), produced by
 * the collectors in aitarget.h; the record layout is struct fdps_unit_record in
 * src/fdpstype.h.  Nothing here range checks an index and nothing here owns
 * state.
 *
 * The effect functions in this file share one shape: they walk the target list
 * applying the effect and QUEUEING a popup per target, and only then drain the
 * queue with one fdps_play_indicator_queue (indicat.h), so every target's
 * number appears together after the whole effect has already landed.
 */
#ifndef ITEM_H
#define ITEM_H

/* 00026230.  Applies one damaging item or spell effect to a whole target list:
   each target takes the damage and gets its number queued over it, and the
   queue is played out once at the end.

   target_count is signed and the loop guard is a signed JL at 00026249, so a
   count of zero or below hits nothing -- and then the queue this function
   drains is whatever the caller left in it, which for the shipped caller is
   empty, so the call costs nothing and the screen is left alone.

   target_ids is target_count BYTES, each widened UNSIGNED (MOV AL / AND
   EAX,0xff at 0002625f and 00026278), so an id of 0x82 names unit 130.  A unit
   listed twice is damaged twice: nothing here deduplicates the list.

   base_damage is the effect's power before the roll; fdps_unit_apply_damage
   (unitstat.h) turns it into base_damage * 9 / 10 plus a random bonus.

   THE NUMBER FLOATED OVER A TARGET IS THE ROLL, NOT THE HP THAT WAS TAKEN OFF.
   The value handed to the popup is fdps_unit_apply_damage's return value, which
   is the figure it rolled before the clamp at zero, so a unit on 3 HP hit for a
   rolled 20 is left on 0 and still shows 20.  Recomputing the popup value as
   the HP difference, which is the obvious way to write it, changes what the
   player sees on every overkill.

   The digits are drawn with glyph base 0, the damage digit set; the healing
   path at fdps_apply_heal_to_targets passes 0x27 for its own. */
extern void fdps_apply_damage_to_targets(int target_count,
                                         unsigned char *target_ids,
                                         int base_damage);
#pragma aux fdps_apply_damage_to_targets "*" parm caller [];

#endif
