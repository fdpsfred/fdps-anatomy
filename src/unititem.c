/* unititem.c -- a unit's inventory and what it has equipped.
 *
 * See unititem.h.  Most of the functions here reach a record through
 * fdps_get_unit_record and work on the eight 2-byte inventory entries at
 * record offset 0x0a; fdps_unit_can_equip_item asks instead whether the unit's
 * class is allowed to equip a given item at all, fdps_unit_find_item_slot
 * touches no record of its own -- it searches an inventory entirely through
 * the other two accessors -- and fdps_unit_item_select_loop is the modal
 * cursor the player moves over those eight entries in the unit status window.
 * The file owns no state.
 */
#include <stddef.h>
#include <string.h>
#include "audio.h"
#include "blit.h"
#include "fdpstype.h"
#include "statunit.h"
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

/* How many item type codes one PROEQU.DAT class record holds: CMP dword ptr
   [EBP-0xc],0x6 / JL at 00026032.  Six, which is the whole of struct
   fdps_class_equip_record and the stride fdps_get_class_equip_record scales
   by. */
#define CLASS_EQUIP_TYPE_COUNT 6

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

/* Where the item list sits inside the 320x200 status window image, and how big
   it is: PUSH 0x95 / PUSH 0x97 / PUSH 0x140 / ADD EAX,0x3b58 / PUSH 0x97 at
   00025b82.  0x3b58 is row 47 * 0x140 + column 152, so the rectangle's top-left
   corner is (0x98, 0x2f) -- the same corner fdps_draw_unit_inventory's four
   call sites all hand it (statunit.h).  The backdrop's own stride is its width,
   because it is a tight 0x97 x 0x95 copy of just that rectangle. */
#define ITEM_LIST_AT 0x3b58
#define ITEM_LIST_WIDTH 0x97
#define ITEM_LIST_ROWS 0x95

/* The pitch of the window image the list is composed into: PUSH 0x140 at both
   00025b8c and 00025bab.  A whole 320-pixel scanline, because the list is a
   rectangle of a full-screen frame and not a surface of its own. */
#define WINDOW_IMAGE_PITCH 0x140

/* The status window's idle animation is not offered here: XOR EAX,EAX / PUSH
   EAX at 00025bca.  A zero is not merely "do not animate" -- it also keeps
   fdps_unit_status_window_wait_input from drawing the rand this window would
   otherwise consume, which is visible in the CRT's random state (statunit.h). */
#define STATUS_WINDOW_NO_IDLE_ANIM 0

/* The six make codes the loop acts on: CMP dword ptr [EBP-0x20],0x48 at
   00025be0, 0x50 at 00025c11, 0x1c at 00025c45, 0x39 at 00025c4b, 0x1 at
   00025c95 and 0x53 at 00025c9b.  Everything else falls through and the list is
   simply painted again. */
#define SCANCODE_UP 0x48
#define SCANCODE_DOWN 0x50
#define SCANCODE_ENTER 0x1c
#define SCANCODE_SPACE 0x39
#define SCANCODE_ESC 0x01
#define SCANCODE_DELETE 0x53

/* The click the cursor moves with, the same asset name fdps_save_slot_select_loop
   uses.  fdps_play_sfx folds the name to upper case in place, so this literal
   ends up upper case after the first move (rebuild_info/pitfalls.md). */
#define CURSOR_MOVE_SFX "Beep.wav"

/* The two answers: MOV dword ptr [EBP-0x4],0x1 at 00025c57 and 00025c8a, and
   MOV dword ptr [EBP-0x4],0xffffffff at 00025ca1. */
#define ITEM_SELECT_CONFIRMED 1
#define ITEM_SELECT_CANCELLED (-1)

/* 00025b20.  The modal cursor loop over one unit's eight inventory entries.
   Everything before the loop happens once: the record call, the byte at record
   +0x02 into [EBP-0x10], and the counted sweep that fills [EBP-0x1c].

   THE OCCUPIED COUNT IS TAKEN ONCE AND IS THE ONLY THING THE WRAP KNOWS.  The
   sweep is the same test fdps_unit_item_count applies -- MOV EAX,[EBP-0x18] /
   ADD EAX,EAX / ADD EAX,[EBP-0xc] / MOV AL,byte ptr [EAX+0xa] / AND AL,0x80 at
   00025b64 -- and it is outside the loop, so an inventory edited while the list
   is open does not move the wrap point.

   THE WRAP IS TWO COMPARES AND NOT A REMAINDER.  Up is CMP dword ptr [EAX],0x0
   / JNZ at 00025bf7, so slot 0 becomes count - 1; down is MOV EDX,[EBP-0x1c] /
   DEC EDX / CMP EDX,[EAX] at 00025c25, so count - 1 becomes 0.  Writing either
   as (*selected_slot + 1) % occupied_count divides by zero, because
   fdps_unit_equip_window opens this loop without first checking that the unit
   carries anything and the count really can be 0.  What the original does with
   a count of 0 is let the index walk outside 0..7; fdps_draw_unit_inventory
   draws no bar for such a row and nothing else reads it, and the caller tests
   fdps_unit_item_count after the loop returns.

   The paint is always both halves in the same order: fdps_blit_rect lays the
   untouched backdrop back over the list rectangle, then
   fdps_draw_unit_inventory redraws the eight rows with the bar on
   *selected_slot.  The wait that follows is handed record +0x02, the unit's
   sprite cache slot (statunit.h), and never the unit index itself.

   THE CONFIRM READS THE ID BYTE WITHOUT LOOKING AT THE FLAG BYTE.  MOV EDX,
   [EAX] / ADD EDX,EDX / ADD EDX,[EBP-0xc] / MOV AL,byte ptr [EDX+0xb] at
   00025c60 is record + 0x0a + 2 * slot + 1, so an empty entry answers with
   whatever id byte was last left in it, and the id is widened without sign.
   usable_only is a plain zero test, CMP dword ptr [EBP+0x18],0x0 at 00025c51,
   and the usability test is CMP byte ptr [EAX+0xd],0x0 on the returned item
   record -- unchecked for null, like every other reader of that table.  A
   refusal is silent: no sound, no message, straight back to the paint. */
int fdps_unit_item_select_loop(int unit_index, int usable_only,
                               unsigned char *window_image,
                               unsigned char *list_backdrop,
                               int *selected_slot)
{
    struct fdps_unit_record *unit;
    /* The ITEM.DAT record of the entry the player tried to confirm, looked up
       only on the usable_only path. */
    struct fdps_item_effect *item;
    /* Record +0x02, the slot of the sprite cache the window animates the unit
       out of.  Cached before the loop and handed to every wait. */
    int sprite_cache_slot;
    /* How many of the eight entries hold something, counted once.  It is the
       cursor's wrap point and nothing else. */
    int occupied_count;
    /* The entry being examined by the counting sweep. */
    int slot_index;
    /* The make code the last wait came back with, held signed and compared as
       a full int. */
    int scancode;
    /* The id byte of the entry being confirmed, widened without sign. */
    int item_id;

    occupied_count = 0;
    unit = fdps_get_unit_record(unit_index);
    sprite_cache_slot = (int) unit->sprite_cache_slot;

    for (slot_index = 0;
         slot_index < INVENTORY_ENTRY_COUNT;
         slot_index++) {
        if ((unit->inventory_slots[slot_index * 2]
             & INVENTORY_FLAG_EMPTY) == 0) {
            occupied_count++;
        }
    }

    for (;;) {
        fdps_blit_rect((unsigned int) list_backdrop, ITEM_LIST_WIDTH,
                       window_image + ITEM_LIST_AT, WINDOW_IMAGE_PITCH,
                       ITEM_LIST_WIDTH, ITEM_LIST_ROWS);
        fdps_draw_unit_inventory(unit_index, *selected_slot,
                                 window_image + ITEM_LIST_AT,
                                 WINDOW_IMAGE_PITCH);
        scancode = fdps_unit_status_window_wait_input(
                       window_image, sprite_cache_slot,
                       STATUS_WINDOW_NO_IDLE_ANIM);

        if (scancode == SCANCODE_UP) {
            fdps_play_sfx(CURSOR_MOVE_SFX);
            if (*selected_slot == 0) {
                *selected_slot = occupied_count - 1;
            } else {
                *selected_slot = *selected_slot - 1;
            }
        } else if (scancode == SCANCODE_DOWN) {
            fdps_play_sfx(CURSOR_MOVE_SFX);
            if (occupied_count - 1 == *selected_slot) {
                *selected_slot = 0;
            } else {
                *selected_slot = *selected_slot + 1;
            }
        } else if (scancode == SCANCODE_ENTER || scancode == SCANCODE_SPACE) {
            if (usable_only == 0) {
                return ITEM_SELECT_CONFIRMED;
            }
            item_id = (int) unit->inventory_slots[*selected_slot * 2 + 1];
            item = fdps_get_item_record(item_id);
            if (item->use_effect != 0) {
                return ITEM_SELECT_CONFIRMED;
            }
        } else if (scancode == SCANCODE_ESC || scancode == SCANCODE_DELETE) {
            return ITEM_SELECT_CANCELLED;
        }
    }
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

/* 00025d20.  Puts one item in the first empty inventory entry.  One counted
   loop, i in [EBP-0x8] against the literal 8 with JL at 00025d42, and the
   record resolved once into [EBP-0x10] before it starts.

   The entry address is formed exactly as the other three scans form it -- MOV
   EAX,[EBP-0x8] / ADD EAX,EAX / ADD EAX,[EBP-0x10] / ADD EAX,0xa at 00025d52 --
   so the entries are the 2-byte pairs of inventory_slots[] again.

   The empty test is AND AL,0x80 / AND EAX,0xff / TEST EAX,EAX / JZ, the same
   mask fdps_unit_item_count uses and not a compare: an entry carrying other
   bits alongside 0x80 is still empty, and the JZ falls through to the increment
   at 00025d4a, so an occupied entry in the middle does not end the scan.

   Taking the slot is two stores.  MOV byte ptr [EDX],0x0 at 00025d73 puts a
   plain zero over the whole flag byte -- that is what clears the empty bit so
   the slot counts, and the equipped bit with it -- and MOV AL,byte ptr
   [EBP+0x18] / MOV byte ptr [EDX+0x1],AL at 00025d76 writes the id beside it,
   reading one byte of the pushed argument and no more.  Then MOV [EBP-0x4],0x1
   and out; nothing else in the record is touched and no entry is moved.

   Falling out of the loop stores 0xffffffff instead, and that path writes
   nothing at all.  Neither argument is range checked. */
int fdps_unit_add_item(int unit_index, int item_id)
{
    struct fdps_unit_record *unit;
    unsigned char *inventory_entry;
    int slot_index;

    unit = fdps_get_unit_record(unit_index);

    for (slot_index = 0;
         slot_index < INVENTORY_ENTRY_COUNT;
         slot_index++) {
        inventory_entry = &unit->inventory_slots[slot_index * 2];
        if ((inventory_entry[0] & INVENTORY_FLAG_EMPTY) != 0) {
            inventory_entry[0] = 0;
            inventory_entry[1] = (unsigned char) item_id;
            return 1;
        }
    }

    return -1;
}

/* 00025fe0.  May this unit's class equip this item?  Three calls and then one
   counted loop, with no branch of any kind before the loop is entered.

   The three record calls happen in source order and each result goes to a
   stack local of its own: MOV [EBP-0x18],EAX at 00025ff8 for the unit record,
   MOV [EBP-0x10],EAX at 0002600f for the class equipment record, MOV
   [EBP-0x14],EAX at 0002601e for the item record.  None of the three pointers
   is tested for null and no argument is range checked.

   The class code is byte +0x20 of the unit record, taken zero-extended: MOV
   EAX,[EBP-0x18] / MOV AL,byte ptr [EAX+0x20] / AND EAX,0xff at 00025ffb, so
   a class code of 0x80 or above asks for record 128..255 and not for a record
   in front of the table.  It is pushed exactly as read -- the INC that every
   caller of fdps_get_class_record applies is absent here, because PROEQU.DAT
   has no leading default row (see table.h).

   The item's type is byte +0x00 of its record, also zero-extended and kept as
   an int: XOR EAX,EAX / MOV EDX,[EBP-0x14] / MOV AL,byte ptr [EDX] at
   00026021.  No other field of the item record is read.

   The loop is i in [EBP-0xc] against the literal 6 with JL at 00026032, and
   the body is MOV EAX,[EBP-0x10] / ADD EAX,[EBP-0xc] / MOV AL,byte ptr [EAX] /
   AND EAX,0xff / CMP EAX,[EBP-0x8] -- one byte per position, so the six
   positions are the six bytes of the record and the stride is 1.  A match
   stores 1 and jumps to the epilogue, so the scan stops at the first equal
   position; falling out of all six stores 0.

   There is no sentinel test anywhere in the loop: all six positions are
   compared even though the unused ones hold 0xFF, and a position holding 0 is
   compared like any other.  All four call sites use the answer as a plain
   boolean, TEST EAX,EAX right after the ADD ESP,0x8. */
int fdps_unit_can_equip_item(int unit_index, int item_id)
{
    struct fdps_unit_record *unit;
    struct fdps_class_equip_record *class_equip;
    struct fdps_item_effect *item;
    int item_type;
    int type_slot;

    unit = fdps_get_unit_record(unit_index);
    class_equip = fdps_get_class_equip_record((int) unit->clazz);
    item = fdps_get_item_record(item_id);
    item_type = (int) item->type;

    for (type_slot = 0;
         type_slot < CLASS_EQUIP_TYPE_COUNT;
         type_slot++) {
        if ((int) class_equip->allowed_item_type[type_slot] == item_type) {
            return 1;
        }
    }

    return 0;
}

/* 00026070.  Equips one inventory entry and takes off whatever the unit wore in
   the same category.  Two record calls, then one counted loop over the eight
   entries, then one unconditional store.

   The unit record is resolved TWICE, both times with the same argument: MOV
   EAX,[EBP+0x14] / PUSH EAX / CALL fdps_get_unit_record / MOV [EBP-0x1c],EAX at
   00026080, and again through the parameter slots at 000260a6 into [EBP-0x24].
   The second resolution serves only the id read below and its result is never
   used again; the loop and the final store both address [EBP-0x1c].  That
   second frame -- two parameter-shaped slots at [EBP-0x34]/[EBP-0x30] copied on
   into [EBP-0x28]/[EBP-0x2c], the body replayed against them, and the byte
   copied out of a result slot at [EBP-0x20] into [EBP-0x8] -- is the shape an
   inline expansion leaves (rebuild_info/build_flags.md).  Written open-coded it
   is the same two calls the original makes; turning it into a plain call to
   fdps_unit_get_item_id would put a CALL here that the original does not have.

   The id of the entry being equipped is MOV EDX,[EBP-0x2c] / ADD EDX,EDX / ADD
   EDX,[EBP-0x24] / XOR EAX,EAX / MOV AL,byte ptr [EDX+0xb] at 000260b1: record
   offset 0x0a plus twice the slot plus the 1 that picks the id byte, widened
   without sign, so an entry holding 0xff asks fdps_get_item_record for record
   255.  Its type is byte +0x00 of the item record, XOR EAX,EAX / MOV AL,byte
   ptr [EDX] at 000260d6, kept as an int.

   The loop is i in [EBP-0x10] against the literal 8 with JL at 000260e7, and
   the entry address is formed as the other four scans form it, MOV EAX,
   [EBP-0x10] / ADD EAX,EAX / ADD EAX,[EBP-0x1c] / ADD EAX,0xa at 000260fa.  The
   skip is AND AL,0x40 / AND EAX,0xff / TEST EAX,EAX / JZ -- a mask, so an entry
   carrying other bits alongside 0x40 is still equipped -- and the JZ goes to the
   increment at 0002615f, so the scan runs all eight entries with no early exit
   and every matching entry is unequipped, not just the first.

   The category test is four signed compares against 0x15 on two values that
   came out of bytes: CMP [EBP-0x4],0x15 / JG at 0002613f goes to the second
   pair, and the accepting paths are (both <= 0x15) at 00026145 and (both >
   0x15) at 00026151.  So the two items share a category exactly when they are
   on the same side of 0x15 -- weapons 0x01..0x15 against armour and everything
   above, not equality of the type byte and not the armour span's own 0x27 top
   end.  Ahead of it, CMP [EBP-0xc],0x0 / JZ at 00026139 leaves an equipped
   entry whose item record reads back type 0 alone; that is what keeps a blank
   ITEM.DAT tail record from being taken off as though it were a weapon.

   Unequipping is MOV byte ptr [EAX],0x0 at 0002615c -- a plain zero over the
   whole flag byte, which takes the equipped bit off and leaves the empty bit
   clear, so the entry becomes a carried item.  The id byte beside it is not
   written.

   The last store is MOV EAX,[EBP+0x18] / ADD EAX,EAX / ADD EAX,[EBP-0x1c] / MOV
   byte ptr [EAX+0xa],0x40 at 00026161: it re-reads the slot argument rather
   than a copy, it is a store of the literal and not an OR, and it is
   unconditional -- an entry the loop had just zeroed is equipped again here,
   and so is one that was marked empty.

   Nothing is range checked and neither record pointer is tested for null.  EAX
   still holds the entry address at the RET, but the function is void: both call
   sites push two arguments, CALL, ADD ESP,0x8 and go straight on to another
   MOV EAX. */
void fdps_unit_equip_slot(int unit_index, int slot)
{
    struct fdps_unit_record *unit;
    struct fdps_item_effect *equip_item;
    struct fdps_item_effect *worn_item;
    unsigned char *inventory_entry;
    int equip_item_id;
    int equip_type;
    int worn_type;
    int slot_index;

    unit = fdps_get_unit_record(unit_index);

    equip_item_id = (int)
        fdps_get_unit_record(unit_index)->inventory_slots[slot * 2 + 1];
    equip_item = fdps_get_item_record(equip_item_id);
    equip_type = (int) equip_item->type;

    for (slot_index = 0;
         slot_index < INVENTORY_ENTRY_COUNT;
         slot_index++) {
        inventory_entry = &unit->inventory_slots[slot_index * 2];
        if ((inventory_entry[0] & INVENTORY_FLAG_EQUIPPED) != 0) {
            worn_item = fdps_get_item_record((int) inventory_entry[1]);
            worn_type = (int) worn_item->type;
            if (worn_type != 0 &&
                ((equip_type <= WEAPON_TYPE_MAX &&
                  worn_type <= WEAPON_TYPE_MAX) ||
                 (equip_type > WEAPON_TYPE_MAX &&
                  worn_type > WEAPON_TYPE_MAX))) {
                inventory_entry[0] = 0;
            }
        }
    }

    unit->inventory_slots[slot * 2] = INVENTORY_FLAG_EQUIPPED;
}

/* 00034520.  Which inventory entry holds this item?  One call for the bound, a
   guard on it, and one counted loop over fdps_unit_get_item_id.  The body
   resolves no record itself and reads no byte of one.

   The bound comes first -- MOV EAX,[EBP+0x14] / PUSH EAX / CALL
   fdps_unit_item_count / ADD ESP,0x4 / MOV [EBP-0xc],EAX at 0003452c -- and is
   then guarded on its own: CMP dword ptr [EBP-0xc],0x0 / JNZ at 0003453b, with
   the zero side storing 0xffffffff and jumping straight to the epilogue.  That
   guard decides nothing the loop test would not decide anyway, since a bound of
   zero runs no iterations; it is a branch the original has and the loop below is
   entered only through it.

   The loop test is MOV EAX,[EBP-0x8] / CMP EAX,[EBP-0xc] / JL at 00034551, the
   signed form, and the value it compares against is the OCCUPIED-ENTRY COUNT and
   not the eight physical entries.  That is the whole behaviour of this function
   and it is not the loop an author would write; see the header.

   The body pushes its two arguments right to left -- MOV EAX,[EBP-0x8] / PUSH
   EAX for the entry index, then MOV EAX,[EBP+0x14] / PUSH EAX for the unit index
   at 00034563 -- so the call is fdps_unit_get_item_id(unit_index, slot).  Its
   result is used once and straight out of EAX: CMP EAX,dword ptr [EBP+0x18] /
   JNZ at 00034573.  That is a full 32-bit equality compare against the argument
   exactly as the caller pushed it, so the id is neither masked nor widened here
   and an item_id above 0xff matches nothing -- fdps_unit_get_item_id answers
   0..255.

   A match stores the entry index and jumps to the epilogue, MOV [EBP-0x4],EAX /
   JMP at 00034578, so the LOWEST matching entry is the answer and the scan does
   not run on.  Falling out of the loop stores 0xffffffff at 00034582, the same
   value the empty-inventory guard stores.  Neither argument is range checked and
   nothing is written. */
int fdps_unit_find_item_slot(int unit_index, int item_id)
{
    int occupied_count;
    int slot_index;

    occupied_count = fdps_unit_item_count(unit_index);
    if (occupied_count == 0) {
        return -1;
    }

    for (slot_index = 0; slot_index < occupied_count; slot_index++) {
        if (fdps_unit_get_item_id(unit_index, slot_index) == item_id) {
            return slot_index;
        }
    }

    return -1;
}
