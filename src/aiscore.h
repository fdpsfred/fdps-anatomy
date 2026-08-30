/* aiscore.h -- how the map AI weighs the candidate actions it has found.
 *
 * A scorer here is handed one thing the acting unit could do -- an item, a
 * spell, an attack -- together with the units a target collector (aitarget.h)
 * says that action would reach, and answers how desirable doing it would be as
 * a single number.  Nothing here changes the battle state or picks anything:
 * the caller compares the numbers and keeps the largest.
 *
 * The scales are per-scorer and are not comparable across them beyond the one
 * threshold fdps_map_actor_take_best_action applies to the winning total.
 */
#ifndef AISCORE_H
#define AISCORE_H

/* How much is using item_id on these targets worth?  target_unit_indices is
   target_count battle unit indices, one byte each; the per-target scores are
   summed and the sum returned.

   Only two ITEM.DAT use_effect codes are weighed at all.  0x0b, the HP
   restoratives, scores each target on how hurt it is -- 8 when its current HP
   is at or below a third of its maximum, 3 when at or below half, 0 otherwise
   -- and triples that when bit 0x80 of the target's ai_behavior is set.  The
   item's own use_amount is not consulted there, so every restorative scores
   alike.  0x1e, the line-shaped damage items, scores 0x12 when the target's
   current HP is at or below use_amount and 8 when it is above, so a killing
   hit outweighs a wounding one.  Every other use_effect code, including 0,
   scores 0 for every target and the function returns 0.

   item_id is not range checked and the record is resolved through
   fdps_get_item_record (table.h) on entry; each target index is resolved
   through fdps_get_unit_record (unit.h) as the walk reaches it, so a record
   pointer is never held across the loop. */
extern int fdps_score_targets_for_item(int item_id, int target_count,
                                       unsigned char *target_unit_indices);
#pragma aux fdps_score_targets_for_item "*" parm caller [];

/* How much is putting a status effect on these targets worth?  target_ids is
   target_count battle unit indices, one byte each; status_offset is the byte
   offset of the effect's timer inside struct fdps_unit_record -- 0x22, 0x23 and
   0x24 for 神之祝福's three buff slots, 0x25 for 腐毒術, 0x26 for 麻痺術, all
   of them within status_timers -- and the answer is score_per_target for every
   target whose timer byte is zero, summed.

   A nonzero timer is turns still to run, so a target already carrying the
   effect is worth nothing and casting on a group that all carry it scores 0.
   target_count is compared signed and the test precedes the body, so a count of
   0 or below scores 0 without reading target_ids.  Index bytes are zero
   extended and are not range checked; each is resolved through
   fdps_get_unit_record (unit.h) as the walk reaches it. */
extern int fdps_score_targets_without_status(int target_count,
                                             unsigned char *target_ids,
                                             int status_offset,
                                             int score_per_target);
#pragma aux fdps_score_targets_without_status "*" parm caller [];

#endif
