/* unitstat.h -- what a battle effect does to one unit's own numbers: HP and
 * MP restored, damage taken, status effects applied and ticked, and the
 * experience each of those credits.
 *
 * Every unit here is named by its index in the map unit array reached through
 * data_fdps_map_unit_array_ptr (src/gamedata.h); the record layout is struct
 * fdps_unit_record in src/fdpstype.h.  No function in this file range checks
 * that index and none of them owns state -- the one global they write is
 * data_fdps_battle_pending_xp_credit, which gamedata.h declares.
 */
#ifndef UNITSTAT_H
#define UNITSTAT_H

#include "fdpstype.h"

/* Restores HP to one unit and credits the experience the heal earned, then
   hands back THE HEAL THAT WAS ROLLED -- not the HP that was actually
   restored.

   The roll is amount * 9 / 10 plus (rand() % 100) * amount / 1000, so a
   request of N gives back between 0.900*N and 0.999*N, and both divisions are
   the signed ones that truncate towards zero.  The roll is added to struct
   fdps_unit_record's hp_current, the total is clamped down to hp_max, and the
   clamped total is written back.

   The returned figure is the roll BEFORE that clamp.  The difference is
   visible: fdps_apply_heal_to_targets floats the returned number over the
   target, so a unit healed from 38 of 40 HP by a rolled 9 shows a 9 and gains
   2.  A caller that wants the HP actually gained has to take hp_current
   before and after for itself.

   The experience is only credited when the unit is a roster character rather
   than an EnemyDat entry -- portrait_id below 0x3c -- and is
   effective_level * 25 * the HP ACTUALLY restored / hp_max, ADDED to
   data_fdps_battle_pending_xp_credit rather than assigned, so several heals
   in one action accumulate.  effective_level is the unit's level byte, plus
   30 for portrait ids 0x0f..0x21, the promoted character forms whose levels
   restart at 1.  fdps_unit_award_exp_and_level_up is what later clamps the
   accumulator to 99 and pays it to the acting unit.

   There is no guard on hp_max: a roster unit whose maximum HP is 0 divides by
   zero here in the original as much as in the rebuild.  amount is not
   required to be positive either, and a negative one drains HP with no clamp
   at zero.

   unit_index is a position in the current battle's unit array and is not
   range checked; the record is resolved through fdps_get_unit_record, so a
   call after the array has moved works on the new block.

   rand() is never seeded by the game (rebuild_info/pitfalls.md), so a given
   battle rolls the same heals every time it is replayed. */
extern int fdps_unit_apply_heal(int unit_index, int amount);
#pragma aux fdps_unit_apply_heal "*" parm caller [];

/* Restores MP to one unit and hands back THE RESTORE THAT WAS ROLLED -- not
   the MP that was actually gained.  The roll is the same one the heal above
   uses, amount * 9 / 10 plus (rand() % 100) * amount / 1000, so a request of N
   gives back between 0.900*N and 0.999*N and both divides truncate towards
   zero.  It is added to struct fdps_unit_record's mp_current, the total is
   clamped down to mp_max, and the clamped total is written back as a 16-bit
   word.

   The returned figure is the roll BEFORE that clamp, and every caller floats
   it over the unit: a unit restored from 38 of 40 MP by a rolled 9 shows a 9
   and gains 2.  A caller that wants the MP actually gained has to read
   mp_current before and after for itself.

   The two MP fields are read UNSIGNED here, which the HP fields of
   fdps_unit_apply_heal are not.  The difference shows on a unit whose MP has
   been driven negative -- nothing clamps at the bottom, so a negative amount
   writes a negative word back -- because the next restore reads that word as a
   number near 65535 and clamps it up to mp_max in one step.

   Nothing else in the record is touched and no experience is credited, which
   is the whole of what separates this from its HP twin.  There is no divide by
   mp_max either, so a unit whose maximum MP is 0 is simply pinned to 0 instead
   of faulting; callers that want to refuse such a target, as
   fdps_apply_item_effect_to_targets does, test mp_max themselves.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call
   after the array has moved works on the new block.

   rand() is never seeded by the game (rebuild_info/pitfalls.md), so a given
   battle rolls the same restores every time it is replayed. */
extern int fdps_unit_restore_mp(int unit_index, int amount);
#pragma aux fdps_unit_restore_mp "*" parm caller [];

#endif
