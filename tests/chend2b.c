/* tests/chend2b.c -- cover for src/chend2b.c.
 *
 * WHAT THE HANDLER IS.  fdps_chapter_25_end at 0003b720 is four calls and one
 * store with no branch anywhere in it, so what it decides is the ORDER of the
 * four calls and the value of the one store.  The file runs the whole handler
 * once, for real, against staged arrays and a fixture cut-scene, and the cases
 * below assert against that single run.  It is the same approach, and the same
 * staging, as the chapter 16 cover in tests/chend2.c, whose body chapter 25's
 * repeats instruction for instruction with its own two operands.
 *
 * Expected values come from the assembly at 0003b720 and from the bodies the
 * four callees were emitted from, never from the emitted C of the handler:
 *
 *   0003b72c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003b731  CALL 0x00023980          the battle party is banked
 *   0003b736  MOV EAX,0x621dc / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win24.dat" is interpreted
 *   0003b744  CALL 0x00039e70          the fallen are revived
 *   0003b749  MOV dword ptr [0x00069cf4],0x19
 *
 * THE TWO NUMBERS DIFFER BY ONE, and the cases below pin both ends of that
 * separately: the member opened is WIN24.DAT, the index of the chapter that
 * has just been won, while the store leaves 25, the index of the chapter that
 * comes next.  The fixture container therefore also holds WIN23.DAT and
 * WIN25.DAT -- the members a slip in either direction would open -- each
 * writing a marker of its own into a different status-timer slot, so a wrong
 * name is a failed assertion rather than a missing member and a silent pass.
 * WIN25.DAT is also the real member for chapter 26's handler, which is
 * covered further down, and WIN26.DAT is its high-side decoy.  WINGA26.DAT,
 * the fifth member, is chapter 27's hidden-route scene.  WIN27.DAT and
 * WIN28.DAT are chapter 28's scene and its high-side decoy (WIN26.DAT
 * doubles as its low-side one).  WIN28.DAT is in turn chapter 29's scene,
 * with WIN27.DAT as its low-side decoy and WIN29.DAT, the last member, as its
 * high-side one; chapter 29 is covered last.
 *
 * HOW THE ORDER IS PINNED DOWN RATHER THAN ASSUMED.  Each of the first three
 * steps leaves a mark the step after it would erase or miss:
 *
 *   The sweep zeroes the hit-point word of the staged enemy, which nothing
 *   later in the handler writes, so the enemy's 0 is the sweep's own work.
 *
 *   WIN24.DAT's first opcode RETIRES battle unit 0.  The writeback skips a
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
 * one in a test image, so a case that left a member at 0 HP would never return.
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
 * WIN24.DAT is chapter 25's victory cinematic: it plays CD audio and .saf
 * clips and draws chapter text through pointers a fresh test image has not
 * filled.  The container staged here is built to the layout in
 * resource_info/vfs.md and read by the game's own fdps_vfs_open and
 * fdps_vfs_load_file.  The staging REFUSES TO OVERWRITE an IconAni.vfs that is
 * already in the run directory and removes its own again in the last case,
 * the protocol tests/icon.c and the other chapter-end covers share.
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
#include "chend2b.h"

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
#define FIXTURE_MEMBERS 8

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
   id 0, so the retire opcode in WIN24.DAT is what a wrong order would trip
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

/* WIN24.DAT, the member chapter 25's handler names: operand 1, so
   status_timers[4], with a value neither decoy writes. */
#define WIN24_MARKER_OPERAND 1
#define WIN24_MARKER_SLOT 4
#define WIN24_MARKER_VALUE 83

/* WIN23.DAT, the member a handler that followed its own script number one step
   low would open, in a slot and with a value of its own.  It is also the
   member the sibling handler one chapter back really does name, so a body
   copied from fdps_chapter_24_end without changing the operand lands here. */
#define WIN23_MARKER_OPERAND 0
#define WIN23_MARKER_SLOT 3
#define WIN23_MARKER_VALUE 89

/* WIN25.DAT, the member a handler that named the chapter it hands ON to rather
   than the one just won would open -- the exact slip the store's 0x19 invites
   -- in a third slot with a third value. */
#define WIN25_MARKER_OPERAND 2
#define WIN25_MARKER_SLOT 5
#define WIN25_MARKER_VALUE 97

/* WIN26.DAT's marker: status_timers[3], the slot WIN23.DAT also writes, with
   a value of its own.  Both members are wrong for both handlers, so a
   non-zero status_timers[3] is a failure either way. */
#define WIN26_MARKER_OPERAND 0
#define WIN26_MARKER_SLOT 3
#define WIN26_MARKER_VALUE 71

/* The retired bit in the flags byte at record +5, which WIN24.DAT's first
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

/* The index the handler must leave: chapter 26, 0-based, the literal of the
   store at 0003b749.

   The run starts from 4, which is none of the three numbers a mistake would
   leave behind: not the stored 25, not the 24 an off-by-one that followed the
   script name would leave, and not the 5 an increment would leave. */
#define CHAPTER_ID_BEFORE 4
#define CHAPTER_ID_AFTER 25

/* The purse the run starts with.  Nothing in the handler may spend from it:
   the revive charges its fee inside the sweep, and the sweep finds no fallen
   member. */
#define PARTY_GOLD_BEFORE 1234

static struct fdps_unit_record unit_image[UNIT_CAPACITY];
static struct fdps_unit_record roster_image[ROSTER_CAPACITY];
static unsigned char item_image[ITEM_TABLE_ROWS
                                * sizeof(struct fdps_item_effect)];

/* WIN24.DAT: retire battle unit 0, write the marker into its status_timers[4],
   stop.  Opcode numbers and operand counts are src/icon.c's ladder -- 0x0b
   takes a unit index, 0x12 takes a unit index, a timer slot measured from
   status_timers[3] and a value, and 0x00 falls into the arm that ends the
   script. */
static unsigned char fixture_win24_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN24_MARKER_OPERAND, WIN24_MARKER_VALUE,
    0x00
};

/* WIN23.DAT: the decoy one step low.  It retires nobody, so a run that opened
   it is two failed assertions rather than one. */
static unsigned char fixture_win23_dat[] = {
    0x12, RANDIS_UNIT, WIN23_MARKER_OPERAND, WIN23_MARKER_VALUE,
    0x00
};

/* WIN25.DAT: the decoy one step high, retiring nobody for the same reason. */
static unsigned char fixture_win25_dat[] = {
    0x12, RANDIS_UNIT, WIN25_MARKER_OPERAND, WIN25_MARKER_VALUE,
    0x00
};

/* WIN26.DAT: the right member for neither handler here.  It is what a
   chapter 26 handler that named the chapter it hands ON to would open, and it
   writes a value of its own into status_timers[3]. */
static unsigned char fixture_win26_dat[] = {
    0x12, RANDIS_UNIT, WIN26_MARKER_OPERAND, WIN26_MARKER_VALUE,
    0x00
};

/* WINGA26.DAT, chapter 27's hidden-route scene: a marker of its own in
   status_timers[4], a value none of the other four members writes.  It
   retires nobody, so the marker is its only trace. */
#define WINGA26_MARKER_OPERAND 1
#define WINGA26_MARKER_SLOT 4
#define WINGA26_MARKER_VALUE 61

static unsigned char fixture_winga26_dat[] = {
    0x12, RANDIS_UNIT, WINGA26_MARKER_OPERAND, WINGA26_MARKER_VALUE,
    0x00
};

/* WIN27.DAT, chapter 28's scene: like WIN24.DAT it RETIRES battle unit 0
   first, so a handler that interpreted it before banking the party would
   leave the roster slot at its filler, and then writes a marker of its own
   into status_timers[4]. */
#define WIN27_MARKER_OPERAND 1
#define WIN27_MARKER_SLOT 4
#define WIN27_MARKER_VALUE 47

static unsigned char fixture_win27_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN27_MARKER_OPERAND, WIN27_MARKER_VALUE,
    0x00
};

/* WIN28.DAT: two roles.  For chapter 28 it is what a handler that named the
   chapter it hands ON to would open.  For chapter 29 it is the real scene,
   so like WIN24.DAT and WIN27.DAT it RETIRES battle unit 0 first -- the
   order witness for that handler's writeback -- and then writes a value of
   its own into status_timers[5]. */
#define WIN28_MARKER_OPERAND 2
#define WIN28_MARKER_SLOT 5
#define WIN28_MARKER_VALUE 53

static unsigned char fixture_win28_dat[] = {
    0x0b, RANDIS_UNIT,
    0x12, RANDIS_UNIT, WIN28_MARKER_OPERAND, WIN28_MARKER_VALUE,
    0x00
};

/* WIN29.DAT: what a chapter 29 handler that named the chapter it hands ON to
   would open.  status_timers[3], a value none of the other members writes,
   and it retires nobody. */
#define WIN29_MARKER_OPERAND 0
#define WIN29_MARKER_SLOT 3
#define WIN29_MARKER_VALUE 37

static unsigned char fixture_win29_dat[] = {
    0x12, RANDIS_UNIT, WIN29_MARKER_OPERAND, WIN29_MARKER_VALUE,
    0x00
};

static char *fixture_names[FIXTURE_MEMBERS] = {
    "WIN23.DAT", "WIN24.DAT", "WIN25.DAT", "WIN26.DAT", "WINGA26.DAT",
    "WIN27.DAT", "WIN28.DAT", "WIN29.DAT"
};

static unsigned char *fixture_bytes[FIXTURE_MEMBERS] = {
    fixture_win23_dat, fixture_win24_dat, fixture_win25_dat,
    fixture_win26_dat, fixture_winga26_dat, fixture_win27_dat,
    fixture_win28_dat, fixture_win29_dat
};

static int fixture_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_win23_dat), sizeof(fixture_win24_dat),
    sizeof(fixture_win25_dat), sizeof(fixture_win26_dat),
    sizeof(fixture_winga26_dat), sizeof(fixture_win27_dat),
    sizeof(fixture_win28_dat), sizeof(fixture_win29_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case says so. */
static int run_state = 0;

/* Whether this file created the container, and so whether it may remove it. */
static int fixture_owned = 0;

/* 0 not attempted, 1 staged by this file, 2 could not be staged. */
static int fixture_state = 0;

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

/* Stages the container once for every run in this file; each later caller
   gets the first answer. */
static int ensure_fixture(void)
{
    if (fixture_state == 0) {
        fixture_state = stage_fixture_archive() ? 1 : 2;
    }
    return fixture_state == 1;
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
   chapter 25's battle has just been won: one live party member and one already
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

    if (!ensure_fixture()) {
        return;
    }

    stage_globals();

    fdps_chapter_25_end();

    capture();
    run_state = 1;
}

/* The map is swept first, and swept the way 00039e10 sweeps it: the enemy
   unit's hit-point word is 0 where the staging left 50, its maximum in the
   word behind it is untouched -- the store is MOV word ptr [EAX+0x40],0x0 and
   not a dword -- and the player unit, whose side byte is 2, keeps the hit
   points the staging gave it.  A handler that omitted the call, the shape
   chapters 1, 2 and 15 have, would leave the enemy at 50.  In chapter 25 the
   sweep is load-bearing rather than belt and braces: the chapter is won by
   three named warlords leaving the field, so the rest of the enemy side is
   still standing when the handler runs. */
static void chapter_25_sweeps_the_enemy_side(void)
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
   the restore that follows.  WIN24.DAT retires unit 0 and the writeback skips
   a retired character-0 unit, so a run that interpreted the script first would
   leave every one of these at the filler. */
static void chapter_25_banks_the_party_before_the_cutscene_runs(void)
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
   thing at 0003b72c is the sweep -- so both the live record's bitmap and the
   roster's copy of it stay at the zeroes the staging left.  A grant copied
   over from fdps_chapter_01_end would show as byte 0 reading 0x01. */
static void chapter_25_grants_no_spell(void)
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

/* The cut-scene the handler names is Win24.dat and it really ran: both of the
   fixture member's opcodes are on the live battle record -- the retired bit at
   +5 and the marker value 83 in status_timers[4].  The two decoys in the
   container write 89 into status_timers[3] and 97 into status_timers[5] and
   retire nobody, so a name one step in either direction is three failed
   assertions rather than a silent pass.  This is the half of the handler's
   deliberate off-by-one that carries the index of the chapter just ENDED, 24,
   against the 25 the store leaves.  The marker does not reach the roster copy,
   whose timers the writeback cleared before the script ran. */
static void chapter_25_victory_cutscene_is_win24_dat(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit_timers[WIN24_MARKER_SLOT], WIN24_MARKER_VALUE);
    CHECK_EQ(seen_unit_timers[WIN23_MARKER_SLOT], 0);
    CHECK_EQ(seen_unit_timers[WIN25_MARKER_SLOT], 0);
    CHECK_EQ(seen_unit_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(seen_slot_timers[WIN24_MARKER_SLOT], 0);
}

/* Nobody fell, so the revive sweep charges nothing and never opens its panel:
   the writeback ran first and put the one roster member on his maximum, which
   leaves the sweep with no member at 0 HP to bill for.  The enemy the first
   call left at 0 HP is not a roster member and is not billed for either. */
static void chapter_25_revive_charges_nothing_when_nobody_fell(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_party_gold, PARTY_GOLD_BEFORE);
}

/* The chapter index is left at chapter 26's, 25, as an assignment and not as a
   step from what was there: this run starts it at 4, so an increment would
   read back 5 and the store's own literal reads back 25.  It also pins the
   other half of the handler's deliberate off-by-one -- a 24 here, matching the
   24 in the script name, would be chapter 25 replayed rather than chapter 26
   started. */
static void chapter_25_advances_the_chapter_index_to_chapter_twenty_six(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_chapter_id, CHAPTER_ID_AFTER);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_26_end at 0003b800.
 *
 * The handler is chapter 25's four closing steps and store behind a gift with
 * two gates, so the cases below run it three times, once per way through the
 * gates, and assert every run against the assembly:
 *
 *   0003b80c  PUSH 0xa2 / PUSH 0x0 / CALL 0x00034520   search for 真炎龍劍
 *   0003b81e  CMP [EBP-0x4],-0x1 / JNZ -> 0003b833 JMP 0003b864
 *   0003b824  PUSH 0x0 / CALL 0x00025240 / CMP EAX,0x8 / JNZ 0003b835
 *   0003b835  seven pushes / CALL 0x0001ff60            text entry 0x18
 *   0003b858  PUSH 0x62 / PUSH 0x0 / CALL 0x00025d20    炎龍劍 into the bag
 *   0003b864  sweep, writeback, "Win25.dat", revive
 *   0003b881  MOV dword ptr [0x00069cf4],0x1a
 *
 * Runs:
 *   GIFT     one carried item that is not 真炎龍劍 and seven empty entries:
 *            both gates pass, 炎龍劍 lands in entry 1.
 *   HAS_TRUE 真炎龍劍 carried in entry 0 and seven empty entries: the first
 *            gate fails with room to spare, so an add here is the search gate
 *            missing, not the count gate.
 *   FULL     eight carried items, none of them 真炎龍劍: the second gate fails
 *            and the bag is unchanged.
 *
 * WHETHER THE LINE IS DRAWN IS NOT ASSERTED.  fdps_draw_text takes its whole
 * effect through pixels at the VGA aperture; the staged text block gives every
 * entry one lone terminator so the draw walks it and paints nothing, the
 * protocol the chapter event covers use.  The FULL run therefore pins only
 * that the count gate refuses the add and that its jump lands on the closing
 * steps rather than past them.
 *
 * The gift is asserted on the ROSTER copy as well as on the live record: the
 * writeback copies the record whole, so 炎龍劍 on the roster is the gift having
 * run before the writeback.  Every run also asserts the closing steps -- the
 * enemy swept, Win25.dat interpreted and neither neighbour, nobody billed and
 * the index left at 26 -- because the two skip paths share them with the gift
 * path.
 * ------------------------------------------------------------------------ */

#define CH26_GIFT_RUN 0
#define CH26_HAS_TRUE_RUN 1
#define CH26_FULL_RUN 2
#define CH26_RUNS 3

/* The inventory entry encoding (src/unititem.c): flag byte then item id; an
   empty entry is 0x80 / 0xff, a carried, unequipped one has flag 0 -- the
   flag fdps_unit_add_item stores. */
#define CH26_INVENTORY_ENTRIES 8
#define CH26_EMPTY_FLAG 0x80
#define CH26_EMPTY_ID 0xff
#define CH26_CARRIED_FLAG 0x00

/* assets/items.md: 0x62 炎龍劍, 0xa2 真炎龍劍, and 0xa3 an item that is
   neither, so the search has something to walk past. */
#define CH26_DRAGON_SWORD 0x62
#define CH26_TRUE_DRAGON_SWORD 0xa2
#define CH26_OTHER_ITEM 0xa3

/* The entry the gift lands in on the GIFT run: the add takes the first entry
   flagged empty, and entry 0 is carried. */
#define CH26_GIFT_ENTRY 1

/* The index chapter 26's handler leaves: 0x1a at 0003b881, chapter 27. */
#define CH26_CHAPTER_ID_AFTER 26

/* A text block whose every entry up to 0x18 names one lone terminator. */
#define CH26_TEXT_IDS 0x19
#define CH26_TEXT_EMPTY_AT 0x40
#define CH26_TEXT_BLOCK_BYTES (CH26_TEXT_EMPTY_AT + 2)
#define CH26_TEXT_END (-1)

struct ch26_snapshot {
    int state;
    unsigned char unit_bag[CH26_INVENTORY_ENTRIES * 2];
    unsigned char slot_bag[CH26_INVENTORY_ENTRIES * 2];
    unsigned char unit_timers[STATUS_TIMER_COUNT];
    int unit_flags;
    int enemy_hp_current;
    int chapter_id;
    int party_gold;
};

static struct ch26_snapshot ch26_seen[CH26_RUNS];
static unsigned char ch26_text_block[CH26_TEXT_BLOCK_BYTES];

static void ch26_stage_text(void)
{
    int text_id;

    memset(ch26_text_block, 0, sizeof(ch26_text_block));
    *(short *) (ch26_text_block + CH26_TEXT_EMPTY_AT) = (short) CH26_TEXT_END;
    for (text_id = 0; text_id < CH26_TEXT_IDS; text_id++) {
        *(short *) (ch26_text_block + text_id * 2) =
            (short) CH26_TEXT_EMPTY_AT;
    }
    data_fdps_current_chapter_text_ptr = ch26_text_block;
}

static void ch26_set_entry(int entry, int flag, int item_id)
{
    unit_image[RANDIS_UNIT].inventory_slots[entry * 2] = (unsigned char) flag;
    unit_image[RANDIS_UNIT].inventory_slots[entry * 2 + 1] =
        (unsigned char) item_id;
}

static void ch26_stage_bag(int run)
{
    int entry;

    for (entry = 0; entry < CH26_INVENTORY_ENTRIES; entry++) {
        ch26_set_entry(entry, CH26_EMPTY_FLAG, CH26_EMPTY_ID);
    }
    if (run == CH26_GIFT_RUN) {
        ch26_set_entry(0, CH26_CARRIED_FLAG, CH26_OTHER_ITEM);
    } else if (run == CH26_HAS_TRUE_RUN) {
        ch26_set_entry(0, CH26_CARRIED_FLAG, CH26_TRUE_DRAGON_SWORD);
    } else {
        for (entry = 0; entry < CH26_INVENTORY_ENTRIES; entry++) {
            ch26_set_entry(entry, CH26_CARRIED_FLAG, CH26_OTHER_ITEM);
        }
    }
}

/* Runs chapter 26's handler once for the given bag and records what it left
   behind; a second call for the same run returns the first answer. */
static struct ch26_snapshot *ch26_run(int run)
{
    struct ch26_snapshot *seen;
    int i;

    seen = &ch26_seen[run];
    if (seen->state != 0) {
        return seen;
    }
    seen->state = 2;

    if (!ensure_fixture()) {
        return seen;
    }

    stage_globals();
    ch26_stage_text();
    ch26_stage_bag(run);

    fdps_chapter_26_end();

    for (i = 0; i < CH26_INVENTORY_ENTRIES * 2; i++) {
        seen->unit_bag[i] = unit_image[RANDIS_UNIT].inventory_slots[i];
        seen->slot_bag[i] =
            roster_image[RANDIS_ROSTER_SLOT].inventory_slots[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        seen->unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
    }
    seen->unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    seen->enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    seen->chapter_id = data_fdps_chapter_current_chapter_id;
    seen->party_gold = data_fdps_shared_party_total_gold;
    seen->state = 1;
    return seen;
}

/* The closing steps every run shares: the enemy swept, WIN25.DAT's marker on
   the live record and neither neighbour's (WIN24.DAT would also have retired
   unit 0), nobody billed, the index at 26 and not the 25 the script name
   would suggest. */
static void ch26_check_closing_steps(struct ch26_snapshot *seen)
{
    CHECK_EQ(seen->enemy_hp_current, 0);
    CHECK_EQ(seen->unit_timers[WIN25_MARKER_SLOT], WIN25_MARKER_VALUE);
    CHECK_EQ(seen->unit_timers[WIN24_MARKER_SLOT], 0);
    CHECK_EQ(seen->unit_timers[WIN26_MARKER_SLOT], 0);
    CHECK_EQ(seen->unit_flags, 0);
    CHECK_EQ(seen->party_gold, PARTY_GOLD_BEFORE);
    CHECK_EQ(seen->chapter_id, CH26_CHAPTER_ID_AFTER);
}

/* Both gates pass: 炎龍劍 goes into entry 1, the first empty one, carried
   and unequipped, on the live record AND on the roster copy -- the latter
   only because the gift runs before the writeback.  Entry 0 keeps its item
   and entry 2 stays empty. */
static void chapter_26_gives_the_dragon_sword_when_randis_lacks_the_true_one(void)
{
    struct ch26_snapshot *seen;

    seen = ch26_run(CH26_GIFT_RUN);
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->unit_bag[CH26_GIFT_ENTRY * 2], CH26_CARRIED_FLAG);
    CHECK_EQ(seen->unit_bag[CH26_GIFT_ENTRY * 2 + 1], CH26_DRAGON_SWORD);
    CHECK_EQ(seen->slot_bag[CH26_GIFT_ENTRY * 2], CH26_CARRIED_FLAG);
    CHECK_EQ(seen->slot_bag[CH26_GIFT_ENTRY * 2 + 1], CH26_DRAGON_SWORD);
    CHECK_EQ(seen->unit_bag[1], CH26_OTHER_ITEM);
    CHECK_EQ(seen->unit_bag[(CH26_GIFT_ENTRY + 1) * 2], CH26_EMPTY_FLAG);
    ch26_check_closing_steps(seen);
}

/* 真炎龍劍 carried, seven entries free: the search gate alone refuses the
   gift, so every entry behind it is still empty on both copies -- no 炎龍劍
   in entry 1 -- and the closing steps still run. */
static void chapter_26_gives_nothing_when_randis_carries_the_true_sword(void)
{
    struct ch26_snapshot *seen;
    int entry;

    seen = ch26_run(CH26_HAS_TRUE_RUN);
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->unit_bag[0], CH26_CARRIED_FLAG);
    CHECK_EQ(seen->unit_bag[1], CH26_TRUE_DRAGON_SWORD);
    for (entry = 1; entry < CH26_INVENTORY_ENTRIES; entry++) {
        CHECK_EQ(seen->unit_bag[entry * 2], CH26_EMPTY_FLAG);
        CHECK_EQ(seen->unit_bag[entry * 2 + 1], CH26_EMPTY_ID);
        CHECK_EQ(seen->slot_bag[entry * 2], CH26_EMPTY_FLAG);
    }
    ch26_check_closing_steps(seen);
}

/* A full bag without 真炎龍劍: the count gate refuses, no entry on either
   copy holds 炎龍劍, and the jump lands on the closing steps rather than past
   them. */
static void chapter_26_gives_nothing_when_the_bag_is_full(void)
{
    struct ch26_snapshot *seen;
    int entry;

    seen = ch26_run(CH26_FULL_RUN);
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    for (entry = 0; entry < CH26_INVENTORY_ENTRIES; entry++) {
        CHECK_EQ(seen->unit_bag[entry * 2], CH26_CARRIED_FLAG);
        CHECK_EQ(seen->unit_bag[entry * 2 + 1], CH26_OTHER_ITEM);
        CHECK_EQ(seen->slot_bag[entry * 2 + 1], CH26_OTHER_ITEM);
    }
    ch26_check_closing_steps(seen);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_27_end at 0003b8f0.
 *
 *   0003b8fc  CALL 0x00039e10                         sweep
 *   0003b901  PUSH 0xb3 / PUSH 0x0 / CALL 0x00034520  -> [EBP-0x8]
 *   0003b913  PUSH 0xdc / PUSH 0x3 / CALL 0x00034520  -> [EBP-0x4]
 *   0003b925  either == -1 -> ending route at 0003b96e
 *   0003b933  remove (0, [EBP-0x8]), remove (3, [EBP-0x4]),
 *             "WinGA26.dat", CALL 0x00039e70, [0x00069cf4] = 0x1b
 *   0003b96e  "Win26.dat", CALL 0x0001ba40, byte [0x000643eb] = 1
 *
 * ONLY THE HIDDEN ROUTE IS RUN.  The ending route calls
 * fdps_play_ending_credit_roll, which reads the blend tables from disk
 * without checking the handle and then plays the whole character gallery and
 * the End movie through the VGA aperture and the CD; none of that can run in a
 * test image, so the three runs that would take the ending route -- either
 * item missing, or both -- are not made here.  What this file pins down for
 * the hidden route is that both removals, the scene, the revive and the store
 * happened, with the right units and the right items.
 *
 * THE BAGS ARE BUILT SO A WRONG UNIT OR A WRONG ITEM SHOWS.  蘭迪斯 carries
 * [0xa3, 魔精石碎片, 反禁制器] and 法蓮娜 carries [反禁制器, 魔精石碎片], so a
 * handler that searched each unit for the other's item, or swapped the unit
 * indices, removes a different entry and leaves a different bag behind than
 * the one asserted:
 *
 *   right   蘭迪斯 [0xa3, 反禁制器, empty]   法蓮娜 [魔精石碎片, empty]
 *
 * Units 1 and 2 stand between them so index 3 is really the fourth record.
 * ------------------------------------------------------------------------ */

#define CH27_FARENA_UNIT 3
#define CH27_UNIT_COUNT 4

#define CH27_MANA_SHARD 0xb3
#define CH27_SEAL_BREAKER 0xdc

/* The index the hidden route leaves: 0x1b at 0003b962, chapter 28. */
#define CH27_CHAPTER_ID_AFTER 27

/* The hit points given to the filler party units 2 and 3.  Not 0, because the
   death pass at the end of the sweep collects every unretired unit at 0 HP
   whatever its side, and its animation cannot run in a test image. */
#define CH27_FILLER_HP 30

struct ch27_snapshot {
    int state;
    unsigned char randis_bag[CH26_INVENTORY_ENTRIES * 2];
    unsigned char farena_bag[CH26_INVENTORY_ENTRIES * 2];
    unsigned char unit_timers[STATUS_TIMER_COUNT];
    int unit_flags;
    int enemy_hp_current;
    int roster_slot_level;
    int chapter_id;
    int quit_requested;
    int party_gold;
};

static struct ch27_snapshot ch27_seen;

static void ch27_set_entry(int unit_index, int entry, int flag, int item_id)
{
    unit_image[unit_index].inventory_slots[entry * 2] = (unsigned char) flag;
    unit_image[unit_index].inventory_slots[entry * 2 + 1] =
        (unsigned char) item_id;
}

static void ch27_stage(void)
{
    int entry;

    unit_image[2].side = PLAYER_SIDE;
    unit_image[2].char_id = ENEMY_CHAR_ID + 1;
    unit_image[2].hp_current = CH27_FILLER_HP;
    unit_image[CH27_FARENA_UNIT].side = PLAYER_SIDE;
    unit_image[CH27_FARENA_UNIT].char_id = ENEMY_CHAR_ID + 2;
    unit_image[CH27_FARENA_UNIT].hp_current = CH27_FILLER_HP;
    data_fdps_map_unit_count = CH27_UNIT_COUNT;

    for (entry = 0; entry < CH26_INVENTORY_ENTRIES; entry++) {
        ch27_set_entry(RANDIS_UNIT, entry, CH26_EMPTY_FLAG, CH26_EMPTY_ID);
        ch27_set_entry(CH27_FARENA_UNIT, entry, CH26_EMPTY_FLAG,
                       CH26_EMPTY_ID);
    }
    ch27_set_entry(RANDIS_UNIT, 0, CH26_CARRIED_FLAG, CH26_OTHER_ITEM);
    ch27_set_entry(RANDIS_UNIT, 1, CH26_CARRIED_FLAG, CH27_MANA_SHARD);
    ch27_set_entry(RANDIS_UNIT, 2, CH26_CARRIED_FLAG, CH27_SEAL_BREAKER);
    ch27_set_entry(CH27_FARENA_UNIT, 0, CH26_CARRIED_FLAG, CH27_SEAL_BREAKER);
    ch27_set_entry(CH27_FARENA_UNIT, 1, CH26_CARRIED_FLAG, CH27_MANA_SHARD);

    data_fdps_shared_quit_game_requested = 0;
}

/* Runs chapter 27's handler once, down the hidden route, and records what it
   left behind. */
static struct ch27_snapshot *ch27_run(void)
{
    struct ch27_snapshot *seen;
    int i;

    seen = &ch27_seen;
    if (seen->state != 0) {
        return seen;
    }
    seen->state = 2;

    if (!ensure_fixture()) {
        return seen;
    }

    stage_globals();
    ch27_stage();

    fdps_chapter_27_end();

    for (i = 0; i < CH26_INVENTORY_ENTRIES * 2; i++) {
        seen->randis_bag[i] = unit_image[RANDIS_UNIT].inventory_slots[i];
        seen->farena_bag[i] = unit_image[CH27_FARENA_UNIT].inventory_slots[i];
    }
    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        seen->unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
    }
    seen->unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    seen->enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    seen->roster_slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    seen->chapter_id = data_fdps_chapter_current_chapter_id;
    seen->quit_requested = (int) data_fdps_shared_quit_game_requested;
    seen->party_gold = data_fdps_shared_party_total_gold;
    seen->state = 1;
    return seen;
}

/* The sweep runs on the hidden route too: the enemy's hit points are 0. */
static void chapter_27_sweeps_the_enemy_side(void)
{
    struct ch27_snapshot *seen;

    seen = ch27_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->enemy_hp_current, 0);
}

/* 魔精石碎片 leaves 蘭迪斯's bag from the slot the first search found (entry
   1), the 反禁制器 behind it closes up into entry 1 and entry 2 goes empty;
   the 0xa3 in entry 0 is untouched.  The 反禁制器 he also carries is NOT
   taken: the second search is on unit 3. */
static void chapter_27_spends_the_mana_shard_from_randis(void)
{
    struct ch27_snapshot *seen;

    seen = ch27_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->randis_bag[1], CH26_OTHER_ITEM);
    CHECK_EQ(seen->randis_bag[2], CH26_CARRIED_FLAG);
    CHECK_EQ(seen->randis_bag[3], CH27_SEAL_BREAKER);
    CHECK_EQ(seen->randis_bag[4], CH26_EMPTY_FLAG);
}

/* 反禁制器 leaves 法蓮娜's bag (unit 3) from entry 0, the slot the second
   search returned, and the 魔精石碎片 she also carries closes up into entry 0
   and is kept; entry 1 goes empty. */
static void chapter_27_spends_the_seal_breaker_from_farena(void)
{
    struct ch27_snapshot *seen;

    seen = ch27_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->farena_bag[0], CH26_CARRIED_FLAG);
    CHECK_EQ(seen->farena_bag[1], CH27_MANA_SHARD);
    CHECK_EQ(seen->farena_bag[2], CH26_EMPTY_FLAG);
}

/* The hidden route's scene is WinGA26.dat: its marker 61 is in
   status_timers[4], and neither Win26.dat's 71 in status_timers[3] -- the
   ending route's scene -- nor any other member's mark is there. */
static void chapter_27_hidden_route_plays_winga26_dat(void)
{
    struct ch27_snapshot *seen;

    seen = ch27_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->unit_timers[WINGA26_MARKER_SLOT], WINGA26_MARKER_VALUE);
    CHECK_EQ(seen->unit_timers[WIN26_MARKER_SLOT], 0);
    CHECK_EQ(seen->unit_timers[WIN25_MARKER_SLOT], 0);
    CHECK_EQ(seen->unit_flags, 0);
}

/* No roster write-back runs anywhere in this handler: roster slot 0 still
   carries the 0xa5 filler as its level where a write-back would have copied
   the battle record's 3. */
static void chapter_27_does_not_bank_the_party(void)
{
    struct ch27_snapshot *seen;

    seen = ch27_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->roster_slot_level, ROSTER_FILLER);
}

/* The hidden route hands the game on to chapter 28 -- the index is the store's
   27, from a start of 4 -- and does not raise the return-to-title flag.  The
   revive finds no roster member at 0 HP and bills nothing. */
static void chapter_27_hidden_route_advances_to_chapter_twenty_eight(void)
{
    struct ch27_snapshot *seen;

    seen = ch27_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->chapter_id, CH27_CHAPTER_ID_AFTER);
    CHECK_EQ(seen->quit_requested, 0);
    CHECK_EQ(seen->party_gold, PARTY_GOLD_BEFORE);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_28_end at 0003b9b0.
 *
 *   0003b9bc  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003b9c1  CALL 0x00023980          the battle party is banked
 *   0003b9c6  MOV EAX,0x6220c / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win27.dat" is interpreted
 *   0003b9d4  CALL 0x00039e70          the fallen are revived
 *   0003b9d9  MOV dword ptr [0x00069cf4],0x1c
 *
 * One run against the same staging as chapter 25's, with the same order
 * witnesses: WIN27.DAT retires unit 0 before its marker, so the roster slot
 * carrying the battle record proves the writeback ran before the scene.
 * ------------------------------------------------------------------------ */

/* The index chapter 28's handler leaves: 0x1c at 0003b9d9, chapter 29. */
#define CH28_CHAPTER_ID_AFTER 28

struct ch28_snapshot {
    int state;
    unsigned char unit_timers[STATUS_TIMER_COUNT];
    unsigned char slot_timers[STATUS_TIMER_COUNT];
    int unit_flags;
    int slot_char_id;
    int slot_level;
    int slot_hp_current;
    int enemy_hp_current;
    int chapter_id;
    int party_gold;
};

static struct ch28_snapshot ch28_seen;

/* Runs chapter 28's handler once and records what it left behind. */
static struct ch28_snapshot *ch28_run(void)
{
    struct ch28_snapshot *seen;
    int i;

    seen = &ch28_seen;
    if (seen->state != 0) {
        return seen;
    }
    seen->state = 2;

    if (!ensure_fixture()) {
        return seen;
    }

    stage_globals();

    fdps_chapter_28_end();

    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        seen->unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        seen->slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    seen->unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    seen->slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    seen->slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    seen->slot_hp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    seen->enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    seen->chapter_id = data_fdps_chapter_current_chapter_id;
    seen->party_gold = data_fdps_shared_party_total_gold;
    seen->state = 1;
    return seen;
}

/* The sweep ran: the enemy's hit points are 0 where the staging left 50. */
static void chapter_28_sweeps_the_enemy_side(void)
{
    struct ch28_snapshot *seen;

    seen = ch28_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->enemy_hp_current, 0);
}

/* The party is banked, and banked before the scene: the roster slot carries
   the battle record's level and the writeback's full heal instead of the
   0xa5 filler, which WIN27.DAT's retire would have prevented had it run
   first, and its status bytes are the writeback's zeroes rather than the
   scene's marker. */
static void chapter_28_banks_the_party_before_the_cutscene_runs(void)
{
    struct ch28_snapshot *seen;

    seen = ch28_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(seen->slot_level, RANDIS_LEVEL);
    CHECK_EQ(seen->slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(seen->slot_timers[WIN27_MARKER_SLOT], 0);
}

/* The scene is Win27.dat, the 0-based id of the chapter just won: its retire
   and its marker 47 are on the live record, and neither WIN26.DAT's 71 in
   status_timers[3] nor WIN28.DAT's 53 in status_timers[5] is. */
static void chapter_28_victory_cutscene_is_win27_dat(void)
{
    struct ch28_snapshot *seen;

    seen = ch28_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->unit_timers[WIN27_MARKER_SLOT], WIN27_MARKER_VALUE);
    CHECK_EQ(seen->unit_timers[WIN26_MARKER_SLOT], 0);
    CHECK_EQ(seen->unit_timers[WIN28_MARKER_SLOT], 0);
    CHECK_EQ(seen->unit_flags, UNIT_FLAG_RETIRED);
}

/* The index is assigned chapter 29's 28 -- from a start of 4, so not an
   increment and not the scene's 27 -- and the revive, finding nobody at
   0 HP, bills nothing. */
static void chapter_28_advances_the_chapter_index_to_chapter_twenty_nine(void)
{
    struct ch28_snapshot *seen;

    seen = ch28_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->chapter_id, CH28_CHAPTER_ID_AFTER);
    CHECK_EQ(seen->party_gold, PARTY_GOLD_BEFORE);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_29_end at 0003ba10.
 *
 *   0003ba1c  CALL 0x00039e10          every unit on the enemy side is swept
 *   0003ba21  CALL 0x00023980          the battle party is banked
 *   0003ba26  MOV EAX,0x62218 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4
 *             the cut-scene "Win28.dat" is interpreted
 *   0003ba34  CALL 0x00039e70          the fallen are revived
 *   0003ba39  MOV dword ptr [0x00069cf4],0x1d
 *
 * One run against the same staging as chapter 25's, with the same order
 * witnesses: WIN28.DAT retires unit 0 before its marker, so the roster slot
 * carrying the battle record proves the writeback ran before the scene.
 * ------------------------------------------------------------------------ */

/* The index chapter 29's handler leaves: 0x1d at 0003ba39, chapter 30. */
#define CH29_CHAPTER_ID_AFTER 29

static struct ch28_snapshot ch29_seen;

/* Runs chapter 29's handler once and records what it left behind, in the
   same record chapter 28's run fills -- the two handlers leave the same
   kinds of trace. */
static struct ch28_snapshot *ch29_run(void)
{
    struct ch28_snapshot *seen;
    int i;

    seen = &ch29_seen;
    if (seen->state != 0) {
        return seen;
    }
    seen->state = 2;

    if (!ensure_fixture()) {
        return seen;
    }

    stage_globals();

    fdps_chapter_29_end();

    for (i = 0; i < STATUS_TIMER_COUNT; i++) {
        seen->unit_timers[i] = unit_image[RANDIS_UNIT].status_timers[i];
        seen->slot_timers[i] =
            roster_image[RANDIS_ROSTER_SLOT].status_timers[i];
    }
    seen->unit_flags = (int) unit_image[RANDIS_UNIT].flags;
    seen->slot_char_id = (int) roster_image[RANDIS_ROSTER_SLOT].char_id;
    seen->slot_level = (int) roster_image[RANDIS_ROSTER_SLOT].level;
    seen->slot_hp_current =
        (int) roster_image[RANDIS_ROSTER_SLOT].hp_current;
    seen->enemy_hp_current = (int) unit_image[ENEMY_UNIT].hp_current;
    seen->chapter_id = data_fdps_chapter_current_chapter_id;
    seen->party_gold = data_fdps_shared_party_total_gold;
    seen->state = 1;
    return seen;
}

/* The sweep ran: the enemy's hit points are 0 where the staging left 50. */
static void chapter_29_sweeps_the_enemy_side(void)
{
    struct ch28_snapshot *seen;

    seen = ch29_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->enemy_hp_current, 0);
}

/* The party is banked, and banked before the scene: the roster slot carries
   the battle record's level and the writeback's full heal instead of the
   0xa5 filler, which WIN28.DAT's retire would have prevented had it run
   first, and its status bytes are the writeback's zeroes rather than the
   scene's marker. */
static void chapter_29_banks_the_party_before_the_cutscene_runs(void)
{
    struct ch28_snapshot *seen;

    seen = ch29_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->slot_char_id, RANDIS_CHAR_ID);
    CHECK_EQ(seen->slot_level, RANDIS_LEVEL);
    CHECK_EQ(seen->slot_hp_current, RANDIS_HP_MAX);
    CHECK_EQ(seen->slot_timers[WIN28_MARKER_SLOT], 0);
}

/* The scene is Win28.dat, the 0-based id of the chapter just won: its retire
   and its marker 53 are on the live record, and neither WIN27.DAT's 47 in
   status_timers[4] nor WIN29.DAT's 37 in status_timers[3] is. */
static void chapter_29_victory_cutscene_is_win28_dat(void)
{
    struct ch28_snapshot *seen;

    seen = ch29_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->unit_timers[WIN28_MARKER_SLOT], WIN28_MARKER_VALUE);
    CHECK_EQ(seen->unit_timers[WIN27_MARKER_SLOT], 0);
    CHECK_EQ(seen->unit_timers[WIN29_MARKER_SLOT], 0);
    CHECK_EQ(seen->unit_flags, UNIT_FLAG_RETIRED);
}

/* The index is assigned chapter 30's 29 -- from a start of 4, so not an
   increment and not the scene's 28 -- and the revive, finding nobody at
   0 HP, bills nothing. */
static void chapter_29_advances_the_chapter_index_to_chapter_thirty(void)
{
    struct ch28_snapshot *seen;

    seen = ch29_run();
    CHECK_EQ(seen->state, 1);
    if (seen->state != 1) {
        return;
    }

    CHECK_EQ(seen->chapter_id, CH29_CHAPTER_ID_AFTER);
    CHECK_EQ(seen->party_gold, PARTY_GOLD_BEFORE);
}

/* ------------------------------------------------------------------------
 * fdps_chapter_30_end at 0003ba80.
 *
 *   0003ba8c  CALL 0x00039e10          sweep
 *   0003ba91  CALL 0x00023980          writeback
 *   0003ba96  "Win29.dat" -> CALL 0x00021650
 *   0003baa4  byte [0x00060158] = 1, byte [0x00060159] = 1
 *   0003bab2  CALL 0x0001ba40          the ending sequence
 *   0003bab7  PUSH 0x7530 -> CALL 0x0003d370 (delay)
 *   0003bac4  "GoodEnd.dat" -> CALL 0x00021650
 *   0003bad2  byte [0x000643eb] = 1
 *
 * NOT RUN HERE, for the reason chapter 27's ending route is not run above,
 * made stronger by one more call.  The body has no branch, so there is no
 * part of it that can be reached without passing through
 * fdps_play_ending_credit_roll, which plays the character gallery and the End
 * movie through the VGA aperture, the audio stack, the keyboard vector and the
 * CD path, and needs the whole machine fence tests/ending.c builds around its
 * own two runs.  Behind it the handler holds in the CRT's delay for thirty
 * seconds: that delay (00043cb0) is a calibrated count of INT 21h AH=2Ch
 * calls, so reprogramming the interval timer does not shorten it, and thirty
 * seconds of it plus the credit roll's own frames approaches the host's
 * 45-second heartbeat window with no chance for the harness to write a
 * heartbeat in between.  What the handler decides -- the order, the two
 * scene names, the two gate stores, the absence of a revive and of a chapter
 * index store, and the return-to-title flag -- is left to the playtest of the
 * game's last chapter.
 * ------------------------------------------------------------------------ */

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

void run_chend2b_tests(void)
{
    RUN_TEST(chapter_25_sweeps_the_enemy_side);
    RUN_TEST(chapter_25_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_25_grants_no_spell);
    RUN_TEST(chapter_25_victory_cutscene_is_win24_dat);
    RUN_TEST(chapter_25_revive_charges_nothing_when_nobody_fell);
    RUN_TEST(chapter_25_advances_the_chapter_index_to_chapter_twenty_six);
    RUN_TEST(chapter_26_gives_the_dragon_sword_when_randis_lacks_the_true_one);
    RUN_TEST(chapter_26_gives_nothing_when_randis_carries_the_true_sword);
    RUN_TEST(chapter_26_gives_nothing_when_the_bag_is_full);
    RUN_TEST(chapter_27_sweeps_the_enemy_side);
    RUN_TEST(chapter_27_spends_the_mana_shard_from_randis);
    RUN_TEST(chapter_27_spends_the_seal_breaker_from_farena);
    RUN_TEST(chapter_27_hidden_route_plays_winga26_dat);
    RUN_TEST(chapter_27_does_not_bank_the_party);
    RUN_TEST(chapter_27_hidden_route_advances_to_chapter_twenty_eight);
    RUN_TEST(chapter_28_sweeps_the_enemy_side);
    RUN_TEST(chapter_28_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_28_victory_cutscene_is_win27_dat);
    RUN_TEST(chapter_28_advances_the_chapter_index_to_chapter_twenty_nine);
    RUN_TEST(chapter_29_sweeps_the_enemy_side);
    RUN_TEST(chapter_29_banks_the_party_before_the_cutscene_runs);
    RUN_TEST(chapter_29_victory_cutscene_is_win28_dat);
    RUN_TEST(chapter_29_advances_the_chapter_index_to_chapter_thirty);
    RUN_TEST(the_fixture_container_is_removed);
}
