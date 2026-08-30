/* unititem.c -- a unit's inventory and what it has equipped.
 *
 * See unititem.h.  Every function here reaches its record through
 * fdps_get_unit_record and works on the eight 2-byte inventory entries at
 * record offset 0x0a; the file owns no state.
 */
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
