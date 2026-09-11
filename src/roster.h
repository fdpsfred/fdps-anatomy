/* roster.h -- the party roster: the character list that travels between
 * chapters.
 *
 * The roster is one heap block of 32 records of struct fdps_unit_record --
 * the same record type the battle/map unit array holds -- reached through
 * data_fdps_roster_array_ptr, with data_fdps_roster_member_count saying how
 * many slots are occupied.  Record i starts at base + i * 0x50;
 * fdps_get_roster_record in table.c is the accessor that says so.
 *
 * A roster record's four derived combat stats (ap, dp, hit, ev at +0x48
 * onwards) are not stored by whoever changes a base stat or moves an item:
 * they are recomputed from scratch by the function below, and every path that
 * edits a roster record's equipment has to call it afterwards.
 */
#ifndef ROSTER_H
#define ROSTER_H

/* Banks the party at the end of a battle.  Walks every battle unit index
   0..data_fdps_map_unit_count-1, resolves the record through
   fdps_get_unit_record (unit.h), and for every roster slot
   0..data_fdps_roster_member_count-1 whose char_id byte matches copies the
   whole 0x50-byte record over the slot and then resets what must not survive
   the chapter: the six status timers are cleared, the flags byte is masked
   down to bit 0, every member who is not retired is restored to full HP, every
   member including the dead is restored to full MP, an experience count above
   99 is zeroed, and the slot's derived combat stats are recomputed by the
   function below.

   There is no break on a match, so a character id held by two slots is written
   into both.  One case is exempt: character id 0 -- Randis -- whose unit
   fdps_unit_is_retired reports has left the field is skipped and his roster
   record is left as it was.  A retired unit of any other id is banked
   normally.

   Neither count is bounded against the roster block's 32 slots and neither
   base pointer is null-checked.  Takes nothing and returns nothing; the result
   is the rewritten roster array. */
extern void fdps_roster_write_back_battle_units(void);
#pragma aux fdps_roster_write_back_battle_units "*" parm caller [];

/* Recomputes roster member roster_index's four derived combat stats in place
   from that record alone: attack from base ap plus the ap modifier of every
   equipped item, defense likewise from base dp, and hit and evade both from
   the single base dexterity word plus the items' hit and ev modifiers -- there
   is no separate evade base.  Every one of the eight inventory entries counts,
   not just the two equipment slots, and an entry counts when its flag byte has
   bit 0x40 set.  The totals are computed in 32 bits and truncated into the
   record's four 16-bit stat fields.

   The status timers at record +0x22..+0x24 are NOT applied: this is the roster
   computation, and the battle one in fdps_unit_recompute_combat_stats is a
   different function on purpose.

   roster_index is not range-checked and the roster pointer is not checked for
   null. */
extern void fdps_roster_recompute_combat_stats(int roster_index);
#pragma aux fdps_roster_recompute_combat_stats "*" parm caller [];

/* Enrols character char_id as the next roster member: the record at index
   data_fdps_roster_member_count is filled from that character's FRIAPRDA.DAT
   base record and FRILEVUP.DAT growth record, its derived combat stats are
   recomputed by the function above, and the member count is incremented.

   char_id is the portrait id the two table accessors take, and it is written
   into the new record twice, as both portrait_id (+0x07) and char_id (+0x08).
   The new member's side byte is 2.

   The two scalings are NOT the same: maximum HP and MP are the base value plus
   (level - 1) growth steps, while attack, defense and dexterity are the base
   value plus a full level of growth steps.  Both current and maximum HP are
   set to the same number, and so are current and maximum MP.

   Nothing is bounded and nothing is checked: the count is not compared against
   the roster block's 32 slots, and the roster and table base pointers are not
   tested for null.  The record is only partly initialised -- position, facing,
   walk step, the AI fields, the event slot and the bytes at +0x28..+0x30 and
   +0x32 keep whatever the block held, and so do the id bytes of the last two
   bag entries. */
extern void fdps_roster_add_character(int char_id);
#pragma aux fdps_roster_add_character "*" parm caller [];

/* Works out the four combat stats roster member roster_index would have while
   wearing item item_id, without changing anything, and writes them through
   out_stats as four ints in the order attack, defense, hit, evade -- the order
   of the record's own four stat fields at +0x48..+0x4e.  All four are always
   written.

   The seeds are the member's three base stats, hit and evade sharing the one
   dexterity word exactly as fdps_roster_recompute_combat_stats seeds them, and
   the candidate item's own four modifiers are added on top unconditionally.
   Every one of the eight inventory entries is then scanned, and an entry adds
   its modifiers only when it is flagged equipped AND its item type sits on the
   opposite side of the 0x15 weapon/armour split from the candidate's: a
   same-category item would be taken off to make room, so its contribution is
   left out of the total.

   The totals are not narrowed to the record's 16-bit stat fields, and the
   caller's class-restriction check is not made here -- the numbers come out
   whether or not the member could wear the item.  Neither the roster index nor
   either item id is range-checked. */
extern void fdps_roster_preview_combat_stats_with_item(int roster_index,
                                                       int item_id,
                                                       int *out_stats);
#pragma aux fdps_roster_preview_combat_stats_with_item "*" parm caller [];

/* Hands item item_id to every roster member that still has room for it.  Walks
   the indices 0..data_fdps_roster_member_count-1 and gives each survivor of
   two tests one copy of the item through fdps_unit_add_item (unititem.h),
   whose result is discarded; nothing is returned and nothing says which
   members were served.

   The two tests are, in order: a member whose eight inventory entries are all
   occupied -- fdps_unit_item_count answers exactly 8 -- is passed over, and
   roster slot 3 is passed over outright once
   data_fdps_chapter_current_chapter_id has reached 0x17, chapter 24 as the
   player counts them.  That second test is a hardcoded pair of literals, not a
   lookup of who is in the party, and it has no upper bound: slot 3 stays cut
   out for the rest of the game even after the character who was in it rejoins.

   The records are resolved through fdps_get_unit_record and so through
   data_fdps_map_unit_array_ptr, which during the field and village phase
   points at the roster block; the roster pointer itself is not read here.

   item_id is passed on whole and only its low byte reaches the record.  The
   bound is a signed compare, so a negative member count hands out nothing.
   Neither the index nor the id is range checked. */
extern void fdps_roster_add_item_to_all(int item_id);
#pragma aux fdps_roster_add_item_to_all "*" parm caller [];

/* Revives every fallen member of the party roster at the cost of the party's
   gold, and shows the player the bill.  Takes nothing and returns nothing; the
   revived records, the reduced gold total and a rebuilt .CEL sprite cache are
   the whole result.  The twenty-nine chapter-end handlers are its only
   callers.

   The sweep walks roster indices 0..data_fdps_roster_member_count-1 through
   fdps_get_roster_record (table.h).  A member whose current HP word is 0 is
   revived in place -- current HP takes the maximum, the flags byte is cleared
   -- and the fee, that member's class fee per level times its level byte, is
   subtracted from data_fdps_shared_party_total_gold.  With no fallen member
   found the function returns having done nothing else.

   THE FEE IS CHARGED WHATEVER THE PARTY CAN AFFORD.  There is no test before
   the subtraction; the total is only lifted back to 0 after the panel has been
   dismissed, so a party that could not pay ends the chapter broke rather than
   with the revive refused.

   THE PANEL DRAWS NOTHING FROM SEVEN FALLEN MEMBERS UP.  The row guard tests
   the NUMBER of revived members against 7, not the row index, so six is the
   largest party this shows a bill for and seven or more shows an empty frame
   that still waits for a key.  Capping the panel at seven rows instead would
   put a screen in front of the player that the original never draws.

   The panel is animated: it holds until a make code arrives, redrawing one
   frame per timer tick with the members' map icons walking, and each frame
   goes out over a vertical retrace.  Rebuilding the sprite cache is part of
   the setup and it is not put back afterwards: the cache is reseeded from
   ICON.CEL with one group per roster member in roster order, so cache slot n
   belongs to roster member n for whoever draws next. */
extern void fdps_roster_revive_fallen_members(void);
#pragma aux fdps_roster_revive_fallen_members "*" parm caller [];

#endif
