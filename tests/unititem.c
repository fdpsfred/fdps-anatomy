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
 * The item block is published one record PAST the start of its storage, so
 * record -1 exists and is addressable.  That is what lets the unsigned read of
 * the id byte be told apart from a signed one: id 0xff must reach record 255
 * and not the record in front of the base.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
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

static unsigned char unit_block[STAGE_UNITS * UNIT_RECORD_STRIDE];
static unsigned char item_block[(STAGE_ITEMS + 1) * ITEM_RECORD_STRIDE];

/* Only ever read for its field sizes and offsets. */
static struct fdps_unit_record layout_probe;

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
    data_fdps_map_unit_array_ptr = unit_block;
    data_fdps_item_effect_table_ptr = item_block + ITEM_RECORD_STRIDE;
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
}
