/* tests/chend2.c -- cover for src/chend2.c.
 *
 * WHAT THE HANDLER IS.  fdps_chapter_16_end at 0003acd0 is four calls and one
 * store with no branch anywhere in it, so nothing about it is worth testing in
 * pieces: what it decides is the ORDER of the four calls and the value of the
 * one store.  The file runs the whole handler once, for real, against staged
 * arrays and a fixture cut-scene, and the cases below assert against that
 * single run.
 *
 * Expected values come from the assembly at 0003acd0 and from the bodies the
 * four callees were emitted from, never from the emitted C of the handler:
 *
 *   0003acdc  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003ace1  CALL 0x00023980          the battle party is banked
 *   0003ace6  MOV EAX,0x62164 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win15.dat" is interpreted
 *   0003acf4  CALL 0x00039e70          the fallen are revived
 *   0003acf9  MOV dword ptr [0x00069cf4],0x10
 *
 * THE TWO NUMBERS DIFFER BY ONE, and the cases below pin both ends of that
 * separately: the member opened is WIN15.DAT, the index of the chapter that
 * has just been won, while the store leaves 16, the index of the chapter that
 * comes next.  A handler that wrote the same number twice would pass one of
 * the two assertions and fail the other.  The fixture container therefore also
 * holds WIN14.DAT and WIN16.DAT -- the members a slip in either direction
 * would open -- each writing a marker of its own into a different status-timer
 * slot, so a wrong name is a failed assertion rather than a missing member and
 * a silent pass.
 *
 * HOW THE ORDER IS PINNED DOWN RATHER THAN ASSUMED.  Each of the first three
 * steps leaves a mark the step after it would erase or miss:
 *
 *   The sweep zeroes the hit-point word of the staged enemy, which nothing
 *   later in the handler writes, so the enemy's 0 is the sweep's own work.
 *
 *   WIN15.DAT's first opcode RETIRES battle unit 0.  The writeback skips a
 *   unit whose character id is 0 and which has left the field -- both halves,
 *   and unit 0 here is character 0 -- so a run that interpreted the script
 *   before banking the party would leave roster slot 0 at its 0xa5 filler
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
 * WHY THE STAGED ENEMY IS ALREADY RETIRED.  The death pass that ends
 * fdps_battle_destroy_remaining_enemies collects units whose retired bit is
 * clear and whose hit points are 0, and for a non-empty list it spins them,
 * plays Explo.Saf and renders frames (src/death.c), none of which can run in a
 * test image.  With the bit already raised the list comes back empty and the
 * hit-point store is all the call did -- which is the part this file can
 * witness.  The enemy also carries a character id no roster slot holds, so the
 * writeback does not bank it and the roster assertions stay about the party.
 *
 * WHY THE CUT-SCENE IS A FIXTURE AND NOT THE SHIPPED ONE.  The shipped
 * WIN15.DAT is chapter 16's victory cinematic: it plays CD audio and .saf
 * clips and draws chapter text through pointers a fresh test image has not
 * filled.  Running it asserts nothing about THIS function and faults on the
 * way, the same reason tests/icon.c, tests/chend1.c and tests/chend1b.c give
 * for staging their own container.  The one staged here is built to the layout
 * in resource_info/vfs.md and read by the game's own fdps_vfs_open and
 * fdps_vfs_load_file.
 *
 * The staging REFUSES TO OVERWRITE an IconAni.vfs that is already in the run
 * directory and removes its own again in the last case, which is the protocol
 * tests/icon.c, tests/chend1.c and tests/chend1b.c share for that name.
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
#include "chend2.h"

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

/* How many records each staged array holds.  Two of the battle array are all
   the run needs; the spares behind them are there so a write past the end of
   the array under test lands somewhere this file can see. */
#define UNIT_CAPACITY 4
#define ROSTER_CAPACITY 4
#define ITEM_TABLE_ROWS 8

/* The roster block is filled with this rather than zeroed, because half of
   what the cases pin down is which bytes the writeback replaced: a zeroed
   block cannot tell an untouched byte from one written with 0. */
#define ROSTER_FILLER 0xa5

/* Battle unit 0 and character 0 are the same person here -- 蘭迪斯 -- and the
   pairing is load-bearing: the writeback's exemption fires only for character
   id 0, so the retire opcode in WIN15.DAT is what a wrong order would trip
   over. */
#define RANDIS_UNIT 0
#define RANDIS_CHAR_ID 0
#define RANDIS_ROSTER_SLOT 0

/* The five bitmap bytes at record +0x1a.  This handler grants no spell, so
   every one of them must still read 0 after the run (src/unit.c). */
#define SPELL_BITMAP_BYTES 5

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

/* The six status bytes at record +0x22.  The SET_UNIT_TIMER opcode's slot
   operand is measured from status_timers[3] (src/icon.c), so operand 0 is
   status_timers[3], operand 1 is status_timers[4] and operand 2 is
   status_timers[5]. */
#define STATUS_TIMER_COUNT 6

/* WIN15.DAT, the member chapter 16's handler names: operand 1, so
   status_timers[4], with a value neither decoy writes. */
#define WIN15_MARKER_OPERAND 1
#define WIN15_MARKER_SLOT 4
#define WIN15_MARKER_VALUE 83

/* WIN14.DAT, the member a handler that followed its own script number one step
   low would open, in a slot and with a value of its own.  It is also the
   member the sibling handler one chapter back really does name, so a body
   copied from fdps_chapter_15_end without changing the operand lands here. */
#define WIN14_MARKER_OPERAND 0
#define WIN14_MARKER_SLOT 3
#define WIN14_MARKER_VALUE 89

/* WIN16.DAT, the member a handler that named the chapter it hands ON to rather
   than the one just won would open -- the exact slip the store's 0x10 invites
   -- in a third slot with a third value. */
#define WIN16_MARKER_OPERAND 2
#define WIN16_MARKER_SLOT 5
#define WIN16_MARKER_VALUE 97

/* The retired bit in the flags byte at record +5, which WIN15.DAT's first
   opcode raises on unit 0 and which the staging raises on the enemy. */
#define UNIT_FLAG_RETIRED 1

/* The side byte at record +6.  0 is the side the sweep clears, so the party
   member's 2 has to be stored explicitly over the zeroed record. */
#define ENEMY_SIDE 0
#define PLAYER_SIDE 2

/* The second battle unit: an enemy the sweep still has hit points to zero. */
#define ENEMY_UNIT 1
#define ENEMY_CHAR_ID 9
#define ENEMY_HP_CURRENT 50
#define ENEMY_HP_MAX 50
#define UNIT_COUNT 2

/* The index the handler must leave: chapter 17, 0-based, the literal of the
   store at 0003acf9.

   The run starts from 4, which is none of the three numbers a mistake would
   leave behind: not the stored 16, not the 15 an off-by-one that followed the
   script name would leave, and not the 5 an increment would leave. */
#define CHAPTER_ID_BEFORE 4
#define CHAPTER_ID_AFTER 16

/* The purse the run starts with.  Nothing in the handler may spend from it:
   the revive charges its fee inside the sweep, and the sweep finds no fallen
   member. */
#define PARTY_GOLD_BEFORE 1234

static struct fdps_unit_record unit_image[UNIT_CAPACITY];
static struct fdps_unit_record roster_image[ROSTER_CAPACITY];
static unsigned char item_image[ITEM_TABLE_ROWS
                                * sizeof(struct fdps_item_effect)];

/* WIN15.DAT: retire battle unit 0, write the marker into its status_timers[4],
   stop.  Opcode numbers and operand counts are src/icon.c's ladder -- 0x0b
   takes a unit index, 0x12 takes a unit index, a timer slot measured from
   status_timers[3] and a value, and 0x00 falls into the arm that ends the
   script. */
static unsigned char fixture_win15_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN15_MARKER_OPERAND, WIN15_MARKER_VALUE,
    0x00
};

/* WIN14.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_win14_dat[] = {
    0x12, RANDIS_UNIT, WIN14_MARKER_OPERAND, WIN14_MARKER_VALUE,
    0x00
};

/* WIN16.DAT: the decoy one step high, retiring nobody for the same reason. */
static unsigned char fixture_win16_dat[] = {
    0x12, RANDIS_UNIT, WIN16_MARKER_OPERAND, WIN16_MARKER_VALUE,
    0x00
};

static char *fixture_names[FIXTURE_MEMBERS] = {
    "WIN14.DAT", "WIN15.DAT", "WIN16.DAT"
};

static unsigned char *fixture_bytes[FIXTURE_MEMBERS] = {
    fixture_win14_dat, fixture_win15_dat, fixture_win16_dat
};

static int fixture_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_win14_dat), sizeof(fixture_win15_dat),
    sizeof(fixture_win16_dat)
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
static int seen_unit_hp_current;
static unsigned char seen_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char seen_slot_timers[STATUS_TIMER_COUNT];
static int seen_slot_char_id;
static int seen_slot_flags;
static int seen_slot_hp_current;
static int seen_slot_hp_max;
static int seen_slot_mp_current;
static int seen_slot_level;
static int seen_enemy_hp_current;
static int seen_enemy_hp_max;
static int seen_enemy_flags;
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

/* The battle array, the roster block and the item table as they stand when
   chapter 16's battle has just been won: one live party member and one already
   retired enemy on the map, one roster slot carrying the party member's
   character id and filler everywhere else. */
static void stage_globals(void)
{
    memset(unit_image, 0, sizeof(unit_image));
    memset(roster_image, ROSTER_FILLER, sizeof(roster_image));
    memset(item_image, 0, sizeof(item_image));

    unit_image[RANDIS_UNIT].char_id = RANDIS_CHAR_ID;
    unit_image[RANDIS_UNIT].flags = 0;
    unit_image[RANDIS_UNIT].side = PLAYER_SIDE;
    unit_image[RANDIS_UNIT].level = RANDIS_LEVEL;
    unit_image[RANDIS_UNIT].clazz = RANDIS_CLASS;
    unit_image[RANDIS_UNIT].hp_current = RANDIS_HP_CURRENT;
    unit_image[RANDIS_UNIT].hp_max = RANDIS_HP_MAX;
    unit_image[RANDIS_UNIT].mp_current = RANDIS_MP_CURRENT;
    unit_image[RANDIS_UNIT].mp_max = RANDIS_MP_MAX;

    unit_image[ENEMY_UNIT].char_id = ENEMY_CHAR_ID;
    unit_image[ENEMY_UNIT].side = ENEMY_SIDE;
    unit_image[ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    unit_image[ENEMY_UNIT].hp_current = ENEMY_HP_CURRENT;
    unit_image[ENEMY_UNIT].hp_max = ENEMY_HP_MAX;

    roster_image[RANDIS_ROSTER_SLOT].char_id = RANDIS_CHAR_ID;

    data_fdps_map_unit_array_ptr = (unsigned char *) unit_image;
    data_fdps_roster_array_ptr = (unsigned char *) roster_image;
    data_fdps_item_effect_table_ptr = item_image;
    data_fdps_map_unit_count = UNIT_COUNT;
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
    seen_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    seen_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    seen_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    seen_slot_hp_current = (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    seen_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    seen_slot_mp_current = (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    seen_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    seen_enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    seen_enemy_hp_max = (int) unit_image[ENEMY_UNIT].hp_max;
    seen_enemy_flags = (int) unit_image[ENEMY_UNIT].flags;
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

    fdps_chapter_16_end();

    capture();
    run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1, 2 and 15 have, would leave the enemy at 50. */
static void chapter_16_sweeps_the_enemy_side(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_enemy_hp_current, 0);
    CHECK_EQ(seen_enemy_hp_max, ENEMY_HP_MAX);
    CHECK_EQ(seen_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN15.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_16_banks_the_party_before_the_cutscene_runs(void)
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

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003acdc is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from fdps_chapter_01_end would show as byte 0 reading 0x01. */
static void chapter_16_grants_no_spell(void)
{
    int i;

    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(seen_unit_spells[i], 0);
        CHECK_EQ(seen_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win15.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 83 in status_timers[4].  The two decoys in the
   container write 89 into status_timers[3] and 97 into status_timers[5] and
   retire nobody, so a name one step in either direction is three failed
   assertions rather than a silent pass.  This is the half of the handler's
   deliberate off-by-one that carries the index of the chapter just ENDED, 15,
   against the 16 the store leaves.  The marker does not reach the roster copy,
   whose timers the writeback cleared before the script ran. */
static void chapter_16_victory_cutscene_is_win15_dat(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit_timers[WIN15_MARKER_SLOT], WIN15_MARKER_VALUE);
    CHECK_EQ(seen_unit_timers[WIN14_MARKER_SLOT], 0);
    CHECK_EQ(seen_unit_timers[WIN16_MARKER_SLOT], 0);
    CHECK_EQ(seen_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(seen_slot_timers[WIN15_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The enemy the first
   call left at 0 HP is not a roster member and is not billed for either. */
static void chapter_16_revive_charges_nothing_when_nobody_fell(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 17's, 16, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 16.  It also pins the
   other half of the handler's deliberate off-by-one -- a 15 here, matching the
   15 in the script name, would be chapter 16 replayed rather than chapter 17
   started. */
static void chapter_16_advances_the_chapter_index_to_chapter_seventeen(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_chapter_id, CHAPTER_ID_AFTER);
}

/* The fixture container goes again, so that nothing this file wrote outlives
   its run and the next file that wants that name finds it free. */
static void the_fixture_container_is_removed(void)
{
    if (!fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

/* --------------------------------------------------------------------------
 * fdps_chapter_17_end at 0003ad40.
 *
 * The same handler shape as chapter 16's above -- four calls and one store,
 * no branch -- so the cases are the same six, asserting the same things about
 * a run of their own.  What differs is the two operands: the member opened is
 * WIN16.DAT, the index of the chapter just won, and the store leaves 17, the
 * index of the chapter that comes next.
 *
 *   0003ad4c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003ad51  CALL 0x00023980          the battle party is banked
 *   0003ad56  MOV EAX,0x62170 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win16.dat" is interpreted
 *   0003ad64  CALL 0x00039e70          the fallen are revived
 *   0003ad69  MOV dword ptr [0x00069cf4],0x11
 *
 * WHY THIS HALF STAGES ITS OWN CONTAINER.  Only one IconAni.vfs can stand in
 * the run directory at a time and the case that removes chapter 16's runs
 * between the two halves, so this half builds one of its own with its own
 * three members.  The same refusal protocol applies: a container already
 * standing is left alone, and then every case here fails on its run-state
 * assertion rather than passing on whatever that other container held.
 *
 * Everything else -- why the enemy is staged already retired, why nobody may
 * be left at 0 HP, why the cut-scene is a fixture rather than the shipped
 * WIN16.DAT -- is the reasoning at the top of this file, unchanged.
 * ------------------------------------------------------------------------ */

/* WIN16.DAT, the member chapter 17's handler names: operand 2, so
   status_timers[5], with a value neither decoy writes. */
#define WIN16_CH17_MARKER_OPERAND 2
#define WIN16_CH17_MARKER_SLOT 5
#define WIN16_CH17_MARKER_VALUE 67

/* WIN15.DAT, the member a handler that followed its own script number one step
   low would open, in a slot and with a value of its own.  It is also the
   member the sibling handler one chapter back really does name, so a body
   copied from fdps_chapter_16_end without changing the operand lands here. */
#define WIN15_CH17_MARKER_OPERAND 1
#define WIN15_CH17_MARKER_SLOT 4
#define WIN15_CH17_MARKER_VALUE 71

/* WIN17.DAT, the member a handler that named the chapter it hands ON to rather
   than the one just won would open -- the exact slip the store's 0x11 invites
   -- in a third slot with a third value. */
#define WIN17_CH17_MARKER_OPERAND 0
#define WIN17_CH17_MARKER_SLOT 3
#define WIN17_CH17_MARKER_VALUE 73

/* The index chapter 17's handler must leave: chapter 18, 0-based, the literal
   of the store at 0003ad69.

   The run starts from the same 4 chapter 16's does, which is none of the three
   numbers a mistake would leave behind: not the stored 17, not the 16 an
   off-by-one that followed the script name would leave, and not the 5 an
   increment would leave. */
#define CH17_CHAPTER_ID_AFTER 17

/* WIN16.DAT: retire battle unit 0, write the marker into its status_timers[5],
   stop -- the same two opcodes chapter 16's real member uses, so the order
   argument below is the same one. */
static unsigned char fixture_ch17_win16_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN16_CH17_MARKER_OPERAND, WIN16_CH17_MARKER_VALUE,
    0x00
};

/* WIN15.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_ch17_win15_dat[] = {
    0x12, RANDIS_UNIT, WIN15_CH17_MARKER_OPERAND, WIN15_CH17_MARKER_VALUE,
    0x00
};

/* WIN17.DAT: the decoy one step high, retiring nobody for the same reason. */
static unsigned char fixture_ch17_win17_dat[] = {
    0x12, RANDIS_UNIT, WIN17_CH17_MARKER_OPERAND, WIN17_CH17_MARKER_VALUE,
    0x00
};

static char *fixture_ch17_names[FIXTURE_MEMBERS] = {
    "WIN15.DAT", "WIN16.DAT", "WIN17.DAT"
};

static unsigned char *fixture_ch17_bytes[FIXTURE_MEMBERS] = {
    fixture_ch17_win15_dat, fixture_ch17_win16_dat, fixture_ch17_win17_dat
};

static int fixture_ch17_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_ch17_win15_dat), sizeof(fixture_ch17_win16_dat),
    sizeof(fixture_ch17_win17_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case says so. */
static int ch17_run_state = 0;

/* Whether this half created the container, and so whether it may remove it. */
static int ch17_fixture_owned = 0;

/* Everything chapter 17's cases assert, captured the instant it returned. */
static unsigned char ch17_seen_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch17_seen_unit_timers[STATUS_TIMER_COUNT];
static unsigned char ch17_seen_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch17_seen_slot_timers[STATUS_TIMER_COUNT];
static int ch17_seen_unit_flags;
static int ch17_seen_unit_hp_current;
static int ch17_seen_slot_char_id;
static int ch17_seen_slot_flags;
static int ch17_seen_slot_hp_current;
static int ch17_seen_slot_hp_max;
static int ch17_seen_slot_mp_current;
static int ch17_seen_slot_level;
static int ch17_seen_enemy_hp_current;
static int ch17_seen_enemy_hp_max;
static int ch17_seen_enemy_flags;
static int ch17_seen_chapter_id;
static int ch17_seen_party_gold;

/* Builds chapter 17's fixture container, or answers no, to the layout in
   resource_info/vfs.md and by the same refusal protocol as the half above. */
static int stage_ch17_fixture_archive(void)
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
        write_name(fp, fixture_ch17_names[i]);
        write_dword(fp, (long) fixture_ch17_lengths[i]);
        write_dword(fp, (long) fixture_ch17_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_ch17_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture_ch17_bytes[i], 1, (size_t) fixture_ch17_lengths[i], fp);
    }
    fclose(fp);

    ch17_fixture_owned = 1;
    return 1;
}

static void ch17_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch17_seen_unit_spells[i] =
            unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch17_seen_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch17_seen_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch17_seen_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch17_seen_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch17_seen_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch17_seen_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch17_seen_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch17_seen_slot_hp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch17_seen_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch17_seen_slot_mp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch17_seen_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch17_seen_enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    ch17_seen_enemy_hp_max = (int) unit_image[ENEMY_UNIT].hp_max;
    ch17_seen_enemy_flags = (int) unit_image[ENEMY_UNIT].flags;
    ch17_seen_chapter_id = data_fdps_chapter_current_chapter_id;
    ch17_seen_party_gold = data_fdps_shared_party_total_gold;
}

/* Runs chapter 17's handler once, against freshly staged arrays and its own
   fixture cut-scene, and records what it left behind. */
static void run_ch17_handler(void)
{
    if (ch17_run_state != 0) {
        return;
    }
    ch17_run_state = 2;

    if (!stage_ch17_fixture_archive()) {
        return;
    }

    stage_globals();

    fdps_chapter_17_end();

    ch17_capture();
    ch17_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1, 2 and 15 have, would leave the enemy at 50. */
static void chapter_17_sweeps_the_enemy_side(void)
{
    run_ch17_handler();
    CHECK_EQ(ch17_run_state, 1);
    if (ch17_run_state != 1) {
        return;
    }

    CHECK_EQ(ch17_seen_enemy_hp_current, 0);
    CHECK_EQ(ch17_seen_enemy_hp_max, ENEMY_HP_MAX);
    CHECK_EQ(ch17_seen_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch17_seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN16.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_17_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_ch17_handler();
    CHECK_EQ(ch17_run_state, 1);
    if (ch17_run_state != 1) {
        return;
    }

    CHECK_EQ(ch17_seen_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch17_seen_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch17_seen_slot_flags, 0);
    CHECK_EQ(ch17_seen_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch17_seen_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch17_seen_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch17_seen_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003ad4c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from fdps_chapter_01_end would show as byte 0 reading 0x01. */
static void chapter_17_grants_no_spell(void)
{
    int i;

    run_ch17_handler();
    CHECK_EQ(ch17_run_state, 1);
    if (ch17_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch17_seen_unit_spells[i], 0);
        CHECK_EQ(ch17_seen_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win16.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 67 in status_timers[5].  The two decoys in the
   container write 71 into status_timers[4] and 73 into status_timers[3] and
   retire nobody, so a name one step in either direction is three failed
   assertions rather than a silent pass.  This is the half of the handler's
   deliberate off-by-one that carries the index of the chapter just ENDED, 16,
   against the 17 the store leaves.  The marker does not reach the roster copy,
   whose timers the writeback cleared before the script ran. */
static void chapter_17_victory_cutscene_is_win16_dat(void)
{
    run_ch17_handler();
    CHECK_EQ(ch17_run_state, 1);
    if (ch17_run_state != 1) {
        return;
    }

    CHECK_EQ(ch17_seen_unit_timers[WIN16_CH17_MARKER_SLOT],
             WIN16_CH17_MARKER_VALUE);
    CHECK_EQ(ch17_seen_unit_timers[WIN15_CH17_MARKER_SLOT], 0);
    CHECK_EQ(ch17_seen_unit_timers[WIN17_CH17_MARKER_SLOT], 0);
    CHECK_EQ(ch17_seen_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch17_seen_slot_timers[WIN16_CH17_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The enemy the first
   call left at 0 HP is not a roster member and is not billed for either. */
static void chapter_17_revive_charges_nothing_when_nobody_fell(void)
{
    run_ch17_handler();
    CHECK_EQ(ch17_run_state, 1);
    if (ch17_run_state != 1) {
        return;
    }

    CHECK_EQ(ch17_seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 18's, 17, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 17.  It also pins the
   other half of the handler's deliberate off-by-one -- a 16 here, matching the
   16 in the script name, would be chapter 17 replayed rather than chapter 18
   started. */
static void chapter_17_advances_the_chapter_index_to_chapter_eighteen(void)
{
    run_ch17_handler();
    CHECK_EQ(ch17_run_state, 1);
    if (ch17_run_state != 1) {
        return;
    }

    CHECK_EQ(ch17_seen_chapter_id, CH17_CHAPTER_ID_AFTER);
}

/* Chapter 17's fixture container goes again, so that nothing this file wrote
   outlives its run and the next file that wants that name finds it free. */
static void the_ch17_fixture_container_is_removed(void)
{
    if (!ch17_fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    ch17_fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

void run_chend2_tests(void)
{
    RUN_TEST(chapter_16_sweeps_the_enemy_side);
    RUN_TEST(chapter_16_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_16_grants_no_spell);
    RUN_TEST(chapter_16_victory_cutscene_is_win15_dat);
    RUN_TEST(chapter_16_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_16_advances_the_chapter_index_to_chapter_seventeen);
    RUN_TEST(the_fixture_container_is_removed);
    RUN_TEST(chapter_17_sweeps_the_enemy_side);
    RUN_TEST(chapter_17_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_17_grants_no_spell);
    RUN_TEST(chapter_17_victory_cutscene_is_win16_dat);
    RUN_TEST(chapter_17_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_17_advances_the_chapter_index_to_chapter_eighteen);
    RUN_TEST(the_ch17_fixture_container_is_removed);
}
