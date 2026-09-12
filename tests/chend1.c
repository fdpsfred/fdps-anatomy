/* tests/chend1.c -- cover for src/chend1.c.
 *
 * WHAT THE HANDLER IS.  fdps_chapter_01_end at 0003a410 is four calls and one
 * store with no branch anywhere in it, so nothing about it is worth testing in
 * pieces: what it decides is the ORDER of the four calls and the value of the
 * one store.  The file therefore runs the whole handler once, for real,
 * against staged arrays and a fixture cut-scene, and the cases below assert
 * against that single run.
 *
 * Expected values come from the assembly at 0003a410 and from the bodies the
 * four callees were emitted from, never from the emitted C of the handler:
 *
 *   0003a41c  PUSH 0x0 / PUSH 0x0 / CALL 0x000282b0 / ADD ESP,0x8
 *             battle unit 0 learns spell 0, bit 0 of the five-byte bitmap at
 *             record +0x1a (src/unit.c)
 *   0003a428  CALL 0x00023980          the battle party is banked
 *   0003a42d  MOV EAX,0x620b0 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win00.dat" is interpreted
 *   0003a43b  CALL 0x00039e70          the fallen are revived
 *   0003a440  MOV dword ptr [0x00069cf4],0x1
 *
 * HOW THE ORDER IS PINNED DOWN RATHER THAN ASSUMED.  Each of the first three
 * steps leaves a mark the step after it would erase or miss:
 *
 *   The spell is granted on the LIVE battle record, and
 *   fdps_roster_write_back_battle_units memmoves that record's whole 0x50
 *   bytes over the roster record.  Record +0x1a is inside the copied block and
 *   outside the six status bytes at +0x22 the writeback clears afterwards, so
 *   the roster's copy of the bitmap carries the bit only if the bit was set
 *   first.  The roster block is filled with 0xa5 before the run, so an
 *   untouched slot is told apart from one written with 0.
 *
 *   The fixture cut-scene's first opcode RETIRES battle unit 0.  The writeback
 *   skips a unit whose character id is 0 and which has left the field -- both
 *   halves, and unit 0 here is character 0 -- so a run that interpreted the
 *   script before banking the party would leave roster slot 0 at its filler
 *   instead of carrying the battle record.  Its second opcode writes a marker
 *   byte into status_timers[4] of the same unit, which the writeback's memset
 *   would have cleared on the roster copy but cannot reach on the live record.
 *
 *   The revive sweep only looks at roster members standing at 0 HP, and the
 *   writeback's full heal has just put slot 0 on its maximum, so the sweep
 *   finds nobody, charges nothing and returns before it opens its panel.
 *
 * WHY THE PANEL MUST NOT OPEN.  fdps_roster_revive_fallen_members ends in a
 * frame loop that runs until a keyboard make code arrives, and nothing queues
 * one in a test image, so a case that left a member at 0 HP would never return
 * -- the same reason tests/roster.c gives for staging every member alive in
 * its own cover of that function.
 *
 * WHY THE CUT-SCENE IS A FIXTURE AND NOT THE SHIPPED ONE.  The shipped
 * WIN00.DAT is chapter 1's victory cinematic: it plays CD audio and .saf clips
 * and draws chapter text through pointers a fresh test image has not filled.
 * Running it asserts nothing about THIS function and faults on the way, the
 * same reason tests/icon.c and tests/chinit1.c give for staging their own
 * container.  The one staged here holds three short scripts built to the
 * layout in resource_info/vfs.md and read by the game's own fdps_vfs_open and
 * fdps_vfs_load_file: WIN00.DAT, the member this handler names, and WIN01.DAT
 * and WIN02.DAT, the members the two handlers after it name, each writing its
 * own marker value into its own timer slot.  A run that opened a neighbouring
 * member is therefore several failed assertions rather than a hang.
 *
 * The staging REFUSES TO OVERWRITE an IconAni.vfs that is already in the run
 * directory and removes its own again in the last case, which is the protocol
 * tests/icon.c and tests/chinit1.c share for that name.
 *
 * WHAT IS STAGED AND WHAT IS REAL.  Both record arrays and the item effect
 * table are this file's own buffers -- at run time they are heap blocks, not
 * loaded files, and the item table is a ticket 23 symbol the build links
 * zero-filled -- and the container is the fixture above.  Nothing below
 * asserts what any global held before the run.
 */
#include <stdio.h>
#include <string.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "chend1.h"

/* The container the interpreter opens.  8.3, and the name it holds as a
   literal -- nothing a caller passes can point it anywhere else. */
#define SCRIPT_ARCHIVE_FILE "IconAni.vfs"

/* resource_info/vfs.md: a 35-byte header, then one 26-byte entry per member,
   then the member bytes end to end with no gaps.  Member names are stored
   upper-cased, because the lookup upper-cases the caller's string before the
   compare. */
#define VFS_HEADER_BYTES 35
#define VFS_ENTRY_BYTES 26
#define VFS_NAME_FIELD_BYTES 13
#define VFS_SIGNATURE_BYTES 24
#define FIXTURE_MEMBERS 3

/* How many records each staged array holds.  One of each is all the run needs;
   the spares behind them are there so a write past the end of the array under
   test lands somewhere this file can see. */
#define UNIT_CAPACITY 4
#define ROSTER_CAPACITY 4
#define ITEM_TABLE_ROWS 8

/* The roster block is filled with this rather than zeroed, because half of
   what the cases pin down is which bytes the writeback replaced: a zeroed
   block cannot tell an untouched byte from one written with 0. */
#define ROSTER_FILLER 0xa5

/* Battle unit 0 and character 0 are the same person here -- 蘭迪斯, the only
   party member chapter 1 has -- and the pairing is load-bearing: the
   writeback's exemption fires only for character id 0, so the retire opcode in
   the fixture script below is what a wrong order would trip over. */
#define RANDIS_UNIT 0
#define RANDIS_CHAR_ID 0
#define RANDIS_ROSTER_SLOT 0

/* What the spell grant must leave in the five bitmap bytes at record +0x1a:
   spell 0 is bit 0 of byte 0, so byte 0 is 0x01 and the other four stay at 0
   (src/unit.c). */
#define SPELL_BITMAP_BYTES 5
#define RANDIS_SPELL_BYTE 0x01

/* The battle record unit 0 carries into the handler.  hp_current is below
   hp_max on purpose: the writeback's full heal is what lifts the roster copy
   to the maximum, so the two numbers being different is what makes that heal
   visible. */
#define RANDIS_HP_CURRENT 25
#define RANDIS_HP_MAX 40
#define RANDIS_MP_CURRENT 2
#define RANDIS_MP_MAX 9
#define RANDIS_LEVEL 3
#define RANDIS_CLASS 0

/* The six status bytes at record +0x22, and the ones the two fixture markers
   use.  The SET_UNIT_TIMER opcode's slot operand is measured from
   status_timers[3] (src/icon.c), so operand 1 is status_timers[4] and operand
   0 is status_timers[3]. */
#define STATUS_TIMER_COUNT 6
#define WIN00_MARKER_OPERAND 1
#define WIN00_MARKER_SLOT 4
#define WIN00_MARKER_VALUE 6
#define WIN01_MARKER_OPERAND 0
#define WIN01_MARKER_SLOT 3
#define WIN01_MARKER_VALUE 11
#define WIN02_MARKER_OPERAND 2
#define WIN02_MARKER_SLOT 5
#define WIN02_MARKER_VALUE 7

/* The retired bit in the flags byte at record +5, which the fixture's first
   opcode raises on unit 0. */
#define UNIT_FLAG_RETIRED 1

/* The chapter index the run starts from and the one the handler must leave.
   9 is neither 0 nor 1, so the store at 0003a440 is pinned as an assignment of
   chapter 2's index and not as a step from whatever was there. */
#define CHAPTER_ID_BEFORE 9
#define CHAPTER_ID_AFTER 1

/* The purse the run starts with.  Nothing in the handler may spend from it:
   the revive charges its fee inside the sweep, and the sweep finds no fallen
   member. */
#define PARTY_GOLD_BEFORE 1234

static struct fdps_unit_record unit_image[UNIT_CAPACITY];
static struct fdps_unit_record roster_image[ROSTER_CAPACITY];
static unsigned char item_image[ITEM_TABLE_ROWS
                                * sizeof(struct fdps_item_effect)];

/* WIN00.DAT: retire battle unit 0, write the marker into its status_timers[4],
   stop.  Opcode numbers and operand counts are src/icon.c's ladder -- 0x0b
   takes a unit index, 0x12 takes a unit index, a timer slot measured from
   status_timers[3] and a value, and 0x00 falls into the arm that ends the
   script. */
static unsigned char fixture_win00_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN00_MARKER_OPERAND, WIN00_MARKER_VALUE,
    0x00
};

/* WIN01.DAT: the member fdps_chapter_02_end names.  It retires nobody and
   writes a different value into a different timer slot, so a run that opened
   it instead of WIN00.DAT is visible in the record rather than in a hang. */
static unsigned char fixture_win01_dat[] = {
    0x12, RANDIS_UNIT, WIN01_MARKER_OPERAND, WIN01_MARKER_VALUE,
    0x00
};

/* WIN02.DAT: the member fdps_chapter_03_end names.  Like WIN00.DAT it retires
   battle unit 0 -- which is what makes the writeback's character-0 exemption
   the witness that the banking happened first -- and it writes a third marker
   value into a third timer slot, so opening the wrong member of the three is
   visible in the record. */
static unsigned char fixture_win02_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN02_MARKER_OPERAND, WIN02_MARKER_VALUE,
    0x00
};

static char *fixture_names[FIXTURE_MEMBERS] = {
    "WIN00.DAT", "WIN01.DAT", "WIN02.DAT"
};

static unsigned char *fixture_bytes[FIXTURE_MEMBERS] = {
    fixture_win00_dat, fixture_win01_dat, fixture_win02_dat
};

static int fixture_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_win00_dat), sizeof(fixture_win01_dat),
    sizeof(fixture_win02_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case says so. */
static int run_state = 0;

/* Whether this file created the container, and so whether it may remove it. */
static int fixture_owned = 0;

/* Everything the cases assert, captured the instant the handler returned. */
static unsigned char seen_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char seen_unit_timers[STATUS_TIMER_COUNT];
static int seen_unit_flags;
static unsigned char seen_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char seen_slot_timers[STATUS_TIMER_COUNT];
static int seen_slot_char_id;
static int seen_slot_flags;
static int seen_slot_hp_current;
static int seen_slot_hp_max;
static int seen_slot_mp_current;
static int seen_slot_level;
static int seen_chapter_id;
static int seen_party_gold;

static void write_word(FILE *fp, int value)
{
    unsigned char bytes[2];

    bytes[0] = (unsigned char) (value & 0xff);
    bytes[1] = (unsigned char) ((value >> 8) & 0xff);
    fwrite(bytes, 1, 2, fp);
}

static void write_dword(FILE *fp, long value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char) (value & 0xff);
    bytes[1] = (unsigned char) ((value >> 8) & 0xff);
    bytes[2] = (unsigned char) ((value >> 16) & 0xff);
    bytes[3] = (unsigned char) ((value >> 24) & 0xff);
    fwrite(bytes, 1, 4, fp);
}

/* The 13-byte name field: the name, a terminator, and zeroes to the end. */
static void write_name(FILE *fp, char *name)
{
    unsigned char field[VFS_NAME_FIELD_BYTES];
    int i;

    memset(field, 0, sizeof(field));
    for (i = 0; i < VFS_NAME_FIELD_BYTES - 1 && name[i] != '\0'; i++) {
        field[i] = (unsigned char) name[i];
    }
    fwrite(field, 1, VFS_NAME_FIELD_BYTES, fp);
}

/* Builds the fixture container, or answers no.  A file of that name that was
   already there is left alone: it is either the shipped 4.7 MB container or
   another test file's fixture, and neither may be clobbered. */
static int stage_fixture_archive(void)
{
    FILE *fp;
    long member_at;
    int i;

    fp = fopen(SCRIPT_ARCHIVE_FILE, "rb");
    if (fp != NULL) {
        fclose(fp);
        return 0;
    }

    fp = fopen(SCRIPT_ARCHIVE_FILE, "wb");
    if (fp == NULL) {
        return 0;
    }

    fwrite("VFS", 1, 3, fp);
    write_word(fp, 1);
    write_word(fp, VFS_HEADER_BYTES);
    write_dword(fp, (long) FIXTURE_MEMBERS);
    fwrite("Dynasty Information Co.,", 1, VFS_SIGNATURE_BYTES, fp);

    member_at = (long) VFS_HEADER_BYTES
                + (long) FIXTURE_MEMBERS * VFS_ENTRY_BYTES;
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        write_name(fp, fixture_names[i]);
        write_dword(fp, (long) fixture_lengths[i]);
        write_dword(fp, (long) fixture_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture_bytes[i], 1, (size_t) fixture_lengths[i], fp);
    }
    fclose(fp);

    fixture_owned = 1;
    return 1;
}

static int file_present(char *name)
{
    FILE *fp;

    fp = fopen(name, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* The battle array, the roster block and the item table as they stand when the
   chapter's battle has just been won: one live party member on the map, one
   roster slot carrying his character id and filler everywhere else. */
static void stage_globals(void)
{
    memset(unit_image, 0, sizeof(unit_image));
    memset(roster_image, ROSTER_FILLER, sizeof(roster_image));
    memset(item_image, 0, sizeof(item_image));

    unit_image[RANDIS_UNIT].char_id = RANDIS_CHAR_ID;
    unit_image[RANDIS_UNIT].flags = 0;
    unit_image[RANDIS_UNIT].level = RANDIS_LEVEL;
    unit_image[RANDIS_UNIT].clazz = RANDIS_CLASS;
    unit_image[RANDIS_UNIT].hp_current = RANDIS_HP_CURRENT;
    unit_image[RANDIS_UNIT].hp_max = RANDIS_HP_MAX;
    unit_image[RANDIS_UNIT].mp_current = RANDIS_MP_CURRENT;
    unit_image[RANDIS_UNIT].mp_max = RANDIS_MP_MAX;

    roster_image[RANDIS_ROSTER_SLOT].char_id = RANDIS_CHAR_ID;

    data_fdps_map_unit_array_ptr = (unsigned char *) unit_image;
    data_fdps_roster_array_ptr = (unsigned char *) roster_image;
    data_fdps_item_effect_table_ptr = item_image;
    data_fdps_map_unit_count = 1;
    data_fdps_roster_member_count = 1;
    data_fdps_chapter_current_chapter_id = CHAPTER_ID_BEFORE;
    data_fdps_shared_party_total_gold = PARTY_GOLD_BEFORE;
}

static void capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        seen_unit_spells[i] = unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        seen_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        seen_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        seen_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    seen_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    seen_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    seen_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    seen_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    seen_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    seen_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    seen_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    seen_chapter_id = data_fdps_chapter_current_chapter_id;
    seen_party_gold = data_fdps_shared_party_total_gold;
}

/* Runs the handler once, against the staged arrays and the fixture cut-scene,
   and records what it left behind. */
static void run_handler(void)
{
    if (run_state != 0) {
        return;
    }
    run_state = 2;

    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();

    fdps_chapter_01_end();

    capture();
    run_state = 1;
}

/* The spell lands on the LIVE battle record: byte 0 of the bitmap at +0x1a
   carries bit 0 and the other four bytes stay clear.  Any other bit, or a
   second byte written, would be a different spell id reaching
   fdps_set_flag_bit. */
static void randis_learns_spell_zero_on_the_battle_record(void)
{
    int i;

    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit_spells[0], RANDIS_SPELL_BYTE);
    for (i = 1; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(seen_unit_spells[i], 0);
    }
}

/* The spell reaches the ROSTER, which is only true if it was granted before
   the writeback.  The roster block was filler before the run, so the five
   bytes read back are the memmove's copy of the battle record's bitmap: had
   the grant come after the writeback they would be 0x01 on the live record and
   0xa5 in the slot, and had it been aimed at the roster record instead the
   live record's byte would be 0 as well. */
static void the_spell_is_banked_because_it_precedes_the_writeback(void)
{
    int i;

    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_slot_spells[0], RANDIS_SPELL_BYTE);
    for (i = 1; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(seen_slot_spells[i], 0);
    }
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot
   carries the battle record's character id, level and maximum, its six status
   bytes were cleared by the writeback's memset, its HP was lifted to the
   maximum by the full heal and its MP by the restore that follows.  The
   fixture script retires unit 0, and the writeback skips a character-0 unit
   that has left the field, so a run that interpreted the script first would
   leave every one of these at the 0xa5 filler. */
static void the_party_is_banked_before_the_cutscene_runs(void)
{
    int i;

    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(seen_slot_level, RANDIS_LEVEL);
    CHECK_EQ(seen_slot_flags, 0);
    CHECK_EQ(seen_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(seen_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(seen_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(seen_slot_timers[i], 0);
    }
}

/* The cut-scene the handler names is Win00.dat and it really ran, after the
   party was banked.  Both of the fixture member's opcodes are visible on the
   live battle record -- the retired bit at +5 and the marker in
   status_timers[4] -- and neither reached the roster copy.  WIN01.DAT, the
   member the next handler names, would have left status_timers[3] at 11 and
   the flags byte at 0 instead. */
static void the_victory_cutscene_is_win00_dat(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit_timers[WIN00_MARKER_SLOT], WIN00_MARKER_VALUE);
    CHECK_EQ(seen_unit_timers[WIN01_MARKER_SLOT], 0);
    CHECK_EQ(seen_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(seen_slot_timers[WIN00_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel.
   The writeback ran first and put the one roster member on his maximum, which
   is what leaves the sweep with no member at 0 HP to bill for. */
static void the_revive_charges_nothing_when_nobody_fell(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 2's, as an assignment and not as a step
   from what was there: the run started it at 9. */
static void the_chapter_index_is_advanced_to_chapter_two(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_chapter_id, CHAPTER_ID_AFTER);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_02_end at 0003a470.
 *
 * The same shape as the handler above with the spell grant taken away: three
 * calls and one store, no branch.  What the cases below pin down is that the
 * grant really is absent, that the cut-scene it names is WIN01.DAT and not its
 * neighbour, and that the store leaves chapter 3's index.
 *
 *   0003a47c  CALL 0x00023980          the battle party is banked
 *   0003a481  MOV EAX,0x620bc / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win01.dat" is interpreted
 *   0003a48f  CALL 0x00039e70          the fallen are revived
 *   0003a494  MOV dword ptr [0x00069cf4],0x2
 *
 * WHAT THE FIXTURE CAN AND CANNOT SEE.  WIN01.DAT retires nobody, so the
 * writeback banks unit 0 whichever side of the script it runs on, and the one
 * byte the script writes -- status_timers[3] of the LIVE record -- sits inside
 * the six bytes the writeback memsets on the roster copy, so the roster reads
 * back as zeroes either way.  The order of those two steps is therefore taken
 * from the call order in the assembly and is not claimed by an assertion here;
 * what the cases do pin is that both of them ran, that the script that ran was
 * this handler's member, and that the revive found the banked party healthy.
 *
 * The run reuses the container the cases above staged -- that file holds
 * WIN01.DAT precisely so this handler has a member to open -- and stages the
 * record arrays again from scratch, so nothing it asserts depends on what the
 * first run left behind.
 * ---------------------------------------------------------------------- */

/* The index the handler must leave: chapter 3, 0-based.  The run starts from
   CHAPTER_ID_BEFORE, 9, so the store is pinned as an assignment and not as a
   step from what was there. */
#define CH02_CHAPTER_ID_AFTER 2

static int ch02_run_state = 0;

static unsigned char ch02_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch02_unit_timers[STATUS_TIMER_COUNT];
static int ch02_unit_flags;
static unsigned char ch02_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch02_slot_timers[STATUS_TIMER_COUNT];
static int ch02_slot_char_id;
static int ch02_slot_flags;
static int ch02_slot_hp_current;
static int ch02_slot_hp_max;
static int ch02_slot_mp_current;
static int ch02_slot_level;
static int ch02_chapter_id;
static int ch02_party_gold;

/* The container is staged once for the whole file: if the cases above built
   it, this run uses it as it stands, and if they did not -- because a file of
   that name was already in the run directory -- this run says so the same way
   they do rather than clobbering it. */
static int ensure_fixture_archive(void)
{
    if (fixture_owned) {
        return 1;
    }
    return stage_fixture_archive();
}

static void ch02_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch02_unit_spells[i] = unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch02_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch02_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch02_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch02_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch02_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch02_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch02_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch02_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch02_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch02_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch02_chapter_id = data_fdps_chapter_current_chapter_id;
    ch02_party_gold = data_fdps_shared_party_total_gold;
}

static void run_chapter_02_handler(void)
{
    if (ch02_run_state != 0) {
        return;
    }
    ch02_run_state = 2;

    if (!ensure_fixture_archive()) {
        return;
    }

    stage_globals();

    fdps_chapter_02_end();

    ch02_capture();
    ch02_run_state = 1;
}

/* The battle party is banked: the slot that was 0xa5 filler now carries the
   battle record's character id and level, its six status bytes were cleared by
   the writeback's memset, its flags were masked to bit 0, its HP was lifted to
   the maximum by the full heal and its MP by the restore that follows.  The
   two HP figures differ before the run, so the maximum reading back in both is
   the heal and not the record's own value. */
static void chapter_02_banks_the_party_onto_the_roster(void)
{
    int i;

    run_chapter_02_handler();
    CHECK_EQ(ch02_run_state, 1);
    if (ch02_run_state != 1) {
        return;
    }

    CHECK_EQ(ch02_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch02_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch02_slot_flags, 0);
    CHECK_EQ(ch02_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch02_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch02_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch02_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003a47c is the writeback -- so both the live record's bitmap and
   the roster's copy of it stay at the zeroes the staging left.  This is the
   one place chapter 2's handler differs in kind from chapter 1's, and a stray
   grant copied over from the neighbour would show as byte 0 reading 0x01. */
static void chapter_02_grants_no_spell(void)
{
    int i;

    run_chapter_02_handler();
    CHECK_EQ(ch02_run_state, 1);
    if (ch02_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch02_unit_spells[i], 0);
        CHECK_EQ(ch02_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win01.dat and it really ran: the fixture
   member's marker is in status_timers[3] of the live battle record.
   WIN00.DAT, the member the previous handler names, would instead have left
   status_timers[4] at 6 and raised the retired bit in the flags byte, so
   opening the wrong member is three failed assertions rather than a silent
   pass.  Neither byte reaches the roster copy, whose timers the writeback
   clears. */
static void chapter_02_victory_cutscene_is_win01_dat(void)
{
    run_chapter_02_handler();
    CHECK_EQ(ch02_run_state, 1);
    if (ch02_run_state != 1) {
        return;
    }

    CHECK_EQ(ch02_unit_timers[WIN01_MARKER_SLOT], WIN01_MARKER_VALUE);
    CHECK_EQ(ch02_unit_timers[WIN00_MARKER_SLOT], 0);
    CHECK_EQ(ch02_unit_flags, 0);
    CHECK_EQ(ch02_slot_timers[WIN01_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The purse reading back
   unchanged is also what proves the sweep returned at all -- its panel loop
   waits for a key no test image queues. */
static void chapter_02_revive_charges_nothing_when_nobody_fell(void)
{
    run_chapter_02_handler();
    CHECK_EQ(ch02_run_state, 1);
    if (ch02_run_state != 1) {
        return;
    }

    CHECK_EQ(ch02_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 3's, as an assignment and not as a step
   from what was there: the run started it at 9. */
static void chapter_02_advances_the_chapter_index_to_chapter_three(void)
{
    run_chapter_02_handler();
    CHECK_EQ(ch02_run_state, 1);
    if (ch02_run_state != 1) {
        return;
    }

    CHECK_EQ(ch02_chapter_id, CH02_CHAPTER_ID_AFTER);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_03_end at 0003a520.
 *
 * Chapter 2's three-step shape with one step in front of it: four calls and
 * one store, no branch.
 *
 *   0003a52c  CALL 0x00039e10          every enemy still standing is destroyed
 *   0003a531  CALL 0x00023980          the battle party is banked
 *   0003a536  MOV EAX,0x620c8 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win02.dat" is interpreted
 *   0003a544  CALL 0x00039e70          the fallen are revived
 *   0003a549  MOV dword ptr [0x00069cf4],0x3
 *
 * WHAT THE DESTROY CALL LEAVES BEHIND.  fdps_battle_destroy_remaining_enemies
 * walks the battle array, tests the side byte at record +6 against 0 and
 * stores a 16-bit zero into the hit-point word at +0x40 of every unit that
 * matches, then calls the death pass once (src/btlend.c, 00039e10).  The
 * staging below therefore adds a second battle unit on side 0 whose hit points
 * are not zero: its word reading back 0 after the run is the witness that the
 * call happened, its maximum in the word at +0x42 reading back unchanged is
 * the witness that the store was the narrow one, and the player unit keeping
 * its own hit points is the witness that the side test is the only test.
 *
 * WHY THE ENEMY IS STAGED ALREADY RETIRED.  The death pass that ends the
 * destroy call collects every unit whose retired bit is clear and whose hit
 * points are 0, and for a non-empty list it spins the units, plays Explo.Saf
 * over their tiles and renders frames (src/death.c).  None of that can run in
 * a test image.  With the retired bit already raised the list comes back empty
 * and the pass returns at its zero-count exit, which leaves the hit-point
 * store above as the only thing the call did -- exactly the part this handler
 * is responsible for having asked for.
 *
 * The enemy also carries a character id no roster slot holds, so the writeback
 * that follows does not bank it and the roster assertions stay about the
 * party.
 *
 * WIN02.DAT retires battle unit 0 the way WIN00.DAT does, so the same witness
 * works here: the writeback skips a character-0 unit that has left the field,
 * and roster slot 0 carrying the battle record rather than the 0xa5 filler is
 * what says the banking ran before the script.
 * ---------------------------------------------------------------------- */

/* The side byte the destroy pass tests at record +6: 0 is the enemy and 2 the
   player's own roster (src/btlend.c, src/chevt4.c). */
#define ENEMY_SIDE 0
#define PLAYER_SIDE 2

/* The second battle unit the chapter 3 run stages: an enemy still standing
   when the chapter was cleared.  Its character id is one no roster slot holds,
   so the writeback leaves it alone, and its retired bit is already raised so
   the death pass the destroy call ends in finds nobody to animate. */
#define ENEMY_UNIT 1
#define ENEMY_CHAR_ID 9
#define ENEMY_HP_CURRENT 50
#define ENEMY_HP_MAX 50
#define CH03_UNIT_COUNT 2

/* The index the handler must leave: chapter 4, 0-based.  The run starts from
   CHAPTER_ID_BEFORE, 9, so the store is pinned as an assignment and not as a
   step from what was there. */
#define CH03_CHAPTER_ID_AFTER 3

static int ch03_run_state = 0;

static unsigned char ch03_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch03_unit_timers[STATUS_TIMER_COUNT];
static int ch03_unit_flags;
static int ch03_unit_hp_current;
static unsigned char ch03_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch03_slot_timers[STATUS_TIMER_COUNT];
static int ch03_slot_char_id;
static int ch03_slot_flags;
static int ch03_slot_hp_current;
static int ch03_slot_hp_max;
static int ch03_slot_mp_current;
static int ch03_slot_level;
static int ch03_enemy_hp_current;
static int ch03_enemy_hp_max;
static int ch03_enemy_flags;
static int ch03_chapter_id;
static int ch03_party_gold;

/* The staged battle array with the enemy the chapter left standing added
   behind the party member, and the player unit's side byte made explicit:
   stage_globals zeroes the record, and a zero side byte is the ENEMY side, so
   a party member left at the default would be destroyed by the first call. */
static void ch03_stage_globals(void)
{
    stage_globals();

    unit_image[RANDIS_UNIT].side = PLAYER_SIDE;

    unit_image[ENEMY_UNIT].char_id = ENEMY_CHAR_ID;
    unit_image[ENEMY_UNIT].side = ENEMY_SIDE;
    unit_image[ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    unit_image[ENEMY_UNIT].hp_current = ENEMY_HP_CURRENT;
    unit_image[ENEMY_UNIT].hp_max = ENEMY_HP_MAX;

    data_fdps_map_unit_count = CH03_UNIT_COUNT;
}

static void ch03_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch03_unit_spells[i] = unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch03_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch03_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch03_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch03_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch03_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch03_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch03_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch03_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch03_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch03_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch03_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch03_enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    ch03_enemy_hp_max = (int) unit_image[ENEMY_UNIT].hp_max;
    ch03_enemy_flags = (int) unit_image[ENEMY_UNIT].flags;
    ch03_chapter_id = data_fdps_chapter_current_chapter_id;
    ch03_party_gold = data_fdps_shared_party_total_gold;
}

static void run_chapter_03_handler(void)
{
    if (ch03_run_state != 0) {
        return;
    }
    ch03_run_state = 2;

    if (!ensure_fixture_archive()) {
        return;
    }

    ch03_stage_globals();

    fdps_chapter_03_end();

    ch03_capture();
    ch03_run_state = 1;
}

/* The map is cleared first, and cleared the way 00039e10 clears it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call would leave the
   enemy at 50. */
static void chapter_03_destroys_the_enemies_still_standing(void)
{
    run_chapter_03_handler();
    CHECK_EQ(ch03_run_state, 1);
    if (ch03_run_state != 1) {
        return;
    }

    CHECK_EQ(ch03_enemy_hp_current, 0);
    CHECK_EQ(ch03_enemy_hp_max, ENEMY_HP_MAX);
    CHECK_EQ(ch03_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch03_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN02.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_03_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_chapter_03_handler();
    CHECK_EQ(ch03_run_state, 1);
    if (ch03_run_state != 1) {
        return;
    }

    CHECK_EQ(ch03_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch03_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch03_slot_flags, 0);
    CHECK_EQ(ch03_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch03_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch03_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch03_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003a52c is the destroy sweep -- so both the live record's bitmap
   and the roster's copy of it stay at the zeroes the staging left.  A grant
   copied over from chapter 1's neighbour would show as byte 0 reading 0x01. */
static void chapter_03_grants_no_spell(void)
{
    int i;

    run_chapter_03_handler();
    CHECK_EQ(ch03_run_state, 1);
    if (ch03_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch03_unit_spells[i], 0);
        CHECK_EQ(ch03_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win02.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker in status_timers[5].  WIN00.DAT would have left
   status_timers[4] at 6 and WIN01.DAT status_timers[3] at 11 with the flags
   byte still 0, so opening either neighbour is a failed assertion rather than
   a silent pass.  The marker does not reach the roster copy, whose timers the
   writeback cleared before the script ran. */
static void chapter_03_victory_cutscene_is_win02_dat(void)
{
    run_chapter_03_handler();
    CHECK_EQ(ch03_run_state, 1);
    if (ch03_run_state != 1) {
        return;
    }

    CHECK_EQ(ch03_unit_timers[WIN02_MARKER_SLOT], WIN02_MARKER_VALUE);
    CHECK_EQ(ch03_unit_timers[WIN01_MARKER_SLOT], 0);
    CHECK_EQ(ch03_unit_timers[WIN00_MARKER_SLOT], 0);
    CHECK_EQ(ch03_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch03_slot_timers[WIN02_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The purse reading back
   unchanged is also what proves the sweep returned at all -- its panel loop
   waits for a key no test image queues.  The enemy the destroy call left at 0
   HP is not a roster member and is not billed for either. */
static void chapter_03_revive_charges_nothing_when_nobody_fell(void)
{
    run_chapter_03_handler();
    CHECK_EQ(ch03_run_state, 1);
    if (ch03_run_state != 1) {
        return;
    }

    CHECK_EQ(ch03_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 4's, as an assignment and not as a step
   from what was there: the run started it at 9. */
static void chapter_03_advances_the_chapter_index_to_chapter_four(void)
{
    run_chapter_03_handler();
    CHECK_EQ(ch03_run_state, 1);
    if (ch03_run_state != 1) {
        return;
    }

    CHECK_EQ(ch03_chapter_id, CH03_CHAPTER_ID_AFTER);
}

/* The fixture container belongs to this file only while its run needs it:
   tests/icon.c and tests/chinit1.c stage a container of the same name for
   their own fixtures and refuse to start if one is already standing. */
static void the_fixture_container_is_removed(void)
{
    if (!fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

void run_chend1_tests(void)
{
    RUN_TEST(randis_learns_spell_zero_on_the_battle_record);
    RUN_TEST(the_spell_is_banked_because_it_precedes_the_writeback);
    RUN_TEST(the_party_is_banked_before_the_cutscene_runs);
    RUN_TEST(the_victory_cutscene_is_win00_dat);
    RUN_TEST(the_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(the_chapter_index_is_advanced_to_chapter_two);
    RUN_TEST(chapter_02_banks_the_party_onto_the_roster);
    RUN_TEST(chapter_02_grants_no_spell);
    RUN_TEST(chapter_02_victory_cutscene_is_win01_dat);
    RUN_TEST(chapter_02_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_02_advances_the_chapter_index_to_chapter_three);
    RUN_TEST(chapter_03_destroys_the_enemies_still_standing);
    RUN_TEST(chapter_03_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_03_grants_no_spell);
    RUN_TEST(chapter_03_victory_cutscene_is_win02_dat);
    RUN_TEST(chapter_03_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_03_advances_the_chapter_index_to_chapter_four);
    RUN_TEST(the_fixture_container_is_removed);
}
