/* unititem.h -- a unit's inventory and what it has equipped.
 *
 * A unit record carries eight 2-byte inventory entries at record offset 0x0a,
 * struct fdps_unit_record's inventory_slots[16] in src/fdpstype.h: entry i is
 * inventory_slots[i * 2], a flag byte whose bit 0x40 means "equipped" and bit
 * 0x80 means "no item here", followed by the item id byte.  The functions here
 * read and edit that set; the records themselves live in the block reached
 * through data_fdps_map_unit_array_ptr (gamedata.h) and are resolved through
 * fdps_get_unit_record (unit.h).  The file owns no state of its own.
 */
#ifndef UNITITEM_H
#define UNITITEM_H

/* Which of the unit's eight inventory slots holds the weapon, or the armour,
   that it currently has equipped?  Returns the slot index 0..7 of the FIRST
   entry that both carries the equipped bit 0x40 and whose item is of the
   requested kind, or -1 when there is no such entry.  Every caller treats -1
   as "nothing equipped": the two counter-attack checks and the AI's attack
   scorer abandon the attack, the battle action menu raises its "no weapon"
   flag, and the shop skips the trade-in.

   want_armor picks the kind, and it is a plain zero test and not a flag
   compare -- any non-zero value asks for armour:
     0        the equipped weapon, item type 1..0x15
     non-zero the equipped armour, item type 0x16..0x27
   The type is byte +0x00 of the item's ITEM.DAT record, so the two spans are
   the weapon and armour halves of the type field (assets/items.md).  Eight of
   the nine call sites push a literal 0; only fdps_shop_buy_loop pushes 1, and
   only when the shop item under the cursor is itself of an armour type.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call
   after the array has moved sees the new block.  No byte of the record and no
   byte of the item table is written.

   The weapon test rejects item type 0 as well as everything above 0x15.  That
   is behaviour and not a redundant guard: ITEM.DAT's records past id 0xe1 are
   all zero (assets/items.md), so a slot holding a bad id reads back type 0,
   and the single-sided `type <= 0x15` an author would write instead would
   report that blank record as the unit's equipped weapon. */
extern int fdps_unit_find_equipped_slot(int unit_index, int want_armor);
#pragma aux fdps_unit_find_equipped_slot "*" parm caller [];

/* What item is in this inventory slot?  Returns the id byte of entry `slot` of
   unit `unit_index` -- struct fdps_unit_record's inventory_slots[slot * 2 + 1]
   -- widened without sign, so the answer is 0..255 and an entry holding 0xff
   answers 255.  That is the id the callers hand to fdps_get_item_record, and
   every one of the fourteen call sites does exactly that with it.

   The entry's flag byte is NOT consulted.  An entry marked empty (bit 0x80) or
   merely carried rather than equipped answers with its id byte just the same,
   so the answer is only meaningful once the caller has established that the
   slot holds something.  Nothing is written and no other byte of the record is
   read.

   Neither argument is range checked, and that is load-bearing rather than an
   omission.  slot goes into the address as slot * 2 with nothing in between,
   so a slot outside 0..7 addresses bytes outside the inventory field:
   fdps_unit_resolve_attack_hit passes the result of
   fdps_unit_find_equipped_slot straight in without testing it for -1, and a
   unit with no equipped weapon therefore reads record offset 0x09 -- the
   reserved_09 byte in front of the inventory -- and hands that to
   fdps_get_item_record.  A bounds check on slot, or an early return of 0 or
   -1, changes what that path does.

   unit_index is a position in the current battle's unit array; the record is
   resolved through fdps_get_unit_record on every call, so a call after the
   array has moved sees the new block. */
extern int fdps_unit_get_item_id(int unit_index, int slot);
#pragma aux fdps_unit_get_item_id "*" parm caller [];

#endif
