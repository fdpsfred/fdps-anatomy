/* tests/unititem.c -- cover for src/unititem.c.
 *
 * Expected values come from the assembly of fdps_unit_find_equipped_slot at
 * 00025140 -- CMP dword ptr [EBP-0xc],0x8 / JL for the eight entries, ADD
 * EAX,EAX / ADD [EBP-0x18] / ADD 0xa for the entry address, AND AL,0x40 for
 * the equipped test, MOV AL,byte ptr [EAX+0x1] / AND EAX,0xff for the id, XOR
 * EAX,EAX / MOV AL,byte ptr [EDX] for the type, CMP [EBP+0x18],0x0 / JNZ for
 * the kind, and the four signed compares against 0x0, 0x15, 0x15 and 0x27 --
 * from the record layouts ticket 17 settled (inventory_slots at +0x0a, unit
 * record 0x50, item record 0x17 with type at +0x00), and from the item type
 * spans in assets/items.md (weapons 01-15, armour 16-27, blank tail records
 * all zero).  None of them is read off the emitted C.
 *
 * Both tables are staged here rather than read from a game file: the function
 * takes its whole input from data_fdps_map_unit_array_ptr,
 * data_fdps_item_effect_table_ptr and its two arguments, so pointing those
 * globals at local blocks is the only way to reach the loop.  Nothing below
 * asserts what either global holds on its own -- ticket 23 owns that.
 *
 * The fdps_unit_get_item_id cases take theirs from the assembly at 00025200 --
 * ADD EAX,EAX for the two-byte entry stride, MOV EAX,[EBP+0x14] / CALL
 * fdps_get_unit_record for the record, XOR EAX,EAX / MOV AL,byte ptr [EDX+0xb]
 * for the unsigned id byte, and the complete absence of any compare or branch
 * in the body -- and from the same record layouts.  That function touches
 * neither the item table nor the entry's flag byte, so its cases stage the
 * unit block alone.
 *
 * The fdps_unit_item_count cases take theirs from the assembly at 00025240 --
 * CMP dword ptr [EBP-0xc],0x8 / JL for the eight entries, ADD EAX,EAX / ADD
 * EAX,[EBP-0x14] / ADD EAX,0xa for the entry address, AND AL,0x80 / AND
 * EAX,0xff / TEST EAX,EAX / JNZ for the empty test, and the JMP at 0002529d
 * that goes to the increment rather than out of the loop -- and from the same
 * record layouts.  That function reads no id byte and touches no item record,
 * so its cases stage the unit block alone.  The two answers its callers branch
 * on are 8 (CMP EAX,0x8 / JNZ at 000388e7) and 0 (TEST EAX,EAX / JNZ at
 * 0003406e), and both are asserted directly.
 *
 * The fdps_unit_remove_item cases take theirs from the assembly at 00025cc0 --
 * MOV EAX,0x7 / SUB EAX,[EBP+0x18] / ADD EAX,EAX for the (7 - slot) * 2 byte
 * count, ADD EAX,0xc for the source entry and ADD EAX,0xa for the destination,
 * and MOV byte ptr [EAX+0x18],0x80 for the one-byte empty marker on the last
 * entry -- and from the same record layouts.  The slot values exercised are
 * 0 through 7, which is the whole range the callers reach; a slot above 7 hands
 * memmove a negative and therefore huge byte count and is not staged here.
 *
 * The fdps_unit_add_item cases take theirs from the assembly at 00025d20 --
 * CMP dword ptr [EBP-0x8],0x8 / JL for the eight entries, ADD EAX,EAX / ADD
 * EAX,[EBP-0x10] / ADD EAX,0xa for the entry address, AND AL,0x80 / AND
 * EAX,0xff / TEST EAX,EAX / JZ for the empty test with the JZ going to the
 * increment at 00025d4a, MOV byte ptr [EDX],0x0 for the flag store and MOV
 * AL,byte ptr [EBP+0x18] / MOV byte ptr [EDX+0x1],AL for the id store, and the
 * two results 0x1 and 0xffffffff -- and from the same record layouts.  The -1
 * answer is the one fdps_battle_search_cell_at_cursor tests with CMP EAX,-0x1
 * at 00018683.  Those cases stage the unit block alone except where they check
 * that a filled slot stops being equipped, which needs an item record.
 *
 * The fdps_unit_can_equip_item cases take theirs from the assembly at 00025fe0
 * -- MOV AL,byte ptr [EAX+0x20] / AND EAX,0xff for the class code and the
 * absence of an INC before the PUSH, XOR EAX,EAX / MOV AL,byte ptr [EDX] for
 * the item's type byte at +0x00, CMP dword ptr [EBP-0xc],0x6 / JL for the six
 * positions, MOV EAX,[EBP-0x10] / ADD EAX,[EBP-0xc] / MOV AL,byte ptr [EAX]
 * for the one-byte stride across them, the single CMP EAX,[EBP-0x8] that is
 * the whole of the loop body's test, and the two results 0x1 and 0x0 -- and
 * from the 6-byte class equipment record ticket 17 settled, whose base is
 * data_fdps_class_equip_table_ptr.  Those cases stage a third block for that
 * table alongside the other two.
 *
 * Both the item block and the class block are published one record PAST the
 * start of their storage, so record -1 exists and is addressable.  That is what
 * lets an unsigned read be told apart from a signed one: an item id of 0xff
 * must reach record 255 and not the record in front of the base, and so must a
 * class code of 0xff.
 *
 * The fdps_unit_equip_slot cases take theirs from the assembly at 00026070 --
 * XOR EAX,EAX / MOV AL,byte ptr [EDX+0xb] after ADD EDX,EDX for the unsigned id
 * of the entry being equipped, XOR EAX,EAX / MOV AL,byte ptr [EDX] for both
 * item type bytes, CMP dword ptr [EBP-0x10],0x8 / JL for the eight entries, AND
 * AL,0x40 for the equipped test, CMP dword ptr [EBP-0xc],0x0 / JZ for the
 * type-0 skip, the four signed compares against 0x15 at 0002613f, 00026145,
 * 0002614b and 00026151, MOV byte ptr [EAX],0x0 for the unequip store and MOV
 * byte ptr [EAX+0xa],0x40 for the final one -- and from the item type spans in
 * assets/items.md.  The type values exercised are the two ends of the weapon
 * span (0x01 and 0x15), the two ends of the armour span (0x16 and 0x27), a
 * consumable above both (0x2a) and the blank record's 0, which is what pins the
 * category test to the single 0x15 split rather than to the two named spans.
 *
 * The fdps_unit_find_item_slot cases take theirs from the assembly at 00034520
 * -- CMP dword ptr [EBP-0xc],0x0 / JNZ at 0003453b for the empty-inventory
 * guard, CMP EAX,[EBP-0xc] / JL at 00034554 for a loop bounded by the
 * OCCUPIED-ENTRY COUNT rather than by the eight entries, CMP EAX,dword ptr
 * [EBP+0x18] / JNZ at 00034573 for the full-width equality test on the value
 * fdps_unit_get_item_id left in EAX, MOV [EBP-0x4],EAX / JMP at 00034578 for
 * the immediate return on the first match, and the two 0xffffffff stores at
 * 00034541 and 00034582.  Its fixtures stage the unit block alone, since
 * neither of the two accessors it calls touches the item table.  The pair of
 * cases that hold the bound against a hole in the inventory are the ones the
 * plate comment's rebuild note is about: they fail for a body written as a
 * sweep over all eight entries, in both directions.
 *
 * The fdps_unit_item_select_loop cases come next and stage a great deal more,
 * because that one is a modal loop that paints through four callees before it
 * waits.  What can and cannot be reached from a test, and why, is set out above
 * that section rather than here, and the same goes for the
 * fdps_unit_item_select_window cases that close the file: those open the real
 * window over the real Status.cel and are the only ones here that read a
 * shipped game file.
 */
#include <i86.h>
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "statunit.h"
#include "unititem.h"

/* IMUL EAX,dword ptr [EBP+0x14],0x50 in fdps_get_unit_record and IMUL
   EAX,dword ptr [EBP+0x14],0x17 in fdps_get_item_record. */
#define UNIT_RECORD_STRIDE 0x50
#define ITEM_RECORD_STRIDE 0x17

/* ADD EAX,0xa at 0002517d: the inventory entries start at record offset
   0x0a, two bytes each, eight of them. */
#define OFF_INVENTORY 0x0a
#define INVENTORY_ENTRY_COUNT 8

/* AND AL,0x40 at 00025188, and the other two values a seeded entry's flag byte
   takes: 0x80 for an empty slot and 0 for a carried, unequipped item. */
#define FLAG_EQUIPPED 0x40
#define FLAG_EMPTY 0x80
#define FLAG_CARRIED 0x00

/* Four units, so an index other than 0 has somewhere to land and a walk that
   strayed into the neighbouring record would be visible. */
#define STAGE_UNITS 4

/* 256 item records for ids 0x00..0xff, plus one in front of the published base
   so that record -1 is real storage rather than whatever lies before the
   array. */
#define STAGE_ITEMS 256

/* IMUL EAX,dword ptr [EBP+0x14],0x6 in fdps_get_class_equip_record, and CMP
   dword ptr [EBP-0xc],0x6 / JL at 00026032 for the six positions of a
   record. */
#define CLASS_EQUIP_STRIDE 6
#define CLASS_EQUIP_TYPE_COUNT 6

/* MOV AL,byte ptr [EAX+0x20] at 00025ffe: the class code is byte +0x20 of the
   unit record. */
#define OFF_CLAZZ 0x20

/* 256 class equipment records for codes 0x00..0xff, plus one in front of the
   published base so that record -1 is real storage.  The file itself holds 36,
   but the body bounds nothing, and the width of the class code read is what
   decides which of 255 and -1 a code of 0xff reaches. */
#define STAGE_CLASSES 256

static unsigned char unit_block[STAGE_UNITS * UNIT_RECORD_STRIDE];
static unsigned char item_block[(STAGE_ITEMS + 1) * ITEM_RECORD_STRIDE];
static unsigned char class_block[(STAGE_CLASSES + 1) * CLASS_EQUIP_STRIDE];

/* Only ever read for their field sizes and offsets. */
static struct fdps_unit_record layout_probe;
static struct fdps_class_equip_record class_layout_probe;

/* Zero both blocks and publish both bases.  A record staged this way has every
   inventory flag byte clear, which is the "nothing equipped" case, and every
   item type byte zero, which is a blank ITEM.DAT tail record. */
static void stage(void)
{
    int i;

    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_block[i] = 0;
    }
    for (i = 0; i < (int) sizeof(item_block); i++) {
        item_block[i] = 0;
    }
    for (i = 0; i < (int) sizeof(class_block); i++) {
        class_block[i] = 0;
    }
    data_fdps_map_unit_array_ptr = unit_block;
    data_fdps_item_effect_table_ptr = item_block + ITEM_RECORD_STRIDE;
    data_fdps_class_equip_table_ptr = class_block + CLASS_EQUIP_STRIDE;
}

static unsigned char *unit_slot(int unit_index)
{
    return unit_block + unit_index * UNIT_RECORD_STRIDE;
}

/* Fills one of the eight 2-byte inventory entries of one unit: flag byte
   first, id byte second, which is the pair the scan reads at +0x0a + 2 * i. */
static void set_entry(int unit_index, int slot_index, int flags, int item_id)
{
    unsigned char *entry;

    entry = unit_slot(unit_index) + OFF_INVENTORY + slot_index * 2;
    entry[0] = (unsigned char) flags;
    entry[1] = (unsigned char) item_id;
}

/* Writes the type byte at +0x00 of one item record.  item_id is signed on
   purpose: -1 addresses the record in front of the published base, which is
   the fixture the id-width case needs. */
static void set_item_type(int item_id, int type)
{
    item_block[(item_id + 1) * ITEM_RECORD_STRIDE] = (unsigned char) type;
}

/* Writes byte +0x20 of a unit record, the class code fdps_unit_can_equip_item
   hands to fdps_get_class_equip_record. */
static void set_class_code(int unit_index, int class_code)
{
    unit_slot(unit_index)[OFF_CLAZZ] = (unsigned char) class_code;
}

/* Writes one of the six type positions of one class equipment record.
   class_code is signed on purpose: -1 addresses the record in front of the
   published base, which is where a sign-extended class code would land. */
static void set_class_equip(int class_code, int position, int type)
{
    class_block[(class_code + 1) * CLASS_EQUIP_STRIDE + position] =
        (unsigned char) type;
}

/* Fills all six positions of a class equipment record with one value, so a
   case can make every position that is not the one under test disagree. */
static void fill_class_equip(int class_code, int type)
{
    int position;

    for (position = 0; position < CLASS_EQUIP_TYPE_COUNT; position++) {
        set_class_equip(class_code, position, type);
    }
}

/* The offsets and strides the body relies on have to be the layouts' own, or
   the C addresses different bytes from the original. */
static void the_two_record_layouts_match_the_strides(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), UNIT_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots),
             OFF_INVENTORY);
    CHECK_EQ((int) sizeof(layout_probe.inventory_slots),
             INVENTORY_ENTRY_COUNT * 2);
    CHECK_EQ((int) sizeof(struct fdps_item_effect), ITEM_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, type), 0);
}

/* Falling out of the loop stores 0xffffffff: a unit with nothing equipped
   answers -1 for both kinds. */
static void a_unit_with_nothing_equipped_answers_minus_one(void)
{
    stage();
    set_item_type(3, 0x01);
    set_entry(0, 0, FLAG_CARRIED, 3);
    set_entry(0, 1, FLAG_EMPTY, 3);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), -1);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), -1);
}

/* The plain case both ways round: one equipped weapon and one equipped piece
   of armour in different slots, each search finding its own. */
static void each_search_finds_its_own_kind(void)
{
    stage();
    set_item_type(10, 0x01);
    set_item_type(20, 0x16);
    set_entry(0, 2, FLAG_EQUIPPED, 20);
    set_entry(0, 5, FLAG_EQUIPPED, 10);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 5);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), 2);
}

/* MOV [EBP-0x4],EAX / JMP to the epilogue on the first accepted entry: with
   two equipped weapons the lower slot wins, and the loop does not run on to
   the higher one. */
static void the_first_matching_slot_wins(void)
{
    stage();
    set_item_type(10, 0x01);
    set_item_type(11, 0x02);
    set_entry(0, 3, FLAG_EQUIPPED, 10);
    set_entry(0, 6, FLAG_EQUIPPED, 11);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 3);
}

/* All eight entries are scanned, and only eight: an equipped weapon in the
   last entry is found, while the same bytes written two entries past the end
   of the field are not.  0x0a + 8 * 2 is 0x1a, inside the record and past the
   inventory. */
static void all_eight_entries_are_scanned_and_no_more(void)
{
    stage();
    set_item_type(10, 0x01);
    set_entry(0, 7, FLAG_EQUIPPED, 10);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 7);

    stage();
    set_item_type(10, 0x01);
    set_entry(0, 8, FLAG_EQUIPPED, 10);
    set_entry(0, 9, FLAG_EQUIPPED, 10);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), -1);
}

/* AND AL,0x40 is a mask and not a compare.  An entry carrying other bits
   alongside 0x40 is still equipped, and an entry carrying only other bits --
   0x80 empty, 0x20, 0x01 -- is skipped however good its item is. */
static void only_bit_0x40_equips_an_entry(void)
{
    stage();
    set_item_type(10, 0x01);
    set_entry(0, 0, FLAG_EMPTY, 10);
    set_entry(0, 1, 0x20, 10);
    set_entry(0, 2, 0x01, 10);
    set_entry(0, 3, 0xbf, 10);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), -1);

    set_entry(0, 3, 0xc1, 10);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 3);
}

/* The weapon span is closed at both ends: CMP ...,0x0 / JLE rejects and CMP
   ...,0x15 / JLE accepts, so 0x01 and 0x15 match while 0x16 does not. */
static void the_weapon_span_runs_from_one_to_0x15(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x15);
    set_item_type(3, 0x16);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 0);

    set_entry(0, 0, FLAG_EQUIPPED, 2);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 0);

    set_entry(0, 0, FLAG_EQUIPPED, 3);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), -1);
}

/* The rebuild note.  An equipped slot whose item record reads back type 0 --
   which is what ITEM.DAT's all-zero records past id 0xe1 give -- is NOT the
   equipped weapon.  Writing `type <= 0x15` without the lower end would answer
   0 here.  A real weapon in a later slot is still found, so the type-0 entry
   is skipped rather than the whole search abandoned. */
static void a_blank_item_record_is_not_an_equipped_weapon(void)
{
    stage();
    set_entry(0, 0, FLAG_EQUIPPED, 0xf0);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), -1);

    set_item_type(10, 0x01);
    set_entry(0, 4, FLAG_EQUIPPED, 10);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 4);
}

/* The armour span is closed at both ends too: CMP ...,0x15 / JLE rejects and
   CMP ...,0x27 / JLE accepts, so 0x16 and 0x27 match while 0x15 and 0x28 do
   not.  Type 0 fails the armour search through the same lower compare. */
static void the_armor_span_runs_from_0x16_to_0x27(void)
{
    stage();
    set_item_type(1, 0x16);
    set_item_type(2, 0x27);
    set_item_type(3, 0x15);
    set_item_type(4, 0x28);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), 0);

    set_entry(0, 0, FLAG_EQUIPPED, 2);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), 0);

    set_entry(0, 0, FLAG_EQUIPPED, 3);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), -1);

    set_entry(0, 0, FLAG_EQUIPPED, 4);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), -1);

    set_entry(0, 0, FLAG_EQUIPPED, 0xf0);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), -1);
}

/* CMP dword ptr [EBP+0x18],0x0 / JNZ: want_armor is a zero test, so 2, -1 and
   0x100 all ask for armour exactly as 1 does.  0x100 matters because a byte
   compare would read it as zero and answer the weapon search instead. */
static void any_non_zero_want_armor_asks_for_armor(void)
{
    stage();
    set_item_type(10, 0x01);
    set_item_type(20, 0x16);
    set_entry(0, 1, FLAG_EQUIPPED, 10);
    set_entry(0, 6, FLAG_EQUIPPED, 20);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 1);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), 6);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 2), 6);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, -1), 6);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0x100), 6);
}

/* MOV AL,byte ptr [EAX+0x1] / AND EAX,0xff: the id byte reaches
   fdps_get_item_record zero-extended.  Record 255 is staged as a weapon and
   the record in FRONT of the base -- where a sign-extended 0xff would land --
   as armour, so a signed read would answer -1 for the weapon search and 0 for
   the armour one.  The staged answers are the reverse of that. */
static void the_item_id_byte_is_unsigned(void)
{
    stage();
    set_item_type(0xff, 0x01);
    set_item_type(-1, 0x16);
    set_entry(0, 0, FLAG_EQUIPPED, 0xff);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 0);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), -1);
}

/* The unit index reaches fdps_get_unit_record and picks the record the search
   runs over: the same entry in three different records answers differently,
   and the record in front of the base is reachable because the stride multiply
   is signed. */
static void the_index_selects_its_own_unit_record(void)
{
    stage();
    set_item_type(10, 0x01);
    set_entry(1, 4, FLAG_EQUIPPED, 10);
    set_entry(2, 6, FLAG_EQUIPPED, 10);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), -1);
    CHECK_EQ(fdps_unit_find_equipped_slot(1, 0), 4);
    CHECK_EQ(fdps_unit_find_equipped_slot(2, 0), 6);

    data_fdps_map_unit_array_ptr = unit_slot(2);
    CHECK_EQ(fdps_unit_find_equipped_slot(-1, 0), 4);
}

/* Nothing is written: neither the unit record the search walks nor the item
   records it reads differ by a byte afterwards, for a call that matches and
   for one that runs the loop out. */
static void the_search_writes_nothing(void)
{
    static unsigned char unit_before[sizeof(unit_block)];
    static unsigned char item_before[sizeof(item_block)];
    int i;
    int unit_diffs;
    int item_diffs;

    stage();
    set_item_type(10, 0x01);
    set_item_type(20, 0x16);
    set_entry(0, 0, FLAG_EQUIPPED, 20);
    set_entry(0, 4, FLAG_EQUIPPED, 10);
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_before[i] = unit_block[i];
    }
    for (i = 0; i < (int) sizeof(item_block); i++) {
        item_before[i] = item_block[i];
    }

    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 4);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), 0);
    CHECK_EQ(fdps_unit_find_equipped_slot(3, 0), -1);

    unit_diffs = 0;
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        if (unit_block[i] != unit_before[i]) {
            unit_diffs++;
        }
    }
    item_diffs = 0;
    for (i = 0; i < (int) sizeof(item_block); i++) {
        if (item_block[i] != item_before[i]) {
            item_diffs++;
        }
    }
    CHECK_EQ(unit_diffs, 0);
    CHECK_EQ(item_diffs, 0);
}

/* The base is re-read on every call, because fdps_get_unit_record reads the
   global each time: republishing it between two otherwise identical calls
   changes the answer. */
static void the_record_is_resolved_on_every_call(void)
{
    stage();
    set_item_type(10, 0x01);
    set_entry(0, 2, FLAG_EQUIPPED, 10);
    set_entry(1, 7, FLAG_EQUIPPED, 10);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 2);

    data_fdps_map_unit_array_ptr = unit_slot(1);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 7);
}

/* fdps_unit_get_item_id at 00025200.  MOV AL,byte ptr [EDX+0xb] after ADD
   EAX,EAX: the byte the function hands back is the SECOND byte of the entry,
   the id, and never the flag byte at +0x0a + 2 * slot. */
static void get_item_id_returns_the_entrys_second_byte(void)
{
    stage();
    set_entry(0, 3, FLAG_CARRIED, 0x2a);
    CHECK_EQ(fdps_unit_get_item_id(0, 3), 0x2a);

    set_entry(0, 3, 0x77, 0x2a);
    CHECK_EQ(fdps_unit_get_item_id(0, 3), 0x2a);
}

/* ADD EAX,EAX before the record base is added: the stride between entries is
   two bytes, so each of the eight slots answers with its own id and no slot
   reads its neighbour's. */
static void get_item_id_reads_each_of_the_eight_slots(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_EQUIPPED, 0x10 + slot_index);
    }
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        CHECK_EQ(fdps_unit_get_item_id(0, slot_index), 0x10 + slot_index);
    }
}

/* XOR EAX,EAX / MOV AL,byte ptr [EDX+0xb]: the byte is widened with no sign,
   so the two ids a sign-extending read would turn negative come back as 255
   and 128.  This is the width fdps_get_item_record is then indexed with. */
static void get_item_id_widens_the_byte_without_sign(void)
{
    stage();
    set_entry(0, 0, FLAG_EQUIPPED, 0xff);
    set_entry(0, 1, FLAG_EQUIPPED, 0x80);
    set_entry(0, 2, FLAG_EQUIPPED, 0x7f);
    CHECK_EQ(fdps_unit_get_item_id(0, 0), 255);
    CHECK_EQ(fdps_unit_get_item_id(0, 1), 128);
    CHECK_EQ(fdps_unit_get_item_id(0, 2), 127);
}

/* There is no test of the flag byte anywhere in the body -- no AND, no TEST,
   no branch of any kind.  An entry marked empty (0x80) or merely carried
   answers with its id byte exactly as an equipped one does, so the answer says
   nothing about whether the slot holds anything. */
static void get_item_id_ignores_the_entry_flag_byte(void)
{
    stage();
    set_entry(0, 5, FLAG_EMPTY, 0x63);
    CHECK_EQ(fdps_unit_get_item_id(0, 5), 0x63);

    set_entry(0, 5, FLAG_CARRIED, 0x63);
    CHECK_EQ(fdps_unit_get_item_id(0, 5), 0x63);

    set_entry(0, 5, FLAG_EQUIPPED, 0x63);
    CHECK_EQ(fdps_unit_get_item_id(0, 5), 0x63);
}

/* The rebuild note.  slot goes into the address as slot * 2 with nothing in
   between, so slot -1 -- which is what fdps_unit_resolve_attack_hit passes
   when fdps_unit_find_equipped_slot found nothing -- reads record offset 0x09,
   the reserved_09 byte in front of the inventory, and slot 8 reads 0x1b, the
   second byte of spells_known_bitmap.  A bounds check or an early return of 0
   or -1 would answer differently on both. */
static void get_item_id_does_not_range_check_the_slot(void)
{
    unsigned char *record;

    stage();
    record = unit_slot(0);
    record[0x09] = 0x5c;
    record[0x1b] = 0x3d;
    CHECK_EQ(fdps_unit_get_item_id(0, -1), 0x5c);
    CHECK_EQ(fdps_unit_get_item_id(0, 8), 0x3d);
}

/* MOV EAX,[EBP+0x14] / PUSH EAX / CALL fdps_get_unit_record: the first
   argument picks the record, so the same slot answers differently in different
   records.  The multiply inside fdps_get_unit_record is signed, so index -1
   reaches the record in front of the published base. */
static void get_item_id_takes_its_record_from_the_index(void)
{
    stage();
    set_entry(0, 2, FLAG_EQUIPPED, 0x11);
    set_entry(1, 2, FLAG_EQUIPPED, 0x22);
    set_entry(2, 2, FLAG_EQUIPPED, 0x33);
    CHECK_EQ(fdps_unit_get_item_id(0, 2), 0x11);
    CHECK_EQ(fdps_unit_get_item_id(1, 2), 0x22);
    CHECK_EQ(fdps_unit_get_item_id(2, 2), 0x33);

    data_fdps_map_unit_array_ptr = unit_slot(1);
    CHECK_EQ(fdps_unit_get_item_id(-1, 2), 0x11);
}

/* The body has no store to the record: the unit block is byte-for-byte the
   same after a read of an occupied slot, an empty one and an out-of-range
   one. */
static void get_item_id_writes_nothing(void)
{
    static unsigned char block_before[sizeof(unit_block)];
    int i;
    int diffs;

    stage();
    set_entry(0, 0, FLAG_EQUIPPED, 0x44);
    set_entry(1, 7, FLAG_EMPTY, 0x55);
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        block_before[i] = unit_block[i];
    }

    CHECK_EQ(fdps_unit_get_item_id(0, 0), 0x44);
    CHECK_EQ(fdps_unit_get_item_id(1, 7), 0x55);
    CHECK_EQ(fdps_unit_get_item_id(2, 9), 0);

    diffs = 0;
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        if (unit_block[i] != block_before[i]) {
            diffs++;
        }
    }
    CHECK_EQ(diffs, 0);
}

/* The base global is read through fdps_get_unit_record on every call rather
   than cached, so republishing it between two identical calls changes the
   answer. */
static void get_item_id_resolves_the_record_on_every_call(void)
{
    stage();
    set_entry(0, 6, FLAG_EQUIPPED, 0x66);
    set_entry(1, 6, FLAG_EQUIPPED, 0x77);
    CHECK_EQ(fdps_unit_get_item_id(0, 6), 0x66);

    data_fdps_map_unit_array_ptr = unit_slot(1);
    CHECK_EQ(fdps_unit_get_item_id(0, 6), 0x77);
}

/* fdps_unit_item_count at 00025240.  AND AL,0x80 / AND EAX,0xff / TEST EAX,
   EAX / JNZ: an entry counts when the empty bit is CLEAR.  A record staged
   with every flag byte zero is therefore eight occupied slots, and one whose
   entries all carry 0x80 is none.  0 and 8 are the two answers the callers
   branch on -- CMP EAX,0x8 / JNZ at 000388e7 and 0003b82e, TEST EAX,EAX / JNZ
   at 0003406e. */
static void item_count_counts_entries_without_the_empty_bit(void)
{
    int slot_index;

    stage();
    CHECK_EQ(fdps_unit_item_count(0), INVENTORY_ENTRY_COUNT);

    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_EMPTY, 0);
    }
    CHECK_EQ(fdps_unit_item_count(0), 0);
}

/* AND AL,0x80 is a mask and not a compare, and the bit it tests is 0x80 and no
   other.  An entry carrying 0x40, 0x01 or 0x7f is occupied; one carrying 0x80,
   0x81 or 0xff is empty.  A single count over one record staged with three of
   each pins both halves at once. */
static void item_count_tests_bit_0x80_alone(void)
{
    stage();
    set_entry(0, 0, FLAG_EQUIPPED, 0x11);
    set_entry(0, 1, 0x01, 0x11);
    set_entry(0, 2, 0x7f, 0x11);
    set_entry(0, 3, FLAG_EMPTY, 0x11);
    set_entry(0, 4, 0x81, 0x11);
    set_entry(0, 5, 0xff, 0x11);
    set_entry(0, 6, FLAG_EMPTY, 0x11);
    set_entry(0, 7, FLAG_CARRIED, 0x11);
    CHECK_EQ(fdps_unit_item_count(0), 4);
}

/* The id byte at +1 of the entry is never read: the same eight flag bytes
   answer the same whether the ids beside them are 0, 0xff or anything else. */
static void item_count_ignores_the_item_id_byte(void)
{
    stage();
    set_entry(0, 0, FLAG_CARRIED, 0x00);
    set_entry(0, 1, FLAG_CARRIED, 0xff);
    set_entry(0, 2, FLAG_EQUIPPED, 0x80);
    set_entry(0, 3, FLAG_EMPTY, 0x2a);
    set_entry(0, 4, FLAG_EMPTY, 0x00);
    set_entry(0, 5, FLAG_EMPTY, 0xff);
    set_entry(0, 6, FLAG_EMPTY, 0x7f);
    set_entry(0, 7, FLAG_EMPTY, 0x01);
    CHECK_EQ(fdps_unit_item_count(0), 3);
}

/* The JMP at 0002529d goes to the increment, not out of the loop: an empty
   entry in the middle does not end the scan, so items sitting above a hole are
   still counted.  An author who wrote the loop as a search for the first empty
   slot would answer 1 for the first fixture and 0 for the second. */
static void item_count_does_not_stop_at_the_first_empty_slot(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_CARRIED, 0x10 + slot_index);
    }
    set_entry(0, 1, FLAG_EMPTY, 0x10);
    set_entry(0, 4, FLAG_EMPTY, 0x10);
    CHECK_EQ(fdps_unit_item_count(0), 6);

    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_EMPTY, 0x10);
    }
    set_entry(0, 7, FLAG_EQUIPPED, 0x10);
    CHECK_EQ(fdps_unit_item_count(0), 1);
}

/* CMP dword ptr [EBP-0xc],0x8 / JL: eight entries and no more.  The last entry
   is inside the count, while an occupied pair written two entries past the end
   of the field -- record offsets 0x1a and 0x1c, inside the record and past the
   inventory -- adds nothing. */
static void item_count_scans_eight_entries_and_no_more(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_EMPTY, 0);
    }
    set_entry(0, 7, FLAG_CARRIED, 0x10);
    CHECK_EQ(fdps_unit_item_count(0), 1);

    set_entry(0, 7, FLAG_EMPTY, 0x10);
    set_entry(0, 8, FLAG_CARRIED, 0x10);
    set_entry(0, 9, FLAG_CARRIED, 0x10);
    CHECK_EQ(fdps_unit_item_count(0), 0);
}

/* MOV EAX,[EBP+0x14] / PUSH EAX / CALL fdps_get_unit_record: the argument
   picks the record the count runs over, so three records staged differently
   answer differently, and the multiply inside fdps_get_unit_record is signed,
   so index -1 reaches the record in front of the published base. */
static void item_count_takes_its_record_from_the_index(void)
{
    int slot_index;
    int unit_index;

    stage();
    for (unit_index = 0; unit_index < STAGE_UNITS; unit_index++) {
        for (slot_index = 0;
             slot_index < INVENTORY_ENTRY_COUNT;
             slot_index++) {
            set_entry(unit_index, slot_index, FLAG_EMPTY, 0);
        }
    }
    set_entry(1, 0, FLAG_CARRIED, 0x10);
    set_entry(1, 3, FLAG_EQUIPPED, 0x11);
    set_entry(2, 5, FLAG_CARRIED, 0x12);
    CHECK_EQ(fdps_unit_item_count(0), 0);
    CHECK_EQ(fdps_unit_item_count(1), 2);
    CHECK_EQ(fdps_unit_item_count(2), 1);

    data_fdps_map_unit_array_ptr = unit_slot(2);
    CHECK_EQ(fdps_unit_item_count(-1), 2);
}

/* No store anywhere in the body: the unit block is byte-for-byte the same
   after a full inventory, an empty one and a mixed one. */
static void item_count_writes_nothing(void)
{
    static unsigned char block_before[sizeof(unit_block)];
    int i;
    int diffs;

    stage();
    set_entry(0, 2, FLAG_EMPTY, 0x33);
    set_entry(1, 0, FLAG_EMPTY, 0x44);
    set_entry(1, 1, FLAG_EMPTY, 0x44);
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        block_before[i] = unit_block[i];
    }

    CHECK_EQ(fdps_unit_item_count(0), 7);
    CHECK_EQ(fdps_unit_item_count(1), 6);
    CHECK_EQ(fdps_unit_item_count(2), 8);

    diffs = 0;
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        if (unit_block[i] != block_before[i]) {
            diffs++;
        }
    }
    CHECK_EQ(diffs, 0);
}

/* The record is resolved through fdps_get_unit_record on every call rather
   than cached, so republishing the base between two identical calls changes
   the answer. */
static void item_count_resolves_the_record_on_every_call(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_EMPTY, 0);
        set_entry(1, slot_index, FLAG_EMPTY, 0);
    }
    set_entry(1, 4, FLAG_CARRIED, 0x10);
    CHECK_EQ(fdps_unit_item_count(0), 0);

    data_fdps_map_unit_array_ptr = unit_slot(1);
    CHECK_EQ(fdps_unit_item_count(0), 1);
}

/* fdps_unit_remove_item at 00025cc0.  MOV EAX,0x7 / SUB EAX,[EBP+0x18] / ADD
   EAX,EAX for the byte count, then the source at record + 0x0c + slot * 2 and
   the destination at record + 0x0a + slot * 2: entries slot+1..7 move down one
   place.  The entries below the removed one keep their bytes, and entry 7 is
   the source of the last move and is never a destination, so its own two bytes
   are untouched by the copy. */
static void remove_item_shifts_the_entries_above_it_down(void)
{
    unsigned char *record;
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, 0x40 + slot_index, 0x10 + slot_index);
    }

    fdps_unit_remove_item(0, 2);

    record = unit_slot(0) + OFF_INVENTORY;
    CHECK_EQ(record[0], 0x40);
    CHECK_EQ(record[1], 0x10);
    CHECK_EQ(record[2], 0x41);
    CHECK_EQ(record[3], 0x11);
    CHECK_EQ(record[4], 0x43);
    CHECK_EQ(record[5], 0x13);
    CHECK_EQ(record[6], 0x44);
    CHECK_EQ(record[7], 0x14);
    CHECK_EQ(record[8], 0x45);
    CHECK_EQ(record[9], 0x15);
    CHECK_EQ(record[10], 0x46);
    CHECK_EQ(record[11], 0x16);
    CHECK_EQ(record[12], 0x47);
    CHECK_EQ(record[13], 0x17);
}

/* Removing entry 0 is the longest move -- count (7 - 0) * 2 = 14 -- and the
   source and destination overlap by six of the seven entries, so a copy that
   ran downward instead of upward would smear entry 1 across the field.  Every
   entry below the last must end up holding its higher neighbour's pair. */
static void remove_item_at_slot_zero_moves_all_seven_entries(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_CARRIED, 0x20 + slot_index);
    }

    fdps_unit_remove_item(0, 0);

    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT - 1; slot_index++) {
        CHECK_EQ(fdps_unit_get_item_id(0, slot_index), 0x21 + slot_index);
    }
}

/* MOV byte ptr [EAX+0x18],0x80: the last entry is marked empty whichever slot
   was removed, and the count that mark answers is the one the callers branch
   on.  A full inventory becomes seven. */
static void remove_item_always_marks_the_last_entry_empty(void)
{
    int removed_slot;
    int slot_index;

    for (removed_slot = 0;
         removed_slot < INVENTORY_ENTRY_COUNT;
         removed_slot++) {
        stage();
        for (slot_index = 0;
             slot_index < INVENTORY_ENTRY_COUNT;
             slot_index++) {
            set_entry(0, slot_index, FLAG_CARRIED, 0x30 + slot_index);
        }

        fdps_unit_remove_item(0, removed_slot);

        CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 14], FLAG_EMPTY);
        CHECK_EQ(fdps_unit_item_count(0), INVENTORY_ENTRY_COUNT - 1);
    }
}

/* The rebuild note.  The store is one byte wide, so the id byte at record
   offset 0x19 keeps what the shift left in it.  Entry 7 is never a destination
   of the move, so what it keeps is its own original id -- 0x37 here -- and
   fdps_unit_get_item_id, which does not consult the flag byte, still answers
   with it.  Clearing the whole 2-byte entry would answer 0. */
static void remove_item_leaves_the_last_entrys_id_byte_alone(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_CARRIED, 0x30 + slot_index);
    }

    fdps_unit_remove_item(0, 3);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 15], 0x37);
    CHECK_EQ(fdps_unit_get_item_id(0, 7), 0x37);
}

/* SUB EAX,[EBP+0x18] with slot 7 gives a count of zero: memmove copies nothing
   and the only change to the record is the empty marker.  Both bytes of entry 6
   and the id byte of entry 7 are exactly as they were. */
static void remove_item_at_the_last_slot_moves_nothing(void)
{
    unsigned char *record;
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_EQUIPPED, 0x50 + slot_index);
    }

    fdps_unit_remove_item(0, INVENTORY_ENTRY_COUNT - 1);

    record = unit_slot(0) + OFF_INVENTORY;
    CHECK_EQ(record[12], FLAG_EQUIPPED);
    CHECK_EQ(record[13], 0x56);
    CHECK_EQ(record[14], FLAG_EMPTY);
    CHECK_EQ(record[15], 0x57);
}

/* The three addresses are all record + 0x0a + something, and the widest write
   is entry 0 through the marker at 0x18: nothing below record offset 0x0a and
   nothing from 0x19 up is touched, and the neighbouring records are not
   touched at all. */
static void remove_item_writes_only_inside_the_inventory_field(void)
{
    static unsigned char block_before[sizeof(unit_block)];
    int i;
    int diffs;

    stage();
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_block[i] = (unsigned char) (i + 1);
    }
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        block_before[i] = unit_block[i];
    }

    fdps_unit_remove_item(1, 0);

    diffs = 0;
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        if (i >= UNIT_RECORD_STRIDE + OFF_INVENTORY &&
            i <= UNIT_RECORD_STRIDE + OFF_INVENTORY + 14) {
            continue;
        }
        if (unit_block[i] != block_before[i]) {
            diffs++;
        }
    }
    CHECK_EQ(diffs, 0);
}

/* MOV EAX,[EBP+0x14] / PUSH EAX / CALL fdps_get_unit_record: the first argument
   picks the record that is edited, so a removal from one unit leaves its
   neighbours' inventories exactly as they were. */
static void remove_item_edits_the_record_the_index_names(void)
{
    int slot_index;
    int unit_index;

    stage();
    for (unit_index = 0; unit_index < STAGE_UNITS; unit_index++) {
        for (slot_index = 0;
             slot_index < INVENTORY_ENTRY_COUNT;
             slot_index++) {
            set_entry(unit_index, slot_index, FLAG_CARRIED,
                      0x60 + slot_index);
        }
    }

    fdps_unit_remove_item(2, 0);

    CHECK_EQ(fdps_unit_item_count(0), INVENTORY_ENTRY_COUNT);
    CHECK_EQ(fdps_unit_item_count(1), INVENTORY_ENTRY_COUNT);
    CHECK_EQ(fdps_unit_item_count(2), INVENTORY_ENTRY_COUNT - 1);
    CHECK_EQ(fdps_unit_item_count(3), INVENTORY_ENTRY_COUNT);
    CHECK_EQ(fdps_unit_get_item_id(2, 0), 0x61);
    CHECK_EQ(fdps_unit_get_item_id(1, 0), 0x60);
}

/* The base is read through fdps_get_unit_record on every call, so republishing
   it between two identical calls sends the second removal to a different
   record. */
static void remove_item_resolves_the_record_on_every_call(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_CARRIED, 0x70 + slot_index);
        set_entry(1, slot_index, FLAG_CARRIED, 0x80 + slot_index);
    }

    fdps_unit_remove_item(0, 0);
    CHECK_EQ(fdps_unit_get_item_id(0, 0), 0x71);
    CHECK_EQ(fdps_unit_get_item_id(1, 0), 0x80);

    data_fdps_map_unit_array_ptr = unit_slot(1);
    fdps_unit_remove_item(0, 0);
    CHECK_EQ(unit_block[UNIT_RECORD_STRIDE + OFF_INVENTORY + 1], 0x81);
}

/* Marks all eight entries of one unit empty.  stage() leaves every flag byte
   zero, which is eight OCCUPIED entries as far as the 0x80 test is concerned,
   so the add cases have to raise the empty bit themselves. */
static void empty_inventory(int unit_index)
{
    int slot_index;

    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(unit_index, slot_index, FLAG_EMPTY, 0);
    }
}

/* fdps_unit_add_item at 00025d20.  The first entry whose flag byte carries
   0x80 takes the item: with entries 0 and 1 occupied the item lands in entry 2,
   the answer is 1, and the entries above it are left empty -- the scan stops at
   the first one it fills, MOV [EBP-0x4],0x1 / JMP to the epilogue. */
static void add_item_fills_the_first_empty_entry(void)
{
    stage();
    empty_inventory(0);
    set_entry(0, 0, FLAG_CARRIED, 0x11);
    set_entry(0, 1, FLAG_EQUIPPED, 0x12);

    CHECK_EQ(fdps_unit_add_item(0, 0x2a), 1);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 4], 0);
    CHECK_EQ(fdps_unit_get_item_id(0, 2), 0x2a);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], FLAG_CARRIED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 6], FLAG_EMPTY);
    CHECK_EQ(fdps_unit_item_count(0), 3);
}

/* The rebuild note.  MOV byte ptr [EDX],0x0 puts a plain zero over the WHOLE
   flag byte, so both the empty bit and the equipped bit go: an entry staged
   0xc0 -- empty and equipped at once -- comes out at 0, which is what makes
   fdps_unit_item_count count the slot and fdps_unit_find_equipped_slot pass it
   over.  Writing only the id byte and leaving the flags alone, which is the
   obvious C, would leave 0xc0 there and count zero items. */
static void add_item_zeroes_the_whole_flag_byte(void)
{
    stage();
    empty_inventory(0);
    set_item_type(10, 0x01);
    set_entry(0, 0, 0xc0, 0);

    CHECK_EQ(fdps_unit_add_item(0, 10), 1);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(fdps_unit_item_count(0), 1);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), -1);
}

/* AND AL,0x80 is a mask and not a compare.  Entries carrying 0x40, 0x00 and
   0x7f are all occupied and are passed over; the first entry carrying 0x80 in
   among other bits -- 0x81 here -- is the one that takes the item, and the
   entries below it keep their bytes. */
static void add_item_tests_bit_0x80_as_a_mask(void)
{
    stage();
    set_entry(0, 0, FLAG_EQUIPPED, 0x11);
    set_entry(0, 1, FLAG_CARRIED, 0x12);
    set_entry(0, 2, 0x7f, 0x13);
    set_entry(0, 3, 0x81, 0x14);
    set_entry(0, 4, 0xff, 0x15);
    set_entry(0, 5, FLAG_EMPTY, 0x16);
    set_entry(0, 6, FLAG_EMPTY, 0x17);
    set_entry(0, 7, FLAG_EMPTY, 0x18);

    CHECK_EQ(fdps_unit_add_item(0, 0x33), 1);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 6], 0);
    CHECK_EQ(fdps_unit_get_item_id(0, 3), 0x33);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 4], 0x7f);
    CHECK_EQ(fdps_unit_get_item_id(0, 2), 0x13);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 8], 0xff);
}

/* The JZ at 00025d6e goes to the increment and not out of the loop, so an
   occupied entry does not end the scan: an item dropped into an inventory whose
   only free entry is a hole in the middle lands in the hole, and the entries
   above the hole keep their own ids.  Nothing is shifted. */
static void add_item_fills_a_hole_rather_than_the_end(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_CARRIED, 0x40 + slot_index);
    }
    set_entry(0, 5, FLAG_EMPTY, 0x45);

    CHECK_EQ(fdps_unit_add_item(0, 0x99), 1);

    CHECK_EQ(fdps_unit_get_item_id(0, 5), 0x99);
    CHECK_EQ(fdps_unit_get_item_id(0, 4), 0x44);
    CHECK_EQ(fdps_unit_get_item_id(0, 6), 0x46);
    CHECK_EQ(fdps_unit_get_item_id(0, 7), 0x47);
    CHECK_EQ(fdps_unit_item_count(0), INVENTORY_ENTRY_COUNT);
}

/* Falling out of the loop stores 0xffffffff and nothing else happens: a unit
   whose eight entries are all occupied answers -1 -- the value
   fdps_battle_search_cell_at_cursor tests with CMP EAX,-0x1 -- and not one byte
   of the block differs afterwards.  A staged record is exactly this case, since
   every flag byte is zero. */
static void add_item_answers_minus_one_when_full_and_writes_nothing(void)
{
    static unsigned char block_before[sizeof(unit_block)];
    int i;
    int diffs;

    stage();
    for (i = 0; i < INVENTORY_ENTRY_COUNT; i++) {
        set_entry(0, i, FLAG_CARRIED, 0x50 + i);
    }
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        block_before[i] = unit_block[i];
    }

    CHECK_EQ(fdps_unit_add_item(0, 0x55), -1);

    diffs = 0;
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        if (unit_block[i] != block_before[i]) {
            diffs++;
        }
    }
    CHECK_EQ(diffs, 0);
}

/* MOV AL,byte ptr [EBP+0x18]: one byte of the pushed argument reaches the
   record and no more, so 0x1ff stores 0xff and -1 stores 0xff as well, while
   0x100 stores 0.  Every call site pushes a full dword, so the truncation is
   the callee's. */
static void add_item_stores_only_the_low_byte_of_the_id(void)
{
    stage();
    empty_inventory(0);

    CHECK_EQ(fdps_unit_add_item(0, 0x1ff), 1);
    CHECK_EQ(fdps_unit_add_item(0, -1), 1);
    CHECK_EQ(fdps_unit_add_item(0, 0x100), 1);
    CHECK_EQ(fdps_unit_add_item(0, 0x2a), 1);

    CHECK_EQ(fdps_unit_get_item_id(0, 0), 0xff);
    CHECK_EQ(fdps_unit_get_item_id(0, 1), 0xff);
    CHECK_EQ(fdps_unit_get_item_id(0, 2), 0x00);
    CHECK_EQ(fdps_unit_get_item_id(0, 3), 0x2a);
}

/* CMP dword ptr [EBP-0x8],0x8 / JL: eight entries and no more.  The last entry
   is inside the scan and takes the item; an empty pair written two entries past
   the end of the field -- record offsets 0x1a and 0x1c, inside the record and
   past the inventory -- is not reached, so the call answers -1 and leaves those
   bytes as they were. */
static void add_item_scans_eight_entries_and_no_more(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_CARRIED, 0x60 + slot_index);
    }
    set_entry(0, 7, FLAG_EMPTY, 0x67);
    CHECK_EQ(fdps_unit_add_item(0, 0x77), 1);
    CHECK_EQ(fdps_unit_get_item_id(0, 7), 0x77);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 14], 0);

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_CARRIED, 0x60 + slot_index);
    }
    set_entry(0, 8, FLAG_EMPTY, 0);
    set_entry(0, 9, FLAG_EMPTY, 0);
    CHECK_EQ(fdps_unit_add_item(0, 0x77), -1);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 16], FLAG_EMPTY);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 18], FLAG_EMPTY);
}

/* The two stores are the entry's own two bytes and nothing else: with the whole
   block filled with a pattern and one entry of unit 1 marked empty, exactly
   record offsets 0x12 and 0x13 of that record differ afterwards. */
static void add_item_writes_only_the_two_bytes_of_the_slot(void)
{
    static unsigned char block_before[sizeof(unit_block)];
    int i;
    int diffs;
    int flag_byte;
    int id_byte;

    stage();
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_block[i] = (unsigned char) (i + 1);
    }
    set_entry(1, 4, FLAG_EMPTY, 0);
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        block_before[i] = unit_block[i];
    }

    CHECK_EQ(fdps_unit_add_item(1, 0x99), 1);

    flag_byte = UNIT_RECORD_STRIDE + OFF_INVENTORY + 8;
    id_byte = flag_byte + 1;
    CHECK_EQ(unit_block[flag_byte], 0);
    CHECK_EQ(unit_block[id_byte], 0x99);

    diffs = 0;
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        if (i == flag_byte || i == id_byte) {
            continue;
        }
        if (unit_block[i] != block_before[i]) {
            diffs++;
        }
    }
    CHECK_EQ(diffs, 0);
}

/* MOV EAX,[EBP+0x14] / PUSH EAX / CALL fdps_get_unit_record: the first argument
   picks the record that is written, so the neighbouring units keep their empty
   inventories.  The multiply inside fdps_get_unit_record is signed, so index -1
   reaches the record in front of the published base. */
static void add_item_writes_the_record_the_index_names(void)
{
    int unit_index;

    stage();
    for (unit_index = 0; unit_index < STAGE_UNITS; unit_index++) {
        empty_inventory(unit_index);
    }

    CHECK_EQ(fdps_unit_add_item(1, 0x61), 1);
    CHECK_EQ(fdps_unit_item_count(0), 0);
    CHECK_EQ(fdps_unit_item_count(1), 1);
    CHECK_EQ(fdps_unit_item_count(2), 0);
    CHECK_EQ(fdps_unit_get_item_id(1, 0), 0x61);

    data_fdps_map_unit_array_ptr = unit_slot(2);
    CHECK_EQ(fdps_unit_add_item(-1, 0x62), 1);
    data_fdps_map_unit_array_ptr = unit_block;
    CHECK_EQ(fdps_unit_get_item_id(1, 1), 0x62);
    CHECK_EQ(fdps_unit_item_count(1), 2);
}

/* The base global is read through fdps_get_unit_record on every call rather
   than cached, so republishing it between two identical calls sends the second
   item to a different record. */
static void add_item_resolves_the_record_on_every_call(void)
{
    stage();
    empty_inventory(0);
    empty_inventory(1);

    CHECK_EQ(fdps_unit_add_item(0, 0x71), 1);

    data_fdps_map_unit_array_ptr = unit_slot(1);
    CHECK_EQ(fdps_unit_add_item(0, 0x72), 1);

    data_fdps_map_unit_array_ptr = unit_block;
    CHECK_EQ(fdps_unit_get_item_id(0, 0), 0x71);
    CHECK_EQ(fdps_unit_get_item_id(1, 0), 0x72);
    CHECK_EQ(fdps_unit_item_count(0), 1);
    CHECK_EQ(fdps_unit_item_count(1), 1);
}

/* The pairing the callers rely on: a full inventory refuses, one removal makes
   room, and the item then lands in the entry the removal marked empty -- the
   last one, since fdps_unit_remove_item packs the survivors down.  The eight
   entries are occupied again afterwards, which is the count the shop and the
   chapter events branch on. */
static void add_item_takes_the_slot_a_removal_freed(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_CARRIED, 0x30 + slot_index);
    }

    CHECK_EQ(fdps_unit_add_item(0, 0x99), -1);

    fdps_unit_remove_item(0, 2);
    CHECK_EQ(fdps_unit_item_count(0), INVENTORY_ENTRY_COUNT - 1);

    CHECK_EQ(fdps_unit_add_item(0, 0x99), 1);
    CHECK_EQ(fdps_unit_get_item_id(0, 7), 0x99);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 14], 0);
    CHECK_EQ(fdps_unit_item_count(0), INVENTORY_ENTRY_COUNT);
}

/* fdps_unit_can_equip_item at 00025fe0.  The strides and offsets the body
   addresses through: 6 for the class equipment record, IMUL EAX,dword ptr
   [EBP+0x14],0x6 in fdps_get_class_equip_record, and +0x20 for the class code
   byte, MOV AL,byte ptr [EAX+0x20] at 00025ffe. */
static void can_equip_layouts_match_the_class_table_stride(void)
{
    CHECK_EQ((int) sizeof(struct fdps_class_equip_record), CLASS_EQUIP_STRIDE);
    CHECK_EQ((int) sizeof(class_layout_probe.allowed_item_type),
             CLASS_EQUIP_TYPE_COUNT);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, clazz), OFF_CLAZZ);
}

/* MOV EAX,[EBP-0x10] / ADD EAX,[EBP-0xc] / MOV AL,byte ptr [EAX]: the six
   positions are the six consecutive bytes of the record, so a match in any one
   of them answers 1.  Every other position is filled with 0xfe, a code no item
   type takes, so only the position under test can be what answered. */
static void can_equip_matches_at_every_one_of_the_six_positions(void)
{
    int position;

    for (position = 0; position < CLASS_EQUIP_TYPE_COUNT; position++) {
        stage();
        set_class_code(0, 3);
        fill_class_equip(3, 0xfe);
        set_class_equip(3, position, 0x0a);
        set_item_type(7, 0x0a);
        CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);
    }
}

/* Running the loop out stores 0 -- MOV dword ptr [EBP-0x4],0x0 at 0002605f --
   and a match stores the literal 1.  The two answers differ by the comparison
   alone: the same six positions and the same item, with one position changed
   to the item's type, flip the answer. */
static void can_equip_answers_zero_when_no_position_matches(void)
{
    stage();
    set_class_code(0, 3);
    set_class_equip(3, 0, 0x01);
    set_class_equip(3, 1, 0x02);
    set_class_equip(3, 2, 0x03);
    set_class_equip(3, 3, 0x04);
    set_class_equip(3, 4, 0x05);
    set_class_equip(3, 5, 0x06);
    set_item_type(7, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 0);

    set_class_equip(3, 4, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);
}

/* AND EAX,0xff / PUSH EAX at 00026001: the class code is pushed exactly as it
   was read, with no INC in between, so class code 5 is record 5 of PROEQU.DAT
   and not record 6.  Staging the match in record 6 alone -- the record the
   PROMAP.DAT bias would have reached -- answers 0. */
static void can_equip_uses_the_class_code_without_the_promap_bias(void)
{
    stage();
    set_class_code(0, 5);
    fill_class_equip(5, 0xfe);
    fill_class_equip(6, 0xfe);
    set_class_equip(6, 0, 0x0a);
    set_item_type(7, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 0);

    set_class_equip(5, 2, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);
}

/* MOV AL,byte ptr [EAX+0x20] / AND EAX,0xff, so a class code of 0xff reaches
   record 255 and not the record in front of the base, which is where a
   sign-extended read would land.  The staged answers are the reverse of a
   signed read's. */
static void can_equip_widens_the_class_code_without_sign(void)
{
    stage();
    set_class_code(0, 0xff);
    fill_class_equip(255, 0xfe);
    fill_class_equip(-1, 0xfe);
    set_class_equip(-1, 0, 0x0a);
    set_item_type(7, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 0);

    set_class_equip(255, 5, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);
}

/* The unit index reaches fdps_get_unit_record and the class code comes out of
   the record it named: two units of different classes and one item answer
   differently. */
static void can_equip_takes_the_class_from_the_unit_the_index_names(void)
{
    stage();
    set_class_code(0, 1);
    set_class_code(2, 2);
    fill_class_equip(1, 0xfe);
    fill_class_equip(2, 0xfe);
    set_class_equip(2, 0, 0x0a);
    set_item_type(7, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 0);
    CHECK_EQ(fdps_unit_can_equip_item(2, 7), 1);
}

/* XOR EAX,EAX / MOV EDX,[EBP-0x14] / MOV AL,byte ptr [EDX] at 00026021: the
   item's type is byte +0x00 of its record and no other byte of it is read.  An
   item record whose every OTHER byte holds the allowed type still answers 0. */
static void can_equip_reads_only_byte_zero_of_the_item_record(void)
{
    int i;

    stage();
    set_class_code(0, 3);
    fill_class_equip(3, 0xfe);
    set_class_equip(3, 1, 0x0a);
    for (i = 0; i < ITEM_RECORD_STRIDE; i++) {
        item_block[(7 + 1) * ITEM_RECORD_STRIDE + i] = 0x0a;
    }
    set_item_type(7, 0x20);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 0);

    set_item_type(7, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);
}

/* CMP dword ptr [EBP-0xc],0x6 / JL: six positions and no seventh.  The byte
   immediately past the record -- position 0 of the next class -- carries the
   item's type and is not seen, while the same value in position 5 is. */
static void can_equip_scans_six_positions_and_no_more(void)
{
    stage();
    set_class_code(0, 4);
    fill_class_equip(4, 0xfe);
    fill_class_equip(5, 0xfe);
    set_class_equip(5, 0, 0x0a);
    set_item_type(7, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 0);

    set_class_equip(4, 5, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);
}

/* There is no test against 0xff and none against 0 anywhere in the loop: the
   only compare in the body is CMP EAX,[EBP-0x8] at 0002604f.  A record whose
   unused middle positions hold 0xff is still scanned to the end, which a scan
   that treated 0xff as a terminator would not do, and a position holding 0x00
   is compared like any other, so a blank ITEM.DAT record -- type 0 -- matches
   it. */
static void can_equip_has_no_sentinel_test(void)
{
    stage();
    set_class_code(0, 2);
    set_class_equip(2, 0, 0x01);
    set_class_equip(2, 1, 0xff);
    set_class_equip(2, 2, 0xff);
    set_class_equip(2, 3, 0xff);
    set_class_equip(2, 4, 0xff);
    set_class_equip(2, 5, 0x0a);
    set_item_type(7, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);

    stage();
    set_class_code(0, 2);
    fill_class_equip(2, 0xff);
    set_class_equip(2, 3, 0x00);
    set_item_type(7, 0x00);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);

    stage();
    set_class_code(0, 2);
    fill_class_equip(2, 0xff);
    set_item_type(7, 0x00);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 0);
}

/* MOV EAX,dword ptr [EBP+0x18] / PUSH EAX at 00026012: the item id is handed
   to fdps_get_item_record as the full signed dword it arrived as, with no mask
   and no bound, so -1 reaches the record in front of the table. */
static void can_equip_hands_the_item_id_to_the_table_unchanged(void)
{
    stage();
    set_class_code(0, 3);
    fill_class_equip(3, 0xfe);
    set_class_equip(3, 0, 0x16);
    set_item_type(-1, 0x16);
    CHECK_EQ(fdps_unit_can_equip_item(0, -1), 1);
}

/* Nothing is written: no byte of the unit block, the item table or the class
   table differs afterwards, for a call that matches and for one that runs the
   loop out. */
static void can_equip_writes_nothing(void)
{
    static unsigned char unit_before[sizeof(unit_block)];
    static unsigned char item_before[sizeof(item_block)];
    static unsigned char class_before[sizeof(class_block)];
    int i;
    int unit_diffs;
    int item_diffs;
    int class_diffs;

    stage();
    set_class_code(0, 3);
    fill_class_equip(3, 0xfe);
    set_class_equip(3, 2, 0x0a);
    set_item_type(7, 0x0a);
    set_item_type(8, 0x20);
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_before[i] = unit_block[i];
    }
    for (i = 0; i < (int) sizeof(item_block); i++) {
        item_before[i] = item_block[i];
    }
    for (i = 0; i < (int) sizeof(class_block); i++) {
        class_before[i] = class_block[i];
    }

    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);
    CHECK_EQ(fdps_unit_can_equip_item(0, 8), 0);

    unit_diffs = 0;
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        if (unit_block[i] != unit_before[i]) {
            unit_diffs++;
        }
    }
    item_diffs = 0;
    for (i = 0; i < (int) sizeof(item_block); i++) {
        if (item_block[i] != item_before[i]) {
            item_diffs++;
        }
    }
    class_diffs = 0;
    for (i = 0; i < (int) sizeof(class_block); i++) {
        if (class_block[i] != class_before[i]) {
            class_diffs++;
        }
    }
    CHECK_EQ(unit_diffs, 0);
    CHECK_EQ(item_diffs, 0);
    CHECK_EQ(class_diffs, 0);
}

/* All three bases are re-read on every call, because each of the three
   accessors reads its global each time: republishing any one of them between
   two otherwise identical calls changes the answer. */
static void can_equip_resolves_its_records_on_every_call(void)
{
    stage();
    set_class_code(0, 2);
    set_class_code(1, 3);
    fill_class_equip(2, 0xfe);
    fill_class_equip(3, 0xfe);
    set_class_equip(3, 0, 0x0a);
    set_item_type(7, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 0);
    data_fdps_map_unit_array_ptr = unit_slot(1);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);

    stage();
    set_class_code(0, 2);
    fill_class_equip(2, 0xfe);
    fill_class_equip(3, 0xfe);
    set_class_equip(3, 0, 0x0a);
    set_item_type(7, 0x0a);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 0);
    data_fdps_class_equip_table_ptr = class_block + CLASS_EQUIP_STRIDE * 2;
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);

    stage();
    set_class_code(0, 2);
    fill_class_equip(2, 0xfe);
    set_class_equip(2, 4, 0x0a);
    set_item_type(7, 0x0a);
    set_item_type(8, 0x20);
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 1);
    data_fdps_item_effect_table_ptr = item_block + ITEM_RECORD_STRIDE * 2;
    CHECK_EQ(fdps_unit_can_equip_item(0, 7), 0);
}

/* fdps_unit_equip_slot at 00026070.  MOV EAX,[EBP+0x18] / ADD EAX,EAX / ADD
   EAX,[EBP-0x1c] / MOV byte ptr [EAX+0xa],0x40 at 00026161: the flag byte of
   the named entry becomes exactly 0x40, unconditionally and as a store of the
   literal rather than an OR, so an entry marked empty comes out equipped and
   occupied.  The id byte beside it is not written. */
static void equip_slot_stores_0x40_over_the_slots_flag_byte(void)
{
    stage();
    set_item_type(10, 0x01);
    set_entry(0, 3, FLAG_EMPTY, 10);

    fdps_unit_equip_slot(0, 3);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 6], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 7], 10);
    CHECK_EQ(fdps_unit_item_count(0), INVENTORY_ENTRY_COUNT);

    stage();
    set_item_type(10, 0x01);
    set_entry(0, 3, 0x81, 10);

    fdps_unit_equip_slot(0, 3);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 6], 0x40);
}

/* The rebuild note.  The category test is the split at type 0x15 and not
   equality of the type byte: equipping a 刀, type 0x02, takes off an already
   worn 劍, type 0x01.  A body that compared the two type bytes for equality
   would leave entry 0 equipped. */
static void equip_slot_unequips_a_weapon_of_a_different_type(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x02);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    set_entry(0, 4, FLAG_CARRIED, 2);

    fdps_unit_equip_slot(0, 4);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 1], 1);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 8], FLAG_EQUIPPED);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 4);
}

/* The same on the armour side: type 0x16 and type 0x20 are both above 0x15, so
   equipping one takes the other off whatever their sub-kinds. */
static void equip_slot_unequips_armor_of_a_different_sub_kind(void)
{
    stage();
    set_item_type(1, 0x16);
    set_item_type(2, 0x20);
    set_entry(0, 2, FLAG_EQUIPPED, 1);
    set_entry(0, 6, FLAG_CARRIED, 2);

    fdps_unit_equip_slot(0, 6);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 4], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 12], FLAG_EQUIPPED);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), 6);
}

/* The two sides of the split do not disturb each other: equipping a weapon
   leaves the worn armour equipped and equipping armour leaves the worn weapon
   equipped, which is what lets a unit wear one of each. */
static void equip_slot_leaves_the_other_category_alone(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x02);
    set_item_type(3, 0x16);
    set_item_type(4, 0x20);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    set_entry(0, 1, FLAG_EQUIPPED, 3);
    set_entry(0, 2, FLAG_CARRIED, 2);
    set_entry(0, 3, FLAG_CARRIED, 4);

    fdps_unit_equip_slot(0, 2);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 4], FLAG_EQUIPPED);

    fdps_unit_equip_slot(0, 3);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 4], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 6], FLAG_EQUIPPED);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 2);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 1), 3);
}

/* CMP dword ptr [EBP-0x4],0x15 with JG and JLE: the boundary is 0x15 on the
   weapon side and 0x16 on the other.  Equipping type 0x15 takes off a worn
   0x01 and leaves a worn 0x16 on; equipping type 0x16 does the reverse. */
static void equip_slot_splits_the_categories_at_type_0x15(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x16);
    set_item_type(3, 0x15);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    set_entry(0, 1, FLAG_EQUIPPED, 2);
    set_entry(0, 7, FLAG_CARRIED, 3);

    fdps_unit_equip_slot(0, 7);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], FLAG_EQUIPPED);

    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x27);
    set_item_type(3, 0x16);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    set_entry(0, 1, FLAG_EQUIPPED, 2);
    set_entry(0, 7, FLAG_CARRIED, 3);

    fdps_unit_equip_slot(0, 7);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], 0);
}

/* The split is at 0x15 and NOT at the top of the armour span: an item of type
   0x2a -- a consumable, above the 0x27 that fdps_unit_find_equipped_slot's
   armour search stops at -- is on the armour side of this test, so equipping
   one takes the worn armour off and leaves the worn weapon on. */
static void equip_slot_counts_everything_above_0x15_as_armor(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x16);
    set_item_type(3, 0x2a);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    set_entry(0, 1, FLAG_EQUIPPED, 2);
    set_entry(0, 5, FLAG_CARRIED, 3);

    fdps_unit_equip_slot(0, 5);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 10], FLAG_EQUIPPED);
}

/* AND AL,0x40 / AND EAX,0xff / TEST EAX,EAX / JZ at 00026108: only entries
   carrying the equipped bit are considered, and the test is a mask.  Entries
   holding the same kind of item with flag bytes 0x00, 0x80, 0x20 and 0x01 keep
   their bytes exactly; the entry carrying 0xc1 has the bit and is zeroed. */
static void equip_slot_only_considers_entries_with_bit_0x40(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x02);
    set_entry(0, 0, FLAG_CARRIED, 1);
    set_entry(0, 1, FLAG_EMPTY, 1);
    set_entry(0, 2, 0x20, 1);
    set_entry(0, 3, 0x01, 1);
    set_entry(0, 4, 0xc1, 1);
    set_entry(0, 6, FLAG_CARRIED, 2);

    fdps_unit_equip_slot(0, 6);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], FLAG_CARRIED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], FLAG_EMPTY);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 4], 0x20);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 6], 0x01);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 8], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 12], FLAG_EQUIPPED);
}

/* CMP dword ptr [EBP-0xc],0x0 / JZ at 00026139: an equipped entry whose item
   record reads back type 0 -- what ITEM.DAT's all-zero tail records give -- is
   left alone, even though 0 is on the weapon side of the 0x15 split and a body
   without that test would zero it.  A real weapon in another entry is still
   taken off, so the type-0 entry is skipped and not the loop abandoned. */
static void equip_slot_leaves_a_blank_item_record_equipped(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x02);
    set_entry(0, 0, FLAG_EQUIPPED, 0xf0);
    set_entry(0, 1, FLAG_EQUIPPED, 1);
    set_entry(0, 5, FLAG_CARRIED, 2);

    fdps_unit_equip_slot(0, 5);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 10], FLAG_EQUIPPED);
}

/* The type-0 test guards the WORN item only; there is no such test on the item
   being equipped.  Equipping an entry whose own item record is blank gives an
   equip type of 0, which is <= 0x15, so the worn weapon still comes off and the
   worn armour stays on. */
static void equip_slot_does_not_test_the_equipped_items_own_type(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x16);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    set_entry(0, 1, FLAG_EQUIPPED, 2);
    set_entry(0, 4, FLAG_CARRIED, 0xf0);

    fdps_unit_equip_slot(0, 4);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 8], FLAG_EQUIPPED);
}

/* The JZ at 00026116 and the fall-through at 00026157 both go to the increment
   at 0002615f, so the loop has no early exit: a unit somehow wearing two items
   of the same category loses BOTH.  A body written as "find the worn one and
   unequip it" would leave the higher entry equipped. */
static void equip_slot_unequips_every_matching_entry(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x03);
    set_item_type(3, 0x02);
    set_entry(0, 1, FLAG_EQUIPPED, 1);
    set_entry(0, 3, FLAG_EQUIPPED, 2);
    set_entry(0, 6, FLAG_CARRIED, 3);

    fdps_unit_equip_slot(0, 6);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 6], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 12], FLAG_EQUIPPED);
}

/* MOV byte ptr [EAX],0x0 at 0002615c writes a plain zero over the WHOLE flag
   byte rather than clearing bit 0x40, so an entry staged 0x41 comes out 0x00
   and not 0x01, and its empty bit stays clear -- the item is still carried and
   still counted.  The id byte beside it is not written. */
static void equip_slot_zeroes_the_whole_flag_byte_it_unequips(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x02);
    set_entry(0, 0, 0x41, 1);
    set_entry(0, 2, FLAG_CARRIED, 2);

    fdps_unit_equip_slot(0, 2);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 1], 1);
    CHECK_EQ(fdps_unit_item_count(0), INVENTORY_ENTRY_COUNT);
    CHECK_EQ(fdps_unit_get_item_id(0, 0), 1);
}

/* The final store happens after the loop and is unconditional, so the entry
   being equipped is put back even when the loop has just zeroed it: equipping
   an entry that is already equipped with an item of its own category leaves it
   at 0x40 rather than at 0. */
static void equip_slot_re_equips_the_entry_the_loop_zeroed(void)
{
    stage();
    set_item_type(1, 0x01);
    set_entry(0, 2, FLAG_EQUIPPED, 1);

    fdps_unit_equip_slot(0, 2);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 4], FLAG_EQUIPPED);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 2);
}

/* CMP dword ptr [EBP-0x10],0x8 / JL at 000260e7: eight entries and no more.
   An equipped pair of the same category written two entries past the end of the
   field -- record offsets 0x1a and 0x1b, inside the record -- is not seen and
   keeps its bytes. */
static void equip_slot_scans_eight_entries_and_no_more(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x02);
    set_entry(0, 7, FLAG_EQUIPPED, 1);
    set_entry(0, 8, FLAG_EQUIPPED, 1);
    set_entry(0, 0, FLAG_CARRIED, 2);

    fdps_unit_equip_slot(0, 0);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 14], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 16], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], FLAG_EQUIPPED);
}

/* The only stores in the body are the loop's flag byte and the final flag
   byte: with the whole block filled with a pattern and unit 1's eight entries
   staged, exactly two bytes differ afterwards -- the flag byte of the entry
   that was taken off and the flag byte of the entry that was put on.  No id
   byte, no other field of the record and no neighbouring record moves. */
static void equip_slot_writes_only_two_flag_bytes(void)
{
    static unsigned char block_before[sizeof(unit_block)];
    int i;
    int diffs;
    int unequipped_flag_offset;
    int equipped_flag_offset;
    int slot_index;

    stage();
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_block[i] = (unsigned char) (i + 1);
    }
    set_item_type(1, 0x01);
    set_item_type(2, 0x02);
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(1, slot_index, FLAG_CARRIED, 1);
    }
    set_entry(1, 0, FLAG_EQUIPPED, 1);
    set_entry(1, 5, FLAG_CARRIED, 2);
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        block_before[i] = unit_block[i];
    }

    fdps_unit_equip_slot(1, 5);

    unequipped_flag_offset = UNIT_RECORD_STRIDE + OFF_INVENTORY + 0;
    equipped_flag_offset = UNIT_RECORD_STRIDE + OFF_INVENTORY + 10;
    CHECK_EQ(unit_block[unequipped_flag_offset], 0);
    CHECK_EQ(unit_block[equipped_flag_offset], FLAG_EQUIPPED);

    diffs = 0;
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        if (i == unequipped_flag_offset || i == equipped_flag_offset) {
            continue;
        }
        if (unit_block[i] != block_before[i]) {
            diffs++;
        }
    }
    CHECK_EQ(diffs, 0);
}

/* MOV AL,byte ptr [EDX+0xb] at 000260bb: the type of the item being equipped
   comes from the entry's SECOND byte, the id, and never from its flag byte.
   The entry under test carries flag byte 0x30 -- which is the id of a weapon
   here -- and id byte 0x40, the id of a piece of armour, so a body that
   classified by the flag byte would take the worn weapon off instead of the
   worn armour.  0x30 has no 0x40 bit, so the entry itself is passed over by
   the loop. */
static void equip_slot_classifies_by_the_id_byte_not_the_flag_byte(void)
{
    stage();
    set_item_type(0x30, 0x01);
    set_item_type(0x40, 0x16);
    set_item_type(1, 0x01);
    set_item_type(2, 0x16);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    set_entry(0, 1, FLAG_EQUIPPED, 2);
    set_entry(0, 2, 0x30, 0x40);

    fdps_unit_equip_slot(0, 2);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 4], FLAG_EQUIPPED);
}

/* The id read is XOR EAX,EAX / MOV AL,byte ptr [EDX+0xb], so an entry holding
   id 0xff asks for record 255 and not for the record in front of the table.
   Record 255 is staged as armour and record -1 as a weapon, so a signed read
   would take the worn weapon off; the staged answer is the reverse. */
static void equip_slot_widens_the_id_byte_without_sign(void)
{
    stage();
    set_item_type(0xff, 0x16);
    set_item_type(-1, 0x01);
    set_item_type(1, 0x01);
    set_item_type(2, 0x16);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    set_entry(0, 1, FLAG_EQUIPPED, 2);
    set_entry(0, 3, FLAG_CARRIED, 0xff);

    fdps_unit_equip_slot(0, 3);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], 0);
}

/* Nothing bounds the slot: it reaches the address as slot * 2 with no test in
   between, both for the id read at inventory_slots[slot * 2 + 1] and for the
   store at inventory_slots[slot * 2].  Slot 8 therefore classifies by record
   offset 0x1b and writes 0x40 to record offset 0x1a, both past the inventory
   field, and the loop still runs over the eight real entries.  A guard here
   would leave 0x1a alone and take nothing off. */
static void equip_slot_does_not_range_check_the_slot(void)
{
    unsigned char *record;

    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x16);
    set_item_type(3, 0x20);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    set_entry(0, 1, FLAG_EQUIPPED, 2);
    record = unit_slot(0);
    record[OFF_INVENTORY + 16] = 0;
    record[OFF_INVENTORY + 17] = 3;

    fdps_unit_equip_slot(0, 8);

    CHECK_EQ(record[OFF_INVENTORY + 16], FLAG_EQUIPPED);
    CHECK_EQ(record[OFF_INVENTORY + 0], FLAG_EQUIPPED);
    CHECK_EQ(record[OFF_INVENTORY + 2], 0);
}

/* MOV EAX,[EBP+0x14] / PUSH EAX / CALL fdps_get_unit_record: the first
   argument picks the record that is walked and written, so the neighbouring
   units keep their equipped entries.  The multiply inside fdps_get_unit_record
   is signed, so index -1 reaches the record in front of the published base. */
static void equip_slot_edits_the_record_the_index_names(void)
{
    int unit_index;

    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x02);
    for (unit_index = 0; unit_index < STAGE_UNITS; unit_index++) {
        set_entry(unit_index, 0, FLAG_EQUIPPED, 1);
        set_entry(unit_index, 4, FLAG_CARRIED, 2);
    }

    fdps_unit_equip_slot(2, 4);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(1)[OFF_INVENTORY + 0], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(2)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(unit_slot(2)[OFF_INVENTORY + 8], FLAG_EQUIPPED);
    CHECK_EQ(unit_slot(3)[OFF_INVENTORY + 0], FLAG_EQUIPPED);

    data_fdps_map_unit_array_ptr = unit_slot(1);
    fdps_unit_equip_slot(-1, 4);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 8], FLAG_EQUIPPED);
}

/* Both resolutions go through fdps_get_unit_record, which reads the base global
   every time, so republishing it between two otherwise identical calls sends
   the second equip to a different record. */
static void equip_slot_resolves_the_record_on_every_call(void)
{
    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x02);
    set_entry(0, 0, FLAG_EQUIPPED, 1);
    set_entry(0, 3, FLAG_CARRIED, 2);
    set_entry(1, 0, FLAG_EQUIPPED, 1);
    set_entry(1, 3, FLAG_CARRIED, 2);

    fdps_unit_equip_slot(0, 3);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(unit_slot(1)[OFF_INVENTORY + 0], FLAG_EQUIPPED);

    data_fdps_map_unit_array_ptr = unit_slot(1);
    fdps_unit_equip_slot(0, 3);
    CHECK_EQ(unit_slot(1)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(unit_slot(1)[OFF_INVENTORY + 6], FLAG_EQUIPPED);
}

/* The pairing fdps_shop_buy_loop relies on at 00033f5b: it adds the purchase
   with fdps_unit_add_item and then equips
   fdps_unit_item_count(unit_index) - 1, which is the entry the add just
   filled.  The bought weapon ends up equipped and the one the unit came in
   with does not. */
static void equip_slot_equips_the_entry_an_add_just_filled(void)
{
    int slot_index;

    stage();
    set_item_type(1, 0x01);
    set_item_type(2, 0x02);
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_EMPTY, 0);
    }
    set_entry(0, 0, FLAG_EQUIPPED, 1);

    CHECK_EQ(fdps_unit_add_item(0, 2), 1);
    CHECK_EQ(fdps_unit_item_count(0), 2);

    fdps_unit_equip_slot(0, fdps_unit_item_count(0) - 1);

    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 0], 0);
    CHECK_EQ(unit_slot(0)[OFF_INVENTORY + 2], FLAG_EQUIPPED);
    CHECK_EQ(fdps_unit_find_equipped_slot(0, 0), 1);
    CHECK_EQ(fdps_unit_get_item_id(0, 1), 2);
}

/* fdps_unit_find_item_slot at 00034520.  CMP dword ptr [EBP-0xc],0x0 / JNZ at
   0003453b: a count of zero leaves through the guard and never reaches the
   loop.  The fixture makes that visible rather than merely true -- entry 0 is
   marked empty but still carries the id being looked for, so a body that
   entered the loop anyway, or that swept all eight entries, would answer 0. */
static void find_item_slot_answers_minus_one_for_an_empty_inventory(void)
{
    stage();
    empty_inventory(0);
    set_entry(0, 0, FLAG_EMPTY, 0x5a);
    CHECK_EQ(fdps_unit_item_count(0), 0);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x5a), -1);
}

/* The plain case.  A packed inventory of four items answers with each id's own
   entry index, and an id nobody holds answers -1 through the store at
   00034582. */
static void find_item_slot_finds_the_id_of_a_packed_entry(void)
{
    stage();
    empty_inventory(0);
    set_entry(0, 0, FLAG_CARRIED, 0x10);
    set_entry(0, 1, FLAG_EQUIPPED, 0x11);
    set_entry(0, 2, FLAG_CARRIED, 0x12);
    set_entry(0, 3, FLAG_CARRIED, 0x13);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x10), 0);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x11), 1);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x12), 2);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x13), 3);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x14), -1);
}

/* MOV [EBP-0x4],EAX / JMP to the epilogue at 00034578: the scan returns on the
   first match and does not run on, so a unit holding two of the same item
   answers with the lower entry. */
static void find_item_slot_answers_the_lowest_matching_entry(void)
{
    stage();
    empty_inventory(0);
    set_entry(0, 0, FLAG_CARRIED, 0x20);
    set_entry(0, 1, FLAG_CARRIED, 0x33);
    set_entry(0, 2, FLAG_CARRIED, 0x33);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x33), 1);
}

/* The rebuild note, first direction.  CMP EAX,[EBP-0xc] / JL at 00034554 bounds
   the scan by fdps_unit_item_count and not by the eight entries, so the stale
   id bytes of the empty entries above the count are never compared.  The
   fixture puts 0x99 in every one of them: the original answers -1, and a body
   written as `for (slot = 0; slot < 8; slot++)` would answer 3. */
static void find_item_slot_stops_at_the_occupied_count(void)
{
    int slot_index;

    stage();
    empty_inventory(0);
    set_entry(0, 0, FLAG_CARRIED, 0x10);
    set_entry(0, 1, FLAG_CARRIED, 0x11);
    set_entry(0, 2, FLAG_CARRIED, 0x12);
    for (slot_index = 3; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_EMPTY, 0x99);
    }

    CHECK_EQ(fdps_unit_item_count(0), 3);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x99), -1);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x12), 2);
}

/* The rebuild note, second direction.  The bound is a COUNT and the entries it
   walks are the first count of them, so an inventory with a hole at the front
   is walked wrongly on purpose: entry 0 is empty and entry 1 holds the item,
   the count is 1, and the single entry the scan reads is the empty one.
   fdps_unit_get_item_id does not consult the flag byte, so the stale id of that
   empty entry is what gets compared -- the original answers 0 for it, which is
   an entry holding nothing, and -1 for the item the unit really has. */
static void find_item_slot_reads_an_empty_entry_inside_the_bound(void)
{
    int slot_index;

    stage();
    empty_inventory(0);
    set_entry(0, 0, FLAG_EMPTY, 0x55);
    set_entry(0, 1, FLAG_CARRIED, 0x77);
    for (slot_index = 2; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_EMPTY, 0);
    }

    CHECK_EQ(fdps_unit_item_count(0), 1);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x55), 0);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x77), -1);
}

/* The other end of the bound: with all eight entries occupied the count is 8
   and the last entry is inside the scan.  stage() leaves every flag byte zero,
   which is exactly that case. */
static void find_item_slot_reaches_the_last_entry_of_a_full_inventory(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_CARRIED, 0x30 + slot_index);
    }

    CHECK_EQ(fdps_unit_item_count(0), INVENTORY_ENTRY_COUNT);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x37), 7);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x38), -1);
}

/* CMP EAX,dword ptr [EBP+0x18] is a full 32-bit compare against the argument as
   it was pushed, and fdps_unit_get_item_id answers 0..255, so an item_id
   outside that range matches nothing: 0x100 does not find the entry holding
   0x00 and -1 does not find the one holding 0xff.  A body that compared only
   the low byte would answer 0 for 0x100, and one that sign-extended the id byte
   would answer 1 for -1. */
static void find_item_slot_compares_the_id_at_full_width(void)
{
    int slot_index;

    stage();
    for (slot_index = 0; slot_index < INVENTORY_ENTRY_COUNT; slot_index++) {
        set_entry(0, slot_index, FLAG_CARRIED, 0x40 + slot_index);
    }
    set_entry(0, 0, FLAG_CARRIED, 0x00);
    set_entry(0, 1, FLAG_CARRIED, 0xff);

    CHECK_EQ(fdps_unit_find_item_slot(0, 0x00), 0);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x100), -1);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0xff), 1);
    CHECK_EQ(fdps_unit_find_item_slot(0, -1), -1);
}

/* MOV EAX,[EBP+0x14] / PUSH EAX ahead of both calls: the unit index is what
   both the bound and every id read are taken from, so the same search answers
   differently per record.  The multiply inside fdps_get_unit_record is signed,
   so index -1 reaches the record in front of the published base. */
static void find_item_slot_takes_its_unit_from_the_index(void)
{
    stage();
    set_entry(0, 3, FLAG_CARRIED, 0x61);
    set_entry(1, 5, FLAG_CARRIED, 0x61);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x61), 3);
    CHECK_EQ(fdps_unit_find_item_slot(1, 0x61), 5);
    CHECK_EQ(fdps_unit_find_item_slot(2, 0x61), -1);

    data_fdps_map_unit_array_ptr = unit_slot(1);
    CHECK_EQ(fdps_unit_find_item_slot(-1, 0x61), 3);
}

/* Neither callee writes and this body has no store of its own, so the unit
   block is byte-for-byte the same after a search that matches, one that runs
   the loop out and one that leaves through the empty-inventory guard. */
static void find_item_slot_writes_nothing(void)
{
    static unsigned char block_before[sizeof(unit_block)];
    int i;
    int diffs;

    stage();
    set_entry(0, 2, FLAG_CARRIED, 0x62);
    empty_inventory(1);
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        block_before[i] = unit_block[i];
    }

    CHECK_EQ(fdps_unit_find_item_slot(0, 0x62), 2);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x63), -1);
    CHECK_EQ(fdps_unit_find_item_slot(1, 0x62), -1);

    diffs = 0;
    for (i = 0; i < (int) sizeof(unit_block); i++) {
        if (unit_block[i] != block_before[i]) {
            diffs++;
        }
    }
    CHECK_EQ(diffs, 0);
}

/* Nothing is cached across the two calls this body makes: both accessors go
   through fdps_get_unit_record themselves, so republishing the base between two
   identical searches changes the answer. */
static void find_item_slot_resolves_the_record_on_every_call(void)
{
    stage();
    set_entry(0, 2, FLAG_CARRIED, 0x66);
    set_entry(1, 6, FLAG_CARRIED, 0x66);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x66), 2);

    data_fdps_map_unit_array_ptr = unit_slot(1);
    CHECK_EQ(fdps_unit_find_item_slot(0, 0x66), 6);
}

/* fdps_unit_item_select_loop, 00025b20.
 *
 * ONLY THE PASS THAT ENDS THE LOOP CAN BE OBSERVED, AND THAT IS STRUCTURAL.
 * The loop's exit is fdps_unit_status_window_wait_input, whose reader
 * fdps_read_scancode_auto_repeat reports the latched byte only when it differs
 * from data_fdps_input_key_repeat_prev_scancode; the reader records the code it
 * reported, and nothing inside a call can change the latch again, because only
 * the INT 09h handler writes it and no test installs one.  A second wait in the
 * same call therefore polls 0xff for ever, and the frame that would end that
 * spin is never drawn either, because a frame needs the timer tick to move and
 * the tick is written by an interrupt as well.  So a case can drive exactly one
 * make code into one call, and the branches that repaint and go round again --
 * the two cursor moves, the silent refusal of an unusable item, and every
 * unrecognised code -- cannot be reached and returned from at all.  What those
 * branches do with the occupied count is a playtest contract; the count itself
 * has no other reader, so it is not observable here either.
 *
 * Expected values come from the assembly at 00025b20: PUSH 0x95 / PUSH 0x97 /
 * PUSH 0x140 / ADD EAX,0x3b58 / PUSH 0x97 at 00025b82 for the backdrop blit,
 * PUSH 0x140 / ADD EAX,0x3b58 / PUSH dword ptr [EAX] at 00025bab for the list
 * redraw, XOR EAX,EAX / PUSH EAX at 00025bca for the idle flag, the six CMPs
 * against 0x48, 0x50, 0x1c, 0x39, 0x1 and 0x53, MOV AL,byte ptr [EDX+0xb] at
 * 00025c6c for the id byte and CMP byte ptr [EAX+0xd],0x0 at 00025c84 for the
 * usability test; and from the 0x11 row pitch and the (0, 8) bar origin
 * fdps_draw_unit_inventory uses (statunit.h).  None of them is read off the
 * emitted C.
 *
 * The fixture keeps every inventory entry marked empty so that the list redraw
 * touches no sprite sheet but the selection bar's, which is fabricated three
 * pixels wide, one row tall and colour 0x70 -- a colour the backdrop pattern
 * never produces, so a bar pixel and a backdrop pixel can never be confused.
 */

/* The window image the list is composed into and the geometry of the list
   rectangle inside it. */
#define WINDOW_PITCH 0x140
#define WINDOW_ROWS 200
#define WINDOW_BYTES (WINDOW_PITCH * WINDOW_ROWS)
#define LIST_AT 0x3b58
#define LIST_W 0x97
#define LIST_H 0x95
#define LIST_BYTES (LIST_W * LIST_H)

/* The value the window starts at, which is what an untouched pixel reads back
   as.  It is below 0x80, and every backdrop pixel is at or above 0x80, so a
   pixel says on its own whether the blit reached it. */
#define WINDOW_FILL 0x5a

/* The selection bar fixture: a one-sprite .CEL whose header says three by one
   and whose single row is one fill op of colour 0x70.  A .CEL's sprite offset
   table starts at +0x0f (resource_info/cel.md) and command byte 0x02 is op 00
   with a run of (0x02 & 0x3f) + 1. */
#define CEL_TABLE_AT 0x0f
#define BAR_W 3
#define BAR_H 1
#define BAR_RUN 0x02
#define BAR_COLOR 0x70
#define BAR_STREAM_AT (CEL_TABLE_AT + 4)
#define BAR_SHEET_BYTES (BAR_STREAM_AT + BAR_H * 2)

/* How far apart the list's rows are and where the bar sits inside one:
   selected_slot * 0x11 + 8, column 0 (statunit.h). */
#define ROW_PITCH 0x11
#define BAR_ROW_BIAS 8

/* CMP byte ptr [EAX+0xd],0x0 at 00025c84: the use effect is byte +0x0d of the
   ITEM.DAT record, and any non-zero value makes the item usable. */
#define OFF_USE_EFFECT 0x0d

/* The make codes the dispatch names. */
#define KEY_ENTER 0x1c
#define KEY_SPACE 0x39
#define KEY_ESC 0x01
#define KEY_DELETE 0x53

/* A slot the list draws no bar for, which is what leaves the whole rectangle
   holding backdrop pixels alone. */
#define SLOT_NONE (-1)

static unsigned char window_image[WINDOW_BYTES];
static unsigned char list_backdrop[LIST_BYTES];
static unsigned char bar_sheet[BAR_SHEET_BYTES];

/* The backdrop's pixel at an offset.  Every value is at or above 0x80, so it is
   never WINDOW_FILL and never BAR_COLOR, and the low seven bits change with the
   offset, so a row copied at the wrong stride reads back as a different byte
   rather than matching anyway. */
static int backdrop_pixel(int at)
{
    return 0x80 | (at & 0x7f);
}

/* Publishes the tables, empties both staged inventories, repaints the window
   and the backdrop, builds the bar sheet, and latches one make code for the
   next poll to report.  Setting the previous code to 0x100 is what makes that
   poll see a change: the latch is a byte and can never hold that value. */
static void loop_stage(unsigned int scancode)
{
    int i;
    struct fdps_cel_header *bar_header;

    stage();
    for (i = 0; i < INVENTORY_ENTRY_COUNT; i++) {
        set_entry(0, i, FLAG_EMPTY, 0);
        set_entry(1, i, FLAG_EMPTY, 0);
    }

    for (i = 0; i < WINDOW_BYTES; i++) {
        window_image[i] = (unsigned char) WINDOW_FILL;
    }
    for (i = 0; i < LIST_BYTES; i++) {
        list_backdrop[i] = (unsigned char) backdrop_pixel(i);
    }

    for (i = 0; i < BAR_SHEET_BYTES; i++) {
        bar_sheet[i] = 0;
    }
    bar_header = (struct fdps_cel_header *) bar_sheet;
    bar_header->sprite_width = (short) BAR_W;
    bar_header->sprite_height = (short) BAR_H;
    bar_header->sprite_count = (short) 1;
    *(int *) (bar_sheet + CEL_TABLE_AT) = BAR_STREAM_AT;
    bar_sheet[BAR_STREAM_AT] = (unsigned char) BAR_RUN;
    bar_sheet[BAR_STREAM_AT + 1] = (unsigned char) BAR_COLOR;
    data_fdps_selection_bar_sheet_ptr = bar_sheet;

    data_fdps_input_last_scancode = (unsigned char) scancode;
    data_fdps_input_key_repeat_prev_scancode = 0x100;
}

/* The use effect byte of one ITEM.DAT record of the staged table. */
static void set_use_effect(int item_id, int use_effect)
{
    item_block[(item_id + 1) * ITEM_RECORD_STRIDE + OFF_USE_EFFECT] =
        (unsigned char) use_effect;
}

/* The window pixel at a position inside the list rectangle. */
static int list_pixel(int row, int col)
{
    return window_image[LIST_AT + row * WINDOW_PITCH + col];
}

/* Escape answers -1 and leaves the cursor where it was: MOV dword ptr
   [EBP-0x4],0xffffffff at 00025ca1, with nothing written through
   [EBP+0x24] on that path. */
static void escape_cancels_and_leaves_the_slot_alone(void)
{
    int selected_slot;

    loop_stage(KEY_ESC);
    selected_slot = 3;
    CHECK_EQ(fdps_unit_item_select_loop(0, 0, window_image, list_backdrop,
                                        &selected_slot), -1);
    CHECK_EQ(selected_slot, 3);
}

/* Delete reaches the same store, CMP dword ptr [EBP-0x20],0x53 / JNZ at
   00025c9b falling into the branch escape took. */
static void delete_cancels_the_same_way_escape_does(void)
{
    int selected_slot;

    loop_stage(KEY_DELETE);
    selected_slot = 6;
    CHECK_EQ(fdps_unit_item_select_loop(0, 0, window_image, list_backdrop,
                                        &selected_slot), -1);
    CHECK_EQ(selected_slot, 6);
}

/* With usable_only clear the confirm returns 1 before the item table is
   reached at all: CMP dword ptr [EBP+0x18],0x0 / JNZ at 00025c51 skips the
   record lookup entirely.  Every record of the staged table has use effect 0,
   so a body that consulted it anyway would refuse and never return. */
static void enter_confirms_without_consulting_the_item_table(void)
{
    int selected_slot;

    loop_stage(KEY_ENTER);
    set_entry(0, 4, FLAG_EMPTY, 0x33);
    selected_slot = 4;
    CHECK_EQ(fdps_unit_item_select_loop(0, 0, window_image, list_backdrop,
                                        &selected_slot), 1);
    CHECK_EQ(selected_slot, 4);
}

/* Space is the second half of the same test, CMP dword ptr [EBP-0x20],0x39 /
   JNZ at 00025c4b falling into the branch enter took. */
static void space_confirms_the_same_way_enter_does(void)
{
    int selected_slot;

    loop_stage(KEY_SPACE);
    selected_slot = 0;
    CHECK_EQ(fdps_unit_item_select_loop(0, 0, window_image, list_backdrop,
                                        &selected_slot), 1);
    CHECK_EQ(selected_slot, 0);
}

/* With usable_only set the confirm is allowed only by a non-zero use effect,
   and the id it looks up is the entry's id byte -- record + 0x0a + 2 * slot + 1
   -- with the entry's flag byte never consulted: the entry below is marked
   empty and is confirmed anyway.
   THIS CASE DISTINGUISHES BY NOT COMING BACK.  Every other record of the staged
   table has use effect 0, so a body that read the flag byte, a neighbouring
   entry or an unscaled slot would refuse; a refusal repaints and waits again,
   and no second wait in one call can return, so the failure shows up as this
   case hanging with the heartbeat frozen on its name rather than as a check
   that did not hold. */
static void usable_only_accepts_the_id_byte_of_the_selected_entry(void)
{
    int selected_slot;

    loop_stage(KEY_ENTER);
    set_entry(0, 3, FLAG_EMPTY, 0x22);
    set_use_effect(0x22, 0x0b);
    selected_slot = 3;
    CHECK_EQ(fdps_unit_item_select_loop(0, 1, window_image, list_backdrop,
                                        &selected_slot), 1);
    CHECK_EQ(selected_slot, 3);
}

/* The backdrop is laid over the list rectangle before the wait, at the window's
   0x140 pitch out of a source of its own 0x97 stride.  That the rectangle holds
   backdrop pixels at all on a pass that returned is what pins the order: the
   wait that ended this call drew nothing, so the paint must have come first. */
static void the_backdrop_is_laid_over_the_list_rectangle(void)
{
    int selected_slot;

    loop_stage(KEY_ENTER);
    selected_slot = SLOT_NONE;
    CHECK_EQ(fdps_unit_item_select_loop(0, 0, window_image, list_backdrop,
                                        &selected_slot), 1);

    CHECK_EQ(list_pixel(0, 0), backdrop_pixel(0));
    CHECK_EQ(list_pixel(0, LIST_W - 1), backdrop_pixel(LIST_W - 1));
    CHECK_EQ(list_pixel(1, 0), backdrop_pixel(LIST_W));
    CHECK_EQ(list_pixel(LIST_H - 1, LIST_W - 1),
             backdrop_pixel((LIST_H - 1) * LIST_W + LIST_W - 1));
}

/* Nothing outside the 0x97 by 0x95 rectangle is touched: the byte in front of
   the rectangle, the column past its width on the first and last rows, and the
   first row past its height all keep the window's fill. */
static void the_blit_stops_at_the_rectangles_edges(void)
{
    int selected_slot;

    loop_stage(KEY_ENTER);
    selected_slot = SLOT_NONE;
    CHECK_EQ(fdps_unit_item_select_loop(0, 0, window_image, list_backdrop,
                                        &selected_slot), 1);

    CHECK_EQ(window_image[LIST_AT - 1], WINDOW_FILL);
    CHECK_EQ(list_pixel(0, LIST_W), WINDOW_FILL);
    CHECK_EQ(list_pixel(LIST_H - 1, LIST_W), WINDOW_FILL);
    CHECK_EQ(list_pixel(LIST_H, 0), WINDOW_FILL);
}

/* The bar goes down after the backdrop and on the row *selected_slot names, at
   slot * 0x11 + 8 from the top of the rectangle and column 0.  The unit index
   is 1 and the slot is 5, so a body that handed fdps_draw_unit_inventory its
   arguments the other way round would put the bar on row 1 * 0x11 + 8; that row
   is checked and must still hold the backdrop. */
static void the_selection_bar_lands_on_the_row_the_slot_names(void)
{
    int selected_slot;
    int bar_row;
    int other_row;

    loop_stage(KEY_ENTER);
    selected_slot = 5;
    CHECK_EQ(fdps_unit_item_select_loop(1, 0, window_image, list_backdrop,
                                        &selected_slot), 1);

    bar_row = 5 * ROW_PITCH + BAR_ROW_BIAS;
    other_row = 1 * ROW_PITCH + BAR_ROW_BIAS;
    CHECK_EQ(list_pixel(bar_row, 0), BAR_COLOR);
    CHECK_EQ(list_pixel(bar_row, BAR_W - 1), BAR_COLOR);
    CHECK_EQ(list_pixel(bar_row, BAR_W),
             backdrop_pixel(bar_row * LIST_W + BAR_W));
    CHECK_EQ(list_pixel(other_row, 0), backdrop_pixel(other_row * LIST_W));
    CHECK_EQ(list_pixel(BAR_ROW_BIAS, 0),
             backdrop_pixel(BAR_ROW_BIAS * LIST_W));
}

/* A slot outside 0..7 draws no bar, which is the state the wrap leaves the
   cursor in when the unit is carrying nothing: the whole rectangle is still the
   backdrop after the paint. */
static void a_slot_outside_the_eight_rows_draws_no_bar(void)
{
    int selected_slot;
    int row;
    int strays;

    loop_stage(KEY_ENTER);
    selected_slot = SLOT_NONE;
    CHECK_EQ(fdps_unit_item_select_loop(0, 0, window_image, list_backdrop,
                                        &selected_slot), 1);

    strays = 0;
    for (row = 0; row < LIST_H; row++) {
        if (list_pixel(row, 0) != backdrop_pixel(row * LIST_W)) {
            strays++;
        }
    }
    CHECK_EQ(strays, 0);
    CHECK_EQ(selected_slot, SLOT_NONE);
}

/* ---- fdps_unit_item_select_window @ 000259c0 -----------------------------
 *
 * THE CALL IS RUN WHOLE, AND ALMOST NOTHING IT DRAWS SURVIVES IT.  The three
 * buffers the window is composed in are allocated, painted, animated and freed
 * inside the one call, and the last thing it does before returning is
 * fdps_close_status_window, which repaints all 64,000 bytes of the screen from
 * the picture behind the window.  So the opening slide-in, the status panel,
 * the item rows and the backdrop copy leave nothing behind that a case could
 * read: they are drawing side effects with no unit observable, deferred here
 * and covered as far as they can be in tests/statunit.c and tests/statwin.c.
 * The rebuild note the plate comment carries -- that the list rectangle has to
 * be copied out BEFORE the item rows are painted into it, or every repaint
 * inside the cursor loop leaves the old highlight showing -- is in that same
 * class: the backdrop is read only by the loop and is freed before the call
 * returns, so what these cases pin about it is that it was taken at all, by
 * the confirm coming back rather than the call faulting on a null buffer.
 *
 * WHAT IS LEFT TO ASSERT is the three things that outlive the call: the answer
 * itself, the caller's slot, and the two globals the call writes on its way
 * through -- data_fdps_village_status_window_unit_idx, which
 * fdps_draw_unit_status_panel publishes from the unit index it was handed
 * (statunit.h), and the heap, which has to come back to the state it was in.
 *
 * ONE MAKE CODE PER CALL, for the reason set out above the loop's own section:
 * nothing here can advance the keyboard latch a second time, so a case drives
 * exactly one code into the modal loop and the branches that repaint and go
 * round again cannot be reached.  A case that fails by not coming back is
 * therefore a real outcome here and is called out where it applies.
 *
 * THE REAL MISC.VFS IS READ.  fdps_load_status_cel_image names its container
 * and its member with literals and takes no argument, so there is nothing to
 * point it at a fixture, and FACE.CEL is needed as well because the panel
 * draws the unit's portrait out of it.  Both are staged by tests/gamefile.lst;
 * every case here stops if either is missing rather than dereferencing what a
 * failed load leaves behind.
 *
 * Expected values come from the assembly at 000259c0: PUSH 0x0 at 00025a5f for
 * the opening draw's slot, MOV [EBP-0x1c],EAX at 00025ad0 with no test between
 * it and the MOV EAX,[EBP-0x4] at 00025b0d for the answer being the loop's
 * unchanged, the two arguments pushed at 00025a41 and 00025a45 for the panel's
 * unit index, and the three CALL 0x0003d478 at 00025ae7, 00025af3 and 00025aff
 * for the buffers going back.  The values -1 and 1 are the loop's own, from
 * the stores at 00025ca1 and 00025c57.  None of them is read off the emitted
 * C.
 */

/* The two containers the call cannot be run without. */
#define WINDOW_CONTAINER_FILE "MISC.VFS"
#define WINDOW_PORTRAIT_FILE "FACE.CEL"

#define VIDEO_MODE_TEXT 0x03
#define VIDEO_MODE_320X200X256 0x13

/* The mode 13h frame the window is opened over and put back on top of. */
#define WINDOW_SCREEN_BYTES 0xfa00

/* Number.cel's shape, restated from tests/statunit.c's fixture: the sprite
   offset table at a fixed +0x0f, one dword per sprite, five colour rows of
   thirteen glyphs, and a stream per sprite of eight rows of one fill op six
   pixels wide -- command 0x05 is op 00 with a run of (5 & 0x3f) + 1. */
#define WN_NUM_TABLE_AT 0x0f
#define WN_NUM_SPRITES 65
#define WN_NUM_STREAM_AT (WN_NUM_TABLE_AT + WN_NUM_SPRITES * 4)
#define WN_NUM_STREAM_BYTES 16
#define WN_NUM_SHEET_BYTES \
    (WN_NUM_STREAM_AT + WN_NUM_SPRITES * WN_NUM_STREAM_BYTES)

/* The gauge sheet: three graphics of 0x75 by 8 raw pixels, 0x3a8 bytes
   apart. */
#define WN_BAR_GRAPHIC_STRIDE 0x3a8
#define WN_BAR_SHEET_BYTES (3 * WN_BAR_GRAPHIC_STRIDE)

/* The sprite cache the panel takes the unit's cell out of: a table of slots at
   the base, each holding twelve dword offsets from that same base, followed by
   one 24x24 stream per slot.  Command 0x17 is a run of 24, the cell width, so
   every row of the stream closes on its own column count.  There is one slot
   per staged unit because a village-phase panel indexes the cache with the
   unit index itself (statunit.h). */
#define WN_CACHE_SLOTS STAGE_UNITS
#define WN_CELL_SIZE 0x18
#define WN_CELL_STREAM_BYTES (WN_CELL_SIZE * 2)
#define WN_CACHE_TABLE_BYTES \
    (WN_CACHE_SLOTS * (int) sizeof(struct fdps_cel_cache_slot))
#define WN_CACHE_BYTES \
    (WN_CACHE_TABLE_BYTES + WN_CACHE_SLOTS * WN_CELL_STREAM_BYTES)

/* The text block: 256 two-byte table entries followed by a stream each, which
   covers every id the panel can compute -- the highest base it adds a record
   byte to is the class name's 0xa1 (statunit.h) and every staged record byte
   is 0. */
#define WN_TEXT_ENTRIES 256
#define WN_TEXT_TABLE_BYTES (WN_TEXT_ENTRIES * 2)
#define WN_TEXT_STREAM_BYTES 4
#define WN_TEXT_BYTES \
    (WN_TEXT_TABLE_BYTES + WN_TEXT_ENTRIES * WN_TEXT_STREAM_BYTES)

/* One glyph is eight pixels wide, one row tall and one byte in the sheet. */
#define WN_GLYPH_WIDTH 8
#define WN_GLYPH_ROWS 1

/* The item the usable-only case confirms on, and the slot it sits in.  The use
   effect is non-zero, which is the only thing the loop's usable test asks
   (00025c84), and every other record of the staged table has zero there. */
#define WN_USABLE_SLOT 3
#define WN_USABLE_ITEM 0x22
#define WN_USABLE_EFFECT 0x0b

/* The unit the usable-only case opens the window on, and the one a swapped
   pair of arguments would reach instead. */
#define WN_UNIT 2
#define WN_OTHER_UNIT 1

/* What the village unit index is set to before each call, so that a panel that
   never ran leaves a value no case expects. */
#define WN_NO_UNIT (-1)

static unsigned char window_number_sheet[WN_NUM_SHEET_BYTES];
static unsigned char window_bar_sheet[WN_BAR_SHEET_BYTES];
static unsigned char window_cache[WN_CACHE_BYTES];
static unsigned char window_text[WN_TEXT_BYTES];
static unsigned char window_font[WN_TEXT_ENTRIES];

/* The effect pack the window's cue misses in: an image whose entry count is
   zero makes the lookup fail without touching the name and without allocating,
   which is what the original does on a machine whose pack does not hold the
   member (audio.h). */
static struct fdps_vfs_image_header window_sfx_pack;

/* The village screen the close puts back.  It goes on the heap for the length
   of one call rather than into a static of its own. */
static unsigned char *window_village_page;

/* Both shipped files the call needs, probed rather than assumed. */
static int window_files_present(void)
{
    FILE *probe;

    probe = fopen(WINDOW_CONTAINER_FILE, "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);

    probe = fopen(WINDOW_PORTRAIT_FILE, "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);
    return 1;
}

/* Used entries currently in the heap.  A used entry becomes a free entry the
   moment it is released, possibly merged with a neighbour, so the used ones
   are counted and the free ones are not. */
static int window_used_heap_blocks(void)
{
    struct _heapinfo entry;
    int used;

    used = 0;
    entry._pentry = NULL;
    while (_heapwalk(&entry) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY) {
            used++;
        }
    }
    return used;
}

static void window_set_video_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Builds every sheet the status panel draws through and points the globals at
   them.  Nothing here is asserted on: the panel's own output is pinned in
   tests/statunit.c, and these exist so that every decode the panel makes stays
   inside a buffer this file owns. */
static void window_stage_sheets(void)
{
    int index;
    int sprite;
    int row;
    int stream_at;
    int slot;

    memset(window_number_sheet, 0, sizeof window_number_sheet);
    for (sprite = 0; sprite < WN_NUM_SPRITES; sprite++) {
        stream_at = WN_NUM_STREAM_AT + sprite * WN_NUM_STREAM_BYTES;
        *(int *) (window_number_sheet + WN_NUM_TABLE_AT + sprite * 4) =
            stream_at;
        for (row = 0; row < 8; row++) {
            window_number_sheet[stream_at + row * 2] = 0x05;
            window_number_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (sprite + 1);
        }
    }

    memset(window_bar_sheet, 0x01, sizeof window_bar_sheet);

    memset(window_cache, 0, sizeof window_cache);
    for (slot = 0; slot < WN_CACHE_SLOTS; slot++) {
        stream_at = WN_CACHE_TABLE_BYTES + slot * WN_CELL_STREAM_BYTES;
        ((struct fdps_cel_cache_slot *) window_cache)[slot].sprite_offset[0] =
            stream_at;
        for (row = 0; row < WN_CELL_SIZE; row++) {
            window_cache[stream_at + row * 2] = 0x17;
            window_cache[stream_at + row * 2 + 1] = (unsigned char) (slot + 1);
        }
    }

    memset(window_text, 0, sizeof window_text);
    for (index = 0; index < WN_TEXT_ENTRIES; index++) {
        window_font[index] = (unsigned char) index;
        stream_at = WN_TEXT_TABLE_BYTES + index * WN_TEXT_STREAM_BYTES;
        *(short *) (window_text + index * 2) = (short) stream_at;
        *(short *) (window_text + stream_at) = (short) index;
        *(short *) (window_text + stream_at + 2) = (short) -1;
    }

    data_fdps_number_glyph_sheet_ptr = window_number_sheet;
    data_fdps_status_gauge_bar_sheet_ptr = window_bar_sheet;
    data_fdps_cel_sprite_cache_ptr = window_cache;
    data_fdps_all_game_text_ptr = window_text;
    data_fdps_font_sheet_ptr = window_font;
    data_fdps_font_glyph_width = (unsigned char) WN_GLYPH_WIDTH;
    data_fdps_glyph_cell_height = (unsigned char) WN_GLYPH_ROWS;
    data_fdps_font_glyph_stride_bytes = 1;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = WN_GLYPH_WIDTH;
    data_fdps_font_line_height = WN_GLYPH_ROWS;
}

/* Everything one call reads that this file can set.  loop_stage above already
   publishes the records, the item table, the selection bar sheet and the make
   code the modal loop will come back on, so this adds what the panel and the
   close need on top of it: every entry of every staged unit empty, so the item
   list draws its bar and nothing else; the village branch, which is the one
   that closes out of a page this file owns rather than out of the scene
   compositor; and the pack the cue misses in. */
static void window_stage(unsigned int scancode)
{
    int unit;
    int slot;

    loop_stage(scancode);
    for (unit = 0; unit < STAGE_UNITS; unit++) {
        for (slot = 0; slot < INVENTORY_ENTRY_COUNT; slot++) {
            set_entry(unit, slot, FLAG_EMPTY, 0);
        }
    }
    window_stage_sheets();

    window_village_page =
        (unsigned char *) malloc((size_t) WINDOW_SCREEN_BYTES);
    memset(window_village_page, 0x3c, (size_t) WINDOW_SCREEN_BYTES);
    data_fdps_village_backdrop_page_ptr = window_village_page;
    data_fdps_village_mode_flag = (unsigned char) 1;
    data_fdps_village_status_window_unit_idx = WN_NO_UNIT;

    memset(&window_sfx_pack, 0, sizeof window_sfx_pack);
    data_fdps_audio_basewav_sfx_bank_buf_ptr =
        (unsigned char *) &window_sfx_pack;

    data_fdps_number_glyph_color_row = 0;
}

/* Gives back the portrait buffer the panel's loader leaves allocated on
   purpose, and the village page, and unpublishes every global that points at
   storage this file owns: fdps_shutdown_free_resources frees all of them
   unguarded. */
static void window_unstage(void)
{
    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
    if (window_village_page != NULL) {
        free(window_village_page);
        window_village_page = NULL;
    }
    data_fdps_village_backdrop_page_ptr = NULL;
    data_fdps_village_mode_flag = 0;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_number_glyph_sheet_ptr = NULL;
    data_fdps_status_gauge_bar_sheet_ptr = NULL;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_all_game_text_ptr = NULL;
    data_fdps_font_sheet_ptr = NULL;
    data_fdps_selection_bar_sheet_ptr = NULL;
}

/* One window, opened with the adapter in the mode it is drawn in and taken
   down again afterwards. */
static int window_open(int unit_index, int usable_only, int *selected_slot)
{
    int answer;

    window_set_video_mode(VIDEO_MODE_320X200X256);
    answer = fdps_unit_item_select_window(unit_index, usable_only,
                                          selected_slot);
    window_set_video_mode(VIDEO_MODE_TEXT);
    window_unstage();
    return answer;
}

/* A cancel comes back as the loop's own -1 and the caller's slot is not
   touched on the way: the window function never writes through the pointer, it
   only hands it on.  The seed is 5 rather than 0 so that a body that reset the
   cursor -- or that handed the opening draw's literal 0 to the loop instead of
   the caller's pointer -- would show up as a 0 here. */
static void a_cancelled_window_answers_minus_one_and_keeps_the_slot(void)
{
    int selected_slot;
    int files_present;

    files_present = window_files_present();
    CHECK_EQ(files_present, 1);
    if (!files_present) {
        return;
    }
    window_stage(KEY_ESC);
    selected_slot = 5;
    CHECK_EQ(window_open(0, 0, &selected_slot), -1);
    CHECK_EQ(selected_slot, 5);
}

/* A confirm comes back as the loop's own 1, again with the slot as the caller
   left it, and the panel is drawn for the unit index the caller named: the
   village-phase panel publishes that index in
   data_fdps_village_status_window_unit_idx and nothing else in the call writes
   it.
   THE UNIT INDEX IS ALSO PINNED BY NOT HANGING.  usable_only is 1 here, so the
   confirm is allowed only by a non-zero use effect on the item the entry names,
   and only unit 2's entry 3 holds that item -- unit 1's entry 3, which is where
   a swapped unit_index/usable_only pair would look, holds an id whose record
   has use effect 0.  A refusal repaints and waits again, and no second wait can
   return, so that failure shows up as this case hanging with the heartbeat
   frozen on its name rather than as a check that did not hold. */
static void a_confirmed_window_answers_one_for_the_unit_it_was_given(void)
{
    int selected_slot;
    int files_present;

    files_present = window_files_present();
    CHECK_EQ(files_present, 1);
    if (!files_present) {
        return;
    }
    window_stage(KEY_ENTER);
    set_entry(WN_UNIT, WN_USABLE_SLOT, FLAG_EMPTY, WN_USABLE_ITEM);
    set_entry(WN_OTHER_UNIT, WN_USABLE_SLOT, FLAG_EMPTY, WN_USABLE_ITEM + 1);
    set_use_effect(WN_USABLE_ITEM, WN_USABLE_EFFECT);
    selected_slot = WN_USABLE_SLOT;

    CHECK_EQ(window_open(WN_UNIT, 1, &selected_slot), 1);
    CHECK_EQ(selected_slot, WN_USABLE_SLOT);
    CHECK_EQ(data_fdps_village_status_window_unit_idx, WN_UNIT);
}

/* The window image, the list backdrop and the saved screen are all three the
   call's own and all three are released before it returns, so a second call
   leaves the heap holding exactly what the first one did.  The first run is
   made before the count is taken because the loaders underneath it hold
   one-time allocations of their own; what is compared is two runs of the same
   shape. */
static void the_window_gives_back_every_block_it_took(void)
{
    int selected_slot;
    int before;
    int after;
    int files_present;

    files_present = window_files_present();
    CHECK_EQ(files_present, 1);
    if (!files_present) {
        return;
    }
    window_stage(KEY_ESC);
    selected_slot = 0;
    CHECK_EQ(window_open(0, 0, &selected_slot), -1);
    before = window_used_heap_blocks();

    window_stage(KEY_ESC);
    selected_slot = 0;
    CHECK_EQ(window_open(0, 0, &selected_slot), -1);
    after = window_used_heap_blocks();

    CHECK_EQ(after, before);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

void run_unititem_tests(void)
{
    RUN_TEST(the_two_record_layouts_match_the_strides);
    RUN_TEST(a_unit_with_nothing_equipped_answers_minus_one);
    RUN_TEST(each_search_finds_its_own_kind);
    RUN_TEST(the_first_matching_slot_wins);
    RUN_TEST(all_eight_entries_are_scanned_and_no_more);
    RUN_TEST(only_bit_0x40_equips_an_entry);
    RUN_TEST(the_weapon_span_runs_from_one_to_0x15);
    RUN_TEST(a_blank_item_record_is_not_an_equipped_weapon);
    RUN_TEST(the_armor_span_runs_from_0x16_to_0x27);
    RUN_TEST(any_non_zero_want_armor_asks_for_armor);
    RUN_TEST(the_item_id_byte_is_unsigned);
    RUN_TEST(the_index_selects_its_own_unit_record);
    RUN_TEST(the_search_writes_nothing);
    RUN_TEST(the_record_is_resolved_on_every_call);
    RUN_TEST(get_item_id_returns_the_entrys_second_byte);
    RUN_TEST(get_item_id_reads_each_of_the_eight_slots);
    RUN_TEST(get_item_id_widens_the_byte_without_sign);
    RUN_TEST(get_item_id_ignores_the_entry_flag_byte);
    RUN_TEST(get_item_id_does_not_range_check_the_slot);
    RUN_TEST(get_item_id_takes_its_record_from_the_index);
    RUN_TEST(get_item_id_writes_nothing);
    RUN_TEST(get_item_id_resolves_the_record_on_every_call);
    RUN_TEST(item_count_counts_entries_without_the_empty_bit);
    RUN_TEST(item_count_tests_bit_0x80_alone);
    RUN_TEST(item_count_ignores_the_item_id_byte);
    RUN_TEST(item_count_does_not_stop_at_the_first_empty_slot);
    RUN_TEST(item_count_scans_eight_entries_and_no_more);
    RUN_TEST(item_count_takes_its_record_from_the_index);
    RUN_TEST(item_count_writes_nothing);
    RUN_TEST(item_count_resolves_the_record_on_every_call);
    RUN_TEST(remove_item_shifts_the_entries_above_it_down);
    RUN_TEST(remove_item_at_slot_zero_moves_all_seven_entries);
    RUN_TEST(remove_item_always_marks_the_last_entry_empty);
    RUN_TEST(remove_item_leaves_the_last_entrys_id_byte_alone);
    RUN_TEST(remove_item_at_the_last_slot_moves_nothing);
    RUN_TEST(remove_item_writes_only_inside_the_inventory_field);
    RUN_TEST(remove_item_edits_the_record_the_index_names);
    RUN_TEST(remove_item_resolves_the_record_on_every_call);
    RUN_TEST(add_item_fills_the_first_empty_entry);
    RUN_TEST(add_item_zeroes_the_whole_flag_byte);
    RUN_TEST(add_item_tests_bit_0x80_as_a_mask);
    RUN_TEST(add_item_fills_a_hole_rather_than_the_end);
    RUN_TEST(add_item_answers_minus_one_when_full_and_writes_nothing);
    RUN_TEST(add_item_stores_only_the_low_byte_of_the_id);
    RUN_TEST(add_item_scans_eight_entries_and_no_more);
    RUN_TEST(add_item_writes_only_the_two_bytes_of_the_slot);
    RUN_TEST(add_item_writes_the_record_the_index_names);
    RUN_TEST(add_item_resolves_the_record_on_every_call);
    RUN_TEST(add_item_takes_the_slot_a_removal_freed);
    RUN_TEST(can_equip_layouts_match_the_class_table_stride);
    RUN_TEST(can_equip_matches_at_every_one_of_the_six_positions);
    RUN_TEST(can_equip_answers_zero_when_no_position_matches);
    RUN_TEST(can_equip_uses_the_class_code_without_the_promap_bias);
    RUN_TEST(can_equip_widens_the_class_code_without_sign);
    RUN_TEST(can_equip_takes_the_class_from_the_unit_the_index_names);
    RUN_TEST(can_equip_reads_only_byte_zero_of_the_item_record);
    RUN_TEST(can_equip_scans_six_positions_and_no_more);
    RUN_TEST(can_equip_has_no_sentinel_test);
    RUN_TEST(can_equip_hands_the_item_id_to_the_table_unchanged);
    RUN_TEST(can_equip_writes_nothing);
    RUN_TEST(can_equip_resolves_its_records_on_every_call);
    RUN_TEST(equip_slot_stores_0x40_over_the_slots_flag_byte);
    RUN_TEST(equip_slot_unequips_a_weapon_of_a_different_type);
    RUN_TEST(equip_slot_unequips_armor_of_a_different_sub_kind);
    RUN_TEST(equip_slot_leaves_the_other_category_alone);
    RUN_TEST(equip_slot_splits_the_categories_at_type_0x15);
    RUN_TEST(equip_slot_counts_everything_above_0x15_as_armor);
    RUN_TEST(equip_slot_only_considers_entries_with_bit_0x40);
    RUN_TEST(equip_slot_leaves_a_blank_item_record_equipped);
    RUN_TEST(equip_slot_does_not_test_the_equipped_items_own_type);
    RUN_TEST(equip_slot_unequips_every_matching_entry);
    RUN_TEST(equip_slot_zeroes_the_whole_flag_byte_it_unequips);
    RUN_TEST(equip_slot_re_equips_the_entry_the_loop_zeroed);
    RUN_TEST(equip_slot_scans_eight_entries_and_no_more);
    RUN_TEST(equip_slot_writes_only_two_flag_bytes);
    RUN_TEST(equip_slot_classifies_by_the_id_byte_not_the_flag_byte);
    RUN_TEST(equip_slot_widens_the_id_byte_without_sign);
    RUN_TEST(equip_slot_does_not_range_check_the_slot);
    RUN_TEST(equip_slot_edits_the_record_the_index_names);
    RUN_TEST(equip_slot_resolves_the_record_on_every_call);
    RUN_TEST(equip_slot_equips_the_entry_an_add_just_filled);
    RUN_TEST(find_item_slot_answers_minus_one_for_an_empty_inventory);
    RUN_TEST(find_item_slot_finds_the_id_of_a_packed_entry);
    RUN_TEST(find_item_slot_answers_the_lowest_matching_entry);
    RUN_TEST(find_item_slot_stops_at_the_occupied_count);
    RUN_TEST(find_item_slot_reads_an_empty_entry_inside_the_bound);
    RUN_TEST(find_item_slot_reaches_the_last_entry_of_a_full_inventory);
    RUN_TEST(find_item_slot_compares_the_id_at_full_width);
    RUN_TEST(find_item_slot_takes_its_unit_from_the_index);
    RUN_TEST(find_item_slot_writes_nothing);
    RUN_TEST(find_item_slot_resolves_the_record_on_every_call);
    RUN_TEST(escape_cancels_and_leaves_the_slot_alone);
    RUN_TEST(delete_cancels_the_same_way_escape_does);
    RUN_TEST(enter_confirms_without_consulting_the_item_table);
    RUN_TEST(space_confirms_the_same_way_enter_does);
    RUN_TEST(usable_only_accepts_the_id_byte_of_the_selected_entry);
    RUN_TEST(the_backdrop_is_laid_over_the_list_rectangle);
    RUN_TEST(the_blit_stops_at_the_rectangles_edges);
    RUN_TEST(the_selection_bar_lands_on_the_row_the_slot_names);
    RUN_TEST(a_slot_outside_the_eight_rows_draws_no_bar);
    RUN_TEST(a_cancelled_window_answers_minus_one_and_keeps_the_slot);
    RUN_TEST(a_confirmed_window_answers_one_for_the_unit_it_was_given);
    RUN_TEST(the_window_gives_back_every_block_it_took);
}
