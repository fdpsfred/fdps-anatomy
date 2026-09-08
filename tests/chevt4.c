/* tests/chevt4.c -- cover for src/chevt4.c.
 *
 * The chapter 20 handler at 000382f0 takes its whole effect through one unit
 * record, so the unit array is staged here rather than read from a game file:
 * pointing data_fdps_map_unit_array_ptr at a local block is the only way to see
 * the stores.  What the global itself holds is ticket 23's and is not asserted,
 * so every case writes the state it wants to see changed.  The turn counter and
 * the chapter text pointer are staged for the same reason.
 *
 * The handler speaks a line before it swaps the sword, and the draw is real:
 * data_fdps_current_chapter_text_ptr is pointed at a block whose every entry
 * names one lone terminator, so fdps_draw_text walks the entry, paints nothing
 * and returns at once without needing a font, a message panel or mode 13h.
 * That is what these cases can see of the draw -- that it happens and survives
 * -- and the text id itself is read off PUSH 0x13 at 0003833c, not asserted
 * here, because a draw that paints nothing leaves nothing behind to tell one
 * entry from another.
 *
 * Two things about the handler are what the cases are really for.  The deadline
 * is CMP 0x14 / JLE at 00038316, so turn 20 is inclusive: that boundary is
 * asserted from both sides, because it is what every obvious rewrite of the
 * test gets wrong at exactly one turn, and getting it wrong costs the player
 * 真炎龍劍 five chapters later.  And there is no one-shot latch anywhere in the
 * body -- the item held is the flag -- so the cases put the shared latch byte
 * up and watch the handler fire anyway, and re-arm the sword after a firing and
 * watch it fire a second time.
 *
 * The expected inventory shape after a firing comes from the two callees
 * (unititem.h): the removal memmoves the entries above the slot down and empties
 * the last one, and the add takes the first entry whose flag has bit 0x80 set,
 * so the new sword lands in the slot the old one vacated.
 */
#include <stddef.h>
#include <string.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "chevt4.h"

/* The two swords, read off PUSH 0xa0 at 000382fc and PUSH 0xa1 at 0003835c:
   灼烈之劍 and 火光之劍 (assets/items.md). */
#define CH20_BLAZING_SWORD 0xa0
#define CH20_FLAME_SWORD 0xa1

/* Something else to carry that is neither of them, so a case can say which
   entry moved: 0xa3, 金屬礦, a plain carried item with no combat effect. */
#define CH20_OTHER_ITEM 0xa3

/* The last turn the event still fires on, CMP 0x14 / JLE at 00038316, and the
   first turn it does not. */
#define CH20_LAST_TURN 0x14
#define CH20_FIRST_LATE_TURN (CH20_LAST_TURN + 1)

/* The turn a battle opens on, which is what fdps_chapter_state_reset writes. */
#define CH20_OPENING_TURN 1

/* The only unit index the gate at 00038310 lets through, and two others that
   have to be refused even while they carry the sword. */
#define CH20_RANDIS 0
#define CH20_OTHER_UNIT_A 1
#define CH20_OTHER_UNIT_B 2
#define CH20_STAGE_UNITS 3

/* An inventory entry nobody is carrying: flag bit 0x80 is what
   fdps_unit_item_count and fdps_unit_add_item read as empty, and the stale id
   beside it is the 0xff a deployment leaves (unititem.h).  A carried entry that
   is not equipped has a flag of 0, which keeps the stat rebuild off the item
   table. */
#define CH20_EMPTY_FLAG 0x80
#define CH20_EMPTY_ID 0xff
#define CH20_CARRIED_FLAG 0x00
#define CH20_INVENTORY_ENTRIES 8

/* The chapter text block: twenty entries, which is what FDETXT20.TXT carries,
   every one of them pointing at the same lone terminator so that a draw walks
   it, paints nothing and returns at once. */
#define CH20_TEXT_IDS 0x14
#define CH20_TEXT_EMPTY_AT 0x40
#define CH20_TEXT_BLOCK_BYTES (CH20_TEXT_EMPTY_AT + 2)
#define CH20_TEXT_END (-1)

/* Randis's base stats and the sentinel the four derived stats are stamped with
   before each run.  The bases are arbitrary and only have to differ from each
   other and from the sentinel; with no equipped entry and no status timer the
   rebuild writes ap = ap_base, dp = dp_base and both hit and ev = dx_base
   (unit.h). */
#define CH20_AP_BASE 41
#define CH20_DP_BASE 31
#define CH20_DX_BASE 21
#define CH20_STAT_SENTINEL 0x7777

/* Every item id is a valid index into the staged item table:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define CH20_ITEM_TABLE_ROWS 256

/* The element of data_fdps_map_cell_event_triggered_flags the sibling handlers
   of neighbouring chapters latch.  This handler must not read or write it. */
#define CH20_LATCH_SLOT 0x10
#define CH20_LATCH_RAISED 1

static unsigned char ch20_text_block[CH20_TEXT_BLOCK_BYTES];
static struct fdps_unit_record ch20_units[CH20_STAGE_UNITS];
static struct fdps_item_effect ch20_items[CH20_ITEM_TABLE_ROWS];
static int ch20_text_staged = 0;

static void ch20_stage_text(void)
{
    int text_id;

    if (ch20_text_staged) {
        return;
    }
    ch20_text_staged = 1;

    memset(ch20_text_block, 0, (size_t) CH20_TEXT_BLOCK_BYTES);
    *(short *) (ch20_text_block + CH20_TEXT_EMPTY_AT) = (short) CH20_TEXT_END;
    for (text_id = 0; text_id < CH20_TEXT_IDS; text_id++) {
        *(short *) (ch20_text_block + text_id * 2) = (short) CH20_TEXT_EMPTY_AT;
    }
}

/* One record: an empty eight-entry inventory, the three stat bases the rebuild
   reads and the sentinel in the four stats it writes. */
static void ch20_blank_unit(int unit_index)
{
    int entry;

    for (entry = 0; entry < CH20_INVENTORY_ENTRIES; entry++) {
        ch20_units[unit_index].inventory_slots[entry * 2] = CH20_EMPTY_FLAG;
        ch20_units[unit_index].inventory_slots[entry * 2 + 1] = CH20_EMPTY_ID;
    }
    ch20_units[unit_index].ap_base = CH20_AP_BASE;
    ch20_units[unit_index].dp_base = CH20_DP_BASE;
    ch20_units[unit_index].dx_base = CH20_DX_BASE;
    ch20_units[unit_index].ap = CH20_STAT_SENTINEL;
    ch20_units[unit_index].dp = CH20_STAT_SENTINEL;
    ch20_units[unit_index].hit = CH20_STAT_SENTINEL;
    ch20_units[unit_index].ev = CH20_STAT_SENTINEL;
}

static void ch20_carry(int unit_index, int entry, int item_id)
{
    ch20_units[unit_index].inventory_slots[entry * 2] = CH20_CARRIED_FLAG;
    ch20_units[unit_index].inventory_slots[entry * 2 + 1] =
        (unsigned char) item_id;
}

/* Everything the handler and its callees read that a case has to make definite:
   three blank records, an item table for the stat rebuild, the text block, the
   turn counter, and the shared latch byte raised so that a body which consulted
   it would be seen refusing to run. */
static void ch20_stage(int battle_turn)
{
    int unit_index;

    ch20_stage_text();

    memset(ch20_units, 0, sizeof(ch20_units));
    memset(ch20_items, 0, sizeof(ch20_items));
    for (unit_index = 0; unit_index < CH20_STAGE_UNITS; unit_index++) {
        ch20_blank_unit(unit_index);
    }

    data_fdps_map_unit_array_ptr = (unsigned char *) ch20_units;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch20_items;
    data_fdps_current_chapter_text_ptr = ch20_text_block;
    data_fdps_battle_turn_counter = battle_turn;
    data_fdps_map_cell_event_triggered_flags[CH20_LATCH_SLOT] =
        CH20_LATCH_RAISED;
}

/* The state every case starts from: Randis carrying the sword in his first
   entry on the battle's opening turn, and the two other staged units carrying
   one each as well, so that a firing by the wrong index would be visible. */
static void ch20_stage_armed(int battle_turn)
{
    ch20_stage(battle_turn);
    ch20_carry(CH20_RANDIS, 0, CH20_BLAZING_SWORD);
    ch20_carry(CH20_OTHER_UNIT_A, 0, CH20_BLAZING_SWORD);
    ch20_carry(CH20_OTHER_UNIT_B, 0, CH20_BLAZING_SWORD);
}

static int ch20_entry_flag(int unit_index, int entry)
{
    return (int) ch20_units[unit_index].inventory_slots[entry * 2];
}

static int ch20_entry_id(int unit_index, int entry)
{
    return (int) ch20_units[unit_index].inventory_slots[entry * 2 + 1];
}

/* Whether any of the eight entries holds that id, flag byte disregarded, which
   is how a case says an item was or was not handed over. */
static int ch20_carries(int unit_index, int item_id)
{
    int entry;

    for (entry = 0; entry < CH20_INVENTORY_ENTRIES; entry++) {
        if (ch20_entry_flag(unit_index, entry) != CH20_EMPTY_FLAG
                && ch20_entry_id(unit_index, entry) == item_id) {
            return 1;
        }
    }
    return 0;
}

/* Whether the four derived stats still hold the sentinel, which is how a case
   says the rebuild did or did not run. */
static int ch20_stats_untouched(int unit_index)
{
    return (int) ch20_units[unit_index].ap == CH20_STAT_SENTINEL
           && (int) ch20_units[unit_index].dp == CH20_STAT_SENTINEL
           && (int) ch20_units[unit_index].hit == CH20_STAT_SENTINEL
           && (int) ch20_units[unit_index].ev == CH20_STAT_SENTINEL;
}

/* ---------------------------------------------------------------------- */

/* The record fields these cases read back and the stride they are indexed by.
   Every one of them would agree with itself while addressing another byte if
   the layout were wrong. */
static void ch20_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots), 0x0a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap), 0x48);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ev), 0x4e);
}

/* The whole taken path, out of the one firing it costs.  灼烈之劍 leaves the
   bag and 火光之劍 takes its place in the entry it vacated -- the removal
   compacts the entries above the slot down and the add takes the first empty
   entry, which is that one -- the rest of the bag is still empty, and the four
   derived stats have lost the sentinel and hold the bases the rebuild writes
   with nothing equipped. */
static void ch20_swaps_the_sword_for_randis(void)
{
    ch20_stage_armed(CH20_OPENING_TURN);

    fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);

    CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_BLAZING_SWORD), 0);
    CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_FLAME_SWORD), 1);
    CHECK_EQ(ch20_entry_flag(CH20_RANDIS, 0), CH20_CARRIED_FLAG);
    CHECK_EQ(ch20_entry_id(CH20_RANDIS, 0), CH20_FLAME_SWORD);
    CHECK_EQ(ch20_entry_flag(CH20_RANDIS, 1), CH20_EMPTY_FLAG);
    CHECK_EQ(ch20_entry_flag(CH20_RANDIS, CH20_INVENTORY_ENTRIES - 1),
             CH20_EMPTY_FLAG);
    CHECK_EQ((int) ch20_units[CH20_RANDIS].ap, CH20_AP_BASE);
    CHECK_EQ((int) ch20_units[CH20_RANDIS].dp, CH20_DP_BASE);
    CHECK_EQ((int) ch20_units[CH20_RANDIS].hit, CH20_DX_BASE);
    CHECK_EQ((int) ch20_units[CH20_RANDIS].ev, CH20_DX_BASE);
}

/* The removal is given the slot the search returned and not a literal 0: with
   金屬礦 in the first entry and the sword in the second, it is the second that
   is emptied and refilled, and the first is left exactly as it was.  A rebuild
   that passed 0 -- which the third call really is given -- would throw away the
   ore and leave the sword behind. */
static void ch20_removes_the_slot_the_search_found(void)
{
    ch20_stage(CH20_OPENING_TURN);
    ch20_carry(CH20_RANDIS, 0, CH20_OTHER_ITEM);
    ch20_carry(CH20_RANDIS, 1, CH20_BLAZING_SWORD);

    fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);

    CHECK_EQ(ch20_entry_flag(CH20_RANDIS, 0), CH20_CARRIED_FLAG);
    CHECK_EQ(ch20_entry_id(CH20_RANDIS, 0), CH20_OTHER_ITEM);
    CHECK_EQ(ch20_entry_flag(CH20_RANDIS, 1), CH20_CARRIED_FLAG);
    CHECK_EQ(ch20_entry_id(CH20_RANDIS, 1), CH20_FLAME_SWORD);
    CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_BLAZING_SWORD), 0);
    CHECK_EQ(ch20_entry_flag(CH20_RANDIS, 2), CH20_EMPTY_FLAG);
}

/* Turn 20 is inside the deadline and turn 21 is not, which is the whole of
   CMP 0x14 / JLE.  Both halves of the boundary are asserted, and three later
   turns as well, because a rebuild that wrote the compare as < 20 fails only on
   the first of these two and looks right everywhere else. */
static void ch20_fires_on_the_last_turn_and_not_after(void)
{
    static int late_turns[3] = {CH20_FIRST_LATE_TURN, 0x20, 99};
    int i;

    ch20_stage_armed(CH20_LAST_TURN);

    fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);

    CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_FLAME_SWORD), 1);
    CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_BLAZING_SWORD), 0);

    for (i = 0; i < 3; i++) {
        ch20_stage_armed(late_turns[i]);

        fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);

        CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_FLAME_SWORD), 0);
        CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_BLAZING_SWORD), 1);
        CHECK_EQ(ch20_stats_untouched(CH20_RANDIS), 1);
    }
}

/* Every turn below the deadline fires, so the gate is a deadline and not an
   equality on one scheduled turn the way the neighbouring chapters' turn
   events are. */
static void ch20_fires_on_every_turn_up_to_the_deadline(void)
{
    static int early_turns[4] = {CH20_OPENING_TURN, 2, 10, 0x13};
    int i;

    for (i = 0; i < 4; i++) {
        ch20_stage_armed(early_turns[i]);

        fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);

        CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_FLAME_SWORD), 1);
        CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_BLAZING_SWORD), 0);
    }
}

/* No unit but battle unit 0 springs the event, and carrying the sword is not
   what qualifies one: both other staged units carry 灼烈之劍 and neither is
   given 火光之劍, neither has its stats rebuilt, and Randis's own bag is left
   alone while they walk over the tile. */
static void ch20_fires_for_no_unit_but_randis(void)
{
    static int other_units[2] = {CH20_OTHER_UNIT_A, CH20_OTHER_UNIT_B};
    int i;

    for (i = 0; i < 2; i++) {
        ch20_stage_armed(CH20_OPENING_TURN);

        fdps_chapter_20_event_upgrade_randis_sword(other_units[i]);

        CHECK_EQ(ch20_carries(other_units[i], CH20_FLAME_SWORD), 0);
        CHECK_EQ(ch20_carries(other_units[i], CH20_BLAZING_SWORD), 1);
        CHECK_EQ(ch20_stats_untouched(other_units[i]), 1);
        CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_BLAZING_SWORD), 1);
        CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_FLAME_SWORD), 0);
        CHECK_EQ(ch20_stats_untouched(CH20_RANDIS), 1);
    }
}

/* A Randis who is not carrying the sword gets nothing: the search misses, the
   third gate refuses, and neither the bag nor the stats move.  This is also the
   state the event leaves behind, which is what stops it firing twice. */
static void ch20_without_the_sword_does_nothing(void)
{
    ch20_stage(CH20_OPENING_TURN);
    ch20_carry(CH20_RANDIS, 0, CH20_OTHER_ITEM);

    fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);

    CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_FLAME_SWORD), 0);
    CHECK_EQ(ch20_entry_id(CH20_RANDIS, 0), CH20_OTHER_ITEM);
    CHECK_EQ(ch20_entry_flag(CH20_RANDIS, 1), CH20_EMPTY_FLAG);
    CHECK_EQ(ch20_stats_untouched(CH20_RANDIS), 1);
}

/* An empty bag is refused the same way, which is the branch
   fdps_unit_find_item_slot takes on a count of zero rather than the one it
   takes on a miss. */
static void ch20_with_an_empty_bag_does_nothing(void)
{
    ch20_stage(CH20_OPENING_TURN);

    fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);

    CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_FLAME_SWORD), 0);
    CHECK_EQ(ch20_entry_flag(CH20_RANDIS, 0), CH20_EMPTY_FLAG);
    CHECK_EQ(ch20_stats_untouched(CH20_RANDIS), 1);
}

/* THERE IS NO ONE-SHOT LATCH.  The shared flag byte is up throughout every case
   in this file and the event fires regardless, and the handler does not write
   it either -- a rebuild that raised a latch of its own would leave the tile
   dead for the Randis who follows the unit that sprang it first.  What really
   stops the repeat is the item: a second firing on the swapped bag does
   nothing, and re-arming the sword makes the same handler fire again on the
   same map. */
static void ch20_has_no_one_shot_latch(void)
{
    ch20_stage_armed(CH20_OPENING_TURN);

    fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);
    CHECK_EQ(ch20_carries(CH20_RANDIS, CH20_FLAME_SWORD), 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH20_LATCH_SLOT],
             CH20_LATCH_RAISED);

    ch20_units[CH20_RANDIS].ap = CH20_STAT_SENTINEL;
    fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);
    CHECK_EQ(ch20_entry_id(CH20_RANDIS, 0), CH20_FLAME_SWORD);
    CHECK_EQ(ch20_entry_flag(CH20_RANDIS, 1), CH20_EMPTY_FLAG);
    CHECK_EQ((int) ch20_units[CH20_RANDIS].ap, CH20_STAT_SENTINEL);

    ch20_carry(CH20_RANDIS, 1, CH20_BLAZING_SWORD);
    fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);
    CHECK_EQ(ch20_entry_id(CH20_RANDIS, 1), CH20_FLAME_SWORD);
    CHECK_EQ(ch20_entry_flag(CH20_RANDIS, 1), CH20_CARRIED_FLAG);
    CHECK_EQ((int) ch20_units[CH20_RANDIS].ap, CH20_AP_BASE);
}

/* A firing writes into one record and no further: the two units either side of
   Randis in the array keep the swords they came in with and their sentinels,
   which is what says the 0x50 stride the record lookup indexes by is the one
   the fixture laid the array out on. */
static void ch20_touches_no_other_unit(void)
{
    ch20_stage_armed(CH20_OPENING_TURN);

    fdps_chapter_20_event_upgrade_randis_sword(CH20_RANDIS);

    CHECK_EQ(ch20_entry_id(CH20_OTHER_UNIT_A, 0), CH20_BLAZING_SWORD);
    CHECK_EQ(ch20_entry_flag(CH20_OTHER_UNIT_A, 0), CH20_CARRIED_FLAG);
    CHECK_EQ(ch20_stats_untouched(CH20_OTHER_UNIT_A), 1);
    CHECK_EQ(ch20_entry_id(CH20_OTHER_UNIT_B, 0), CH20_BLAZING_SWORD);
    CHECK_EQ(ch20_entry_flag(CH20_OTHER_UNIT_B, 0), CH20_CARRIED_FLAG);
    CHECK_EQ(ch20_stats_untouched(CH20_OTHER_UNIT_B), 1);
}

void run_chevt4_tests(void)
{
    RUN_TEST(ch20_record_shape_matches_the_offsets);
    RUN_TEST(ch20_swaps_the_sword_for_randis);
    RUN_TEST(ch20_removes_the_slot_the_search_found);
    RUN_TEST(ch20_fires_on_the_last_turn_and_not_after);
    RUN_TEST(ch20_fires_on_every_turn_up_to_the_deadline);
    RUN_TEST(ch20_fires_for_no_unit_but_randis);
    RUN_TEST(ch20_without_the_sword_does_nothing);
    RUN_TEST(ch20_with_an_empty_bag_does_nothing);
    RUN_TEST(ch20_has_no_one_shot_latch);
    RUN_TEST(ch20_touches_no_other_unit);
}
