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

/* --------------------------------------------------------------------------
 * fdps_chapter_18_end at 0003ada0.
 *
 * NOT THE FAMILY'S PLAIN SHAPE.  The four calls and the store are still there,
 * in the family's order, but a gate stands in front of them and decides two
 * things at once: whether 蘭迪斯's 神的聖印 becomes the 勇者徽章, and which of
 * two victory scenes plays.
 *
 *   0003adb4  PUSH 0xa9 / PUSH 0x0 / CALL 0x00034520 / MOV [EBP-0x14],EAX
 *             the seal is looked for in unit 0's bag
 *   0003adc6  CMP dword ptr [EBP-0x14],-0x1 / JZ 0003ae2d
 *             not carried: the whole gate is skipped
 *   0003add3  CMP dword ptr [EBP-0x10],0x9 / JL
 *             the promotion sweep, a LITERAL NINE
 *   0003adf5  MOV DL,[EAX+0x7] / CMP DL,[EAX+0x8] / JZ
 *             promoted means form id at +0x07 differs from character id at +8
 *   0003ae0c  CALL 0x00025cc0 then PUSH 0xdb / CALL 0x00025d20
 *             the trade, and it runs BEFORE the writeback
 *   0003ae2d  CALL 0x00039e10 / CALL 0x00023980
 *   0003ae3d  MOV EAX,0x6217c ... "Win17-1.dat" when the trade was made
 *   0003ae4d  MOV EAX,0x62188 ... "Win17.dat" when it was not
 *   0003ae5b  CALL 0x00039e70
 *   0003ae60  MOV dword ptr [0x00069cf4],0x12
 *
 * THREE RUNS, because one run cannot witness a branch.  Each stages the
 * battle array and the roster afresh and runs the whole handler for real:
 *
 *   A  蘭迪斯 carries the seal, none of the nine has changed class, and the
 *      TENTH unit has -- so the trade must still be made.  That tenth unit is
 *      what pins the bound at a literal nine rather than at the live unit
 *      count: the run stages eleven units and sets data_fdps_map_unit_count to
 *      eleven, so a sweep written over the array would see the promotion and
 *      deny a badge the original grants.
 *   B  the same, except that it is unit EIGHT that has changed class -- the
 *      last index the sweep does look at -- so the trade must be denied.  A
 *      and B together pin both ends of `unit_index < 9`.
 *   C  蘭迪斯 is not carrying the seal at all, so the gate is skipped before
 *      the sweep ever runs and nothing is traded.
 *
 * The two runs that deny the trade both have to open the ORDINARY scene, and
 * the run that makes it has to open the alternative one, so the fixture
 * container holds four members: the two the handler names and two decoys.
 * WIN16.DAT is where a body copied from fdps_chapter_17_end without changing
 * the operand would land, and WIN18.DAT is where a handler that named the
 * chapter it hands ON to -- the 0x12 of the store -- would land.  Each writes
 * a marker of its own, so a wrong name is a failed assertion rather than a
 * missing member and a silent pass.
 *
 * Everything else -- why the enemy is staged already retired, why nobody may
 * be left at 0 HP, why the cut-scene is a fixture rather than the shipped
 * WIN17.DAT, and the refusal protocol around the IconAni.vfs name -- is the
 * reasoning at the top of this file, unchanged.  The one difference is that
 * all three runs share ONE container: it is built by whichever runs first and
 * removed by the last case.
 * ------------------------------------------------------------------------ */

/* The four members, and the marker each writes.  The two the handler really
   names take slots of their own; the two decoys share status_timers[5] with
   values of their own, so one assertion that the slot is still 0 rules both
   of them out and the value says which one ran if it is not. */
#define CH18_ALT_MARKER_OPERAND 1
#define CH18_ALT_MARKER_SLOT 4
#define CH18_ALT_MARKER_VALUE 101
#define CH18_PLAIN_MARKER_OPERAND 0
#define CH18_PLAIN_MARKER_SLOT 3
#define CH18_PLAIN_MARKER_VALUE 103
#define CH18_DECOY_MARKER_OPERAND 2
#define CH18_DECOY_MARKER_SLOT 5
#define CH18_WIN16_DECOY_VALUE 109
#define CH18_WIN18_DECOY_VALUE 107

#define CH18_FIXTURE_MEMBERS 4

/* The index chapter 18's handler must leave: chapter 19, 0-based, the literal
   of the store at 0003ae60.

   The run starts from the same 4 the two halves above start from, which is
   none of the three numbers a mistake would leave behind: not the stored 18,
   not the 17 an off-by-one that followed the script name would leave, and not
   the 5 an increment would leave. */
#define CH18_CHAPTER_ID_AFTER 18

/* The two item ids the trade is between (assets/items.md), and a third the
   bag carries throughout so that the removal has something to close the gap
   over.  0x34 is 藥草, which is not either of the two and is not consulted by
   anything the handler calls. */
#define CH18_SEAL_ID 0xa9
#define CH18_BADGE_ID 0xdb
#define CH18_FILLER_ITEM_ID 0x34

/* One unit's bag: eight 2-byte entries at record +0x0a, a flag byte and an id
   byte each, with 0x80 in the flag byte meaning the entry is empty
   (src/unititem.c). */
#define CH18_BAG_BYTES 16
#define CH18_BAG_ENTRIES 8
#define CH18_BAG_EMPTY 0x80
#define CH18_BAG_CARRIED 0x00
#define CH18_BAG_UNUSED_ID 0xff

/* How many units each run stages.  Nine party members the sweep looks at, a
   tenth it must not, and an enemy behind them -- and the count global is set
   to all eleven, which is the point of the tenth. */
#define CH18_LATE_JOINER_UNIT 9
#define CH18_ENEMY_UNIT 10
#define CH18_UNIT_COUNT 11
#define CH18_UNIT_CAPACITY 12

/* The late joiner: 瑪麗安's slot, carrying a character id no roster slot holds
   and a form id that differs from it, which is what a promoted record looks
   like. */
#define CH18_LATE_JOINER_CHAR_ID 9
#define CH18_PROMOTED_PORTRAIT_ID 20

/* The unit the denial run promotes: index eight, the last one the sweep
   reaches. */
#define CH18_LAST_SWEPT_UNIT 8

/* The enemy the sweep still has hit points to zero, staged already retired for
   the reason at the top of this file. */
#define CH18_ENEMY_CHAR_ID 30
#define CH18_ENEMY_HP 44

/* Every party member is staged below its maximum so the writeback's full heal
   is visible, and well above 0 so the revive panel never opens. */
#define CH18_PARTY_HP_CURRENT 18
#define CH18_PARTY_HP_MAX 31

static struct fdps_unit_record ch18_unit_image[CH18_UNIT_CAPACITY];
static struct fdps_unit_record ch18_roster_image[ROSTER_CAPACITY];
static unsigned char ch18_item_image[ITEM_TABLE_ROWS
                                     * sizeof(struct fdps_item_effect)];

/* WIN17-1.DAT: retire battle unit 0, write the alternative marker into its
   status_timers[4], stop.  The retire opcode is what makes a run that
   interpreted the script BEFORE banking the party visible, exactly as it does
   for the two halves above. */
static unsigned char fixture_win17_1_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, CH18_ALT_MARKER_OPERAND, CH18_ALT_MARKER_VALUE,
    0x00
};

/* WIN17.DAT: the ordinary scene, the same two opcodes with a marker of its
   own. */
static unsigned char fixture_win17_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, CH18_PLAIN_MARKER_OPERAND, CH18_PLAIN_MARKER_VALUE,
    0x00
};

/* WIN16.DAT: the decoy a body copied from chapter 17's handler lands on.  It
   retires nobody, so a run that opened it is two failed assertions rather than
   one. */
static unsigned char fixture_ch18_win16_dat[] = {
    0x12, RANDIS_UNIT, CH18_DECOY_MARKER_OPERAND, CH18_WIN16_DECOY_VALUE,
    0x00
};

/* WIN18.DAT: the decoy a handler that named the chapter it hands ON to lands
   on, retiring nobody for the same reason. */
static unsigned char fixture_ch18_win18_dat[] = {
    0x12, RANDIS_UNIT, CH18_DECOY_MARKER_OPERAND, CH18_WIN18_DECOY_VALUE,
    0x00
};

static char *fixture_ch18_names[CH18_FIXTURE_MEMBERS] = {
    "WIN16.DAT", "WIN17-1.DAT", "WIN17.DAT", "WIN18.DAT"
};

static unsigned char *fixture_ch18_bytes[CH18_FIXTURE_MEMBERS] = {
    fixture_ch18_win16_dat, fixture_win17_1_dat, fixture_win17_dat,
    fixture_ch18_win18_dat
};

static int fixture_ch18_lengths[CH18_FIXTURE_MEMBERS] = {
    sizeof(fixture_ch18_win16_dat), sizeof(fixture_win17_1_dat),
    sizeof(fixture_win17_dat), sizeof(fixture_ch18_win18_dat)
};

/* 0 not attempted, 1 built by this half and usable, 2 unavailable. */
static int ch18_fixture_state = 0;

/* What one run of the handler left behind. */
struct ch18_snapshot {
    unsigned char unit_bag[CH18_BAG_BYTES];
    unsigned char slot_bag[CH18_BAG_BYTES];
    unsigned char unit_timers[STATUS_TIMER_COUNT];
    int unit_flags;
    int slot_char_id;
    int slot_hp_current;
    int slot_hp_max;
    int enemy_hp_current;
    int enemy_hp_max;
    int chapter_id;
    int party_gold;
    int late_joiner_portrait_id;
    int late_joiner_char_id;
};

/* 0 not attempted, 1 the run happened and its snapshot is good, 2 the run
   could not be made and every case that reads it says so. */
static int ch18_trade_state = 0;
static int ch18_denied_state = 0;
static int ch18_noseal_state = 0;
static struct ch18_snapshot ch18_trade_seen;
static struct ch18_snapshot ch18_denied_seen;
static struct ch18_snapshot ch18_noseal_seen;

/* Builds the container the three runs share, once, to the layout in
   resource_info/vfs.md and by the same refusal protocol as the halves above:
   an IconAni.vfs that was already there is left alone and every case then
   fails on its run-state assertion rather than passing on whatever that
   container held. */
static int ch18_fixture_available(void)
{
    FILE *fp;
    long member_at;
    int i;

    if (ch18_fixture_state != 0) {
        return ch18_fixture_state == 1;
    }
    ch18_fixture_state = 2;

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
    write_dword(fp, (long) CH18_FIXTURE_MEMBERS);
    fwrite("Dynasty Information Co.,", 1, VFS_SIGNATURE_BYTES, fp);

    member_at = (long) VFS_HEADER_BYTES
                + (long) CH18_FIXTURE_MEMBERS * VFS_ENTRY_BYTES;
    for (i = 0; i < CH18_FIXTURE_MEMBERS; i++) {
        write_name(fp, fixture_ch18_names[i]);
        write_dword(fp, (long) fixture_ch18_lengths[i]);
        write_dword(fp, (long) fixture_ch18_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_ch18_lengths[i];
    }
    for (i = 0; i < CH18_FIXTURE_MEMBERS; i++) {
        fwrite(fixture_ch18_bytes[i], 1, (size_t) fixture_ch18_lengths[i], fp);
    }
    fclose(fp);

    ch18_fixture_state = 1;
    return 1;
}

/* Empties one bag: every entry flagged empty with the unused id beside it. */
static void ch18_clear_bag(struct fdps_unit_record *unit)
{
    int entry;

    for (entry = 0; entry < CH18_BAG_ENTRIES; entry++) {
        unit->inventory_slots[entry * 2] = CH18_BAG_EMPTY;
        unit->inventory_slots[entry * 2 + 1] = CH18_BAG_UNUSED_ID;
    }
}

/* Puts one carried, unequipped item in one bag entry.  Carried and not
   equipped is what matters: the 0x40 bit is what would send
   fdps_roster_recompute_combat_stats into the item table, and this file's
   table is eight zeroed rows rather than the real ITEM.DAT. */
static void ch18_put_item(struct fdps_unit_record *unit, int entry,
                          int item_id)
{
    unit->inventory_slots[entry * 2] = CH18_BAG_CARRIED;
    unit->inventory_slots[entry * 2 + 1] = (unsigned char) item_id;
}

/* The battle array, the roster block and the item table as they stand when
   chapter 18's battle has just been won.  promoted_unit is the index of the
   one unit staged as having already changed class, or -1 for none;
   randis_carries_seal says whether the seal is in 蘭迪斯's bag. */
static void ch18_stage_globals(int promoted_unit, int randis_carries_seal)
{
    int unit_index;

    memset(ch18_unit_image, 0, sizeof(ch18_unit_image));
    memset(ch18_roster_image, ROSTER_FILLER, sizeof(ch18_roster_image));
    memset(ch18_item_image, 0, sizeof(ch18_item_image));

    for (unit_index = 0; unit_index < CH18_UNIT_COUNT; unit_index++) {
        ch18_unit_image[unit_index].side = PLAYER_SIDE;
        ch18_unit_image[unit_index].char_id = (unsigned char) unit_index;
        ch18_unit_image[unit_index].portrait_id = (unsigned char) unit_index;
        ch18_unit_image[unit_index].hp_current = CH18_PARTY_HP_CURRENT;
        ch18_unit_image[unit_index].hp_max = CH18_PARTY_HP_MAX;
        ch18_clear_bag(&ch18_unit_image[unit_index]);
    }

    ch18_unit_image[CH18_LATE_JOINER_UNIT].char_id =
        CH18_LATE_JOINER_CHAR_ID;
    ch18_unit_image[CH18_LATE_JOINER_UNIT].portrait_id =
        CH18_LATE_JOINER_CHAR_ID;

    if (promoted_unit >= 0) {
        ch18_unit_image[promoted_unit].portrait_id =
            CH18_PROMOTED_PORTRAIT_ID;
    }

    ch18_unit_image[CH18_ENEMY_UNIT].side = ENEMY_SIDE;
    ch18_unit_image[CH18_ENEMY_UNIT].char_id = CH18_ENEMY_CHAR_ID;
    ch18_unit_image[CH18_ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    ch18_unit_image[CH18_ENEMY_UNIT].hp_current = CH18_ENEMY_HP;
    ch18_unit_image[CH18_ENEMY_UNIT].hp_max = CH18_ENEMY_HP;

    /* 蘭迪斯's bag: the seal in the FIRST entry and the filler item behind it,
       so the removal has to shift the filler down and the badge lands in the
       entry the filler vacated rather than in the one the seal was in.  A
       trade that overwrote the seal in place would leave the filler where it
       is and put the badge in entry 0. */
    if (randis_carries_seal) {
        ch18_put_item(&ch18_unit_image[RANDIS_UNIT], 0, CH18_SEAL_ID);
        ch18_put_item(&ch18_unit_image[RANDIS_UNIT], 1,
                      CH18_FILLER_ITEM_ID);
    } else {
        ch18_put_item(&ch18_unit_image[RANDIS_UNIT], 0,
                      CH18_FILLER_ITEM_ID);
    }

    ch18_roster_image[RANDIS_ROSTER_SLOT].char_id = RANDIS_CHAR_ID;

    data_fdps_map_unit_array_ptr = (unsigned char *) ch18_unit_image;
    data_fdps_roster_array_ptr = (unsigned char *) ch18_roster_image;
    data_fdps_item_effect_table_ptr = ch18_item_image;
    data_fdps_map_unit_count = CH18_UNIT_COUNT;
    data_fdps_roster_member_count = 1;
    data_fdps_chapter_current_chapter_id = CHAPTER_ID_BEFORE;
    data_fdps_shared_party_total_gold = PARTY_GOLD_BEFORE;
}

static void ch18_capture(struct ch18_snapshot *seen)
{
    int i;

    for (i = 0; i < CH18_BAG_BYTES; i++) {
        seen->unit_bag[i] = ch18_unit_image[RANDIS_UNIT].inventory_slots[i];
        seen->slot_bag[i] =
            ch18_roster_image[RANDIS_ROSTER_SLOT].inventory_slots[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        seen->unit_timers[i] = ch18_unit_image[RANDIS_UNIT].status_timers[i];
    }
    seen->unit_flags = (int) ch18_unit_image[RANDIS_UNIT].flags;
    seen->slot_char_id = (int) ch18_roster_image[RANDIS_ROSTER_SLOT].char_id;
    seen->slot_hp_current =
        (int) ch18_roster_image[RANDIS_ROSTER_SLOT].hp_current;
    seen->slot_hp_max = (int) ch18_roster_image[RANDIS_ROSTER_SLOT].hp_max;
    seen->enemy_hp_current =
        (int) ch18_unit_image[CH18_ENEMY_UNIT].hp_current;
    seen->enemy_hp_max = (int) ch18_unit_image[CH18_ENEMY_UNIT].hp_max;
    seen->chapter_id = data_fdps_chapter_current_chapter_id;
    seen->party_gold = data_fdps_shared_party_total_gold;
    seen->late_joiner_portrait_id =
        (int) ch18_unit_image[CH18_LATE_JOINER_UNIT].portrait_id;
    seen->late_joiner_char_id =
        (int) ch18_unit_image[CH18_LATE_JOINER_UNIT].char_id;
}

/* Run A: the seal is carried, the nine are unpromoted and the TENTH unit is
   promoted, so the trade must be made anyway. */
static void ch18_run_trade(void)
{
    if (ch18_trade_state != 0) {
        return;
    }
    ch18_trade_state = 2;

    if (!ch18_fixture_available()) {
        return;
    }

    ch18_stage_globals(CH18_LATE_JOINER_UNIT, 1);
    fdps_chapter_18_end();
    ch18_capture(&ch18_trade_seen);
    ch18_trade_state = 1;
}

/* Run B: the seal is carried and unit EIGHT has changed class, the last index
   the sweep reaches, so the trade must be denied. */
static void ch18_run_denied(void)
{
    if (ch18_denied_state != 0) {
        return;
    }
    ch18_denied_state = 2;

    if (!ch18_fixture_available()) {
        return;
    }

    ch18_stage_globals(CH18_LAST_SWEPT_UNIT, 1);
    fdps_chapter_18_end();
    ch18_capture(&ch18_denied_seen);
    ch18_denied_state = 1;
}

/* Run C: the seal is not carried, so the gate closes before the sweep. */
static void ch18_run_no_seal(void)
{
    if (ch18_noseal_state != 0) {
        return;
    }
    ch18_noseal_state = 2;

    if (!ch18_fixture_available()) {
        return;
    }

    ch18_stage_globals(-1, 0);
    fdps_chapter_18_end();
    ch18_capture(&ch18_noseal_seen);
    ch18_noseal_state = 1;
}

/* How many bag entries of a captured bag hold one item id, counting only
   entries that are not flagged empty. */
static int ch18_bag_holds(unsigned char *bag, int item_id)
{
    int entry;
    int found;

    found = 0;
    for (entry = 0; entry < CH18_BAG_ENTRIES; entry++) {
        if ((bag[entry * 2] & CH18_BAG_EMPTY) == 0
                && (int) bag[entry * 2 + 1] == item_id) {
            found++;
        }
    }
    return found;
}

/* The trade is made, and made as two calls rather than one edit: the seal is
   gone from the bag, the filler item that sat behind it has been shifted down
   into entry 0 by the removal, and the badge has been put in entry 1 -- the
   entry the shift vacated, not the entry the seal was in. */
static void chapter_18_trades_the_seal_for_the_hero_badge(void)
{
    ch18_run_trade();
    CHECK_EQ(ch18_trade_state, 1);
    if (ch18_trade_state != 1) {
        return;
    }

    CHECK_EQ(ch18_bag_holds(ch18_trade_seen.unit_bag, CH18_SEAL_ID), 0);
    CHECK_EQ(ch18_bag_holds(ch18_trade_seen.unit_bag, CH18_BADGE_ID), 1);
    CHECK_EQ(ch18_trade_seen.unit_bag[0], CH18_BAG_CARRIED);
    CHECK_EQ(ch18_trade_seen.unit_bag[1], CH18_FILLER_ITEM_ID);
    CHECK_EQ(ch18_trade_seen.unit_bag[2], CH18_BAG_CARRIED);
    CHECK_EQ(ch18_trade_seen.unit_bag[3], CH18_BADGE_ID);
    CHECK_EQ(ch18_trade_seen.unit_bag[4] & CH18_BAG_EMPTY, CH18_BAG_EMPTY);
}

/* The trade happens BEFORE the writeback, so the bag banked onto the roster is
   the traded one: roster slot 0 carries the badge and not the seal.  An order
   the other way round would bank the seal and throw the badge away with the
   battle record.  The slot is the battle record's copy in the first place --
   its character id and its healed hit points say the writeback ran at all. */
static void chapter_18_banks_the_traded_bag_onto_the_roster(void)
{
    ch18_run_trade();
    CHECK_EQ(ch18_trade_state, 1);
    if (ch18_trade_state != 1) {
        return;
    }

    CHECK_EQ(ch18_trade_seen.slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch18_trade_seen.slot_hp_max, CH18_PARTY_HP_MAX);
    CHECK_EQ(ch18_trade_seen.slot_hp_current, CH18_PARTY_HP_MAX);
    CHECK_EQ(ch18_bag_holds(ch18_trade_seen.slot_bag, CH18_BADGE_ID), 1);
    CHECK_EQ(ch18_bag_holds(ch18_trade_seen.slot_bag, CH18_SEAL_ID), 0);
}

/* The scene the trade earns is Win17-1.dat and it really ran: both of that
   member's opcodes are on the live battle record -- the retired bit at +5 and
   the marker 101 in status_timers[4].  WIN17.DAT would have written 103 into
   status_timers[3] and either decoy a value of its own into status_timers[5],
   so a name in any direction is a failed assertion rather than a silent pass.
   The retire also pins the order: the writeback skips a retired character-0
   unit, so the roster assertions above could not have held if the script had
   been interpreted first. */
static void chapter_18_alt_victory_cutscene_is_win17_1_dat(void)
{
    ch18_run_trade();
    CHECK_EQ(ch18_trade_state, 1);
    if (ch18_trade_state != 1) {
        return;
    }

    CHECK_EQ(ch18_trade_seen.unit_timers[CH18_ALT_MARKER_SLOT],
             CH18_ALT_MARKER_VALUE);
    CHECK_EQ(ch18_trade_seen.unit_timers[CH18_PLAIN_MARKER_SLOT], 0);
    CHECK_EQ(ch18_trade_seen.unit_timers[CH18_DECOY_MARKER_SLOT], 0);
    CHECK_EQ(ch18_trade_seen.unit_flags, UNIT_FLAG_RETIRED);
}

/* The map is swept, and swept the way 00039e10 sweeps it: the enemy's
   hit-point word is 0 where the staging left 44 and its maximum in the word
   behind it is untouched.  A handler that omitted the call would leave the
   enemy at 44. */
static void chapter_18_sweeps_the_enemy_side(void)
{
    ch18_run_trade();
    CHECK_EQ(ch18_trade_state, 1);
    if (ch18_trade_state != 1) {
        return;
    }

    CHECK_EQ(ch18_trade_seen.enemy_hp_current, 0);
    CHECK_EQ(ch18_trade_seen.enemy_hp_max, CH18_ENEMY_HP);
}

/* THE SWEEP'S BOUND IS A LITERAL NINE.  This run stages eleven units, tells
   the game so through data_fdps_map_unit_count, and gives unit 9 a form id
   that differs from its character id -- a promoted 瑪麗安.  The badge is still
   granted, which is only possible if the loop stopped at index 8.  A loop
   written over the live unit count, or over the whole roster, fails here.
   Unit 9's own record is left alone by the handler either way. */
static void chapter_18_sweep_ignores_the_tenth_unit(void)
{
    ch18_run_trade();
    CHECK_EQ(ch18_trade_state, 1);
    if (ch18_trade_state != 1) {
        return;
    }

    CHECK_EQ(ch18_bag_holds(ch18_trade_seen.unit_bag, CH18_BADGE_ID), 1);
    CHECK_EQ(ch18_trade_seen.late_joiner_portrait_id,
             CH18_PROMOTED_PORTRAIT_ID);
    CHECK_EQ(ch18_trade_seen.late_joiner_char_id, CH18_LATE_JOINER_CHAR_ID);
}

/* The chapter index is left at chapter 19's, 18, as an assignment and not as a
   step from what was there: the run starts it at 4, so an increment would read
   back 5 and the store's own literal reads back 18.  It also pins the other
   half of the handler's deliberate off-by-one -- a 17 here, matching the 17 in
   both script names, would be chapter 18 replayed. */
static void chapter_18_advances_the_chapter_index_to_chapter_nineteen(void)
{
    ch18_run_trade();
    CHECK_EQ(ch18_trade_state, 1);
    if (ch18_trade_state != 1) {
        return;
    }

    CHECK_EQ(ch18_trade_seen.chapter_id, CH18_CHAPTER_ID_AFTER);
}

/* Nobody fell, so the revive sweep charges nothing: the writeback put the one
   roster member on his maximum, which leaves it no member at 0 HP to bill for.
   The enemy the first call left at 0 HP is not a roster member and is not
   billed for either. */
static void chapter_18_revive_charges_nothing_when_nobody_fell(void)
{
    ch18_run_trade();
    CHECK_EQ(ch18_trade_state, 1);
    if (ch18_trade_state != 1) {
        return;
    }

    CHECK_EQ(ch18_trade_seen.party_gold, PARTY_GOLD_BEFORE);
}

/* A PROMOTED UNIT AT INDEX EIGHT DENIES THE TRADE, which pins the other end of
   the bound: index 8 is inside the sweep where index 9 above was outside it.
   The seal is still in the bag, in the entry it started in, and no badge was
   added -- neither in the battle record nor in the roster copy of it. */
static void chapter_18_a_promoted_ninth_unit_denies_the_trade(void)
{
    ch18_run_denied();
    CHECK_EQ(ch18_denied_state, 1);
    if (ch18_denied_state != 1) {
        return;
    }

    CHECK_EQ(ch18_bag_holds(ch18_denied_seen.unit_bag, CH18_SEAL_ID), 1);
    CHECK_EQ(ch18_bag_holds(ch18_denied_seen.unit_bag, CH18_BADGE_ID), 0);
    CHECK_EQ(ch18_denied_seen.unit_bag[1], CH18_SEAL_ID);
    CHECK_EQ(ch18_denied_seen.unit_bag[3], CH18_FILLER_ITEM_ID);
    CHECK_EQ(ch18_bag_holds(ch18_denied_seen.slot_bag, CH18_BADGE_ID), 0);
}

/* The denied run plays the ORDINARY scene: the marker 103 in
   status_timers[3], with the alternative's slot and both decoys' slot still
   0.  This is the second thing the gate decides and it is decided by the same
   flag as the trade, so a handler that traded nothing but still played
   Win17-1.dat fails here. */
static void chapter_18_ordinary_cutscene_is_win17_dat_when_denied(void)
{
    ch18_run_denied();
    CHECK_EQ(ch18_denied_state, 1);
    if (ch18_denied_state != 1) {
        return;
    }

    CHECK_EQ(ch18_denied_seen.unit_timers[CH18_PLAIN_MARKER_SLOT],
             CH18_PLAIN_MARKER_VALUE);
    CHECK_EQ(ch18_denied_seen.unit_timers[CH18_ALT_MARKER_SLOT], 0);
    CHECK_EQ(ch18_denied_seen.unit_timers[CH18_DECOY_MARKER_SLOT], 0);
    CHECK_EQ(ch18_denied_seen.unit_flags, UNIT_FLAG_RETIRED);
}

/* The four unconditional steps run down the denied arm too: the map is still
   swept, the party is still banked and the chapter is still advanced.  The
   gate decides what 蘭迪斯 carries and which scene plays, and nothing else. */
static void chapter_18_still_closes_the_chapter_when_denied(void)
{
    ch18_run_denied();
    CHECK_EQ(ch18_denied_state, 1);
    if (ch18_denied_state != 1) {
        return;
    }

    CHECK_EQ(ch18_denied_seen.enemy_hp_current, 0);
    CHECK_EQ(ch18_denied_seen.slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch18_denied_seen.slot_hp_current, CH18_PARTY_HP_MAX);
    CHECK_EQ(ch18_denied_seen.chapter_id, CH18_CHAPTER_ID_AFTER);
    CHECK_EQ(ch18_denied_seen.party_gold, PARTY_GOLD_BEFORE);
}

/* WITHOUT THE SEAL NOTHING IS TRADED, and the gate closes on the -1 from
   fdps_unit_find_item_slot before the sweep has run at all: 蘭迪斯's bag is
   exactly what the staging put in it, one filler item in entry 0 and seven
   empty entries, and no badge reaches the roster copy either.  Every one of
   the nine is unpromoted in this run, so the only thing that can be holding
   the badge back is the missing seal. */
static void chapter_18_grants_nothing_without_the_seal(void)
{
    ch18_run_no_seal();
    CHECK_EQ(ch18_noseal_state, 1);
    if (ch18_noseal_state != 1) {
        return;
    }

    CHECK_EQ(ch18_bag_holds(ch18_noseal_seen.unit_bag, CH18_BADGE_ID), 0);
    CHECK_EQ(ch18_bag_holds(ch18_noseal_seen.unit_bag, CH18_SEAL_ID), 0);
    CHECK_EQ(ch18_noseal_seen.unit_bag[1], CH18_FILLER_ITEM_ID);
    CHECK_EQ(ch18_noseal_seen.unit_bag[2] & CH18_BAG_EMPTY, CH18_BAG_EMPTY);
    CHECK_EQ(ch18_bag_holds(ch18_noseal_seen.slot_bag, CH18_BADGE_ID), 0);
}

/* The no-seal run plays the ordinary scene and closes the chapter the same
   way the denied run does.  The two arms of the gate reach the same four
   steps, which is what the assembly's JZ at 0003adca jumps straight to. */
static void chapter_18_ordinary_cutscene_runs_without_the_seal(void)
{
    ch18_run_no_seal();
    CHECK_EQ(ch18_noseal_state, 1);
    if (ch18_noseal_state != 1) {
        return;
    }

    CHECK_EQ(ch18_noseal_seen.unit_timers[CH18_PLAIN_MARKER_SLOT],
             CH18_PLAIN_MARKER_VALUE);
    CHECK_EQ(ch18_noseal_seen.unit_timers[CH18_ALT_MARKER_SLOT], 0);
    CHECK_EQ(ch18_noseal_seen.unit_timers[CH18_DECOY_MARKER_SLOT], 0);
    CHECK_EQ(ch18_noseal_seen.enemy_hp_current, 0);
    CHECK_EQ(ch18_noseal_seen.chapter_id, CH18_CHAPTER_ID_AFTER);
}

/* The shared fixture container goes again, so that nothing this file wrote
   outlives its run and the next file that wants that name finds it free. */
static void the_ch18_fixture_container_is_removed(void)
{
    if (ch18_fixture_state != 1) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    ch18_fixture_state = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

/* --------------------------------------------------------------------------
 * fdps_chapter_19_end at 0003b0b0.
 *
 * NOT THE FAMILY'S SHAPE EITHER, and it differs at both ends: a conditional
 * free full recovery of the party stands in front, and the map sweep the
 * halves above all make is simply absent.
 *
 *   0003b0bc  CMP byte ptr [0x000640e9],0x0 / JZ 0003b125
 *             element 0x11 of data_fdps_map_cell_event_triggered_flags, the
 *             duel latch: clear and the whole recovery is skipped
 *   0003b0cf  CMP EAX,[0x00060150] / JL
 *             the recovery walks the LIVE unit count, not a literal
 *   0003b0e5  CALL 0x0002d210          the record is resolved
 *   0003b0f3  MOV AL,[EAX+0x8] / AND EAX,0xff / CMP EAX,0xb / JG 0003b123
 *             an UNSIGNED test on the character id
 *   0003b103  MOV byte ptr [EAX+0x5],0x0      the whole flags byte
 *   0003b10a  MOV DX,[EAX+0x42] / MOV [EAX+0x40],DX     hp_current = hp_max
 *   0003b118  MOV DX,[EAX+0x46] / MOV [EAX+0x44],DX     mp_current = mp_max
 *   0003b125  CALL 0x00023980          the battle party is banked
 *   0003b12a  MOV EAX,0x62194 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win18.dat" is interpreted
 *   0003b138  CALL 0x00039e70          the fallen are revived
 *   0003b13d  MOV dword ptr [0x00069cf4],0x13
 *
 * There is no CALL 0x00039e10 anywhere in the body, which is the one thing
 * this half can witness that none of the three above can.
 *
 * TWO RUNS, because one run cannot witness a branch.  Each stages the battle
 * array and the roster afresh and runs the whole handler for real:
 *
 *   A  the duel latch is UP.  Every staged record carries the retired bit the
 *      duel staging leaves behind plus a second bit beside it, and every one
 *      is staged below its maximum in both hit points and magic points.
 *   B  the duel latch is DOWN, everything else identical, so nothing may be
 *      recovered and the writeback's own rule -- keep the retired bit, do not
 *      heal -- is what the roster ends up holding.
 *
 * WHAT EACH STAGED RECORD IS FOR.  The five inside the live count pin the two
 * ends of the character-id test and the sign it is read with, and the sixth
 * pins the loop bound:
 *
 *   unit 0   character 0, and the record the cut-scene retires
 *   unit 1   character 0x0b, the LAST id the test lets through
 *   unit 2   character 0x0c, the first it does not
 *   unit 3   character 0x80, which a SIGNED test would let through as -128
 *   unit 4   an enemy, character 0x1e, still standing with hit points left
 *   unit 5   character 0x03, sitting BEYOND data_fdps_map_unit_count
 *
 * WHY THE ENEMY IS STAGED ALREADY RETIRED even though nothing here sweeps it:
 * so that a handler which did call the sweep fails on the hit-point assertion
 * rather than hanging.  The death pass that ends
 * fdps_battle_destroy_remaining_enemies collects units whose retired bit is
 * clear and whose hit points are 0, and for a non-empty list it spins them and
 * renders frames, none of which can run in a test image (src/death.c).
 *
 * WHY NOBODY MAY BE LEFT AT 0 HIT POINTS on the roster, in either run: the
 * revive panel ends in a frame loop that runs until a keyboard make code
 * arrives and nothing queues one in a test image, the same reason the halves
 * above give.  Run B's roster copies therefore keep the below-maximum hit
 * points they were staged with rather than reaching 0.
 *
 * Everything else -- why the cut-scene is a fixture rather than the shipped
 * WIN18.DAT, and the refusal protocol around the IconAni.vfs name -- is the
 * reasoning at the top of this file, unchanged.  Both runs share ONE
 * container, built by whichever runs first and removed by the last case.
 * ------------------------------------------------------------------------ */

/* The duel latch the whole recovery is gated on: element 0x11 of the 32-entry
   array, the byte at 0x000640e9 the compare at 0003b0bc reads. */
#define CH19_DUEL_LATCH_SLOT 0x11

/* The three members, and the marker each writes.  WIN18.DAT is the one the
   handler names; the two decoys share status_timers[5] with values of their
   own, so one assertion that the slot is still 0 rules both out and the value
   says which one ran if it is not.  WIN17.DAT is where a body copied from
   fdps_chapter_18_end without changing the operand would land, and WIN19.DAT
   is where a handler that named the chapter it hands ON to -- the 0x13 of the
   store -- would land. */
#define CH19_WIN18_MARKER_OPERAND 1
#define CH19_WIN18_MARKER_SLOT 4
#define CH19_WIN18_MARKER_VALUE 118
#define CH19_DECOY_MARKER_OPERAND 2
#define CH19_DECOY_MARKER_SLOT 5
#define CH19_WIN17_DECOY_VALUE 117
#define CH19_WIN19_DECOY_VALUE 119

#define CH19_FIXTURE_MEMBERS 3

/* The index chapter 19's handler must leave: chapter 20, 0-based, the literal
   of the store at 0003b13d.

   The run starts from the same 4 the halves above start from, which is none of
   the numbers a mistake would leave behind: not the stored 19, not the 18 an
   off-by-one that followed the script name would leave, and not the 5 an
   increment would leave. */
#define CH19_CHAPTER_ID_AFTER 19

/* The battle array.  Six records staged, five of them inside the live count. */
#define CH19_UNIT_CAPACITY 8
#define CH19_UNIT_COUNT 5
#define CH19_ROSTER_CAPACITY 4

#define CH19_LAST_ROSTER_UNIT 1
#define CH19_LAST_ROSTER_CHAR_ID 0x0b
#define CH19_PAST_BOUND_UNIT 2
#define CH19_PAST_BOUND_CHAR_ID 0x0c
#define CH19_HIGH_ID_UNIT 3
#define CH19_HIGH_ID_CHAR_ID 0x80
#define CH19_ENEMY_UNIT_SLOT 4
#define CH19_ENEMY_UNIT_CHAR_ID 0x1e
#define CH19_BEYOND_COUNT_UNIT 5
#define CH19_BEYOND_COUNT_CHAR_ID 0x03

/* The roster block: character 0 in slot 0 and character 0x0b in slot 1, the
   two the writeback has somewhere to bank. */
#define CH19_LAST_ROSTER_SLOT 1
#define CH19_ROSTER_MEMBERS 2

/* What every record is staged with: the retired bit 0x01 the duel staging
   leaves behind, and bit 0x04 beside it so that the handler's whole-byte store
   can be told apart from an AND-NOT of bit 0, which would leave the 0x04.

   THE SECOND BIT CANNOT BE 0x80.  fdps_icon_script_run opens by calling
   fdps_units_clear_status_bit7, which masks 0x80 off every record inside
   data_fdps_map_unit_count (src/icon.c), so a record staged with 0x81 comes
   back holding 0x01 whether this handler touched it or not and neither the
   whole-byte case nor the untouched-record cases would mean anything. */
#define CH19_STAGED_FLAGS 0x05

/* Every record is staged below its maximum in both pools, so the recovery's
   two word stores are visible, and every one is well above 0 so the revive
   panel never opens. */
#define CH19_RANDIS_HP_CURRENT 25
#define CH19_RANDIS_HP_MAX 40
#define CH19_RANDIS_MP_CURRENT 2
#define CH19_RANDIS_MP_MAX 9
#define CH19_RANDIS_LEVEL 3
#define CH19_LAST_ROSTER_HP_CURRENT 10
#define CH19_LAST_ROSTER_HP_MAX 33
#define CH19_LAST_ROSTER_MP_CURRENT 1
#define CH19_LAST_ROSTER_MP_MAX 7
#define CH19_PAST_BOUND_HP_CURRENT 12
#define CH19_PAST_BOUND_HP_MAX 44
#define CH19_PAST_BOUND_MP_CURRENT 3
#define CH19_PAST_BOUND_MP_MAX 8
#define CH19_HIGH_ID_HP_CURRENT 14
#define CH19_HIGH_ID_HP_MAX 45
#define CH19_HIGH_ID_MP_CURRENT 4
#define CH19_HIGH_ID_MP_MAX 6
#define CH19_BEYOND_HP_CURRENT 5
#define CH19_BEYOND_HP_MAX 50
#define CH19_ENEMY_HP 44

static struct fdps_unit_record ch19_unit_image[CH19_UNIT_CAPACITY];
static struct fdps_unit_record ch19_roster_image[CH19_ROSTER_CAPACITY];
static unsigned char ch19_item_image[ITEM_TABLE_ROWS
                                     * sizeof(struct fdps_item_effect)];

/* WIN18.DAT: retire battle unit 0, write the marker into its
   status_timers[4], stop.  The retire opcode is what makes a run that
   interpreted the script BEFORE banking the party visible: the writeback
   refuses to bank character 0 once he has left the field, so the roster slot
   would still be filler. */
static unsigned char fixture_win18_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, CH19_WIN18_MARKER_OPERAND, CH19_WIN18_MARKER_VALUE,
    0x00
};

/* WIN17.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_ch19_win17_dat[] = {
    0x12, RANDIS_UNIT, CH19_DECOY_MARKER_OPERAND, CH19_WIN17_DECOY_VALUE,
    0x00
};

/* WIN19.DAT: the decoy a handler that named the chapter it hands ON to lands
   on, retiring nobody for the same reason. */
static unsigned char fixture_ch19_win19_dat[] = {
    0x12, RANDIS_UNIT, CH19_DECOY_MARKER_OPERAND, CH19_WIN19_DECOY_VALUE,
    0x00
};

static char *fixture_ch19_names[CH19_FIXTURE_MEMBERS] = {
    "WIN17.DAT", "WIN18.DAT", "WIN19.DAT"
};

static unsigned char *fixture_ch19_bytes[CH19_FIXTURE_MEMBERS] = {
    fixture_ch19_win17_dat, fixture_win18_dat, fixture_ch19_win19_dat
};

static int fixture_ch19_lengths[CH19_FIXTURE_MEMBERS] = {
    sizeof(fixture_ch19_win17_dat), sizeof(fixture_win18_dat),
    sizeof(fixture_ch19_win19_dat)
};

/* 0 not attempted, 1 built by this half and usable, 2 unavailable. */
static int ch19_fixture_state = 0;

/* What one run of the handler left behind. */
struct ch19_snapshot {
    unsigned char randis_timers[STATUS_TIMER_COUNT];
    int randis_flags;
    int randis_hp_current;
    int randis_mp_current;
    int last_roster_flags;
    int last_roster_hp_current;
    int last_roster_mp_current;
    int past_bound_flags;
    int past_bound_hp_current;
    int past_bound_mp_current;
    int high_id_flags;
    int high_id_hp_current;
    int high_id_mp_current;
    int beyond_flags;
    int beyond_hp_current;
    int enemy_flags;
    int enemy_hp_current;
    int slot_level;
    int slot_hp_current;
    int slot_mp_current;
    int last_slot_flags;
    int last_slot_hp_current;
    int chapter_id;
    int party_gold;
};

/* 0 not attempted, 1 the run happened and its snapshot is good, 2 the run
   could not be made and every case that reads it says so. */
static int ch19_duel_state = 0;
static int ch19_nolatch_state = 0;
static struct ch19_snapshot ch19_duel_seen;
static struct ch19_snapshot ch19_nolatch_seen;

/* Builds the container the two runs share, once, to the layout in
   resource_info/vfs.md and by the same refusal protocol as the halves above. */
static int ch19_fixture_available(void)
{
    FILE *fp;
    long member_at;
    int i;

    if (ch19_fixture_state != 0) {
        return ch19_fixture_state == 1;
    }
    ch19_fixture_state = 2;

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
    write_dword(fp, (long) CH19_FIXTURE_MEMBERS);
    fwrite("Dynasty Information Co.,", 1, VFS_SIGNATURE_BYTES, fp);

    member_at = (long) VFS_HEADER_BYTES
                + (long) CH19_FIXTURE_MEMBERS * VFS_ENTRY_BYTES;
    for (i = 0; i < CH19_FIXTURE_MEMBERS; i++) {
        write_name(fp, fixture_ch19_names[i]);
        write_dword(fp, (long) fixture_ch19_lengths[i]);
        write_dword(fp, (long) fixture_ch19_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_ch19_lengths[i];
    }
    for (i = 0; i < CH19_FIXTURE_MEMBERS; i++) {
        fwrite(fixture_ch19_bytes[i], 1, (size_t) fixture_ch19_lengths[i], fp);
    }
    fclose(fp);

    ch19_fixture_state = 1;
    return 1;
}

/* The battle array, the roster block and the item table as they stand when
   chapter 19's battle has just been won and the duel has been staged over it.
   latch says whether the chapter's duel was ever put to the player. */
static void ch19_stage_globals(int latch)
{
    memset(ch19_unit_image, 0, sizeof(ch19_unit_image));
    memset(ch19_roster_image, ROSTER_FILLER, sizeof(ch19_roster_image));
    memset(ch19_item_image, 0, sizeof(ch19_item_image));

    ch19_unit_image[RANDIS_UNIT].char_id = RANDIS_CHAR_ID;
    ch19_unit_image[RANDIS_UNIT].side = PLAYER_SIDE;
    ch19_unit_image[RANDIS_UNIT].flags = CH19_STAGED_FLAGS;
    ch19_unit_image[RANDIS_UNIT].level = CH19_RANDIS_LEVEL;
    ch19_unit_image[RANDIS_UNIT].hp_current = CH19_RANDIS_HP_CURRENT;
    ch19_unit_image[RANDIS_UNIT].hp_max = CH19_RANDIS_HP_MAX;
    ch19_unit_image[RANDIS_UNIT].mp_current = CH19_RANDIS_MP_CURRENT;
    ch19_unit_image[RANDIS_UNIT].mp_max = CH19_RANDIS_MP_MAX;

    ch19_unit_image[CH19_LAST_ROSTER_UNIT].char_id = CH19_LAST_ROSTER_CHAR_ID;
    ch19_unit_image[CH19_LAST_ROSTER_UNIT].side = PLAYER_SIDE;
    ch19_unit_image[CH19_LAST_ROSTER_UNIT].flags = CH19_STAGED_FLAGS;
    ch19_unit_image[CH19_LAST_ROSTER_UNIT].hp_current =
        CH19_LAST_ROSTER_HP_CURRENT;
    ch19_unit_image[CH19_LAST_ROSTER_UNIT].hp_max = CH19_LAST_ROSTER_HP_MAX;
    ch19_unit_image[CH19_LAST_ROSTER_UNIT].mp_current =
        CH19_LAST_ROSTER_MP_CURRENT;
    ch19_unit_image[CH19_LAST_ROSTER_UNIT].mp_max = CH19_LAST_ROSTER_MP_MAX;

    ch19_unit_image[CH19_PAST_BOUND_UNIT].char_id = CH19_PAST_BOUND_CHAR_ID;
    ch19_unit_image[CH19_PAST_BOUND_UNIT].side = PLAYER_SIDE;
    ch19_unit_image[CH19_PAST_BOUND_UNIT].flags = CH19_STAGED_FLAGS;
    ch19_unit_image[CH19_PAST_BOUND_UNIT].hp_current =
        CH19_PAST_BOUND_HP_CURRENT;
    ch19_unit_image[CH19_PAST_BOUND_UNIT].hp_max = CH19_PAST_BOUND_HP_MAX;
    ch19_unit_image[CH19_PAST_BOUND_UNIT].mp_current =
        CH19_PAST_BOUND_MP_CURRENT;
    ch19_unit_image[CH19_PAST_BOUND_UNIT].mp_max = CH19_PAST_BOUND_MP_MAX;

    ch19_unit_image[CH19_HIGH_ID_UNIT].char_id = CH19_HIGH_ID_CHAR_ID;
    ch19_unit_image[CH19_HIGH_ID_UNIT].side = PLAYER_SIDE;
    ch19_unit_image[CH19_HIGH_ID_UNIT].flags = CH19_STAGED_FLAGS;
    ch19_unit_image[CH19_HIGH_ID_UNIT].hp_current = CH19_HIGH_ID_HP_CURRENT;
    ch19_unit_image[CH19_HIGH_ID_UNIT].hp_max = CH19_HIGH_ID_HP_MAX;
    ch19_unit_image[CH19_HIGH_ID_UNIT].mp_current = CH19_HIGH_ID_MP_CURRENT;
    ch19_unit_image[CH19_HIGH_ID_UNIT].mp_max = CH19_HIGH_ID_MP_MAX;

    ch19_unit_image[CH19_ENEMY_UNIT_SLOT].char_id = CH19_ENEMY_UNIT_CHAR_ID;
    ch19_unit_image[CH19_ENEMY_UNIT_SLOT].side = ENEMY_SIDE;
    ch19_unit_image[CH19_ENEMY_UNIT_SLOT].flags = UNIT_FLAG_RETIRED;
    ch19_unit_image[CH19_ENEMY_UNIT_SLOT].hp_current = CH19_ENEMY_HP;
    ch19_unit_image[CH19_ENEMY_UNIT_SLOT].hp_max = CH19_ENEMY_HP;

    ch19_unit_image[CH19_BEYOND_COUNT_UNIT].char_id =
        CH19_BEYOND_COUNT_CHAR_ID;
    ch19_unit_image[CH19_BEYOND_COUNT_UNIT].side = PLAYER_SIDE;
    ch19_unit_image[CH19_BEYOND_COUNT_UNIT].flags = CH19_STAGED_FLAGS;
    ch19_unit_image[CH19_BEYOND_COUNT_UNIT].hp_current =
        CH19_BEYOND_HP_CURRENT;
    ch19_unit_image[CH19_BEYOND_COUNT_UNIT].hp_max = CH19_BEYOND_HP_MAX;

    ch19_roster_image[RANDIS_ROSTER_SLOT].char_id = RANDIS_CHAR_ID;
    ch19_roster_image[CH19_LAST_ROSTER_SLOT].char_id =
        CH19_LAST_ROSTER_CHAR_ID;

    data_fdps_map_cell_event_triggered_flags[CH19_DUEL_LATCH_SLOT] =
        (unsigned char) latch;
    data_fdps_map_unit_array_ptr = (unsigned char *) ch19_unit_image;
    data_fdps_roster_array_ptr = (unsigned char *) ch19_roster_image;
    data_fdps_item_effect_table_ptr = ch19_item_image;
    data_fdps_map_unit_count = CH19_UNIT_COUNT;
    data_fdps_roster_member_count = CH19_ROSTER_MEMBERS;
    data_fdps_chapter_current_chapter_id = CHAPTER_ID_BEFORE;
    data_fdps_shared_party_total_gold = PARTY_GOLD_BEFORE;
}

static void ch19_capture(struct ch19_snapshot *seen)
{
    int i;

    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        seen->randis_timers[i] = ch19_unit_image[RANDIS_UNIT].status_timers[i];
    }
    seen->randis_flags = (int) ch19_unit_image[RANDIS_UNIT].flags;
    seen->randis_hp_current = (int) ch19_unit_image[RANDIS_UNIT].hp_current;
    seen->randis_mp_current = (int) ch19_unit_image[RANDIS_UNIT].mp_current;
    seen->last_roster_flags =
        (int) ch19_unit_image[CH19_LAST_ROSTER_UNIT].flags;
    seen->last_roster_hp_current =
        (int) ch19_unit_image[CH19_LAST_ROSTER_UNIT].hp_current;
    seen->last_roster_mp_current =
        (int) ch19_unit_image[CH19_LAST_ROSTER_UNIT].mp_current;
    seen->past_bound_flags = (int) ch19_unit_image[CH19_PAST_BOUND_UNIT].flags;
    seen->past_bound_hp_current =
        (int) ch19_unit_image[CH19_PAST_BOUND_UNIT].hp_current;
    seen->past_bound_mp_current =
        (int) ch19_unit_image[CH19_PAST_BOUND_UNIT].mp_current;
    seen->high_id_flags = (int) ch19_unit_image[CH19_HIGH_ID_UNIT].flags;
    seen->high_id_hp_current =
        (int) ch19_unit_image[CH19_HIGH_ID_UNIT].hp_current;
    seen->high_id_mp_current =
        (int) ch19_unit_image[CH19_HIGH_ID_UNIT].mp_current;
    seen->beyond_flags = (int) ch19_unit_image[CH19_BEYOND_COUNT_UNIT].flags;
    seen->beyond_hp_current =
        (int) ch19_unit_image[CH19_BEYOND_COUNT_UNIT].hp_current;
    seen->enemy_flags = (int) ch19_unit_image[CH19_ENEMY_UNIT_SLOT].flags;
    seen->enemy_hp_current =
        (int) ch19_unit_image[CH19_ENEMY_UNIT_SLOT].hp_current;
    seen->slot_level = (int) ch19_roster_image[RANDIS_ROSTER_SLOT].level;
    seen->slot_hp_current =
        (int) ch19_roster_image[RANDIS_ROSTER_SLOT].hp_current;
    seen->slot_mp_current =
        (int) ch19_roster_image[RANDIS_ROSTER_SLOT].mp_current;
    seen->last_slot_flags =
        (int) ch19_roster_image[CH19_LAST_ROSTER_SLOT].flags;
    seen->last_slot_hp_current =
        (int) ch19_roster_image[CH19_LAST_ROSTER_SLOT].hp_current;
    seen->chapter_id = data_fdps_chapter_current_chapter_id;
    seen->party_gold = data_fdps_shared_party_total_gold;
}

/* Run A: the duel was put to the player, so the recovery must run. */
static void ch19_run_duel(void)
{
    if (ch19_duel_state != 0) {
        return;
    }
    ch19_duel_state = 2;

    if (!ch19_fixture_available()) {
        return;
    }

    ch19_stage_globals(1);
    fdps_chapter_19_end();
    ch19_capture(&ch19_duel_seen);
    ch19_duel_state = 1;
}

/* Run B: the duel was never offered, so the recovery must be skipped whole. */
static void ch19_run_no_latch(void)
{
    if (ch19_nolatch_state != 0) {
        return;
    }
    ch19_nolatch_state = 2;

    if (!ch19_fixture_available()) {
        return;
    }

    ch19_stage_globals(0);
    fdps_chapter_19_end();
    ch19_capture(&ch19_nolatch_seen);
    ch19_nolatch_state = 1;
}

/* The recovery reaches the last id the test lets through, 0x0b, and it does
   all three of its stores there: the whole flags byte is gone, hit points are
   on the maximum and so are magic points.  Expected values are the record's
   own maxima as staged, which is what MOV DX,[EAX+0x42] / MOV [EAX+0x40],DX
   and MOV DX,[EAX+0x46] / MOV [EAX+0x44],DX at 0003b10a and 0003b118 copy. */
static void chapter_19_recovers_the_party_when_the_duel_was_offered(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(ch19_duel_seen.last_roster_flags, 0);
    CHECK_EQ(ch19_duel_seen.last_roster_hp_current, CH19_LAST_ROSTER_HP_MAX);
    CHECK_EQ(ch19_duel_seen.last_roster_mp_current, CH19_LAST_ROSTER_MP_MAX);
}

/* The flags store is a whole byte and not an AND-NOT of the retired bit: the
   record was staged with 0x05 and the 0x04 has to be gone with the retired
   one.  MOV byte ptr [EAX+0x5],0x0 at 0003b103. */
static void chapter_19_recovery_clears_the_whole_flags_byte(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(CH19_STAGED_FLAGS & 0x04, 0x04);
    CHECK_EQ(ch19_duel_seen.last_roster_flags & 0x04, 0);
    CHECK_EQ(ch19_duel_seen.last_roster_flags & 0x01, 0);
}

/* The character-id test stops at 0x0b: the record carrying 0x0c is left
   exactly as it was staged, flags byte and both pools.  CMP EAX,0xb / JG
   0003b123. */
static void chapter_19_recovery_stops_after_character_id_eleven(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(ch19_duel_seen.past_bound_flags, CH19_STAGED_FLAGS);
    CHECK_EQ(ch19_duel_seen.past_bound_hp_current, CH19_PAST_BOUND_HP_CURRENT);
    CHECK_EQ(ch19_duel_seen.past_bound_mp_current, CH19_PAST_BOUND_MP_CURRENT);
}

/* The character id is read UNSIGNED.  0x80 is 128 and fails the test; a signed
   char would make it -128 and pass, healing the record.  AND EAX,0xff at
   0003b0f6, and char_id declared unsigned char in src/fdpstype.h. */
static void chapter_19_recovery_reads_the_character_id_unsigned(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(ch19_duel_seen.high_id_flags, CH19_STAGED_FLAGS);
    CHECK_EQ(ch19_duel_seen.high_id_hp_current, CH19_HIGH_ID_HP_CURRENT);
    CHECK_EQ(ch19_duel_seen.high_id_mp_current, CH19_HIGH_ID_MP_CURRENT);
}

/* The sweep's bound is data_fdps_map_unit_count and not a literal: the sixth
   record carries character id 3, which the test would let through, and sits
   one past the count, so it must come out untouched.  CMP EAX,[0x00060150] /
   JL at 0003b0cf. */
static void chapter_19_recovery_walks_the_live_unit_count(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(CH19_BEYOND_COUNT_UNIT >= CH19_UNIT_COUNT, 1);
    CHECK_EQ(ch19_duel_seen.beyond_flags, CH19_STAGED_FLAGS);
    CHECK_EQ(ch19_duel_seen.beyond_hp_current, CH19_BEYOND_HP_CURRENT);
}

/* The map is never swept.  There is no CALL 0x00039e10 in the body, so the
   enemy staged with hit points left keeps every one of them -- the one thing
   that separates this handler from chapters 16, 17 and 18. */
static void chapter_19_does_not_sweep_the_enemy_side(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(ch19_duel_seen.enemy_hp_current, CH19_ENEMY_HP);
    CHECK_EQ(ch19_duel_seen.enemy_flags, UNIT_FLAG_RETIRED);
}

/* The recovery runs BEFORE the writeback.  The roster copy of character 0x0b
   comes out un-retired and on its maximum; had the writeback gone first it
   would have seen the retired bit, kept it and left the below-maximum hit
   points beside it -- which is exactly what run B below shows. */
static void chapter_19_recovers_the_party_before_banking_it(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(ch19_duel_seen.last_slot_flags, 0);
    CHECK_EQ(ch19_duel_seen.last_slot_hp_current, CH19_LAST_ROSTER_HP_MAX);
}

/* The party is banked BEFORE the cut-scene is interpreted.  WIN18.DAT's first
   opcode retires battle unit 0, and the writeback refuses to bank character 0
   once he has left the field, so a run that interpreted the script first would
   leave roster slot 0 at its 0xa5 filler instead of carrying the record.  The
   level the slot ends up holding is the battle record's, and the marker in the
   LIVE record's status_timers[4] -- which the writeback's memset clears on the
   roster copy but cannot reach on the battle record -- says the script ran. */
static void chapter_19_banks_the_party_before_the_cutscene_runs(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(ch19_duel_seen.slot_level, CH19_RANDIS_LEVEL);
    CHECK_EQ(ch19_duel_seen.slot_hp_current, CH19_RANDIS_HP_MAX);
    CHECK_EQ(ch19_duel_seen.slot_mp_current, CH19_RANDIS_MP_MAX);
    CHECK_EQ(ch19_duel_seen.randis_flags, UNIT_FLAG_RETIRED);
}

/* The member opened is WIN18.DAT, the chapter just won, and neither of the two
   decoys either side of it in the image.  MOV EAX,0x62194 at 0003b12a. */
static void chapter_19_victory_cutscene_is_win18_dat(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(ch19_duel_seen.randis_timers[CH19_WIN18_MARKER_SLOT],
             CH19_WIN18_MARKER_VALUE);
    CHECK_EQ(ch19_duel_seen.randis_timers[CH19_DECOY_MARKER_SLOT], 0);
}

/* The revive sweep charges nothing, because the recovery and the writeback
   between them have left no roster member at 0 hit points for it to find. */
static void chapter_19_revive_charges_nothing_when_nobody_fell(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(ch19_duel_seen.party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is advanced to 19, chapter 20, and not to the 18 the
   script name carries.  MOV dword ptr [0x00069cf4],0x13 at 0003b13d. */
static void chapter_19_advances_the_chapter_index_to_chapter_twenty(void)
{
    ch19_run_duel();
    CHECK_EQ(ch19_duel_state, 1);
    CHECK_EQ(ch19_duel_seen.chapter_id, CH19_CHAPTER_ID_AFTER);
}

/* Run B.  With the latch down the whole recovery is jumped over: the record
   carrying character 0x0b keeps the flags byte and both below-maximum pools it
   was staged with.  CMP byte ptr [0x000640e9],0x0 / JZ 0003b125 at 0003b0bc. */
static void chapter_19_recovers_nobody_without_the_duel_latch(void)
{
    ch19_run_no_latch();
    CHECK_EQ(ch19_nolatch_state, 1);
    CHECK_EQ(ch19_nolatch_seen.last_roster_flags, CH19_STAGED_FLAGS);
    CHECK_EQ(ch19_nolatch_seen.last_roster_hp_current,
             CH19_LAST_ROSTER_HP_CURRENT);
    CHECK_EQ(ch19_nolatch_seen.last_roster_mp_current,
             CH19_LAST_ROSTER_MP_CURRENT);
}

/* And the writeback then does what it does with a retired record: it keeps the
   masked bit and does NOT lift hit points to the maximum, which is why the
   recovery has to run in front of it rather than behind it. */
static void chapter_19_banks_a_retired_party_without_the_latch(void)
{
    ch19_run_no_latch();
    CHECK_EQ(ch19_nolatch_state, 1);
    CHECK_EQ(ch19_nolatch_seen.last_slot_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch19_nolatch_seen.last_slot_hp_current,
             CH19_LAST_ROSTER_HP_CURRENT);
}

/* The three unconditional steps run either way: the same cut-scene is opened
   and the same chapter index is left behind. */
static void chapter_19_still_closes_the_chapter_without_the_latch(void)
{
    ch19_run_no_latch();
    CHECK_EQ(ch19_nolatch_state, 1);
    CHECK_EQ(ch19_nolatch_seen.randis_timers[CH19_WIN18_MARKER_SLOT],
             CH19_WIN18_MARKER_VALUE);
    CHECK_EQ(ch19_nolatch_seen.randis_timers[CH19_DECOY_MARKER_SLOT], 0);
    CHECK_EQ(ch19_nolatch_seen.chapter_id, CH19_CHAPTER_ID_AFTER);
    CHECK_EQ(ch19_nolatch_seen.party_gold, PARTY_GOLD_BEFORE);
}

/* The shared fixture container goes again, so that nothing this file wrote
   outlives its run and the next file that wants that name finds it free. */
static void the_ch19_fixture_container_is_removed(void)
{
    if (ch19_fixture_state != 1) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    ch19_fixture_state = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

/* --------------------------------------------------------------------------
 * fdps_chapter_20_end at 0003b1d0.
 *
 * The family's plain shape again, the one chapters 16 and 17 have -- four
 * calls and one store, no branch and no local -- so the cases are the same six
 * asserting the same things about a run of their own, with one more that pins
 * what this handler does NOT do.  The two operands that differ: the member
 * opened is WIN19.DAT, the index of the chapter just won, and the store leaves
 * 20, the index of the chapter that comes next.
 *
 *   0003b1dc  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003b1e1  CALL 0x00023980          the battle party is banked
 *   0003b1e6  MOV EAX,0x621a0 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win19.dat" is interpreted
 *   0003b1f4  CALL 0x00039e70          the fallen are revived
 *   0003b1f9  MOV dword ptr [0x00069cf4],0x14
 *
 * NOTHING STANDS IN FRONT OF THE FOUR, which is the difference from the
 * chapter 19 half just above: no latch is read and no unit record is recovered
 * before the sweep, so a single staging and a single run cover the whole
 * handler.  The case that catches a gate copied over from that sibling is
 * chapter_20_recovers_nobody_before_banking below, which stages a party member
 * carrying a mark of its own and pins what the writeback alone does with it.
 *
 * WHY THIS HALF STAGES ITS OWN CONTAINER.  Only one IconAni.vfs can stand in
 * the run directory at a time and the case that removes chapter 19's runs
 * between the two halves, so this half builds one of its own with its own
 * three members.  The same refusal protocol applies: a container already
 * standing is left alone, and then every case here fails on its run-state
 * assertion rather than passing on whatever that other container held.
 *
 * Everything else -- why the enemy is staged already retired, why nobody may
 * be left at 0 HP, why the cut-scene is a fixture rather than the shipped
 * WIN19.DAT -- is the reasoning at the top of this file, unchanged.
 * ------------------------------------------------------------------------ */

/* WIN19.DAT, the member chapter 20's handler names: operand 2, so
   status_timers[5], with a value neither decoy writes. */
#define WIN19_CH20_MARKER_OPERAND 2
#define WIN19_CH20_MARKER_SLOT 5
#define WIN19_CH20_MARKER_VALUE 101

/* WIN18.DAT, the member a handler that followed its own script number one step
   low would open, in a slot and with a value of its own.  It is also the
   member the sibling handler one chapter back really does name, so a body
   copied from fdps_chapter_19_end without changing the operand lands here. */
#define WIN18_CH20_MARKER_OPERAND 1
#define WIN18_CH20_MARKER_SLOT 4
#define WIN18_CH20_MARKER_VALUE 103

/* WIN20.DAT, the member a handler that named the chapter it hands ON to rather
   than the one just won would open -- the exact slip the store's 0x14 invites.
   It is a real member of the shipped container too, sitting at 0x621ac
   immediately behind the name this handler uses, so the slip would not
   announce itself as a missing member. */
#define WIN20_CH20_MARKER_OPERAND 0
#define WIN20_CH20_MARKER_SLOT 3
#define WIN20_CH20_MARKER_VALUE 107

/* The index chapter 20's handler must leave: chapter 21, 0-based, the literal
   of the store at 0003b1f9.

   The run starts from the same 4 chapter 16's does, which is none of the three
   numbers a mistake would leave behind: not the stored 20, not the 19 an
   off-by-one that followed the script name would leave, and not the 5 an
   increment would leave. */
#define CH20_CHAPTER_ID_AFTER 20

/* A mark the party member is staged carrying, above the bit 0 the writeback
   keeps: bit 1 of the flags byte at record +5.  It is what makes the absence
   of chapter 19's recovery sweep visible. */
#define CH20_STAGED_PARTY_FLAGS 2

/* WIN19.DAT: retire battle unit 0, write the marker into its status_timers[5],
   stop -- the same two opcodes the halves above use, so the order argument
   below is the same one. */
static unsigned char fixture_ch20_win19_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN19_CH20_MARKER_OPERAND, WIN19_CH20_MARKER_VALUE,
    0x00
};

/* WIN18.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_ch20_win18_dat[] = {
    0x12, RANDIS_UNIT, WIN18_CH20_MARKER_OPERAND, WIN18_CH20_MARKER_VALUE,
    0x00
};

/* WIN20.DAT: the decoy one step high, retiring nobody for the same reason. */
static unsigned char fixture_ch20_win20_dat[] = {
    0x12, RANDIS_UNIT, WIN20_CH20_MARKER_OPERAND, WIN20_CH20_MARKER_VALUE,
    0x00
};

static char *fixture_ch20_names[FIXTURE_MEMBERS] = {
    "WIN18.DAT", "WIN19.DAT", "WIN20.DAT"
};

static unsigned char *fixture_ch20_bytes[FIXTURE_MEMBERS] = {
    fixture_ch20_win18_dat, fixture_ch20_win19_dat, fixture_ch20_win20_dat
};

static int fixture_ch20_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_ch20_win18_dat), sizeof(fixture_ch20_win19_dat),
    sizeof(fixture_ch20_win20_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case says so. */
static int ch20_run_state = 0;

/* Whether this half created the container, and so whether it may remove it. */
static int ch20_fixture_owned = 0;

/* Everything chapter 20's cases assert, captured the instant it returned. */
static unsigned char ch20_seen_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch20_seen_unit_timers[STATUS_TIMER_COUNT];
static unsigned char ch20_seen_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch20_seen_slot_timers[STATUS_TIMER_COUNT];
static int ch20_seen_unit_flags;
static int ch20_seen_unit_hp_current;
static int ch20_seen_unit_mp_current;
static int ch20_seen_slot_char_id;
static int ch20_seen_slot_flags;
static int ch20_seen_slot_hp_current;
static int ch20_seen_slot_hp_max;
static int ch20_seen_slot_mp_current;
static int ch20_seen_slot_level;
static int ch20_seen_enemy_hp_current;
static int ch20_seen_enemy_hp_max;
static int ch20_seen_enemy_flags;
static int ch20_seen_chapter_id;
static int ch20_seen_party_gold;

/* Builds chapter 20's fixture container, or answers no, to the layout in
   resource_info/vfs.md and by the same refusal protocol as the halves above. */
static int stage_ch20_fixture_archive(void)
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
        write_name(fp, fixture_ch20_names[i]);
        write_dword(fp, (long) fixture_ch20_lengths[i]);
        write_dword(fp, (long) fixture_ch20_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_ch20_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture_ch20_bytes[i], 1, (size_t) fixture_ch20_lengths[i], fp);
    }
    fclose(fp);

    ch20_fixture_owned = 1;
    return 1;
}

/* The shared staging, plus the mark on the party member's flags byte that
   chapter 19's recovery sweep would have taken off and this handler must leave
   for the writeback to mask. */
static void ch20_stage_globals(void)
{
    stage_globals();
    unit_image[RANDIS_UNIT].flags = CH20_STAGED_PARTY_FLAGS;
}

static void ch20_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch20_seen_unit_spells[i] =
            unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch20_seen_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch20_seen_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch20_seen_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch20_seen_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch20_seen_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch20_seen_unit_mp_current = (int) unit_image[RANDIS_UNIT].mp_current;
    ch20_seen_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch20_seen_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch20_seen_slot_hp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch20_seen_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch20_seen_slot_mp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch20_seen_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch20_seen_enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    ch20_seen_enemy_hp_max = (int) unit_image[ENEMY_UNIT].hp_max;
    ch20_seen_enemy_flags = (int) unit_image[ENEMY_UNIT].flags;
    ch20_seen_chapter_id = data_fdps_chapter_current_chapter_id;
    ch20_seen_party_gold = data_fdps_shared_party_total_gold;
}

/* Runs chapter 20's handler once, against freshly staged arrays and its own
   fixture cut-scene, and records what it left behind. */
static void run_ch20_handler(void)
{
    if (ch20_run_state != 0) {
        return;
    }
    ch20_run_state = 2;

    if (!stage_ch20_fixture_archive()) {
        return;
    }

    ch20_stage_globals();

    fdps_chapter_20_end();

    ch20_capture();
    ch20_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1, 2, 15 and 19 have -- and 19 is the handler immediately before
   this one -- would leave the enemy at 50. */
static void chapter_20_sweeps_the_enemy_side(void)
{
    run_ch20_handler();
    CHECK_EQ(ch20_run_state, 1);
    if (ch20_run_state != 1) {
        return;
    }

    CHECK_EQ(ch20_seen_enemy_hp_current, 0);
    CHECK_EQ(ch20_seen_enemy_hp_max, ENEMY_HP_MAX);
    CHECK_EQ(ch20_seen_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch20_seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN19.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_20_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_ch20_handler();
    CHECK_EQ(ch20_run_state, 1);
    if (ch20_run_state != 1) {
        return;
    }

    CHECK_EQ(ch20_seen_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch20_seen_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch20_seen_slot_flags, 0);
    CHECK_EQ(ch20_seen_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch20_seen_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch20_seen_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch20_seen_slot_timers[i], 0);
    }
}

/* NOTHING RECOVERS THE LIVE RECORD BEFORE THE BANKING, which is what separates
   this handler from the chapter 19 sibling it sits behind.  The party member
   is staged carrying flags 2, and on the live record that 2 is still there
   underneath the retired bit WIN19.DAT raised -- the byte reads back 3 -- and
   the live magic points are still the 2 the staging gave rather than the
   maximum a recovery would have restored.  A recovery sweep copied over from
   fdps_chapter_19_end would have cleared the byte and put both pools on their
   maxima before the writeback ever ran. */
static void chapter_20_recovers_nobody_before_banking(void)
{
    run_ch20_handler();
    CHECK_EQ(ch20_run_state, 1);
    if (ch20_run_state != 1) {
        return;
    }

    CHECK_EQ(ch20_seen_unit_flags,
             CH20_STAGED_PARTY_FLAGS | UNIT_FLAG_RETIRED);
    CHECK_EQ(ch20_seen_unit_mp_current, RANDIS_MP_CURRENT);
    CHECK_EQ(ch20_seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003b1dc is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from fdps_chapter_01_end would show as byte 0 reading 0x01. */
static void chapter_20_grants_no_spell(void)
{
    int i;

    run_ch20_handler();
    CHECK_EQ(ch20_run_state, 1);
    if (ch20_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch20_seen_unit_spells[i], 0);
        CHECK_EQ(ch20_seen_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win19.dat and it really ran: the marker
   value 101 is in status_timers[5] of the live battle record, where only that
   member's second opcode puts it.  The two decoys in the container write 103
   into status_timers[4] and 107 into status_timers[3], so a name one step in
   either direction is three failed assertions rather than a silent pass -- and
   both of those names are real members of the shipped container as well, so
   neither slip would show up as a missing member.  This is the half of the
   handler's deliberate off-by-one that carries the index of the chapter just
   ENDED, 19, against the 20 the store leaves.  The marker does not reach the
   roster copy, whose timers the writeback cleared before the script ran. */
static void chapter_20_victory_cutscene_is_win19_dat(void)
{
    run_ch20_handler();
    CHECK_EQ(ch20_run_state, 1);
    if (ch20_run_state != 1) {
        return;
    }

    CHECK_EQ(ch20_seen_unit_timers[WIN19_CH20_MARKER_SLOT],
             WIN19_CH20_MARKER_VALUE);
    CHECK_EQ(ch20_seen_unit_timers[WIN18_CH20_MARKER_SLOT], 0);
    CHECK_EQ(ch20_seen_unit_timers[WIN20_CH20_MARKER_SLOT], 0);
    CHECK_EQ(ch20_seen_slot_timers[WIN19_CH20_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The enemy the first
   call left at 0 HP is not a roster member and is not billed for either. */
static void chapter_20_revive_charges_nothing_when_nobody_fell(void)
{
    run_ch20_handler();
    CHECK_EQ(ch20_run_state, 1);
    if (ch20_run_state != 1) {
        return;
    }

    CHECK_EQ(ch20_seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 21's, 20, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 20.  It also pins the
   other half of the handler's deliberate off-by-one -- a 19 here, matching the
   19 in the script name, would be chapter 20 replayed rather than chapter 21
   started. */
static void chapter_20_advances_the_chapter_index_to_chapter_twenty_one(void)
{
    run_ch20_handler();
    CHECK_EQ(ch20_run_state, 1);
    if (ch20_run_state != 1) {
        return;
    }

    CHECK_EQ(ch20_seen_chapter_id, CH20_CHAPTER_ID_AFTER);
}

/* Chapter 20's fixture container goes again, so that nothing this file wrote
   outlives its run and the next file that wants that name finds it free. */
static void the_ch20_fixture_container_is_removed(void)
{
    if (!ch20_fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    ch20_fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

/* --------------------------------------------------------------------------
 * fdps_chapter_21_end at 0003b230.
 *
 * The family's plain shape again, the one chapters 16, 17 and 20 have -- four
 * calls and one store, no branch and no local -- so the cases are the same
 * seven asserting the same things about a run of their own.  The two operands
 * that differ from the chapter 20 half above: the member opened is WIN20.DAT,
 * the index of the chapter just won, and the store leaves 21, the index of the
 * chapter that comes next.
 *
 *   0003b23c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003b241  CALL 0x00023980          the battle party is banked
 *   0003b246  MOV EAX,0x621ac / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win20.dat" is interpreted
 *   0003b254  CALL 0x00039e70          the fallen are revived
 *   0003b259  MOV dword ptr [0x00069cf4],0x15
 *
 * NOTHING STANDS IN FRONT OF THE FOUR: no latch is read and no unit record is
 * recovered before the sweep, so a single staging and a single run cover the
 * whole handler.  The case that catches a gate copied over from
 * fdps_chapter_19_end is chapter_21_recovers_nobody_before_banking below,
 * which stages a party member carrying a mark of its own and pins what the
 * writeback alone does with it.
 *
 * WHY THIS HALF STAGES ITS OWN CONTAINER.  Only one IconAni.vfs can stand in
 * the run directory at a time and the case that removes chapter 20's runs
 * between the two halves, so this half builds one of its own with its own
 * three members.  The same refusal protocol applies: a container already
 * standing is left alone, and then every case here fails on its run-state
 * assertion rather than passing on whatever that other container held.
 *
 * Everything else -- why the enemy is staged already retired, why nobody may
 * be left at 0 HP, why the cut-scene is a fixture rather than the shipped
 * WIN20.DAT -- is the reasoning at the top of this file, unchanged.
 * ------------------------------------------------------------------------ */

/* WIN20.DAT, the member chapter 21's handler names: operand 1, so
   status_timers[4], with a value neither decoy writes. */
#define WIN20_CH21_MARKER_OPERAND 1
#define WIN20_CH21_MARKER_SLOT 4
#define WIN20_CH21_MARKER_VALUE 109

/* WIN19.DAT, the member a handler that followed its own script number one step
   low would open, in a slot and with a value of its own.  It is also the
   member the sibling handler one chapter back really does name, so a body
   copied from fdps_chapter_20_end without changing the operand lands here. */
#define WIN19_CH21_MARKER_OPERAND 0
#define WIN19_CH21_MARKER_SLOT 3
#define WIN19_CH21_MARKER_VALUE 113

/* WIN21.DAT, the member a handler that named the chapter it hands ON to rather
   than the one just won would open -- the exact slip the store's 0x15 invites.
   It is a real member of the shipped container too, sitting at 0x621b8
   immediately behind the name this handler uses, so the slip would not
   announce itself as a missing member. */
#define WIN21_CH21_MARKER_OPERAND 2
#define WIN21_CH21_MARKER_SLOT 5
#define WIN21_CH21_MARKER_VALUE 127

/* The index chapter 21's handler must leave: chapter 22, 0-based, the literal
   of the store at 0003b259.

   The run starts from the same 4 chapter 16's does, which is none of the three
   numbers a mistake would leave behind: not the stored 21, not the 20 an
   off-by-one that followed the script name would leave, and not the 5 an
   increment would leave. */
#define CH21_CHAPTER_ID_AFTER 21

/* A mark the party member is staged carrying, above the bit 0 the writeback
   keeps: bit 1 of the flags byte at record +5.  It is what makes the absence
   of a recovery sweep visible. */
#define CH21_STAGED_PARTY_FLAGS 2

/* WIN20.DAT: retire battle unit 0, write the marker into its status_timers[4],
   stop -- the same two opcodes the halves above use, so the order argument
   below is the same one. */
static unsigned char fixture_ch21_win20_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN20_CH21_MARKER_OPERAND, WIN20_CH21_MARKER_VALUE,
    0x00
};

/* WIN19.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_ch21_win19_dat[] = {
    0x12, RANDIS_UNIT, WIN19_CH21_MARKER_OPERAND, WIN19_CH21_MARKER_VALUE,
    0x00
};

/* WIN21.DAT: the decoy one step high, retiring nobody for the same reason. */
static unsigned char fixture_ch21_win21_dat[] = {
    0x12, RANDIS_UNIT, WIN21_CH21_MARKER_OPERAND, WIN21_CH21_MARKER_VALUE,
    0x00
};

static char *fixture_ch21_names[FIXTURE_MEMBERS] = {
    "WIN19.DAT", "WIN20.DAT", "WIN21.DAT"
};

static unsigned char *fixture_ch21_bytes[FIXTURE_MEMBERS] = {
    fixture_ch21_win19_dat, fixture_ch21_win20_dat, fixture_ch21_win21_dat
};

static int fixture_ch21_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_ch21_win19_dat), sizeof(fixture_ch21_win20_dat),
    sizeof(fixture_ch21_win21_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case says so. */
static int ch21_run_state = 0;

/* Whether this half created the container, and so whether it may remove it. */
static int ch21_fixture_owned = 0;

/* Everything chapter 21's cases assert, captured the instant it returned. */
static unsigned char ch21_seen_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch21_seen_unit_timers[STATUS_TIMER_COUNT];
static unsigned char ch21_seen_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch21_seen_slot_timers[STATUS_TIMER_COUNT];
static int ch21_seen_unit_flags;
static int ch21_seen_unit_hp_current;
static int ch21_seen_unit_mp_current;
static int ch21_seen_slot_char_id;
static int ch21_seen_slot_flags;
static int ch21_seen_slot_hp_current;
static int ch21_seen_slot_hp_max;
static int ch21_seen_slot_mp_current;
static int ch21_seen_slot_level;
static int ch21_seen_enemy_hp_current;
static int ch21_seen_enemy_hp_max;
static int ch21_seen_enemy_flags;
static int ch21_seen_chapter_id;
static int ch21_seen_party_gold;

/* Builds chapter 21's fixture container, or answers no, to the layout in
   resource_info/vfs.md and by the same refusal protocol as the halves above. */
static int stage_ch21_fixture_archive(void)
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
        write_name(fp, fixture_ch21_names[i]);
        write_dword(fp, (long) fixture_ch21_lengths[i]);
        write_dword(fp, (long) fixture_ch21_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_ch21_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture_ch21_bytes[i], 1, (size_t) fixture_ch21_lengths[i], fp);
    }
    fclose(fp);

    ch21_fixture_owned = 1;
    return 1;
}

/* The shared staging, plus the mark on the party member's flags byte that a
   recovery sweep would have taken off and this handler must leave for the
   writeback to mask. */
static void ch21_stage_globals(void)
{
    stage_globals();
    unit_image[RANDIS_UNIT].flags = CH21_STAGED_PARTY_FLAGS;
}

static void ch21_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch21_seen_unit_spells[i] =
            unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch21_seen_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch21_seen_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch21_seen_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch21_seen_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch21_seen_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch21_seen_unit_mp_current = (int) unit_image[RANDIS_UNIT].mp_current;
    ch21_seen_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch21_seen_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch21_seen_slot_hp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch21_seen_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch21_seen_slot_mp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch21_seen_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch21_seen_enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    ch21_seen_enemy_hp_max = (int) unit_image[ENEMY_UNIT].hp_max;
    ch21_seen_enemy_flags = (int) unit_image[ENEMY_UNIT].flags;
    ch21_seen_chapter_id = data_fdps_chapter_current_chapter_id;
    ch21_seen_party_gold = data_fdps_shared_party_total_gold;
}

/* Runs chapter 21's handler once, against freshly staged arrays and its own
   fixture cut-scene, and records what it left behind. */
static void run_ch21_handler(void)
{
    if (ch21_run_state != 0) {
        return;
    }
    ch21_run_state = 2;

    if (!stage_ch21_fixture_archive()) {
        return;
    }

    ch21_stage_globals();

    fdps_chapter_21_end();

    ch21_capture();
    ch21_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1, 2, 15 and 19 have, would leave the enemy at 50. */
static void chapter_21_sweeps_the_enemy_side(void)
{
    run_ch21_handler();
    CHECK_EQ(ch21_run_state, 1);
    if (ch21_run_state != 1) {
        return;
    }

    CHECK_EQ(ch21_seen_enemy_hp_current, 0);
    CHECK_EQ(ch21_seen_enemy_hp_max, ENEMY_HP_MAX);
    CHECK_EQ(ch21_seen_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch21_seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN20.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_21_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_ch21_handler();
    CHECK_EQ(ch21_run_state, 1);
    if (ch21_run_state != 1) {
        return;
    }

    CHECK_EQ(ch21_seen_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch21_seen_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch21_seen_slot_flags, 0);
    CHECK_EQ(ch21_seen_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch21_seen_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch21_seen_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch21_seen_slot_timers[i], 0);
    }
}

/* NOTHING RECOVERS THE LIVE RECORD BEFORE THE BANKING.  The party member is
   staged carrying flags 2, and on the live record that 2 is still there
   underneath the retired bit WIN20.DAT raised -- the byte reads back 3 -- and
   the live magic points are still the 2 the staging gave rather than the
   maximum a recovery would have restored.  A recovery sweep copied over from
   fdps_chapter_19_end would have cleared the byte and put both pools on their
   maxima before the writeback ever ran. */
static void chapter_21_recovers_nobody_before_banking(void)
{
    run_ch21_handler();
    CHECK_EQ(ch21_run_state, 1);
    if (ch21_run_state != 1) {
        return;
    }

    CHECK_EQ(ch21_seen_unit_flags,
             CH21_STAGED_PARTY_FLAGS | UNIT_FLAG_RETIRED);
    CHECK_EQ(ch21_seen_unit_mp_current, RANDIS_MP_CURRENT);
    CHECK_EQ(ch21_seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003b23c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from fdps_chapter_01_end would show as byte 0 reading 0x01. */
static void chapter_21_grants_no_spell(void)
{
    int i;

    run_ch21_handler();
    CHECK_EQ(ch21_run_state, 1);
    if (ch21_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch21_seen_unit_spells[i], 0);
        CHECK_EQ(ch21_seen_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win20.dat and it really ran: the marker
   value 109 is in status_timers[4] of the live battle record, where only that
   member's second opcode puts it.  The two decoys in the container write 113
   into status_timers[3] and 127 into status_timers[5], so a name one step in
   either direction is three failed assertions rather than a silent pass -- and
   both of those names are real members of the shipped container as well, so
   neither slip would show up as a missing member.  This is the half of the
   handler's deliberate off-by-one that carries the index of the chapter just
   ENDED, 20, against the 21 the store leaves.  The marker does not reach the
   roster copy, whose timers the writeback cleared before the script ran. */
static void chapter_21_victory_cutscene_is_win20_dat(void)
{
    run_ch21_handler();
    CHECK_EQ(ch21_run_state, 1);
    if (ch21_run_state != 1) {
        return;
    }

    CHECK_EQ(ch21_seen_unit_timers[WIN20_CH21_MARKER_SLOT],
             WIN20_CH21_MARKER_VALUE);
    CHECK_EQ(ch21_seen_unit_timers[WIN19_CH21_MARKER_SLOT], 0);
    CHECK_EQ(ch21_seen_unit_timers[WIN21_CH21_MARKER_SLOT], 0);
    CHECK_EQ(ch21_seen_slot_timers[WIN20_CH21_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The enemy the first
   call left at 0 HP is not a roster member and is not billed for either. */
static void chapter_21_revive_charges_nothing_when_nobody_fell(void)
{
    run_ch21_handler();
    CHECK_EQ(ch21_run_state, 1);
    if (ch21_run_state != 1) {
        return;
    }

    CHECK_EQ(ch21_seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 22's, 21, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 21.  It also pins the
   other half of the handler's deliberate off-by-one -- a 20 here, matching the
   20 in the script name, would be chapter 21 replayed rather than chapter 22
   started. */
static void chapter_21_advances_the_chapter_index_to_chapter_twenty_two(void)
{
    run_ch21_handler();
    CHECK_EQ(ch21_run_state, 1);
    if (ch21_run_state != 1) {
        return;
    }

    CHECK_EQ(ch21_seen_chapter_id, CH21_CHAPTER_ID_AFTER);
}

/* Chapter 21's fixture container goes again, so that nothing this file wrote
   outlives its run and the next file that wants that name finds it free. */
static void the_ch21_fixture_container_is_removed(void)
{
    if (!ch21_fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    ch21_fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

/* --------------------------------------------------------------------------
 * fdps_chapter_22_end at 0003b2a0.
 *
 * The family's plain shape again, the one chapters 16, 17, 20 and 21 have --
 * four calls and one store, no branch and no local -- so the cases are the
 * same seven asserting the same things about a run of their own.  The two
 * operands that differ from the chapter 21 half above: the member opened is
 * WIN21.DAT, the index of the chapter just won, and the store leaves 22, the
 * index of the chapter that comes next.
 *
 *   0003b2ac  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003b2b1  CALL 0x00023980          the battle party is banked
 *   0003b2b6  MOV EAX,0x621b8 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win21.dat" is interpreted
 *   0003b2c4  CALL 0x00039e70          the fallen are revived
 *   0003b2c9  MOV dword ptr [0x00069cf4],0x16
 *
 * NOTHING STANDS IN FRONT OF THE FOUR: no latch is read and no unit record is
 * recovered before the sweep, so a single staging and a single run cover the
 * whole handler.  The case that catches a gate copied over from
 * fdps_chapter_19_end is chapter_22_recovers_nobody_before_banking below,
 * which stages a party member carrying a mark of its own and pins what the
 * writeback alone does with it.
 *
 * WHY THE SWEEP MATTERS MORE HERE THAN IN THE TWO HALVES ABOVE.  Chapter 22
 * clears on one named boss and its post-action test declares no victory of
 * its own, so on the shipped map the handler really is entered with enemies
 * still standing and the sweep is what clears them.  What the CALL does is the
 * same either way, and chapter_22_sweeps_the_enemy_side below pins the same
 * store the other halves pin -- a handler that dropped the call, the shape
 * chapters 1, 2, 15 and 19 have, would leave the enemy at 50.
 *
 * WHY THIS HALF STAGES ITS OWN CONTAINER.  Only one IconAni.vfs can stand in
 * the run directory at a time and the case that removes chapter 21's runs
 * between the two halves, so this half builds one of its own with its own
 * three members.  The same refusal protocol applies: a container already
 * standing is left alone, and then every case here fails on its run-state
 * assertion rather than passing on whatever that other container held.
 *
 * Everything else -- why the enemy is staged already retired, why nobody may
 * be left at 0 HP, why the cut-scene is a fixture rather than the shipped
 * WIN21.DAT -- is the reasoning at the top of this file, unchanged.
 * ------------------------------------------------------------------------ */

/* WIN21.DAT, the member chapter 22's handler names: operand 1, so
   status_timers[4], with a value neither decoy writes. */
#define WIN21_CH22_MARKER_OPERAND 1
#define WIN21_CH22_MARKER_SLOT 4
#define WIN21_CH22_MARKER_VALUE 71

/* WIN20.DAT, the member a handler that followed its own script number one step
   low would open, in a slot and with a value of its own.  It is also the
   member the sibling handler one chapter back really does name, so a body
   copied from fdps_chapter_21_end without changing the operand lands here. */
#define WIN20_CH22_MARKER_OPERAND 0
#define WIN20_CH22_MARKER_SLOT 3
#define WIN20_CH22_MARKER_VALUE 79

/* WIN22.DAT, the member a handler that named the chapter it hands ON to rather
   than the one just won would open -- the exact slip the store's 0x16 invites.
   It is a real member of the shipped container too, sitting at 0x621c4
   immediately behind the name this handler uses, so the slip would not
   announce itself as a missing member. */
#define WIN22_CH22_MARKER_OPERAND 2
#define WIN22_CH22_MARKER_SLOT 5
#define WIN22_CH22_MARKER_VALUE 101

/* The index chapter 22's handler must leave: chapter 23, 0-based, the literal
   of the store at 0003b2c9.

   The run starts from the same 4 chapter 16's does, which is none of the three
   numbers a mistake would leave behind: not the stored 22, not the 21 an
   off-by-one that followed the script name would leave, and not the 5 an
   increment would leave. */
#define CH22_CHAPTER_ID_AFTER 22

/* A mark the party member is staged carrying, above the bit 0 the writeback
   keeps: bit 1 of the flags byte at record +5.  It is what makes the absence
   of a recovery sweep visible. */
#define CH22_STAGED_PARTY_FLAGS 2

/* WIN21.DAT: retire battle unit 0, write the marker into its status_timers[4],
   stop -- the same two opcodes the halves above use, so the order argument
   below is the same one. */
static unsigned char fixture_ch22_win21_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN21_CH22_MARKER_OPERAND, WIN21_CH22_MARKER_VALUE,
    0x00
};

/* WIN20.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_ch22_win20_dat[] = {
    0x12, RANDIS_UNIT, WIN20_CH22_MARKER_OPERAND, WIN20_CH22_MARKER_VALUE,
    0x00
};

/* WIN22.DAT: the decoy one step high, retiring nobody for the same reason. */
static unsigned char fixture_ch22_win22_dat[] = {
    0x12, RANDIS_UNIT, WIN22_CH22_MARKER_OPERAND, WIN22_CH22_MARKER_VALUE,
    0x00
};

static char *fixture_ch22_names[FIXTURE_MEMBERS] = {
    "WIN20.DAT", "WIN21.DAT", "WIN22.DAT"
};

static unsigned char *fixture_ch22_bytes[FIXTURE_MEMBERS] = {
    fixture_ch22_win20_dat, fixture_ch22_win21_dat, fixture_ch22_win22_dat
};

static int fixture_ch22_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_ch22_win20_dat), sizeof(fixture_ch22_win21_dat),
    sizeof(fixture_ch22_win22_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case says so. */
static int ch22_run_state = 0;

/* Whether this half created the container, and so whether it may remove it. */
static int ch22_fixture_owned = 0;

/* Everything chapter 22's cases assert, captured the instant it returned. */
static unsigned char ch22_seen_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch22_seen_unit_timers[STATUS_TIMER_COUNT];
static unsigned char ch22_seen_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch22_seen_slot_timers[STATUS_TIMER_COUNT];
static int ch22_seen_unit_flags;
static int ch22_seen_unit_hp_current;
static int ch22_seen_unit_mp_current;
static int ch22_seen_slot_char_id;
static int ch22_seen_slot_flags;
static int ch22_seen_slot_hp_current;
static int ch22_seen_slot_hp_max;
static int ch22_seen_slot_mp_current;
static int ch22_seen_slot_level;
static int ch22_seen_enemy_hp_current;
static int ch22_seen_enemy_hp_max;
static int ch22_seen_enemy_flags;
static int ch22_seen_chapter_id;
static int ch22_seen_party_gold;

/* Builds chapter 22's fixture container, or answers no, to the layout in
   resource_info/vfs.md and by the same refusal protocol as the halves above. */
static int stage_ch22_fixture_archive(void)
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
        write_name(fp, fixture_ch22_names[i]);
        write_dword(fp, (long) fixture_ch22_lengths[i]);
        write_dword(fp, (long) fixture_ch22_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_ch22_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture_ch22_bytes[i], 1, (size_t) fixture_ch22_lengths[i], fp);
    }
    fclose(fp);

    ch22_fixture_owned = 1;
    return 1;
}

/* The shared staging, plus the mark on the party member's flags byte that a
   recovery sweep would have taken off and this handler must leave for the
   writeback to mask. */
static void ch22_stage_globals(void)
{
    stage_globals();
    unit_image[RANDIS_UNIT].flags = CH22_STAGED_PARTY_FLAGS;
}

static void ch22_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch22_seen_unit_spells[i] =
            unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch22_seen_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch22_seen_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch22_seen_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch22_seen_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch22_seen_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch22_seen_unit_mp_current = (int) unit_image[RANDIS_UNIT].mp_current;
    ch22_seen_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch22_seen_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch22_seen_slot_hp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch22_seen_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch22_seen_slot_mp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch22_seen_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch22_seen_enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    ch22_seen_enemy_hp_max = (int) unit_image[ENEMY_UNIT].hp_max;
    ch22_seen_enemy_flags = (int) unit_image[ENEMY_UNIT].flags;
    ch22_seen_chapter_id = data_fdps_chapter_current_chapter_id;
    ch22_seen_party_gold = data_fdps_shared_party_total_gold;
}

/* Runs chapter 22's handler once, against freshly staged arrays and its own
   fixture cut-scene, and records what it left behind. */
static void run_ch22_handler(void)
{
    if (ch22_run_state != 0) {
        return;
    }
    ch22_run_state = 2;

    if (!stage_ch22_fixture_archive()) {
        return;
    }

    ch22_stage_globals();

    fdps_chapter_22_end();

    ch22_capture();
    ch22_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1, 2, 15 and 19 have, would leave the enemy at 50, and on this
   chapter that omission is not cosmetic: the clear does not wait on the enemy
   side being empty, so this call is the only thing that empties it. */
static void chapter_22_sweeps_the_enemy_side(void)
{
    run_ch22_handler();
    CHECK_EQ(ch22_run_state, 1);
    if (ch22_run_state != 1) {
        return;
    }

    CHECK_EQ(ch22_seen_enemy_hp_current, 0);
    CHECK_EQ(ch22_seen_enemy_hp_max, ENEMY_HP_MAX);
    CHECK_EQ(ch22_seen_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch22_seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN21.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_22_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_ch22_handler();
    CHECK_EQ(ch22_run_state, 1);
    if (ch22_run_state != 1) {
        return;
    }

    CHECK_EQ(ch22_seen_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch22_seen_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch22_seen_slot_flags, 0);
    CHECK_EQ(ch22_seen_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch22_seen_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch22_seen_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch22_seen_slot_timers[i], 0);
    }
}

/* NOTHING RECOVERS THE LIVE RECORD BEFORE THE BANKING.  The party member is
   staged carrying flags 2, and on the live record that 2 is still there
   underneath the retired bit WIN21.DAT raised -- the byte reads back 3 -- and
   the live magic points are still the 2 the staging gave rather than the
   maximum a recovery would have restored.  A recovery sweep copied over from
   fdps_chapter_19_end would have cleared the byte and put both pools on their
   maxima before the writeback ever ran. */
static void chapter_22_recovers_nobody_before_banking(void)
{
    run_ch22_handler();
    CHECK_EQ(ch22_run_state, 1);
    if (ch22_run_state != 1) {
        return;
    }

    CHECK_EQ(ch22_seen_unit_flags,
             CH22_STAGED_PARTY_FLAGS | UNIT_FLAG_RETIRED);
    CHECK_EQ(ch22_seen_unit_mp_current, RANDIS_MP_CURRENT);
    CHECK_EQ(ch22_seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003b2ac is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from fdps_chapter_01_end would show as byte 0 reading 0x01. */
static void chapter_22_grants_no_spell(void)
{
    int i;

    run_ch22_handler();
    CHECK_EQ(ch22_run_state, 1);
    if (ch22_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch22_seen_unit_spells[i], 0);
        CHECK_EQ(ch22_seen_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win21.dat and it really ran: the marker
   value 71 is in status_timers[4] of the live battle record, where only that
   member's second opcode puts it.  The two decoys in the container write 79
   into status_timers[3] and 101 into status_timers[5], so a name one step in
   either direction is three failed assertions rather than a silent pass -- and
   both of those names are real members of the shipped container as well, so
   neither slip would show up as a missing member.  This is the half of the
   handler's deliberate off-by-one that carries the index of the chapter just
   ENDED, 21, against the 22 the store leaves.  The marker does not reach the
   roster copy, whose timers the writeback cleared before the script ran. */
static void chapter_22_victory_cutscene_is_win21_dat(void)
{
    run_ch22_handler();
    CHECK_EQ(ch22_run_state, 1);
    if (ch22_run_state != 1) {
        return;
    }

    CHECK_EQ(ch22_seen_unit_timers[WIN21_CH22_MARKER_SLOT],
             WIN21_CH22_MARKER_VALUE);
    CHECK_EQ(ch22_seen_unit_timers[WIN20_CH22_MARKER_SLOT], 0);
    CHECK_EQ(ch22_seen_unit_timers[WIN22_CH22_MARKER_SLOT], 0);
    CHECK_EQ(ch22_seen_slot_timers[WIN21_CH22_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The enemy the first
   call left at 0 HP is not a roster member and is not billed for either. */
static void chapter_22_revive_charges_nothing_when_nobody_fell(void)
{
    run_ch22_handler();
    CHECK_EQ(ch22_run_state, 1);
    if (ch22_run_state != 1) {
        return;
    }

    CHECK_EQ(ch22_seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 23's, 22, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 22.  It also pins the
   other half of the handler's deliberate off-by-one -- a 21 here, matching the
   21 in the script name, would be chapter 22 replayed rather than chapter 23
   started. */
static void chapter_22_advances_the_chapter_index_to_chapter_twenty_three(void)
{
    run_ch22_handler();
    CHECK_EQ(ch22_run_state, 1);
    if (ch22_run_state != 1) {
        return;
    }

    CHECK_EQ(ch22_seen_chapter_id, CH22_CHAPTER_ID_AFTER);
}

/* Chapter 22's fixture container goes again, so that nothing this file wrote
   outlives its run and the next file that wants that name finds it free. */
static void the_ch22_fixture_container_is_removed(void)
{
    if (!ch22_fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    ch22_fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

/* --------------------------------------------------------------------------
 * fdps_chapter_23_end at 0003b350.
 *
 * The family's four calls and one store, with a sweep of the map's THIRD SIDE
 * wedged between the first two -- and that sweep is the whole reason this half
 * stages a battle array of its own instead of the two-record one the plain
 * handlers share.
 *
 *   0003b35c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003b361  MOV dword ptr [EBP-0x8],0xb
 *   0003b36b  CMP EAX,dword ptr [0x00060150] / JL
 *   0003b381  CALL 0x0002d210          the record of unit [EBP-0x8]
 *   0003b38f  MOV AL,[EAX+0x6] / AND EAX,0xff / CMP EAX,0x1 / JNZ
 *   0003b39f  MOV byte ptr [EAX+0x5],0x1
 *   0003b3a5  CALL 0x00023980          the battle party is banked
 *   0003b3aa  MOV EAX,0x621c4 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win22.dat" is interpreted
 *   0003b3b8  CALL 0x00039e70          the fallen are revived
 *   0003b3bd  MOV dword ptr [0x00069cf4],0x17
 *
 * WHAT THE STAGED ARRAY IS SHAPED TO CATCH.  Four of the six records exist
 * only as probes of the loop's three edges, and each one would be rewritten by
 * a different plausible mistake:
 *
 *   unit 10 is side 1 and sits one index BELOW the start.  A sweep written
 *   from 0 rather than from 11 retires it.
 *
 *   unit 11 is side 1 and is the first index the sweep does reach.  It is the
 *   only record whose flags byte may change, and it is staged carrying two
 *   spare bits so that the WHOLE-BYTE store is separable from an OR: the byte
 *   must read back 1 and not 7.
 *
 *   unit 12 is side 2 and sits above the start.  A sweep that dropped the side
 *   test, or tested it for "not the enemy side", retires it.
 *
 *   unit 14 is side 1 and sits one index past data_fdps_map_unit_count.  A
 *   bound written as the array's capacity, or as the map's opening size,
 *   reaches it; the live count does not.
 *
 * HOW THE SWEEP'S PLACE IN THE ORDER IS PINNED DOWN.  Unit 11 carries a
 * character id that roster slot 1 also carries, so the writeback banks it.
 * fdps_roster_write_back_battle_units masks the banked flags byte to bit 0 and
 * then skips the restore-from-maximum whenever that bit survived (src/
 * roster.c), so with the sweep in front of it slot 1 arrives holding the
 * battle's 12 hit points, and with the sweep moved behind it -- or left out
 * altogether -- slot 1 arrives healed to 30.  That single number is the whole
 * ordering argument.
 *
 * WHY EVERY STAGED RECORD IS EITHER ALIVE OR ALREADY RETIRED.  The death pass
 * that ends fdps_battle_destroy_remaining_enemies collects units whose retired
 * bit is clear and whose hit points are 0 and plays them off the map, none of
 * which can run in a test image.  The enemy is staged with the bit already up
 * and everything else is staged with hit points to spare, so the pass finds an
 * empty list.  The third-side records are not at risk from the first call
 * either way: it only zeroes hit points on side 0.
 *
 * WHY NOBODY MAY BE LEFT AT 0 HP ON THE ROSTER, why the cut-scene is a fixture
 * rather than the shipped WIN22.DAT, and why the container refuses to
 * overwrite one that is already there, are the reasoning at the top of this
 * file, unchanged.
 * ------------------------------------------------------------------------ */

/* WIN22.DAT, the member chapter 23's handler names: operand 1, so
   status_timers[4], with a value neither decoy writes. */
#define WIN22_CH23_MARKER_OPERAND 1
#define WIN22_CH23_MARKER_SLOT 4
#define WIN22_CH23_MARKER_VALUE 73

/* WIN21.DAT, the member a handler that followed its own script number one step
   low would open.  It is also the member the sibling handler one chapter back
   really does name, so a body copied from fdps_chapter_22_end without changing
   the operand lands here. */
#define WIN21_CH23_MARKER_OPERAND 0
#define WIN21_CH23_MARKER_SLOT 3
#define WIN21_CH23_MARKER_VALUE 61

/* WIN23.DAT, the member a handler that named the chapter it hands ON to rather
   than the one just won would open -- the exact slip the store's 0x17 invites.
   It is a real member of the shipped container too, sitting at 0x621d0
   immediately behind the name this handler uses, so the slip would not
   announce itself as a missing member. */
#define WIN23_CH23_MARKER_OPERAND 2
#define WIN23_CH23_MARKER_SLOT 5
#define WIN23_CH23_MARKER_VALUE 67

/* The index chapter 23's handler must leave: chapter 24, 0-based, the literal
   of the store at 0003b3bd.

   The run starts from the same 4 chapter 16's does, which is none of the three
   numbers a mistake would leave behind: not the stored 23, not the 22 an
   off-by-one that followed the script name would leave, and not the 5 an
   increment would leave. */
#define CH23_CHAPTER_ID_AFTER 23

/* The battle array this half stages.  Fourteen records are in play and two
   more sit behind them: one is the past-the-count probe and the last is spare,
   so a walk that ran off the end lands somewhere this file can see. */
#define CH23_UNIT_COUNT 14
#define CH23_UNIT_CAPACITY 16

/* The roster block: two members, because the second is what makes the
   writeback's treatment of a swept unit visible. */
#define CH23_ROSTER_MEMBERS 2
#define CH23_ROSTER_CAPACITY 4
#define CH23_NPC_ROSTER_SLOT 1

/* The four probes and the enemy.  10 is the last index below the sweep's
   start, 11 the first index it reaches, 12 a player-side record above the
   start, 13 the enemy and 14 one past the live count. */
#define CH23_EARLY_NPC_UNIT 10
#define CH23_NPC_UNIT 11
#define CH23_PLAYER_SIDE_UNIT 12
#define CH23_ENEMY_UNIT 13
#define CH23_BEYOND_COUNT_UNIT 14

/* The side byte at record +6 that the sweep is looking for: 1, the third side,
   against 0 for the enemy and 2 for the player's. */
#define CH23_NPC_SIDE 1

/* The character ids the probes carry.  Only CH23_NPC_CHAR_ID is also a roster
   slot's, so it is the only one of them the writeback banks; the others are
   ids no slot holds, which keeps every roster assertion about the two members
   that are meant to move. */
#define CH23_NPC_CHAR_ID 37
#define CH23_EARLY_NPC_CHAR_ID 38
#define CH23_PLAYER_SIDE_CHAR_ID 40
#define CH23_BEYOND_COUNT_CHAR_ID 39
#define CH23_ENEMY_CHAR_ID 84

/* What the filler records between the probes carry, one per index so that a
   stray write can be told apart from a record that was always going to hold
   that value.  The range 50..65 misses both roster slots' ids. */
#define CH23_FILLER_CHAR_ID_BASE 50

/* What the third-side probes are staged carrying in their flags byte: bits 1
   and 2, which nothing in this handler's reach reads or writes.  It is what
   separates the handler's whole-byte store from an OR -- an OR leaves 7 --
   and what makes an untouched probe tell itself apart from a swept one.

   IT CANNOT BE THE 0x80 HAS-ACTED BIT, which is the obvious mark to stage.
   fdps_icon_script_run opens by calling fdps_units_clear_status_bit7, which
   ANDs 0x7f over the flags byte of every unit below data_fdps_map_unit_count
   (src/unit.c), so a 0x80 staged here is gone by the time the run is captured
   whatever the sweep did -- both probes would read 0 and the swept record
   would read 1 either way. */
#define CH23_STAGED_NPC_FLAGS 0x06

/* A mark the party member is staged carrying, above the bit 0 the writeback
   keeps: bit 1 of the flags byte at record +5.  It is what makes the absence
   of a recovery sweep of the kind fdps_chapter_19_end has visible, and it also
   catches a third-side sweep written from index 0. */
#define CH23_STAGED_PARTY_FLAGS 2

/* Unit 11's pools.  hp_current is well below hp_max on purpose: which of the
   two reaches roster slot 1 is the ordering assertion this half turns on. */
#define CH23_NPC_HP_CURRENT 12
#define CH23_NPC_HP_MAX 30
#define CH23_NPC_MP_CURRENT 1
#define CH23_NPC_MP_MAX 4
#define CH23_NPC_LEVEL 1

/* Hit points for the probes that only have to stay above zero, so that the
   death pass inside the first call finds an empty list. */
#define CH23_PROBE_HP 9
#define CH23_ENEMY_HP 50

/* WIN22.DAT: retire battle unit 0, write the marker into its status_timers[4],
   stop -- the same two opcodes the halves above use, so the order argument
   below is the same one. */
static unsigned char fixture_ch23_win22_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN22_CH23_MARKER_OPERAND, WIN22_CH23_MARKER_VALUE,
    0x00
};

/* WIN21.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_ch23_win21_dat[] = {
    0x12, RANDIS_UNIT, WIN21_CH23_MARKER_OPERAND, WIN21_CH23_MARKER_VALUE,
    0x00
};

/* WIN23.DAT: the decoy one step high, retiring nobody for the same reason. */
static unsigned char fixture_ch23_win23_dat[] = {
    0x12, RANDIS_UNIT, WIN23_CH23_MARKER_OPERAND, WIN23_CH23_MARKER_VALUE,
    0x00
};

static char *fixture_ch23_names[FIXTURE_MEMBERS] = {
    "WIN21.DAT", "WIN22.DAT", "WIN23.DAT"
};

static unsigned char *fixture_ch23_bytes[FIXTURE_MEMBERS] = {
    fixture_ch23_win21_dat, fixture_ch23_win22_dat, fixture_ch23_win23_dat
};

static int fixture_ch23_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_ch23_win21_dat), sizeof(fixture_ch23_win22_dat),
    sizeof(fixture_ch23_win23_dat)
};

static struct fdps_unit_record ch23_unit_image[CH23_UNIT_CAPACITY];
static struct fdps_unit_record ch23_roster_image[CH23_ROSTER_CAPACITY];
static unsigned char ch23_item_image[ITEM_TABLE_ROWS
                                     * sizeof(struct fdps_item_effect)];

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case says so. */
static int ch23_run_state = 0;

/* Whether this half created the container, and so whether it may remove it. */
static int ch23_fixture_owned = 0;

/* Everything chapter 23's cases assert, captured the instant it returned. */
static unsigned char ch23_seen_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch23_seen_unit_timers[STATUS_TIMER_COUNT];
static unsigned char ch23_seen_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch23_seen_slot_timers[STATUS_TIMER_COUNT];
static int ch23_seen_unit_flags;
static int ch23_seen_unit_hp_current;
static int ch23_seen_unit_mp_current;
static int ch23_seen_slot_char_id;
static int ch23_seen_slot_flags;
static int ch23_seen_slot_hp_current;
static int ch23_seen_slot_hp_max;
static int ch23_seen_slot_mp_current;
static int ch23_seen_slot_level;
static int ch23_seen_enemy_hp_current;
static int ch23_seen_enemy_hp_max;
static int ch23_seen_enemy_flags;
static int ch23_seen_early_npc_flags;
static int ch23_seen_npc_flags;
static int ch23_seen_npc_hp_current;
static int ch23_seen_player_side_flags;
static int ch23_seen_beyond_count_flags;
static int ch23_seen_npc_slot_char_id;
static int ch23_seen_npc_slot_flags;
static int ch23_seen_npc_slot_hp_current;
static int ch23_seen_npc_slot_hp_max;
static int ch23_seen_npc_slot_mp_current;
static int ch23_seen_chapter_id;
static int ch23_seen_party_gold;

/* Builds chapter 23's fixture container, or answers no, to the layout in
   resource_info/vfs.md and by the same refusal protocol as the halves above. */
static int stage_ch23_fixture_archive(void)
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
        write_name(fp, fixture_ch23_names[i]);
        write_dword(fp, (long) fixture_ch23_lengths[i]);
        write_dword(fp, (long) fixture_ch23_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_ch23_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture_ch23_bytes[i], 1, (size_t) fixture_ch23_lengths[i], fp);
    }
    fclose(fp);

    ch23_fixture_owned = 1;
    return 1;
}

/* The battle array, the roster block and the item table as they stand when
   chapter 23's battle has just been won: one live party member, four
   third-side and player-side probes around the sweep's three edges, and one
   already retired enemy. */
static void ch23_stage_globals(void)
{
    int unit_index;

    memset(ch23_unit_image, 0, sizeof(ch23_unit_image));
    memset(ch23_roster_image, ROSTER_FILLER, sizeof(ch23_roster_image));
    memset(ch23_item_image, 0, sizeof(ch23_item_image));

    /* Every record in the array is filled first, and none of them may be left
       as the zeroed block memset produced: a zeroed record is side 0 with 0
       hit points and a clear retired bit, which is exactly the shape the death
       pass inside fdps_battle_destroy_remaining_enemies collects and plays off
       the map, and that pass renders frames and cannot run in a test image.
       The filler slots are therefore the party's own side with hit points to
       spare, and they carry character ids no roster slot holds so that they
       take no part in the writeback. */
    for (unit_index = 0; unit_index < CH23_UNIT_CAPACITY; unit_index++) {
        ch23_unit_image[unit_index].char_id =
            (unsigned char) (CH23_FILLER_CHAR_ID_BASE + unit_index);
        ch23_unit_image[unit_index].side = PLAYER_SIDE;
        ch23_unit_image[unit_index].hp_current = CH23_PROBE_HP;
        ch23_unit_image[unit_index].hp_max = CH23_PROBE_HP;
    }

    ch23_unit_image[RANDIS_UNIT].char_id = RANDIS_CHAR_ID;
    ch23_unit_image[RANDIS_UNIT].flags = CH23_STAGED_PARTY_FLAGS;
    ch23_unit_image[RANDIS_UNIT].side = PLAYER_SIDE;
    ch23_unit_image[RANDIS_UNIT].level = RANDIS_LEVEL;
    ch23_unit_image[RANDIS_UNIT].clazz = RANDIS_CLASS;
    ch23_unit_image[RANDIS_UNIT].hp_current = RANDIS_HP_CURRENT;
    ch23_unit_image[RANDIS_UNIT].hp_max = RANDIS_HP_MAX;
    ch23_unit_image[RANDIS_UNIT].mp_current = RANDIS_MP_CURRENT;
    ch23_unit_image[RANDIS_UNIT].mp_max = RANDIS_MP_MAX;

    ch23_unit_image[CH23_EARLY_NPC_UNIT].char_id = CH23_EARLY_NPC_CHAR_ID;
    ch23_unit_image[CH23_EARLY_NPC_UNIT].side = CH23_NPC_SIDE;
    ch23_unit_image[CH23_EARLY_NPC_UNIT].flags = CH23_STAGED_NPC_FLAGS;
    ch23_unit_image[CH23_EARLY_NPC_UNIT].hp_current = CH23_PROBE_HP;
    ch23_unit_image[CH23_EARLY_NPC_UNIT].hp_max = CH23_PROBE_HP;

    ch23_unit_image[CH23_NPC_UNIT].char_id = CH23_NPC_CHAR_ID;
    ch23_unit_image[CH23_NPC_UNIT].side = CH23_NPC_SIDE;
    ch23_unit_image[CH23_NPC_UNIT].flags = CH23_STAGED_NPC_FLAGS;
    ch23_unit_image[CH23_NPC_UNIT].level = CH23_NPC_LEVEL;
    ch23_unit_image[CH23_NPC_UNIT].hp_current = CH23_NPC_HP_CURRENT;
    ch23_unit_image[CH23_NPC_UNIT].hp_max = CH23_NPC_HP_MAX;
    ch23_unit_image[CH23_NPC_UNIT].mp_current = CH23_NPC_MP_CURRENT;
    ch23_unit_image[CH23_NPC_UNIT].mp_max = CH23_NPC_MP_MAX;

    ch23_unit_image[CH23_PLAYER_SIDE_UNIT].char_id = CH23_PLAYER_SIDE_CHAR_ID;
    ch23_unit_image[CH23_PLAYER_SIDE_UNIT].side = PLAYER_SIDE;
    ch23_unit_image[CH23_PLAYER_SIDE_UNIT].flags = CH23_STAGED_NPC_FLAGS;
    ch23_unit_image[CH23_PLAYER_SIDE_UNIT].hp_current = CH23_PROBE_HP;
    ch23_unit_image[CH23_PLAYER_SIDE_UNIT].hp_max = CH23_PROBE_HP;

    ch23_unit_image[CH23_ENEMY_UNIT].char_id = CH23_ENEMY_CHAR_ID;
    ch23_unit_image[CH23_ENEMY_UNIT].side = ENEMY_SIDE;
    ch23_unit_image[CH23_ENEMY_UNIT].flags = UNIT_FLAG_RETIRED;
    ch23_unit_image[CH23_ENEMY_UNIT].hp_current = CH23_ENEMY_HP;
    ch23_unit_image[CH23_ENEMY_UNIT].hp_max = CH23_ENEMY_HP;

    ch23_unit_image[CH23_BEYOND_COUNT_UNIT].char_id =
        CH23_BEYOND_COUNT_CHAR_ID;
    ch23_unit_image[CH23_BEYOND_COUNT_UNIT].side = CH23_NPC_SIDE;
    ch23_unit_image[CH23_BEYOND_COUNT_UNIT].flags = CH23_STAGED_NPC_FLAGS;
    ch23_unit_image[CH23_BEYOND_COUNT_UNIT].hp_current = CH23_PROBE_HP;
    ch23_unit_image[CH23_BEYOND_COUNT_UNIT].hp_max = CH23_PROBE_HP;

    ch23_roster_image[RANDIS_ROSTER_SLOT].char_id = RANDIS_CHAR_ID;
    ch23_roster_image[CH23_NPC_ROSTER_SLOT].char_id = CH23_NPC_CHAR_ID;

    data_fdps_map_unit_array_ptr = (unsigned char *) ch23_unit_image;
    data_fdps_roster_array_ptr = (unsigned char *) ch23_roster_image;
    data_fdps_item_effect_table_ptr = ch23_item_image;
    data_fdps_map_unit_count = CH23_UNIT_COUNT;
    data_fdps_roster_member_count = CH23_ROSTER_MEMBERS;
    data_fdps_chapter_current_chapter_id = CHAPTER_ID_BEFORE;
    data_fdps_shared_party_total_gold = PARTY_GOLD_BEFORE;
}

static void ch23_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch23_seen_unit_spells[i] =
            ch23_unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch23_seen_slot_spells[i] =
            ch23_roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch23_seen_unit_timers[i] =
            ch23_unit_image[RANDIS_UNIT].status_timers[i];
        ch23_seen_slot_timers[i] =
            ch23_roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch23_seen_unit_flags = (int) ch23_unit_image[RANDIS_UNIT].flags;
    ch23_seen_unit_hp_current = (int) ch23_unit_image[RANDIS_UNIT].hp_current;
    ch23_seen_unit_mp_current = (int) ch23_unit_image[RANDIS_UNIT].mp_current;
    ch23_seen_slot_char_id =
        (int) ch23_roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch23_seen_slot_flags = (int) ch23_roster_image[RANDIS_ROSTER_SLOT].flags;
    ch23_seen_slot_hp_current =
        (int) ch23_roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch23_seen_slot_hp_max =
        (int) ch23_roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch23_seen_slot_mp_current =
        (int) ch23_roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch23_seen_slot_level = (int) ch23_roster_image[RANDIS_ROSTER_SLOT].level;
    ch23_seen_enemy_hp_current =
        (int) ch23_unit_image[CH23_ENEMY_UNIT].hp_current;
    ch23_seen_enemy_hp_max = (int) ch23_unit_image[CH23_ENEMY_UNIT].hp_max;
    ch23_seen_enemy_flags = (int) ch23_unit_image[CH23_ENEMY_UNIT].flags;
    ch23_seen_early_npc_flags =
        (int) ch23_unit_image[CH23_EARLY_NPC_UNIT].flags;
    ch23_seen_npc_flags = (int) ch23_unit_image[CH23_NPC_UNIT].flags;
    ch23_seen_npc_hp_current =
        (int) ch23_unit_image[CH23_NPC_UNIT].hp_current;
    ch23_seen_player_side_flags =
        (int) ch23_unit_image[CH23_PLAYER_SIDE_UNIT].flags;
    ch23_seen_beyond_count_flags =
        (int) ch23_unit_image[CH23_BEYOND_COUNT_UNIT].flags;
    ch23_seen_npc_slot_char_id =
        (int) ch23_roster_image[CH23_NPC_ROSTER_SLOT].char_id;
    ch23_seen_npc_slot_flags =
        (int) ch23_roster_image[CH23_NPC_ROSTER_SLOT].flags;
    ch23_seen_npc_slot_hp_current =
        (int) ch23_roster_image[CH23_NPC_ROSTER_SLOT].hp_current;
    ch23_seen_npc_slot_hp_max =
        (int) ch23_roster_image[CH23_NPC_ROSTER_SLOT].hp_max;
    ch23_seen_npc_slot_mp_current =
        (int) ch23_roster_image[CH23_NPC_ROSTER_SLOT].mp_current;
    ch23_seen_chapter_id = data_fdps_chapter_current_chapter_id;
    ch23_seen_party_gold = data_fdps_shared_party_total_gold;
}

/* Runs chapter 23's handler once, against freshly staged arrays and its own
   fixture cut-scene, and records what it left behind. */
static void run_ch23_handler(void)
{
    if (ch23_run_state != 0) {
        return;
    }
    ch23_run_state = 2;

    if (!stage_ch23_fixture_archive()) {
        return;
    }

    ch23_stage_globals();

    fdps_chapter_23_end();

    ch23_capture();
    ch23_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  The third-side record at unit 11 keeps its hit
   points too: 00039e10 tests for side 0 alone, so the handler's own sweep
   below is the only thing in this function that touches side 1. */
static void chapter_23_sweeps_the_enemy_side(void)
{
    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_enemy_hp_current, 0);
    CHECK_EQ(ch23_seen_enemy_hp_max, CH23_ENEMY_HP);
    CHECK_EQ(ch23_seen_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch23_seen_unit_hp_current, RANDIS_HP_CURRENT);
    CHECK_EQ(ch23_seen_npc_hp_current, CH23_NPC_HP_CURRENT);
}

/* The third side is retired, and retired by a WHOLE-BYTE STORE.  Unit 11 was
   staged holding bits 1 and 2 and reads back exactly 1: both of them are gone,
   which is what separates MOV byte ptr [EAX+0x5],0x1 at 0003b39f from the
   unit->flags |= 1 a reader would write by reflex.  An OR would leave 7, and
   nothing between the sweep and the capture would take those bits off again --
   the cut-scene's opening AND 0x7f reaches only bit 7. */
static void chapter_23_retires_the_third_side(void)
{
    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_npc_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch23_seen_npc_flags & CH23_STAGED_NPC_FLAGS, 0);
}

/* The sweep starts at unit 11 and not at unit 0.  Unit 10 is side 1 and sits
   one index below the literal 0xb the counter is seeded with at 0003b361, and
   it is left holding the 0x06 it was staged with.  A sweep written from 0
   rewrites its byte to 1, and a sweep written from any start below 11 that is
   still above 0 is caught by the same record. */
static void chapter_23_third_side_sweep_starts_at_unit_eleven(void)
{
    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_early_npc_flags, CH23_STAGED_NPC_FLAGS);
}

/* The sweep tests for side 1 exactly.  Unit 12 is side 2 and sits above the
   start, and it keeps the 0x06 it was staged with: the test at 0003b397 is
   CMP EAX,0x1 and not a test for "not the enemy side", which would retire the
   player's own units from index 11 up. */
static void chapter_23_third_side_sweep_skips_the_player_side(void)
{
    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_player_side_flags, CH23_STAGED_NPC_FLAGS);
}

/* The sweep's bound is data_fdps_map_unit_count, re-read every iteration at
   0003b36b.  Unit 14 is side 1 and sits one index past the live count of 14,
   and it keeps the 0x06 it was staged with: a bound written as the array's
   capacity, or as a literal taken from the map's opening size, would reach
   it. */
static void chapter_23_third_side_sweep_walks_the_live_unit_count(void)
{
    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_beyond_count_flags, CH23_STAGED_NPC_FLAGS);
}

/* THE SWEEP RUNS BEFORE THE WRITEBACK, and roster slot 1 is where that shows.
   Unit 11 carries the character id slot 1 holds, so the writeback banks it:
   the flags byte arrives masked to bit 0, which is 1, and because that bit
   survived the mask the restore-from-maximum is skipped, so the slot holds the
   battle's 12 hit points against a maximum of 30.  Move the sweep behind the
   writeback -- or leave it out -- and the same slot arrives healed to 30.  The
   magic points are restored either way, because that store sits after the join
   (src/roster.c). */
static void chapter_23_banks_the_swept_third_side_out_of_play(void)
{
    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_npc_slot_char_id, CH23_NPC_CHAR_ID);
    CHECK_EQ(ch23_seen_npc_slot_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch23_seen_npc_slot_hp_current, CH23_NPC_HP_CURRENT);
    CHECK_EQ(ch23_seen_npc_slot_hp_max, CH23_NPC_HP_MAX);
    CHECK_EQ(ch23_seen_npc_slot_mp_current, CH23_NPC_MP_MAX);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0 -- the staged 2 does not survive that mask -- its HP was lifted to
   the maximum by the full heal and its MP by the restore that follows.
   WIN22.DAT retires unit 0 and the writeback skips a retired character-0 unit,
   so a run that interpreted the script first would leave every one of these at
   the filler. */
static void chapter_23_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch23_seen_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch23_seen_slot_flags, 0);
    CHECK_EQ(ch23_seen_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch23_seen_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch23_seen_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch23_seen_slot_timers[i], 0);
    }
}

/* NOTHING RECOVERS THE PARTY'S LIVE RECORDS.  The handler's only unit-record
   store is the third-side one, so 蘭迪斯's live magic points are still the 2
   the staging gave rather than the maximum a recovery of the kind
   fdps_chapter_19_end has would have restored, and his live hit points are
   still the battle's 25.  His flags byte still carries the staged 2 as well,
   underneath the retired bit the cut-scene raised on him afterwards -- a
   recovery would have cleared the byte before the writeback ever ran. */
static void chapter_23_recovers_nobody_before_banking(void)
{
    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_unit_flags,
             CH23_STAGED_PARTY_FLAGS | UNIT_FLAG_RETIRED);
    CHECK_EQ(ch23_seen_unit_mp_current, RANDIS_MP_CURRENT);
    CHECK_EQ(ch23_seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003b35c is the enemy sweep -- so both the live record's bitmap and
   the roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from fdps_chapter_01_end would show as byte 0 reading 0x01. */
static void chapter_23_grants_no_spell(void)
{
    int i;

    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch23_seen_unit_spells[i], 0);
        CHECK_EQ(ch23_seen_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win22.dat and it really ran: the marker
   value 73 is in status_timers[4] of the live battle record, where only that
   member's second opcode puts it.  The two decoys in the container write 61
   into status_timers[3] and 67 into status_timers[5], so a name one step in
   either direction is three failed assertions rather than a silent pass -- and
   both of those names are real members of the shipped container as well, so
   neither slip would show up as a missing member.  This is the half of the
   handler's deliberate off-by-one that carries the index of the chapter just
   ENDED, 22, against the 23 the store leaves.  The marker does not reach the
   roster copy, whose timers the writeback cleared before the script ran. */
static void chapter_23_victory_cutscene_is_win22_dat(void)
{
    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_unit_timers[WIN22_CH23_MARKER_SLOT],
             WIN22_CH23_MARKER_VALUE);
    CHECK_EQ(ch23_seen_unit_timers[WIN21_CH23_MARKER_SLOT], 0);
    CHECK_EQ(ch23_seen_unit_timers[WIN23_CH23_MARKER_SLOT], 0);
    CHECK_EQ(ch23_seen_slot_timers[WIN22_CH23_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel.
   Both roster members are above 0 hit points when it runs: the writeback put
   slot 0 on its maximum, and slot 1 -- the swept third-side record, which the
   writeback deliberately did NOT heal -- still carries the 12 it came out of
   the battle with. */
static void chapter_23_revive_charges_nothing_when_nobody_fell(void)
{
    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 24's, 23, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 23.  It also pins the
   other half of the handler's deliberate off-by-one -- a 22 here, matching the
   22 in the script name, would be chapter 23 replayed rather than chapter 24
   started. */
static void chapter_23_advances_the_chapter_index_to_chapter_twenty_four(void)
{
    run_ch23_handler();
    CHECK_EQ(ch23_run_state, 1);
    if (ch23_run_state != 1) {
        return;
    }

    CHECK_EQ(ch23_seen_chapter_id, CH23_CHAPTER_ID_AFTER);
}

/* Chapter 23's fixture container goes again, so that nothing this file wrote
   outlives its run and the next file that wants that name finds it free. */
static void the_ch23_fixture_container_is_removed(void)
{
    if (!ch23_fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    ch23_fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

/* --------------------------------------------------------------------------
 * fdps_chapter_24_end at 0003b600.
 *
 * Chapter 19's conditional recovery in front of the family's FULL four calls
 * and one store -- so this half is the chapter 19 half again with the enemy
 * sweep put back, and the sweep lands after the recovery rather than first.
 *
 *   0003b60c  CMP byte ptr [0x000640e9],0x0 / JZ 0003b675
 *             element 0x11 of data_fdps_map_cell_event_triggered_flags, the
 *             duel latch: clear and the whole recovery is skipped
 *   0003b61f  CMP EAX,[0x00060150] / JL
 *             the recovery walks the LIVE unit count, not a literal
 *   0003b635  CALL 0x0002d210          the record is resolved
 *   0003b643  MOV AL,[EAX+0x8] / AND EAX,0xff / CMP EAX,0xb / JG 0003b673
 *             an UNSIGNED test on the character id
 *   0003b653  MOV byte ptr [EAX+0x5],0x0      the whole flags byte
 *   0003b65a  MOV DX,[EAX+0x42] / MOV [EAX+0x40],DX     hp_current = hp_max
 *   0003b668  MOV DX,[EAX+0x46] / MOV [EAX+0x44],DX     mp_current = mp_max
 *   0003b675  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003b67a  CALL 0x00023980          the battle party is banked
 *   0003b67f  MOV EAX,0x621d0 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win23.dat" is interpreted
 *   0003b68d  CALL 0x00039e70          the fallen are revived
 *   0003b692  MOV dword ptr [0x00069cf4],0x18
 *
 * TWO RUNS, latch up (A) and latch down (B), staged exactly as the chapter 19
 * half stages them and for the same reasons: the same six records, the same
 * 0x05 in every flags byte, every pool below its maximum and above 0.
 *
 *   unit 0   character 0, and the record the cut-scene retires
 *   unit 1   character 0x0b, the LAST id the test lets through
 *   unit 2   character 0x0c, the first it does not
 *   unit 3   character 0x80, which a SIGNED test would let through as -128
 *   unit 4   an enemy, character 0x1e, already retired, hit points left
 *   unit 5   character 0x03, sitting BEYOND data_fdps_map_unit_count
 *
 * THE ENEMY IS WHAT SEPARATES THIS HALF FROM CHAPTER 19'S.  There the enemy
 * had to keep its hit points; here the sweep at 0003b675 must zero them in
 * both runs.  It is staged already retired so the sweep's death pass finds an
 * empty list (src/death.c) -- the reason the top of this file gives.
 *
 * WHAT THIS HALF CANNOT WITNESS: the order of the recovery against the enemy
 * sweep.  It only shows on a side-0 record carrying a roster id, and that
 * record, un-retired and then zeroed, is exactly what sends the death pass
 * into its frame loop.  The assembly is the evidence for that order.
 * ------------------------------------------------------------------------ */

#define CH24_DUEL_LATCH_SLOT 0x11

/* WIN23.DAT is the member the handler names; WIN22.DAT is where a body copied
   from fdps_chapter_23_end without changing the operand would land, and
   WIN24.DAT is where a handler that named the chapter it hands ON to -- the
   0x18 of the store -- would land.  Both decoys share status_timers[5]. */
#define CH24_WIN23_MARKER_OPERAND 1
#define CH24_WIN23_MARKER_SLOT 4
#define CH24_WIN23_MARKER_VALUE 123
#define CH24_DECOY_MARKER_OPERAND 2
#define CH24_DECOY_MARKER_SLOT 5
#define CH24_WIN22_DECOY_VALUE 122
#define CH24_WIN24_DECOY_VALUE 124

#define CH24_FIXTURE_MEMBERS 3

/* The index chapter 24's handler must leave: chapter 25, 0-based, the literal
   of the store at 0003b692.  The run starts from CHAPTER_ID_BEFORE, 4. */
#define CH24_CHAPTER_ID_AFTER 24

#define CH24_UNIT_CAPACITY 8
#define CH24_UNIT_COUNT 5
#define CH24_ROSTER_CAPACITY 4

#define CH24_LAST_ROSTER_UNIT 1
#define CH24_LAST_ROSTER_CHAR_ID 0x0b
#define CH24_PAST_BOUND_UNIT 2
#define CH24_PAST_BOUND_CHAR_ID 0x0c
#define CH24_HIGH_ID_UNIT 3
#define CH24_HIGH_ID_CHAR_ID 0x80
#define CH24_ENEMY_UNIT_SLOT 4
#define CH24_ENEMY_UNIT_CHAR_ID 0x1e
#define CH24_BEYOND_COUNT_UNIT 5
#define CH24_BEYOND_COUNT_CHAR_ID 0x03

#define CH24_LAST_ROSTER_SLOT 1
#define CH24_ROSTER_MEMBERS 2

/* The retired bit plus 0x04, for the reason CH19_STAGED_FLAGS gives: 0x04
   tells a whole-byte store from an AND-NOT, and 0x80 would be masked off by
   the cut-scene interpreter whatever this handler did. */
#define CH24_STAGED_FLAGS 0x05

#define CH24_RANDIS_HP_CURRENT 25
#define CH24_RANDIS_HP_MAX 40
#define CH24_RANDIS_MP_CURRENT 2
#define CH24_RANDIS_MP_MAX 9
#define CH24_RANDIS_LEVEL 3
#define CH24_LAST_ROSTER_HP_CURRENT 10
#define CH24_LAST_ROSTER_HP_MAX 33
#define CH24_LAST_ROSTER_MP_CURRENT 1
#define CH24_LAST_ROSTER_MP_MAX 7
#define CH24_PAST_BOUND_HP_CURRENT 12
#define CH24_PAST_BOUND_HP_MAX 44
#define CH24_PAST_BOUND_MP_CURRENT 3
#define CH24_PAST_BOUND_MP_MAX 8
#define CH24_HIGH_ID_HP_CURRENT 14
#define CH24_HIGH_ID_HP_MAX 45
#define CH24_HIGH_ID_MP_CURRENT 4
#define CH24_HIGH_ID_MP_MAX 6
#define CH24_BEYOND_HP_CURRENT 5
#define CH24_BEYOND_HP_MAX 50
#define CH24_ENEMY_HP 44

static struct fdps_unit_record ch24_unit_image[CH24_UNIT_CAPACITY];
static struct fdps_unit_record ch24_roster_image[CH24_ROSTER_CAPACITY];
static unsigned char ch24_item_image[ITEM_TABLE_ROWS
                                     * sizeof(struct fdps_item_effect)];

/* WIN23.DAT: retire battle unit 0, write the marker into its
   status_timers[4], stop.  The retire is what shows the party was banked
   before the script ran, as in the chapter 19 half. */
static unsigned char fixture_win23_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, CH24_WIN23_MARKER_OPERAND, CH24_WIN23_MARKER_VALUE,
    0x00
};

static unsigned char fixture_ch24_win22_dat[] = {
    0x12, RANDIS_UNIT, CH24_DECOY_MARKER_OPERAND, CH24_WIN22_DECOY_VALUE,
    0x00
};

static unsigned char fixture_ch24_win24_dat[] = {
    0x12, RANDIS_UNIT, CH24_DECOY_MARKER_OPERAND, CH24_WIN24_DECOY_VALUE,
    0x00
};

static char *fixture_ch24_names[CH24_FIXTURE_MEMBERS] = {
    "WIN22.DAT", "WIN23.DAT", "WIN24.DAT"
};

static unsigned char *fixture_ch24_bytes[CH24_FIXTURE_MEMBERS] = {
    fixture_ch24_win22_dat, fixture_win23_dat, fixture_ch24_win24_dat
};

static int fixture_ch24_lengths[CH24_FIXTURE_MEMBERS] = {
    sizeof(fixture_ch24_win22_dat), sizeof(fixture_win23_dat),
    sizeof(fixture_ch24_win24_dat)
};

/* 0 not attempted, 1 built by this half and usable, 2 unavailable. */
static int ch24_fixture_state = 0;

/* What one run of the handler left behind. */
struct ch24_snapshot {
    unsigned char randis_timers[STATUS_TIMER_COUNT];
    int randis_flags;
    int last_roster_flags;
    int last_roster_hp_current;
    int last_roster_mp_current;
    int past_bound_flags;
    int past_bound_hp_current;
    int past_bound_mp_current;
    int high_id_flags;
    int high_id_hp_current;
    int high_id_mp_current;
    int beyond_flags;
    int beyond_hp_current;
    int enemy_flags;
    int enemy_hp_current;
    int slot_level;
    int slot_hp_current;
    int slot_mp_current;
    int last_slot_flags;
    int last_slot_hp_current;
    int chapter_id;
    int party_gold;
};

/* 0 not attempted, 1 the run happened and its snapshot is good, 2 the run
   could not be made and every case that reads it says so. */
static int ch24_duel_state = 0;
static int ch24_nolatch_state = 0;
static struct ch24_snapshot ch24_duel_seen;
static struct ch24_snapshot ch24_nolatch_seen;

/* Builds the container the two runs share, once, to the layout in
   resource_info/vfs.md and by the same refusal protocol as the halves above. */
static int ch24_fixture_available(void)
{
    FILE *fp;
    long member_at;
    int i;

    if (ch24_fixture_state != 0) {
        return ch24_fixture_state == 1;
    }
    ch24_fixture_state = 2;

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
    write_dword(fp, (long) CH24_FIXTURE_MEMBERS);
    fwrite("Dynasty Information Co.,", 1, VFS_SIGNATURE_BYTES, fp);

    member_at = (long) VFS_HEADER_BYTES
                + (long) CH24_FIXTURE_MEMBERS * VFS_ENTRY_BYTES;
    for (i = 0; i < CH24_FIXTURE_MEMBERS; i++) {
        write_name(fp, fixture_ch24_names[i]);
        write_dword(fp, (long) fixture_ch24_lengths[i]);
        write_dword(fp, (long) fixture_ch24_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_ch24_lengths[i];
    }
    for (i = 0; i < CH24_FIXTURE_MEMBERS; i++) {
        fwrite(fixture_ch24_bytes[i], 1, (size_t) fixture_ch24_lengths[i], fp);
    }
    fclose(fp);

    ch24_fixture_state = 1;
    return 1;
}

/* Stages one record of the battle array. */
static void ch24_stage_unit(int unit_index, int char_id, int side, int flags,
                            int hp_current, int hp_max, int mp_current,
                            int mp_max)
{
    struct fdps_unit_record *unit;

    unit = &ch24_unit_image[unit_index];
    unit->char_id = (unsigned char) char_id;
    unit->side = (unsigned char) side;
    unit->flags = (unsigned char) flags;
    unit->hp_current = (short) hp_current;
    unit->hp_max = (short) hp_max;
    unit->mp_current = (short) mp_current;
    unit->mp_max = (short) mp_max;
}

/* The battle array, the roster block and the item table as they stand when
   chapter 24's battle has just been won and the duel has been staged over it.
   latch says whether the chapter's duel was ever put to the player. */
static void ch24_stage_globals(int latch)
{
    memset(ch24_unit_image, 0, sizeof(ch24_unit_image));
    memset(ch24_roster_image, ROSTER_FILLER, sizeof(ch24_roster_image));
    memset(ch24_item_image, 0, sizeof(ch24_item_image));

    ch24_stage_unit(RANDIS_UNIT, RANDIS_CHAR_ID, PLAYER_SIDE,
                    CH24_STAGED_FLAGS, CH24_RANDIS_HP_CURRENT,
                    CH24_RANDIS_HP_MAX, CH24_RANDIS_MP_CURRENT,
                    CH24_RANDIS_MP_MAX);
    ch24_unit_image[RANDIS_UNIT].level = CH24_RANDIS_LEVEL;
    ch24_stage_unit(CH24_LAST_ROSTER_UNIT, CH24_LAST_ROSTER_CHAR_ID,
                    PLAYER_SIDE, CH24_STAGED_FLAGS,
                    CH24_LAST_ROSTER_HP_CURRENT, CH24_LAST_ROSTER_HP_MAX,
                    CH24_LAST_ROSTER_MP_CURRENT, CH24_LAST_ROSTER_MP_MAX);
    ch24_stage_unit(CH24_PAST_BOUND_UNIT, CH24_PAST_BOUND_CHAR_ID,
                    PLAYER_SIDE, CH24_STAGED_FLAGS,
                    CH24_PAST_BOUND_HP_CURRENT, CH24_PAST_BOUND_HP_MAX,
                    CH24_PAST_BOUND_MP_CURRENT, CH24_PAST_BOUND_MP_MAX);
    ch24_stage_unit(CH24_HIGH_ID_UNIT, CH24_HIGH_ID_CHAR_ID, PLAYER_SIDE,
                    CH24_STAGED_FLAGS, CH24_HIGH_ID_HP_CURRENT,
                    CH24_HIGH_ID_HP_MAX, CH24_HIGH_ID_MP_CURRENT,
                    CH24_HIGH_ID_MP_MAX);
    ch24_stage_unit(CH24_ENEMY_UNIT_SLOT, CH24_ENEMY_UNIT_CHAR_ID, ENEMY_SIDE,
                    UNIT_FLAG_RETIRED, CH24_ENEMY_HP, CH24_ENEMY_HP, 0, 0);
    ch24_stage_unit(CH24_BEYOND_COUNT_UNIT, CH24_BEYOND_COUNT_CHAR_ID,
                    PLAYER_SIDE, CH24_STAGED_FLAGS, CH24_BEYOND_HP_CURRENT,
                    CH24_BEYOND_HP_MAX, 0, 0);

    ch24_roster_image[RANDIS_ROSTER_SLOT].char_id = RANDIS_CHAR_ID;
    ch24_roster_image[CH24_LAST_ROSTER_SLOT].char_id =
        CH24_LAST_ROSTER_CHAR_ID;

    data_fdps_map_cell_event_triggered_flags[CH24_DUEL_LATCH_SLOT] =
        (unsigned char) latch;
    data_fdps_map_unit_array_ptr = (unsigned char *) ch24_unit_image;
    data_fdps_roster_array_ptr = (unsigned char *) ch24_roster_image;
    data_fdps_item_effect_table_ptr = ch24_item_image;
    data_fdps_map_unit_count = CH24_UNIT_COUNT;
    data_fdps_roster_member_count = CH24_ROSTER_MEMBERS;
    data_fdps_chapter_current_chapter_id = CHAPTER_ID_BEFORE;
    data_fdps_shared_party_total_gold = PARTY_GOLD_BEFORE;
}

static void ch24_capture(struct ch24_snapshot *seen)
{
    int i;
    struct fdps_unit_record *unit;

    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        seen->randis_timers[i] = ch24_unit_image[RANDIS_UNIT].status_timers[i];
    }
    seen->randis_flags = (int) ch24_unit_image[RANDIS_UNIT].flags;
    unit = &ch24_unit_image[CH24_LAST_ROSTER_UNIT];
    seen->last_roster_flags = (int) unit->flags;
    seen->last_roster_hp_current = (int) unit->hp_current;
    seen->last_roster_mp_current = (int) unit->mp_current;
    unit = &ch24_unit_image[CH24_PAST_BOUND_UNIT];
    seen->past_bound_flags = (int) unit->flags;
    seen->past_bound_hp_current = (int) unit->hp_current;
    seen->past_bound_mp_current = (int) unit->mp_current;
    unit = &ch24_unit_image[CH24_HIGH_ID_UNIT];
    seen->high_id_flags = (int) unit->flags;
    seen->high_id_hp_current = (int) unit->hp_current;
    seen->high_id_mp_current = (int) unit->mp_current;
    unit = &ch24_unit_image[CH24_BEYOND_COUNT_UNIT];
    seen->beyond_flags = (int) unit->flags;
    seen->beyond_hp_current = (int) unit->hp_current;
    unit = &ch24_unit_image[CH24_ENEMY_UNIT_SLOT];
    seen->enemy_flags = (int) unit->flags;
    seen->enemy_hp_current = (int) unit->hp_current;
    seen->slot_level = (int) ch24_roster_image[RANDIS_ROSTER_SLOT].level;
    seen->slot_hp_current =
        (int) ch24_roster_image[RANDIS_ROSTER_SLOT].hp_current;
    seen->slot_mp_current =
        (int) ch24_roster_image[RANDIS_ROSTER_SLOT].mp_current;
    seen->last_slot_flags =
        (int) ch24_roster_image[CH24_LAST_ROSTER_SLOT].flags;
    seen->last_slot_hp_current =
        (int) ch24_roster_image[CH24_LAST_ROSTER_SLOT].hp_current;
    seen->chapter_id = data_fdps_chapter_current_chapter_id;
    seen->party_gold = data_fdps_shared_party_total_gold;
}

/* Run A: the duel was put to the player, so the recovery must run. */
static void ch24_run_duel(void)
{
    if (ch24_duel_state != 0) {
        return;
    }
    ch24_duel_state = 2;

    if (!ch24_fixture_available()) {
        return;
    }

    ch24_stage_globals(1);
    fdps_chapter_24_end();
    ch24_capture(&ch24_duel_seen);
    ch24_duel_state = 1;
}

/* Run B: the duel was never offered, so the recovery must be skipped whole. */
static void ch24_run_no_latch(void)
{
    if (ch24_nolatch_state != 0) {
        return;
    }
    ch24_nolatch_state = 2;

    if (!ch24_fixture_available()) {
        return;
    }

    ch24_stage_globals(0);
    fdps_chapter_24_end();
    ch24_capture(&ch24_nolatch_seen);
    ch24_nolatch_state = 1;
}

/* The recovery reaches the last id the test lets through, 0x0b, and does all
   three of its stores there: MOV byte ptr [EAX+0x5],0x0 at 0003b653, then the
   two word copies from +0x42 and +0x46 at 0003b65a and 0003b668. */
static void chapter_24_recovers_the_party_when_the_duel_was_offered(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(ch24_duel_seen.last_roster_flags, 0);
    CHECK_EQ(ch24_duel_seen.last_roster_hp_current, CH24_LAST_ROSTER_HP_MAX);
    CHECK_EQ(ch24_duel_seen.last_roster_mp_current, CH24_LAST_ROSTER_MP_MAX);
}

/* The flags store is a whole byte: the 0x04 staged beside the retired bit has
   to be gone as well. */
static void chapter_24_recovery_clears_the_whole_flags_byte(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(CH24_STAGED_FLAGS & 0x04, 0x04);
    CHECK_EQ(ch24_duel_seen.last_roster_flags & 0x04, 0);
    CHECK_EQ(ch24_duel_seen.last_roster_flags & 0x01, 0);
}

/* The character-id test stops at 0x0b: CMP EAX,0xb / JG 0003b673. */
static void chapter_24_recovery_stops_after_character_id_eleven(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(ch24_duel_seen.past_bound_flags, CH24_STAGED_FLAGS);
    CHECK_EQ(ch24_duel_seen.past_bound_hp_current, CH24_PAST_BOUND_HP_CURRENT);
    CHECK_EQ(ch24_duel_seen.past_bound_mp_current, CH24_PAST_BOUND_MP_CURRENT);
}

/* The character id is read UNSIGNED: AND EAX,0xff at 0003b646, so 0x80 is 128
   and fails the test. */
static void chapter_24_recovery_reads_the_character_id_unsigned(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(ch24_duel_seen.high_id_flags, CH24_STAGED_FLAGS);
    CHECK_EQ(ch24_duel_seen.high_id_hp_current, CH24_HIGH_ID_HP_CURRENT);
    CHECK_EQ(ch24_duel_seen.high_id_mp_current, CH24_HIGH_ID_MP_CURRENT);
}

/* The bound is data_fdps_map_unit_count: CMP EAX,[0x00060150] / JL at
   0003b61f.  The record one past the count carries a passing id and must come
   out untouched. */
static void chapter_24_recovery_walks_the_live_unit_count(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(CH24_BEYOND_COUNT_UNIT >= CH24_UNIT_COUNT, 1);
    CHECK_EQ(ch24_duel_seen.beyond_flags, CH24_STAGED_FLAGS);
    CHECK_EQ(ch24_duel_seen.beyond_hp_current, CH24_BEYOND_HP_CURRENT);
}

/* The map IS swept, CALL 0x00039e10 at 0003b675: the enemy's hit points are
   zeroed, and its retired bit is left as staged.  This is the case that tells
   this handler from chapter 19's. */
static void chapter_24_sweeps_the_enemy_side(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(ch24_duel_seen.enemy_hp_current, 0);
    CHECK_EQ(ch24_duel_seen.enemy_flags, UNIT_FLAG_RETIRED);
}

/* The recovery runs BEFORE the writeback: the roster copy of character 0x0b
   comes out un-retired and on its maximum. */
static void chapter_24_recovers_the_party_before_banking_it(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(ch24_duel_seen.last_slot_flags, 0);
    CHECK_EQ(ch24_duel_seen.last_slot_hp_current, CH24_LAST_ROSTER_HP_MAX);
}

/* The party is banked BEFORE the cut-scene: WIN23.DAT retires unit 0 and the
   writeback would then refuse to bank character 0, leaving roster slot 0 at
   its filler. */
static void chapter_24_banks_the_party_before_the_cutscene_runs(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(ch24_duel_seen.slot_level, CH24_RANDIS_LEVEL);
    CHECK_EQ(ch24_duel_seen.slot_hp_current, CH24_RANDIS_HP_MAX);
    CHECK_EQ(ch24_duel_seen.slot_mp_current, CH24_RANDIS_MP_MAX);
    CHECK_EQ(ch24_duel_seen.randis_flags, UNIT_FLAG_RETIRED);
}

/* The member opened is WIN23.DAT and neither decoy: MOV EAX,0x621d0 at
   0003b67f. */
static void chapter_24_victory_cutscene_is_win23_dat(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(ch24_duel_seen.randis_timers[CH24_WIN23_MARKER_SLOT],
             CH24_WIN23_MARKER_VALUE);
    CHECK_EQ(ch24_duel_seen.randis_timers[CH24_DECOY_MARKER_SLOT], 0);
}

/* The revive sweep finds nobody at 0 hit points and charges nothing. */
static void chapter_24_revive_charges_nothing_when_nobody_fell(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(ch24_duel_seen.party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is advanced to 24, chapter 25, and not to the 23 the
   script name carries.  MOV dword ptr [0x00069cf4],0x18 at 0003b692. */
static void chapter_24_advances_the_chapter_index_to_chapter_twenty_five(void)
{
    ch24_run_duel();
    CHECK_EQ(ch24_duel_state, 1);
    CHECK_EQ(ch24_duel_seen.chapter_id, CH24_CHAPTER_ID_AFTER);
}

/* Run B.  With the latch down the whole recovery is jumped over: CMP byte ptr
   [0x000640e9],0x0 / JZ 0003b675 at 0003b60c. */
static void chapter_24_recovers_nobody_without_the_duel_latch(void)
{
    ch24_run_no_latch();
    CHECK_EQ(ch24_nolatch_state, 1);
    CHECK_EQ(ch24_nolatch_seen.last_roster_flags, CH24_STAGED_FLAGS);
    CHECK_EQ(ch24_nolatch_seen.last_roster_hp_current,
             CH24_LAST_ROSTER_HP_CURRENT);
    CHECK_EQ(ch24_nolatch_seen.last_roster_mp_current,
             CH24_LAST_ROSTER_MP_CURRENT);
}

/* And the writeback then keeps the retired bit and does not heal, which is why
   the recovery has to stand in front of it. */
static void chapter_24_banks_a_retired_party_without_the_latch(void)
{
    ch24_run_no_latch();
    CHECK_EQ(ch24_nolatch_state, 1);
    CHECK_EQ(ch24_nolatch_seen.last_slot_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch24_nolatch_seen.last_slot_hp_current,
             CH24_LAST_ROSTER_HP_CURRENT);
}

/* The JZ lands on the sweep, not past it: the four unconditional steps run
   either way -- the enemy is zeroed, the same cut-scene is opened and the same
   chapter index is left behind. */
static void chapter_24_still_closes_the_chapter_without_the_latch(void)
{
    ch24_run_no_latch();
    CHECK_EQ(ch24_nolatch_state, 1);
    CHECK_EQ(ch24_nolatch_seen.enemy_hp_current, 0);
    CHECK_EQ(ch24_nolatch_seen.randis_timers[CH24_WIN23_MARKER_SLOT],
             CH24_WIN23_MARKER_VALUE);
    CHECK_EQ(ch24_nolatch_seen.randis_timers[CH24_DECOY_MARKER_SLOT], 0);
    CHECK_EQ(ch24_nolatch_seen.chapter_id, CH24_CHAPTER_ID_AFTER);
    CHECK_EQ(ch24_nolatch_seen.party_gold, PARTY_GOLD_BEFORE);
}

/* The shared fixture container goes again. */
static void the_ch24_fixture_container_is_removed(void)
{
    if (ch24_fixture_state != 1) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    ch24_fixture_state = 0;
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
    RUN_TEST(chapter_18_trades_the_seal_for_the_hero_badge);
    RUN_TEST(chapter_18_banks_the_traded_bag_onto_the_roster);
    RUN_TEST(chapter_18_alt_victory_cutscene_is_win17_1_dat);
    RUN_TEST(chapter_18_sweeps_the_enemy_side);
    RUN_TEST(chapter_18_sweep_ignores_the_tenth_unit);
    RUN_TEST(chapter_18_advances_the_chapter_index_to_chapter_nineteen);
    RUN_TEST(chapter_18_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_18_a_promoted_ninth_unit_denies_the_trade);
    RUN_TEST(chapter_18_ordinary_cutscene_is_win17_dat_when_denied);
    RUN_TEST(chapter_18_still_closes_the_chapter_when_denied);
    RUN_TEST(chapter_18_grants_nothing_without_the_seal);
    RUN_TEST(chapter_18_ordinary_cutscene_runs_without_the_seal);
    RUN_TEST(the_ch18_fixture_container_is_removed);
    RUN_TEST(chapter_19_recovers_the_party_when_the_duel_was_offered);
    RUN_TEST(chapter_19_recovery_clears_the_whole_flags_byte);
    RUN_TEST(chapter_19_recovery_stops_after_character_id_eleven);
    RUN_TEST(chapter_19_recovery_reads_the_character_id_unsigned);
    RUN_TEST(chapter_19_recovery_walks_the_live_unit_count);
    RUN_TEST(chapter_19_does_not_sweep_the_enemy_side);
    RUN_TEST(chapter_19_recovers_the_party_before_banking_it);
    RUN_TEST(chapter_19_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_19_victory_cutscene_is_win18_dat);
    RUN_TEST(chapter_19_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_19_advances_the_chapter_index_to_chapter_twenty);
    RUN_TEST(chapter_19_recovers_nobody_without_the_duel_latch);
    RUN_TEST(chapter_19_banks_a_retired_party_without_the_latch);
    RUN_TEST(chapter_19_still_closes_the_chapter_without_the_latch);
    RUN_TEST(the_ch19_fixture_container_is_removed);
    RUN_TEST(chapter_20_sweeps_the_enemy_side);
    RUN_TEST(chapter_20_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_20_recovers_nobody_before_banking);
    RUN_TEST(chapter_20_grants_no_spell);
    RUN_TEST(chapter_20_victory_cutscene_is_win19_dat);
    RUN_TEST(chapter_20_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_20_advances_the_chapter_index_to_chapter_twenty_one);
    RUN_TEST(the_ch20_fixture_container_is_removed);
    RUN_TEST(chapter_21_sweeps_the_enemy_side);
    RUN_TEST(chapter_21_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_21_recovers_nobody_before_banking);
    RUN_TEST(chapter_21_grants_no_spell);
    RUN_TEST(chapter_21_victory_cutscene_is_win20_dat);
    RUN_TEST(chapter_21_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_21_advances_the_chapter_index_to_chapter_twenty_two);
    RUN_TEST(the_ch21_fixture_container_is_removed);
    RUN_TEST(chapter_22_sweeps_the_enemy_side);
    RUN_TEST(chapter_22_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_22_recovers_nobody_before_banking);
    RUN_TEST(chapter_22_grants_no_spell);
    RUN_TEST(chapter_22_victory_cutscene_is_win21_dat);
    RUN_TEST(chapter_22_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_22_advances_the_chapter_index_to_chapter_twenty_three);
    RUN_TEST(the_ch22_fixture_container_is_removed);
    RUN_TEST(chapter_23_sweeps_the_enemy_side);
    RUN_TEST(chapter_23_retires_the_third_side);
    RUN_TEST(chapter_23_third_side_sweep_starts_at_unit_eleven);
    RUN_TEST(chapter_23_third_side_sweep_skips_the_player_side);
    RUN_TEST(chapter_23_third_side_sweep_walks_the_live_unit_count);
    RUN_TEST(chapter_23_banks_the_swept_third_side_out_of_play);
    RUN_TEST(chapter_23_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_23_recovers_nobody_before_banking);
    RUN_TEST(chapter_23_grants_no_spell);
    RUN_TEST(chapter_23_victory_cutscene_is_win22_dat);
    RUN_TEST(chapter_23_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_23_advances_the_chapter_index_to_chapter_twenty_four);
    RUN_TEST(the_ch23_fixture_container_is_removed);
    RUN_TEST(chapter_24_recovers_the_party_when_the_duel_was_offered);
    RUN_TEST(chapter_24_recovery_clears_the_whole_flags_byte);
    RUN_TEST(chapter_24_recovery_stops_after_character_id_eleven);
    RUN_TEST(chapter_24_recovery_reads_the_character_id_unsigned);
    RUN_TEST(chapter_24_recovery_walks_the_live_unit_count);
    RUN_TEST(chapter_24_sweeps_the_enemy_side);
    RUN_TEST(chapter_24_recovers_the_party_before_banking_it);
    RUN_TEST(chapter_24_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_24_victory_cutscene_is_win23_dat);
    RUN_TEST(chapter_24_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_24_advances_the_chapter_index_to_chapter_twenty_five);
    RUN_TEST(chapter_24_recovers_nobody_without_the_duel_latch);
    RUN_TEST(chapter_24_banks_a_retired_party_without_the_latch);
    RUN_TEST(chapter_24_still_closes_the_chapter_without_the_latch);
    RUN_TEST(the_ch24_fixture_container_is_removed);
}
