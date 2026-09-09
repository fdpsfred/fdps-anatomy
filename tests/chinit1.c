/* tests/chinit1.c -- cover for src/chinit1.c.
 *
 * One entry handler per section, each with a run of its own that enters its
 * chapter once for real; the shared staging, the fixture container and the
 * two helpers that free the chapter globals sit at the top and are used by
 * every one of them.  fdps_chapter_01_init is first and fdps_chapter_02_init
 * follows it further down.
 *
 * WHAT THE HANDLER IS.  fdps_chapter_01_init at 00020e90 is six calls and two
 * stores with no branch anywhere in it, so nothing about it is worth testing
 * in pieces: what it decides is the ORDER of the six calls and the two field
 * writes that follow them.  The one case below therefore runs the whole
 * handler once against the shipped containers and reads the answers off the
 * state it leaves, and the cases that follow assert against that one run.
 *
 * Expected values come from the assembly at 00020e90 and from the shipped
 * data, never from the emitted C:
 *
 *   PUSH 0x0 / CALL 0x00023bc0        character 0 joins the roster
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61804 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon00.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x2 / CALL 0x0002d210        map unit 2's record is resolved
 *   MOV byte ptr [EAX + 0x25],0xb     eleven turns of poison
 *   MOV word ptr [EAX + 0x40],0x64    100 current HP
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * WHY UNIT 2 IS THE GUEST HERO, AND WHY THE TWO STORES CANNOT COME EARLIER.
 * The shipped MAP00.DAT declares 1 player slot and 22 scripted deployments
 * and tags NONE of them wave 0, so the state reset leaves the unit array one
 * record long -- the same fact tests/chapter.c and tests/deploy.c pin against
 * the same file.  Index 2 only exists once the cut-scene has deployed
 * somebody, and the shipped ICON00.DAT is what does it: read with the opcode
 * ladder in src/icon.c its last SWITCHMAP returns to chapter 0 and it then
 * runs DEPLOY_WAVE 2 followed by DEPLOY_WAVE 1.  MAP00.DAT's wave tags are 1
 * on one record and 2 on one record (spawn 21 and spawn 0), so that pair puts
 * spawn 0 at index 1 and spawn 21 at index 2, and spawn 21 is side 1,
 * character 12, level 10 -- 索爾, the guest hero.  The two stores are the
 * last thing the handler does for that reason and not by preference.
 *
 * WHY THE CUT-SCENE IS A FIXTURE AND NOT THE SHIPPED ONE.  The shipped
 * ICON00.DAT is the chapter's opening cinematic: 372 instructions that switch
 * to three cut-scene-only maps, play CD tracks and .saf clips, and draw
 * chapter text through a pointer the walk-through of a fresh test image has
 * not filled.  Running it asserts nothing about THIS function and faults on
 * the way, the same reason tests/icon.c gives for not running the shipped
 * members.  So the container the interpreter opens is staged here holding two
 * short scripts, built to the layout in resource_info/vfs.md and read by the
 * game's own fdps_vfs_open and fdps_vfs_load_file.  ICON00.DAT holds the
 * three things the handler's own behaviour depends on and nothing else:
 * DEPLOY_WAVE 2 then DEPLOY_WAVE 1, which reproduces the shipped script's
 * deployment order out of the real MAP00.DAT, and then a SET_UNIT_TIMER that
 * writes a value into the very byte the handler writes afterwards, so "the
 * handler's store lands after the cut-scene" becomes an assertion instead of
 * an assumption.  ICON01.DAT -- the member the NEXT handler names -- deploys
 * a different wave, so loading the wrong member is a different unit count and
 * a different unit 2.
 *
 * The staging REFUSES TO OVERWRITE an IconAni.vfs that is already in the run
 * directory, and removes its own again in the last case: tests/icon.c stages
 * a container of the same name for its own fixture and skips itself if one is
 * already there.
 *
 * WHAT IS STAGED AND WHAT IS REAL.  The four data tables and the roster block
 * are staged here, because they are ticket 23 symbols the build links
 * zero-filled and because the HP the deployment computes has to be a number
 * this file knows.  Everything the chapter load reads is the real container:
 * MAP00.DAT's counts and wave tags, MAP00.COD's placement records, the tile
 * layers, ICON.CEL's sprites, MISC.VFS's title card.  Nothing below asserts
 * what any global held before the run.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED.  The cursor walk at the end of the
 * handler runs with the cursor switched on, so every step of it goes through
 * fdps_render_view_frame, which ends holding the caller until
 * data_fdps_timer_tick_counter moves.  In the game that counter is advanced
 * off AIL's timer; nothing advances it in a test image, so the first frame
 * would never end.  The run hooks IRQ0 for its duration with a handler that
 * increments the counter and chains to the one that was there, which is what
 * tests/anim.c and tests/aiact.c do for the same reason.
 *
 * WHY THE ADAPTER IS PUT INTO MODE 13H.  The title card and every rendered
 * frame write to the aperture at 0xa0000, which only answers in a graphics
 * mode, and the cursor sheet Cusor.cel is loaded out of MISC.VFS into
 * data_fdps_cursor_highlight_sprite_sheet_ptr for the same reason the game
 * loads it at start-up: fdps_blit_cursor_tile reads the sheet's offset table
 * through that global with no null test.  Text mode is restored before the
 * first assertion so a failure is printed on a readable screen.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "deploy.h"
#include "keybd.h"
#include "mapdraw.h"
#include "vfs.h"
#include "chinit1.h"

/* The shipped containers the run needs, and the sheet reader's own floor:
   fdps_cache_cel_sprite_group takes a fixed 0x2970-byte bite out of ICON.CEL's
   offset table, so a shorter file is one it runs off the end of.  Same guard
   tests/chapter.c, tests/deploy.c and tests/rsrc.c use. */
#define CEL_NAME "ICON.CEL"
#define FIELD_NAME "FIELD.VFS"
#define FIELD1_NAME "FIELD1.VFS"
#define FIELD2_NAME "FIELD2.VFS"
#define MISC_NAME "MISC.VFS"
#define CEL_MIN_SIZE (15L + 0x2970L)

/* The sheet fdps_blit_cursor_tile reads, and where the game loads it from. */
#define CURSOR_SHEET_MEMBER "Cusor.cel"

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
#define FIXTURE_MEMBERS 2

/* Chapter 1 is chapter id 0: the entry-handler table slot number is the
   0-based id, and this handler is slot 0 of the table at 00060074. */
#define CHAPTER_01_ID 0

/* MAP00.DAT's own header bytes, read back to prove the chapter really
   loaded: one player slot at +1 and 22 scripted deployments at +2. */
#define CH0_PLAYER_SLOTS 1
#define CH0_CHAR_SPAWNS 22

/* What ICON00.DAT below leaves behind: the one player slot, then the wave-2
   record, then the wave-1 record. */
#define CH0_UNITS_AFTER_SCRIPT 3

/* MAP00.COD record 22 -- the placement behind the map's 22 scripted ones, and
   so the party's start tile -- is (16, 22), and the cursor globals are in
   pixels of 24. */
#define CH0_PARTY_TILE_X 16
#define CH0_PARTY_TILE_Y 22
#define CURSOR_TILE_STEP 24

/* Who the two deployments put where.  MAP00.DAT spawn 0 is the wave-2 record
   and spawn 21 the wave-1 one; the second is side 1, character 12, level 10.
   Character 12 is below the enemy id base of 60, so its stats come from the
   character tables staged below and not from the enemy table. */
#define GUEST_HERO_UNIT 2
#define GUEST_HERO_CHAR_ID 12
#define GUEST_HERO_LEVEL 10
#define GUEST_HERO_SIDE 1

/* The character the handler puts on the roster, and the side a roster member
   is deployed on. */
#define OPENING_CHAR_ID 0
#define PLAYER_SIDE 2

/* What the handler writes on the guest hero: MOV byte ptr [EAX + 0x25],0xb
   and MOV word ptr [EAX + 0x40],0x64.  status_timers[3] is record +0x25. */
#define POISON_TIMER_SLOT 3
#define GUEST_HERO_POISON_TURNS 0x0b
#define GUEST_HERO_OPENING_HP 100
#define STATUS_TIMER_COUNT 6

/* What ICON00.DAT's SET_UNIT_TIMER puts in that same byte while the cut-scene
   is still running.  It is not 0x0b and not 0, so the byte read back afterwards
   says which of the two writes happened last. */
#define SCRIPT_POISON_TURNS 7

/* The stats staged for the two characters the run deploys, and the HP each
   ends up with.  fdps_roster_add_character and fdps_deploy_unit both compute
   hp_base + hp_min * (level - 1), so character 0 at level 1 keeps its base and
   character 12 at the deployment record's level 10 takes nine growth steps.
   The guest hero's maximum is deliberately nowhere near 100: the handler must
   leave +0x42 alone, and a body that wrote the 100 through an int would carry
   it into that field as well. */
#define RANDIS_LEVEL 1
#define RANDIS_HP_BASE 40
#define RANDIS_HP_MIN 3
#define RANDIS_HP_MAX 40

#define SOL_HP_BASE 200
#define SOL_HP_MIN 20
#define SOL_HP_MAX (SOL_HP_BASE + SOL_HP_MIN * (GUEST_HERO_LEVEL - 1))

/* --- what the chapter 2 run below expects ------------------------------- */

/* Chapter 2 is chapter id 1, and this handler is slot 1 of the table at
   00060074: the dword at 00060078 is 00020ef0. */
#define CHAPTER_02_ID 1

/* The character PUSH 0x6 at 00020efc puts on the roster: 尤利安 the 僧侶,
   character id 6 (assets/characters.md). */
#define JOINING_CHAR_ID 6

/* MAP01.DAT's own header bytes, read back to prove chapter 2's map really
   loaded and not chapter 1's: two player slots at +1 and 27 scripted
   deployments at +2, against MAP00.DAT's 1 and 22. */
#define CH1_PLAYER_SLOTS 2
#define CH1_CHAR_SPAWNS 27

/* How long the unit array is when the handler returns.  MAP01.DAT tags all 27
   of its deployments wave 1 and none of them wave 0, so the chapter state
   reset's own opening deploy adds nothing, and the fixture ICON01.DAT below
   asks for wave 3, which that map does not carry either.  The array is
   therefore the two player slots and nothing else -- had ICON00.DAT been the
   member opened it would have deployed wave 2 (no records here) and then wave
   1 (all 27), for 29. */
#define CH1_UNITS_AFTER_SCRIPT CH1_PLAYER_SLOTS

/* MAP01.COD record 27 -- the placement behind the map's 27 scripted ones, and
   so the first party slot's start tile -- is (14, 27).  The record a party
   slot is put on is data_fdps_map_char_spawn_count + slot_index and not the
   slot number (src/deploy.c). */
#define CH1_PARTY_TILE_X 14
#define CH1_PARTY_TILE_Y 27

/* 尤利安's own line out of FRIAPRDA.DAT and FRILEVUP.DAT
   (assets/characters.md): level 3, 48 base HP, 8 HP a level.  The tables are
   staged rather than loaded -- ticket 23 has not emitted them -- so these are
   put in by hand, and fdps_roster_add_character computes hp_base + hp_min *
   (level - 1) from them. */
#define JULIAN_LEVEL 3
#define JULIAN_HP_BASE 48
#define JULIAN_HP_MIN 8
#define JULIAN_HP_MAX (JULIAN_HP_BASE + JULIAN_HP_MIN * (JULIAN_LEVEL - 1))

/* What ICON01.DAT's SET_UNIT_TIMER writes, and where.  The opcode's slot
   operand is measured from status_timers[3] (src/icon.c), so operand 1 is
   status_timers[4] -- a different slot from the [3] ICON00.DAT writes, and a
   different value, so a run that opened the wrong member is visible in the
   array rather than merely in its length. */
#define SCRIPT_CH02_MARKER_OPERAND 1
#define SCRIPT_CH02_MARKER_SLOT 4
#define SCRIPT_CH02_MARKER_VALUE 5

/* What fdps_build_map_unit_array leaves in a player slot with no roster member
   behind it: the record zeroed and the retired bit set at +5.  The party has
   one member when chapter 2 starts here and MAP01.DAT wants two slots, so the
   second one is exactly that. */
#define UNIT_FLAG_RETIRED 1

/* Table rows wide enough for every id the run touches: character 12 indexes
   the roster tables directly, and every item id including 0xff is a valid
   index because fdps_unit_recompute_combat_stats follows the equipped flag
   into fdps_get_item_record with no bound of any kind. */
#define TABLE_ROWS 64
#define ITEM_TABLE_ROWS 256

/* The roster block: one 0x50-byte record per member, and the handler appends
   at index 0 because fdps_title_screen has just reset the count to 0. */
#define ROSTER_SLOTS 8
#define UNIT_RECORD_STRIDE 0x50

/* How many entries the per-cell event flag table has. */
#define CELL_EVENT_FLAGS 32

/* The adapter and the timer vector. */
#define VGA_BASE 0x000a0000
#define MODE_320X200X256 0x13
#define MODE_TEXT 0x03
#define TIMER_VECTOR 8
#define DAC_ENTRIES 256

static struct fdps_unit_record stage_roster[ROSTER_SLOTS];
static struct fdps_character_base_record stage_char[TABLE_ROWS];
static struct fdps_character_growth stage_growth[TABLE_ROWS];
static struct fdps_enemy_data stage_enemy[TABLE_ROWS];
static struct fdps_item_effect stage_items[ITEM_TABLE_ROWS];
static unsigned char stage_palette[DAC_ENTRIES * 3];

/* ICON00.DAT: deploy the wave-2 record, deploy the wave-1 record, write the
   poison byte the handler will overwrite, stop.  Opcode numbers and operand
   counts are src/icon.c's ladder -- 0x04 takes a wave number and a
   placement flag, 0x12 takes a unit index, a timer slot measured from
   record +0x22 + 3 and a value, and 0x00 falls into the arm that ends the
   script. */
static unsigned char fixture_icon00_dat[] = {
    0x04, 0x02, 0x01,
    0x04, 0x01, 0x01,
    0x12, GUEST_HERO_UNIT, 0x00, SCRIPT_POISON_TURNS,
    0x00
};

/* ICON01.DAT: the member fdps_chapter_02_init names.  Its DEPLOY_WAVE asks
   for wave 3, which is a different wave from either of ICON00.DAT's, so on
   MAP00.DAT it would put down that map's seven wave-3 records instead of the
   two ICON00.DAT deploys -- chapter 1's run must never reach it and its unit
   count says so.  On MAP01.DAT, the map chapter 2 loads, wave 3 matches
   nothing at all: all 27 of that map's deployments are tagged wave 1.

   The SET_UNIT_TIMER after it is the witness that THIS member and not
   ICON00.DAT was the one the interpreter opened.  It writes a value of its
   own into a timer slot of its own on unit 0, and ICON00.DAT's own
   SET_UNIT_TIMER writes a different value into a different slot on unit 2, so
   the two are told apart by what is in the array afterwards rather than by
   the unit count alone. */
static unsigned char fixture_icon01_dat[] = {
    0x04, 0x03, 0x01,
    0x12, 0x00, SCRIPT_CH02_MARKER_OPERAND, SCRIPT_CH02_MARKER_VALUE,
    0x00
};

static char *fixture_names[FIXTURE_MEMBERS] = { "ICON00.DAT", "ICON01.DAT" };

static unsigned char *fixture_bytes[FIXTURE_MEMBERS] = {
    fixture_icon00_dat, fixture_icon01_dat
};

static int fixture_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_icon00_dat), sizeof(fixture_icon01_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run_state = 0;

/* Whether this file created the container, and so whether it may remove it. */
static int fixture_owned = 0;

/* Everything the cases assert, captured the instant the handler returned. */
static int seen_roster_count;
static int seen_roster_char_id;
static int seen_player_slots;
static int seen_char_spawns;
static int seen_unit_count;
static int seen_unit0_char_id;
static int seen_unit0_flags;
static int seen_unit0_side;
static int seen_unit0_hp_max;
static int seen_unit2_char_id;
static int seen_unit2_level;
static int seen_unit2_side;
static unsigned char seen_unit2_timers[STATUS_TIMER_COUNT];
static int seen_unit2_hp_current;
static int seen_unit2_hp_max;
static int seen_cursor_x;
static int seen_cursor_y;
static int seen_chapter_id;

static void (__interrupt __far *saved_timer)();

static void __interrupt __far tick_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(saved_timer);
}

static void set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

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
   tests/icon.c's own fixture, and neither may be clobbered. */
static int stage_fixture_archive(void)
{
    FILE *fp;
    long member_at;
    int i;

    /* The two runs below share one container: whichever of them gets there
       first builds it, and the second finds it already standing.  Without this
       the second run would see a file of that name in the directory and take
       it for somebody else's. */
    if (fixture_owned) {
        return 1;
    }

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

static int containers_present(void)
{
    FILE *fp;
    long size;

    fp = fopen(CEL_NAME, "rb");
    if (fp == NULL) {
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    if (size < CEL_MIN_SIZE) {
        return 0;
    }

    return file_present(FIELD_NAME) && file_present(FIELD1_NAME)
           && file_present(FIELD2_NAME) && file_present(MISC_NAME);
}

static void zero_bytes(void *block, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        bytes[i] = 0;
    }
}

/* Nulling, not freeing: the chapter load frees every one of these on entry,
   and with a layer count of zero and null everywhere else it frees nothing --
   the state a freshly started process is in. */
static void clear_chapter_globals(void)
{
    int layer;

    for (layer = 0; layer < 6; layer++) {
        data_fdps_scene_layer_tile_map_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_sheet_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_attr_ptr[layer] = NULL;
    }
    data_fdps_scene_layer_count = 0;
    data_fdps_current_chapter_text_ptr = NULL;
    data_fdps_tile_event_data_table_ptr = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_spawn_pos_table_ptr = NULL;
}

static void free_chapter_globals(void)
{
    int layer;

    for (layer = 0; layer < data_fdps_scene_layer_count; layer++) {
        free(data_fdps_scene_layer_tile_map_ptrs[layer]);
        free(data_fdps_scene_layer_tile_sheet_ptrs[layer]);
        free(data_fdps_scene_layer_tile_attr_ptr[layer]);
    }
    free(data_fdps_current_chapter_text_ptr);
    free(data_fdps_tile_event_data_table_ptr);
    free(data_fdps_map_cell_event_code_layer_ptr);
    free(data_fdps_battle_move_grid_ptr);
    clear_chapter_globals();

    if (data_fdps_map_unit_count != 0) {
        free(data_fdps_map_unit_array_ptr);
    }
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;

    if (data_fdps_cel_sprite_cache_count != 0) {
        free(data_fdps_cel_sprite_cache_ptr);
    }
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;

    free(data_fdps_cursor_highlight_sprite_sheet_ptr);
    data_fdps_cursor_highlight_sprite_sheet_ptr = NULL;
    data_fdps_vga_main_palette_ptr = NULL;

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* Everything the handler reads that ticket 23 has not filled, plus a palette
   for the two fades to ramp and a stale value in every scalar the run is
   expected to overwrite. */
static void stage_globals(void)
{
    int member;
    int entry;
    int flag_index;

    clear_chapter_globals();

    zero_bytes(stage_char, (int) sizeof(stage_char));
    zero_bytes(stage_growth, (int) sizeof(stage_growth));
    zero_bytes(stage_enemy, (int) sizeof(stage_enemy));
    zero_bytes(stage_items, (int) sizeof(stage_items));
    zero_bytes(stage_roster, (int) sizeof(stage_roster));

    stage_char[OPENING_CHAR_ID].level = (unsigned char) RANDIS_LEVEL;
    stage_char[OPENING_CHAR_ID].hp_base = (short) RANDIS_HP_BASE;
    stage_growth[OPENING_CHAR_ID].hp_min = (unsigned char) RANDIS_HP_MIN;
    stage_char[GUEST_HERO_CHAR_ID].hp_base = (short) SOL_HP_BASE;
    stage_growth[GUEST_HERO_CHAR_ID].hp_min = (unsigned char) SOL_HP_MIN;
    stage_char[JOINING_CHAR_ID].level = (unsigned char) JULIAN_LEVEL;
    stage_char[JOINING_CHAR_ID].hp_base = (short) JULIAN_HP_BASE;
    stage_growth[JOINING_CHAR_ID].hp_min = (unsigned char) JULIAN_HP_MIN;

    data_fdps_battle_character_base_table_ptr = (unsigned char *) stage_char;
    data_fdps_battle_character_growth_table_ptr =
        (unsigned char *) stage_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) stage_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) stage_items;

    for (member = 0; member < ROSTER_SLOTS; member++) {
        stage_roster[member].char_id = 0xff;
    }
    data_fdps_roster_array_ptr = (unsigned char *) stage_roster;
    data_fdps_roster_member_count = 0;

    for (entry = 0; entry < DAC_ENTRIES; entry++) {
        stage_palette[entry * 3] = (unsigned char) (entry % 64);
        stage_palette[entry * 3 + 1] = (unsigned char) ((entry * 3) % 64);
        stage_palette[entry * 3 + 2] = (unsigned char) ((entry * 5) % 64);
    }
    data_fdps_vga_main_palette_ptr = stage_palette;

    /* Both are staged at numbers no map carries, so reading the loaded map's
       own pair back -- MAP00.DAT's 1 and 22, MAP01.DAT's 2 and 27 -- says the
       chapter really loaded, and says which one. */
    data_fdps_map_player_slot_count = 99;
    data_fdps_map_char_spawn_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_ptr = NULL;

    for (flag_index = 0; flag_index < CELL_EVENT_FLAGS; flag_index++) {
        data_fdps_map_cell_event_triggered_flags[flag_index] = 0xff;
    }

    /* The terrain panel is left switched off, which is the state a fresh
       process is in: it draws through two sheet pointers ticket 23 has not
       filled and fdps_draw_cursor_info_panel tests these two first. */
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;

    data_fdps_chapter_event_or_battle_end_code = 2;
    data_fdps_battle_turn_counter = 7;
    data_fdps_map_cursor_draw_mode = 6;
    data_fdps_battle_view_window_origin_x = 7;
    data_fdps_battle_view_window_origin_y = 9;
    data_fdps_map_cursor_world_x = 48;
    data_fdps_map_cursor_world_y = 72;
    data_fdps_input_scancode_queue_head = 3;
    data_fdps_input_scancode_queue_write_index = 9;
    data_fdps_scene_layer_scroll_last_tick = 0;
    data_fdps_view_frame_last_tick = 0;
}

static void capture(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *unit2;
    int slot;

    seen_roster_count = data_fdps_roster_member_count;
    seen_roster_char_id = (int) stage_roster[0].char_id;
    seen_player_slots = data_fdps_map_player_slot_count;
    seen_char_spawns = data_fdps_map_char_spawn_count;
    seen_unit_count = data_fdps_map_unit_count;
    seen_cursor_x = data_fdps_map_cursor_world_x;
    seen_cursor_y = data_fdps_map_cursor_world_y;
    seen_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    unit2 = unit0 + GUEST_HERO_UNIT;

    seen_unit0_char_id = (int) unit0->char_id;
    seen_unit0_flags = (int) unit0->flags;
    seen_unit0_side = (int) unit0->side;
    seen_unit0_hp_max = (int) unit0->hp_max;

    seen_unit2_char_id = (int) unit2->char_id;
    seen_unit2_level = (int) unit2->level;
    seen_unit2_side = (int) unit2->side;
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen_unit2_timers[slot] = unit2->status_timers[slot];
    }
    seen_unit2_hp_current = (int) unit2->hp_current;
    seen_unit2_hp_max = (int) unit2->hp_max;
}

/* Runs the handler once, against the shipped containers and the fixture
   cut-scene, and records what it left behind. */
static void run_handler(void)
{
    if (run_state != 0) {
        return;
    }
    run_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_01_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_01_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture();
    free_chapter_globals();
    run_state = 1;
}

/* 蘭迪斯 is on the map, which says the roster add ran BEFORE the state
   reset.  The reset deploys the map's one player slot out of the roster array
   and stops at the roster count, and the count was 0 on entry: had the two
   run the other way round, slot 0 would be the spare fdps_build_map_unit_array
   writes for a slot with no member behind it -- zeroed, with the retired bit
   set at record +5 -- instead of a live player-side unit carrying the roster
   record's HP.  The two map counts are read back as well, because they were
   staged at numbers MAP00.DAT does not carry. */
static void randis_is_on_the_map_before_the_chapter_is_built(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_roster_count, 1);
    CHECK_EQ(seen_roster_char_id, OPENING_CHAR_ID);
    CHECK_EQ(seen_player_slots, CH0_PLAYER_SLOTS);
    CHECK_EQ(seen_char_spawns, CH0_CHAR_SPAWNS);
    CHECK_EQ(seen_unit0_char_id, OPENING_CHAR_ID);
    CHECK_EQ(seen_unit0_flags, 0);
    CHECK_EQ(seen_unit0_side, PLAYER_SIDE);
    CHECK_EQ(seen_unit0_hp_max, RANDIS_HP_MAX);
}

/* The cut-scene the handler names is Icon00.dat and it really ran.  The unit
   count is the player slot plus the two records ICON00.DAT deploys; ICON01.DAT
   deploys seven, and the shipped map's wave tags are what make those two
   numbers different.  Unit 2 is MAP00.DAT's wave-1 record -- side 1,
   character 12, level 10 -- and the chapter id is untouched, the handler
   neither reading nor writing it. */
static void the_chapter_1_cutscene_deploys_the_guest_hero(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit_count, CH0_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen_unit2_char_id, GUEST_HERO_CHAR_ID);
    CHECK_EQ(seen_unit2_level, GUEST_HERO_LEVEL);
    CHECK_EQ(seen_unit2_side, GUEST_HERO_SIDE);
    CHECK_EQ(seen_chapter_id, CHAPTER_01_ID);
}

/* Eleven turns of poison land in status_timers[3], and they land AFTER the
   cut-scene: ICON00.DAT's SET_UNIT_TIMER put 7 in that same byte while the
   script was running, so a handler whose store came before the script would
   leave 7 behind.  The other five timers are the zeroes the deployment
   memset there -- the store is one byte wide and touches no neighbour. */
static void the_guest_hero_opens_poisoned_for_eleven_turns(void)
{
    int slot;

    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit2_timers[POISON_TIMER_SLOT], GUEST_HERO_POISON_TURNS);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != POISON_TIMER_SLOT) {
            CHECK_EQ(seen_unit2_timers[slot], 0);
        }
    }
}

/* Current HP is set to 100 and the maximum is not touched.  The deployment
   computed 380 from the staged tables, so a store that was 32 bits wide, or
   that carried the same literal into +0x42, is a different answer here. */
static void the_guest_hero_opens_on_a_hundred_hit_points(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit2_hp_current, GUEST_HERO_OPENING_HP);
    CHECK_EQ(seen_unit2_hp_max, SOL_HP_MAX);
}

/* The cursor ends on unit 0's tile and not on the guest hero's.  The walk
   starts from the (0, 0) the state reset left and MAP00.COD record 22 puts
   蘭迪斯 on tile (16, 22), so the cursor globals are that tile scaled by
   the 24-pixel step. */
static void the_cursor_is_parked_on_unit_zero(void)
{
    run_handler();
    CHECK_EQ(run_state, 1);
    if (run_state != 1) {
        return;
    }

    CHECK_EQ(seen_cursor_x, CH0_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen_cursor_y, CH0_PARTY_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_02_init @ 00020ef0 ------------------------------------
 *
 * Five calls, straight line, no branch and -- unlike chapter 1's handler --
 * no store of its own anywhere in the body.  What it decides is which
 * character joins, the ORDER of the five calls, and which cut-scene member
 * and which unit the two arguments name, so the run below is the same shape
 * as the one above: enter chapter 2 once for real and read the answers off
 * the state it leaves.
 *
 * Expected values come from the assembly at 00020ef0 and from the shipped
 * data, never from the emitted C:
 *
 *   PUSH 0x6 / CALL 0x00023bc0        character 6 joins the roster
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61810 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon01.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * WHY THE MAP IS THE WITNESS FOR THE ORDER.  MAP01.DAT declares two player
 * slots against MAP00.DAT's one, and fdps_build_map_unit_array fills slot i
 * from roster slot i only while i is below the roster count, writing a zeroed
 * record with the retired bit set for the rest.  The roster is empty on entry
 * here, so the handler's own add is the only thing that can make slot 0 a
 * live unit: a body that reset the chapter before adding 尤利安 leaves TWO
 * retired spares, and slot 1 is a retired spare either way.
 *
 * WHY THE CUT-SCENE IS A FIXTURE.  Same reason the chapter 1 run gives: the
 * shipped ICON01.DAT is a cinematic that switches maps, plays CD tracks and
 * draws through pointers a test image has not filled.  The staged ICON01.DAT
 * is three instructions -- a DEPLOY_WAVE for a wave MAP01.DAT does not carry,
 * a SET_UNIT_TIMER marker, and the terminator -- and the container it lives
 * in is the one the chapter 1 run already built.
 */

static int run2_state = 0;

static int seen2_roster_count;
static int seen2_roster_char_id;
static int seen2_player_slots;
static int seen2_char_spawns;
static int seen2_unit_count;
static int seen2_unit0_char_id;
static int seen2_unit0_flags;
static int seen2_unit0_side;
static int seen2_unit0_hp_current;
static int seen2_unit0_hp_max;
static unsigned char seen2_unit0_timers[STATUS_TIMER_COUNT];
static int seen2_unit1_flags;
static int seen2_unit1_char_id;
static int seen2_cursor_x;
static int seen2_cursor_y;
static int seen2_chapter_id;

static void capture_chapter_02(void)
{
    struct fdps_unit_record *unit0;
    int slot;

    seen2_roster_count = data_fdps_roster_member_count;
    seen2_roster_char_id = (int) stage_roster[0].char_id;
    seen2_player_slots = data_fdps_map_player_slot_count;
    seen2_char_spawns = data_fdps_map_char_spawn_count;
    seen2_unit_count = data_fdps_map_unit_count;
    seen2_cursor_x = data_fdps_map_cursor_world_x;
    seen2_cursor_y = data_fdps_map_cursor_world_y;
    seen2_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    seen2_unit0_char_id = (int) unit0->char_id;
    seen2_unit0_flags = (int) unit0->flags;
    seen2_unit0_side = (int) unit0->side;
    seen2_unit0_hp_current = (int) unit0->hp_current;
    seen2_unit0_hp_max = (int) unit0->hp_max;
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen2_unit0_timers[slot] = unit0->status_timers[slot];
    }

    seen2_unit1_flags = (int) unit0[1].flags;
    seen2_unit1_char_id = (int) unit0[1].char_id;
}

/* Runs the chapter 2 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The timer hook and the
   graphics mode are here for the reasons the chapter 1 run gives. */
static void run_chapter_02_handler(void)
{
    if (run2_state != 0) {
        return;
    }
    run2_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_02_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_02_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_02();
    free_chapter_globals();
    run2_state = 1;
}

/* 尤利安 is on the map as a live player-side unit, which says the roster add
   ran BEFORE the state reset and that the argument it was handed is character
   6.  The two map counts are read back as well, because they were staged at
   numbers no map carries and because MAP01.DAT's pair is not MAP00.DAT's --
   they say chapter 2's map is the one that loaded. */
static void julian_is_on_the_map_before_the_chapter_is_built(void)
{
    run_chapter_02_handler();
    CHECK_EQ(run2_state, 1);
    if (run2_state != 1) {
        return;
    }

    CHECK_EQ(seen2_roster_count, 1);
    CHECK_EQ(seen2_roster_char_id, JOINING_CHAR_ID);
    CHECK_EQ(seen2_player_slots, CH1_PLAYER_SLOTS);
    CHECK_EQ(seen2_char_spawns, CH1_CHAR_SPAWNS);
    CHECK_EQ(seen2_unit0_char_id, JOINING_CHAR_ID);
    CHECK_EQ(seen2_unit0_flags, 0);
    CHECK_EQ(seen2_unit0_side, PLAYER_SIDE);
    CHECK_EQ(seen2_unit0_hp_max, JULIAN_HP_MAX);
    CHECK_EQ(seen2_unit0_hp_current, JULIAN_HP_MAX);
}

/* The map's second player slot has nobody behind it and is the zeroed,
   retired spare, because the handler adds exactly one character.  A second
   fdps_roster_add_character here -- or an add that ran after the reset,
   leaving the count at 0 when the array was built -- shows up in this pair. */
static void the_second_player_slot_is_the_retired_spare(void)
{
    run_chapter_02_handler();
    CHECK_EQ(run2_state, 1);
    if (run2_state != 1) {
        return;
    }

    CHECK_EQ(seen2_unit_count, CH1_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen2_unit1_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(seen2_unit1_char_id, 0);
}

/* The cut-scene the handler names is Icon01.dat and it really ran.  Its
   SET_UNIT_TIMER marker is in unit 0's status_timers[4]; ICON00.DAT's marker
   is a different value in status_timers[3], and every other timer byte is one
   of the zeroes the deployment memset there.  The chapter id is untouched,
   the handler neither reading nor writing it -- both the script number and
   the title-card graphic are chosen from it by the callees. */
static void the_chapter_2_cutscene_is_icon01_dat(void)
{
    int slot;

    run_chapter_02_handler();
    CHECK_EQ(run2_state, 1);
    if (run2_state != 1) {
        return;
    }

    CHECK_EQ(seen2_unit0_timers[SCRIPT_CH02_MARKER_SLOT],
             SCRIPT_CH02_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH02_MARKER_SLOT) {
            CHECK_EQ(seen2_unit0_timers[slot], 0);
        }
    }
    CHECK_EQ(seen2_chapter_id, CHAPTER_02_ID);
}

/* The cursor ends on unit 0's tile.  The walk starts from the (0, 0) the state
   reset left, and MAP01.COD record 27 -- the first record past the map's 27
   scripted deployments -- puts 尤利安 on tile (14, 27), so the cursor globals
   are that tile scaled by the 24-pixel step.  Indexing the placement table by
   the slot number instead would land him on record 0, tile (9, 4). */
static void the_chapter_2_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_02_handler();
    CHECK_EQ(run2_state, 1);
    if (run2_state != 1) {
        return;
    }

    CHECK_EQ(seen2_cursor_x, CH1_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen2_cursor_y, CH1_PARTY_TILE_Y * CURSOR_TILE_STEP);
}

/* Takes the fixture container away again, so tests/icon.c can stage its own.
   A container this file did not create is somebody else's and is left where
   it stands, which is also the only path on which this case asserts
   nothing. */
static void the_fixture_container_is_removed(void)
{
    if (!fixture_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    fixture_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

void run_chinit1_tests(void)
{
    RUN_TEST(randis_is_on_the_map_before_the_chapter_is_built);
    RUN_TEST(the_chapter_1_cutscene_deploys_the_guest_hero);
    RUN_TEST(the_guest_hero_opens_poisoned_for_eleven_turns);
    RUN_TEST(the_guest_hero_opens_on_a_hundred_hit_points);
    RUN_TEST(the_cursor_is_parked_on_unit_zero);
    RUN_TEST(julian_is_on_the_map_before_the_chapter_is_built);
    RUN_TEST(the_second_player_slot_is_the_retired_spare);
    RUN_TEST(the_chapter_2_cutscene_is_icon01_dat);
    RUN_TEST(the_chapter_2_cursor_is_parked_on_unit_zero);
    RUN_TEST(the_fixture_container_is_removed);
}
