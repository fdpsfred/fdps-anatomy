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

/* 00026fd0.  Applies one HP-restoring item or spell effect to a whole target
   list, animation included: the "Posion.saf" effect clip plays over the listed
   units, they flash white, each one is healed and gets its number queued over
   it, and the queue is played out once at the end.  Nothing is returned.

   It does not come back quickly.  The clip is 18 frames held for two ticks
   each and the flash is another eight, so a call costs upwards of 44 timer
   ticks -- about two and a half seconds at the game's 18.2 Hz -- before the
   first point of HP is restored.

   BOTH ANIMATIONS RUN WHATEVER THE COUNT IS.  They are called before the loop
   guard is reached, so a target_count of zero or below still plays the clip and
   the flash in full and only then heals nothing; the queue this function drains
   is then whatever the caller left in it, which for the shipped caller is
   empty.  A rebuild that returned early on an empty list would be silent where
   the original is not.

   target_count is signed and the loop guard is a signed JL at 00027014.

   target_ids is target_count BYTES, each widened UNSIGNED (MOV AL / AND
   EAX,0xff at 00027027 and 00027040), so an id of 0x82 names unit 130.  A unit
   listed twice is healed twice: nothing here deduplicates the list.  The same
   array and count go to both animations unchanged.

   base_heal is the effect's power before the roll; fdps_unit_apply_heal
   (unitstat.h) turns it into base_heal * 9 / 10 plus a random bonus.  The
   shipped caller, fdps_apply_item_effect_to_targets, reaches here with
   identical arguments for use_effect 0x0b, the consumable HP restores, and for
   use_effect 0x20, the healing staves, spears and bows.

   THE NUMBER FLOATED OVER A TARGET IS THE ROLL, NOT THE HP THAT WAS PUT ON.
   The value handed to the popup is fdps_unit_apply_heal's return value, which
   is the figure it rolled before the clamp at maximum HP, so a unit two HP
   short of full healed for a rolled 20 is left at full and still shows 20.
   Recomputing the popup value as the HP difference, which is the obvious way to
   write it, changes what the player sees on every overheal.

   The digits are drawn with glyph base 0x27, the healing digit set;
   fdps_apply_damage_to_targets passes 0 for its own. */
extern void fdps_apply_heal_to_targets(int target_count,
                                       unsigned char *target_ids,
                                       int base_heal);
#pragma aux fdps_apply_heal_to_targets "*" parm caller [];

/* 000262a0.  Uses one item out of a battle unit's bag on a list of targets and
   then settles whatever that killed.  Nothing is returned; both call sites
   throw EAX away.

   The item is named by WHERE IT SITS, not by its id: the id is read out of the
   acting unit's record at offset 0xb + 2 * item_slot, so the caller has to hand
   over a slot the unit really holds.  Its ITEM.DAT record supplies the two
   fields everything below runs on -- the use-effect code at +0x0d and the
   SIGNED use-amount word at +0x0e.

   FOUR EFFECT CODES DO NOTHING AT ALL.  0x05 (空白道具), 0x06 (光之水晶,
   空之魔石), 0x0d (精靈之劍) and 0x1b (封咒手套) match no branch, so using one
   of those items plays nothing, changes nothing and does not consume the item.
   That is the original's behaviour and not a gap to fill in.

   WHICH CODES CONSUME THE ITEM IS NOT A PROPERTY OF THE EFFECT.  Five effects
   come in an item form and a weapon form that do exactly the same thing, and
   only the item form reaches fdps_unit_remove_item: 0x01/0x07 fire, 0x02/0x08
   thunder, 0x03/0x09 ice, 0x04/0x0a earth, 0x0b/0x20 HP restore.  0x1e, the
   beam cannons, is weapon-borne with no item twin and is never consumed.
   Everything else -- the MP restore, the six stat-ups, the two status cures
   and the three named-character upgrades -- is item-only and is consumed on
   the arm that does the work.

   The permanent stat-up codes (0x0f, 0x10, 0x11, 0x12, 0x13, 0x14) and the
   three named-character upgrades (0x21, 0x22, 0x23) act on target_ids[0] ALONE
   however long the list is, and their amounts are constants in the code: every
   one of those items carries a use_amount of 0.

   THE CALL BLOCKS FOR SECONDS AND CAN BLOCK FOR EVER.  Most branches play an
   effect clip, a flash or 25 shake frames, all of which wait on the timer
   interrupt, and the 0x21/0x22/0x23 branches open a modal message window.  The
   death settlement at the end can run a whole reward sequence.  Only the four
   dead codes above and 0x1e come back without touching the frame clock -- and
   0x1e still drains the popup queue.

   target_count and target_ids are the collector's output: a count that is
   signed and an array of unit indices one byte each, each widened UNSIGNED.
   unit_index is the acting unit, which is also who fdps_run_death_scripts pays
   the rewards to at the end. */
extern void fdps_apply_item_effect_to_targets(int unit_index, int item_slot,
                                              int target_count,
                                              unsigned char *target_ids);
#pragma aux fdps_apply_item_effect_to_targets "*" parm caller [];

#endif
