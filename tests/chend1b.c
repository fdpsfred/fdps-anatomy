/* tests/chend1b.c -- cover for src/chend1b.c.
 *
 * WHAT THE HANDLER IS.  fdps_chapter_12_end at 0003aa30 is four calls and one
 * store with no branch anywhere in it, so nothing about it is worth testing in
 * pieces: what it decides is the ORDER of the four calls and the value of the
 * one store.  The file runs the whole handler once, for real, against staged
 * arrays and a fixture cut-scene, and the cases below assert against that
 * single run.
 *
 * Expected values come from the assembly at 0003aa30 and from the bodies the
 * four callees were emitted from, never from the emitted C of the handler:
 *
 *   0003aa3c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003aa41  CALL 0x00023980          the battle party is banked
 *   0003aa46  MOV EAX,0x62134 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win11.dat" is interpreted
 *   0003aa54  CALL 0x00039e70          the fallen are revived
 *   0003aa59  MOV dword ptr [0x00069cf4],0xc
 *
 * THE TWO NUMBERS DIFFER BY ONE, and the cases below pin both ends of that
 * separately: the member opened is WIN11.DAT, the index of the chapter that
 * has just been won, while the store leaves 12, the index of the chapter that
 * comes next.  A handler that wrote the same number twice would pass one of
 * the two assertions and fail the other.  The fixture container therefore also
 * holds WIN10.DAT and WIN12.DAT -- the members a slip in either direction
 * would open -- each writing a marker of its own, so a wrong name is a failed
 * assertion rather than a missing member and a silent pass.
 *
 * HOW THE ORDER IS PINNED DOWN RATHER THAN ASSUMED.  Each of the first three
 * steps leaves a mark the step after it would erase or miss:
 *
 *   The sweep zeroes the hit-point word of the staged enemy, which nothing
 *   later in the handler writes, so the enemy's 0 is the sweep's own work.
 *
 *   WIN11.DAT's first opcode RETIRES battle unit 0.  The writeback skips a
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
 * WIN11.DAT is chapter 12's victory cinematic: it plays CD audio and .saf
 * clips and draws chapter text through pointers a fresh test image has not
 * filled.  Running it asserts nothing about THIS function and faults on the
 * way, the same reason tests/icon.c, tests/chend1.c and tests/chinit1.c give
 * for staging their own container.  The one staged here is built to the layout
 * in resource_info/vfs.md and read by the game's own fdps_vfs_open and
 * fdps_vfs_load_file.
 *
 * The staging REFUSES TO OVERWRITE an IconAni.vfs that is already in the run
 * directory and removes its own again in the last case, which is the protocol
 * tests/icon.c, tests/chend1.c and tests/chinit1.c share for that name.
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
#include "chend1b.h"

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
   id 0, so the retire opcode in WIN11.DAT is what a wrong order would trip
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

/* WIN11.DAT, the member this handler names: operand 1, so status_timers[4],
   with a value neither decoy writes. */
#define WIN11_MARKER_OPERAND 1
#define WIN11_MARKER_SLOT 4
#define WIN11_MARKER_VALUE 41

/* WIN10.DAT, the member a handler that followed its own script number one step
   low would open, in a slot and with a value of its own. */
#define WIN10_MARKER_OPERAND 0
#define WIN10_MARKER_SLOT 3
#define WIN10_MARKER_VALUE 43

/* WIN12.DAT, the member a handler that named the chapter it hands ON to rather
   than the one just won would open -- the exact slip the store's 12 invites --
   in a third slot with a third value. */
#define WIN12_MARKER_OPERAND 2
#define WIN12_MARKER_SLOT 5
#define WIN12_MARKER_VALUE 47

/* The retired bit in the flags byte at record +5, which WIN11.DAT's first
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

/* The index the handler must leave: chapter 13, 0-based, the literal of the
   store at 0003aa59.

   The run starts from 4, which is none of the three numbers a mistake would
   leave behind: not the stored 12, not the 11 an off-by-one that followed the
   script name would leave, and not the 5 an increment would leave. */
#define CHAPTER_ID_BEFORE 4
#define CHAPTER_ID_AFTER 12

/* The purse the run starts with.  Nothing in the handler may spend from it:
   the revive charges its fee inside the sweep, and the sweep finds no fallen
   member. */
#define PARTY_GOLD_BEFORE 1234

static struct fdps_unit_record unit_image[UNIT_CAPACITY];
static struct fdps_unit_record roster_image[ROSTER_CAPACITY];
static unsigned char item_image[ITEM_TABLE_ROWS
                                * sizeof(struct fdps_item_effect)];

/* WIN11.DAT: retire battle unit 0, write the marker into its status_timers[4],
   stop.  Opcode numbers and operand counts are src/icon.c's ladder -- 0x0b
   takes a unit index, 0x12 takes a unit index, a timer slot measured from
   status_timers[3] and a value, and 0x00 falls into the arm that ends the
   script. */
static unsigned char fixture_win11_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN11_MARKER_OPERAND, WIN11_MARKER_VALUE,
    0x00
};

/* WIN10.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_win10_dat[] = {
    0x12, RANDIS_UNIT, WIN10_MARKER_OPERAND, WIN10_MARKER_VALUE,
    0x00
};

/* WIN12.DAT: the decoy one step high, retiring nobody for the same reason. */
static unsigned char fixture_win12_dat[] = {
    0x12, RANDIS_UNIT, WIN12_MARKER_OPERAND, WIN12_MARKER_VALUE,
    0x00
};

static char *fixture_names[FIXTURE_MEMBERS] = {
    "WIN10.DAT", "WIN11.DAT", "WIN12.DAT"
};

static unsigned char *fixture_bytes[FIXTURE_MEMBERS] = {
    fixture_win10_dat, fixture_win11_dat, fixture_win12_dat
};

static int fixture_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_win10_dat), sizeof(fixture_win11_dat),
    sizeof(fixture_win12_dat)
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
   chapter 12's battle has just been won: one live party member and one already
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

    fdps_chapter_12_end();

    capture();
    run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1 and 2 have, would leave the enemy at 50. */
static void chapter_12_sweeps_the_enemy_side(void)
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
   the restore that follows.  WIN11.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_12_banks_the_party_before_the_cutscene_runs(void)
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
   thing at 0003aa3c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from fdps_chapter_01_end would show as byte 0 reading 0x01. */
static void chapter_12_grants_no_spell(void)
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

/* The cut-scene the handler names is Win11.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 41 in status_timers[4].  The two decoys in the
   container write 43 into status_timers[3] and 47 into status_timers[5] and
   retire nobody, so a name one step in either direction is three failed
   assertions rather than a silent pass.  This is the half of the handler's
   deliberate off-by-one that carries the index of the chapter just ENDED, 11,
   against the 12 the store leaves.  The marker does not reach the roster copy,
   whose timers the writeback cleared before the script ran. */
static void chapter_12_victory_cutscene_is_win11_dat(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit_timers[WIN11_MARKER_SLOT], WIN11_MARKER_VALUE);
    CHECK_EQ(seen_unit_timers[WIN10_MARKER_SLOT], 0);
    CHECK_EQ(seen_unit_timers[WIN12_MARKER_SLOT], 0);
    CHECK_EQ(seen_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(seen_slot_timers[WIN11_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The enemy the first
   call left at 0 HP is not a roster member and is not billed for either. */
static void chapter_12_revive_charges_nothing_when_nobody_fell(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 13's, 12, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 12.  It also pins the
   other half of the handler's deliberate off-by-one -- an 11 here, matching
   the 11 in the script name, would be chapter 12 replayed rather than chapter
   13 started. */
static void chapter_12_advances_the_chapter_index_to_chapter_thirteen(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_chapter_id, CHAPTER_ID_AFTER);
}

/* The fixture container belongs to this file only while its run needs it:
   tests/icon.c, tests/chend1.c and tests/chinit1.c stage a container of the
   same name for their own fixtures and refuse to start if one is already
   standing. */
static void the_fixture_container_is_removed(void)
{
    if (!fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_13_end at 0003aa90.
 *
 * The same four calls and one store as chapter 12's handler, differing in
 * exactly two operands, so the cases below pin exactly those two operands plus
 * the order the four calls run in:
 *
 *   0003aa9c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003aaa1  CALL 0x00023980          the battle party is banked
 *   0003aaa6  MOV EAX,0x62140 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win12.dat" is interpreted
 *   0003aab4  CALL 0x00039e70          the fallen are revived
 *   0003aab9  MOV dword ptr [0x00069cf4],0xd
 *
 * THE OFF-BY-ONE IS PINNED AT BOTH ENDS the way chapter 12's is: the member
 * opened is WIN12.DAT, the index of the chapter just won, while the store
 * leaves 13, the index of the chapter that comes next.  The fixture container
 * therefore also holds WIN11.DAT and WIN13.DAT -- the members a slip in either
 * direction would open -- each writing a marker of its own into a different
 * status-timer slot, so a wrong name is a failed assertion rather than a
 * missing member and a silent pass.  The marker values differ from chapter
 * 12's fixture as well, so a snapshot taken from the wrong run is visible too.
 *
 * THE CONTAINER IS STAGED AGAIN, NOT SHARED.  Chapter 12's cases run first and
 * the case that removes their container runs between the two families, so this
 * half builds an IconAni.vfs of its own with its own three members.  The same
 * refusal protocol applies: a container already standing is left alone, and
 * then every case here fails on its run-state assertion rather than passing on
 * whatever that other container happened to hold.
 *
 * Everything else -- why the enemy is staged already retired, why nobody may
 * be left at 0 HP, why the cut-scene is a fixture rather than the shipped
 * WIN12.DAT -- is the reasoning at the top of this file, unchanged.
 * ------------------------------------------------------------------------ */

/* WIN12.DAT, the member chapter 13's handler names: operand 2, so
   status_timers[5], with a value none of the other members writes. */
#define WIN12_CH13_MARKER_OPERAND 2
#define WIN12_CH13_MARKER_SLOT 5
#define WIN12_CH13_MARKER_VALUE 53

/* WIN11.DAT, the member a handler that followed its own script number one step
   low would open, in a slot and with a value of its own. */
#define WIN11_CH13_MARKER_OPERAND 1
#define WIN11_CH13_MARKER_SLOT 4
#define WIN11_CH13_MARKER_VALUE 51

/* WIN13.DAT, the member a handler that named the chapter it hands ON to rather
   than the one just won would open -- the exact slip the store's 13 invites --
   in a third slot with a third value. */
#define WIN13_CH13_MARKER_OPERAND 0
#define WIN13_CH13_MARKER_SLOT 3
#define WIN13_CH13_MARKER_VALUE 59

/* The index chapter 13's handler must leave: chapter 14, 0-based, the literal
   of the store at 0003aab9.

   The run starts from the same 4 chapter 12's does, which is none of the three
   numbers a mistake would leave behind: not the stored 13, not the 12 an
   off-by-one that followed the script name would leave, and not the 5 an
   increment would leave. */
#define CH13_CHAPTER_ID_AFTER 13

/* WIN12.DAT: retire battle unit 0, write the marker into its status_timers[5],
   stop -- the same two opcodes chapter 12's real member uses, so the order
   argument below is the same one. */
static unsigned char fixture_ch13_win12_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN12_CH13_MARKER_OPERAND, WIN12_CH13_MARKER_VALUE,
    0x00
};

/* WIN11.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_ch13_win11_dat[] = {
    0x12, RANDIS_UNIT, WIN11_CH13_MARKER_OPERAND, WIN11_CH13_MARKER_VALUE,
    0x00
};

/* WIN13.DAT: the decoy one step high, retiring nobody for the same reason. */
static unsigned char fixture_ch13_win13_dat[] = {
    0x12, RANDIS_UNIT, WIN13_CH13_MARKER_OPERAND, WIN13_CH13_MARKER_VALUE,
    0x00
};

static char *fixture_ch13_names[FIXTURE_MEMBERS] = {
    "WIN11.DAT", "WIN12.DAT", "WIN13.DAT"
};

static unsigned char *fixture_ch13_bytes[FIXTURE_MEMBERS] = {
    fixture_ch13_win11_dat, fixture_ch13_win12_dat, fixture_ch13_win13_dat
};

static int fixture_ch13_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_ch13_win11_dat), sizeof(fixture_ch13_win12_dat),
    sizeof(fixture_ch13_win13_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case says so. */
static int ch13_run_state = 0;

/* Whether this half created the container, and so whether it may remove it. */
static int ch13_fixture_owned = 0;

/* Everything chapter 13's cases assert, captured the instant it returned. */
static unsigned char ch13_seen_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch13_seen_unit_timers[STATUS_TIMER_COUNT];
static unsigned char ch13_seen_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch13_seen_slot_timers[STATUS_TIMER_COUNT];
static int ch13_seen_unit_flags;
static int ch13_seen_unit_hp_current;
static int ch13_seen_slot_char_id;
static int ch13_seen_slot_flags;
static int ch13_seen_slot_hp_current;
static int ch13_seen_slot_hp_max;
static int ch13_seen_slot_mp_current;
static int ch13_seen_slot_level;
static int ch13_seen_enemy_hp_current;
static int ch13_seen_enemy_hp_max;
static int ch13_seen_enemy_flags;
static int ch13_seen_chapter_id;
static int ch13_seen_party_gold;

/* Builds chapter 13's fixture container, or answers no, to the layout in
   resource_info/vfs.md and by the same refusal protocol as the half above. */
static int stage_ch13_fixture_archive(void)
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
        write_name(fp, fixture_ch13_names[i]);
        write_dword(fp, (long) fixture_ch13_lengths[i]);
        write_dword(fp, (long) fixture_ch13_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_ch13_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture_ch13_bytes[i], 1, (size_t) fixture_ch13_lengths[i], fp);
    }
    fclose(fp);

    ch13_fixture_owned = 1;
    return 1;
}

static void ch13_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch13_seen_unit_spells[i] =
            unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch13_seen_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch13_seen_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch13_seen_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch13_seen_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch13_seen_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch13_seen_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch13_seen_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch13_seen_slot_hp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch13_seen_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch13_seen_slot_mp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch13_seen_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch13_seen_enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    ch13_seen_enemy_hp_max = (int) unit_image[ENEMY_UNIT].hp_max;
    ch13_seen_enemy_flags = (int) unit_image[ENEMY_UNIT].flags;
    ch13_seen_chapter_id = data_fdps_chapter_current_chapter_id;
    ch13_seen_party_gold = data_fdps_shared_party_total_gold;
}

/* Runs chapter 13's handler once, against freshly staged arrays and its own
   fixture cut-scene, and records what it left behind. */
static void run_ch13_handler(void)
{
    if (ch13_run_state != 0) {
        return;
    }
    ch13_run_state = 2;

    if (!stage_ch13_fixture_archive()) {
        return;
    }

    stage_globals();

    fdps_chapter_13_end();

    ch13_capture();
    ch13_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1 and 2 have, would leave the enemy at 50. */
static void chapter_13_sweeps_the_enemy_side(void)
{
    run_ch13_handler();
    CHECK_EQ(ch13_run_state, 1);
    if (ch13_run_state != 1) {
        return;
    }

    CHECK_EQ(ch13_seen_enemy_hp_current, 0);
    CHECK_EQ(ch13_seen_enemy_hp_max, ENEMY_HP_MAX);
    CHECK_EQ(ch13_seen_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch13_seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN12.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_13_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_ch13_handler();
    CHECK_EQ(ch13_run_state, 1);
    if (ch13_run_state != 1) {
        return;
    }

    CHECK_EQ(ch13_seen_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch13_seen_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch13_seen_slot_flags, 0);
    CHECK_EQ(ch13_seen_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch13_seen_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch13_seen_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch13_seen_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003aa9c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from fdps_chapter_01_end would show as byte 0 reading 0x01. */
static void chapter_13_grants_no_spell(void)
{
    int i;

    run_ch13_handler();
    CHECK_EQ(ch13_run_state, 1);
    if (ch13_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch13_seen_unit_spells[i], 0);
        CHECK_EQ(ch13_seen_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win12.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 53 in status_timers[5].  The two decoys in the
   container write 51 into status_timers[4] and 59 into status_timers[3] and
   retire nobody, so a name one step in either direction is three failed
   assertions rather than a silent pass.  This is the half of the handler's
   deliberate off-by-one that carries the index of the chapter just ENDED, 12,
   against the 13 the store leaves.  The marker does not reach the roster copy,
   whose timers the writeback cleared before the script ran. */
static void chapter_13_victory_cutscene_is_win12_dat(void)
{
    run_ch13_handler();
    CHECK_EQ(ch13_run_state, 1);
    if (ch13_run_state != 1) {
        return;
    }

    CHECK_EQ(ch13_seen_unit_timers[WIN12_CH13_MARKER_SLOT],
             WIN12_CH13_MARKER_VALUE);
    CHECK_EQ(ch13_seen_unit_timers[WIN11_CH13_MARKER_SLOT], 0);
    CHECK_EQ(ch13_seen_unit_timers[WIN13_CH13_MARKER_SLOT], 0);
    CHECK_EQ(ch13_seen_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch13_seen_slot_timers[WIN12_CH13_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The enemy the first
   call left at 0 HP is not a roster member and is not billed for either. */
static void chapter_13_revive_charges_nothing_when_nobody_fell(void)
{
    run_ch13_handler();
    CHECK_EQ(ch13_run_state, 1);
    if (ch13_run_state != 1) {
        return;
    }

    CHECK_EQ(ch13_seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 14's, 13, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 13.  It also pins the
   other half of the handler's deliberate off-by-one -- a 12 here, matching the
   12 in the script name, would be chapter 13 replayed rather than chapter 14
   started. */
static void chapter_13_advances_the_chapter_index_to_chapter_fourteen(void)
{
    run_ch13_handler();
    CHECK_EQ(ch13_run_state, 1);
    if (ch13_run_state != 1) {
        return;
    }

    CHECK_EQ(ch13_seen_chapter_id, CH13_CHAPTER_ID_AFTER);
}

/* Chapter 13's fixture container goes the same way chapter 12's did, so that
   nothing this file wrote outlives its run. */
static void the_ch13_fixture_container_is_removed(void)
{
    if (!ch13_fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    ch13_fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_14_end at 0003aaf0.
 *
 * The same four calls and one store as chapters 12's and 13's handlers,
 * differing from chapter 13's in exactly two operands, so the cases below pin
 * exactly those two operands plus the order the four calls run in:
 *
 *   0003aafc  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003ab01  CALL 0x00023980          the battle party is banked
 *   0003ab06  MOV EAX,0x6214c / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win13.dat" is interpreted
 *   0003ab14  CALL 0x00039e70          the fallen are revived
 *   0003ab19  MOV dword ptr [0x00069cf4],0xe
 *
 * THE OFF-BY-ONE IS PINNED AT BOTH ENDS the way the two halves above are: the
 * member opened is WIN13.DAT, the index of the chapter just won, while the
 * store leaves 14, the index of the chapter that comes next.  The fixture
 * container therefore also holds WIN12.DAT and WIN14.DAT -- the members a slip
 * in either direction would open -- each writing a marker of its own into a
 * different status-timer slot, so a wrong name is a failed assertion rather
 * than a missing member and a silent pass.  The marker values differ from both
 * earlier fixtures as well, so a snapshot taken from the wrong run is visible
 * too.
 *
 * THE CONTAINER IS STAGED AGAIN, NOT SHARED.  Chapter 13's cases run first and
 * the case that removes their container runs between the two families, so this
 * half builds an IconAni.vfs of its own with its own three members, under the
 * same refusal protocol: a container already standing is left alone, and then
 * every case here fails on its run-state assertion rather than passing on
 * whatever that other container happened to hold.
 *
 * Everything else -- why the enemy is staged already retired, why nobody may
 * be left at 0 HP, why the cut-scene is a fixture rather than the shipped
 * WIN13.DAT -- is the reasoning at the top of this file, unchanged.
 * ------------------------------------------------------------------------ */

/* WIN13.DAT, the member chapter 14's handler names: operand 1, so
   status_timers[4], with a value none of the other members writes. */
#define WIN13_CH14_MARKER_OPERAND 1
#define WIN13_CH14_MARKER_SLOT 4
#define WIN13_CH14_MARKER_VALUE 61

/* WIN12.DAT, the member a handler that followed its own script number one step
   low would open, in a slot and with a value of its own. */
#define WIN12_CH14_MARKER_OPERAND 0
#define WIN12_CH14_MARKER_SLOT 3
#define WIN12_CH14_MARKER_VALUE 67

/* WIN14.DAT, the member a handler that named the chapter it hands ON to rather
   than the one just won would open -- the exact slip the store's 14 invites --
   in a third slot with a third value. */
#define WIN14_CH14_MARKER_OPERAND 2
#define WIN14_CH14_MARKER_SLOT 5
#define WIN14_CH14_MARKER_VALUE 71

/* The index chapter 14's handler must leave: chapter 15, 0-based, the literal
   of the store at 0003ab19.

   The run starts from the same 4 the two halves above do, which is none of the
   three numbers a mistake would leave behind: not the stored 14, not the 13 an
   off-by-one that followed the script name would leave, and not the 5 an
   increment would leave. */
#define CH14_CHAPTER_ID_AFTER 14

/* WIN13.DAT: retire battle unit 0, write the marker into its status_timers[4],
   stop -- the same two opcodes the earlier real members use, so the order
   argument below is the same one. */
static unsigned char fixture_ch14_win13_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN13_CH14_MARKER_OPERAND, WIN13_CH14_MARKER_VALUE,
    0x00
};

/* WIN12.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_ch14_win12_dat[] = {
    0x12, RANDIS_UNIT, WIN12_CH14_MARKER_OPERAND, WIN12_CH14_MARKER_VALUE,
    0x00
};

/* WIN14.DAT: the decoy one step high, retiring nobody for the same reason. */
static unsigned char fixture_ch14_win14_dat[] = {
    0x12, RANDIS_UNIT, WIN14_CH14_MARKER_OPERAND, WIN14_CH14_MARKER_VALUE,
    0x00
};

static char *fixture_ch14_names[FIXTURE_MEMBERS] = {
    "WIN12.DAT", "WIN13.DAT", "WIN14.DAT"
};

static unsigned char *fixture_ch14_bytes[FIXTURE_MEMBERS] = {
    fixture_ch14_win12_dat, fixture_ch14_win13_dat, fixture_ch14_win14_dat
};

static int fixture_ch14_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_ch14_win12_dat), sizeof(fixture_ch14_win13_dat),
    sizeof(fixture_ch14_win14_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case says so. */
static int ch14_run_state = 0;

/* Whether this half created the container, and so whether it may remove it. */
static int ch14_fixture_owned = 0;

/* Everything chapter 14's cases assert, captured the instant it returned. */
static unsigned char ch14_seen_unit_spells[SPELL_BITMAP_BYTES];
static unsigned char ch14_seen_unit_timers[STATUS_TIMER_COUNT];
static unsigned char ch14_seen_slot_spells[SPELL_BITMAP_BYTES];
static unsigned char ch14_seen_slot_timers[STATUS_TIMER_COUNT];
static int ch14_seen_unit_flags;
static int ch14_seen_unit_hp_current;
static int ch14_seen_slot_char_id;
static int ch14_seen_slot_flags;
static int ch14_seen_slot_hp_current;
static int ch14_seen_slot_hp_max;
static int ch14_seen_slot_mp_current;
static int ch14_seen_slot_level;
static int ch14_seen_enemy_hp_current;
static int ch14_seen_enemy_hp_max;
static int ch14_seen_enemy_flags;
static int ch14_seen_chapter_id;
static int ch14_seen_party_gold;

/* Builds chapter 14's fixture container, or answers no, to the layout in
   resource_info/vfs.md and by the same refusal protocol as the halves
   above. */
static int stage_ch14_fixture_archive(void)
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
        write_name(fp, fixture_ch14_names[i]);
        write_dword(fp, (long) fixture_ch14_lengths[i]);
        write_dword(fp, (long) fixture_ch14_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture_ch14_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture_ch14_bytes[i], 1, (size_t) fixture_ch14_lengths[i], fp);
    }
    fclose(fp);

    ch14_fixture_owned = 1;
    return 1;
}

static void ch14_capture(void)
{
    int i;

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        ch14_seen_unit_spells[i] =
            unit_image[RANDIS_UNIT].spells_known_bitmap[i];
        ch14_seen_slot_spells[i] =
            roster_image[RANDIS_ROSTER_SLOT].spells_known_bitmap[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        ch14_seen_unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        ch14_seen_slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    ch14_seen_unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    ch14_seen_unit_hp_current = (int) unit_image[RANDIS_UNIT].hp_current;
    ch14_seen_slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    ch14_seen_slot_flags = (int) roster_image[RANDIS_ROSTER_SLOT].flags;
    ch14_seen_slot_hp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    ch14_seen_slot_hp_max = (int) roster_image[RANDIS_ROSTER_SLOT].hp_max;
    ch14_seen_slot_mp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].mp_current;
    ch14_seen_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    ch14_seen_enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    ch14_seen_enemy_hp_max = (int) unit_image[ENEMY_UNIT].hp_max;
    ch14_seen_enemy_flags = (int) unit_image[ENEMY_UNIT].flags;
    ch14_seen_chapter_id = data_fdps_chapter_current_chapter_id;
    ch14_seen_party_gold = data_fdps_shared_party_total_gold;
}

/* Runs chapter 14's handler once, against freshly staged arrays and its own
   fixture cut-scene, and records what it left behind. */
static void run_ch14_handler(void)
{
    if (ch14_run_state != 0) {
        return;
    }
    ch14_run_state = 2;

    if (!stage_ch14_fixture_archive()) {
        return;
    }

    stage_globals();

    fdps_chapter_14_end();

    ch14_capture();
    ch14_run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1 and 2 have, would leave the enemy at 50. */
static void chapter_14_sweeps_the_enemy_side(void)
{
    run_ch14_handler();
    CHECK_EQ(ch14_run_state, 1);
    if (ch14_run_state != 1) {
        return;
    }

    CHECK_EQ(ch14_seen_enemy_hp_current, 0);
    CHECK_EQ(ch14_seen_enemy_hp_max, ENEMY_HP_MAX);
    CHECK_EQ(ch14_seen_enemy_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch14_seen_unit_hp_current, RANDIS_HP_CURRENT);
}

/* The battle party is banked, and banked BEFORE the cut-scene: the slot that
   was 0xa5 filler carries the battle record's character id and level, its six
   status bytes were cleared by the writeback's memset, its flags were masked
   to bit 0, its HP was lifted to the maximum by the full heal and its MP by
   the restore that follows.  WIN13.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_14_banks_the_party_before_the_cutscene_runs(void)
{
    int i;

    run_ch14_handler();
    CHECK_EQ(ch14_run_state, 1);
    if (ch14_run_state != 1) {
        return;
    }

    CHECK_EQ(ch14_seen_slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(ch14_seen_slot_level, RANDIS_LEVEL);
    CHECK_EQ(ch14_seen_slot_flags, 0);
    CHECK_EQ(ch14_seen_slot_hp_max, RANDIS_HP_MAX);
    CHECK_EQ(ch14_seen_slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(ch14_seen_slot_mp_current, RANDIS_MP_MAX);
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        CHECK_EQ(ch14_seen_slot_timers[i], 0);
    }
}

/* No spell is granted.  The handler has no fdps_set_flag_bit call -- the first
   thing at 0003aafc is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from fdps_chapter_01_end would show as byte 0 reading 0x01. */
static void chapter_14_grants_no_spell(void)
{
    int i;

    run_ch14_handler();
    CHECK_EQ(ch14_run_state, 1);
    if (ch14_run_state != 1) {
        return;
    }

    for (i = 0; i < SPELL_BITMAP_BYTES; i++) {
        CHECK_EQ(ch14_seen_unit_spells[i], 0);
        CHECK_EQ(ch14_seen_slot_spells[i], 0);
    }
}

/* The cut-scene the handler names is Win13.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 61 in status_timers[4].  The two decoys in the
   container write 67 into status_timers[3] and 71 into status_timers[5] and
   retire nobody, so a name one step in either direction is three failed
   assertions rather than a silent pass.  This is the half of the handler's
   deliberate off-by-one that carries the index of the chapter just ENDED, 13,
   against the 14 the store leaves.  The marker does not reach the roster copy,
   whose timers the writeback cleared before the script ran. */
static void chapter_14_victory_cutscene_is_win13_dat(void)
{
    run_ch14_handler();
    CHECK_EQ(ch14_run_state, 1);
    if (ch14_run_state != 1) {
        return;
    }

    CHECK_EQ(ch14_seen_unit_timers[WIN13_CH14_MARKER_SLOT],
             WIN13_CH14_MARKER_VALUE);
    CHECK_EQ(ch14_seen_unit_timers[WIN12_CH14_MARKER_SLOT], 0);
    CHECK_EQ(ch14_seen_unit_timers[WIN14_CH14_MARKER_SLOT], 0);
    CHECK_EQ(ch14_seen_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(ch14_seen_slot_timers[WIN13_CH14_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The enemy the first
   call left at 0 HP is not a roster member and is not billed for either. */
static void chapter_14_revive_charges_nothing_when_nobody_fell(void)
{
    run_ch14_handler();
    CHECK_EQ(ch14_run_state, 1);
    if (ch14_run_state != 1) {
        return;
    }

    CHECK_EQ(ch14_seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 15's, 14, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 14.  It also pins the
   other half of the handler's deliberate off-by-one -- a 13 here, matching the
   13 in the script name, would be chapter 14 replayed rather than chapter 15
   started. */
static void chapter_14_advances_the_chapter_index_to_chapter_fifteen(void)
{
    run_ch14_handler();
    CHECK_EQ(ch14_run_state, 1);
    if (ch14_run_state != 1) {
        return;
    }

    CHECK_EQ(ch14_seen_chapter_id, CH14_CHAPTER_ID_AFTER);
}

/* Chapter 14's fixture container goes the same way the earlier ones did, so
   that nothing this file wrote outlives its run. */
static void the_ch14_fixture_container_is_removed(void)
{
    if (!ch14_fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    ch14_fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

void run_chend1b_tests(void)
{
    RUN_TEST(chapter_12_sweeps_the_enemy_side);
    RUN_TEST(chapter_12_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_12_grants_no_spell);
    RUN_TEST(chapter_12_victory_cutscene_is_win11_dat);
    RUN_TEST(chapter_12_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_12_advances_the_chapter_index_to_chapter_thirteen);
    RUN_TEST(the_fixture_container_is_removed);
    RUN_TEST(chapter_13_sweeps_the_enemy_side);
    RUN_TEST(chapter_13_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_13_grants_no_spell);
    RUN_TEST(chapter_13_victory_cutscene_is_win12_dat);
    RUN_TEST(chapter_13_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_13_advances_the_chapter_index_to_chapter_fourteen);
    RUN_TEST(the_ch13_fixture_container_is_removed);
    RUN_TEST(chapter_14_sweeps_the_enemy_side);
    RUN_TEST(chapter_14_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_14_grants_no_spell);
    RUN_TEST(chapter_14_victory_cutscene_is_win13_dat);
    RUN_TEST(chapter_14_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_14_advances_the_chapter_index_to_chapter_fifteen);
    RUN_TEST(the_ch14_fixture_container_is_removed);
}
