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
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "table.h"
#include "unit.h"
#include "unititem.h"
#include "roster.h"

/* The stride of one roster record, as the original writes it: IMUL
   EAX,dword ptr [EBP-0x2c],0x50 at 00023ad8 and IMUL EAX,dword ptr
   [EBP-0x18],0x50 at 000239ed, the same literal fdps_get_roster_record uses.
   A literal and not sizeof(struct fdps_unit_record) for the same reason
   table.c gives for the file tables -- 0x50 is the block's own layout and the
   struct agrees with it only while it stays byte-packed, so a lost pack pragma
   would silently move every record past the first
   (rebuild_info/pitfalls.md). */
#define ROSTER_RECORD_STRIDE 0x50

/* How many status-effect timer bytes the write-back clears: PUSH 0x6 at
   00023a43, the six bytes at record +0x22..+0x27. */
#define STATUS_TIMER_COUNT 6

/* The highest experience count a banked record may carry: CMP EAX,0x63 / JLE
   at 00023a94.  Anything above it is reset to 0. */
#define EXP_CARRY_MAX 99

/* The value the flags byte holds once it has been masked down to bit 0, i.e.
   the member is retired: CMP EAX,0x1 / JZ at 00023a68. */
#define UNIT_FLAG_RETIRED 1

/* The character id the exemption below is written for: CMP byte ptr
   [EAX + 0x8],0x0 at 00023a17.  Id 0 is Randis, the one character whose
   leaving the field must not overwrite his roster record. */
#define CHAR_ID_RANDIS 0

/* 00023980.  Banks the party at the end of a battle: every battle unit's
   record is copied back over the roster record of the same character, and the
   fields that must not survive the chapter are reset.

   Both loop bounds are re-read from their globals on every iteration -- CMP
   EAX,dword ptr [0x00060150] at 0002399e and CMP EAX,dword ptr [0x00064114]
   at 000239cc are inside the loops, not hoisted -- so a callee that moved
   either count would be obeyed from the next iteration on.  Neither of the
   three callees does.

   The roster slot address is formed inline -- the argument is copied into a
   parameter-shaped slot at [EBP-0x18], IMUL by 0x50, MOV EDX,[0x00064108],
   ADD, and the result lands in [EBP-0x14] before being copied into the slot
   variable.  That is the inline-expansion fingerprint rebuild_info/
   build_flags.md describes, of fdps_get_roster_record at 00023950; the open
   arithmetic here reproduces it, whereas calling the accessor would put a CALL
   where the original has none.

   There is no break when a slot matches.  The inner loop runs to the end of
   the roster every time, so a character id sitting in two slots has both of
   them written, and the last one written is simply the last one scanned.

   The exemption is narrow and its two halves are both load-bearing: the slot
   is skipped only when the unit's character id is 0 AND fdps_unit_is_retired
   says the unit has left the field.  A retired unit of any other id is banked
   like every other, and an id-0 unit that is still standing is banked too.

   The order of the resets after the copy matters.  memmove brings the whole
   0x50-byte record over first, so every field below is written on top of the
   unit's own value and not on top of the roster's previous one; in particular
   hp_current arrives from the battle before the full heal decides whether to
   replace it.

   The full heal is skipped for the dead and the MP restore is not: the test at
   00023a6b guards only the HP pair, and the MP store at 00023a85 sits after
   the join.  Moving the MP store inside that branch is the obvious tidy-up and
   leaves every fallen member's MP at whatever the battle left.

   The experience byte is read UNSIGNED -- MOV AL,[EAX+0x3c] / AND EAX,0xff /
   CMP EAX,0x63 -- which is what catches the 0xff sentinel the deployment path
   writes for a unit that is not a roster character.  Typed signed, 0xff
   compares as -1, survives into the roster and makes the status panel print
   1000 (rebuild_info/pitfalls.md, contract C).

   Nothing is bounded and nothing is checked: neither count is compared against
   the roster block's 32 slots and neither base pointer is tested for null. */
void fdps_roster_write_back_battle_units(void)
{
    struct fdps_unit_record *unit;
    struct fdps_unit_record *slot;
    int unit_index;
    int roster_index;

    /* The slot pointer is seeded with the bare roster base before the walk
       starts -- MOV EAX,[0x00064108] / MOV [EBP-0x10],EAX at 0002398c -- and
       the value is never read: every path that reads the slot has assigned it
       the record address first.  It is kept because it is what the original
       does, not because anything depends on it. */
    slot = (struct fdps_unit_record *) data_fdps_roster_array_ptr;

    for (unit_index = 0;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        unit = fdps_get_unit_record(unit_index);

        for (roster_index = 0;
             roster_index < data_fdps_roster_member_count;
             roster_index++) {
            slot = (struct fdps_unit_record *)
                   (data_fdps_roster_array_ptr +
                    roster_index * ROSTER_RECORD_STRIDE);

            if (unit->char_id != slot->char_id) {
                continue;
            }
            if (unit->char_id == CHAR_ID_RANDIS &&
                fdps_unit_is_retired(unit_index) != 0) {
                continue;
            }

            memmove(slot, unit, ROSTER_RECORD_STRIDE);
            memset(slot->status_timers, 0, STATUS_TIMER_COUNT);
            slot->flags &= 1;
            if (slot->flags != UNIT_FLAG_RETIRED) {
                slot->hp_current = slot->hp_max;
            }
            slot->mp_current = slot->mp_max;
            if (slot->exp_carry > EXP_CARRY_MAX) {
                slot->exp_carry = 0;
            }
            fdps_roster_recompute_combat_stats(roster_index);
        }
    }
}

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

/* How many carried items the base record hands over: CMP dword ptr
   [EBP-0x10],0x4 at 00023cc0.  Four, out of the eight bag entries a unit
   record has -- the two in front of them are the equipment slots and the two
   behind them are only flagged empty. */
#define CARRIED_ITEM_COUNT 4

/* The three flag values an inventory entry's first byte takes as the record is
   seeded: 0x40 equipped, 0x80 empty, 0 carried and not equipped.  0x40 is the
   bit fdps_roster_recompute_combat_stats above tests, and 0x80 is what the
   bag-listing screens read as "no item here". */
#define INVENTORY_FLAG_EQUIPPED 0x40
#define INVENTORY_FLAG_EMPTY 0x80
#define INVENTORY_FLAG_CARRIED 0x00

/* The id an unused carried-item position holds in the base record: CMP
   EAX,0xff at 00023cde, on the byte zero-extended by AND EAX,0xff. */
#define CARRIED_ITEM_NONE 0xff

/* 00023bc0.  The new member goes at the index the count already holds and the
   count is incremented last, so the record and the count are only in step
   after the body has finished; the recompute call in between is passed the
   OLD count, which is the new member's index.  The count is re-read from the
   global for that call -- PUSH dword ptr [0x00064114] at 00023e04 -- and not
   taken from the local the record address was formed from at 00023bcc.

   The two growth scalings differ and the difference is not a typo to tidy up.
   Maximum HP and MP take (level - 1) steps -- DEC EAX at 00023c23 and
   00023c3e, on the level before the IMUL -- so a level 1 character starts on
   exactly the base record's HP and MP.  Attack, defense and dexterity take a
   full LEVEL of steps, with no DEC anywhere near them (00023d85, 00023d9f,
   00023dcc), so the same level 1 character already carries one step of each of
   those.  Writing all five the same way is the obvious reading and moves every
   enrolled character's HP and MP by one growth step.

   Contract C is live in that arithmetic.  The three base stats and the two
   base HP/MP words arrive through MOVSX and are signed, while every growth
   byte arrives zero-extended -- XOR EDX,EDX / MOV DL at 00023c1b, and the
   16-bit XOR AH,AH form at 00023d83 and 00023d9d -- so a growth byte of 0xff
   is 255 steps of gain and never one step of loss.

   What the body does NOT write is as much a part of the record's state as
   what it does.  Position, facing and walk step (+0x00..+0x04), the AI fields
   (+0x34..+0x36), the event slot (+0x3d), the death-script operand (+0x32) and
   the nine bytes at +0x28 are left holding whatever the roster block held
   there before, and so are the id bytes of bag entries 6 and 7 at +0x17 and
   +0x19 -- those two entries get their empty flag written and nothing else.
   Clearing them as well is the obvious tidy-up and would make a record that
   the original never produces.

   The four derived combat stats at +0x48..+0x4f are not written here either:
   fdps_roster_recompute_combat_stats writes all four from the bases and the
   equipment this body has just seeded, which is why the call is at the end. */
void fdps_roster_add_character(int char_id)
{
    struct fdps_unit_record *member;
    struct fdps_character_base_record *base_record;
    struct fdps_character_growth *growth;
    int member_index;
    int level;
    int hp_start;
    int mp_start;
    int ap_base;
    int dp_base;
    int dx_base;
    int carried_index;

    member_index = data_fdps_roster_member_count;
    member = (struct fdps_unit_record *)
             (data_fdps_roster_array_ptr +
              member_index * ROSTER_RECORD_STRIDE);

    base_record = fdps_get_character_base_record(char_id);
    growth = fdps_get_growth_record(char_id);

    level = (int) base_record->level;
    hp_start = (int) base_record->hp_base +
               (int) growth->hp_min * (level - 1);
    mp_start = (int) base_record->mp_base +
               (int) growth->mp_min * (level - 1);
    ap_base = (int) base_record->ap_base;
    dp_base = (int) base_record->dp_base;
    dx_base = (int) base_record->dx_base;

    member->flags = 0;
    member->side = 2;
    member->portrait_id = (unsigned char) char_id;
    member->char_id = (unsigned char) char_id;
    member->reserved_09 = 0;

    member->inventory_slots[0] = INVENTORY_FLAG_EQUIPPED;
    member->inventory_slots[1] = base_record->equipped_item_0;
    member->inventory_slots[2] = INVENTORY_FLAG_EQUIPPED;
    member->inventory_slots[3] = base_record->equipped_item_1;

    for (carried_index = 0;
         carried_index < CARRIED_ITEM_COUNT;
         carried_index++) {
        if (base_record->carried_items[carried_index] == CARRIED_ITEM_NONE) {
            member->inventory_slots[4 + carried_index * 2] =
                INVENTORY_FLAG_EMPTY;
        } else {
            member->inventory_slots[4 + carried_index * 2] =
                INVENTORY_FLAG_CARRIED;
        }
        member->inventory_slots[5 + carried_index * 2] =
            base_record->carried_items[carried_index];
    }

    member->inventory_slots[12] = INVENTORY_FLAG_EMPTY;
    member->inventory_slots[14] = INVENTORY_FLAG_EMPTY;

    memmove(member->spells_known_bitmap, base_record->spell_mask, 4);
    member->spells_known_bitmap[4] = 0;
    member->race = base_record->race_id;
    member->clazz = base_record->class_id;
    member->level = (unsigned char) level;
    memset(member->status_timers, 0, 6);
    member->death_script_opcode = 0xff;

    member->ap_base = (short) (ap_base + level * (int) growth->ap_min);
    member->dp_base = (short) (dp_base + level * (int) growth->dp_min);
    member->move = base_record->move;
    member->exp_carry = 0;
    member->dx_base = (short) (dx_base + (int) growth->dx_min * level);
    member->hp_current = (short) hp_start;
    member->hp_max = (short) hp_start;
    member->mp_current = (short) mp_start;
    member->mp_max = (short) mp_start;

    fdps_roster_recompute_combat_stats(data_fdps_roster_member_count);
    data_fdps_roster_member_count = data_fdps_roster_member_count + 1;
}

/* The type code that divides the item table into its two equipment
   categories: CMP dword ptr [EBP-0x4],0x15 at 00033642 on the candidate's
   type and CMP EAX,0x15 at 00033652 on the entry's.  Codes 0x01..0x15 are the
   weapon kinds and 0x16 upwards the armour and the non-weapon items.  The same
   literal is the same split everywhere it appears -- fdps_unit_equip_slot
   compares the two types against it at 0002613f and 00026145, and clears the
   old entry's flag byte at 0002615c when they land on the SAME side. */
#define ITEM_TYPE_LAST_WEAPON 0x15

/* 00033570.  What the four combat stats would come to if this roster member
   put this item on, for the status panel that shows the prospective figures
   beside the current ones.  Nothing is written into the record; the four
   totals go out through the caller's array.

   The four seeds are the same three base stats fdps_roster_recompute_combat_
   stats uses, and the same trap: hit and evade both come off the ONE dexterity
   word at +0x3e -- MOVSX EDX,word ptr [EAX+0x3e] at 000335cd for hit and again
   at 000335e0 for evade.  Reaching for +0x40 as an evade base gives a number
   that disagrees with the one the recompute stores at record +0x4e, so the
   panel would light an arrow on a stat the item did not change
   (rebuild_info/pitfalls.md).

   The candidate's own four modifiers are added into the seeds unconditionally,
   before the scan and regardless of its type or of whether the member's class
   may wear it: fdps_unit_can_equip_item is a separate check the caller runs
   before it prints the numbers.

   The scan is what makes this function more than the recompute plus one item.
   An equipped entry contributes only when its type sits on the OPPOSITE side
   of the threshold from the candidate's, because equipping the candidate would
   displace a same-category item and that item's modifiers would go with it.
   Both halves of that condition are spelled out in the assembly as two
   compares each, 00033642/00033652 for the weapon-candidate case and
   00033657/00033667 for the armour-candidate case, and the entry is skipped
   whenever the two sides agree.

   Every entry's item record is fetched BEFORE the flag byte is tested -- the
   CALL at 00033627 sits above the AND AL,0x40 at 00033637 -- so all eight
   lookups happen whatever the flags hold.  Hoisting the call into the
   condition is the obvious tidy-up and is only invisible because
   fdps_get_item_record is pure arithmetic on the table base.

   Both bytes taken out of the record are zero-extended: the type through XOR
   EAX,EAX / MOV AL at 00033618-0003361d for the candidate and AND EAX,0xff at
   0003364d for an entry, and the id likewise at 00033618.  A type of 0xff is
   255 and lands on the armour side of the threshold, never -1 on the weapon
   side, and an id of 0xff indexes 255 records forward (contract C).

   The four totals leave as 32-bit ints -- MOV dword ptr [EDX],EAX at 000336a5
   and the three stores behind it -- and are NOT narrowed the way the
   recompute's word stores narrow its own.  Nothing is range-checked: neither
   the roster index nor either item id, and the caller's array is written
   through unexamined. */
void fdps_roster_preview_combat_stats_with_item(int roster_index, int item_id,
                                                int *out_stats)
{
    struct fdps_unit_record *member;
    unsigned char *inventory_entry;
    /* One slot, as the original has: [EBP-0x20] holds the candidate's record
       until the scan starts and then each entry's in turn.  The candidate's
       type is the one thing that has to outlive it, which is why it is copied
       out to its own variable before the loop. */
    struct fdps_item_effect *item_record;
    int candidate_type;
    int entry_item_id;
    int attack_total;
    int defense_total;
    int hit_total;
    int evade_total;
    int entry_index;

    item_record = fdps_get_item_record(item_id);
    candidate_type = (int) item_record->type;

    member = fdps_get_roster_record(roster_index);

    attack_total = (int) member->ap_base + (int) item_record->ap;
    defense_total = (int) member->dp_base + (int) item_record->dp;
    hit_total = (int) member->dx_base + (int) item_record->hit;
    evade_total = (int) member->dx_base + (int) item_record->ev;

    for (entry_index = 0;
         entry_index < INVENTORY_ENTRY_COUNT;
         entry_index++) {
        inventory_entry = &member->inventory_slots[entry_index * 2];
        entry_item_id = (int) inventory_entry[1];
        item_record = fdps_get_item_record(entry_item_id);

        if ((inventory_entry[0] & INVENTORY_FLAG_EQUIPPED) != 0 &&
            ((candidate_type <= ITEM_TYPE_LAST_WEAPON &&
              (int) item_record->type > ITEM_TYPE_LAST_WEAPON) ||
             (candidate_type > ITEM_TYPE_LAST_WEAPON &&
              (int) item_record->type <= ITEM_TYPE_LAST_WEAPON))) {
            attack_total += (int) item_record->ap;
            defense_total += (int) item_record->dp;
            hit_total += (int) item_record->hit;
            evade_total += (int) item_record->ev;
        }
    }

    out_stats[0] = attack_total;
    out_stats[1] = defense_total;
    out_stats[2] = hit_total;
    out_stats[3] = evade_total;
}

/* The answer from fdps_unit_item_count that means the member has no room:
   CMP EAX,0x8 / JZ at 00036b24.  It is an equality test against the count of
   occupied entries and not a >= on a free-slot index, so the only value that
   skips a member is exactly 8. */
#define INVENTORY_FULL_COUNT 8

/* The chapter gate, both halves of it as literals: CMP dword ptr
   [0x00069cf4],0x17 / JL at 00036b29 and CMP dword ptr [EBP-0x4],0x3 / JZ at
   00036b32.  From chapter index 0x17 onwards -- chapter 24 as the player
   counts them -- roster slot 3 gets nothing.  There is no upper bound on the
   chapter and no test of who is actually in the slot. */
#define CHAPTER_INDEX_SLOT_3_LEAVES 0x17
#define ROSTER_SLOT_THAT_LEAVES 3

/* 00036af0.  Hands one item to every roster member that still has room for it.
   The bonus lottery at 00036460 is the only caller and reaches this three
   times, with 0xb4, 0xb9 and one of the three 斬鐵劍 tiers.

   The bound is data_fdps_roster_member_count read afresh at the top of every
   pass -- MOV EAX,[EBP-0x4] / CMP EAX,dword ptr [0x00064114] / JL at
   00036b03 -- and the compare is signed, so a negative count hands out
   nothing rather than running away (contract C).

   Both accessors reach the record through fdps_get_unit_record and therefore
   through data_fdps_map_unit_array_ptr, not through the roster pointer.  That
   is not a slip: this runs in the field/village phase, where
   fdps_load_field_chapter_resources has pointed that global straight at the
   roster block, so the loop is walking roster records with the index the count
   bounds.  Resolving the record through fdps_get_roster_record instead would
   read the right block today and the wrong one from any caller reached during
   a battle.

   The two skips are separate tests and are made in this order: a member whose
   eight inventory entries are all occupied is passed over, and then slot 3 is
   passed over outright once the chapter index has reached 0x17.  Every member
   that survives both gets fdps_unit_add_item, whose 1-or-(-1) result is
   discarded -- ADD ESP,0x8 and straight to the increment at 00036b49 -- so a
   failed insertion is silent.  It cannot arise here anyway: the only way
   fdps_unit_add_item returns -1 is a full inventory, which is the case the
   count test has already removed.

   The chapter gate must be written out as the pair of literals it is.  The
   obvious general form -- skipping members flagged retired, or letting slot 3
   back in when its character rejoins the party in chapter 28 -- changes who
   receives the lottery item from chapter 28 on, because the original has no
   upper bound on the chapter and never looks at the record
   (rebuild_info/pitfalls.md).

   Neither the index nor the item id is range checked, no result is reported,
   and members that were skipped are not recorded anywhere. */
void fdps_roster_add_item_to_all(int item_id)
{
    /* The one local the original has: [EBP-0x4], and the frame is SUB ESP,0x4.
       The item count is not kept anywhere -- CMP EAX,0x8 compares the call's
       result where it lands -- so it is tested where it is fetched rather than
       through a variable of its own. */
    int roster_index;

    for (roster_index = 0;
         roster_index < data_fdps_roster_member_count;
         roster_index++) {
        if (fdps_unit_item_count(roster_index) != INVENTORY_FULL_COUNT &&
            (data_fdps_chapter_current_chapter_id <
                 CHAPTER_INDEX_SLOT_3_LEAVES ||
             roster_index != ROSTER_SLOT_THAT_LEAVES)) {
            fdps_unit_add_item(roster_index, item_id);
        }
    }
}
