/* unititem.c -- a unit's inventory and what it has equipped.
 *
 * See unititem.h.  Every function here reaches its record through
 * fdps_get_unit_record and works on the eight 2-byte inventory entries at
 * record offset 0x0a; the file owns no state.
 */
#include <stddef.h>
#include <string.h>
#include "fdpstype.h"
#include "table.h"
#include "unit.h"
#include "unititem.h"

/* How many inventory entries a unit record has: CMP dword ptr [EBP-0xc],0x8 /
   JL at 00025162.  Eight, which is the sixteen bytes of inventory_slots read
   two at a time. */
#define INVENTORY_ENTRY_COUNT 8

/* The equipped bit of an entry's flag byte: AND AL,0x40 at 00025188.  It is
   the same bit fdps_unit_recompute_combat_stats tests when it sums the
   equipment modifiers, and the test is a mask and not a compare -- an entry
   whose flag byte carries other bits alongside 0x40 is still equipped. */
#define INVENTORY_FLAG_EQUIPPED 0x40

/* The empty bit of an entry's flag byte: AND AL,0x80 at 0002528c.  Set means
   the slot holds nothing.  It is the bit fdps_unit_remove_item raises on the
   last entry -- MOV byte ptr [EAX+0x18],0x80 at 00025d09, record offset 0x0a
   plus 7 * 2 -- once it has shifted the entries above the removed one down. */
#define INVENTORY_FLAG_EMPTY 0x80

/* The two ends of the item type field the two searches accept: CMP dword ptr
   [EBP-0x8],0x15 at 000251c0 and again at 000251d2, and CMP dword
   ptr [EBP-0x8],0x27 at 000251d8.  Types 1..0x15 are the weapons and
   0x16..0x27 the armour (assets/items.md); everything above 0x27 is a
   consumable or a promotion badge and matches neither search. */
#define WEAPON_TYPE_MAX 0x15
#define ARMOR_TYPE_MAX 0x27

/* 00025140.  The equipped-slot search.  One counted loop over the eight
   inventory entries, i in [EBP-0xc] against the literal 8 with JL, and inside
   it a skip and two accept tests.

   The entry address is formed the long way -- MOV EAX,[EBP-0xc] / ADD EAX,EAX
   / ADD EAX,[EBP-0x18] / ADD EAX,0xa at 00025175 -- which is the record base
   plus 0x0a plus twice the index, so the entries are the 2-byte pairs of
   inventory_slots[] and 0x0a is that field's offset.

   The skip is AND AL,0x40 / AND EAX,0xff / TEST EAX,EAX / JZ: an entry without
   the equipped bit is passed over without the item table being touched at all.
   For an equipped entry the id byte is taken zero-extended -- MOV AL,byte ptr
   [EAX+0x1] / AND EAX,0xff at 00025196 -- so a slot holding id 0xff asks
   fdps_get_item_record for record 255 and not for record -1.

   The type is read straight off the returned pointer, XOR EAX,EAX / MOV EDX,
   [EBP-0x10] / MOV AL,byte ptr [EDX] at 000251aa, so it is byte +0x00 of the
   item record widened to int without sign.  The pointer itself is never
   compared against null and no other field of the record is read.

   want_armor is tested against zero and nothing else, CMP dword ptr
   [EBP+0x18],0x0 / JNZ at 000251b4, so any non-zero value selects the armour
   span.  The weapon accept is the pair CMP ...,0x0 / JLE skip then CMP
   ...,0x15 / JLE accept, and the armour accept is CMP ...,0x15 / JLE skip then
   CMP ...,0x27 / JLE accept; all four are the signed forms, on a value that
   came out of a byte, so the spans are closed at both ends.  The lower end of
   the weapon span is the one that carries meaning at run time: it is what
   keeps a blank ITEM.DAT tail record, which reads back type 0, from being
   reported as an equipped weapon.

   The first accepted slot returns immediately, MOV [EBP-0x4],EAX / JMP to the
   epilogue, so a unit carrying two equipped weapons yields the lower slot.
   Falling out of the loop stores 0xffffffff instead.  Neither argument is
   range checked and nothing is written. */
int fdps_unit_find_equipped_slot(int unit_index, int want_armor)
{
    struct fdps_unit_record *unit;
    struct fdps_item_effect *item;
    unsigned char *inventory_entry;
    int item_type;
    int slot_index;

    unit = fdps_get_unit_record(unit_index);

    for (slot_index = 0;
         slot_index < INVENTORY_ENTRY_COUNT;
         slot_index++) {
        inventory_entry = &unit->inventory_slots[slot_index * 2];
        if ((inventory_entry[0] & INVENTORY_FLAG_EQUIPPED) != 0) {
            item = fdps_get_item_record((int) inventory_entry[1]);
            item_type = (int) item->type;
            if (want_armor == 0) {
                if (item_type > 0 && item_type <= WEAPON_TYPE_MAX) {
                    return slot_index;
                }
            } else {
                if (item_type > WEAPON_TYPE_MAX &&
                    item_type <= ARMOR_TYPE_MAX) {
                    return slot_index;
                }
            }
        }
    }

    return -1;
}

/* 00025200.  The inventory entry's id byte, read straight out.  Straight-line
   code with no branch at all: MOV EAX,[EBP+0x14] / PUSH EAX / CALL
   fdps_get_unit_record / ADD ESP,0x4 for the record, then MOV EAX,[EBP+0x18] /
   ADD EAX,EAX / ADD EDX,EAX for twice the slot and MOV AL,byte ptr [EDX+0xb]
   for the byte.  0x0b is 0x0a, the offset of inventory_slots, plus the 1 that
   picks the id byte of the entry rather than its flag byte.

   XOR EAX,EAX before the MOV AL, so the byte is widened without sign: a slot
   holding id 0xff answers 255, which is the id fdps_get_item_record then
   indexes with, and not -1.

   Neither argument is tested and the entry's flag byte is not read, so an
   empty entry answers with whatever id byte was last left in it.  slot is
   multiplied and added with nothing in between -- keeping that unguarded is
   the point; see the header. */
int fdps_unit_get_item_id(int unit_index, int slot)
{
    struct fdps_unit_record *unit;

    unit = fdps_get_unit_record(unit_index);

    return (int) unit->inventory_slots[slot * 2 + 1];
}

/* 00025240.  How many of the eight inventory entries are occupied.  One
   counted loop, i in [EBP-0xc] against the literal 8 with JL at 00025269, and
   the running total in [EBP-0x8] cleared before the record is even resolved --
   MOV dword ptr [EBP-0x8],0x0 at 0002524c, ahead of the CALL.

   The entry address is formed exactly as the equipped search forms it, MOV
   EAX,[EBP-0xc] / ADD EAX,EAX / ADD EAX,[EBP-0x14] / ADD EAX,0xa at 00025279,
   so the entries are the 2-byte pairs of inventory_slots[] again.

   The test is AND AL,0x80 / AND EAX,0xff / TEST EAX,EAX / JNZ: a mask on the
   flag byte and not a compare, so an entry carrying other bits alongside 0x80
   still counts as empty and an entry carrying any bits BUT 0x80 -- the 0x40 of
   an equipped item, or the plain 0 of a carried one -- counts as occupied.
   The id byte at +1 is not read at all, so a slot's id says nothing about
   whether it is counted.

   Every one of the eight entries is examined; the JMP at 0002529d goes to the
   increment and not out of the loop, so an empty entry in the middle does not
   end the scan and a unit holding items above a hole answers with all of them.
   Neither the unit index nor the record pointer coming back from
   fdps_get_unit_record is checked, and nothing is written. */
int fdps_unit_item_count(int unit_index)
{
    struct fdps_unit_record *unit;
    unsigned char *inventory_entry;
    int occupied_count;
    int slot_index;

    occupied_count = 0;
    unit = fdps_get_unit_record(unit_index);

    for (slot_index = 0;
         slot_index < INVENTORY_ENTRY_COUNT;
         slot_index++) {
        inventory_entry = &unit->inventory_slots[slot_index * 2];
        if ((inventory_entry[0] & INVENTORY_FLAG_EMPTY) == 0) {
            occupied_count++;
        }
    }

    return occupied_count;
}

/* 00025cc0.  Drops one inventory entry and closes the gap.  Straight-line code
   with no branch at all -- the whole body is the record call, one memmove and
   one byte store.

   The record comes back into the only local the function has, MOV dword ptr
   [EBP-0x4],EAX at 00025cd8, and every one of the three addresses below is
   built from it.

   The three memmove arguments are pushed right to left and are computed in
   that order.  The count first: MOV EAX,0x7 / SUB EAX,[EBP+0x18] / ADD EAX,EAX
   at 00025cdb, which is (7 - slot) * 2 bytes, the entries above the removed one
   at two bytes each.  Then the source, MOV EAX,[EBP+0x18] / ADD EAX,EAX / ADD
   EAX,[EBP-0x4] / ADD EAX,0xc, which is entry slot+1; then the destination, the
   same three instructions with ADD EAX,0xa, which is entry slot.  So entries
   slot+1..7 become entries slot..6.  The two spans overlap by every entry but
   one and the copy runs upward, which is what makes this memmove and not
   memcpy.

   The count is formed as a signed subtract and pushed as it stands, so it is
   the signed value that reaches memmove's unsigned parameter: slot 7 makes it
   zero and nothing moves, and any slot above 7 makes it negative and therefore
   a huge byte count.  Neither argument is range checked anywhere in the body.

   The last store is MOV byte ptr [EAX+0x18],0x80 at 00025d09 -- record offset
   0x0a plus 7 * 2, the flag byte of the last entry -- and it is one byte wide.
   The id byte beside it at record offset 0x19 keeps whatever the memmove left
   there; see the header for why widening this store changes behaviour.

   EAX still holds the record pointer at the RET, but the function is declared
   void because no caller reads it: all 37 call sites push two arguments, CALL,
   and ADD ESP,0x8. */
void fdps_unit_remove_item(int unit_index, int slot)
{
    struct fdps_unit_record *unit;

    unit = fdps_get_unit_record(unit_index);

    memmove(&unit->inventory_slots[slot * 2],
            &unit->inventory_slots[slot * 2 + 2],
            (size_t) ((INVENTORY_ENTRY_COUNT - 1 - slot) * 2));

    unit->inventory_slots[(INVENTORY_ENTRY_COUNT - 1) * 2] =
        INVENTORY_FLAG_EMPTY;
}
