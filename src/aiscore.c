/* aiscore.c -- scoring the map AI's candidate attacks, items and spells.
 *
 * See aiscore.h for what each scorer is asked and what its number means.
 * Nothing here owns state: every scorer reads the data tables through the
 * accessors in table.h and the battle units through unit.h, and reports.
 */
#include "fdpstype.h"
#include "table.h"
#include "unit.h"
#include "aiscore.h"

/* 000132b0.  Fetches the ITEM.DAT record once, keeps use_amount and the
   use_effect code, and runs one of two target walks or neither.

   The two walks read the same current-HP word at unit record +0x40 with
   different signedness and that is behaviour, not spelling: MOVSX at 00013329
   in the restorative walk against MOV AX + AND 0xffff at 000133dc in the
   damage walk, while the item's use_amount at +0x0e is sign-extended at
   000132d5 either way.  Reading hp_current as the signed short the layout
   declares in both places reproduces the first walk and breaks the second: a
   unit whose HP word had gone negative would score 0x12, "this finishes it",
   where the original scores 8, "still standing".  Hence the cast in the
   damage walk and none in the restorative one.

   The two thresholds are signed divisions of the maximum -- IDIV EBX with
   EBX = 3 at 00013348, and SAR-based halving at 00013363 -- and both compare
   with JL, so they are strictly less-than against the current HP: at exactly
   a third the score is 8, not 3.  Writing either as <= moves the boundary of
   every heal decision the AI makes.

   use_effect is loaded zero-extended (XOR EAX,EAX / MOV AL at 000132dc), so
   the dispatch is over 0x00..0xff and 0x0b and 0x1e are the only codes with a
   walk; the caller filters out 0 before calling but this function does not
   depend on that.

   use_amount is read before the dispatch and the restorative walk never looks
   at it, which is why all four HP items score alike however much they
   restore. */
int fdps_score_targets_for_item(int item_id, int target_count,
                                unsigned char *target_unit_indices)
{
    struct fdps_item_effect *item;
    struct fdps_unit_record *target;
    int use_amount;
    int use_effect;
    int target_index;
    int hp_current;
    int hp_max;
    int score;
    int total;

    total = 0;
    item = fdps_get_item_record(item_id);
    use_amount = item->use_amount;
    use_effect = item->use_effect;
    if (use_effect == 0x0b) {
        for (target_index = 0; target_index < target_count; target_index++) {
            target = fdps_get_unit_record(target_unit_indices[target_index]);
            hp_current = target->hp_current;
            hp_max = target->hp_max;
            if (hp_max / 3 < hp_current) {
                if (hp_max / 2 < hp_current) {
                    score = 0;
                } else {
                    score = 3;
                }
            } else {
                score = 8;
            }
            if ((target->ai_behavior & 0x80) != 0) {
                score = score * 3;
            }
            total = total + score;
        }
    } else if (use_effect == 0x1e) {
        for (target_index = 0; target_index < target_count; target_index++) {
            target = fdps_get_unit_record(target_unit_indices[target_index]);
            if (use_amount < (int) (unsigned short) target->hp_current) {
                score = 8;
            } else {
                score = 0x12;
            }
            total = total + score;
        }
    }
    return total;
}

/* 00013c20.  One walk over the target list, one byte tested per target.

   The status byte is reached as record + status_offset rather than through a
   named field because the offset is an argument: MOV EAX,[EBP-0x8] / ADD
   EAX,[EBP+0x1c] / CMP byte ptr [EAX],0x0 at 00013c65.  The five offsets the
   caller passes -- 0x22, 0x23 and 0x24 for 神之祝福's three buff slots, 0x25
   for 腐毒術 and 0x26 for 麻痺術 -- are all inside struct fdps_unit_record's
   status_timers, and each byte is a count of turns the effect still has to
   run, so any nonzero value means the effect is already on the unit and that
   target contributes nothing.

   The loop test is CMP EAX,[EBP+0x14] / JL at 00013c3d: it runs before the
   body and it is signed, so a target_count of 0 or below returns the untouched
   accumulator without reading target_ids at all.

   The index is loaded MOV AL / AND EAX,0xff at 00013c52, so it is zero
   extended -- an index byte of 0x81 is unit 129.  Neither that index nor the
   record pointer is range checked, because fdps_get_unit_record checks
   neither, and the record is re-resolved on every pass rather than held.

   fdps_score_targets_for_spell stores the result of all five of its calls with
   MOV, not ADD -- 00013a96, 00013b18, 00013b3a, 00013b51 and 00013b68 all
   write the same accumulator at [EBP-0x14] -- so in the 神之祝福 branch the
   0x22 and 0x23 totals are computed and then overwritten by the 0x24 one. */
int fdps_score_targets_without_status(int target_count,
                                      unsigned char *target_ids,
                                      int status_offset, int score_per_target)
{
    struct fdps_unit_record *target;
    int target_index;
    int total;

    total = 0;
    for (target_index = 0; target_index < target_count; target_index++) {
        target = fdps_get_unit_record(target_ids[target_index]);
        if (*((unsigned char *) target + status_offset) == 0) {
            total = total + score_per_target;
        }
    }
    return total;
}
