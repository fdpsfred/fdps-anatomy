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

/* Expands one unit's learned-spell bitmap into a list of spell ids and returns
   how many there are.  The bitmap is struct fdps_unit_record's
   spells_known_bitmap, the five bytes at record offset 0x1a, and it is walked
   byte 0 first and, within each byte, from bit 0 upwards, so the ids come out
   in ascending order.  The id a set bit stands for is byte_index * 8 + bit,
   which puts the whole span at 0..39.

   out_ids is filled from its element 0 with exactly the ids that are set, one
   byte each, packed with no gaps -- it is a list, not a copy of the bitmap
   indexed by spell id.  Nothing bounds checks it, so the caller supplies
   whatever room it needs, and the six callers that pass a buffer do not all
   give room for the 40 ids this can write: fdps_draw_spell_list_page's stack
   array is exactly 40 bytes, while fdps_map_actor_score_best_spell's is 20.
   Elements past the returned count are left as the caller had them.

   out_ids may be NULL, and then nothing is written and the function is a plain
   population count of the bitmap.  Two call sites use it that way, both asking
   only whether the unit knows any spell at all: fdps_battle_action_menu marks
   the spell command unavailable in its menu state when the answer is 0, and
   fdps_score_targets_for_spell skips the scoring step that would follow.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call after
   the array has moved reads the new block.  Nothing in the record is written
   and no global is touched. */
extern int fdps_unit_collect_known_spells(int unit_index,
                                          unsigned char *out_ids);
#pragma aux fdps_unit_collect_known_spells "*" parm caller [];

/* Takes HP off one unit for a damaging effect, credits the experience a hit on
   an enemy earns, and hands back THE DAMAGE THAT WAS ROLLED -- not the HP the
   unit actually lost.

   The roll is base_damage * 9 / 10 plus (rand() % 100) * base_damage / 1000, so
   a nominal figure of N takes off between 0.900*N and 0.999*N and never the
   whole of it; both divisions are the signed ones that truncate towards zero,
   and folding the two terms into one expression changes the numbers because
   each truncates on its own.  The roll is subtracted from struct
   fdps_unit_record's hp_current, the result is clamped up to 0, and the clamped
   result is written back as a 16-bit word.

   The returned figure is the roll BEFORE that clamp, and all three callers
   float it over the target: a unit on 5 HP hit for a rolled 9 is left on 0 and
   shows a 9.  A caller that wants the HP actually lost has to read hp_current
   before and after for itself.

   Both HP fields are read UNSIGNED here, which the HP fields of
   fdps_unit_apply_heal are not.  It shows on a unit whose hp_current has been
   driven negative -- fdps_unit_apply_heal with a negative amount does that, and
   nothing clamps at the bottom -- because this function reads that word back as
   a number near 65535, so the clamp at zero never fires and the subtraction
   leaves a negative word standing.

   Experience is credited only when the unit's side byte is 0, the side an
   ENEMYDAT.DAT unit is deployed with; a hit on the player's own units credits
   nothing and leaves the accumulator holding whatever a previous effect put
   there.  The award is the enemy record's exp_reward multiplied by the target's
   level byte, prorated by damage_rolled / hp_max while the target is still
   standing and paid IN FULL when the clamped HP is 0 -- so a kill pays the
   whole record value however little of the damage was needed, and so does a hit
   on a unit that was already at 0.  It is ADDED to
   data_fdps_battle_pending_xp_credit rather than assigned, so several hits in
   one action accumulate; fdps_unit_award_exp_and_level_up is what later clamps
   the accumulator to 99 and pays it to the acting unit.

   The enemy record is fdps_get_enemy_record(portrait_id - 0x3c) and neither the
   index nor hp_max is checked: a side 0 unit carrying a roster portrait id
   reads in front of the table, and a surviving target whose hp_max is 0 divides
   by zero here in the original as much as in the rebuild.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call after
   the array has moved works on the new block.

   rand() is never seeded by the game (rebuild_info/pitfalls.md), so a given
   battle rolls the same damage every time it is replayed. */
extern int fdps_unit_apply_damage(int unit_index, int base_damage);
#pragma aux fdps_unit_apply_damage "*" parm caller [];

/* Rolls all three status ailments onto one unit at a flat 20% each, which is
   what 鬼動死靈陣 (spell id 0x0d) does on top of its damage.  Both callers
   reach it only for that spell id and only once the spell has landed.

   The three slots are struct fdps_unit_record's status_timers[3], [4] and [5]
   -- record offsets 0x25, 0x26 and 0x27, poison, paralysis and 封魔咒術 --
   and each is rolled on its own: rand() % 100 under 20 lands it, and the
   duration written is rand() % 2 + 2, so two or three turns.  A slot the roll
   misses is left exactly as it was.

   THE UNIT IS ASKED FOR ITS IMMUNITY ONCE PER LANDED ROLL, NOT ONCE PER CALL.
   fdps_unit_is_ailment_immune is consulted only after the chance roll has
   already succeeded, and an immune unit therefore still burns three draws out
   of the shared rand() stream and gets nothing.  The order matters to every
   later roll in the battle, not to this unit.

   A SLOT THAT IS ALREADY AFFLICTED IS REFRESHED.  There is no test for a
   running timer, so a landed roll overwrites whatever count was there --
   upwards or downwards -- where fdps_unit_apply_status_effect leaves an
   existing ailment alone.

   Nothing else is written: no HP, no MP, and no experience.  Ailments seeded
   this way credit data_fdps_battle_pending_xp_credit nothing, which is the
   whole of what separates the accounting here from
   fdps_unit_apply_status_effect's.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call after
   the array has moved works on the new block.

   rand() is never seeded by the game (rebuild_info/pitfalls.md), so a given
   battle rolls the same ailments every time it is replayed. */
extern void fdps_unit_inflict_random_ailments(int unit_index);
#pragma aux fdps_unit_inflict_random_ailments "*" parm caller [];

/* Lands one named status effect on one unit and returns whether it took: 1 when
   the effect is now running, 0 when it is not.  fdps_cast_spell_on_targets is
   the only caller and its three call sites do not read the answer the same way.
   The two ailment sites -- id 0x11, and 0x12 or 0x13 -- draw the miss indicator
   through fdps_show_miss_indicator on a 0 and do nothing on a 1.  The 神之祝福
   loop is the other polarity: it draws nothing at all on a 0 and on a 1 shows
   the buff sprite through fdps_show_sprite_indicator and then rebuilds the
   target's derived numbers through fdps_unit_recompute_combat_stats.

   effect_id IS NOT ALWAYS A SPELL ID.  0x11, 0x12 and 0x13 are the ids of
   封魔咒術, 腐毒術 and 麻痺術 and are used as themselves; anything else is
   a 神之祝福 buff slot, and the caller passes 0, 1 or 2 there.  The id a buff
   slot carries is REPLACED by 0x14 before anything else happens, so all three
   buffs roll against 神之祝福's own hit rate of 100 rather than against
   spells 0, 1 and 2 (assets/spells.md).

   The byte written is one of struct fdps_unit_record's six status_timers, and
   which one is not the order the ids are in: 0x11 selects slot 5 (record
   +0x27), 0x12 slot 3 (+0x25), 0x13 slot 4 (+0x26), and a buff slot n selects
   slot n (+0x22 + n) -- the three fdps_unit_recompute_combat_stats reads as a
   1.15x attack multiplier, a 1.15x defence multiplier and a flat +15.  The
   value stored is rand() % 2 + 2, so every effect runs for two or three turns.

   Three tests gate the write and they are asked IN THIS ORDER, each only if the
   one before it passed.  The effect's hit rate out of MAGICDAT.DAT beats
   rand() % 100; the chosen timer is still 0; and, FOR THE THREE AILMENT IDS
   ONLY, fdps_unit_is_ailment_immune says the unit is not immune.  A buff slot
   skips the immunity test outright, so a unit immune to poison still takes all
   three blessings.  The order is observable beyond this unit: rand() is one
   stream shared by every roll in the battle, so a draw this function does not
   make is a value some later roll takes instead.

   A TIMER THAT IS ALREADY RUNNING IS LEFT ALONE.  The effect simply fails and
   returns 0, where fdps_unit_inflict_random_ailments has no such test and
   refreshes whatever count it finds.

   When it lands, ten times the target's level byte is ADDED to
   data_fdps_battle_pending_xp_credit rather than assigned, so several targets
   in one cast accumulate; fdps_unit_award_exp_and_level_up is what later clamps
   the accumulator to 99 and pays it to the acting unit.  Nothing is credited
   when the effect misses, and the target's side and portrait id are not looked
   at at all -- a status effect landed on one of the player's own units credits
   the party exactly as an enemy does.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call after
   the array has moved works on the new block.  Neither the effect id nor the
   slot it picks is bounded either: a buff slot of 6 or more writes past
   status_timers into the record's own fields.

   rand() is never seeded by the game (rebuild_info/pitfalls.md), so a given
   battle rolls the same effects every time it is replayed. */
extern int fdps_unit_apply_status_effect(int effect_id, int unit_index);
#pragma aux fdps_unit_apply_status_effect "*" parm caller [];

/* Can this unit be given a status ailment?  Returns 1 when it cannot and 0 when
   it can, and every caller uses it the same way round: it is a gate that lets
   the ailment through only on a 0.

   Two fields of struct fdps_unit_record decide it, and either one on its own is
   enough.  The class byte makes the unit immune when it is 0x19 (機兵), 0x21
   or 0x22 (守護獸, 將軍), or 0x24 through 0x26 (？？, 惡靈, 活屍).  The
   portrait id makes it immune when it lies in 0x3c..0x44, which is the first
   nine records of ENEMYDAT.DAT -- portrait ids from 0x3c up index that table as
   id - 0x3c.  Anything else is 0.

   THE IMMUNE CLASS SET HAS A HOLE IN IT.  0x23 (傭兵) sits between the two
   spans and is NOT immune, so writing the tidier 0x21..0x26 makes mercenaries
   immune to poison, paralysis and 封魔咒術.  0x1a (魔神) is below the first
   span and is likewise not immune, and neither is 0x27, the unnamed class the
   data file carries past the end of the second span.  The portrait-id half is a
   different field and cannot be folded into the class comparison at all.

   The three ailment bytes this gates are struct fdps_unit_record's
   status_timers: +0x25 poison, +0x26 paralysis, +0x27 封魔咒術.
   fdps_combat_compute_hit_outcome checks it before a weapon inflicts poison or
   paralysis, fdps_unit_apply_status_effect before a spell writes any one of the
   three, and fdps_unit_inflict_random_ailments before it seeds all three.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call after
   the array has moved reads the new block.  Nothing is written and no global is
   touched. */
extern int fdps_unit_is_ailment_immune(int unit_index);
#pragma aux fdps_unit_is_ailment_immune "*" parm caller [];

#endif
