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

/* ------------------------------------------------------------------ *
 * fdps_roster_add_character @ 00023bc0
 *
 * Expected values come from the assembly of that body and from ticket 17's
 * three layouts in src/fdpstype.h -- struct fdps_unit_record, struct
 * fdps_character_base_record and struct fdps_character_growth.  The five
 * facts the cases below are aimed at:
 *
 *   00023bcc  the record is roster base + [0x00064114] * 0x50, and the count
 *             is incremented only at 00023e12, after everything else.
 *   00023c23  HP and MP take DEC EAX before the IMUL -- (level - 1) steps --
 *             while 00023d85, 00023d9f and 00023dcc scale AP, DP and DX by the
 *             full level with no DEC.
 *   00023c1b  every growth byte is zero-extended (XOR EDX,EDX / MOV DL, and
 *             the XOR AH,AH form) while 00023c2a takes the base stats with
 *             MOVSX -- contract C on both sides of one sum.
 *   00023cde  a carried item of 0xff makes the entry's flag byte 0x80, any
 *             other id makes it 0, and the id is copied either way; 00023d1a
 *             and 00023d21 write only the FLAG byte of bag entries 6 and 7.
 *   00023e0a  the recompute call is passed the count, i.e. the new member's
 *             own index, before the increment.
 *
 * The two table lookups run through the real accessors in src/table.c, so the
 * fixtures are staged at those accessors' own strides, 0x18 and 0x0b.  The
 * roster block is filled with 0xa5 rather than zeroed, because half of what
 * this body is pinned on is which bytes it leaves alone; a zeroed block cannot
 * tell an untouched byte from one written with 0.  Nothing here asserts what a
 * real FRIAPRDA.DAT or FRILEVUP.DAT contains -- every byte read back is one
 * this file wrote -- and all four globals go back to null on the way out.
 */

/* Strides and capacities of the two character tables, from
   fdps_get_character_base_record and fdps_get_growth_record, and the 60
   records each file holds (resource_info/data_tables.md). */
#define BASE_STRIDE 0x18
#define GROWTH_STRIDE 0x0b
#define CHARACTER_COUNT 60

/* The byte the roster block is filled with before each add, chosen because it
   is neither 0 nor 0xff nor any value the body writes. */
#define ROSTER_FILLER 0xa5

/* Field offsets inside a roster record that only the add cases read. */
#define OFF_FLAGS 0x05
#define OFF_SIDE 0x06
#define OFF_PORTRAIT_ID 0x07
#define OFF_CHAR_ID 0x08
#define OFF_RESERVED_09 0x09
#define OFF_SPELLS_KNOWN 0x1a
#define OFF_RACE 0x1f
#define OFF_CLASS 0x20
#define OFF_LEVEL 0x21
#define OFF_GAP_028 0x28
#define OFF_DEATH_OPCODE 0x31
#define OFF_DEATH_OPERAND 0x32
#define OFF_AI_BEHAVIOR 0x34
#define OFF_MOVE 0x3b
#define OFF_EXP_CARRY 0x3c
#define OFF_EVENT_SLOT 0x3d
#define OFF_MP_CURRENT 0x44
#define OFF_MP_MAX 0x46

/* Field offsets inside a FRIAPRDA.DAT base record. */
#define BASE_OFF_RACE 0x00
#define BASE_OFF_CLASS 0x01
#define BASE_OFF_LEVEL 0x02
#define BASE_OFF_HP 0x03
#define BASE_OFF_MP 0x05
#define BASE_OFF_MOVE 0x07
#define BASE_OFF_SPELL_MASK 0x08
#define BASE_OFF_EQUIP_0 0x0c
#define BASE_OFF_EQUIP_1 0x0d
#define BASE_OFF_CARRIED 0x0e
#define BASE_OFF_AP 0x12
#define BASE_OFF_DP 0x14
#define BASE_OFF_DX 0x16

/* Field offsets inside a FRILEVUP.DAT growth record: five {min, exclusive
   max} pairs, of which this body reads only the mins. */
#define GROWTH_OFF_AP_MIN 0x00
#define GROWTH_OFF_DP_MIN 0x02
#define GROWTH_OFF_DX_MIN 0x04
#define GROWTH_OFF_HP_MIN 0x06
#define GROWTH_OFF_MP_MIN 0x08

/* The character the cases enrol, and the numbers its two records carry.  The
   five growth mins are all different so a swapped pair shows as a wrong
   number, and the two equipment ids differ from every carried id. */
#define FIXTURE_CHAR_ID 5
#define FIXTURE_RACE 3
#define FIXTURE_CLASS 7
#define FIXTURE_LEVEL 5
#define FIXTURE_HP_BASE 30
#define FIXTURE_MP_BASE 12
#define FIXTURE_MOVE 6
#define FIXTURE_AP_BASE 40
#define FIXTURE_DP_BASE 25
#define FIXTURE_DX_BASE 17
#define FIXTURE_EQUIP_0 3
#define FIXTURE_EQUIP_1 4
#define FIXTURE_AP_MIN 2
#define FIXTURE_DP_MIN 3
#define FIXTURE_DX_MIN 4
#define FIXTURE_HP_MIN 6
#define FIXTURE_MP_MIN 1

static unsigned char base_image[CHARACTER_COUNT * BASE_STRIDE];
static unsigned char growth_image[CHARACTER_COUNT * GROWTH_STRIDE];

static unsigned char *base_at(int char_id)
{
    return base_image + char_id * BASE_STRIDE;
}

static unsigned char *growth_at(int char_id)
{
    return growth_image + char_id * GROWTH_STRIDE;
}

/* The roster goes to filler and the three tables to zero, then the fixture
   character's two records are written and the member count is put back to 0.
   Item 3 carries four distinct modifiers so the recompute the body ends with
   is visible; item 4, the second equipped id, stays all-zero. */
static void stage_add_fixture(void)
{
    int byte_index;
    unsigned char *base;
    unsigned char *growth;

    for (byte_index = 0;
         byte_index < (ROSTER_CAPACITY + 1) * ROSTER_STRIDE;
         byte_index++) {
        roster_image[byte_index] = ROSTER_FILLER;
    }
    for (byte_index = 0;
         byte_index < ITEM_IMAGE_RECORDS * ITEM_STRIDE;
         byte_index++) {
        item_image[byte_index] = 0;
    }
    for (byte_index = 0; byte_index < CHARACTER_COUNT * BASE_STRIDE;
         byte_index++) {
        base_image[byte_index] = 0;
    }
    for (byte_index = 0; byte_index < CHARACTER_COUNT * GROWTH_STRIDE;
         byte_index++) {
        growth_image[byte_index] = 0;
    }

    base = base_at(FIXTURE_CHAR_ID);
    base[BASE_OFF_RACE] = FIXTURE_RACE;
    base[BASE_OFF_CLASS] = FIXTURE_CLASS;
    base[BASE_OFF_LEVEL] = FIXTURE_LEVEL;
    put_word(base + BASE_OFF_HP, FIXTURE_HP_BASE);
    put_word(base + BASE_OFF_MP, FIXTURE_MP_BASE);
    base[BASE_OFF_MOVE] = FIXTURE_MOVE;
    base[BASE_OFF_SPELL_MASK + 0] = 0x11;
    base[BASE_OFF_SPELL_MASK + 1] = 0x22;
    base[BASE_OFF_SPELL_MASK + 2] = 0x33;
    base[BASE_OFF_SPELL_MASK + 3] = 0x44;
    base[BASE_OFF_EQUIP_0] = FIXTURE_EQUIP_0;
    base[BASE_OFF_EQUIP_1] = FIXTURE_EQUIP_1;
    base[BASE_OFF_CARRIED + 0] = 0x10;
    base[BASE_OFF_CARRIED + 1] = 0xff;
    base[BASE_OFF_CARRIED + 2] = 0x00;
    base[BASE_OFF_CARRIED + 3] = 0xff;
    put_word(base + BASE_OFF_AP, FIXTURE_AP_BASE);
    put_word(base + BASE_OFF_DP, FIXTURE_DP_BASE);
    put_word(base + BASE_OFF_DX, FIXTURE_DX_BASE);

    growth = growth_at(FIXTURE_CHAR_ID);
    growth[GROWTH_OFF_AP_MIN] = FIXTURE_AP_MIN;
    growth[GROWTH_OFF_DP_MIN] = FIXTURE_DP_MIN;
    growth[GROWTH_OFF_DX_MIN] = FIXTURE_DX_MIN;
    growth[GROWTH_OFF_HP_MIN] = FIXTURE_HP_MIN;
    growth[GROWTH_OFF_MP_MIN] = FIXTURE_MP_MIN;

    stage_item(FIXTURE_EQUIP_0, 7, 5, 3, 9);

    data_fdps_roster_array_ptr = roster_image;
    data_fdps_item_effect_table_ptr = item_image;
    data_fdps_battle_character_base_table_ptr = base_image;
    data_fdps_battle_character_growth_table_ptr = growth_image;
    data_fdps_roster_member_count = 0;
}

static unsigned char byte_of(int roster_index, int offset)
{
    return member_at(roster_index)[offset];
}

/* The new member lands at the index the count already holds -- record 2 when
   the count is 2 -- the records in front of it are not touched, and the count
   comes out one higher.  Slot 3 stays filler too, so nothing was written a
   record early or a record late. */
static void the_member_lands_at_the_count_and_the_count_advances(void)
{
    stage_add_fixture();
    data_fdps_roster_member_count = 2;
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(2, OFF_CHAR_ID), FIXTURE_CHAR_ID);
    CHECK_EQ(data_fdps_roster_member_count, 3);
    CHECK_EQ(byte_of(0, OFF_CHAR_ID), ROSTER_FILLER);
    CHECK_EQ(byte_of(1, OFF_CHAR_ID), ROSTER_FILLER);
    CHECK_EQ(byte_of(3, OFF_CHAR_ID), ROSTER_FILLER);
}

/* The five fixed header bytes: flags cleared, side 2 -- the player's side, the
   literal at 00023c76 -- the character id written into BOTH the portrait byte
   and the char_id byte, and the spare at +0x09 cleared. */
static void the_header_bytes_are_seeded_from_the_argument(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(0, OFF_FLAGS), 0);
    CHECK_EQ(byte_of(0, OFF_SIDE), 2);
    CHECK_EQ(byte_of(0, OFF_PORTRAIT_ID), FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(0, OFF_CHAR_ID), FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(0, OFF_RESERVED_09), 0);
}

/* Bag entries 0 and 1 take the base record's two equipment ids and the
   equipped flag 0x40, which is the bit fdps_roster_recompute_combat_stats
   tests, so both items are worn from the moment the member is enrolled. */
static void the_two_equipment_entries_are_flagged_equipped(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 0), 0x40);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 1), FIXTURE_EQUIP_0);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 2), 0x40);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 3), FIXTURE_EQUIP_1);
}

/* Entries 2..5 take the four carried ids.  The flag byte is 0x80 exactly when
   the id is 0xff and 0 otherwise -- id 0x00 is a real item and gets 0, not the
   empty flag -- and the id byte is copied either way, 0xff included.  So an
   empty carried slot is 0x80 alongside an id of 0xff and not alongside a
   cleared id byte. */
static void a_carried_id_of_ff_is_the_only_one_flagged_empty(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 4), 0x00);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 5), 0x10);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 6), 0x80);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 7), 0xff);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 8), 0x00);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 9), 0x00);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 10), 0x80);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 11), 0xff);
}

/* Bag entries 6 and 7 get their flag byte written and their id byte left
   alone: MOV byte ptr [EAX+0x16],0x80 and [EAX+0x18],0x80 at 00023d1a and
   00023d1e, and nothing at +0x17 or +0x19.  Clearing those two ids as well is
   the obvious tidy-up; the original leaves whatever the block held. */
static void the_last_two_bag_entries_keep_their_id_bytes(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 12), 0x80);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 13), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 14), 0x80);
    CHECK_EQ(byte_of(0, OFF_INVENTORY + 15), ROSTER_FILLER);
}

/* The known-spell bitmap is five bytes and the base record supplies four:
   memmove of 4 at 00023d35, then MOV byte ptr [EAX+0x1e],0x0 for the fifth.
   Spell ids 0x20..0x27 therefore start unlearned however the record was
   filled before. */
static void the_spell_bitmap_takes_four_bytes_and_clears_the_fifth(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(0, OFF_SPELLS_KNOWN + 0), 0x11);
    CHECK_EQ(byte_of(0, OFF_SPELLS_KNOWN + 1), 0x22);
    CHECK_EQ(byte_of(0, OFF_SPELLS_KNOWN + 2), 0x33);
    CHECK_EQ(byte_of(0, OFF_SPELLS_KNOWN + 3), 0x44);
    CHECK_EQ(byte_of(0, OFF_SPELLS_KNOWN + 4), 0x00);
}

/* Race, class and level are copied out of the base record, all six status
   timers are cleared by the memset at 00023d6f, and the death-script opcode is
   set to 0xff -- the "no script" value, and the one byte of that pair the body
   writes. */
static void the_identity_timers_and_death_script_are_set(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(0, OFF_RACE), FIXTURE_RACE);
    CHECK_EQ(byte_of(0, OFF_CLASS), FIXTURE_CLASS);
    CHECK_EQ(byte_of(0, OFF_LEVEL), FIXTURE_LEVEL);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 0), 0);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 1), 0);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 2), 0);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 3), 0);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 4), 0);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 5), 0);
    CHECK_EQ(byte_of(0, OFF_DEATH_OPCODE), 0xff);
    CHECK_EQ(byte_of(0, OFF_MOVE), FIXTURE_MOVE);
    CHECK_EQ(byte_of(0, OFF_EXP_CARRY), 0);
}

/* HP and MP take (level - 1) growth steps: at level 5 that is 30 + 4 * 6 = 54
   and 12 + 4 * 1 = 16, and both the current and the maximum word get the same
   number, so the member is enrolled at full health. */
static void hp_and_mp_scale_by_level_minus_one(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(stat_of(0, OFF_HP_CURRENT), 54);
    CHECK_EQ(stat_of(0, OFF_HP_MAX), 54);
    CHECK_EQ(stat_of(0, OFF_MP_CURRENT), 16);
    CHECK_EQ(stat_of(0, OFF_MP_MAX), 16);
}

/* Attack, defense and dexterity take a FULL level of growth steps: at level 5
   that is 40 + 5 * 2 = 50, 25 + 5 * 3 = 40 and 17 + 5 * 4 = 37.  The asymmetry
   with HP and MP above is the point -- writing all five the same way is the
   obvious reading and gives a level 5 character 6 too much HP. */
static void ap_dp_and_dx_scale_by_the_full_level(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(stat_of(0, OFF_AP_BASE), 50);
    CHECK_EQ(stat_of(0, OFF_DP_BASE), 40);
    CHECK_EQ(stat_of(0, OFF_DX_BASE), 37);
}

/* The same asymmetry seen at the bottom of the level range, where it is
   sharpest: a level 1 character gets no HP or MP growth at all and starts on
   the base record's own 30 and 12, but already carries one step of attack,
   defense and dexterity -- 42, 28 and 21. */
static void a_level_one_character_gets_growth_on_the_stats_only(void)
{
    stage_add_fixture();
    base_at(FIXTURE_CHAR_ID)[BASE_OFF_LEVEL] = 1;
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(0, OFF_LEVEL), 1);
    CHECK_EQ(stat_of(0, OFF_HP_MAX), FIXTURE_HP_BASE);
    CHECK_EQ(stat_of(0, OFF_MP_MAX), FIXTURE_MP_BASE);
    CHECK_EQ(stat_of(0, OFF_AP_BASE), 42);
    CHECK_EQ(stat_of(0, OFF_DP_BASE), 28);
    CHECK_EQ(stat_of(0, OFF_DX_BASE), 21);
}

/* Contract C, on both sides of the same sum.  A growth byte of 0xff is
   zero-extended: at level 3 it adds 2 * 255 = 510 to HP, and never subtracts
   two.  A base stat word of 0xfff6 is sign-extended: it is -10, and with the
   AP growth staged to 0 the record's attack base comes out -10 and not
   65526. */
static void growth_bytes_are_unsigned_and_base_stats_are_signed(void)
{
    stage_add_fixture();
    base_at(FIXTURE_CHAR_ID)[BASE_OFF_LEVEL] = 3;
    growth_at(FIXTURE_CHAR_ID)[GROWTH_OFF_HP_MIN] = 0xff;
    growth_at(FIXTURE_CHAR_ID)[GROWTH_OFF_AP_MIN] = 0;
    put_word(base_at(FIXTURE_CHAR_ID) + BASE_OFF_AP, 0xfff6);
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(stat_of(0, OFF_HP_MAX), FIXTURE_HP_BASE + 510);
    CHECK_EQ(stat_of(0, OFF_AP_BASE), -10);
}

/* The body ends by calling fdps_roster_recompute_combat_stats for the new
   member, so the four derived stats at +0x48..+0x4e are filled before the
   count moves.  Equipped item 3 carries ap 7, hit 5, dp 3 and ev 9, so the
   totals are the bases just written plus those: 57, 43, 42 and 46 -- and hit
   and evade both come off the one dexterity base of 37.  Filler in those four
   fields would read as 0xa5a5, so a missing call cannot pass this. */
static void the_derived_combat_stats_are_recomputed_for_the_new_member(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(stat_of(0, OFF_AP), 57);
    CHECK_EQ(stat_of(0, OFF_DP), 43);
    CHECK_EQ(stat_of(0, OFF_HIT), 42);
    CHECK_EQ(stat_of(0, OFF_EV), 46);
}

/* And it is recomputed for the NEW member's index and not for member 0: with
   the count at 2 the stats land in record 2, while record 0's four fields stay
   filler. */
static void the_recompute_runs_on_the_new_index(void)
{
    stage_add_fixture();
    data_fdps_roster_member_count = 2;
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(stat_of(2, OFF_AP), 57);
    CHECK_EQ(stat_of(2, OFF_EV), 46);
    CHECK_EQ(byte_of(0, OFF_AP), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_EV), ROSTER_FILLER);
}

/* The record is only partly initialised, and the gaps are behaviour.  Position
   and sprite state at +0x00..+0x04, the nine bytes at +0x28, the death-script
   operand at +0x32, the three AI bytes at +0x34 and the event slot at +0x3d
   are all still filler after the add.  A rebuild that helpfully zeroes the
   whole record first would pass every other case above and put a scripted unit
   at (0,0) facing 0. */
static void the_uninitialised_fields_are_left_alone(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(0, 0x00), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, 0x01), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, 0x02), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, 0x03), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, 0x04), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_GAP_028 + 0), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_GAP_028 + 8), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_DEATH_OPERAND + 0), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_DEATH_OPERAND + 1), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_AI_BEHAVIOR + 0), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_AI_BEHAVIOR + 1), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_AI_BEHAVIOR + 2), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_EVENT_SLOT), ROSTER_FILLER);
}

/* Two adds in a row fill two consecutive records and leave the count at 2, so
   a chapter's opening run of calls enrols one member per call.  Nothing in the
   body caches the count or the record address across calls. */
static void consecutive_adds_fill_consecutive_records(void)
{
    stage_add_fixture();
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    fdps_roster_add_character(FIXTURE_CHAR_ID);
    CHECK_EQ(data_fdps_roster_member_count, 2);
    CHECK_EQ(byte_of(0, OFF_CHAR_ID), FIXTURE_CHAR_ID);
    CHECK_EQ(byte_of(1, OFF_CHAR_ID), FIXTURE_CHAR_ID);
    CHECK_EQ(stat_of(1, OFF_HP_MAX), 54);
    CHECK_EQ(stat_of(1, OFF_AP), 57);
}

/* The argument reaches both table lookups and the two id bytes: enrolling a
   different character reads that character's own records.  Character 9 is
   staged with its own level and bases, and the record that comes out carries
   its numbers and its id, not the fixture character's. */
static void the_argument_selects_both_table_records(void)
{
    unsigned char *base;
    unsigned char *growth;

    stage_add_fixture();
    base = base_at(9);
    base[BASE_OFF_RACE] = 1;
    base[BASE_OFF_CLASS] = 2;
    base[BASE_OFF_LEVEL] = 2;
    put_word(base + BASE_OFF_HP, 100);
    put_word(base + BASE_OFF_AP, 11);
    base[BASE_OFF_CARRIED + 0] = 0xff;
    base[BASE_OFF_CARRIED + 1] = 0xff;
    base[BASE_OFF_CARRIED + 2] = 0xff;
    base[BASE_OFF_CARRIED + 3] = 0xff;
    growth = growth_at(9);
    growth[GROWTH_OFF_HP_MIN] = 10;
    growth[GROWTH_OFF_AP_MIN] = 5;

    fdps_roster_add_character(9);
    CHECK_EQ(byte_of(0, OFF_CHAR_ID), 9);
    CHECK_EQ(byte_of(0, OFF_PORTRAIT_ID), 9);
    CHECK_EQ(byte_of(0, OFF_LEVEL), 2);
    CHECK_EQ(stat_of(0, OFF_HP_MAX), 110);
    CHECK_EQ(stat_of(0, OFF_AP_BASE), 21);
}

/* ------------------------------------------------------------------ *
 * fdps_roster_write_back_battle_units @ 00023980
 *
 * Expected values come from the assembly of that body and from ticket 17's
 * struct fdps_unit_record.  The facts the cases below are aimed at:
 *
 *   000239ed  the slot is roster base + roster_index * 0x50, and the scan
 *             runs the whole roster with no break on a match, so two slots
 *             carrying one id are both written.
 *   00023a0b  the match is the char_id byte at +0x08 on both records.
 *   00023a17  the exemption needs BOTH halves: char id 0 and
 *             fdps_unit_is_retired reporting the unit has left.
 *   00023a3b  memmove of the whole 0x50 bytes lands first, so every reset
 *             below overwrites a value that came from the battle record.
 *   00023a4e  memset clears exactly the six timer bytes at +0x22.
 *   00023a6b  the HP restore is guarded by the masked flags byte while the MP
 *             store at 00023a85 sits after the join and runs for the dead too.
 *   00023a8f  the experience byte is zero-extended before CMP EAX,0x63, so
 *             0xff is 255 and is reset, not -1 and kept.
 *   00023aa4  the recompute is passed the ROSTER index of the matched slot.
 *
 * Both arrays run through the real accessors: the roster through the same
 * inline stride the body uses and the unit through fdps_get_unit_record in
 * src/unit.c.  Neither is a loaded file -- the roster is the heap block
 * allocated at 000296b8 and the unit array is the battle's own block -- so
 * staged byte buffers are the shape both globals hold at run time.  The roster
 * is filled with 0xa5 rather than zeroed because half of what these cases pin
 * down is which slots and which bytes are left alone, and a zeroed block
 * cannot tell an untouched byte from one written with 0.
 */

/* The unit record stride, which is the same 0x50 as the roster's:
   fdps_get_unit_record scales by sizeof(struct fdps_unit_record) and the
   record is that size.  Eight units is more than any case here needs, plus a
   spare record behind them. */
#define UNIT_STRIDE 0x50
#define UNIT_CAPACITY 8

/* The battle unit ids these cases use.  Id 0 is Randis, the one the exemption
   is written for; 5 and 6 are ordinary members, and neither is 0xa5, so a
   filler slot cannot match one by accident. */
#define WB_CHAR_RANDIS 0
#define WB_CHAR_A 5
#define WB_CHAR_B 6

static unsigned char unit_image[(UNIT_CAPACITY + 1) * UNIT_STRIDE];

static unsigned char *unit_at(int unit_index)
{
    return unit_image + unit_index * UNIT_STRIDE;
}

/* Roster to filler, unit and item blocks to zero, all four globals pointed at
   this file's buffers and both counts to 0; each case sets the two counts it
   wants. */
static void stage_writeback_fixture(void)
{
    int byte_index;

    for (byte_index = 0;
         byte_index < (ROSTER_CAPACITY + 1) * ROSTER_STRIDE;
         byte_index++) {
        roster_image[byte_index] = ROSTER_FILLER;
    }
    for (byte_index = 0;
         byte_index < (UNIT_CAPACITY + 1) * UNIT_STRIDE;
         byte_index++) {
        unit_image[byte_index] = 0;
    }
    for (byte_index = 0;
         byte_index < ITEM_IMAGE_RECORDS * ITEM_STRIDE;
         byte_index++) {
        item_image[byte_index] = 0;
    }

    data_fdps_roster_array_ptr = roster_image;
    data_fdps_map_unit_array_ptr = unit_image;
    data_fdps_item_effect_table_ptr = item_image;
    data_fdps_roster_member_count = 0;
    data_fdps_map_unit_count = 0;
}

static void stage_unit(int unit_index, int char_id, int flags)
{
    unsigned char *record;

    record = unit_at(unit_index);
    record[OFF_CHAR_ID] = (unsigned char) char_id;
    record[OFF_FLAGS] = (unsigned char) flags;
}

static void stage_slot_id(int roster_index, int char_id)
{
    member_at(roster_index)[OFF_CHAR_ID] = (unsigned char) char_id;
}

static unsigned char unit_byte_of(int unit_index, int offset)
{
    return unit_at(unit_index)[offset];
}

/* The whole record crosses, not a chosen handful of fields: bytes from the
   front, the middle and the back of the battle record all appear in the slot,
   and the slot's filler is gone from every one of them. */
static void the_whole_battle_record_is_copied_over_the_slot(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0);
    unit_at(0)[0x00] = 3;
    unit_at(0)[0x01] = 4;
    unit_at(0)[OFF_RACE] = 9;
    unit_at(0)[OFF_LEVEL] = 7;
    put_word(unit_at(0) + OFF_AP_BASE, 40);
    stage_slot_id(0, WB_CHAR_A);

    fdps_roster_write_back_battle_units();

    CHECK_EQ(byte_of(0, 0x00), 3);
    CHECK_EQ(byte_of(0, 0x01), 4);
    CHECK_EQ(byte_of(0, OFF_RACE), 9);
    CHECK_EQ(byte_of(0, OFF_LEVEL), 7);
    CHECK_EQ(stat_of(0, OFF_AP_BASE), 40);
    CHECK_EQ(byte_of(0, OFF_CHAR_ID), WB_CHAR_A);
}

/* memset takes exactly six bytes at +0x22.  The byte in front of the run
   (+0x21, level) and the first byte behind it (+0x28) both carry values the
   battle record supplied and must still be there. */
static void the_six_status_timers_are_cleared_and_nothing_else_is(void)
{
    int timer_index;

    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0);
    for (timer_index = 0; timer_index < 6; timer_index++) {
        unit_at(0)[OFF_STATUS_TIMERS + timer_index] = 9;
    }
    unit_at(0)[OFF_LEVEL] = 0x33;
    unit_at(0)[OFF_GAP_028] = 0x77;
    stage_slot_id(0, WB_CHAR_A);

    fdps_roster_write_back_battle_units();

    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 0), 0);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 1), 0);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 2), 0);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 3), 0);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 4), 0);
    CHECK_EQ(byte_of(0, OFF_STATUS_TIMERS + 5), 0);
    CHECK_EQ(byte_of(0, OFF_LEVEL), 0x33);
    CHECK_EQ(byte_of(0, OFF_GAP_028), 0x77);
}

/* AND byte ptr [EAX+0x5],0x1 keeps bit 0 and drops the rest, so every battle
   status bit is gone from the banked record and only the retired flag is
   carried out of the chapter. */
static void the_flags_byte_keeps_only_bit_zero(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0xf6);
    stage_slot_id(0, WB_CHAR_A);
    fdps_roster_write_back_battle_units();
    CHECK_EQ(byte_of(0, OFF_FLAGS), 0);

    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0xf7);
    stage_slot_id(0, WB_CHAR_A);
    fdps_roster_write_back_battle_units();
    CHECK_EQ(byte_of(0, OFF_FLAGS), 1);
}

/* A survivor comes out of the battle at full HP and full MP whatever the
   battle left him on, and the maxima are untouched. */
static void a_survivor_is_restored_to_full_hp_and_mp(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0);
    put_word(unit_at(0) + OFF_HP_CURRENT, 3);
    put_word(unit_at(0) + OFF_HP_MAX, 50);
    put_word(unit_at(0) + OFF_MP_CURRENT, 1);
    put_word(unit_at(0) + OFF_MP_MAX, 20);
    stage_slot_id(0, WB_CHAR_A);

    fdps_roster_write_back_battle_units();

    CHECK_EQ(stat_of(0, OFF_HP_CURRENT), 50);
    CHECK_EQ(stat_of(0, OFF_HP_MAX), 50);
    CHECK_EQ(stat_of(0, OFF_MP_CURRENT), 20);
    CHECK_EQ(stat_of(0, OFF_MP_MAX), 20);
}

/* The asymmetry between the two restores, and it is the point of them: a
   retired member keeps the HP the battle left him -- 3, the value memmove
   brought across -- while his MP is topped up all the same, because the MP
   store is past the join at 00023a7b.  Pulling the MP store inside the guard
   is the obvious tidy-up and leaves every fallen member on his battle MP. */
static void a_retired_member_keeps_his_hp_and_still_gets_his_mp(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 1);
    put_word(unit_at(0) + OFF_HP_CURRENT, 3);
    put_word(unit_at(0) + OFF_HP_MAX, 50);
    put_word(unit_at(0) + OFF_MP_CURRENT, 1);
    put_word(unit_at(0) + OFF_MP_MAX, 20);
    stage_slot_id(0, WB_CHAR_A);

    fdps_roster_write_back_battle_units();

    CHECK_EQ(byte_of(0, OFF_FLAGS), 1);
    CHECK_EQ(stat_of(0, OFF_HP_CURRENT), 3);
    CHECK_EQ(stat_of(0, OFF_HP_MAX), 50);
    CHECK_EQ(stat_of(0, OFF_MP_CURRENT), 20);
}

/* The masked flags byte and not the raw one decides the heal: a battle record
   whose flags are 0xfe has bit 0 clear, so once the mask has run the member is
   a survivor and is healed even though the raw byte was far from zero. */
static void the_heal_is_decided_after_the_flags_are_masked(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0xfe);
    put_word(unit_at(0) + OFF_HP_CURRENT, 3);
    put_word(unit_at(0) + OFF_HP_MAX, 50);
    stage_slot_id(0, WB_CHAR_A);

    fdps_roster_write_back_battle_units();

    CHECK_EQ(byte_of(0, OFF_FLAGS), 0);
    CHECK_EQ(stat_of(0, OFF_HP_CURRENT), 50);
}

/* Contract C, and the rebuild note the plate comment carries.  99 is the
   highest count that survives, 100 is reset, and 0xff -- the sentinel the
   deployment path writes for a unit that is not a roster character -- is 255
   and is reset too.  Read as a signed char it would be -1, would pass the
   test and would reach the status panel. */
static void an_experience_count_above_ninety_nine_is_reset(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0);
    unit_at(0)[OFF_EXP_CARRY] = 99;
    stage_slot_id(0, WB_CHAR_A);
    fdps_roster_write_back_battle_units();
    CHECK_EQ(byte_of(0, OFF_EXP_CARRY), 99);

    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0);
    unit_at(0)[OFF_EXP_CARRY] = 100;
    stage_slot_id(0, WB_CHAR_A);
    fdps_roster_write_back_battle_units();
    CHECK_EQ(byte_of(0, OFF_EXP_CARRY), 0);

    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0);
    unit_at(0)[OFF_EXP_CARRY] = 0xff;
    stage_slot_id(0, WB_CHAR_A);
    fdps_roster_write_back_battle_units();
    CHECK_EQ(byte_of(0, OFF_EXP_CARRY), 0);
}

/* A slot whose char_id is not the unit's is passed over untouched: its filler
   is still there afterwards, front and back. */
static void a_slot_with_another_char_id_is_left_alone(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0);
    unit_at(0)[0x00] = 3;
    stage_slot_id(0, WB_CHAR_B);

    fdps_roster_write_back_battle_units();

    CHECK_EQ(byte_of(0, 0x00), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_FLAGS), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_CHAR_ID), WB_CHAR_B);
    CHECK_EQ(byte_of(0, OFF_HP_CURRENT), ROSTER_FILLER);
}

/* The exemption needs both halves.  Randis retired is skipped and his slot
   keeps its filler; Randis still standing is banked like anybody else. */
static void randis_is_exempt_only_while_he_is_retired(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_RANDIS, 1);
    unit_at(0)[0x00] = 3;
    stage_slot_id(0, WB_CHAR_RANDIS);
    fdps_roster_write_back_battle_units();
    CHECK_EQ(byte_of(0, 0x00), ROSTER_FILLER);
    CHECK_EQ(byte_of(0, OFF_FLAGS), ROSTER_FILLER);

    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_RANDIS, 0);
    unit_at(0)[0x00] = 3;
    stage_slot_id(0, WB_CHAR_RANDIS);
    fdps_roster_write_back_battle_units();
    CHECK_EQ(byte_of(0, 0x00), 3);
    CHECK_EQ(byte_of(0, OFF_FLAGS), 0);
}

/* The other half of the exemption: the id must be 0.  A retired unit of any
   other character is banked, flag and all, so the roster learns that he fell.
   Skipping every retired unit is the obvious reading and would lose the
   death. */
static void a_retired_unit_of_another_id_is_still_banked(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 1);
    unit_at(0)[0x00] = 3;
    stage_slot_id(0, WB_CHAR_A);

    fdps_roster_write_back_battle_units();

    CHECK_EQ(byte_of(0, 0x00), 3);
    CHECK_EQ(byte_of(0, OFF_FLAGS), 1);
}

/* There is no break on a match: the inner loop runs to the end of the roster,
   so a character id sitting in three slots is written into all three. */
static void every_slot_carrying_the_id_is_written(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 3;
    stage_unit(0, WB_CHAR_A, 0);
    unit_at(0)[0x00] = 3;
    stage_slot_id(0, WB_CHAR_A);
    stage_slot_id(1, WB_CHAR_A);
    stage_slot_id(2, WB_CHAR_A);

    fdps_roster_write_back_battle_units();

    CHECK_EQ(byte_of(0, 0x00), 3);
    CHECK_EQ(byte_of(1, 0x00), 3);
    CHECK_EQ(byte_of(2, 0x00), 3);
}

/* The recompute is handed the matched slot's ROSTER index, not the unit's.
   Unit 0 matches slot 2 only, so slot 2's four derived stats come out of the
   record just installed -- attack 40 + item 3's 7 -- while slot 0's stay
   filler.  The battle record's own attack word is staged to 999, so a missing
   recompute would leave that value in the slot and not 47. */
static void the_recompute_runs_on_the_matched_roster_index(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 3;
    stage_item(3, 7, 5, 3, 9);
    stage_unit(0, WB_CHAR_A, 0);
    put_word(unit_at(0) + OFF_AP_BASE, 40);
    put_word(unit_at(0) + OFF_DX_BASE, 17);
    put_word(unit_at(0) + OFF_AP, 999);
    unit_at(0)[OFF_INVENTORY + 0] = 0x40;
    unit_at(0)[OFF_INVENTORY + 1] = 3;
    stage_slot_id(0, WB_CHAR_B);
    stage_slot_id(1, WB_CHAR_B);
    stage_slot_id(2, WB_CHAR_A);

    fdps_roster_write_back_battle_units();

    CHECK_EQ(stat_of(2, OFF_AP), 47);
    CHECK_EQ(stat_of(2, OFF_EV), 26);
    CHECK_EQ(byte_of(0, OFF_AP), ROSTER_FILLER);
    CHECK_EQ(byte_of(1, OFF_AP), ROSTER_FILLER);
}

/* Every battle unit is walked and each finds its own slot, in whatever order
   the two arrays happen to hold them: unit 0 is character 5 and lands in slot
   1, unit 1 is character 6 and lands in slot 0. */
static void every_unit_is_walked_and_each_finds_its_own_slot(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 2;
    data_fdps_roster_member_count = 2;
    stage_unit(0, WB_CHAR_A, 0);
    unit_at(0)[0x00] = 3;
    stage_unit(1, WB_CHAR_B, 0);
    unit_at(1)[0x00] = 4;
    stage_slot_id(0, WB_CHAR_B);
    stage_slot_id(1, WB_CHAR_A);

    fdps_roster_write_back_battle_units();

    CHECK_EQ(byte_of(0, 0x00), 4);
    CHECK_EQ(byte_of(1, 0x00), 3);
}

/* Both bounds are tested before their bodies run, so a battle with no units
   writes nothing and a roster with no members takes nothing, even when the ids
   would have matched.  The unit array is left alone either way. */
static void a_zero_count_on_either_side_writes_nothing(void)
{
    stage_writeback_fixture();
    data_fdps_map_unit_count = 0;
    data_fdps_roster_member_count = 1;
    stage_unit(0, WB_CHAR_A, 0);
    unit_at(0)[0x00] = 3;
    stage_slot_id(0, WB_CHAR_A);
    fdps_roster_write_back_battle_units();
    CHECK_EQ(byte_of(0, 0x00), ROSTER_FILLER);

    stage_writeback_fixture();
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 0;
    stage_unit(0, WB_CHAR_A, 0);
    unit_at(0)[0x00] = 3;
    stage_slot_id(0, WB_CHAR_A);
    fdps_roster_write_back_battle_units();
    CHECK_EQ(byte_of(0, 0x00), ROSTER_FILLER);
    CHECK_EQ(unit_byte_of(0, 0x00), 3);
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

    RUN_TEST(the_member_lands_at_the_count_and_the_count_advances);
    RUN_TEST(the_header_bytes_are_seeded_from_the_argument);
    RUN_TEST(the_two_equipment_entries_are_flagged_equipped);
    RUN_TEST(a_carried_id_of_ff_is_the_only_one_flagged_empty);
    RUN_TEST(the_last_two_bag_entries_keep_their_id_bytes);
    RUN_TEST(the_spell_bitmap_takes_four_bytes_and_clears_the_fifth);
    RUN_TEST(the_identity_timers_and_death_script_are_set);
    RUN_TEST(hp_and_mp_scale_by_level_minus_one);
    RUN_TEST(ap_dp_and_dx_scale_by_the_full_level);
    RUN_TEST(a_level_one_character_gets_growth_on_the_stats_only);
    RUN_TEST(growth_bytes_are_unsigned_and_base_stats_are_signed);
    RUN_TEST(the_derived_combat_stats_are_recomputed_for_the_new_member);
    RUN_TEST(the_recompute_runs_on_the_new_index);
    RUN_TEST(the_uninitialised_fields_are_left_alone);
    RUN_TEST(consecutive_adds_fill_consecutive_records);
    RUN_TEST(the_argument_selects_both_table_records);

    RUN_TEST(the_whole_battle_record_is_copied_over_the_slot);
    RUN_TEST(the_six_status_timers_are_cleared_and_nothing_else_is);
    RUN_TEST(the_flags_byte_keeps_only_bit_zero);
    RUN_TEST(a_survivor_is_restored_to_full_hp_and_mp);
    RUN_TEST(a_retired_member_keeps_his_hp_and_still_gets_his_mp);
    RUN_TEST(the_heal_is_decided_after_the_flags_are_masked);
    RUN_TEST(an_experience_count_above_ninety_nine_is_reset);
    RUN_TEST(a_slot_with_another_char_id_is_left_alone);
    RUN_TEST(randis_is_exempt_only_while_he_is_retired);
    RUN_TEST(a_retired_unit_of_another_id_is_still_banked);
    RUN_TEST(every_slot_carrying_the_id_is_written);
    RUN_TEST(the_recompute_runs_on_the_matched_roster_index);
    RUN_TEST(every_unit_is_walked_and_each_finds_its_own_slot);
    RUN_TEST(a_zero_count_on_either_side_writes_nothing);

    /* Put every global this file wrote back where it found it.  Ticket 23 has
       yet to define the five pointers, and leaving a pointer to this file's
       static buffers in any of them would hand the next unit an address it has
       no business holding; the two counts go back to 0 for the same reason,
       since the cases above moved them. */
    data_fdps_roster_array_ptr = (unsigned char *) 0;
    data_fdps_map_unit_array_ptr = (unsigned char *) 0;
    data_fdps_item_effect_table_ptr = (unsigned char *) 0;
    data_fdps_battle_character_base_table_ptr = (unsigned char *) 0;
    data_fdps_battle_character_growth_table_ptr = (unsigned char *) 0;
    data_fdps_roster_member_count = 0;
    data_fdps_map_unit_count = 0;
}
