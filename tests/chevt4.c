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
#include <stdio.h>
#include <stdlib.h>
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


/* ---------------------------------------------------------------------- */

/* Chapter 21's first ambush at 00038380, from here down.
 *
 * Both of its gates are cheap to see -- refusing to fire leaves the map alone
 * and needs nothing staged but a unit array -- and the interesting one is the
 * side test, which is CMP EAX,0x2 / JZ and not the CMP 0 / JA the chapter 10
 * and chapter 19 ambushes use.  Side 1, the guest side, is the one value that
 * tells the two spellings apart, so it is asserted from both directions.
 *
 * What the handler hands fdps_deploy_wave -- PUSH dword ptr [0x00069cf4] /
 * PUSH 0x1 / PUSH EAX with EAX zeroed at 000383b6..000383bb -- is left nowhere
 * afterwards, so the cases that check those three values let the deployment
 * happen for real.  fdps_deploy_wave opens ICON.CEL and FIELD.VFS for itself,
 * staged through tests/gamefile.lst, and the coordinates read back are the
 * placement records inside this file's own MAP00.COD and MAP01.COD:
 *
 *   MAP00.COD  record 0 (18, 0)  record 1 (22, 12)
 *   MAP01.COD  record 0 (9, 4)
 *
 * -- the same records tests/deploy.c's wave cases expect.  A run without those
 * two files would not fail a check, it would hang in fdps_wait_any_key, so
 * every firing case skips itself when they are not there.
 *
 * The line spoken after the deployment is real as well: the chapter text
 * pointer is aimed at a block whose every entry names one lone terminator, so
 * fdps_draw_text walks the entry, paints nothing and returns without needing a
 * font, a message panel or mode 13h.  The block runs to 0x15 entries because
 * PUSH 0x14 at 000383dc asks for the last of them.
 *
 * The unit array is malloc'd rather than staged into a static block, because a
 * deployment reallocs it: a static block handed to realloc is not a smaller
 * fixture, it is undefined behaviour.  It is never freed, for the same reason
 * tests/deploy.c does not free it -- the block moves under the global on every
 * deployment.
 */

/* The slot of data_fdps_map_cell_event_triggered_flags the handler latches,
   MOV byte ptr [0x000640e8],0x1 against the array base 0x000640d8.  Same slot
   the chapter 20 cases above hold up to prove that handler ignores it. */
#define CH21_LATCH_SLOT 0x10

/* The wave key the handler asks for, PUSH 0x1 at 000383b9.  The cases stage a
   record either side of it so a record tagged 0 or 2 is present to be left
   behind. */
#define CH21_WAVE 1

/* The side the gate at 000383af admits, and the three that have to be refused:
   the enemy, the guest -- which the sibling ambushes of chapters 10 and 19 let
   through -- and a byte with its top bit set, which says the compare is over
   the whole unsigned byte rather than a sign test. */
#define CH21_PLAYER_SIDE 2
#define CH21_ENEMY_SIDE 0
#define CH21_GUEST_SIDE 1
#define CH21_HIGH_SIDE 0xff

/* A map big enough for MAP00.COD's records, which reach (22, 12).  The tile
   search fdps_deploy_unit runs on a place_exact of 0 walks the whole grid, and
   the marking pass writes at a unit's own tile with no bound, so the grid has
   to cover the coordinates the real placement file names. */
#define CH21_GRID_W 32
#define CH21_GRID_H 16

/* Where the deployment records start in the MAP%02d.DAT block and where its
   header keeps their count: ADD EAX,0x83 at 00023463 and MOV AL,byte ptr
   [EAX+0x2] at 000238bb. */
#define CH21_SPAWN_TABLE_RECORD_BASE 0x83
#define CH21_SPAWN_TABLE_COUNT_OFFSET 2

/* The scene-layer offsets fdps_map_load_tile_info reads through: the tile
   map's width word at +7 with its 16-bit ids from +0xb, the attribute table's
   4-byte rows from +0x11, and the event layer's width at +7 with its cells
   from +0x10 (src/maptile.c). */
#define CH21_TILE_MAP_WIDTH_OFFSET 7
#define CH21_TILE_MAP_IDS_OFFSET 0xb
#define CH21_TILE_ATTR_ROWS_OFFSET 0x11
#define CH21_EVENT_LAYER_WIDTH_OFFSET 7
#define CH21_EVENT_LAYER_CELLS_OFFSET 0x10

/* A terrain code the deployment search accepts and one it rejects, either side
   of the CMP EAX,0x5 / JGE at 00023404. */
#define CH21_TERRAIN_WALKABLE 1
#define CH21_TERRAIN_BLOCKED 5

#define CH21_TILE_ATTR_ROWS 16
#define CH21_CHAR_TABLE_ROWS 8
#define CH21_ENEMY_TABLE_ROWS 8

/* Every item id is a valid index into the staged item table, 0xff included:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define CH21_ITEM_TABLE_ROWS 256
#define CH21_ITEM_ID_NONE 0xff

/* The unit array's stride, as the deployment path allocates it: PUSH 0x50 at
   000232cc, and the stride fdps_get_unit_record multiplies the index by. */
#define CH21_UNIT_STRIDE 0x50

/* The chapter text block the two ambush lines are spoken from: 0x16 entries,
   because PUSH 0x14 at 000383dc and PUSH 0x15 at 0003845f between them ask for
   the last of them, each pointing at the same lone terminator so that a draw
   walks it, paints nothing and returns. */
#define CH21_TEXT_IDS 0x16
#define CH21_TEXT_EMPTY_AT 0x40
#define CH21_TEXT_BLOCK_BYTES (CH21_TEXT_EMPTY_AT + 2)
#define CH21_TEXT_END (-1)

static unsigned char ch21_grid[4 + CH21_GRID_W * CH21_GRID_H * 2];
static unsigned char ch21_spawn_table[CH21_SPAWN_TABLE_RECORD_BASE + 8 * 0x1a];
static unsigned char ch21_tile_map[CH21_TILE_MAP_IDS_OFFSET +
                                   CH21_GRID_W * CH21_GRID_H * 2];
static unsigned char ch21_tile_attr[CH21_TILE_ATTR_ROWS_OFFSET +
                                    CH21_TILE_ATTR_ROWS * 4];
static unsigned char ch21_event_layer[CH21_EVENT_LAYER_CELLS_OFFSET +
                                      CH21_GRID_W * CH21_GRID_H];
static struct fdps_character_base_record ch21_char_base[CH21_CHAR_TABLE_ROWS];
static struct fdps_character_growth ch21_growth[CH21_CHAR_TABLE_ROWS];
static struct fdps_enemy_data ch21_enemy[CH21_ENEMY_TABLE_ROWS];
static struct fdps_item_effect ch21_items[CH21_ITEM_TABLE_ROWS];
static unsigned char ch21_text_block[CH21_TEXT_BLOCK_BYTES];

static int ch21_files_checked = 0;
static int ch21_files_ready = 0;

/* ICON.CEL is read by fdps_cache_cel_sprite_group, which takes a fixed
   0x2970-byte bite out of the sheet's offset table, so a shorter file is one
   that reader runs off the end of rather than a smaller fixture.  FIELD.VFS
   cannot be stood in for at all: a missing container sends fdps_deploy_wave
   into fdps_wait_any_key and a missing member into exit(1). */
static void ch21_ensure_game_files(void)
{
    FILE *fp;
    long size;

    if (ch21_files_checked) {
        return;
    }
    ch21_files_checked = 1;

    fp = fopen("ICON.CEL", "rb");
    if (fp == NULL) {
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    if (size < (long) (15 + 0x2970)) {
        return;
    }

    fp = fopen("FIELD.VFS", "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);
    ch21_files_ready = 1;
}

static struct fdps_char_spawn_record *ch21_spawn_at(int index)
{
    return (struct fdps_char_spawn_record *)
           (ch21_spawn_table + CH21_SPAWN_TABLE_RECORD_BASE) + index;
}

/* One deployment record with nothing equipped and nothing carried, tagged with
   the wave it belongs to.  The side the record carries is what the unit it
   deploys gets and has nothing to do with the side the handler tests. */
static void ch21_set_spawn(int index, int char_id, int wave_no)
{
    ch21_spawn_at(index)->char_id = (unsigned char) char_id;
    ch21_spawn_at(index)->level = 1;
    ch21_spawn_at(index)->side = 0;
    ch21_spawn_at(index)->equipped_item_0 = CH21_ITEM_ID_NONE;
    ch21_spawn_at(index)->equipped_item_1 = CH21_ITEM_ID_NONE;
    ch21_spawn_at(index)->carried_items[0] = CH21_ITEM_ID_NONE;
    ch21_spawn_at(index)->carried_items[1] = CH21_ITEM_ID_NONE;
    ch21_spawn_at(index)->carried_items[2] = CH21_ITEM_ID_NONE;
    ch21_spawn_at(index)->carried_items[3] = CH21_ITEM_ID_NONE;
    ch21_spawn_at(index)->carried_items[4] = CH21_ITEM_ID_NONE;
    ch21_spawn_at(index)->carried_items[5] = CH21_ITEM_ID_NONE;
    ch21_spawn_at(index)->wave_no = (unsigned char) wave_no;
}

static void ch21_set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch21_tile_attr + CH21_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

/* The cell of the tile map at (tile_x, tile_y), so one tile can be given an id
   whose attribute row says the deployment search must not use it. */
static void ch21_set_tile_id(int tile_x, int tile_y, int tile_id)
{
    short *tile_ids;

    tile_ids = (short *) (ch21_tile_map + CH21_TILE_MAP_IDS_OFFSET);
    tile_ids[tile_y * CH21_GRID_W + tile_x] = (short) tile_id;
}

static struct fdps_unit_record *ch21_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH21_UNIT_STRIDE);
}

/* A blank walkable map with unit_count units already on it, the latch down,
   the chapter global on map 0 and the text block in place.  Each staged unit
   stands on its own tile along the top row, clear of every placement record
   these cases read back.  The sides are left at 0 and each case sets the one
   it is about. */
static void ch21_stage(int unit_count)
{
    int i;

    memset(ch21_grid, 0, sizeof(ch21_grid));
    memset(ch21_spawn_table, 0, sizeof(ch21_spawn_table));
    memset(ch21_tile_map, 0, sizeof(ch21_tile_map));
    memset(ch21_tile_attr, 0, sizeof(ch21_tile_attr));
    memset(ch21_event_layer, 0, sizeof(ch21_event_layer));
    memset(ch21_char_base, 0, sizeof(ch21_char_base));
    memset(ch21_growth, 0, sizeof(ch21_growth));
    memset(ch21_enemy, 0, sizeof(ch21_enemy));
    memset(ch21_items, 0, sizeof(ch21_items));

    memset(ch21_text_block, 0, (size_t) CH21_TEXT_BLOCK_BYTES);
    *(short *) (ch21_text_block + CH21_TEXT_EMPTY_AT) = (short) CH21_TEXT_END;
    for (i = 0; i < CH21_TEXT_IDS; i++) {
        *(short *) (ch21_text_block + i * 2) = (short) CH21_TEXT_EMPTY_AT;
    }

    *(short *) ch21_grid = (short) CH21_GRID_W;
    *(short *) (ch21_grid + 2) = (short) CH21_GRID_H;

    *(short *) (ch21_tile_map + CH21_TILE_MAP_WIDTH_OFFSET) =
        (short) CH21_GRID_W;
    for (i = 0; i < CH21_TILE_ATTR_ROWS; i++) {
        ch21_set_terrain(i, CH21_TERRAIN_WALKABLE);
    }

    *(short *) (ch21_event_layer + CH21_EVENT_LAYER_WIDTH_OFFSET) =
        (short) CH21_GRID_W;

    data_fdps_battle_move_grid_ptr = ch21_grid;
    data_fdps_tile_event_data_table_ptr = ch21_spawn_table;
    data_fdps_scene_layer_tile_map_ptrs[0] = ch21_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ch21_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = ch21_event_layer;
    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) ch21_char_base;
    data_fdps_battle_character_growth_table_ptr = (unsigned char *) ch21_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch21_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch21_items;
    data_fdps_current_chapter_text_ptr = ch21_text_block;

    data_fdps_map_unit_array_ptr =
        (unsigned char *) malloc((size_t) (unit_count * CH21_UNIT_STRIDE));
    memset(data_fdps_map_unit_array_ptr, 0,
           (size_t) (unit_count * CH21_UNIT_STRIDE));
    data_fdps_map_unit_count = unit_count;
    for (i = 0; i < unit_count; i++) {
        ch21_unit(i)->pos_x = (unsigned char) i;
        ch21_unit(i)->pos_y = 0;
    }

    data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT] = 0;
    data_fdps_chapter_current_chapter_id = 0;
}

/* The byte the gate reads is record +6 and the record is 0x50 bytes, which is
   the stride the lookup multiplies by.  Every case below would agree with
   itself while addressing another byte if either were wrong. */
static void ch21_side_is_the_byte_at_record_six(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH21_UNIT_STRIDE);
}

/* The three sides that are not 2 are refused, and the guest side is the one
   that matters: the chapter 10 and chapter 19 ambushes admit it, and a rebuild
   that copied their side != 0 test here would let a guest spring this one.
   0xff is refused as well, which says the compare is over the whole unsigned
   byte and is not a sign test.  Nothing is deployed and the latch stays down,
   so no game file is needed for any of these. */
static void ch21_fires_for_the_player_side_only(void)
{
    static int refused_sides[3] = {CH21_ENEMY_SIDE, CH21_GUEST_SIDE,
                                   CH21_HIGH_SIDE};
    int i;

    for (i = 0; i < 3; i++) {
        ch21_stage(1);
        ch21_unit(0)->side = (unsigned char) refused_sides[i];
        ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 1;
        ch21_set_spawn(0, 5, CH21_WAVE);

        fdps_chapter_21_event_deploy_wave_1(0);

        CHECK_EQ(data_fdps_map_unit_count, 1);
        CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[
                     CH21_LATCH_SLOT], 0);
    }
}

/* The gate reads the record the argument names and not unit 0: units 0, 1 and
   3 are the enemy and only unit 2 is the player's, so a firing for 2 goes
   through and a firing for any of the others does not. */
static void ch21_reads_the_record_the_index_names(void)
{
    ch21_stage(4);
    ch21_unit(0)->side = CH21_ENEMY_SIDE;
    ch21_unit(1)->side = CH21_ENEMY_SIDE;
    ch21_unit(2)->side = CH21_PLAYER_SIDE;
    ch21_unit(3)->side = CH21_ENEMY_SIDE;

    fdps_chapter_21_event_deploy_wave_1(0);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT],
             0);
    fdps_chapter_21_event_deploy_wave_1(3);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT],
             0);

    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 1;
    ch21_set_spawn(0, 5, CH21_WAVE);

    fdps_chapter_21_event_deploy_wave_1(2);

    CHECK_EQ(data_fdps_map_unit_count, 5);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT],
             1);
}

/* The latch is tested against 0 and not against 1, so any non-zero value in
   the slot blocks the body -- and it blocks it for a unit that would otherwise
   qualify.  The slot is left exactly as it was found. */
static void ch21_any_non_zero_latch_blocks(void)
{
    static int raised_values[3] = {1, 2, 0xff};
    int i;

    for (i = 0; i < 3; i++) {
        ch21_stage(1);
        ch21_unit(0)->side = CH21_PLAYER_SIDE;
        ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 1;
        ch21_set_spawn(0, 5, CH21_WAVE);
        data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT] =
            (unsigned char) raised_values[i];

        fdps_chapter_21_event_deploy_wave_1(0);

        CHECK_EQ(data_fdps_map_unit_count, 1);
        CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[
                     CH21_LATCH_SLOT], raised_values[i]);
    }
}

/* The wave key is 1: of three records tagged 0, 1 and 2 only the middle one is
   deployed, it is appended behind the unit already on the map, and it lands on
   MAP00.COD record 1's tile (22, 12).  The latch comes up to exactly 1. */
static void ch21_deploys_the_records_tagged_wave_1(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21_stage(1);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;
    ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch21_set_spawn(0, 5, CH21_WAVE - 1);
    ch21_set_spawn(1, 6, CH21_WAVE);
    ch21_set_spawn(2, 7, CH21_WAVE + 1);

    fdps_chapter_21_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch21_unit(1)->char_id, 6);
    CHECK_EQ((int) ch21_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch21_unit(1)->pos_y, 12);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT],
             1);
}

/* The map number comes from data_fdps_chapter_current_chapter_id read at the
   call site and not from anything the handler holds: with the global on 1 the
   deployment reads MAP01.COD, whose record 0 is (9, 4), where map 0's record 0
   is (18, 0). */
static void ch21_map_number_comes_from_the_chapter_global(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21_stage(1);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;
    ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 1;
    ch21_set_spawn(0, 5, CH21_WAVE);
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_21_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch21_unit(1)->pos_x, 9);
    CHECK_EQ((int) ch21_unit(1)->pos_y, 4);
}

/* The placement flag is 0, so the arrival goes on the nearest free walkable
   tile to its record's coordinates rather than on the coordinates themselves:
   with (22, 12) made unwalkable the unit lands at (22, 13) instead of standing
   on the blocked tile, which is what a place_exact of 1 would have done. */
static void ch21_places_on_the_nearest_free_tile(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21_stage(1);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;
    ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 2;
    ch21_set_spawn(0, 5, CH21_WAVE - 1);
    ch21_set_spawn(1, 6, CH21_WAVE);
    ch21_set_tile_id(22, 12, 1);
    ch21_set_terrain(1, CH21_TERRAIN_BLOCKED);

    fdps_chapter_21_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch21_unit(1)->char_id, 6);
    CHECK_EQ((int) ch21_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch21_unit(1)->pos_y, 13);
}

/* The latch the handler raises is the one it reads, so a second firing on the
   same map deploys nothing more -- and the latch it raises is the shared array
   element rather than a private static, which is what the chapter reset clears
   and the save file carries.  Clearing that element by hand re-arms the event,
   which a static could not be made to do. */
static void ch21_fires_once_and_the_latch_is_the_shared_slot(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21_stage(1);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;
    ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 2;
    ch21_set_spawn(0, 5, CH21_WAVE);
    ch21_set_spawn(1, 6, CH21_WAVE);

    fdps_chapter_21_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT],
             1);

    fdps_chapter_21_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);

    data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT] = 0;
    fdps_chapter_21_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 5);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT],
             1);
}

/* ---------------------------------------------------------------------- */

/* Chapter 21's second ambush at 00038400, from here down.
 *
 * It shares both gates and the whole deployment shape with the wave-1 handler
 * above, so the cases here are about the three things that differ, and the
 * fixture above is reused for everything else.
 *
 * The latch is a DIFFERENT byte -- 0x000640e9 against the array base
 * 0x000640d8, so element 0x11 where the handler above uses 0x10.  Both tiles
 * are armed on the same map at the same time, so a rebuild that copied the
 * neighbour's slot would let the first unit to trip either tile disarm both:
 * that is asserted from both sides, by firing with the wave-1 slot already up
 * and by watching this handler leave that slot alone.
 *
 * The tail is the inline expansion of fdps_object_set_field34_low_nibble_range
 * over unit indices 0x0b..0x50 INCLUSIVE, and both ends of that range are
 * asserted from both sides, because the idiomatic half-open loop is wrong at
 * exactly one unit -- the map's last -- and a unit left in mode 2 holds
 * position instead of advancing, which nothing else in a run would report.  The
 * merge keeps the high nibble, so a case walks bytes whose high nibbles differ.
 *
 * Every firing case has to stage at least 0x52 units, because the loop resolves
 * every index in its range through fdps_get_unit_record and nothing bounds it
 * against data_fdps_map_unit_count: a shorter array is not a smaller fixture,
 * it is writes past the end of the block.  ch21b_stage lays those units out
 * eight to a row so the placement records these cases read back -- MAP00.COD
 * record 1 at (22, 12) and MAP01.COD record 0 at (9, 4) -- stay clear of them.
 *
 * A firing runs fdps_deploy_wave for real, which opens ICON.CEL and FIELD.VFS
 * for itself, so each firing case skips itself when those are not staged;
 * without them the handler would not fail a check, it would hang in
 * fdps_wait_any_key.
 */

/* The slot this handler latches, MOV byte ptr [0x000640e9],0x1 at 0003846f
   against the array base 0x000640d8 -- one past the slot the wave-1 cases
   above use. */
#define CH21B_LATCH_SLOT 0x11

/* The wave key the handler asks for, PUSH 0x2 at 0003843c.  The cases stage a
   record either side of it so a record tagged 1 or 3 is present to be left
   behind. */
#define CH21B_WAVE 2

/* The inclusive ends of the advance, the 0xb at 00038476 and the 0x50 at
   0003847d, and the two indices either side of them that have to come through
   untouched. */
#define CH21B_FIRST_ADVANCED 0x0b
#define CH21B_LAST_ADVANCED 0x50
#define CH21B_BELOW_FIRST (CH21B_FIRST_ADVANCED - 1)
#define CH21B_ABOVE_LAST (CH21B_LAST_ADVANCED + 1)

/* One more unit than the last index the loop writes, which is the shortest
   array a firing may be given. */
#define CH21B_STAGED_UNITS (CH21B_ABOVE_LAST + 1)

/* How wide ch21b_stage lays the staged units out.  Eight columns keeps every
   one of them at x < 8, clear of both placement records these cases read
   back, and keeps the tallest column inside the grid's 16 rows. */
#define CH21B_STAGE_COLUMNS 8

/* A behaviour byte in the state MAP20.DAT deploys the chapter's enemies in --
   mode 2, holding position -- with two high-nibble flag bits set, and what the
   merge must leave behind: the same high nibble over mode 0. */
#define CH21B_HOLDING 0xa2
#define CH21B_ADVANCING 0xa0

/* Four more behaviour bytes whose high nibbles differ, and what the merge
   makes of each: the mask is 0xf0 and the value merged in is 0. */
#define CH21B_NIBBLE_IN_0 0x00
#define CH21B_NIBBLE_IN_1 0xff
#define CH21B_NIBBLE_IN_2 0x42
#define CH21B_NIBBLE_IN_3 0x0f
#define CH21B_NIBBLE_OUT_0 0x00
#define CH21B_NIBBLE_OUT_1 0xf0
#define CH21B_NIBBLE_OUT_2 0x40
#define CH21B_NIBBLE_OUT_3 0x00

/* The wave-1 fixture with the units re-laid onto a block of columns, this
   handler's own latch put down, and every unit in the advance range holding
   position, which is the state the chapter's map deploys them in. */
static void ch21b_stage(int unit_count)
{
    int i;

    ch21_stage(unit_count);
    for (i = 0; i < unit_count; i++) {
        ch21_unit(i)->pos_x = (unsigned char) (i % CH21B_STAGE_COLUMNS);
        ch21_unit(i)->pos_y = (unsigned char) (i / CH21B_STAGE_COLUMNS);
        ch21_unit(i)->ai_behavior = CH21B_HOLDING;
    }
    data_fdps_map_cell_event_triggered_flags[CH21B_LATCH_SLOT] = 0;
}

/* The gate admits side 2 alone, exactly as the wave-1 gate does: the enemy,
   the guest and a byte with its top bit set are all refused, and a refusal
   leaves the latch down and the garrison holding position.  Nothing is
   deployed on any of these, so no game file is needed. */
static void ch21b_fires_for_the_player_side_only(void)
{
    static int refused_sides[3] = {CH21_ENEMY_SIDE, CH21_GUEST_SIDE,
                                   CH21_HIGH_SIDE};
    int i;

    for (i = 0; i < 3; i++) {
        ch21b_stage(CH21B_STAGED_UNITS);
        ch21_unit(0)->side = (unsigned char) refused_sides[i];
        ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 1;
        ch21_set_spawn(0, 5, CH21B_WAVE);

        fdps_chapter_21_event_deploy_wave_2(0);

        CHECK_EQ(data_fdps_map_unit_count, CH21B_STAGED_UNITS);
        CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[
                     CH21B_LATCH_SLOT], 0);
        CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior,
                 CH21B_HOLDING);
    }
}

/* The latch is tested against 0 and not against 1, so any non-zero value in
   this handler's own slot blocks the body for a unit that would otherwise
   qualify, and the slot is left exactly as it was found. */
static void ch21b_any_non_zero_latch_blocks(void)
{
    static int raised_values[3] = {1, 2, 0xff};
    int i;

    for (i = 0; i < 3; i++) {
        ch21b_stage(CH21B_STAGED_UNITS);
        ch21_unit(0)->side = CH21_PLAYER_SIDE;
        ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 1;
        ch21_set_spawn(0, 5, CH21B_WAVE);
        data_fdps_map_cell_event_triggered_flags[CH21B_LATCH_SLOT] =
            (unsigned char) raised_values[i];

        fdps_chapter_21_event_deploy_wave_2(0);

        CHECK_EQ(data_fdps_map_unit_count, CH21B_STAGED_UNITS);
        CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[
                     CH21B_LATCH_SLOT], raised_values[i]);
        CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior,
                 CH21B_HOLDING);
    }
}

/* The two ambushes on this map do NOT share a latch: with the wave-1 slot
   already up this handler still fires, and it leaves that slot alone while
   raising its own.  A rebuild that copied the neighbour's slot number would
   fail both halves of this. */
static void ch21b_latch_is_not_the_wave_1_slot(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21b_stage(CH21B_STAGED_UNITS);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;
    data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT] = 1;

    fdps_chapter_21_event_deploy_wave_2(0);

    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21B_LATCH_SLOT],
             1);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21_LATCH_SLOT],
             1);
    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior,
             CH21B_ADVANCING);
}

/* The wave key is 2: of three records tagged 1, 2 and 3 only the middle one is
   deployed, it is appended behind the units already on the map, and it lands
   on MAP00.COD record 1's tile (22, 12).  The latch comes up to exactly 1. */
static void ch21b_deploys_the_records_tagged_wave_2(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21b_stage(CH21B_STAGED_UNITS);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;
    ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch21_set_spawn(0, 5, CH21B_WAVE - 1);
    ch21_set_spawn(1, 6, CH21B_WAVE);
    ch21_set_spawn(2, 7, CH21B_WAVE + 1);

    fdps_chapter_21_event_deploy_wave_2(0);

    CHECK_EQ(data_fdps_map_unit_count, CH21B_STAGED_UNITS + 1);
    CHECK_EQ((int) ch21_unit(CH21B_STAGED_UNITS)->char_id, 6);
    CHECK_EQ((int) ch21_unit(CH21B_STAGED_UNITS)->pos_x, 22);
    CHECK_EQ((int) ch21_unit(CH21B_STAGED_UNITS)->pos_y, 12);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21B_LATCH_SLOT],
             1);
}

/* The map number comes from data_fdps_chapter_current_chapter_id read at the
   call site and not from anything the handler holds: with the global on 1 the
   deployment reads MAP01.COD, whose record 0 is (9, 4), where map 0's record 0
   is (18, 0). */
static void ch21b_map_number_comes_from_the_chapter_global(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21b_stage(CH21B_STAGED_UNITS);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;
    ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 1;
    ch21_set_spawn(0, 5, CH21B_WAVE);
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_21_event_deploy_wave_2(0);

    CHECK_EQ(data_fdps_map_unit_count, CH21B_STAGED_UNITS + 1);
    CHECK_EQ((int) ch21_unit(CH21B_STAGED_UNITS)->pos_x, 9);
    CHECK_EQ((int) ch21_unit(CH21B_STAGED_UNITS)->pos_y, 4);
}

/* The placement flag is 0, so the arrival goes on the nearest free walkable
   tile to its record's coordinates rather than on the coordinates themselves:
   with (22, 12) made unwalkable the unit lands at (22, 13) instead of standing
   on the blocked tile, which is what a place_exact of 1 would have done. */
static void ch21b_places_on_the_nearest_free_tile(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21b_stage(CH21B_STAGED_UNITS);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;
    ch21_spawn_table[CH21_SPAWN_TABLE_COUNT_OFFSET] = 2;
    ch21_set_spawn(0, 5, CH21B_WAVE - 1);
    ch21_set_spawn(1, 6, CH21B_WAVE);
    ch21_set_tile_id(22, 12, 1);
    ch21_set_terrain(1, CH21_TERRAIN_BLOCKED);

    fdps_chapter_21_event_deploy_wave_2(0);

    CHECK_EQ(data_fdps_map_unit_count, CH21B_STAGED_UNITS + 1);
    CHECK_EQ((int) ch21_unit(CH21B_STAGED_UNITS)->char_id, 6);
    CHECK_EQ((int) ch21_unit(CH21B_STAGED_UNITS)->pos_x, 22);
    CHECK_EQ((int) ch21_unit(CH21B_STAGED_UNITS)->pos_y, 13);
}

/* The advance covers unit indices 0x0b through 0x50 and both ends are
   inclusive.  0x0a is the last party member and comes through holding
   position, 0x0b is the first unit moved, 0x50 is the LAST unit moved -- the
   half-open loop every rewrite reaches for leaves that one holding position --
   and 0x51 is past the end and untouched.  Nothing is deployed here, so the
   array the loop walks is exactly the one staged. */
static void ch21b_advance_range_is_inclusive_at_both_ends(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21b_stage(CH21B_STAGED_UNITS);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;

    fdps_chapter_21_event_deploy_wave_2(0);

    CHECK_EQ((int) ch21_unit(CH21B_BELOW_FIRST)->ai_behavior, CH21B_HOLDING);
    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior,
             CH21B_ADVANCING);
    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED + 1)->ai_behavior,
             CH21B_ADVANCING);
    CHECK_EQ((int) ch21_unit(CH21B_LAST_ADVANCED - 1)->ai_behavior,
             CH21B_ADVANCING);
    CHECK_EQ((int) ch21_unit(CH21B_LAST_ADVANCED)->ai_behavior,
             CH21B_ADVANCING);
    CHECK_EQ((int) ch21_unit(CH21B_ABOVE_LAST)->ai_behavior, CH21B_HOLDING);
    CHECK_EQ((int) ch21_unit(0)->ai_behavior, CH21B_HOLDING);
}

/* The store is a read-modify-write of the low nibble alone: the byte is masked
   with 0xf0 and 0 is merged in, so the two AI flag bits the scorers read out of
   the high nibble survive.  Writing the mode whole would flatten all four
   bytes to 0. */
static void ch21b_advance_preserves_the_high_nibble(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21b_stage(CH21B_STAGED_UNITS);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;
    ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior = CH21B_NIBBLE_IN_0;
    ch21_unit(CH21B_FIRST_ADVANCED + 1)->ai_behavior = CH21B_NIBBLE_IN_1;
    ch21_unit(CH21B_FIRST_ADVANCED + 2)->ai_behavior = CH21B_NIBBLE_IN_2;
    ch21_unit(CH21B_FIRST_ADVANCED + 3)->ai_behavior = CH21B_NIBBLE_IN_3;

    fdps_chapter_21_event_deploy_wave_2(0);

    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior,
             CH21B_NIBBLE_OUT_0);
    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED + 1)->ai_behavior,
             CH21B_NIBBLE_OUT_1);
    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED + 2)->ai_behavior,
             CH21B_NIBBLE_OUT_2);
    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED + 3)->ai_behavior,
             CH21B_NIBBLE_OUT_3);
}

/* The gate reads the record the argument names and not unit 0, and it is the
   record's byte at +6: units 0, 1 and 3 are the enemy and only unit 2 is the
   player's, so a firing for 2 goes through and a firing for any of the others
   does not. */
static void ch21b_reads_the_record_the_index_names(void)
{
    ch21b_stage(CH21B_STAGED_UNITS);
    ch21_unit(0)->side = CH21_ENEMY_SIDE;
    ch21_unit(1)->side = CH21_ENEMY_SIDE;
    ch21_unit(2)->side = CH21_PLAYER_SIDE;
    ch21_unit(3)->side = CH21_ENEMY_SIDE;

    fdps_chapter_21_event_deploy_wave_2(0);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21B_LATCH_SLOT],
             0);
    fdps_chapter_21_event_deploy_wave_2(3);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21B_LATCH_SLOT],
             0);

    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }

    fdps_chapter_21_event_deploy_wave_2(2);

    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21B_LATCH_SLOT],
             1);
    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior,
             CH21B_ADVANCING);
}

/* The latch the handler raises is the one it reads, so a second firing on the
   same map does nothing more -- and the latch it raises is the shared array
   element rather than a private static, which is what the chapter reset clears
   and the save file carries.  Clearing that element by hand re-arms the event,
   which a static could not be made to do. */
static void ch21b_fires_once_and_the_latch_is_the_shared_slot(void)
{
    ch21_ensure_game_files();
    if (!ch21_files_ready) {
        return;
    }
    ch21b_stage(CH21B_STAGED_UNITS);
    ch21_unit(0)->side = CH21_PLAYER_SIDE;

    fdps_chapter_21_event_deploy_wave_2(0);
    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior,
             CH21B_ADVANCING);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21B_LATCH_SLOT],
             1);

    ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior = CH21B_HOLDING;
    fdps_chapter_21_event_deploy_wave_2(0);
    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior,
             CH21B_HOLDING);

    data_fdps_map_cell_event_triggered_flags[CH21B_LATCH_SLOT] = 0;
    fdps_chapter_21_event_deploy_wave_2(0);
    CHECK_EQ((int) ch21_unit(CH21B_FIRST_ADVANCED)->ai_behavior,
             CH21B_ADVANCING);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH21B_LATCH_SLOT],
             1);
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
    RUN_TEST(ch21_side_is_the_byte_at_record_six);
    RUN_TEST(ch21_fires_for_the_player_side_only);
    RUN_TEST(ch21_reads_the_record_the_index_names);
    RUN_TEST(ch21_any_non_zero_latch_blocks);
    RUN_TEST(ch21_deploys_the_records_tagged_wave_1);
    RUN_TEST(ch21_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch21_places_on_the_nearest_free_tile);
    RUN_TEST(ch21_fires_once_and_the_latch_is_the_shared_slot);
    RUN_TEST(ch21b_fires_for_the_player_side_only);
    RUN_TEST(ch21b_any_non_zero_latch_blocks);
    RUN_TEST(ch21b_reads_the_record_the_index_names);
    RUN_TEST(ch21b_latch_is_not_the_wave_1_slot);
    RUN_TEST(ch21b_deploys_the_records_tagged_wave_2);
    RUN_TEST(ch21b_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch21b_places_on_the_nearest_free_tile);
    RUN_TEST(ch21b_advance_range_is_inclusive_at_both_ends);
    RUN_TEST(ch21b_advance_preserves_the_high_nibble);
    RUN_TEST(ch21b_fires_once_and_the_latch_is_the_shared_slot);
}
