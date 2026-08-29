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

#endif
