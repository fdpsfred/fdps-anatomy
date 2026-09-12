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
 * container.  The one staged here holds ten short scripts built to the
 * layout in resource_info/vfs.md and read by the game's own fdps_vfs_open and
 * fdps_vfs_load_file: WIN00.DAT, the member this handler names, and WIN01.DAT
 * through WIN09.DAT, the members the handlers after it name, one short script
 * per handler covered in this file.  Each writes a marker no other member
 * writes -- a slot of its own where the opcode still has one free, and
 * otherwise a value of its own in a shared slot.  A run that opened a
 * neighbouring member is therefore several failed assertions rather than a
 * hang.
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
#define FIXTURE_MEMBERS 10

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
/* The array holds six timers, so the three operands above have used every slot
   the opcode can reach.  WIN03.DAT therefore reuses WIN01.DAT's slot with a
   value of its own: the pair (slot 3, value 13) is reached by no other member,
   and WIN03.DAT also retires unit 0 where WIN01.DAT does not, so the two are
   told apart twice over. */
#define WIN03_MARKER_OPERAND 0
#define WIN03_MARKER_SLOT 3
#define WIN03_MARKER_VALUE 13

/* WIN04.DAT reuses WIN00.DAT's slot for the same reason, with a value of its
   own: the pair (slot 4, value 21) is reached by no other member, so a run
   that opened WIN00.DAT instead would read back 6 there. */
#define WIN04_MARKER_OPERAND 1
#define WIN04_MARKER_SLOT 4
#define WIN04_MARKER_VALUE 21

/* WIN05.DAT reuses WIN02.DAT's slot for the same reason, with a value of its
   own: the pair (slot 5, value 23) is reached by no other member, so a run
   that opened WIN02.DAT instead would read back 7 there. */
#define WIN05_MARKER_OPERAND 2
#define WIN05_MARKER_SLOT 5
#define WIN05_MARKER_VALUE 23

/* WIN06.DAT reuses WIN01.DAT's and WIN03.DAT's slot for the same reason, with
   a value neither of them writes: the pair (slot 3, value 17) is reached by no
   other member, so a run that opened either of those instead would read back
   11 or 13 there. */
#define WIN06_MARKER_OPERAND 0
#define WIN06_MARKER_SLOT 3
#define WIN06_MARKER_VALUE 17

/* WIN07.DAT reuses WIN02.DAT's and WIN05.DAT's slot for the same reason, with
   a value neither of them writes: the pair (slot 5, value 29) is reached by no
   other member, so a run that opened either of those instead would read back 7
   or 23 there. */
#define WIN07_MARKER_OPERAND 2
#define WIN07_MARKER_SLOT 5
#define WIN07_MARKER_VALUE 29

/* WIN08.DAT reuses WIN00.DAT's and WIN04.DAT's slot for the same reason, with
   a value neither of them writes: the pair (slot 4, value 31) is reached by no
   other member, so a run that opened either of those instead would read back 6
   or 21 there. */
#define WIN08_MARKER_OPERAND 1
#define WIN08_MARKER_SLOT 4
#define WIN08_MARKER_VALUE 31

/* WIN09.DAT reuses WIN01.DAT's, WIN03.DAT's and WIN06.DAT's slot for the same
   reason, with a value none of them writes: the pair (slot 3, value 19) is
   reached by no other member, so a run that opened any of those instead would
   read back 11, 13 or 17 there. */
#define WIN09_MARKER_OPERAND 0
#define WIN09_MARKER_SLOT 3
#define WIN09_MARKER_VALUE 19

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

/* WIN03.DAT: the member fdps_chapter_04_end names.  Like WIN02.DAT it retires
   battle unit 0 -- which is what makes the writeback's character-0 exemption
   the witness that the banking happened first -- and it writes a fourth marker
   value, so opening any of the other three members is visible in the record
   rather than in a silent pass. */
static unsigned char fixture_win03_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN03_MARKER_OPERAND, WIN03_MARKER_VALUE,
    0x00
};

/* WIN04.DAT: the member fdps_chapter_05_end names.  Like WIN02.DAT and
   WIN03.DAT it retires battle unit 0 -- which is what makes the writeback's
   character-0 exemption the witness that the banking happened first -- and it
   writes a fifth marker, so opening any of the other four members is visible
   in the record rather than in a silent pass. */
static unsigned char fixture_win04_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN04_MARKER_OPERAND, WIN04_MARKER_VALUE,
    0x00
};

/* WIN05.DAT: the member fdps_chapter_06_end names.  Like WIN02.DAT, WIN03.DAT
   and WIN04.DAT it retires battle unit 0 -- which is what makes the
   writeback's character-0 exemption the witness that the banking happened
   first -- and it writes a sixth marker, so opening any of the other five
   members is visible in the record rather than in a silent pass. */
static unsigned char fixture_win05_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN05_MARKER_OPERAND, WIN05_MARKER_VALUE,
    0x00
};

/* WIN06.DAT: the member fdps_chapter_07_end names.  Like WIN02.DAT through
   WIN05.DAT it retires battle unit 0 -- which is what makes the writeback's
   character-0 exemption the witness that the banking happened first -- and it
   writes a seventh marker, so opening any of the other six members is visible
   in the record rather than in a silent pass. */
static unsigned char fixture_win06_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN06_MARKER_OPERAND, WIN06_MARKER_VALUE,
    0x00
};

/* WIN07.DAT: the member fdps_chapter_08_end names.  Like WIN02.DAT through
   WIN06.DAT it retires battle unit 0 -- which is what makes the writeback's
   character-0 exemption the witness that the banking happened first -- and it
   writes an eighth marker, so opening any of the other seven members is
   visible in the record rather than in a silent pass. */
static unsigned char fixture_win07_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN07_MARKER_OPERAND, WIN07_MARKER_VALUE,
    0x00
};

/* WIN08.DAT: the member fdps_chapter_09_end names.  Like WIN02.DAT through
   WIN07.DAT it retires battle unit 0 -- which is what makes the writeback's
   character-0 exemption the witness that the banking happened first -- and it
   writes a ninth marker, so opening any of the other eight members is visible
   in the record rather than in a silent pass. */
static unsigned char fixture_win08_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN08_MARKER_OPERAND, WIN08_MARKER_VALUE,
    0x00
};

/* WIN09.DAT: the member fdps_chapter_10_end names.  Like WIN02.DAT through
   WIN08.DAT it retires battle unit 0 -- which is what makes the writeback's
   character-0 exemption the witness that the banking happened first -- and it
   writes a tenth marker, so opening any of the other nine members is visible
   in the record rather than in a silent pass. */
static unsigned char fixture_win09_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN09_MARKER_OPERAND, WIN09_MARKER_VALUE,
    0x00
};

static char *fixture_names[FIXTURE_MEMBERS] = {
    "WIN00.DAT", "WIN01.DAT", "WIN02.DAT", "WIN03.DAT", "WIN04.DAT",
    "WIN05.DAT", "WIN06.DAT", "WIN07.DAT", "WIN08.DAT", "WIN09.DAT"
};

static unsigned char *fixture_bytes[FIXTURE_MEMBERS] = {
    fixture_win00_dat, fixture_win01_dat, fixture_win02_dat,
    fixture_win03_dat, fixture_win04_dat, fixture_win05_dat,
    fixture_win06_dat, fixture_win07_dat, fixture_win08_dat,
    fixture_win09_dat
};

static int fixture_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_win00_dat), sizeof(fixture_win01_dat),
    sizeof(fixture_win02_dat), sizeof(fixture_win03_dat),
    sizeof(fixture_win04_dat), sizeof(fixture_win05_dat),
    sizeof(fixture_win06_dat), sizeof(fixture_win07_dat),
    sizeof(fixture_win08_dat), sizeof(fixture_win09_dat)
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

/* ------------------------------------------------------------------------
 * fdps_chapter_04_end at 0003a590.
 *
 * Chapter 3's body instruction for instruction, with its own script name and
 * its own stored index: four calls and one store, no branch.
 *
 *   0003a59c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003a5a1  CALL 0x00023980          the battle party is banked
 *   0003a5a6  MOV EAX,0x620d4 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win03.dat" is interpreted
 *   0003a5b4  CALL 0x00039e70          the fallen are revived
 *   0003a5b9  MOV dword ptr [0x00069cf4],0x4
 *
 * WHAT THE SWEEP IS ASKED TO DO, AND WHY IT IS STAGED SO IT HAS WORK.
 * fdps_battle_destroy_remaining_enemies stores a 16-bit zero into the
 * hit-point word at +0x40 of every unit whose side byte at +6 is 0, whether or
 * not that unit has already left the field, and then calls the death pass once
 * (src/btlend.c, 00039e10).  On chapter 4's own victory path it normally finds
 * the enemy side already emptied -- the chapter is cleared through the shared
 * end test, which records a clear only when no enemy is standing (btlend.h) --
 * so the staging below puts a retired enemy on the map with hit points still
 * on it.  That is the only arrangement in which the call has a visible effect
 * at all, and without it the case could not tell a handler that makes the call
 * from one that does not.
 *
 * The enemy is staged ALREADY RETIRED for the reason chapter 3's section above
 * gives: the death pass that ends the call collects units whose retired bit is
 * clear and whose hit points are 0, and for a non-empty list it spins them,
 * plays Explo.Saf and renders frames (src/death.c), none of which can run in a
 * test image.  With the bit already raised the list comes back empty and the
 * hit-point store is all the call did.
 *
 * The enemy also carries a character id no roster slot holds, so the writeback
 * that follows does not bank it and the roster assertions stay about the
 * party.
 *
 * WIN03.DAT retires battle unit 0 the way WIN00.DAT and WIN02.DAT do, so the
 * same witness for the order works here: the writeback skips a character-0
 * unit that has left the field, and roster slot 0 carrying the battle record
 * rather than the 0xa5 filler is what says the banking ran before the script.
 * ---------------------------------------------------------------------- */

/* The second battle unit the chapter 4 run stages: an enemy the sweep still
   has hit points to zero.  Its character id is one no roster slot holds and
   its retired bit is already raised, for the two reasons above. */
#define CH04_ENEMY_UNIT 1
#define CH04_ENEMY_CHAR_ID 9
#define CH04_ENEMY_HP_CURRENT 50
#define CH04_ENEMY_HP_MAX 50
#define CH04_UNIT_COUNT 2

/* The index the handler must leave: chapter 5, 0-based.  The run starts from
   CHAPTER_ID_BEFORE, 9, so the store is pinned as an assignment and not as a
   step from what was there. */
#define CH04_CHAPTER_ID_AFTER 4

static int ch04_run_state = 0;

static unsigned char ch04_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch04_unit_timers[STATUS_TIMER_COUNT];
static int ch04_unit_flags;
static int ch04_unit_hp_current;
static unsigned char ch04_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch04_slot_timers[STATUS_TIMER_COUNT];
static int ch04_slot_char_id;
static int ch04_slot_flags;
static int ch04_slot_hp_current;
static int ch04_slot_hp_max;
static int ch04_slot_mp_current;
static int ch04_slot_level;
static int ch04_enemy_hp_current;
static int ch04_enemy_hp_max;
static int ch04_enemy_flags;
static int ch04_chapter_id;
static int ch04_party_gold;

/* The staged battle array with the enemy added behind the party member, and
   the player unit's side byte made explicit: stage_globals zeroes the record,
   and a zero side byte is the ENEMY side, so a party member left at the
   default would be swept by the first call. */
static void ch04_stage_globals(void)
{
    stage_globals();

    unit_image[RANDIS_UNIT].side = PLAYER_SIDE;

    unit_image[CH04_ENEMY_UNIT].char_id = CH04_ENEMY_CHAR_ID;
    unit_image[CH04_ENEMY_UNIT].side = ENEMY_SIDE;
    unit_image[CH04_ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    unit_image[CH04_ENEMY_UNIT].hp_current = CH04_ENEMY_HP_CURRENT;
    unit_image[CH04_ENEMY_UNIT].hp_max = CH04_ENEMY_HP_MAX;

    data_fdps_map_unit_count = CH04_UNIT_COUNT;
}

static void ch04_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch04_unit_spells[i] = unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch04_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch04_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch04_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch04_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch04_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch04_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch04_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch04_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch04_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch04_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch04_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch04_enemy_hp_current = (int) unit_image[CH04_ENEMY_UNIT].hp_current;
    ch04_enemy_hp_max = (int) unit_image[CH04_ENEMY_UNIT].hp_max;
    ch04_enemy_flags = (int) unit_image[CH04_ENEMY_UNIT].flags;
    ch04_chapter_id = data_fdps_chapter_current_chapter_id;
    ch04_party_gold = data_fdps_shared_party_total_gold;
}

static void run_chapter_04_handler(void)
{
    if (ch04_run_state != 0) {
        return;
    }
    ch04_run_state = 2;

    if (!ensure_fixture_archive()) {
        return;
    }

    ch04_stage_globals();

    fdps_chapter_04_end();

    ch04_capture();
    ch04_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1 and 2 have, would leave the enemy at 50. */
static void chapter_04_sweeps_the_enemy_side(void)
{
    run_chapter_04_handler();
    CHECK_EQ(ch04_run_state, 1);
    if (ch04_run_state != 1) {
        return;
    }

    CHECK_EQ(ch04_enemy_hp_current, 0);
    CHECK_EQ(ch04_enemy_hp_max, CH04_ENEMY_HP_MAX);
    CHECK_EQ(ch04_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch04_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN03.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_04_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_chapter_04_handler();
    CHECK_EQ(ch04_run_state, 1);
    if (ch04_run_state != 1) {
        return;
    }

    CHECK_EQ(ch04_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch04_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch04_slot_flags, 0);
    CHECK_EQ(ch04_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch04_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch04_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch04_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003a59c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from chapter 1's neighbour would show as byte 0 reading 0x01. */
static void chapter_04_grants_no_spell(void)
{
    int i;

    run_chapter_04_handler();
    CHECK_EQ(ch04_run_state, 1);
    if (ch04_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch04_unit_spells[i], 0);
        CHECK_EQ(ch04_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win03.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 13 in status_timers[3].  WIN01.DAT writes that same
   slot with 11 and raises no flag, WIN00.DAT would have left status_timers[4]
   at 6 and WIN02.DAT status_timers[5] at 7, so opening any neighbour is a
   failed assertion rather than a silent pass.  The marker does not reach the
   roster copy, whose timers the writeback cleared before the script ran. */
static void chapter_04_victory_cutscene_is_win03_dat(void)
{
    run_chapter_04_handler();
    CHECK_EQ(ch04_run_state, 1);
    if (ch04_run_state != 1) {
        return;
    }

    CHECK_EQ(ch04_unit_timers[WIN03_MARKER_SLOT], WIN03_MARKER_VALUE);
    CHECK_EQ(ch04_unit_timers[WIN00_MARKER_SLOT], 0);
    CHECK_EQ(ch04_unit_timers[WIN02_MARKER_SLOT], 0);
    CHECK_EQ(ch04_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch04_slot_timers[WIN03_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The purse reading back
   unchanged is also what proves the sweep returned at all -- its panel loop
   waits for a key no test image queues.  The enemy the first call left at 0 HP
   is not a roster member and is not billed for either. */
static void chapter_04_revive_charges_nothing_when_nobody_fell(void)
{
    run_chapter_04_handler();
    CHECK_EQ(ch04_run_state, 1);
    if (ch04_run_state != 1) {
        return;
    }

    CHECK_EQ(ch04_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 5's, as an assignment and not as a step
   from what was there: the run started it at 9. */
static void chapter_04_advances_the_chapter_index_to_chapter_five(void)
{
    run_chapter_04_handler();
    CHECK_EQ(ch04_run_state, 1);
    if (ch04_run_state != 1) {
        return;
    }

    CHECK_EQ(ch04_chapter_id, CH04_CHAPTER_ID_AFTER);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_05_end at 0003a600.
 *
 * Chapters 3 and 4's body instruction for instruction, with its own script
 * name and its own stored index: four calls and one store, no branch.
 *
 *   0003a60c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003a611  CALL 0x00023980          the battle party is banked
 *   0003a616  MOV EAX,0x620e0 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win04.dat" is interpreted
 *   0003a624  CALL 0x00039e70          the fallen are revived
 *   0003a629  MOV dword ptr [0x00069cf4],0x5
 *
 * WHAT THE SWEEP IS ASKED TO DO, AND WHY IT IS STAGED SO IT HAS WORK.
 * fdps_battle_destroy_remaining_enemies stores a 16-bit zero into the
 * hit-point word at +0x40 of every unit whose side byte at +6 is 0, whether or
 * not that unit has already left the field, and then calls the death pass once
 * (src/btlend.c, 00039e10).  On chapter 5's own victory path it normally finds
 * the enemy side already emptied -- the chapter is cleared through the shared
 * end test, which records a clear only when no enemy is standing (btlend.h) --
 * so the staging below puts a retired enemy on the map with hit points still
 * on it.  That is the only arrangement in which the call has a visible effect
 * at all, and without it the case could not tell a handler that makes the call
 * from one that does not.
 *
 * The enemy is staged ALREADY RETIRED for the reason chapter 3's section above
 * gives: the death pass that ends the call collects units whose retired bit is
 * clear and whose hit points are 0, and for a non-empty list it spins them,
 * plays Explo.Saf and renders frames (src/death.c), none of which can run in a
 * test image.  With the bit already raised the list comes back empty and the
 * hit-point store is all the call did.
 *
 * The enemy also carries a character id no roster slot holds, so the writeback
 * that follows does not bank it and the roster assertions stay about the
 * party.
 *
 * WIN04.DAT retires battle unit 0 the way WIN02.DAT and WIN03.DAT do, so the
 * same witness for the order works here: the writeback skips a character-0
 * unit that has left the field, and roster slot 0 carrying the battle record
 * rather than the 0xa5 filler is what says the banking ran before the script.
 * ---------------------------------------------------------------------- */

/* The second battle unit the chapter 5 run stages: an enemy the sweep still
   has hit points to zero.  Its character id is one no roster slot holds and
   its retired bit is already raised, for the two reasons above. */
#define CH05_ENEMY_UNIT 1
#define CH05_ENEMY_CHAR_ID 9
#define CH05_ENEMY_HP_CURRENT 50
#define CH05_ENEMY_HP_MAX 50
#define CH05_UNIT_COUNT 2

/* The index the handler must leave: chapter 6, 0-based.  The run starts from
   CHAPTER_ID_BEFORE, 9, so the store is pinned as an assignment and not as a
   step from what was there. */
#define CH05_CHAPTER_ID_AFTER 5

static int ch05_run_state = 0;

static unsigned char ch05_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch05_unit_timers[STATUS_TIMER_COUNT];
static int ch05_unit_flags;
static int ch05_unit_hp_current;
static unsigned char ch05_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch05_slot_timers[STATUS_TIMER_COUNT];
static int ch05_slot_char_id;
static int ch05_slot_flags;
static int ch05_slot_hp_current;
static int ch05_slot_hp_max;
static int ch05_slot_mp_current;
static int ch05_slot_level;
static int ch05_enemy_hp_current;
static int ch05_enemy_hp_max;
static int ch05_enemy_flags;
static int ch05_chapter_id;
static int ch05_party_gold;

/* The staged battle array with the enemy added behind the party member, and
   the player unit's side byte made explicit: stage_globals zeroes the record,
   and a zero side byte is the ENEMY side, so a party member left at the
   default would be swept by the first call. */
static void ch05_stage_globals(void)
{
    stage_globals();

    unit_image[RANDIS_UNIT].side = PLAYER_SIDE;

    unit_image[CH05_ENEMY_UNIT].char_id = CH05_ENEMY_CHAR_ID;
    unit_image[CH05_ENEMY_UNIT].side = ENEMY_SIDE;
    unit_image[CH05_ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    unit_image[CH05_ENEMY_UNIT].hp_current = CH05_ENEMY_HP_CURRENT;
    unit_image[CH05_ENEMY_UNIT].hp_max = CH05_ENEMY_HP_MAX;

    data_fdps_map_unit_count = CH05_UNIT_COUNT;
}

static void ch05_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch05_unit_spells[i] = unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch05_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch05_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch05_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch05_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch05_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch05_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch05_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch05_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch05_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch05_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch05_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch05_enemy_hp_current = (int) unit_image[CH05_ENEMY_UNIT].hp_current;
    ch05_enemy_hp_max = (int) unit_image[CH05_ENEMY_UNIT].hp_max;
    ch05_enemy_flags = (int) unit_image[CH05_ENEMY_UNIT].flags;
    ch05_chapter_id = data_fdps_chapter_current_chapter_id;
    ch05_party_gold = data_fdps_shared_party_total_gold;
}

static void run_chapter_05_handler(void)
{
    if (ch05_run_state != 0) {
        return;
    }
    ch05_run_state = 2;

    if (!ensure_fixture_archive()) {
        return;
    }

    ch05_stage_globals();

    fdps_chapter_05_end();

    ch05_capture();
    ch05_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1 and 2 have, would leave the enemy at 50. */
static void chapter_05_sweeps_the_enemy_side(void)
{
    run_chapter_05_handler();
    CHECK_EQ(ch05_run_state, 1);
    if (ch05_run_state != 1) {
        return;
    }

    CHECK_EQ(ch05_enemy_hp_current, 0);
    CHECK_EQ(ch05_enemy_hp_max, CH05_ENEMY_HP_MAX);
    CHECK_EQ(ch05_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch05_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN04.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_05_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_chapter_05_handler();
    CHECK_EQ(ch05_run_state, 1);
    if (ch05_run_state != 1) {
        return;
    }

    CHECK_EQ(ch05_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch05_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch05_slot_flags, 0);
    CHECK_EQ(ch05_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch05_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch05_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch05_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003a60c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from chapter 1's neighbour would show as byte 0 reading 0x01. */
static void chapter_05_grants_no_spell(void)
{
    int i;

    run_chapter_05_handler();
    CHECK_EQ(ch05_run_state, 1);
    if (ch05_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch05_unit_spells[i], 0);
        CHECK_EQ(ch05_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win04.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 21 in status_timers[4].  WIN00.DAT writes that same
   slot with 6, WIN01.DAT would have left status_timers[3] at 11 with no flag
   raised, WIN02.DAT status_timers[5] at 7 and WIN03.DAT status_timers[3] at
   13, so opening any neighbour is a failed assertion rather than a silent
   pass.  The marker does not reach the roster copy, whose timers the writeback
   cleared before the script ran. */
static void chapter_05_victory_cutscene_is_win04_dat(void)
{
    run_chapter_05_handler();
    CHECK_EQ(ch05_run_state, 1);
    if (ch05_run_state != 1) {
        return;
    }

    CHECK_EQ(ch05_unit_timers[WIN04_MARKER_SLOT], WIN04_MARKER_VALUE);
    CHECK_EQ(ch05_unit_timers[WIN01_MARKER_SLOT], 0);
    CHECK_EQ(ch05_unit_timers[WIN02_MARKER_SLOT], 0);
    CHECK_EQ(ch05_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch05_slot_timers[WIN04_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The purse reading back
   unchanged is also what proves the sweep returned at all -- its panel loop
   waits for a key no test image queues.  The enemy the first call left at 0 HP
   is not a roster member and is not billed for either. */
static void chapter_05_revive_charges_nothing_when_nobody_fell(void)
{
    run_chapter_05_handler();
    CHECK_EQ(ch05_run_state, 1);
    if (ch05_run_state != 1) {
        return;
    }

    CHECK_EQ(ch05_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 6's, as an assignment and not as a step
   from what was there: the run started it at 9. */
static void chapter_05_advances_the_chapter_index_to_chapter_six(void)
{
    run_chapter_05_handler();
    CHECK_EQ(ch05_run_state, 1);
    if (ch05_run_state != 1) {
        return;
    }

    CHECK_EQ(ch05_chapter_id, CH05_CHAPTER_ID_AFTER);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_06_end at 0003a670.
 *
 * Chapters 3, 4 and 5's body instruction for instruction, with its own script
 * name and its own stored index: four calls and one store, no branch.
 *
 *   0003a67c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003a681  CALL 0x00023980          the battle party is banked
 *   0003a686  MOV EAX,0x620ec / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win05.dat" is interpreted
 *   0003a694  CALL 0x00039e70          the fallen are revived
 *   0003a699  MOV dword ptr [0x00069cf4],0x6
 *
 * WHAT THE SWEEP IS ASKED TO DO, AND WHY IT IS STAGED SO IT HAS WORK.
 * fdps_battle_destroy_remaining_enemies stores a 16-bit zero into the
 * hit-point word at +0x40 of every unit whose side byte at +6 is 0, whether or
 * not that unit has already left the field, and then calls the death pass once
 * (src/btlend.c, 00039e10).  On chapter 6's own victory path it normally finds
 * the enemy side already emptied -- the chapter is cleared through the shared
 * end test, which records a clear only when no enemy is standing (btlend.h) --
 * so the staging below puts a retired enemy on the map with hit points still
 * on it.  That is the only arrangement in which the call has a visible effect
 * at all, and without it the case could not tell a handler that makes the call
 * from one that does not.
 *
 * The enemy is staged ALREADY RETIRED for the reason chapter 3's section above
 * gives: the death pass that ends the call collects units whose retired bit is
 * clear and whose hit points are 0, and for a non-empty list it spins them,
 * plays Explo.Saf and renders frames (src/death.c), none of which can run in a
 * test image.  With the bit already raised the list comes back empty and the
 * hit-point store is all the call did.
 *
 * The enemy also carries a character id no roster slot holds, so the writeback
 * that follows does not bank it and the roster assertions stay about the
 * party.
 *
 * WIN05.DAT retires battle unit 0 the way WIN02.DAT, WIN03.DAT and WIN04.DAT
 * do, so the same witness for the order works here: the writeback skips a
 * character-0 unit that has left the field, and roster slot 0 carrying the
 * battle record rather than the 0xa5 filler is what says the banking ran
 * before the script.
 * ---------------------------------------------------------------------- */

/* The second battle unit the chapter 6 run stages: an enemy the sweep still
   has hit points to zero.  Its character id is one no roster slot holds and
   its retired bit is already raised, for the two reasons above. */
#define CH06_ENEMY_UNIT 1
#define CH06_ENEMY_CHAR_ID 9
#define CH06_ENEMY_HP_CURRENT 50
#define CH06_ENEMY_HP_MAX 50
#define CH06_UNIT_COUNT 2

/* The index the handler must leave: chapter 7, 0-based.  The run starts from
   CHAPTER_ID_BEFORE, 9, so the store is pinned as an assignment and not as a
   step from what was there. */
#define CH06_CHAPTER_ID_AFTER 6

static int ch06_run_state = 0;

static unsigned char ch06_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch06_unit_timers[STATUS_TIMER_COUNT];
static int ch06_unit_flags;
static int ch06_unit_hp_current;
static unsigned char ch06_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch06_slot_timers[STATUS_TIMER_COUNT];
static int ch06_slot_char_id;
static int ch06_slot_flags;
static int ch06_slot_hp_current;
static int ch06_slot_hp_max;
static int ch06_slot_mp_current;
static int ch06_slot_level;
static int ch06_enemy_hp_current;
static int ch06_enemy_hp_max;
static int ch06_enemy_flags;
static int ch06_chapter_id;
static int ch06_party_gold;

/* The staged battle array with the enemy added behind the party member, and
   the player unit's side byte made explicit: stage_globals zeroes the record,
   and a zero side byte is the ENEMY side, so a party member left at the
   default would be swept by the first call. */
static void ch06_stage_globals(void)
{
    stage_globals();

    unit_image[RANDIS_UNIT].side = PLAYER_SIDE;

    unit_image[CH06_ENEMY_UNIT].char_id = CH06_ENEMY_CHAR_ID;
    unit_image[CH06_ENEMY_UNIT].side = ENEMY_SIDE;
    unit_image[CH06_ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    unit_image[CH06_ENEMY_UNIT].hp_current = CH06_ENEMY_HP_CURRENT;
    unit_image[CH06_ENEMY_UNIT].hp_max = CH06_ENEMY_HP_MAX;

    data_fdps_map_unit_count = CH06_UNIT_COUNT;
}

static void ch06_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch06_unit_spells[i] = unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch06_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch06_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch06_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch06_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch06_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch06_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch06_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch06_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch06_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch06_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch06_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch06_enemy_hp_current = (int) unit_image[CH06_ENEMY_UNIT].hp_current;
    ch06_enemy_hp_max = (int) unit_image[CH06_ENEMY_UNIT].hp_max;
    ch06_enemy_flags = (int) unit_image[CH06_ENEMY_UNIT].flags;
    ch06_chapter_id = data_fdps_chapter_current_chapter_id;
    ch06_party_gold = data_fdps_shared_party_total_gold;
}

static void run_chapter_06_handler(void)
{
    if (ch06_run_state != 0) {
        return;
    }
    ch06_run_state = 2;

    if (!ensure_fixture_archive()) {
        return;
    }

    ch06_stage_globals();

    fdps_chapter_06_end();

    ch06_capture();
    ch06_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1 and 2 have, would leave the enemy at 50. */
static void chapter_06_sweeps_the_enemy_side(void)
{
    run_chapter_06_handler();
    CHECK_EQ(ch06_run_state, 1);
    if (ch06_run_state != 1) {
        return;
    }

    CHECK_EQ(ch06_enemy_hp_current, 0);
    CHECK_EQ(ch06_enemy_hp_max, CH06_ENEMY_HP_MAX);
    CHECK_EQ(ch06_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch06_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN05.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_06_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_chapter_06_handler();
    CHECK_EQ(ch06_run_state, 1);
    if (ch06_run_state != 1) {
        return;
    }

    CHECK_EQ(ch06_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch06_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch06_slot_flags, 0);
    CHECK_EQ(ch06_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch06_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch06_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch06_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003a67c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from chapter 1's neighbour would show as byte 0 reading 0x01. */
static void chapter_06_grants_no_spell(void)
{
    int i;

    run_chapter_06_handler();
    CHECK_EQ(ch06_run_state, 1);
    if (ch06_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch06_unit_spells[i], 0);
        CHECK_EQ(ch06_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win05.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 23 in status_timers[5].  WIN02.DAT writes that same
   slot with 7, WIN00.DAT and WIN04.DAT write status_timers[4] with 6 and 21,
   and WIN01.DAT and WIN03.DAT write status_timers[3] with 11 and 13, so
   opening any neighbour is a failed assertion rather than a silent pass.  The
   marker does not reach the roster copy, whose timers the writeback cleared
   before the script ran. */
static void chapter_06_victory_cutscene_is_win05_dat(void)
{
    run_chapter_06_handler();
    CHECK_EQ(ch06_run_state, 1);
    if (ch06_run_state != 1) {
        return;
    }

    CHECK_EQ(ch06_unit_timers[WIN05_MARKER_SLOT], WIN05_MARKER_VALUE);
    CHECK_EQ(ch06_unit_timers[WIN01_MARKER_SLOT], 0);
    CHECK_EQ(ch06_unit_timers[WIN00_MARKER_SLOT], 0);
    CHECK_EQ(ch06_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch06_slot_timers[WIN05_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The purse reading back
   unchanged is also what proves the sweep returned at all -- its panel loop
   waits for a key no test image queues.  The enemy the first call left at 0 HP
   is not a roster member and is not billed for either. */
static void chapter_06_revive_charges_nothing_when_nobody_fell(void)
{
    run_chapter_06_handler();
    CHECK_EQ(ch06_run_state, 1);
    if (ch06_run_state != 1) {
        return;
    }

    CHECK_EQ(ch06_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 7's, as an assignment and not as a step
   from what was there: the run started it at 9. */
static void chapter_06_advances_the_chapter_index_to_chapter_seven(void)
{
    run_chapter_06_handler();
    CHECK_EQ(ch06_run_state, 1);
    if (ch06_run_state != 1) {
        return;
    }

    CHECK_EQ(ch06_chapter_id, CH06_CHAPTER_ID_AFTER);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_07_end at 0003a6d0.
 *
 * Chapters 3 to 6's body instruction for instruction, with its own script name
 * and its own stored index: four calls and one store, no branch.
 *
 *   0003a6dc  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003a6e1  CALL 0x00023980          the battle party is banked
 *   0003a6e6  MOV EAX,0x620f8 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win06.dat" is interpreted
 *   0003a6f4  CALL 0x00039e70          the fallen are revived
 *   0003a6f9  MOV dword ptr [0x00069cf4],0x7
 *
 * WHAT THE SWEEP IS ASKED TO DO, AND WHY IT IS STAGED SO IT HAS WORK.
 * fdps_battle_destroy_remaining_enemies stores a 16-bit zero into the
 * hit-point word at +0x40 of every unit whose side byte at +6 is 0, whether or
 * not that unit has already left the field, and then calls the death pass once
 * (src/btlend.c, 00039e10).  On chapter 7's own victory path it normally finds
 * the enemy side already emptied -- the chapter is cleared through the shared
 * end test alone, which records a clear only when no enemy is standing
 * (fdps_chapter_07_post_action, chpost1.h; btlend.h) -- so the staging below
 * puts a retired enemy on the map with hit points still on it.  That is the
 * only arrangement in which the call has a visible effect at all, and without
 * it the case could not tell a handler that makes the call from one that does
 * not.
 *
 * The enemy is staged ALREADY RETIRED for the reason chapter 3's section above
 * gives: the death pass that ends the call collects units whose retired bit is
 * clear and whose hit points are 0, and for a non-empty list it spins them,
 * plays Explo.Saf and renders frames (src/death.c), none of which can run in a
 * test image.  With the bit already raised the list comes back empty and the
 * hit-point store is all the call did.
 *
 * The enemy also carries a character id no roster slot holds, so the writeback
 * that follows does not bank it and the roster assertions stay about the
 * party.
 *
 * WIN06.DAT retires battle unit 0 the way WIN02.DAT through WIN05.DAT do, so
 * the same witness for the order works here: the writeback skips a character-0
 * unit that has left the field, and roster slot 0 carrying the battle record
 * rather than the 0xa5 filler is what says the banking ran before the script.
 * ---------------------------------------------------------------------- */

/* The second battle unit the chapter 7 run stages: an enemy the sweep still
   has hit points to zero.  Its character id is one no roster slot holds and
   its retired bit is already raised, for the two reasons above. */
#define CH07_ENEMY_UNIT 1
#define CH07_ENEMY_CHAR_ID 9
#define CH07_ENEMY_HP_CURRENT 50
#define CH07_ENEMY_HP_MAX 50
#define CH07_UNIT_COUNT 2

/* The index the handler must leave: chapter 8, 0-based.  The run starts from
   CHAPTER_ID_BEFORE, 9, so the store is pinned as an assignment and not as a
   step from what was there. */
#define CH07_CHAPTER_ID_AFTER 7

static int ch07_run_state = 0;

static unsigned char ch07_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch07_unit_timers[STATUS_TIMER_COUNT];
static int ch07_unit_flags;
static int ch07_unit_hp_current;
static unsigned char ch07_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch07_slot_timers[STATUS_TIMER_COUNT];
static int ch07_slot_char_id;
static int ch07_slot_flags;
static int ch07_slot_hp_current;
static int ch07_slot_hp_max;
static int ch07_slot_mp_current;
static int ch07_slot_level;
static int ch07_enemy_hp_current;
static int ch07_enemy_hp_max;
static int ch07_enemy_flags;
static int ch07_chapter_id;
static int ch07_party_gold;

/* The staged battle array with the enemy added behind the party member, and
   the player unit's side byte made explicit: stage_globals zeroes the record,
   and a zero side byte is the ENEMY side, so a party member left at the
   default would be swept by the first call. */
static void ch07_stage_globals(void)
{
    stage_globals();

    unit_image[RANDIS_UNIT].side = PLAYER_SIDE;

    unit_image[CH07_ENEMY_UNIT].char_id = CH07_ENEMY_CHAR_ID;
    unit_image[CH07_ENEMY_UNIT].side = ENEMY_SIDE;
    unit_image[CH07_ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    unit_image[CH07_ENEMY_UNIT].hp_current = CH07_ENEMY_HP_CURRENT;
    unit_image[CH07_ENEMY_UNIT].hp_max = CH07_ENEMY_HP_MAX;

    data_fdps_map_unit_count = CH07_UNIT_COUNT;
}

static void ch07_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch07_unit_spells[i] = unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch07_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch07_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch07_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch07_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch07_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch07_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch07_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch07_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch07_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch07_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch07_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch07_enemy_hp_current = (int) unit_image[CH07_ENEMY_UNIT].hp_current;
    ch07_enemy_hp_max = (int) unit_image[CH07_ENEMY_UNIT].hp_max;
    ch07_enemy_flags = (int) unit_image[CH07_ENEMY_UNIT].flags;
    ch07_chapter_id = data_fdps_chapter_current_chapter_id;
    ch07_party_gold = data_fdps_shared_party_total_gold;
}

static void run_chapter_07_handler(void)
{
    if (ch07_run_state != 0) {
        return;
    }
    ch07_run_state = 2;

    if (!ensure_fixture_archive()) {
        return;
    }

    ch07_stage_globals();

    fdps_chapter_07_end();

    ch07_capture();
    ch07_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1 and 2 have, would leave the enemy at 50. */
static void chapter_07_sweeps_the_enemy_side(void)
{
    run_chapter_07_handler();
    CHECK_EQ(ch07_run_state, 1);
    if (ch07_run_state != 1) {
        return;
    }

    CHECK_EQ(ch07_enemy_hp_current, 0);
    CHECK_EQ(ch07_enemy_hp_max, CH07_ENEMY_HP_MAX);
    CHECK_EQ(ch07_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch07_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN06.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_07_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_chapter_07_handler();
    CHECK_EQ(ch07_run_state, 1);
    if (ch07_run_state != 1) {
        return;
    }

    CHECK_EQ(ch07_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch07_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch07_slot_flags, 0);
    CHECK_EQ(ch07_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch07_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch07_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch07_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003a6dc is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from chapter 1's neighbour would show as byte 0 reading 0x01. */
static void chapter_07_grants_no_spell(void)
{
    int i;

    run_chapter_07_handler();
    CHECK_EQ(ch07_run_state, 1);
    if (ch07_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch07_unit_spells[i], 0);
        CHECK_EQ(ch07_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win06.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 17 in status_timers[3].  WIN01.DAT and WIN03.DAT
   write that same slot with 11 and 13, WIN00.DAT and WIN04.DAT write
   status_timers[4] with 6 and 21, and WIN02.DAT and WIN05.DAT write
   status_timers[5] with 7 and 23, so opening any neighbour is a failed
   assertion rather than a silent pass.  The marker does not reach the roster
   copy, whose timers the writeback cleared before the script ran. */
static void chapter_07_victory_cutscene_is_win06_dat(void)
{
    run_chapter_07_handler();
    CHECK_EQ(ch07_run_state, 1);
    if (ch07_run_state != 1) {
        return;
    }

    CHECK_EQ(ch07_unit_timers[WIN06_MARKER_SLOT], WIN06_MARKER_VALUE);
    CHECK_EQ(ch07_unit_timers[WIN00_MARKER_SLOT], 0);
    CHECK_EQ(ch07_unit_timers[WIN02_MARKER_SLOT], 0);
    CHECK_EQ(ch07_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch07_slot_timers[WIN06_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The purse reading back
   unchanged is also what proves the sweep returned at all -- its panel loop
   waits for a key no test image queues.  The enemy the first call left at 0 HP
   is not a roster member and is not billed for either. */
static void chapter_07_revive_charges_nothing_when_nobody_fell(void)
{
    run_chapter_07_handler();
    CHECK_EQ(ch07_run_state, 1);
    if (ch07_run_state != 1) {
        return;
    }

    CHECK_EQ(ch07_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 8's, as an assignment and not as a step
   from what was there: the run started it at 9. */
static void chapter_07_advances_the_chapter_index_to_chapter_eight(void)
{
    run_chapter_07_handler();
    CHECK_EQ(ch07_run_state, 1);
    if (ch07_run_state != 1) {
        return;
    }

    CHECK_EQ(ch07_chapter_id, CH07_CHAPTER_ID_AFTER);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_08_end at 0003a800.
 *
 * Chapters 3 to 7's body instruction for instruction, with its own script name
 * and its own stored index: four calls and one store, no branch.
 *
 *   0003a80c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003a811  CALL 0x00023980          the battle party is banked
 *   0003a816  MOV EAX,0x62104 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win07.dat" is interpreted
 *   0003a824  CALL 0x00039e70          the fallen are revived
 *   0003a829  MOV dword ptr [0x00069cf4],0x8
 *
 * WHY THE SWEEP MATTERS MORE HERE THAN IN THE SIX SLOTS BEFORE IT.  Chapter 8
 * is the underground-prison chapter, and fdps_chapter_08_post_action is the
 * only test in its family that never consults the enemy side: it records the
 * clear when the four captives are off the battlefield with at least one of
 * them escaped alive (chpost1.h).  So unlike chapters 4 to 7, where the sweep
 * normally finds the enemy side already empty, chapter 8 reaches this handler
 * with enemies still standing and the sweep is what retires them.
 *
 * The staging still puts the enemy on the map ALREADY RETIRED, for the reason
 * chapter 3's section above gives and not because the game reaches the handler
 * that way: the death pass that ends fdps_battle_destroy_remaining_enemies
 * collects units whose retired bit is clear and whose hit points are 0, and
 * for a non-empty list it spins them, plays Explo.Saf and renders frames
 * (src/death.c), none of which can run in a test image.  With the bit already
 * raised the list comes back empty and the hit-point store is all the call
 * did -- which is the part this file can witness.
 *
 * The enemy also carries a character id no roster slot holds, so the writeback
 * that follows does not bank it and the roster assertions stay about the
 * party.
 *
 * WIN07.DAT retires battle unit 0 the way WIN02.DAT through WIN06.DAT do, so
 * the same witness for the order works here: the writeback skips a character-0
 * unit that has left the field, and roster slot 0 carrying the battle record
 * rather than the 0xa5 filler is what says the banking ran before the script.
 * ---------------------------------------------------------------------- */

/* The second battle unit the chapter 8 run stages: an enemy the sweep still
   has hit points to zero.  Its character id is one no roster slot holds and
   its retired bit is already raised, for the two reasons above. */
#define CH08_ENEMY_UNIT 1
#define CH08_ENEMY_CHAR_ID 9
#define CH08_ENEMY_HP_CURRENT 50
#define CH08_ENEMY_HP_MAX 50
#define CH08_UNIT_COUNT 2

/* The index the handler must leave: chapter 9, 0-based.  The run starts from
   CHAPTER_ID_BEFORE, 9, so the store is pinned as an assignment and not as a
   step from what was there -- a step would read back 10. */
#define CH08_CHAPTER_ID_AFTER 8

static int ch08_run_state = 0;

static unsigned char ch08_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch08_unit_timers[STATUS_TIMER_COUNT];
static int ch08_unit_flags;
static int ch08_unit_hp_current;
static unsigned char ch08_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch08_slot_timers[STATUS_TIMER_COUNT];
static int ch08_slot_char_id;
static int ch08_slot_flags;
static int ch08_slot_hp_current;
static int ch08_slot_hp_max;
static int ch08_slot_mp_current;
static int ch08_slot_level;
static int ch08_enemy_hp_current;
static int ch08_enemy_hp_max;
static int ch08_enemy_flags;
static int ch08_chapter_id;
static int ch08_party_gold;

/* The staged battle array with the enemy added behind the party member, and
   the player unit's side byte made explicit: stage_globals zeroes the record,
   and a zero side byte is the ENEMY side, so a party member left at the
   default would be swept by the first call. */
static void ch08_stage_globals(void)
{
    stage_globals();

    unit_image[RANDIS_UNIT].side = PLAYER_SIDE;

    unit_image[CH08_ENEMY_UNIT].char_id = CH08_ENEMY_CHAR_ID;
    unit_image[CH08_ENEMY_UNIT].side = ENEMY_SIDE;
    unit_image[CH08_ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    unit_image[CH08_ENEMY_UNIT].hp_current = CH08_ENEMY_HP_CURRENT;
    unit_image[CH08_ENEMY_UNIT].hp_max = CH08_ENEMY_HP_MAX;

    data_fdps_map_unit_count = CH08_UNIT_COUNT;
}

static void ch08_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch08_unit_spells[i] = unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch08_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch08_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch08_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch08_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch08_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch08_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch08_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch08_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch08_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch08_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch08_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch08_enemy_hp_current = (int) unit_image[CH08_ENEMY_UNIT].hp_current;
    ch08_enemy_hp_max = (int) unit_image[CH08_ENEMY_UNIT].hp_max;
    ch08_enemy_flags = (int) unit_image[CH08_ENEMY_UNIT].flags;
    ch08_chapter_id = data_fdps_chapter_current_chapter_id;
    ch08_party_gold = data_fdps_shared_party_total_gold;
}

static void run_chapter_08_handler(void)
{
    if (ch08_run_state != 0) {
        return;
    }
    ch08_run_state = 2;

    if (!ensure_fixture_archive()) {
        return;
    }

    ch08_stage_globals();

    fdps_chapter_08_end();

    ch08_capture();
    ch08_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1 and 2 have, would leave the enemy at 50. */
static void chapter_08_sweeps_the_enemy_side(void)
{
    run_chapter_08_handler();
    CHECK_EQ(ch08_run_state, 1);
    if (ch08_run_state != 1) {
        return;
    }

    CHECK_EQ(ch08_enemy_hp_current, 0);
    CHECK_EQ(ch08_enemy_hp_max, CH08_ENEMY_HP_MAX);
    CHECK_EQ(ch08_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch08_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN07.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_08_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_chapter_08_handler();
    CHECK_EQ(ch08_run_state, 1);
    if (ch08_run_state != 1) {
        return;
    }

    CHECK_EQ(ch08_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch08_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch08_slot_flags, 0);
    CHECK_EQ(ch08_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch08_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch08_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch08_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003a80c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from chapter 1's neighbour would show as byte 0 reading 0x01. */
static void chapter_08_grants_no_spell(void)
{
    int i;

    run_chapter_08_handler();
    CHECK_EQ(ch08_run_state, 1);
    if (ch08_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch08_unit_spells[i], 0);
        CHECK_EQ(ch08_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win07.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 29 in status_timers[5].  WIN02.DAT and WIN05.DAT
   write that same slot with 7 and 23, WIN01.DAT, WIN03.DAT and WIN06.DAT write
   status_timers[3] with 11, 13 and 17, and WIN00.DAT and WIN04.DAT write
   status_timers[4] with 6 and 21, so opening any neighbour is a failed
   assertion rather than a silent pass.  The marker does not reach the roster
   copy, whose timers the writeback cleared before the script ran. */
static void chapter_08_victory_cutscene_is_win07_dat(void)
{
    run_chapter_08_handler();
    CHECK_EQ(ch08_run_state, 1);
    if (ch08_run_state != 1) {
        return;
    }

    CHECK_EQ(ch08_unit_timers[WIN07_MARKER_SLOT], WIN07_MARKER_VALUE);
    CHECK_EQ(ch08_unit_timers[WIN00_MARKER_SLOT], 0);
    CHECK_EQ(ch08_unit_timers[WIN01_MARKER_SLOT], 0);
    CHECK_EQ(ch08_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch08_slot_timers[WIN07_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The purse reading back
   unchanged is also what proves the sweep returned at all -- its panel loop
   waits for a key no test image queues.  The enemy the first call left at 0 HP
   is not a roster member and is not billed for either. */
static void chapter_08_revive_charges_nothing_when_nobody_fell(void)
{
    run_chapter_08_handler();
    CHECK_EQ(ch08_run_state, 1);
    if (ch08_run_state != 1) {
        return;
    }

    CHECK_EQ(ch08_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 9's, as an assignment and not as a step
   from what was there: the run started it at 9, so an increment would read
   back 10 and the store's own literal reads back 8. */
static void chapter_08_advances_the_chapter_index_to_chapter_nine(void)
{
    run_chapter_08_handler();
    CHECK_EQ(ch08_run_state, 1);
    if (ch08_run_state != 1) {
        return;
    }

    CHECK_EQ(ch08_chapter_id, CH08_CHAPTER_ID_AFTER);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_09_end at 0003a880.
 *
 * Chapters 3 to 8's body instruction for instruction, with its own script name
 * and its own stored index: four calls and one store, no branch.
 *
 *   0003a88c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003a891  CALL 0x00023980          the battle party is banked
 *   0003a896  MOV EAX,0x62110 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win08.dat" is interpreted
 *   0003a8a4  CALL 0x00039e70          the fallen are revived
 *   0003a8a9  MOV dword ptr [0x00069cf4],0x9
 *
 * THE TWO NUMBERS DIFFER BY ONE, and the cases below pin both ends of that
 * separately: the member opened is WIN08.DAT, the index of the chapter that
 * has just been won, while the store leaves 9, the index of the chapter that
 * comes next.  A handler that wrote the same number twice would pass one of
 * the two assertions and fail the other.
 *
 * WHERE THE SWEEP SITS IN THIS CHAPTER.  Chapter 9's post-action test runs
 * fdps_battle_check_default_end_conditions and then adds two defeat tests of
 * its own for the guests at unit slots 6 and 7 (chpost1.h), so the clear that
 * brings the dispatcher here is the shared one and the enemy side is normally
 * already empty when the handler runs -- the belt-and-braces position the
 * sweep has in chapters 4 to 7, not the load-bearing one it has in chapters 3
 * and 8.  What the call does is not conditional on that, and the case below
 * witnesses it on a staged enemy that still has hit points.
 *
 * The staging puts that enemy on the map ALREADY RETIRED, for the reason
 * chapter 3's section above gives and not because the game reaches the handler
 * that way: the death pass that ends fdps_battle_destroy_remaining_enemies
 * collects units whose retired bit is clear and whose hit points are 0, and
 * for a non-empty list it spins them, plays Explo.Saf and renders frames
 * (src/death.c), none of which can run in a test image.  With the bit already
 * raised the list comes back empty and the hit-point store is all the call
 * did -- which is the part this file can witness.
 *
 * The enemy also carries a character id no roster slot holds, so the writeback
 * that follows does not bank it and the roster assertions stay about the
 * party.
 *
 * WIN08.DAT retires battle unit 0 the way WIN02.DAT through WIN07.DAT do, so
 * the same witness for the order works here: the writeback skips a character-0
 * unit that has left the field, and roster slot 0 carrying the battle record
 * rather than the 0xa5 filler is what says the banking ran before the script.
 * ---------------------------------------------------------------------- */

/* The second battle unit the chapter 9 run stages: an enemy the sweep still
   has hit points to zero.  Its character id is one no roster slot holds and
   its retired bit is already raised, for the two reasons above. */
#define CH09_ENEMY_UNIT 1
#define CH09_ENEMY_CHAR_ID 9
#define CH09_ENEMY_HP_CURRENT 50
#define CH09_ENEMY_HP_MAX 50
#define CH09_UNIT_COUNT 2

/* The index the handler must leave: chapter 10, 0-based, the literal of the
   store at 0003a8a9.

   This run does NOT start from the file's shared CHAPTER_ID_BEFORE, because
   that value is 9 as well and an assertion that reads back the number the run
   put there proves nothing.  It starts from 3 instead, which is neither the
   stored 9, nor the 8 an off-by-one would leave, nor the 4 an increment would
   leave. */
#define CH09_CHAPTER_ID_BEFORE 3
#define CH09_CHAPTER_ID_AFTER 9

static int ch09_run_state = 0;

static unsigned char ch09_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch09_unit_timers[STATUS_TIMER_COUNT];
static int ch09_unit_flags;
static int ch09_unit_hp_current;
static unsigned char ch09_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch09_slot_timers[STATUS_TIMER_COUNT];
static int ch09_slot_char_id;
static int ch09_slot_flags;
static int ch09_slot_hp_current;
static int ch09_slot_hp_max;
static int ch09_slot_mp_current;
static int ch09_slot_level;
static int ch09_enemy_hp_current;
static int ch09_enemy_hp_max;
static int ch09_enemy_flags;
static int ch09_chapter_id;
static int ch09_party_gold;

/* The staged battle array with the enemy added behind the party member, and
   the player unit's side byte made explicit: stage_globals zeroes the record,
   and a zero side byte is the ENEMY side, so a party member left at the
   default would be swept by the first call. */
static void ch09_stage_globals(void)
{
    stage_globals();

    unit_image[RANDIS_UNIT].side = PLAYER_SIDE;

    unit_image[CH09_ENEMY_UNIT].char_id = CH09_ENEMY_CHAR_ID;
    unit_image[CH09_ENEMY_UNIT].side = ENEMY_SIDE;
    unit_image[CH09_ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    unit_image[CH09_ENEMY_UNIT].hp_current = CH09_ENEMY_HP_CURRENT;
    unit_image[CH09_ENEMY_UNIT].hp_max = CH09_ENEMY_HP_MAX;

    data_fdps_map_unit_count = CH09_UNIT_COUNT;
    data_fdps_chapter_current_chapter_id = CH09_CHAPTER_ID_BEFORE;
}

static void ch09_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch09_unit_spells[i] = unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch09_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch09_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch09_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch09_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch09_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch09_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch09_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch09_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch09_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch09_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch09_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch09_enemy_hp_current = (int) unit_image[CH09_ENEMY_UNIT].hp_current;
    ch09_enemy_hp_max = (int) unit_image[CH09_ENEMY_UNIT].hp_max;
    ch09_enemy_flags = (int) unit_image[CH09_ENEMY_UNIT].flags;
    ch09_chapter_id = data_fdps_chapter_current_chapter_id;
    ch09_party_gold = data_fdps_shared_party_total_gold;
}

static void run_chapter_09_handler(void)
{
    if (ch09_run_state != 0) {
        return;
    }
    ch09_run_state = 2;

    if (!ensure_fixture_archive()) {
        return;
    }

    ch09_stage_globals();

    fdps_chapter_09_end();

    ch09_capture();
    ch09_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1 and 2 have, would leave the enemy at 50. */
static void chapter_09_sweeps_the_enemy_side(void)
{
    run_chapter_09_handler();
    CHECK_EQ(ch09_run_state, 1);
    if (ch09_run_state != 1) {
        return;
    }

    CHECK_EQ(ch09_enemy_hp_current, 0);
    CHECK_EQ(ch09_enemy_hp_max, CH09_ENEMY_HP_MAX);
    CHECK_EQ(ch09_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch09_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN08.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_09_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_chapter_09_handler();
    CHECK_EQ(ch09_run_state, 1);
    if (ch09_run_state != 1) {
        return;
    }

    CHECK_EQ(ch09_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch09_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch09_slot_flags, 0);
    CHECK_EQ(ch09_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch09_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch09_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch09_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003a88c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from chapter 1's neighbour would show as byte 0 reading 0x01. */
static void chapter_09_grants_no_spell(void)
{
    int i;

    run_chapter_09_handler();
    CHECK_EQ(ch09_run_state, 1);
    if (ch09_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch09_unit_spells[i], 0);
        CHECK_EQ(ch09_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win08.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 31 in status_timers[4].  WIN00.DAT and WIN04.DAT
   write that same slot with 6 and 21, WIN01.DAT, WIN03.DAT and WIN06.DAT write
   status_timers[3] with 11, 13 and 17, and WIN02.DAT, WIN05.DAT and WIN07.DAT
   write status_timers[5] with 7, 23 and 29, so opening any neighbour is a
   failed assertion rather than a silent pass.  This is also the half of the
   handler's deliberate off-by-one that carries the index of the chapter just
   ENDED, 08, against the 9 the store leaves.  The marker does not reach the
   roster copy, whose timers the writeback cleared before the script ran. */
static void chapter_09_victory_cutscene_is_win08_dat(void)
{
    run_chapter_09_handler();
    CHECK_EQ(ch09_run_state, 1);
    if (ch09_run_state != 1) {
        return;
    }

    CHECK_EQ(ch09_unit_timers[WIN08_MARKER_SLOT], WIN08_MARKER_VALUE);
    CHECK_EQ(ch09_unit_timers[WIN01_MARKER_SLOT], 0);
    CHECK_EQ(ch09_unit_timers[WIN02_MARKER_SLOT], 0);
    CHECK_EQ(ch09_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch09_slot_timers[WIN08_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The purse reading back
   unchanged is also what proves the sweep returned at all -- its panel loop
   waits for a key no test image queues.  The enemy the first call left at 0 HP
   is not a roster member and is not billed for either. */
static void chapter_09_revive_charges_nothing_when_nobody_fell(void)
{
    run_chapter_09_handler();
    CHECK_EQ(ch09_run_state, 1);
    if (ch09_run_state != 1) {
        return;
    }

    CHECK_EQ(ch09_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 10's, 9, as an assignment and not as a
   step from what was there: this run starts it at 3, so an increment would
   read back 4 and the store's own literal reads back 9.  It also pins the
   other half of the handler's deliberate off-by-one -- an 8 here, matching the
   08 in the script name, would be chapter 9 replayed rather than chapter 10
   started. */
static void chapter_09_advances_the_chapter_index_to_chapter_ten(void)
{
    run_chapter_09_handler();
    CHECK_EQ(ch09_run_state, 1);
    if (ch09_run_state != 1) {
        return;
    }

    CHECK_EQ(ch09_chapter_id, CH09_CHAPTER_ID_AFTER);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_10_end at 0003a960.
 *
 * Chapters 3 to 9's body instruction for instruction, with its own script name
 * and its own stored index: four calls and one store, no branch.
 *
 *   0003a96c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003a971  CALL 0x00023980          the battle party is banked
 *   0003a976  MOV EAX,0x6211c / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win09.dat" is interpreted
 *   0003a984  CALL 0x00039e70          the fallen are revived
 *   0003a989  MOV dword ptr [0x00069cf4],0xa
 *
 * THE TWO NUMBERS DIFFER BY ONE, and the cases below pin both ends of that
 * separately: the member opened is WIN09.DAT, the index of the chapter that
 * has just been won, while the store leaves 10, the index of the chapter that
 * comes next.  A handler that wrote the same number twice would pass one of
 * the two assertions and fail the other.
 *
 * WHERE THE SWEEP SITS IN THIS CHAPTER.  Chapter 10 is the escape chapter, and
 * fdps_chapter_10_post_action (chpost1.h) is the only test of its family that
 * never calls fdps_battle_check_default_end_conditions: the clear is recorded
 * when all eight player slots have reached the map's bottom row or retired,
 * with the enemy side never consulted.  So the dispatcher normally arrives
 * here with the enemy side still standing and this first call is the whole of
 * what retires it -- the load-bearing position it has in chapters 3 and 8,
 * only more so.  The case below witnesses it on a staged enemy that still has
 * hit points, which is what the shipped chapter hands the handler.
 *
 * The staging puts that enemy on the map ALREADY RETIRED, for the reason
 * chapter 3's section above gives and not because the game reaches the handler
 * that way: the death pass that ends fdps_battle_destroy_remaining_enemies
 * collects units whose retired bit is clear and whose hit points are 0, and
 * for a non-empty list it spins them, plays Explo.Saf and renders frames
 * (src/death.c), none of which can run in a test image.  With the bit already
 * raised the list comes back empty and the hit-point store is all the call
 * did -- which is the part this file can witness.
 *
 * The enemy also carries a character id no roster slot holds, so the writeback
 * that follows does not bank it and the roster assertions stay about the
 * party.
 *
 * WIN09.DAT retires battle unit 0 the way WIN02.DAT through WIN08.DAT do, so
 * the same witness for the order works here: the writeback skips a character-0
 * unit that has left the field, and roster slot 0 carrying the battle record
 * rather than the 0xa5 filler is what says the banking ran before the script.
 * ---------------------------------------------------------------------- */

/* The second battle unit the chapter 10 run stages: an enemy the sweep still
   has hit points to zero.  Its character id is one no roster slot holds and
   its retired bit is already raised, for the two reasons above. */
#define CH10_ENEMY_UNIT 1
#define CH10_ENEMY_CHAR_ID 9
#define CH10_ENEMY_HP_CURRENT 50
#define CH10_ENEMY_HP_MAX 50
#define CH10_UNIT_COUNT 2

/* The index the handler must leave: chapter 11, 0-based, the literal of the
   store at 0003a989.

   The run starts from 4, which is none of the three numbers a mistake would
   leave behind: not the stored 10, not the 9 an off-by-one that followed the
   script name would leave, and not the 5 an increment would leave. */
#define CH10_CHAPTER_ID_BEFORE 4
#define CH10_CHAPTER_ID_AFTER 10

static int ch10_run_state = 0;

static unsigned char ch10_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch10_unit_timers[STATUS_TIMER_COUNT];
static int ch10_unit_flags;
static int ch10_unit_hp_current;
static unsigned char ch10_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch10_slot_timers[STATUS_TIMER_COUNT];
static int ch10_slot_char_id;
static int ch10_slot_flags;
static int ch10_slot_hp_current;
static int ch10_slot_hp_max;
static int ch10_slot_mp_current;
static int ch10_slot_level;
static int ch10_enemy_hp_current;
static int ch10_enemy_hp_max;
static int ch10_enemy_flags;
static int ch10_chapter_id;
static int ch10_party_gold;

/* The staged battle array with the enemy added behind the party member, and
   the player unit's side byte made explicit: stage_globals zeroes the record,
   and a zero side byte is the ENEMY side, so a party member left at the
   default would be swept by the first call. */
static void ch10_stage_globals(void)
{
    stage_globals();

    unit_image[RANDIS_UNIT].side = PLAYER_SIDE;

    unit_image[CH10_ENEMY_UNIT].char_id = CH10_ENEMY_CHAR_ID;
    unit_image[CH10_ENEMY_UNIT].side = ENEMY_SIDE;
    unit_image[CH10_ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    unit_image[CH10_ENEMY_UNIT].hp_current = CH10_ENEMY_HP_CURRENT;
    unit_image[CH10_ENEMY_UNIT].hp_max = CH10_ENEMY_HP_MAX;

    data_fdps_map_unit_count = CH10_UNIT_COUNT;
    data_fdps_chapter_current_chapter_id = CH10_CHAPTER_ID_BEFORE;
}

static void ch10_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch10_unit_spells[i] = unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch10_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch10_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch10_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch10_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch10_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch10_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch10_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch10_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch10_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch10_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch10_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch10_enemy_hp_current = (int) unit_image[CH10_ENEMY_UNIT].hp_current;
    ch10_enemy_hp_max = (int) unit_image[CH10_ENEMY_UNIT].hp_max;
    ch10_enemy_flags = (int) unit_image[CH10_ENEMY_UNIT].flags;
    ch10_chapter_id = data_fdps_chapter_current_chapter_id;
    ch10_party_gold = data_fdps_shared_party_total_gold;
}

static void run_chapter_10_handler(void)
{
    if (ch10_run_state != 0) {
        return;
    }
    ch10_run_state = 2;

    if (!ensure_fixture_archive()) {
        return;
    }

    ch10_stage_globals();

    fdps_chapter_10_end();

    ch10_capture();
    ch10_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1 and 2 have, would leave the enemy at 50, and on this chapter that
   omission is the one that matters most: nothing else in the game retires the
   enemies an escape chapter is won without fighting. */
static void chapter_10_sweeps_the_enemy_side(void)
{
    run_chapter_10_handler();
    CHECK_EQ(ch10_run_state, 1);
    if (ch10_run_state != 1) {
        return;
    }

    CHECK_EQ(ch10_enemy_hp_current, 0);
    CHECK_EQ(ch10_enemy_hp_max, CH10_ENEMY_HP_MAX);
    CHECK_EQ(ch10_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch10_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN09.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_10_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_chapter_10_handler();
    CHECK_EQ(ch10_run_state, 1);
    if (ch10_run_state != 1) {
        return;
    }

    CHECK_EQ(ch10_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch10_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch10_slot_flags, 0);
    CHECK_EQ(ch10_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch10_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch10_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch10_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003a96c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from chapter 1's neighbour would show as byte 0 reading 0x01. */
static void chapter_10_grants_no_spell(void)
{
    int i;

    run_chapter_10_handler();
    CHECK_EQ(ch10_run_state, 1);
    if (ch10_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch10_unit_spells[i], 0);
        CHECK_EQ(ch10_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win09.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 19 in status_timers[3].  WIN01.DAT, WIN03.DAT and
   WIN06.DAT write that same slot with 11, 13 and 17, WIN00.DAT, WIN04.DAT and
   WIN08.DAT write status_timers[4] with 6, 21 and 31, and WIN02.DAT,
   WIN05.DAT and WIN07.DAT write status_timers[5] with 7, 23 and 29, so opening
   any neighbour is a failed assertion rather than a silent pass.  This is also
   the half of the handler's deliberate off-by-one that carries the index of
   the chapter just ENDED, 09, against the 10 the store leaves.  The marker
   does not reach the roster copy, whose timers the writeback cleared before
   the script ran. */
static void chapter_10_victory_cutscene_is_win09_dat(void)
{
    run_chapter_10_handler();
    CHECK_EQ(ch10_run_state, 1);
    if (ch10_run_state != 1) {
        return;
    }

    CHECK_EQ(ch10_unit_timers[WIN09_MARKER_SLOT], WIN09_MARKER_VALUE);
    CHECK_EQ(ch10_unit_timers[WIN00_MARKER_SLOT], 0);
    CHECK_EQ(ch10_unit_timers[WIN02_MARKER_SLOT], 0);
    CHECK_EQ(ch10_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch10_slot_timers[WIN09_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The purse reading back
   unchanged is also what proves the sweep returned at all -- its panel loop
   waits for a key no test image queues.  The enemy the first call left at 0 HP
   is not a roster member and is not billed for either. */
static void chapter_10_revive_charges_nothing_when_nobody_fell(void)
{
    run_chapter_10_handler();
    CHECK_EQ(ch10_run_state, 1);
    if (ch10_run_state != 1) {
        return;
    }

    CHECK_EQ(ch10_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 11's, 10, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 10.  It also pins the
   other half of the handler's deliberate off-by-one -- a 9 here, matching the
   09 in the script name, would be chapter 10 replayed rather than chapter 11
   started. */
static void chapter_10_advances_the_chapter_index_to_chapter_eleven(void)
{
    run_chapter_10_handler();
    CHECK_EQ(ch10_run_state, 1);
    if (ch10_run_state != 1) {
        return;
    }

    CHECK_EQ(ch10_chapter_id, CH10_CHAPTER_ID_AFTER);
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
    RUN_TEST(chapter_04_sweeps_the_enemy_side);
    RUN_TEST(chapter_04_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_04_grants_no_spell);
    RUN_TEST(chapter_04_victory_cutscene_is_win03_dat);
    RUN_TEST(chapter_04_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_04_advances_the_chapter_index_to_chapter_five);
    RUN_TEST(chapter_05_sweeps_the_enemy_side);
    RUN_TEST(chapter_05_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_05_grants_no_spell);
    RUN_TEST(chapter_05_victory_cutscene_is_win04_dat);
    RUN_TEST(chapter_05_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_05_advances_the_chapter_index_to_chapter_six);
    RUN_TEST(chapter_06_sweeps_the_enemy_side);
    RUN_TEST(chapter_06_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_06_grants_no_spell);
    RUN_TEST(chapter_06_victory_cutscene_is_win05_dat);
    RUN_TEST(chapter_06_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_06_advances_the_chapter_index_to_chapter_seven);
    RUN_TEST(chapter_07_sweeps_the_enemy_side);
    RUN_TEST(chapter_07_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_07_grants_no_spell);
    RUN_TEST(chapter_07_victory_cutscene_is_win06_dat);
    RUN_TEST(chapter_07_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_07_advances_the_chapter_index_to_chapter_eight);
    RUN_TEST(chapter_08_sweeps_the_enemy_side);
    RUN_TEST(chapter_08_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_08_grants_no_spell);
    RUN_TEST(chapter_08_victory_cutscene_is_win07_dat);
    RUN_TEST(chapter_08_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_08_advances_the_chapter_index_to_chapter_nine);
    RUN_TEST(chapter_09_sweeps_the_enemy_side);
    RUN_TEST(chapter_09_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_09_grants_no_spell);
    RUN_TEST(chapter_09_victory_cutscene_is_win08_dat);
    RUN_TEST(chapter_09_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_09_advances_the_chapter_index_to_chapter_ten);
    RUN_TEST(chapter_10_sweeps_the_enemy_side);
    RUN_TEST(chapter_10_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_10_grants_no_spell);
    RUN_TEST(chapter_10_victory_cutscene_is_win09_dat);
    RUN_TEST(chapter_10_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_10_advances_the_chapter_index_to_chapter_eleven);
    RUN_TEST(the_fixture_container_is_removed);
}
