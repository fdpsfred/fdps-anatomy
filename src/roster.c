/* roster.c -- the party roster: the list of characters that travels between
 * chapters, as opposed to the battle/map unit array the same record type is
 * also used for.
 *
 * The roster is one heap block of 32 records of struct fdps_unit_record,
 * reached through data_fdps_roster_array_ptr, with
 * data_fdps_roster_member_count saying how many of the slots are occupied.
 * Nothing in this file owns state: every function works in place on the block
 * that pointer holds.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "table.h"
#include "roster.h"

/* The stride of one roster record, as the original writes it: IMUL
   EAX,dword ptr [EBP-0x2c],0x50 at 00023ad8, the same literal
   fdps_get_roster_record uses.  A literal and not sizeof(struct
   fdps_unit_record) for the same reason table.c gives for the file tables --
   0x50 is the block's own layout and the struct agrees with it only while it
   stays byte-packed, so a lost pack pragma would silently move every record
   past the first (rebuild_info/pitfalls.md). */
#define ROSTER_RECORD_STRIDE 0x50

/* How many inventory entries the scan below covers: CMP dword ptr
   [EBP-0x18],0x8 at 00023b18.  All eight, not the two slots
   fdps_roster_add_character marks equipped. */
#define INVENTORY_ENTRY_COUNT 8

/* 00023ac0.  Base stats plus equipment, and nothing else.  The record is
   addressed inline -- IMUL by 0x50, MOV EDX,[0x00064108], ADD -- rather than
   through fdps_get_roster_record, which is the same three instructions at
   00023950; calling the accessor would put a CALL where the original has none.

   Hit and evade are seeded from the ONE dexterity word.  MOVSX EAX,word ptr
   [EAX+0x3e] at 00023b04 lands in [EBP-0xc] and 00023b0b copies that same
   value into [EBP-0x8]; there is no second base field involved.  The obvious
   'one base field per derived stat' version would reach for +0x40 for evade,
   which is hp_current, and be wrong (rebuild_info/pitfalls.md).

   Then the two accumulators part company, and which item field goes to which
   is worth reading twice: [EBP-0x8] takes the item's +0x03 (hit) and is stored
   to record+0x4c, [EBP-0xc] takes +0x07 (ev) and is stored to record+0x4e.
   The item record's four modifiers are NOT in the same order as the four
   destination fields.

   All four accumulators are 32-bit and every value entering them arrives
   through MOVSX -- the three base stats at +0x37, +0x39 and +0x3e and all four
   item modifiers (contract C).  The four stores are word stores (MOV word ptr
   [EDX+0x48],AX), so a total outside 16 bits is truncated on the way into the
   record rather than saturated; accumulating in short instead would wrap at a
   different point in a chain of large modifiers.

   The equipped test is bit 0x40 alone -- AND AL,0x40 at 00023b3b -- so an
   entry's other flag bits, 0x80 empty included, decide nothing here, and the
   item id is zero-extended out of the entry's second byte (XOR EAX,EAX / MOV
   AL,byte ptr [EDX+0x1] at 00023b46), so ids run 0..255 and none of them is
   negative.  The id is handed to fdps_get_item_record unchecked, which is what
   makes item 0xff readable past the end of ITEM.DAT.

   There is no bound on roster_index and no null check on the roster pointer.

   This is the roster half of a pair, and the halves are not interchangeable:
   fdps_unit_recompute_combat_stats at the battle side runs the identical sum
   and then applies the status timers at +0x22..+0x24 -- +15 dexterity, and
   1.15x on attack and defense.  Nothing in this body reads those three bytes.
   Folding the two into one shared routine changes roster stats wherever a
   roster record happens to carry a non-zero byte there. */
void fdps_roster_recompute_combat_stats(int roster_index)
{
    struct fdps_unit_record *member;
    struct fdps_item_effect *item;
    unsigned char *inventory_entry;
    int attack_total;
    int defense_total;
    int hit_total;
    int evade_total;
    int entry_index;

    member = (struct fdps_unit_record *)
             (data_fdps_roster_array_ptr +
              roster_index * ROSTER_RECORD_STRIDE);

    attack_total = (int) member->ap_base;
    defense_total = (int) member->dp_base;
    evade_total = (int) member->dx_base;
    hit_total = evade_total;

    for (entry_index = 0;
         entry_index < INVENTORY_ENTRY_COUNT;
         entry_index++) {
        inventory_entry = &member->inventory_slots[entry_index * 2];
        if ((inventory_entry[0] & 0x40) != 0) {
            item = fdps_get_item_record((int) inventory_entry[1]);
            attack_total += (int) item->ap;
            defense_total += (int) item->dp;
            hit_total += (int) item->hit;
            evade_total += (int) item->ev;
        }
    }

    member->ap = (short) attack_total;
    member->dp = (short) defense_total;
    member->hit = (short) hit_total;
    member->ev = (short) evade_total;
}
