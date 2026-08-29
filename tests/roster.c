/* tests/roster.c -- cover for src/roster.c.
 *
 * Expected values come from the assembly of fdps_roster_recompute_combat_stats
 * @ 00023ac0 and from ticket 17's struct fdps_unit_record and struct
 * fdps_item_effect in src/fdpstype.h.  None of them is read off the emitted C.
 * The four facts the cases below are aimed at are all in the body:
 *
 *   00023ad8  IMUL EAX,[EBP-0x2c],0x50 / MOV EDX,[0x00064108] / ADD EDX,EAX
 *             -- the record is base + index * 0x50, unbiased and unchecked.
 *   00023af0  MOVSX from +0x37, +0x39 and +0x3e seeds attack, defense and BOTH
 *             hit and evade; 00023b0b copies the one dexterity word into the
 *             second of them.
 *   00023b18  the scan runs entry 0..7 over the 2-byte entries at +0x0a, tests
 *             AND AL,0x40 on the flag byte and zero-extends the id byte.
 *   00023b60  the item modifiers go +0x01 to attack, +0x05 to defense, +0x03
 *             to hit and +0x07 to evade -- an order that does not match the
 *             order of the four destination fields at +0x48..+0x4e.
 *
 * The lookup runs through the real fdps_get_item_record in src/table.c, so the
 * item fixture is staged at that accessor's own 0x17 stride.  Neither array is
 * a loaded file here: the roster is a heap block for the whole run and the item
 * table is a block fdps_load_data_tables unpacks out of the VFS, so staged byte
 * buffers are exactly the shape the two globals hold at run time.  Nothing
 * below asserts what a real roster or a real ITEM.DAT contains -- every byte
 * read back is one this file wrote -- and both globals are put back to null on
 * the way out, since ticket 23 has yet to define them.
 *
 * Records and modifiers are written a byte at a time, little-endian, because
 * every stat word in both layouts sits at an ODD offset and this fixture must
 * not assume the layout it is checking.
 */
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "roster.h"

/* The strides the two IMULs state, and the capacities the blocks are sized
   for: the roster block is the 0xa00 bytes allocated at 000296b8, and ITEM.DAT
   holds 251 records (resource_info/data_tables.md). */
#define ROSTER_STRIDE 0x50
#define ROSTER_CAPACITY 32
#define ITEM_STRIDE 0x17
#define ITEM_COUNT 251

/* The item block is sized for every id a byte can hold and not for the 251
   records the file has, because the id arrives zero-extended out of an
   inventory entry: ids 251..255 are reachable and their records land past the
   end of the real table, which is the behaviour the id case below is about. */
#define ITEM_IMAGE_RECORDS 257

/* Field offsets inside a roster record, from struct fdps_unit_record. */
#define OFF_INVENTORY 0x0a
#define OFF_STATUS_TIMERS 0x22
#define OFF_AP_BASE 0x37
#define OFF_DP_BASE 0x39
#define OFF_DX_BASE 0x3e
#define OFF_HP_CURRENT 0x40
#define OFF_HP_MAX 0x42
#define OFF_AP 0x48
#define OFF_DP 0x4a
#define OFF_HIT 0x4c
#define OFF_EV 0x4e

/* Field offsets inside an item record, from struct fdps_item_effect. */
#define ITEM_OFF_AP 0x01
#define ITEM_OFF_HIT 0x03
#define ITEM_OFF_DP 0x05
#define ITEM_OFF_EV 0x07

/* One spare record behind each block, so a case that steps off the end lands
   on real storage rather than past the buffer. */
static unsigned char roster_image[(ROSTER_CAPACITY + 1) * ROSTER_STRIDE];
static unsigned char item_image[ITEM_IMAGE_RECORDS * ITEM_STRIDE];

static void put_word(unsigned char *field, int value)
{
    field[0] = (unsigned char) (value & 0xff);
    field[1] = (unsigned char) ((value >> 8) & 0xff);
}

/* Reads a stat field back the way the record stores it: two bytes,
   little-endian, interpreted signed.  Signed because every one of these fields
   is a short and a negative total is a case below. */
static short get_word(unsigned char *field)
{
    return (short) (unsigned short) (field[0] | (field[1] << 8));
}

static unsigned char *member_at(int roster_index)
{
    return roster_image + roster_index * ROSTER_STRIDE;
}

static unsigned char *item_at(int item_id)
{
    return item_image + item_id * ITEM_STRIDE;
}

/* Both blocks go back to all-zero between cases, so a leftover equipped entry
   from an earlier case cannot make a later one pass. */
static void clear_fixture(void)
{
    int byte_index;

    for (byte_index = 0;
         byte_index < (ROSTER_CAPACITY + 1) * ROSTER_STRIDE;
         byte_index++) {
        roster_image[byte_index] = 0;
    }
    for (byte_index = 0;
         byte_index < ITEM_IMAGE_RECORDS * ITEM_STRIDE;
         byte_index++) {
        item_image[byte_index] = 0;
    }
    data_fdps_roster_array_ptr = roster_image;
    data_fdps_item_effect_table_ptr = item_image;
}

static void stage_member(int roster_index, int ap_base, int dp_base,
                         int dx_base)
{
    unsigned char *record;

    record = member_at(roster_index);
    put_word(record + OFF_AP_BASE, ap_base);
    put_word(record + OFF_DP_BASE, dp_base);
    put_word(record + OFF_DX_BASE, dx_base);
}

/* Fills one of the eight 2-byte inventory entries: flag byte first, id byte
   second, which is the pair the scan reads at +0x0a + 2 * entry. */
static void stage_entry(int roster_index, int entry_index,
                        int flags, int item_id)
{
    unsigned char *entry;

    entry = member_at(roster_index) + OFF_INVENTORY + entry_index * 2;
    entry[0] = (unsigned char) flags;
    entry[1] = (unsigned char) item_id;
}

static void stage_item(int item_id, int ap, int hit, int dp, int ev)
{
    unsigned char *record;

    record = item_at(item_id);
    put_word(record + ITEM_OFF_AP, ap);
    put_word(record + ITEM_OFF_HIT, hit);
    put_word(record + ITEM_OFF_DP, dp);
    put_word(record + ITEM_OFF_EV, ev);
}

static short stat_of(int roster_index, int offset)
{
    return get_word(member_at(roster_index) + offset);
}

/* With nothing equipped the four totals are the three base stats and nothing
   else, and attack and defense come from their own fields: MOVSX word ptr
   [EAX+0x37] into the attack accumulator, [EAX+0x39] into defense. */
static void the_base_stats_seed_the_totals(void)
{
    clear_fixture();
    stage_member(0, 40, 25, 17);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 40);
    CHECK_EQ(stat_of(0, OFF_DP), 25);
}

/* The rebuild note's first trap.  Both hit and evade are seeded from the one
   dexterity word at +0x3e; +0x40 is hp_current and is not a base stat for
   anything.  hp_current is staged to a value that would be visible if evade
   reached for it. */
static void hit_and_evade_share_the_one_dexterity_seed(void)
{
    clear_fixture();
    stage_member(0, 40, 25, 17);
    put_word(member_at(0) + OFF_HP_CURRENT, 99);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_HIT), 17);
    CHECK_EQ(stat_of(0, OFF_EV), 17);
}

/* Each item modifier reaches its own destination, and the mapping is the one
   the assembly makes rather than field order: item +0x01 to record +0x48,
   +0x05 to +0x4a, +0x03 to +0x4c and +0x07 to +0x4e.  The four modifiers are
   distinct so a swapped pair shows up as a wrong number and not a coincidence,
   and hit and evade differ so the two accumulators that started equal are seen
   to part company. */
static void an_equipped_item_adds_its_four_modifiers(void)
{
    clear_fixture();
    stage_member(0, 40, 25, 17);
    stage_item(3, 7, 5, 3, 9);
    stage_entry(0, 0, 0x40, 3);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 47);
    CHECK_EQ(stat_of(0, OFF_DP), 28);
    CHECK_EQ(stat_of(0, OFF_HIT), 22);
    CHECK_EQ(stat_of(0, OFF_EV), 26);
}

/* AND AL,0x40 tests that one bit.  An entry holding a carried item -- flags 0,
   the state fdps_roster_add_character leaves the six carried slots in -- and
   an entry flagged empty with 0x80 both contribute nothing, and an entry with
   0x40 set alongside other bits contributes. */
static void only_bit_0x40_makes_an_entry_count(void)
{
    clear_fixture();
    stage_member(0, 40, 25, 17);
    stage_item(3, 7, 5, 3, 9);
    stage_entry(0, 0, 0x00, 3);
    stage_entry(0, 1, 0x80, 3);
    stage_entry(0, 2, 0xbf, 3);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 40);

    clear_fixture();
    stage_member(0, 40, 25, 17);
    stage_item(3, 7, 5, 3, 9);
    stage_entry(0, 0, 0xc1, 3);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 47);
}

/* The rebuild note's second trap: the scan covers all eight entries at +0x0a,
   not the two equipment slots.  An equipped item in the last entry counts, and
   a ninth entry's worth of bytes -- +0x1a, which is spells_known_bitmap -- is
   past the end of the scan and counts for nothing however it is flagged. */
static void every_one_of_the_eight_entries_is_scanned(void)
{
    clear_fixture();
    stage_member(0, 40, 25, 17);
    stage_item(3, 7, 5, 3, 9);
    stage_entry(0, 7, 0x40, 3);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 47);

    clear_fixture();
    stage_member(0, 40, 25, 17);
    stage_item(3, 7, 5, 3, 9);
    stage_entry(0, 8, 0x40, 3);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 40);
}

/* Every equipped entry adds, so eight of the same item add eight times: the
   accumulators run across the whole loop and are not reset per entry. */
static void all_eight_equipped_entries_accumulate(void)
{
    int entry_index;

    clear_fixture();
    stage_member(0, 40, 25, 17);
    stage_item(3, 7, 5, 3, 9);
    for (entry_index = 0; entry_index < 8; entry_index++) {
        stage_entry(0, entry_index, 0x40, 3);
    }
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 40 + 8 * 7);
    CHECK_EQ(stat_of(0, OFF_DP), 25 + 8 * 3);
    CHECK_EQ(stat_of(0, OFF_HIT), 17 + 8 * 5);
    CHECK_EQ(stat_of(0, OFF_EV), 17 + 8 * 9);
}

/* Contract C.  Every value entering the accumulators arrives through MOVSX: a
   base stat word of 0xffce is -50 and an item modifier of 0xffff is -1, so a
   cursed item lowers the stat instead of adding 65535 to it. */
static void base_stats_and_modifiers_are_signed(void)
{
    clear_fixture();
    stage_member(0, 0xffce, 0xffff, 0xfffb);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), -50);
    CHECK_EQ(stat_of(0, OFF_DP), -1);
    CHECK_EQ(stat_of(0, OFF_HIT), -5);

    clear_fixture();
    stage_member(0, 40, 25, 17);
    stage_item(3, 0xfff6, 0xffff, 0xfffe, 0xfffd);
    stage_entry(0, 0, 0x40, 3);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 30);
    CHECK_EQ(stat_of(0, OFF_DP), 23);
    CHECK_EQ(stat_of(0, OFF_HIT), 16);
    CHECK_EQ(stat_of(0, OFF_EV), 14);
}

/* The id byte is zero-extended (XOR EAX,EAX / MOV AL,byte ptr [EDX+0x1]), so
   an id of 0xff is 255 and indexes forwards; it never becomes -1 and steps
   backwards off the front of the table.  Item 0xff is reachable in play and
   its record lands 92 bytes past the end of ITEM.DAT's 251 records, which is
   what makes the guide's "FF BUG item" read a different thing from one run to
   the next; the fixture stages storage there so the case measures the index
   and not the memory that happens to follow the real table. */
static void the_item_id_byte_is_unsigned(void)
{
    clear_fixture();
    stage_member(0, 40, 25, 17);
    stage_item(0xff, 7, 5, 3, 9);
    stage_entry(0, 0, 0x40, 0xff);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 47);
    CHECK_EQ(stat_of(0, OFF_EV), 26);
}

/* The store narrows and does not saturate: MOV word ptr [EDX+0x48],AX takes
   the low 16 bits of a total that was accumulated as a dword.  Two items of
   +20000 on top of a base of 30000 make 70000, which lands in the record as
   4464 and not as 32767; the mirror case, -30000 with two modifiers of -20000,
   lands as -4464.  This pins the store, not the accumulator's width: addition
   agrees on the low 16 bits whatever width it is done in, so no test can tell
   a short accumulator from the original's dword one.  The width is taken from
   00023b67's ADD dword ptr [EBP-0x14],EAX and stated in the emitted C. */
static void the_totals_are_truncated_by_the_word_store(void)
{
    clear_fixture();
    stage_member(0, 30000, 0, 0);
    stage_item(3, 20000, 0, 0, 0);
    stage_entry(0, 0, 0x40, 3);
    stage_entry(0, 1, 0x40, 3);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 4464);

    clear_fixture();
    stage_member(0, 0x8ad0, 0, 0);
    stage_item(3, 0xb1e0, 0, 0, 0);
    stage_entry(0, 0, 0x40, 3);
    stage_entry(0, 1, 0x40, 3);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), -4464);
}

/* The record is base + index * 0x50 with no bias, so recomputing member 2
   reads member 2's own bases and writes member 2's own stat fields.  Members 1
   and 3 are staged with equipment that would move their totals if the wrong
   record were addressed, and their stat fields must still be the zeros
   clear_fixture left. */
static void the_index_selects_the_record_by_eighty_byte_stride(void)
{
    clear_fixture();
    stage_member(1, 11, 11, 11);
    stage_member(2, 40, 25, 17);
    stage_member(3, 33, 33, 33);
    stage_item(3, 7, 5, 3, 9);
    stage_entry(1, 0, 0x40, 3);
    stage_entry(2, 0, 0x40, 3);
    stage_entry(3, 0, 0x40, 3);
    fdps_roster_recompute_combat_stats(2);
    CHECK_EQ(stat_of(2, OFF_AP), 47);
    CHECK_EQ(stat_of(2, OFF_EV), 26);
    CHECK_EQ(stat_of(1, OFF_AP), 0);
    CHECK_EQ(stat_of(3, OFF_AP), 0);
}

/* Nothing bounds roster_index: index 32 is one past the roster's 32 slots and
   the record is still formed and written, at exactly 32 * 0x50 from the base.
   Clamping it to the last slot, or refusing it, is the obvious guard and would
   not be this function. */
static void a_roster_index_past_the_end_is_not_clamped(void)
{
    clear_fixture();
    stage_member(ROSTER_CAPACITY, 40, 25, 17);
    fdps_roster_recompute_combat_stats(ROSTER_CAPACITY);
    CHECK_EQ(stat_of(ROSTER_CAPACITY, OFF_AP), 40);
    CHECK_EQ(stat_of(ROSTER_CAPACITY, OFF_HIT), 17);
}

/* The rebuild note's third trap.  The battle half adds 15 to the dexterity
   seed while the timer byte at +0x24 runs and scales attack and defense by
   1.15 while +0x22 and +0x23 run.  This body never reads those three bytes, so
   a record carrying them non-zero produces exactly the same four totals as one
   that does not. */
static void the_status_timers_are_not_applied(void)
{
    short attack_without_timers;
    short defense_without_timers;
    short hit_without_timers;
    short evade_without_timers;

    clear_fixture();
    stage_member(0, 40, 25, 17);
    fdps_roster_recompute_combat_stats(0);
    attack_without_timers = stat_of(0, OFF_AP);
    defense_without_timers = stat_of(0, OFF_DP);
    hit_without_timers = stat_of(0, OFF_HIT);
    evade_without_timers = stat_of(0, OFF_EV);

    clear_fixture();
    stage_member(0, 40, 25, 17);
    member_at(0)[OFF_STATUS_TIMERS + 0] = 9;
    member_at(0)[OFF_STATUS_TIMERS + 1] = 9;
    member_at(0)[OFF_STATUS_TIMERS + 2] = 9;
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), attack_without_timers);
    CHECK_EQ(stat_of(0, OFF_DP), defense_without_timers);
    CHECK_EQ(stat_of(0, OFF_HIT), hit_without_timers);
    CHECK_EQ(stat_of(0, OFF_EV), evade_without_timers);
}

/* The four word stores at +0x48..+0x4e are the only writes the body makes:
   nothing is written back into the base stats it read, and the two HP words
   between them and the dexterity seed are untouched. */
static void only_the_four_stat_words_are_written(void)
{
    clear_fixture();
    stage_member(0, 40, 25, 17);
    put_word(member_at(0) + OFF_HP_CURRENT, 55);
    put_word(member_at(0) + OFF_HP_MAX, 60);
    stage_item(3, 7, 5, 3, 9);
    stage_entry(0, 0, 0x40, 3);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(get_word(member_at(0) + OFF_AP_BASE), 40);
    CHECK_EQ(get_word(member_at(0) + OFF_DP_BASE), 25);
    CHECK_EQ(get_word(member_at(0) + OFF_DX_BASE), 17);
    CHECK_EQ(get_word(member_at(0) + OFF_HP_CURRENT), 55);
    CHECK_EQ(get_word(member_at(0) + OFF_HP_MAX), 60);
}

/* The stats are recomputed from the record, never accumulated onto what is
   already in the four fields: a second call on an unchanged record produces
   the same numbers, and stale values sitting in the fields beforehand are
   overwritten rather than added to. */
static void the_totals_are_recomputed_not_accumulated(void)
{
    clear_fixture();
    stage_member(0, 40, 25, 17);
    stage_item(3, 7, 5, 3, 9);
    stage_entry(0, 0, 0x40, 3);
    put_word(member_at(0) + OFF_AP, 500);
    put_word(member_at(0) + OFF_EV, 500);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 47);
    CHECK_EQ(stat_of(0, OFF_EV), 26);
    fdps_roster_recompute_combat_stats(0);
    CHECK_EQ(stat_of(0, OFF_AP), 47);
    CHECK_EQ(stat_of(0, OFF_EV), 26);
}

/* The base is read out of the global inside the body -- MOV EDX,dword ptr
   [0x00064108] at 00023adc -- so moving the roster between calls moves which
   block is recomputed.  The same record contents at a second base produce the
   result there and leave the first block alone. */
static void the_roster_base_is_read_on_every_call(void)
{
    clear_fixture();
    stage_member(4, 40, 25, 17);
    data_fdps_roster_array_ptr = roster_image + ROSTER_STRIDE;
    fdps_roster_recompute_combat_stats(3);
    CHECK_EQ(stat_of(4, OFF_AP), 40);
    CHECK_EQ(stat_of(3, OFF_AP), 0);
    data_fdps_roster_array_ptr = roster_image;
}

void run_roster_tests(void)
{
    RUN_TEST(the_base_stats_seed_the_totals);
    RUN_TEST(hit_and_evade_share_the_one_dexterity_seed);
    RUN_TEST(an_equipped_item_adds_its_four_modifiers);
    RUN_TEST(only_bit_0x40_makes_an_entry_count);
    RUN_TEST(every_one_of_the_eight_entries_is_scanned);
    RUN_TEST(all_eight_equipped_entries_accumulate);
    RUN_TEST(base_stats_and_modifiers_are_signed);
    RUN_TEST(the_item_id_byte_is_unsigned);
    RUN_TEST(the_totals_are_truncated_by_the_word_store);
    RUN_TEST(the_index_selects_the_record_by_eighty_byte_stride);
    RUN_TEST(a_roster_index_past_the_end_is_not_clamped);
    RUN_TEST(the_status_timers_are_not_applied);
    RUN_TEST(only_the_four_stat_words_are_written);
    RUN_TEST(the_totals_are_recomputed_not_accumulated);
    RUN_TEST(the_roster_base_is_read_on_every_call);

    /* Put both globals back to null.  Ticket 23 has yet to define them, and
       leaving a pointer to this file's static buffers in either would hand the
       next unit an address it has no business holding. */
    data_fdps_roster_array_ptr = (unsigned char *) 0;
    data_fdps_item_effect_table_ptr = (unsigned char *) 0;
}
