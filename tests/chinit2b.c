/* tests/chinit2b.c -- cover for src/chinit2b.c.
 *
 * One entry handler per section, in address order.  So far those are
 * fdps_chapter_25_init at 000214d0, fdps_chapter_26_init at 00021510,
 * fdps_chapter_27_init at 00021550 and fdps_chapter_28_init at 00021590.
 * Each section enters its chapter once for real and reads the answers off the
 * state the handler left; each stages its own three-member IconAni.vfs and
 * takes it away again in its own last case, so only one fixture container
 * exists at a time and the sections must run in the order the runner lists
 * them.
 *
 * WHAT THE HANDLER IS.  fdps_chapter_25_init is four calls with no branch, no
 * loop and no store anywhere in it, so nothing about it is worth testing in
 * pieces: what it decides is WHICH cut-scene member is opened, WHICH unit the
 * cursor is parked on, and the ORDER the four calls run in.  The run below
 * therefore enters chapter 25 once for real against the shipped containers and
 * reads the answers off the state it leaves, and every case asserts against
 * that one run.
 *
 * Expected values come from the assembly at 000214d0 and from the shipped
 * data, never from the emitted C:
 *
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61924 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon24.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * NOBODY JOINS AND NO SLOT IS LEFT OVER.  The party is twelve members when the
 * chapter opens -- the eleven chapters 1 to 19 assembled and 珊, whom chapter
 * 24's handler appended -- and MAP24.DAT is the first map in the game to ask
 * for twelve player slots, so every slot has a member behind it and the array
 * carries no zeroed, retired spare.  There is no untouched roster slot to read
 * the 0xff sentinel out of, so what says the handler added nobody is the count
 * still reading twelve.
 *
 * 法蓮娜 IS RETIRED BY THE CUT-SCENE.  The shipped ICON24.DAT holds one
 * RETIRE -- map unit 3 -- and no REVIVE anywhere, and a player slot's map unit
 * index is its roster slot, so the eleven the player commands this chapter are
 * every slot but 3, which is the strategy guide's own 己方 line for the
 * chapter, 法蓮娜以外的所有人.  That is the one thing this chapter's map
 * cannot express by itself, so the fixture below keeps that opcode verbatim.
 *
 * WHY THE CUT-SCENE IS A FIXTURE AND NOT THE SHIPPED ONE.  Same reason
 * tests/chinit1.c, tests/chinit1b.c and tests/chinit2.c give for their runs:
 * the shipped ICON24.DAT walks and poses actors through eighty-eight opcodes,
 * shakes the view nine times, sets two CD music tracks and draws chapter text,
 * none of which this file is asking about and all of which cost a rendered
 * frame per sub-step.  The staged ICON24.DAT keeps the shipped member's single
 * RETIRE, names the tile the shipped member's place-and-walk leaves unit 0 on,
 * and adds a marker so the run can say which member the interpreter opened.
 *
 * WHY THE TWO SWITCH_MAPs ARE LEFT OUT OF THE FIXTURE.  ICON24.DAT is the one
 * member in this file that switches map on its own account: the opcode at
 * script offset 4 resets the state onto map 56, the cut-scene stage, and the
 * one at offset 183 resets it back onto map 24 before the RETIRE.  The pair
 * leaves nothing behind that any case here can read -- the second switch puts
 * the chapter id back to 24 and rebuilds the board from map 24 again, which is
 * exactly the board the handler's own reset had already built, so whatever
 * stood on map 56 is thrown away with it -- so keeping them would buy two more
 * full chapter loads and no assertion.  What they do is recorded in
 * src/chinit2b.h; what this file tests is the handler.
 *
 * WHY THE NEIGHBOURING MEMBERS ARE STAGED TOO.  Icon24.dat is chapter
 * TWENTY-FIVE's script -- the number in the name is the 0-based chapter id --
 * so the mistake this file has to be able to catch is a handler naming the
 * member one either side of its own, and the member one late is the one a
 * rewrite that read the chapter number out of the handler's own name would
 * reach for.  A container holding only the member under test would turn that
 * mistake into a member the interpreter cannot find, and a member it cannot
 * find sends the interpreter into fdps_wait_any_key, which spins for a
 * keyboard interrupt that never comes.  So the container holds three members:
 * ICON24.DAT under test, with ICON23.DAT and ICON25.DAT at the two ends as
 * decoys that deploy nobody, retire nobody, switch no map and mark unit 0 with
 * a value of their own.  A run that opened either decoy is caught twice over:
 * it leaves a full party on the map and a marked unit 0.
 *
 * WHAT IS STAGED AND WHAT IS REAL.  The four data tables and the roster block
 * are staged here, because they are ticket 23 symbols the build links
 * zero-filled; the character rows are FRIAPRDA.DAT's and FRILEVUP.DAT's own
 * numbers as recorded in assets/characters.md.  Everything the chapter load
 * reads is the real container: MAP24.DAT's counts and wave tags, MAP24.COD's
 * placement records, the tile layers, ICON.CEL's sprites, MISC.VFS's title
 * card.  Nothing below asserts what any global held before the run.
 *
 * The staging REFUSES TO OVERWRITE an IconAni.vfs that is already in the run
 * directory, and removes its own again in the last case: tests/chinit1.c,
 * tests/chinit1b.c, tests/chinit2.c and tests/icon.c stage a container of the
 * same name for their own fixtures and each skips itself if one is already
 * there.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED AND WHY THE ADAPTER IS PUT INTO MODE 13H.
 * The reasons tests/chinit1.c gives: the closing cursor walk holds each frame
 * until data_fdps_timer_tick_counter moves and nothing advances it in a test
 * image, and the title card and every rendered frame write to the aperture at
 * 0xa0000, which only answers in a graphics mode.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "deploy.h"
#include "keybd.h"
#include "mapdraw.h"
#include "vfs.h"
#include "roster.h"
#include "chinit2b.h"

/* The shipped containers the run needs, and the sheet reader's own floor:
   fdps_cache_cel_sprite_group takes a fixed 0x2970-byte bite out of ICON.CEL's
   offset table, so a shorter file is one it runs off the end of.  Same guard
   tests/chinit1.c, tests/chinit1b.c, tests/chinit2.c and tests/deploy.c
   use. */
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
#define FIXTURE_MEMBERS 3

/* The side code the player's own units carry and the one a map deployment
   carries, the side a guest would carry, and the flag bit a RETIRE raises. */
#define PLAYER_SIDE 2
#define ENEMY_SIDE 0
#define GUEST_SIDE 1
#define UNIT_FLAG_RETIRED 1

/* How many rows the staged tables carry.  The enemy table is indexed by
   char_id - 0x3c (src/deploy.c) and the highest id chapter 25 puts on the
   board is 104, the 黑暗祭司, which is row 44; the item table is indexed by an
   id the roster add takes straight out of an inventory entry with no bound of
   any kind. */
#define TABLE_ROWS 128
#define ITEM_TABLE_ROWS 256

/* The roster block: one 0x50-byte record per member, twelve of them in use. */
#define ROSTER_SLOTS 16

/* How many status bytes a unit record carries, and how many entries the
   per-cell event flag table has. */
#define STATUS_TIMER_COUNT 6
#define CELL_EVENT_FLAGS 32

/* The adapter and the timer vector. */
#define MODE_320X200X256 0x13
#define MODE_TEXT 0x03
#define TIMER_VECTOR 8
#define DAC_ENTRIES 256

/* How the cursor globals are scaled from a tile: 24 pixels a tile. */
#define CURSOR_TILE_STEP 24

/* --- fdps_chapter_25_init @ 000214d0 constants -------------------------- */

/* Chapter 25 is chapter id 24, and this handler is slot 24 of the table at
   00060074 -- the dword at 000600d4 is 000214d0, and that data reference is
   the function's only xref. */
#define CHAPTER_25_ID 24

/* The party when chapter 25 opens: the eleven chapters 1 to 19 assembled plus
   珊, whom chapter 24's handler added.  This handler adds nobody -- there is
   no PUSH/CALL 0x00023bc0 anywhere in its body. */
#define PARTY_AT_CHAPTER_25 12

/* The twelve characters and their own FRIAPRDA.DAT and FRILEVUP.DAT rows
   (assets/characters.md), so the roster adds compute the numbers the tables
   really hold rather than zeroes. */
#define RANDIS_CHAR_ID 0
#define RANDIS_LEVEL 1
#define RANDIS_HP_BASE 56
#define RANDIS_MP_BASE 0
#define RANDIS_HP_MIN 9
#define RANDIS_MP_MIN 3

#define JULIAN_CHAR_ID 6
#define JULIAN_LEVEL 3
#define JULIAN_HP_BASE 48
#define JULIAN_MP_BASE 30
#define JULIAN_HP_MIN 8
#define JULIAN_MP_MIN 4

#define ARC_CHAR_ID 4
#define ARC_LEVEL 7
#define ARC_HP_BASE 64
#define ARC_MP_BASE 0
#define ARC_HP_MIN 10
#define ARC_MP_MIN 2

#define FLARENA_CHAR_ID 1
#define FLARENA_LEVEL 3
#define FLARENA_HP_BASE 42
#define FLARENA_MP_BASE 40
#define FLARENA_HP_MIN 6
#define FLARENA_MP_MIN 7

#define JUNA_CHAR_ID 3
#define JUNA_LEVEL 15
#define JUNA_HP_BASE 85
#define JUNA_MP_BASE 0
#define JUNA_HP_MIN 10
#define JUNA_MP_MIN 2

#define FEITAGA_CHAR_ID 2
#define FEITAGA_LEVEL 13
#define FEITAGA_HP_BASE 78
#define FEITAGA_MP_BASE 65
#define FEITAGA_HP_MIN 6
#define FEITAGA_MP_MIN 7

#define BRANDO_CHAR_ID 8
#define BRANDO_LEVEL 14
#define BRANDO_HP_BASE 50
#define BRANDO_MP_BASE 0
#define BRANDO_HP_MIN 9
#define BRANDO_MP_MIN 3

#define GAIA_CHAR_ID 9
#define GAIA_LEVEL 16
#define GAIA_HP_BASE 60
#define GAIA_MP_BASE 0
#define GAIA_HP_MIN 12
#define GAIA_MP_MIN 3

#define QINQIN_CHAR_ID 7
#define QINQIN_LEVEL 15
#define QINQIN_HP_BASE 60
#define QINQIN_MP_BASE 12
#define QINQIN_HP_MIN 12
#define QINQIN_MP_MIN 2

#define MARIAN_CHAR_ID 5
#define MARIAN_LEVEL 10
#define MARIAN_HP_BASE 98
#define MARIAN_MP_BASE 0
#define MARIAN_HP_MIN 8
#define MARIAN_MP_MIN 2

#define LANCELOT_CHAR_ID 11
#define LANCELOT_LEVEL 15
#define LANCELOT_HP_BASE 420
#define LANCELOT_MP_BASE 0
#define LANCELOT_HP_MIN 14
#define LANCELOT_MP_MIN 0

#define SHAN_CHAR_ID 10
#define SHAN_LEVEL 10
#define SHAN_HP_BASE 266
#define SHAN_MP_BASE 232
#define SHAN_HP_MIN 11
#define SHAN_MP_MIN 12

/* MAP24.DAT's own header bytes, read back to prove chapter 25's map is the one
   that loaded: twelve player slots at +1 and fifty-nine scripted deployments
   at +2 (src/rsrc.c).  Both were staged at other numbers before the run.

   Twelve player slots against a roster of twelve is the first time in the game
   the two meet exactly, so no slot falls through to the zeroed, retired spare
   fdps_build_map_unit_array writes for a player slot with no roster member
   behind it (src/deploy.c).

   The MAP24_ prefix is the FILE's name, which is numbered by the 0-based
   chapter id and so agrees with chapter 25's id here. */
#define MAP24_PLAYER_SLOTS 12
#define MAP24_CHAR_SPAWNS 59

/* What the chapter state reset puts on the board, and what the cut-scene adds
   to it: nothing.  MAP24.DAT tags forty-one of its fifty-nine records wave 0,
   which the reset deploys, and the shipped ICON24.DAT has no DEPLOY_WAVE
   anywhere in its 507 bytes when walked with the opcode ladder in src/icon.c.
   So the board this handler returns on is the twelve player slots and the
   map's own opening wave, and nothing else. */
#define MAP24_WAVE_ZERO_UNITS 41
#define CH25_UNITS_AFTER_SCRIPT \
    (PARTY_AT_CHAPTER_25 + MAP24_WAVE_ZERO_UNITS)
#define CH25_ENEMY_SIDE_UNITS MAP24_WAVE_ZERO_UNITS
#define CH25_GUEST_SIDE_UNITS 0

/* The census those forty-one come to, by character id and level, which is the
   strategy guide's 敵方 list for the chapter less the one group the map holds
   back.  The guide's own HP and MV figures settle which id is which:
   ENEMYDAT.DAT is indexed by the character id less 60, holds HP as a per-level
   coefficient and MV outright (assets/characters.md), so row 4 at 180 a level
   is the guide's LV30 塞克斯 HP5400, row 5 at 150 with 100 MP its LV30 布魯森
   HP4500 MP3000, row 6 at 130 with 200 MP its LV30 汎拉沛 HP3900 MP6000,
   row 44 at 40 a level with MV4 its LV16 黑暗祭司 HP640 MV4, row 18 at 45 with
   MV7 its LV16 地獄騎士 HP720 MV7, row 40 at 40 with MV5 its LV16 鎧甲武士
   HP640 MV5 and row 35 at 35 with MV5 its LV16 神箭手 HP560 MV5.

     characters 64, 65 and 66, one each -- LV30 塞克斯, 布魯森, 汎拉沛
     character 104, one                 -- LV16 黑暗祭司
     character 78,  eight               -- LV16 地獄騎士
     character 100, eighteen            -- LV16 鎧甲武士
     character 95,  eleven              -- LV16 神箭手

   Every group the guide lists is on the board at full strength from the start
   but one: the eighteen LV16 天空騎士 are the map's wave 1, which is the
   guide's 事件 reinforcement and no part of what this handler deploys. */
#define MAP24_GENERAL_A_CHAR_ID 64
#define MAP24_GENERAL_B_CHAR_ID 65
#define MAP24_GENERAL_C_CHAR_ID 66
#define MAP24_GENERAL_LEVEL 30
#define MAP24_GENERAL_COUNT 1
#define MAP24_PRIEST_CHAR_ID 104
#define MAP24_PRIEST_COUNT 1
#define MAP24_HELL_KNIGHT_CHAR_ID 78
#define MAP24_HELL_KNIGHT_COUNT 8
#define MAP24_ARMOUR_SAMURAI_CHAR_ID 100
#define MAP24_ARMOUR_SAMURAI_COUNT 18
#define MAP24_SHARPSHOOTER_CHAR_ID 95
#define MAP24_SHARPSHOOTER_COUNT 11
#define MAP24_ENEMY_LEVEL 16

/* Where 塞克斯 lands, and why it is worth pinning by index and not only by
   count.  The reset's own opening deploy appends the map's wave-0 records in
   file order behind the twelve player slots, and record 0 -- the first record
   in the file, and one the map tags wave 0 -- is the LV30 塞克斯.  A run that
   deployed before the board was rebuilt, or that rebuilt the board after the
   cut-scene, would not put him there. */
#define CH25_FIRST_ENEMY_MAP_UNIT PARTY_AT_CHAPTER_25

/* The group the map holds back, and the id that catches it.  MAP24.DAT's wave
   1 is eighteen LV16 天空騎士, the guide's 事件 reinforcement, and the id
   appears in no other record of the file, so any count of it at all is a wave
   this handler must not have deployed. */
#define MAP24_SKY_KNIGHT_CHAR_ID 97

/* Which player slot the cut-scene takes off the board.  ICON24.DAT's one
   RETIRE_UNIT, at script offset 185 and the first opcode after the return to
   map 24, names map unit 3; there is no REVIVE anywhere behind it in its 507
   bytes.  A player slot's map unit index is its roster slot and the roster is
   in join order, so the member taken off is 法蓮娜 and the eleven left are the
   guide's 己方 line for the chapter, 法蓮娜以外的所有人. */
#define SCRIPT_CH25_RETIRED_UNIT 3

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 52 is
   the last of the fifty-three, so a run that left a shorter board does not
   reach it.
   The value is one neither decoy writes.  The slot is status_timers[4], record
   offset 0x26, for the reason tests/chinit1.c gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1.
   The shipped member has no SET_UNIT_TIMER of its own, so this marker is the
   fixture's alone. */
#define SCRIPT_CH25_MARKER_UNIT (CH25_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH25_MARKER_OPERAND 1
#define SCRIPT_CH25_MARKER_SLOT 4
#define SCRIPT_CH25_MARKER_VALUE 124

/* What the two decoys write, and where.  Unit 0 exists whichever member is
   opened, so a decoy's mark lands somewhere every case can read.  Neither
   value is the one the real member writes.  The names are the chapters whose
   scripts they are: ICON23.DAT is chapter 24's and ICON25.DAT is chapter
   26's, one either side of the handler under test. */
#define SCRIPT_DECOY_MARKER_UNIT 0
#define SCRIPT_DECOY_MARKER_OPERAND 1
#define SCRIPT_CH24_DECOY_VALUE 123
#define SCRIPT_CH26_DECOY_VALUE 125

/* Where the cursor ends, and why it is the cut-scene's tile and not the map's.
   ICON24.DAT places map unit 0 on (4, 0) at script offset 273 and then walks
   it through the two group walks at offsets 348 and 364, one tile each with
   the facing operand 0, which src/icon.c's ladder makes a step down.  So unit
   0 is on (4, 2) when the cursor call reads it, and nothing of MAP24.COD's own
   start tile for player slot 0 survives into the answer.  A handler that
   skipped the cursor call leaves the zeroes fdps_chapter_state_reset writes
   over both globals (src/chapter.c); the staging below is a third value again,
   so a run in which the reset never happened is told apart from both. */
#define CH25_CURSOR_TILE_X 4
#define CH25_CURSOR_TILE_Y 2
#define CH25_STAGED_CURSOR_X 336
#define CH25_STAGED_CURSOR_Y 240


static struct fdps_unit_record stage_roster[ROSTER_SLOTS];
static struct fdps_character_base_record stage_char[TABLE_ROWS];
static struct fdps_character_growth stage_growth[TABLE_ROWS];
static struct fdps_enemy_data stage_enemy[TABLE_ROWS];
static struct fdps_item_effect stage_items[ITEM_TABLE_ROWS];
static unsigned char stage_palette[DAC_ENTRIES * 3];

/* ICON23.DAT: the member the handler must NOT open, one chapter early -- it is
   chapter 24's own script.  It deploys nobody, retires nobody, switches no map
   and marks unit 0 with a value of its own. */
static unsigned char fixture_icon23_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH24_DECOY_VALUE,
    0x00
};

/* ICON24.DAT: the member fdps_chapter_25_init names.  The RETIRE is the
   shipped member's own, with its operand, and it carries no REVIVE behind it;
   the PLACE_UNIT stands in for the shipped member's own place-and-walk of unit
   0 and leaves it on the tile they leave it on.  The SET_UNIT_TIMER marker at
   the end is this file's own addition, because the shipped member has none: it
   writes a value of its own onto the last unit the map's opening wave
   appended.  The shipped member's other eighty-odd opcodes walk and pose the
   cast, switch to the cut-scene stage and back, scroll and shake the view, set
   the music and draw chapter text, and are left out for the reasons the file
   comment gives. */
static unsigned char fixture_icon24_dat[] = {
    0x0b, SCRIPT_CH25_RETIRED_UNIT,
    0x0a, 0, CH25_CURSOR_TILE_X, CH25_CURSOR_TILE_Y, 0,
    0x12, SCRIPT_CH25_MARKER_UNIT, SCRIPT_CH25_MARKER_OPERAND,
    SCRIPT_CH25_MARKER_VALUE,
    0x00
};

/* ICON25.DAT: the member the handler must NOT open, one chapter late -- it is
   chapter 26's own script.  It deploys nobody, retires nobody, switches no map
   and marks unit 0 with a value of its own. */
static unsigned char fixture_icon25_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH26_DECOY_VALUE,
    0x00
};

static char *fixture_names[FIXTURE_MEMBERS] = {
    "ICON23.DAT", "ICON24.DAT", "ICON25.DAT"
};

static unsigned char *fixture_bytes[FIXTURE_MEMBERS] = {
    fixture_icon23_dat, fixture_icon24_dat, fixture_icon25_dat
};

static int fixture_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_icon23_dat), sizeof(fixture_icon24_dat),
    sizeof(fixture_icon25_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run25_state = 0;

/* Whether this file created the container, and so whether it may remove it. */
static int fixture_owned = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order: chapters 1 to 4 one each, chapter 7 裘娜, chapter 8 費塔加, chapter 9
   布蘭多 and 蓋亞, chapter 11 琴琴, chapter 15 瑪麗安, chapter 19 蘭斯洛特 and
   chapter 24 珊. */
static int chapter_25_party[PARTY_AT_CHAPTER_25] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID, MARIAN_CHAR_ID, LANCELOT_CHAR_ID, SHAN_CHAR_ID
};

/* Everything the cases assert, captured the instant the handler returned. */
static int seen25_roster_count;
static int seen25_player_slots;
static int seen25_char_spawns;
static int seen25_unit_count;
static int seen25_party_char_ids[PARTY_AT_CHAPTER_25];
static int seen25_party_sides[PARTY_AT_CHAPTER_25];
static int seen25_party_flags[PARTY_AT_CHAPTER_25];
static int seen25_general_a;
static int seen25_general_b;
static int seen25_general_c;
static int seen25_priests;
static int seen25_hell_knights;
static int seen25_armour_samurai;
static int seen25_sharpshooters;
static int seen25_sky_knights;
static int seen25_enemy_side_units;
static int seen25_guest_side_units;
static int seen25_first_enemy_char_id;
static int seen25_first_enemy_level;
static unsigned char seen25_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen25_unit0_timers[STATUS_TIMER_COUNT];
static int seen25_cursor_x;
static int seen25_cursor_y;
static int seen25_chapter_id;

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
   another test file's fixture, and neither may be clobbered. */
static int stage_fixture_archive(void)
{
    FILE *fp;
    long member_at;
    int i;

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

/* One character's own two table rows, so the roster add computes the numbers
   assets/characters.md records rather than zeroes. */
static void stage_character(int char_id, int level, int hp_base, int mp_base,
                            int hp_min, int mp_min)
{
    stage_char[char_id].level = (unsigned char) level;
    stage_char[char_id].hp_base = (short) hp_base;
    stage_char[char_id].mp_base = (short) mp_base;
    stage_growth[char_id].hp_min = (unsigned char) hp_min;
    stage_growth[char_id].mp_min = (unsigned char) mp_min;
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

    stage_character(RANDIS_CHAR_ID, RANDIS_LEVEL, RANDIS_HP_BASE,
                    RANDIS_MP_BASE, RANDIS_HP_MIN, RANDIS_MP_MIN);
    stage_character(JULIAN_CHAR_ID, JULIAN_LEVEL, JULIAN_HP_BASE,
                    JULIAN_MP_BASE, JULIAN_HP_MIN, JULIAN_MP_MIN);
    stage_character(ARC_CHAR_ID, ARC_LEVEL, ARC_HP_BASE,
                    ARC_MP_BASE, ARC_HP_MIN, ARC_MP_MIN);
    stage_character(FLARENA_CHAR_ID, FLARENA_LEVEL, FLARENA_HP_BASE,
                    FLARENA_MP_BASE, FLARENA_HP_MIN, FLARENA_MP_MIN);
    stage_character(JUNA_CHAR_ID, JUNA_LEVEL, JUNA_HP_BASE,
                    JUNA_MP_BASE, JUNA_HP_MIN, JUNA_MP_MIN);
    stage_character(FEITAGA_CHAR_ID, FEITAGA_LEVEL, FEITAGA_HP_BASE,
                    FEITAGA_MP_BASE, FEITAGA_HP_MIN, FEITAGA_MP_MIN);
    stage_character(BRANDO_CHAR_ID, BRANDO_LEVEL, BRANDO_HP_BASE,
                    BRANDO_MP_BASE, BRANDO_HP_MIN, BRANDO_MP_MIN);
    stage_character(GAIA_CHAR_ID, GAIA_LEVEL, GAIA_HP_BASE,
                    GAIA_MP_BASE, GAIA_HP_MIN, GAIA_MP_MIN);
    stage_character(QINQIN_CHAR_ID, QINQIN_LEVEL, QINQIN_HP_BASE,
                    QINQIN_MP_BASE, QINQIN_HP_MIN, QINQIN_MP_MIN);
    stage_character(MARIAN_CHAR_ID, MARIAN_LEVEL, MARIAN_HP_BASE,
                    MARIAN_MP_BASE, MARIAN_HP_MIN, MARIAN_MP_MIN);
    stage_character(LANCELOT_CHAR_ID, LANCELOT_LEVEL, LANCELOT_HP_BASE,
                    LANCELOT_MP_BASE, LANCELOT_HP_MIN, LANCELOT_MP_MIN);
    stage_character(SHAN_CHAR_ID, SHAN_LEVEL, SHAN_HP_BASE,
                    SHAN_MP_BASE, SHAN_HP_MIN, SHAN_MP_MIN);

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

    /* Both are staged at numbers MAP24.DAT does not carry -- it says 12 and 59
       -- so reading its own pair back says the chapter really loaded. */
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

static void capture_chapter_25(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    struct fdps_unit_record *first_enemy;
    int unit_index;
    int char_id;
    int level;
    int slot;

    seen25_roster_count = data_fdps_roster_member_count;
    seen25_player_slots = data_fdps_map_player_slot_count;
    seen25_char_spawns = data_fdps_map_char_spawn_count;
    seen25_unit_count = data_fdps_map_unit_count;
    seen25_cursor_x = data_fdps_map_cursor_world_x;
    seen25_cursor_y = data_fdps_map_cursor_world_y;
    seen25_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_25; slot++) {
        seen25_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen25_party_sides[slot] = (int) unit0[slot].side;
        seen25_party_flags[slot] = (int) unit0[slot].flags;
    }

    seen25_general_a = 0;
    seen25_general_b = 0;
    seen25_general_c = 0;
    seen25_priests = 0;
    seen25_hell_knights = 0;
    seen25_armour_samurai = 0;
    seen25_sharpshooters = 0;
    seen25_sky_knights = 0;
    seen25_enemy_side_units = 0;
    seen25_guest_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_25;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen25_enemy_side_units++;
        } else if (unit0[unit_index].side == GUEST_SIDE) {
            seen25_guest_side_units++;
        }
        if (level == MAP24_GENERAL_LEVEL) {
            if (char_id == MAP24_GENERAL_A_CHAR_ID) {
                seen25_general_a++;
            } else if (char_id == MAP24_GENERAL_B_CHAR_ID) {
                seen25_general_b++;
            } else if (char_id == MAP24_GENERAL_C_CHAR_ID) {
                seen25_general_c++;
            }
        } else if (level == MAP24_ENEMY_LEVEL) {
            if (char_id == MAP24_PRIEST_CHAR_ID) {
                seen25_priests++;
            } else if (char_id == MAP24_HELL_KNIGHT_CHAR_ID) {
                seen25_hell_knights++;
            } else if (char_id == MAP24_ARMOUR_SAMURAI_CHAR_ID) {
                seen25_armour_samurai++;
            } else if (char_id == MAP24_SHARPSHOOTER_CHAR_ID) {
                seen25_sharpshooters++;
            } else if (char_id == MAP24_SKY_KNIGHT_CHAR_ID) {
                seen25_sky_knights++;
            }
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen25_marked_timers[slot] = 0;
        seen25_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* Both reads are guarded by the unit count: a run that opened a member
       leaving a shorter array would not have the index, and reading past it
       would be reading memory the allocation does not cover.  The cases below
       fail on the zero that is left instead. */
    seen25_first_enemy_char_id = 0;
    seen25_first_enemy_level = 0;
    if (data_fdps_map_unit_count > CH25_FIRST_ENEMY_MAP_UNIT) {
        first_enemy = unit0 + CH25_FIRST_ENEMY_MAP_UNIT;
        seen25_first_enemy_char_id = (int) first_enemy->char_id;
        seen25_first_enemy_level = (int) first_enemy->level;
    }

    if (data_fdps_map_unit_count > SCRIPT_CH25_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH25_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen25_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 25 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The twelve members the
   party has when the chapter opens are staged first, in join order, because
   this handler adds nobody itself. */
static void run_chapter_25_handler(void)
{
    int member;

    if (run25_state != 0) {
        return;
    }
    run25_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_map_cursor_world_x = CH25_STAGED_CURSOR_X;
    data_fdps_map_cursor_world_y = CH25_STAGED_CURSOR_Y;
    data_fdps_chapter_current_chapter_id = CHAPTER_25_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_25; member++) {
        fdps_roster_add_character(chapter_25_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_25_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_25();
    free_chapter_globals();
    run25_state = 1;
}

/* Nobody joins this chapter and every player slot has a member behind it.  The
   roster count still reading twelve is what says the handler made no add --
   there is no fdps_roster_add_character in its body -- and the two map header
   counts are read back as well because they were staged at numbers no map
   carries: MAP24.DAT's own twelve and fifty-nine say chapter 25's map is the
   one that loaded.  Twelve slots against twelve members is the first exact
   meeting in the game, so no slot falls through to the zeroed, retired spare
   fdps_build_map_unit_array writes for a slot with no member behind it. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_twenty_five(void)
{
    run_chapter_25_handler();
    CHECK_EQ(run25_state, 1);
    if (run25_state != 1) {
        return;
    }

    CHECK_EQ(seen25_roster_count, PARTY_AT_CHAPTER_25);
    CHECK_EQ(seen25_player_slots, MAP24_PLAYER_SLOTS);
    CHECK_EQ(seen25_char_spawns, MAP24_CHAR_SPAWNS);
}

/* The chapter is fought without 法蓮娜, and it is the cut-scene that takes her
   off: map unit 3 carries the retired bit when the handler returns -- the
   RETIRE ICON24.DAT runs as its first opcode after the return to map 24, with
   no REVIVE behind it -- and the other eleven player slots carry a clear flags
   byte, which also says no slot fell through to the zeroed, retired spare.
   Read through the join order that is the guide's 己方 line for the chapter,
   法蓮娜以外的所有人. */
static void chapter_twenty_five_is_fought_without_flarena(void)
{
    int slot;

    run_chapter_25_handler();
    CHECK_EQ(run25_state, 1);
    if (run25_state != 1) {
        return;
    }

    for (slot = 0; slot < PARTY_AT_CHAPTER_25; slot++) {
        CHECK_EQ(seen25_party_char_ids[slot], chapter_25_party[slot]);
        CHECK_EQ(seen25_party_sides[slot], PLAYER_SIDE);
        if (slot != SCRIPT_CH25_RETIRED_UNIT) {
            CHECK_EQ(seen25_party_flags[slot], 0);
        }
    }

    CHECK_EQ(seen25_party_char_ids[SCRIPT_CH25_RETIRED_UNIT], FLARENA_CHAR_ID);
    CHECK_EQ(seen25_party_flags[SCRIPT_CH25_RETIRED_UNIT], UNIT_FLAG_RETIRED);
}

/* What the reset leaves on the map, the cut-scene adding nothing to it: the
   twelve player slots and MAP24.DAT's forty-one wave-0 records, for
   fifty-three.  The census behind those forty-one is the strategy guide's
   敵方 list for the chapter found by character id and level, less the one
   group the map holds back -- one each of the LV30 塞克斯, 布魯森 and 汎拉沛,
   one LV16 黑暗祭司, eight LV16 地獄騎士, eighteen LV16 鎧甲武士 and eleven
   LV16 神箭手 -- and all forty-one are on the enemy side, with no guest at all.

   塞克斯 is asserted at map unit 12 by id and level and not merely counted,
   because that index is what says the reset's own deploy ran behind the player
   slots and in the map's record order: it is the first unit behind the twelve,
   and record 0 is the first record of the file.

   The 天空騎士 census says the chapter's later wave has not fired.  Those
   eighteen are MAP24.DAT's wave 1, the guide's 事件 reinforcement, and the id
   appears in no wave-0 record at all. */
static void chapter_twenty_five_opens_with_the_guides_enemy_group(void)
{
    run_chapter_25_handler();
    CHECK_EQ(run25_state, 1);
    if (run25_state != 1) {
        return;
    }

    CHECK_EQ(seen25_unit_count, CH25_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen25_enemy_side_units, CH25_ENEMY_SIDE_UNITS);
    CHECK_EQ(seen25_guest_side_units, CH25_GUEST_SIDE_UNITS);

    CHECK_EQ(seen25_first_enemy_char_id, MAP24_GENERAL_A_CHAR_ID);
    CHECK_EQ(seen25_first_enemy_level, MAP24_GENERAL_LEVEL);

    CHECK_EQ(seen25_general_a, MAP24_GENERAL_COUNT);
    CHECK_EQ(seen25_general_b, MAP24_GENERAL_COUNT);
    CHECK_EQ(seen25_general_c, MAP24_GENERAL_COUNT);

    CHECK_EQ(seen25_priests, MAP24_PRIEST_COUNT);
    CHECK_EQ(seen25_hell_knights, MAP24_HELL_KNIGHT_COUNT);
    CHECK_EQ(seen25_armour_samurai, MAP24_ARMOUR_SAMURAI_COUNT);
    CHECK_EQ(seen25_sharpshooters, MAP24_SHARPSHOOTER_COUNT);

    CHECK_EQ(seen25_sky_knights, 0);
}

/* The cut-scene the handler names is Icon24.dat and not the neighbour on
   either side of it.  The marker sits on unit 52 with the value only
   ICON24.DAT writes, and neither decoy writes anything there: each marks unit
   0 with a value of its own, so unit 0's timers are read back clear as the
   other half of the same question.  Every other timer on the marked unit is
   clear too: neither the handler nor the shipped cut-scene writes a status on
   anybody this chapter, ICON24.DAT having no SET_UNIT_TIMER in its 507 bytes.

   The chapter id still reading 24 is what says the run ended on chapter 25's
   own map.  The shipped member switches the id to 56 and back again itself, so
   this is the value the script leaves rather than one nothing touched; both
   the member name and the title-card graphic are chosen from it by the
   callees. */
static void the_chapter_25_cutscene_is_icon24_dat(void)
{
    int slot;

    run_chapter_25_handler();
    CHECK_EQ(run25_state, 1);
    if (run25_state != 1) {
        return;
    }

    CHECK_EQ(seen25_marked_timers[SCRIPT_CH25_MARKER_SLOT],
             SCRIPT_CH25_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH25_MARKER_SLOT) {
            CHECK_EQ(seen25_marked_timers[slot], 0);
        }
        CHECK_EQ(seen25_unit0_timers[slot], 0);
    }

    CHECK_EQ(seen25_chapter_id, CHAPTER_25_ID);
}

/* The cursor ends on unit 0's tile, and on this chapter that is the tile the
   cut-scene walked unit 0 to, (4, 2): ICON24.DAT places unit 0 on (4, 0) and
   then steps it down twice, so nothing of MAP24.COD's start tile for player
   slot 0 survives into the answer.  What the case pins is the call and its
   operand -- unit 0 is the only unit the fixture moves, so a handler that
   named any other one reads that slot's MAP24.COD start tile instead, and none
   of the twelve is (4, 2) -- and a handler that made no cursor call at all
   leaves the zeroes
   fdps_chapter_state_reset writes over both globals, with the staged (14, 10)
   a third value again. */
static void the_chapter_25_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_25_handler();
    CHECK_EQ(run25_state, 1);
    if (run25_state != 1) {
        return;
    }

    CHECK_EQ(seen25_cursor_x, CH25_CURSOR_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen25_cursor_y, CH25_CURSOR_TILE_Y * CURSOR_TILE_STEP);
}

/* Takes the fixture container away again, so a later test file can stage its
   own.  A container this file did not create is somebody else's and is left
   where it stands, which is also the only path on which this case asserts
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

/* --- fdps_chapter_26_init @ 00021510 constants -------------------------- */

/* Chapter 26 is chapter id 25, and this handler is slot 25 of the table at
   00060074 -- the dword at 000600d8 is 00021510, and that data reference is
   the function's only xref. */
#define CHAPTER_26_ID 25

/* The party when chapter 26 opens.  It is the same twelve chapter 25 opened
   with: 珊, added by chapter 24's handler, is the last member to join in the
   game, and neither chapter 25's handler nor this one has a
   fdps_roster_add_character in its body. */
#define PARTY_AT_CHAPTER_26 12

/* MAP25.DAT's own header bytes, read back to prove chapter 26's map is the one
   that loaded: twelve player slots at +1 and eighty scripted deployments at +2
   (src/rsrc.c).  Both were staged at other numbers before the run.

   Twelve player slots against a roster of twelve means no slot falls through
   to the zeroed, retired spare fdps_build_map_unit_array writes for a player
   slot with no roster member behind it (src/deploy.c). */
#define MAP25_PLAYER_SLOTS 12
#define MAP25_CHAR_SPAWNS 80

/* What the chapter state reset puts on the board, and what the cut-scene adds
   to it: nothing.  A deployment record's wave tag is byte 21 of the 26-byte
   struct fdps_char_spawn_record, and MAP25.DAT tags sixty-eight of its eighty
   records wave 0, which the reset deploys behind the twelve player slots for
   eighty map units in all.  The shipped ICON25.DAT has no DEPLOY_WAVE anywhere
   in its 1,564 bytes when walked with the opcode ladder in src/icon.c, so the
   board this handler returns on is those eighty and nothing else.

   Chapter 26 is the chapter that opens with almost everything already on the
   field: only twelve of the eighty records are held back. */
#define MAP25_WAVE_ZERO_UNITS 68
#define CH26_UNITS_AFTER_SCRIPT \
    (PARTY_AT_CHAPTER_26 + MAP25_WAVE_ZERO_UNITS)
#define CH26_ENEMY_SIDE_UNITS MAP25_WAVE_ZERO_UNITS
#define CH26_GUEST_SIDE_UNITS 0

/* The census those sixty-eight come to, by character id and level, which is
   the strategy guide's 敵方 list for the chapter less the one group the map
   holds back.  ENEMYDAT.DAT is indexed by the character id less 60 and holds
   HP and MP as per-level coefficients with MV outright
   (assets/characters.md), which settles which id is which against the guide's
   own figures: row 7 at 210 HP and 150 MP a level is the LV30 凱因巴
   HP6300 MP4500, row 4 at 180 the LV30 塞克斯 HP5400, row 5 at 150 with 100
   MP the LV30 布魯森 HP4500 MP3000 and row 6 at 130 with 200 MP the LV30
   汎拉沛 HP3900 MP6000; row 44 at 40 HP and 30 MP with MV4 is the LV18
   黑暗祭司 HP720 MP540, row 18 at 45 with MV7 the LV18 地獄騎士 HP810,
   row 35 at 35 with MV5 the LV18 神箭手 HP630, row 40 at 40 with MV5 the
   LV18 鎧甲武士 HP720 and row 37 at 32 with MV8 the LV18 天空騎士 HP576
   MV8.

     characters 67, 64, 65 and 66, one each
                              -- LV30 凱因巴, 塞克斯, 布魯森, 汎拉沛
     character 104, four      -- LV18 黑暗祭司
     character 78,  ten       -- LV18 地獄騎士
     character 95,  ten       -- LV18 神箭手
     character 100, twenty-four -- LV18 鎧甲武士
     character 97,  sixteen   -- LV18 天空騎士

   The nine counts sum to the sixty-eight, so asserting all of them pins the
   census exactly rather than merely bounding it.  Every group the guide lists
   is on the board at full strength from the first turn but one: the guide's
   own second 鎧甲武士 line, seven more of them, is the map's wave 2. */
#define MAP25_GENERAL_A_CHAR_ID 64
#define MAP25_GENERAL_B_CHAR_ID 65
#define MAP25_GENERAL_C_CHAR_ID 66
#define MAP25_GENERAL_D_CHAR_ID 67
#define MAP25_GENERAL_LEVEL 30
#define MAP25_GENERAL_COUNT 1
#define MAP25_PRIEST_CHAR_ID 104
#define MAP25_PRIEST_COUNT 4
#define MAP25_HELL_KNIGHT_CHAR_ID 78
#define MAP25_HELL_KNIGHT_COUNT 10
#define MAP25_SHARPSHOOTER_CHAR_ID 95
#define MAP25_SHARPSHOOTER_COUNT 10
#define MAP25_ARMOUR_SAMURAI_CHAR_ID 100
#define MAP25_ARMOUR_SAMURAI_COUNT 24
#define MAP25_SKY_KNIGHT_CHAR_ID 97
#define MAP25_SKY_KNIGHT_COUNT 16
#define MAP25_ENEMY_LEVEL 18

/* Where 塞克斯 lands, and why it is worth pinning by index and not only by
   count.  The reset's own opening deploy appends the map's wave-0 records in
   file order behind the twelve player slots, and record 0 -- the first record
   in the file, and one the map tags wave 0 -- is the LV30 塞克斯.  A run that
   deployed before the board was rebuilt, or that rebuilt the board after the
   cut-scene, would not put him there. */
#define CH26_FIRST_ENEMY_MAP_UNIT PARTY_AT_CHAPTER_26

/* What the map holds back, and the ids that catch it.  Twelve of the eighty
   records are not wave 0: seven more LV18 鎧甲武士 in wave 2, which is the
   guide's second 鎧甲武士 line and its 事件 reinforcement, and five wave-3
   records that are the guide's 友方 -- the LV40 英雄索爾 and four LV40 侍衛,
   all on the guest side.  The seven share their character id with the
   twenty-four already down, so what catches them is the count reading
   twenty-four and not thirty-one; the two ally ids appear in no wave-0 record
   at all, so any count of either is a wave this handler must not have
   deployed. */
#define MAP25_HERO_SOL_CHAR_ID 12
#define MAP25_GUARD_CHAR_ID 59

/* Which player slot the cut-scene leaves off the board.  ICON25.DAT retires
   map units 1 to 11 at script offsets 7 to 27, so that only 蘭迪斯 is drawn
   through the opening, and then revives 1, 2 and 4 to 11 at offsets 696 to
   714.  Map unit 3 is the one index the revive run skips and nothing revives
   it later.  A player slot's map unit index is its roster slot and the roster
   is in join order, so the member left off is 法蓮娜 and the eleven remaining
   are the guide's 己方 line for the chapter, 法蓮娜以外的所有人. */
#define SCRIPT_CH26_LEFT_OFF_UNIT 3

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 79 is
   the last of the eighty, so a run that left a shorter board does not reach
   it.
   The value is one neither decoy writes.  The slot is status_timers[4], record
   offset 0x26, for the reason tests/chinit1.c gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1.
   The shipped member has no SET_UNIT_TIMER of its own, so this marker is the
   fixture's alone. */
#define SCRIPT_CH26_MARKER_UNIT (CH26_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH26_MARKER_OPERAND 1
#define SCRIPT_CH26_MARKER_SLOT 4
#define SCRIPT_CH26_MARKER_VALUE 126

/* What the two decoys in this chapter's container write, and where.  Unit 0
   exists whichever member is opened, so a decoy's mark lands somewhere every
   case can read, and neither value is the one the real member writes.  The
   names are the chapters whose scripts they are: ICON24.DAT is chapter 25's
   and ICON26.DAT is chapter 27's, one either side of the handler under
   test. */
#define SCRIPT_CH25_DECOY_VALUE 124
#define SCRIPT_CH27_DECOY_VALUE 127

/* Where the cursor ends, and why it is the cut-scene's tile and not the map's.
   MAP25.COD starts player slot 0 on (6, 36) -- the record at
   0xb + (80 + 0) * 6, read as two signed words (src/deploy.c) -- and
   ICON25.DAT walks map unit 0 four tiles with facing 2 at script offset 67,
   which src/icon.c's ladder makes a step up each.  Nothing places unit 0
   outright anywhere in the member, so (6, 32) is a tile only the map's start
   record and the cut-scene's walk together arrive at.  A handler that skipped
   the cursor call leaves the zeroes fdps_chapter_state_reset writes over both
   globals (src/chapter.c); the staging below is a third value again, so a run
   in which the reset never happened is told apart from both. */
#define CH26_START_TILE_X 6
#define CH26_START_TILE_Y 36
#define CH26_WALK_TILES 4
#define CH26_CURSOR_TILE_X CH26_START_TILE_X
#define CH26_CURSOR_TILE_Y (CH26_START_TILE_Y - CH26_WALK_TILES)
#define CH26_STAGED_CURSOR_X 336
#define CH26_STAGED_CURSOR_Y 240

/* How many rendered frames each walk sub-step of the fixture's own walk is
   held for.  The shipped member asks for four, which at six sub-steps a tile
   and four tiles is ninety-six frames of waiting on the timer; one is the
   shortest hold that still moves the unit, because the position update sits
   inside the hold loop and a hold of zero walks nowhere (src/icon.c).  Tile
   count and facing are the shipped member's own, so the tile the case expects
   is the tile the game arrives at. */
#define SCRIPT_CH26_WALK_HOLD_FRAMES 1
#define SCRIPT_CH26_WALK_FACING_UP 2


/* ICON24.DAT: the member the handler must NOT open, one chapter early -- it is
   chapter 25's own script.  It retires nobody, revives nobody, walks nobody
   and marks unit 0 with a value of its own. */
static unsigned char fixture26_icon24_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH25_DECOY_VALUE,
    0x00
};

/* ICON25.DAT: the member fdps_chapter_26_init names.  The eleven RETIREs and
   the ten REVIVEs are the shipped member's own, with their operands and in the
   shipped order, and they are what leaves map unit 3 off the board; the WALK
   is the shipped member's own walk of unit 0, four tiles with facing 2, with
   only the per-sub-step hold shortened.  The SET_UNIT_TIMER marker at the end
   is this file's own addition, because the shipped member has none: it writes
   a value of its own onto the last unit the map's opening wave appended.  The
   shipped member's other hundred-odd opcodes fade, scroll, shake and pose the
   cast, set the music and draw chapter text, and are left out for the reasons
   the file comment gives. */
static unsigned char fixture26_icon25_dat[] = {
    0x0b, 1, 0x0b, 2, 0x0b, 3, 0x0b, 4, 0x0b, 5, 0x0b, 6,
    0x0b, 7, 0x0b, 8, 0x0b, 9, 0x0b, 10, 0x0b, 11,
    0x01, SCRIPT_CH26_WALK_HOLD_FRAMES, CH26_WALK_TILES, 1,
    0, SCRIPT_CH26_WALK_FACING_UP,
    0x0c, 1, 0x0c, 2, 0x0c, 4, 0x0c, 5, 0x0c, 6,
    0x0c, 7, 0x0c, 8, 0x0c, 9, 0x0c, 10, 0x0c, 11,
    0x12, SCRIPT_CH26_MARKER_UNIT, SCRIPT_CH26_MARKER_OPERAND,
    SCRIPT_CH26_MARKER_VALUE,
    0x00
};

/* ICON26.DAT: the member the handler must NOT open, one chapter late -- it is
   chapter 27's own script, and the one a rewrite that read the chapter number
   out of the handler's own name would reach for.  It retires nobody, revives
   nobody, walks nobody and marks unit 0 with a value of its own. */
static unsigned char fixture26_icon26_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH27_DECOY_VALUE,
    0x00
};

static char *fixture26_names[FIXTURE_MEMBERS] = {
    "ICON24.DAT", "ICON25.DAT", "ICON26.DAT"
};

static unsigned char *fixture26_bytes[FIXTURE_MEMBERS] = {
    fixture26_icon24_dat, fixture26_icon25_dat, fixture26_icon26_dat
};

static int fixture26_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture26_icon24_dat), sizeof(fixture26_icon25_dat),
    sizeof(fixture26_icon26_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run26_state = 0;

/* Whether this half of the file created the container, and so whether it may
   remove it. */
static int fixture26_owned = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order.  It is chapter 25's list unchanged, because nobody joined in
   between. */
static int chapter_26_party[PARTY_AT_CHAPTER_26] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID, MARIAN_CHAR_ID, LANCELOT_CHAR_ID, SHAN_CHAR_ID
};

/* Everything the chapter 26 cases assert, captured the instant the handler
   returned. */
static int seen26_roster_count;
static int seen26_player_slots;
static int seen26_char_spawns;
static int seen26_unit_count;
static int seen26_party_char_ids[PARTY_AT_CHAPTER_26];
static int seen26_party_sides[PARTY_AT_CHAPTER_26];
static int seen26_party_flags[PARTY_AT_CHAPTER_26];
static int seen26_general_a;
static int seen26_general_b;
static int seen26_general_c;
static int seen26_general_d;
static int seen26_priests;
static int seen26_hell_knights;
static int seen26_sharpshooters;
static int seen26_armour_samurai;
static int seen26_sky_knights;
static int seen26_allies;
static int seen26_enemy_side_units;
static int seen26_guest_side_units;
static int seen26_first_enemy_char_id;
static int seen26_first_enemy_level;
static unsigned char seen26_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen26_unit0_timers[STATUS_TIMER_COUNT];
static int seen26_cursor_x;
static int seen26_cursor_y;
static int seen26_chapter_id;

/* Builds chapter 26's fixture container, or answers no.  Same refusal as the
   chapter 25 staging above: a file of that name that was already there is
   either the shipped 4.7 MB container or another test file's fixture, and
   neither may be clobbered.  The chapter 25 case that removes its own
   container runs before this one, so the two never collide. */
static int stage_chapter_26_archive(void)
{
    FILE *fp;
    long member_at;
    int i;

    if (fixture26_owned) {
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
        write_name(fp, fixture26_names[i]);
        write_dword(fp, (long) fixture26_lengths[i]);
        write_dword(fp, (long) fixture26_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture26_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture26_bytes[i], 1, (size_t) fixture26_lengths[i], fp);
    }
    fclose(fp);

    fixture26_owned = 1;
    return 1;
}

static void capture_chapter_26(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    struct fdps_unit_record *first_enemy;
    int unit_index;
    int char_id;
    int level;
    int slot;

    seen26_roster_count = data_fdps_roster_member_count;
    seen26_player_slots = data_fdps_map_player_slot_count;
    seen26_char_spawns = data_fdps_map_char_spawn_count;
    seen26_unit_count = data_fdps_map_unit_count;
    seen26_cursor_x = data_fdps_map_cursor_world_x;
    seen26_cursor_y = data_fdps_map_cursor_world_y;
    seen26_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_26; slot++) {
        seen26_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen26_party_sides[slot] = (int) unit0[slot].side;
        seen26_party_flags[slot] = (int) unit0[slot].flags;
    }

    seen26_general_a = 0;
    seen26_general_b = 0;
    seen26_general_c = 0;
    seen26_general_d = 0;
    seen26_priests = 0;
    seen26_hell_knights = 0;
    seen26_sharpshooters = 0;
    seen26_armour_samurai = 0;
    seen26_sky_knights = 0;
    seen26_allies = 0;
    seen26_enemy_side_units = 0;
    seen26_guest_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_26;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen26_enemy_side_units++;
        } else if (unit0[unit_index].side == GUEST_SIDE) {
            seen26_guest_side_units++;
        }
        if (char_id == MAP25_HERO_SOL_CHAR_ID
            || char_id == MAP25_GUARD_CHAR_ID) {
            seen26_allies++;
        }
        if (level == MAP25_GENERAL_LEVEL) {
            if (char_id == MAP25_GENERAL_A_CHAR_ID) {
                seen26_general_a++;
            } else if (char_id == MAP25_GENERAL_B_CHAR_ID) {
                seen26_general_b++;
            } else if (char_id == MAP25_GENERAL_C_CHAR_ID) {
                seen26_general_c++;
            } else if (char_id == MAP25_GENERAL_D_CHAR_ID) {
                seen26_general_d++;
            }
        } else if (level == MAP25_ENEMY_LEVEL) {
            if (char_id == MAP25_PRIEST_CHAR_ID) {
                seen26_priests++;
            } else if (char_id == MAP25_HELL_KNIGHT_CHAR_ID) {
                seen26_hell_knights++;
            } else if (char_id == MAP25_SHARPSHOOTER_CHAR_ID) {
                seen26_sharpshooters++;
            } else if (char_id == MAP25_ARMOUR_SAMURAI_CHAR_ID) {
                seen26_armour_samurai++;
            } else if (char_id == MAP25_SKY_KNIGHT_CHAR_ID) {
                seen26_sky_knights++;
            }
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen26_marked_timers[slot] = 0;
        seen26_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* Both reads are guarded by the unit count: a run that opened a member
       leaving a shorter array would not have the index, and reading past it
       would be reading memory the allocation does not cover.  The cases below
       fail on the zero that is left instead. */
    seen26_first_enemy_char_id = 0;
    seen26_first_enemy_level = 0;
    if (data_fdps_map_unit_count > CH26_FIRST_ENEMY_MAP_UNIT) {
        first_enemy = unit0 + CH26_FIRST_ENEMY_MAP_UNIT;
        seen26_first_enemy_char_id = (int) first_enemy->char_id;
        seen26_first_enemy_level = (int) first_enemy->level;
    }

    if (data_fdps_map_unit_count > SCRIPT_CH26_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH26_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen26_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 26 handler once, against the shipped containers and this
   chapter's fixture cut-scene, and records what it left behind.  The twelve
   members the party has when the chapter opens are staged first, in join
   order, because this handler adds nobody itself. */
static void run_chapter_26_handler(void)
{
    int member;

    if (run26_state != 0) {
        return;
    }
    run26_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_chapter_26_archive()) {
        return;
    }

    stage_globals();
    data_fdps_map_cursor_world_x = CH26_STAGED_CURSOR_X;
    data_fdps_map_cursor_world_y = CH26_STAGED_CURSOR_Y;
    data_fdps_chapter_current_chapter_id = CHAPTER_26_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_26; member++) {
        fdps_roster_add_character(chapter_26_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_26_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_26();
    free_chapter_globals();
    run26_state = 1;
}

/* Nobody joins this chapter either, and every player slot has a member behind
   it.  The roster count still reading twelve is what says the handler made no
   add -- there is no fdps_roster_add_character in its body -- and the two map
   header counts are read back as well because they were staged at numbers no
   map carries: MAP25.DAT's own twelve and eighty say chapter 26's map is the
   one that loaded.  Twelve slots against twelve members means no slot falls
   through to the zeroed, retired spare fdps_build_map_unit_array writes for a
   slot with no member behind it. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_twenty_six(void)
{
    run_chapter_26_handler();
    CHECK_EQ(run26_state, 1);
    if (run26_state != 1) {
        return;
    }

    CHECK_EQ(seen26_roster_count, PARTY_AT_CHAPTER_26);
    CHECK_EQ(seen26_player_slots, MAP25_PLAYER_SLOTS);
    CHECK_EQ(seen26_char_spawns, MAP25_CHAR_SPAWNS);
}

/* The chapter is fought without 法蓮娜, and it is the cut-scene that leaves
   her off: ICON25.DAT retires map units 1 to 11 and then revives all of them
   but 3, so map unit 3 carries the retired bit when the handler returns and
   the other eleven player slots carry a clear flags byte -- which also says no
   slot fell through to the zeroed, retired spare.  Read through the join order
   that is the guide's 己方 line for the chapter, 法蓮娜以外的所有人. */
static void chapter_twenty_six_is_fought_without_flarena(void)
{
    int slot;

    run_chapter_26_handler();
    CHECK_EQ(run26_state, 1);
    if (run26_state != 1) {
        return;
    }

    for (slot = 0; slot < PARTY_AT_CHAPTER_26; slot++) {
        CHECK_EQ(seen26_party_char_ids[slot], chapter_26_party[slot]);
        CHECK_EQ(seen26_party_sides[slot], PLAYER_SIDE);
        if (slot != SCRIPT_CH26_LEFT_OFF_UNIT) {
            CHECK_EQ(seen26_party_flags[slot], 0);
        }
    }

    CHECK_EQ(seen26_party_char_ids[SCRIPT_CH26_LEFT_OFF_UNIT],
             FLARENA_CHAR_ID);
    CHECK_EQ(seen26_party_flags[SCRIPT_CH26_LEFT_OFF_UNIT],
             UNIT_FLAG_RETIRED);
}

/* What the reset leaves on the map, the cut-scene adding nothing to it: the
   twelve player slots and MAP25.DAT's sixty-eight wave-0 records, for eighty.
   The census behind those sixty-eight is the strategy guide's 敵方 list for
   the chapter found by character id and level, less the one group the map
   holds back -- one each of the LV30 凱因巴, 塞克斯, 布魯森 and 汎拉沛,
   four LV18 黑暗祭司, ten LV18 地獄騎士, ten LV18 神箭手, twenty-four
   LV18 鎧甲武士 and sixteen LV18 天空騎士 -- and all sixty-eight are on
   the enemy side, with no guest at all.  The nine counts sum to the
   sixty-eight, so the census is pinned exactly.

   塞克斯 is asserted at map unit 12 by id and level and not merely counted,
   because that index is what says the reset's own deploy ran behind the player
   slots and in the map's record order: it is the first unit behind the twelve,
   and record 0 is the first record of the file.

   The 鎧甲武士 count reading twenty-four and not thirty-one is what says the
   chapter's later wave has not fired: the seven the map holds back share their
   character id with the twenty-four already down, so nothing but the count
   tells them apart.  The two ally ids catch the wave behind that one -- the
   LV40 英雄索爾 and four LV40 侍衛 of wave 3, which neither this handler nor
   its cut-scene deploys. */
static void chapter_twenty_six_opens_with_the_guides_enemy_group(void)
{
    run_chapter_26_handler();
    CHECK_EQ(run26_state, 1);
    if (run26_state != 1) {
        return;
    }

    CHECK_EQ(seen26_unit_count, CH26_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen26_enemy_side_units, CH26_ENEMY_SIDE_UNITS);
    CHECK_EQ(seen26_guest_side_units, CH26_GUEST_SIDE_UNITS);

    CHECK_EQ(seen26_first_enemy_char_id, MAP25_GENERAL_A_CHAR_ID);
    CHECK_EQ(seen26_first_enemy_level, MAP25_GENERAL_LEVEL);

    CHECK_EQ(seen26_general_a, MAP25_GENERAL_COUNT);
    CHECK_EQ(seen26_general_b, MAP25_GENERAL_COUNT);
    CHECK_EQ(seen26_general_c, MAP25_GENERAL_COUNT);
    CHECK_EQ(seen26_general_d, MAP25_GENERAL_COUNT);

    CHECK_EQ(seen26_priests, MAP25_PRIEST_COUNT);
    CHECK_EQ(seen26_hell_knights, MAP25_HELL_KNIGHT_COUNT);
    CHECK_EQ(seen26_sharpshooters, MAP25_SHARPSHOOTER_COUNT);
    CHECK_EQ(seen26_armour_samurai, MAP25_ARMOUR_SAMURAI_COUNT);
    CHECK_EQ(seen26_sky_knights, MAP25_SKY_KNIGHT_COUNT);

    CHECK_EQ(seen26_allies, 0);
}

/* The cut-scene the handler names is Icon25.dat and not the neighbour on
   either side of it.  The marker sits on unit 79 with the value only
   ICON25.DAT writes, and neither decoy writes anything there: each marks unit
   0 with a value of its own, so unit 0's timers are read back clear as the
   other half of the same question.  Every other timer on the marked unit is
   clear too: neither the handler nor the shipped cut-scene writes a status on
   anybody this chapter, ICON25.DAT having no SET_UNIT_TIMER in its 1,564
   bytes.

   The chapter id still reading 25 says the run ended on chapter 26's own map.
   Unlike chapter 25's member this one has no SWITCH_MAP at all, so the id is
   the one the dispatcher put there and nothing in the script ever touched it;
   both the member name and the title-card graphic are chosen from it by the
   callees. */
static void the_chapter_26_cutscene_is_icon25_dat(void)
{
    int slot;

    run_chapter_26_handler();
    CHECK_EQ(run26_state, 1);
    if (run26_state != 1) {
        return;
    }

    CHECK_EQ(seen26_marked_timers[SCRIPT_CH26_MARKER_SLOT],
             SCRIPT_CH26_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH26_MARKER_SLOT) {
            CHECK_EQ(seen26_marked_timers[slot], 0);
        }
        CHECK_EQ(seen26_unit0_timers[slot], 0);
    }

    CHECK_EQ(seen26_chapter_id, CHAPTER_26_ID);
}

/* The cursor ends on unit 0's tile, and on this chapter that tile is
   MAP25.COD's start record for player slot 0 walked four tiles up by the
   cut-scene: (6, 36) becomes (6, 32), so the answer is one neither the map nor
   the script reaches alone.  What the case pins is the call and its operand --
   unit 0 is the only unit the fixture moves, so a handler that named any other
   one reads that slot's MAP25.COD start tile instead, and none of the twelve
   is (6, 32) -- and a handler that made no cursor call at all leaves the
   zeroes fdps_chapter_state_reset writes over both globals, with the staged
   (14, 10) a third value again. */
static void the_chapter_26_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_26_handler();
    CHECK_EQ(run26_state, 1);
    if (run26_state != 1) {
        return;
    }

    CHECK_EQ(seen26_cursor_x, CH26_CURSOR_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen26_cursor_y, CH26_CURSOR_TILE_Y * CURSOR_TILE_STEP);
}

/* Takes chapter 26's fixture container away again, so a later test file can
   stage its own.  A container this half of the file did not create is somebody
   else's and is left where it stands, which is also the only path on which
   this case asserts nothing. */
static void the_chapter_26_fixture_container_is_removed(void)
{
    if (!fixture26_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    fixture26_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}


/* --- fdps_chapter_27_init @ 00021550 constants -------------------------- */

/* Chapter 27 is chapter id 26, and this handler is slot 26 of the table at
   00060074 -- the dword at 000600dc is 00021550, and that data reference is
   the function's only xref. */
#define CHAPTER_27_ID 26

/* The party when chapter 27 opens.  It is the same twelve chapters 25 and 26
   opened with: 珊, added by chapter 24's handler, is the last member to join
   in the game, and none of the three handlers has a
   fdps_roster_add_character in its body. */
#define PARTY_AT_CHAPTER_27 12

/* MAP26.DAT's own header bytes, read back to prove chapter 27's map is the one
   that loaded: twelve player slots at +1 and fifty-five scripted deployments
   at +2 (src/rsrc.c).  Both were staged at other numbers before the run.  The
   file is 1,561 bytes, which is the 131-byte header and fifty-five 26-byte
   records exactly.

   Twelve player slots against a roster of twelve means no slot falls through
   to the zeroed, retired spare fdps_build_map_unit_array writes for a player
   slot with no roster member behind it (src/deploy.c). */
#define MAP26_PLAYER_SLOTS 12
#define MAP26_CHAR_SPAWNS 55

/* What the chapter state reset puts on the board, and what the cut-scene adds
   to it: nothing.  A deployment record's wave tag is byte 21 of the 26-byte
   struct fdps_char_spawn_record, and MAP26.DAT tags twenty-five of its
   fifty-five records wave 0, which the reset deploys behind the twelve player
   slots for thirty-seven map units in all.  The shipped ICON26.DAT has no
   DEPLOY_WAVE anywhere in its 719 bytes when walked with the opcode ladder in
   src/icon.c, so the board this handler returns on is those thirty-seven and
   nothing else.  Every one of the twenty-five is on the enemy side; the map
   carries no guest record at any wave. */
#define MAP26_WAVE_ZERO_UNITS 25
#define CH27_UNITS_AFTER_SCRIPT \
    (PARTY_AT_CHAPTER_27 + MAP26_WAVE_ZERO_UNITS)
#define CH27_ENEMY_SIDE_UNITS MAP26_WAVE_ZERO_UNITS
#define CH27_GUEST_SIDE_UNITS 0

/* The census those twenty-five come to, by character id and level, which is
   the strategy guide's 敌方 list for the chapter less the one group the map
   holds back.  ENEMYDAT.DAT is indexed by the character id less 60 and holds
   HP and MP as per-level coefficients with MV outright
   (assets/characters.md), which settles which id is which against the guide's
   own figures: row 3 at 300 HP and 250 MP a level is the LV40 魔導王吉歐
   HP12000 MP10000, row 4 at 180 the LV30 塞克斯 HP5400, row 5 at 150 with
   100 MP the LV30 布魯森 HP4500 MP3000, row 6 at 130 with 200 MP the LV30
   汎拉沫 HP3900 MP6000 and row 7 at 210 with 150 MP the LV30 凱因巴
   HP6300 MP4500; row 35 at 35 HP a level with MV5 is the LV18 神箭手 HP630
   and row 40 at 40 with MV5 the LV18 鑺甲武士 HP720.

     character 63,  one       -- LV40 魔導王吉歐
     characters 64, 65, 66 and 67, one each
                              -- LV30 塞克斯, 布魯森, 汎拉沫, 凱因巴
     character 95,  eight     -- LV18 神箭手
     character 100, twelve    -- LV18 鑺甲武士

   The seven counts sum to the twenty-five, so asserting all of them pins the
   census exactly rather than merely bounding it. */
#define MAP26_MAGE_KING_CHAR_ID 63
#define MAP26_MAGE_KING_LEVEL 40
#define MAP26_MAGE_KING_COUNT 1
#define MAP26_GENERAL_A_CHAR_ID 64
#define MAP26_GENERAL_B_CHAR_ID 65
#define MAP26_GENERAL_C_CHAR_ID 66
#define MAP26_GENERAL_D_CHAR_ID 67
#define MAP26_GENERAL_LEVEL 30
#define MAP26_GENERAL_COUNT 1
#define MAP26_SHARPSHOOTER_CHAR_ID 95
#define MAP26_SHARPSHOOTER_COUNT 8
#define MAP26_ARMOUR_SAMURAI_CHAR_ID 100
#define MAP26_ARMOUR_SAMURAI_COUNT 12
#define MAP26_ENEMY_LEVEL 18

/* Where 吉歐 lands, and why it is worth pinning by index and not only by
   count.  The reset's own opening deploy appends the map's wave-0 records in
   file order behind the twelve player slots, and record 0 -- the first record
   in the file, the file's only level-40 record, and one the map tags wave 0 --
   is the LV40 魔導王吉歐.  So he is map unit 12, which is the slot chapter
   27's victory test names (src/chpost2.c).  A run that deployed before the
   board was rebuilt, or that rebuilt the board after the cut-scene, would not
   put him there. */
#define CH27_FIRST_ENEMY_MAP_UNIT PARTY_AT_CHAPTER_27

/* What the map holds back, and the ids that catch it.  All thirty records that
   are not wave 0 are wave 1: ten LV18 地獄騎士 and twenty LV18 天空騎士,
   which is the guide's 事件 -- the reinforcement that arrives once the four
   魔戰將軍 are down.  Neither id appears in any wave-0 record, so any count of
   either at all is a wave this handler must not have deployed. */
#define MAP26_HELL_KNIGHT_CHAR_ID 78
#define MAP26_SKY_KNIGHT_CHAR_ID 97

/* Which player slot the cut-scene leaves off the board.  ICON26.DAT's one
   RETIRE_UNIT, its third opcode and at script offset 4, names map unit 3, and
   the member carries no REVIVE_UNIT anywhere in its 719 bytes.  A player
   slot's map unit index is its roster slot and the roster is in join order, so
   the member left off is 法蓮娜 and the eleven remaining are the guide's
   己方 line for the chapter, 法蓮娜以外的所有人.  The member's FACE_UNITS
   at script offset 298 lists eleven units and skips 3, which is the same
   eleven read a second way. */
#define SCRIPT_CH27_LEFT_OFF_UNIT 3

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 36 is
   the last of the thirty-seven, so a run that left a shorter board does not
   reach it.
   The value is one neither decoy writes.  The slot is status_timers[4], record
   offset 0x26, for the reason tests/chinit1.c gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1.
   The shipped member has no SET_UNIT_TIMER of its own, so this marker is the
   fixture's alone. */
#define SCRIPT_CH27_MARKER_UNIT (CH27_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH27_MARKER_OPERAND 1
#define SCRIPT_CH27_MARKER_SLOT 4
#define SCRIPT_CH27_MARKER_VALUE 129

/* What the late decoy in this chapter's container writes.  ICON27.DAT is
   chapter 28's script, the member a rewrite that read the chapter number out
   of the handler's own name would reach for; the early decoy is ICON25.DAT,
   chapter 26's script, and it writes SCRIPT_CH26_DECOY_VALUE, the value the
   chapter 25 section above already gives that member.  Both mark unit 0, which
   exists whichever member is opened, and neither value is the one the real
   member writes. */
#define SCRIPT_CH28_DECOY_VALUE 128

/* Where the cursor ends, and why it is the cut-scene's tile and not the map's.
   MAP26.COD starts player slot 0 on (8, 27) -- the record at
   0xb + (55 + 0) * 6, read as two signed words (src/deploy.c) -- and
   ICON26.DAT walks map unit 0 one tile with facing 2 four separate times, at
   script offsets 326, 332, 351 and 367, which src/icon.c's ladder makes a step
   up each.  Nothing places unit 0 outright anywhere in the member -- the one
   PLACE_UNIT it carries is 吉歐's -- so (8, 23) is a tile only the map's start
   record and the cut-scene's walks together arrive at.  A handler that skipped
   the cursor call leaves the zeroes fdps_chapter_state_reset writes over both
   globals (src/chapter.c); the staging below is a third value again, so a run
   in which the reset never happened is told apart from both. */
#define CH27_START_TILE_X 8
#define CH27_START_TILE_Y 27
#define CH27_WALK_TILES 4
#define CH27_CURSOR_TILE_X CH27_START_TILE_X
#define CH27_CURSOR_TILE_Y (CH27_START_TILE_Y - CH27_WALK_TILES)
#define CH27_STAGED_CURSOR_X 336
#define CH27_STAGED_CURSOR_Y 240

/* How many rendered frames each walk sub-step of the fixture's own walk is
   held for, and the facing the shipped walks carry.  The shipped member asks
   for one frame on each of its four walks already; one is also the shortest
   hold that still moves the unit, because the position update sits inside the
   hold loop and a hold of zero walks nowhere (src/icon.c).  The four
   single-tile walks are collapsed into one walk of four tiles, which commits
   one tile per sub-step 6 either way and so lands on the same tile. */
#define SCRIPT_CH27_WALK_HOLD_FRAMES 1
#define SCRIPT_CH27_WALK_FACING_UP 2


/* ICON25.DAT: the member the handler must NOT open, one chapter early -- it is
   chapter 26's own script.  It retires nobody, revives nobody, walks nobody
   and marks unit 0 with a value of its own. */
static unsigned char fixture27_icon25_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH26_DECOY_VALUE,
    0x00
};

/* ICON26.DAT: the member fdps_chapter_27_init names.  The RETIRE is the
   shipped member's own, with its operand, and it carries no REVIVE behind it,
   which is what leaves map unit 3 off the board; the WALK stands in for the
   shipped member's four single-tile walks of unit 0, same facing and the same
   four tiles in one opcode.  The SET_UNIT_TIMER marker at the end is this
   file's own addition, because the shipped member has none: it writes a value
   of its own onto the last unit the map's opening wave appended.  The shipped
   member's other seventy-odd opcodes fade, scroll, shake and pose the cast,
   set the music, place and walk 吉歐 and draw chapter text, and are left out
   for the reasons the file comment gives. */
static unsigned char fixture27_icon26_dat[] = {
    0x0b, SCRIPT_CH27_LEFT_OFF_UNIT,
    0x01, SCRIPT_CH27_WALK_HOLD_FRAMES, CH27_WALK_TILES, 1,
    0, SCRIPT_CH27_WALK_FACING_UP,
    0x12, SCRIPT_CH27_MARKER_UNIT, SCRIPT_CH27_MARKER_OPERAND,
    SCRIPT_CH27_MARKER_VALUE,
    0x00
};

/* ICON27.DAT: the member the handler must NOT open, one chapter late -- it is
   chapter 28's own script, and the one a rewrite that read the chapter number
   out of the handler's own name would reach for.  It retires nobody, revives
   nobody, walks nobody and marks unit 0 with a value of its own. */
static unsigned char fixture27_icon27_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH28_DECOY_VALUE,
    0x00
};

static char *fixture27_names[FIXTURE_MEMBERS] = {
    "ICON25.DAT", "ICON26.DAT", "ICON27.DAT"
};

static unsigned char *fixture27_bytes[FIXTURE_MEMBERS] = {
    fixture27_icon25_dat, fixture27_icon26_dat, fixture27_icon27_dat
};

static int fixture27_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture27_icon25_dat), sizeof(fixture27_icon26_dat),
    sizeof(fixture27_icon27_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run27_state = 0;

/* Whether this half of the file created the container, and so whether it may
   remove it. */
static int fixture27_owned = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order.  It is chapter 26's list unchanged, because nobody joined in
   between. */
static int chapter_27_party[PARTY_AT_CHAPTER_27] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID, MARIAN_CHAR_ID, LANCELOT_CHAR_ID, SHAN_CHAR_ID
};

/* Everything the chapter 27 cases assert, captured the instant the handler
   returned. */
static int seen27_roster_count;
static int seen27_player_slots;
static int seen27_char_spawns;
static int seen27_unit_count;
static int seen27_party_char_ids[PARTY_AT_CHAPTER_27];
static int seen27_party_sides[PARTY_AT_CHAPTER_27];
static int seen27_party_flags[PARTY_AT_CHAPTER_27];
static int seen27_mage_king;
static int seen27_general_a;
static int seen27_general_b;
static int seen27_general_c;
static int seen27_general_d;
static int seen27_sharpshooters;
static int seen27_armour_samurai;
static int seen27_hell_knights;
static int seen27_sky_knights;
static int seen27_enemy_side_units;
static int seen27_guest_side_units;
static int seen27_first_enemy_char_id;
static int seen27_first_enemy_level;
static unsigned char seen27_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen27_unit0_timers[STATUS_TIMER_COUNT];
static int seen27_cursor_x;
static int seen27_cursor_y;
static int seen27_chapter_id;

/* Builds chapter 27's fixture container, or answers no.  Same refusal as the
   two stagings above: a file of that name that was already there is either the
   shipped 4.7 MB container or another test file's fixture, and neither may be
   clobbered.  The chapter 26 case that removes its own container runs before
   this one, so the three never collide. */
static int stage_chapter_27_archive(void)
{
    FILE *fp;
    long member_at;
    int i;

    if (fixture27_owned) {
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
        write_name(fp, fixture27_names[i]);
        write_dword(fp, (long) fixture27_lengths[i]);
        write_dword(fp, (long) fixture27_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture27_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture27_bytes[i], 1, (size_t) fixture27_lengths[i], fp);
    }
    fclose(fp);

    fixture27_owned = 1;
    return 1;
}

static void capture_chapter_27(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    struct fdps_unit_record *first_enemy;
    int unit_index;
    int char_id;
    int level;
    int slot;

    seen27_roster_count = data_fdps_roster_member_count;
    seen27_player_slots = data_fdps_map_player_slot_count;
    seen27_char_spawns = data_fdps_map_char_spawn_count;
    seen27_unit_count = data_fdps_map_unit_count;
    seen27_cursor_x = data_fdps_map_cursor_world_x;
    seen27_cursor_y = data_fdps_map_cursor_world_y;
    seen27_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_27; slot++) {
        seen27_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen27_party_sides[slot] = (int) unit0[slot].side;
        seen27_party_flags[slot] = (int) unit0[slot].flags;
    }

    seen27_mage_king = 0;
    seen27_general_a = 0;
    seen27_general_b = 0;
    seen27_general_c = 0;
    seen27_general_d = 0;
    seen27_sharpshooters = 0;
    seen27_armour_samurai = 0;
    seen27_hell_knights = 0;
    seen27_sky_knights = 0;
    seen27_enemy_side_units = 0;
    seen27_guest_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_27;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen27_enemy_side_units++;
        } else if (unit0[unit_index].side == GUEST_SIDE) {
            seen27_guest_side_units++;
        }
        if (char_id == MAP26_HELL_KNIGHT_CHAR_ID) {
            seen27_hell_knights++;
        } else if (char_id == MAP26_SKY_KNIGHT_CHAR_ID) {
            seen27_sky_knights++;
        }
        if (level == MAP26_MAGE_KING_LEVEL) {
            if (char_id == MAP26_MAGE_KING_CHAR_ID) {
                seen27_mage_king++;
            }
        } else if (level == MAP26_GENERAL_LEVEL) {
            if (char_id == MAP26_GENERAL_A_CHAR_ID) {
                seen27_general_a++;
            } else if (char_id == MAP26_GENERAL_B_CHAR_ID) {
                seen27_general_b++;
            } else if (char_id == MAP26_GENERAL_C_CHAR_ID) {
                seen27_general_c++;
            } else if (char_id == MAP26_GENERAL_D_CHAR_ID) {
                seen27_general_d++;
            }
        } else if (level == MAP26_ENEMY_LEVEL) {
            if (char_id == MAP26_SHARPSHOOTER_CHAR_ID) {
                seen27_sharpshooters++;
            } else if (char_id == MAP26_ARMOUR_SAMURAI_CHAR_ID) {
                seen27_armour_samurai++;
            }
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen27_marked_timers[slot] = 0;
        seen27_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* Both reads are guarded by the unit count: a run that opened a member
       leaving a shorter array would not have the index, and reading past it
       would be reading memory the allocation does not cover.  The cases below
       fail on the zero that is left instead. */
    seen27_first_enemy_char_id = 0;
    seen27_first_enemy_level = 0;
    if (data_fdps_map_unit_count > CH27_FIRST_ENEMY_MAP_UNIT) {
        first_enemy = unit0 + CH27_FIRST_ENEMY_MAP_UNIT;
        seen27_first_enemy_char_id = (int) first_enemy->char_id;
        seen27_first_enemy_level = (int) first_enemy->level;
    }

    if (data_fdps_map_unit_count > SCRIPT_CH27_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH27_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen27_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 27 handler once, against the shipped containers and this
   chapter's fixture cut-scene, and records what it left behind.  The twelve
   members the party has when the chapter opens are staged first, in join
   order, because this handler adds nobody itself. */
static void run_chapter_27_handler(void)
{
    int member;

    if (run27_state != 0) {
        return;
    }
    run27_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_chapter_27_archive()) {
        return;
    }

    stage_globals();
    data_fdps_map_cursor_world_x = CH27_STAGED_CURSOR_X;
    data_fdps_map_cursor_world_y = CH27_STAGED_CURSOR_Y;
    data_fdps_chapter_current_chapter_id = CHAPTER_27_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_27; member++) {
        fdps_roster_add_character(chapter_27_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_27_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_27();
    free_chapter_globals();
    run27_state = 1;
}

/* Nobody joins this chapter either, and every player slot has a member behind
   it.  The roster count still reading twelve is what says the handler made no
   add -- there is no fdps_roster_add_character in its body -- and the two map
   header counts are read back as well because they were staged at numbers no
   map carries: MAP26.DAT's own twelve and fifty-five say chapter 27's map is
   the one that loaded.  Twelve slots against twelve members means no slot
   falls through to the zeroed, retired spare fdps_build_map_unit_array writes
   for a slot with no member behind it. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_twenty_seven(void)
{
    run_chapter_27_handler();
    CHECK_EQ(run27_state, 1);
    if (run27_state != 1) {
        return;
    }

    CHECK_EQ(seen27_roster_count, PARTY_AT_CHAPTER_27);
    CHECK_EQ(seen27_player_slots, MAP26_PLAYER_SLOTS);
    CHECK_EQ(seen27_char_spawns, MAP26_CHAR_SPAWNS);
}

/* The chapter is fought without 法蓮娜, and it is the cut-scene that leaves
   her off: ICON26.DAT retires map unit 3 and revives nobody, so map unit 3
   carries the retired bit when the handler returns and the other eleven player
   slots carry a clear flags byte -- which also says no slot fell through to
   the zeroed, retired spare.  Read through the join order that is the guide's
   己方 line for the chapter, 法蓮娜以外的所有人. */
static void chapter_twenty_seven_is_fought_without_flarena(void)
{
    int slot;

    run_chapter_27_handler();
    CHECK_EQ(run27_state, 1);
    if (run27_state != 1) {
        return;
    }

    for (slot = 0; slot < PARTY_AT_CHAPTER_27; slot++) {
        CHECK_EQ(seen27_party_char_ids[slot], chapter_27_party[slot]);
        CHECK_EQ(seen27_party_sides[slot], PLAYER_SIDE);
        if (slot != SCRIPT_CH27_LEFT_OFF_UNIT) {
            CHECK_EQ(seen27_party_flags[slot], 0);
        }
    }

    CHECK_EQ(seen27_party_char_ids[SCRIPT_CH27_LEFT_OFF_UNIT],
             FLARENA_CHAR_ID);
    CHECK_EQ(seen27_party_flags[SCRIPT_CH27_LEFT_OFF_UNIT],
             UNIT_FLAG_RETIRED);
}

/* What the reset leaves on the map, the cut-scene adding nothing to it: the
   twelve player slots and MAP26.DAT's twenty-five wave-0 records, for
   thirty-seven.  The census behind those twenty-five is the strategy guide's
   敌方 list for the chapter found by character id and level, less the one
   group the map holds back -- one LV40 魔導王吉歐, one each of the LV30
   塞克斯, 布魯森, 汎拉沫 and 凱因巴, eight LV18 神箭手 and twelve LV18
   鑺甲武士 -- and all twenty-five are on the enemy side, with no guest at
   all.  The seven counts sum to the twenty-five, so the census is pinned
   exactly.

   吉歐 is asserted at map unit 12 by id and level and not merely counted,
   because that index is what says the reset's own deploy ran behind the player
   slots and in the map's record order: it is the first unit behind the twelve,
   and record 0 is the first record of the file.  It is also the slot chapter
   27's victory test names.

   The wave the handler must not have deployed is caught by two ids that appear
   in no wave-0 record at all: the ten LV18 地獄騎士 and twenty LV18
   天空騎士 of wave 1, the guide's 事件 reinforcement, so any count of
   either is a failure. */
static void chapter_twenty_seven_opens_with_the_guides_enemy_group(void)
{
    run_chapter_27_handler();
    CHECK_EQ(run27_state, 1);
    if (run27_state != 1) {
        return;
    }

    CHECK_EQ(seen27_unit_count, CH27_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen27_enemy_side_units, CH27_ENEMY_SIDE_UNITS);
    CHECK_EQ(seen27_guest_side_units, CH27_GUEST_SIDE_UNITS);

    CHECK_EQ(seen27_first_enemy_char_id, MAP26_MAGE_KING_CHAR_ID);
    CHECK_EQ(seen27_first_enemy_level, MAP26_MAGE_KING_LEVEL);

    CHECK_EQ(seen27_mage_king, MAP26_MAGE_KING_COUNT);
    CHECK_EQ(seen27_general_a, MAP26_GENERAL_COUNT);
    CHECK_EQ(seen27_general_b, MAP26_GENERAL_COUNT);
    CHECK_EQ(seen27_general_c, MAP26_GENERAL_COUNT);
    CHECK_EQ(seen27_general_d, MAP26_GENERAL_COUNT);

    CHECK_EQ(seen27_sharpshooters, MAP26_SHARPSHOOTER_COUNT);
    CHECK_EQ(seen27_armour_samurai, MAP26_ARMOUR_SAMURAI_COUNT);

    CHECK_EQ(seen27_hell_knights, 0);
    CHECK_EQ(seen27_sky_knights, 0);
}

/* The cut-scene the handler names is Icon26.dat and not the neighbour on
   either side of it.  The marker sits on unit 36 with the value only
   ICON26.DAT writes, and neither decoy writes anything there: each marks unit
   0 with a value of its own, so unit 0's timers are read back clear as the
   other half of the same question.  Every other timer on the marked unit is
   clear too: neither the handler nor the shipped cut-scene writes a status on
   anybody this chapter, ICON26.DAT having no SET_UNIT_TIMER in its 719 bytes.

   The chapter id still reading 26 says the run ended on chapter 27's own map.
   Like chapter 26's member and unlike chapter 25's this one has no SWITCH_MAP
   at all, so the id is the one the dispatcher put there and nothing in the
   script ever touched it; both the member name and the title-card graphic are
   chosen from it by the callees. */
static void the_chapter_27_cutscene_is_icon26_dat(void)
{
    int slot;

    run_chapter_27_handler();
    CHECK_EQ(run27_state, 1);
    if (run27_state != 1) {
        return;
    }

    CHECK_EQ(seen27_marked_timers[SCRIPT_CH27_MARKER_SLOT],
             SCRIPT_CH27_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH27_MARKER_SLOT) {
            CHECK_EQ(seen27_marked_timers[slot], 0);
        }
        CHECK_EQ(seen27_unit0_timers[slot], 0);
    }

    CHECK_EQ(seen27_chapter_id, CHAPTER_27_ID);
}

/* The cursor ends on unit 0's tile, and on this chapter that tile is
   MAP26.COD's start record for player slot 0 walked four tiles up by the
   cut-scene: (8, 27) becomes (8, 23), so the answer is one neither the map nor
   the script reaches alone.  What the case pins is the call and its operand --
   unit 0 is the only player unit the fixture moves, so a handler that named
   any other one reads that slot's MAP26.COD start tile instead, and none of
   the twelve is (8, 23) -- and a handler that made no cursor call at all
   leaves the zeroes fdps_chapter_state_reset writes over both globals, with
   the staged (14, 10) a third value again. */
static void the_chapter_27_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_27_handler();
    CHECK_EQ(run27_state, 1);
    if (run27_state != 1) {
        return;
    }

    CHECK_EQ(seen27_cursor_x, CH27_CURSOR_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen27_cursor_y, CH27_CURSOR_TILE_Y * CURSOR_TILE_STEP);
}

/* Takes chapter 27's fixture container away again, so a later test file can
   stage its own.  A container this section did not create is somebody else's
   and is left where it stands, which is also the only path on which this case
   asserts nothing. */
static void the_chapter_27_fixture_container_is_removed(void)
{
    if (!fixture27_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    fixture27_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

/* --- fdps_chapter_28_init @ 00021590 constants -------------------------- */

/* Chapter 28 is chapter id 27, and this handler is slot 27 of the table at
   00060074 -- the dword at 000600e0 is 00021590, and that data reference is
   the function's only xref. */
#define CHAPTER_28_ID 27

/* The party when chapter 28 opens.  It is the same twelve chapters 25, 26 and
   27 opened with: 珊, added by chapter 24's handler, is the last member to
   join in the game, and none of the four handlers has a
   fdps_roster_add_character in its body. */
#define PARTY_AT_CHAPTER_28 12

/* MAP27.DAT's own header bytes, read back to prove chapter 28's map is the one
   that loaded: twelve player slots at +1 and sixty-five scripted deployments
   at +2 (src/rsrc.c).  Both were staged at other numbers before the run, and
   the spawn count is the one that tells this map from chapter 27's fifty-five.
   The file is 1,821 bytes, which is the 131-byte header and sixty-five 26-byte
   records exactly.

   Twelve player slots against a roster of twelve means no slot falls through
   to the zeroed, retired spare fdps_build_map_unit_array writes for a player
   slot with no roster member behind it (src/deploy.c). */
#define MAP27_PLAYER_SLOTS 12
#define MAP27_CHAR_SPAWNS 65

/* What the chapter state reset puts on the board, and what the cut-scene adds
   to it: nothing.  A deployment record's wave tag is byte 21 of the 26-byte
   struct fdps_char_spawn_record, and MAP27.DAT tags thirty-seven of its
   sixty-five records wave 0, which the reset deploys behind the twelve player
   slots for forty-nine map units in all.  The shipped ICON27.DAT has no
   DEPLOY_WAVE anywhere in its 267 bytes when walked with the opcode ladder in
   src/icon.c, so the board this handler returns on is those forty-nine and
   nothing else.  Every one of the thirty-seven is on the enemy side; the map
   carries no guest record at any wave. */
#define MAP27_WAVE_ZERO_UNITS 37
#define CH28_UNITS_AFTER_SCRIPT \
    (PARTY_AT_CHAPTER_28 + MAP27_WAVE_ZERO_UNITS)
#define CH28_ENEMY_SIDE_UNITS MAP27_WAVE_ZERO_UNITS
#define CH28_GUEST_SIDE_UNITS 0

/* The census those thirty-seven come to, by character id and level, which is
   the strategy guide's 敵方 list for the chapter exactly -- this map holds no
   group of its opening cast back.  ENEMYDAT.DAT is indexed by the character id
   less 60 and holds HP and MP as per-level coefficients with MV outright
   (assets/characters.md), which settles which id is which against the guide's
   own figures: row 44 at 40 HP and 30 MP a level with MV4 is the LV25
   黑暗祭司 HP1000 MP750, row 45 at 25 HP a level with MV4 the LV25 幽魂
   HP625, row 24 at 38 with MV4 the LV25 骷髏兵 HP950, and row 47 at 35 with
   MV7 the LV25 地獄犬 HP875 -- the 地獄犬's MV7 being the one movement figure
   in the chapter's list that is not 4.

     character 104, four      -- LV25 黑暗祭司
     character 105, nine      -- LV25 幽魂
     character 84,  ten       -- LV25 骷髏兵
     character 107, fourteen  -- LV25 地獄犬

   The four counts sum to the thirty-seven, so asserting all of them pins the
   census exactly rather than merely bounding it, and it is also what catches a
   reinforcement wave having been deployed: the three ids 84, 105 and 107 all
   appear in the later waves as well, so on this chapter there is no id that is
   a wave marker of its own and the counts are the whole test. */
#define MAP27_DARK_PRIEST_CHAR_ID 104
#define MAP27_DARK_PRIEST_COUNT 4
#define MAP27_WRAITH_CHAR_ID 105
#define MAP27_WRAITH_COUNT 9
#define MAP27_SKELETON_CHAR_ID 84
#define MAP27_SKELETON_COUNT 10
#define MAP27_HELL_HOUND_CHAR_ID 107
#define MAP27_HELL_HOUND_COUNT 14
#define MAP27_ENEMY_LEVEL 25

/* Where the map's first record lands, and why it is worth pinning by index and
   not only by count.  The reset's own opening deploy appends the map's wave-0
   records in file order behind the twelve player slots, and record 0 -- the
   first record in the file, tagged wave 0 -- is a LV25 骷髏兵.  So it is map
   unit 12.  A run that deployed before the board was rebuilt, or that rebuilt
   the board after the cut-scene, would not put it there. */
#define CH28_FIRST_ENEMY_MAP_UNIT PARTY_AT_CHAPTER_28

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 48 is
   the last of the forty-nine, so a run that left a shorter board does not
   reach it.
   The value is one neither decoy writes.  The slot is status_timers[4], record
   offset 0x26, for the reason tests/chinit1.c gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1.
   The shipped member has no SET_UNIT_TIMER of its own, so this marker is the
   fixture's alone. */
#define SCRIPT_CH28_MARKER_UNIT (CH28_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH28_MARKER_OPERAND 1
#define SCRIPT_CH28_MARKER_SLOT 4
#define SCRIPT_CH28_MARKER_VALUE 130

/* What the late decoy in this chapter's container writes.  ICON28.DAT is
   chapter 29's script, the member a rewrite that read the chapter number out
   of the handler's own name would reach for; the early decoy is ICON26.DAT,
   chapter 27's script, and it writes SCRIPT_CH27_DECOY_VALUE, the value the
   chapter 26 section above already gives that member.  Both mark unit 0, which
   exists whichever member is opened, and neither value is the one the real
   member writes.  ICON27.DAT is the member under test here and carries this
   section's own marker value, where the chapter 27 section above staged it as
   a decoy writing SCRIPT_CH28_DECOY_VALUE; the two containers never exist at
   the same time. */
#define SCRIPT_CH29_DECOY_VALUE 131

/* Where the cursor ends, and why none of it comes from the map.  ICON27.DAT
   places map unit 0 outright on (11, 24) with its first PLACE_UNIT, at script
   offset 7, so MAP27.COD's own start record for player slot 0 -- the record at
   0xb + (65 + 0) * 6, read as two signed words (src/deploy.c), which is
   (8, 18) -- is overwritten before anything moves.  The member then walks unit
   0 five tiles up, in the four group walks at script offsets 87, 103, 119 and
   135; one tile right, in the walk at offset 155 whose facing operand for
   unit 0 is 3, the arm src/icon.c's ladder makes a step right; and two more
   tiles up, at offsets 173 and 195.  That leaves unit 0 on (12, 17).  A
   handler that skipped the cursor call leaves the zeroes
   fdps_chapter_state_reset writes over both globals (src/chapter.c); the
   staging below is a third value again, so a run in which the reset never
   happened is told apart from both. */
#define CH28_PLACE_TILE_X 11
#define CH28_PLACE_TILE_Y 24
#define CH28_WALK_UP_TILES_BEFORE_TURN 5
#define CH28_WALK_RIGHT_TILES 1
#define CH28_WALK_UP_TILES_AFTER_TURN 2
#define CH28_CURSOR_TILE_X (CH28_PLACE_TILE_X + CH28_WALK_RIGHT_TILES)
#define CH28_CURSOR_TILE_Y (CH28_PLACE_TILE_Y \
                            - CH28_WALK_UP_TILES_BEFORE_TURN \
                            - CH28_WALK_UP_TILES_AFTER_TURN)
#define CH28_STAGED_CURSOR_X 336
#define CH28_STAGED_CURSOR_Y 240

/* How many rendered frames each walk sub-step of the fixture's own walks is
   held for, the facings the shipped walks carry, and the facing its PLACE_UNIT
   writes.  The shipped member asks for two frames on each of its nine walks;
   one is the shortest hold that still moves the unit, because the position
   update sits inside the hold loop and a hold of zero walks nowhere
   (src/icon.c).  The shipped member's seven walks that list unit 0 are
   collapsed into three, one per facing run, which commits one tile per
   sub-step 6 either way and so lands on the same tile. */
#define SCRIPT_CH28_WALK_HOLD_FRAMES 1
#define SCRIPT_CH28_WALK_FACING_UP 2
#define SCRIPT_CH28_WALK_FACING_RIGHT 3
#define SCRIPT_CH28_PLACE_FACING 0

/* The second of the shipped member's twelve PLACE_UNITs that this fixture has
   to carry, and the only other one it needs.  MAP27.COD starts player slot 11
   on (12, 17) -- the record at 0xb + (65 + 11) * 6, read as two signed words
   (src/deploy.c) -- which is the very tile the cut-scene walks unit 0 to, so a
   fixture that placed unit 0 alone would leave two units standing there and the
   cursor pair would no longer tell unit 0 from unit 11.  The shipped member has
   no such ambiguity: its PLACE_UNIT at script offset 62, bytes
   0a 0b 0a 18 00, moves unit 11 to (10, 24) before anything walks, and no walk
   in the member ever lists unit 11.  Those are the operands repeated here.  No
   other player slot needs one: the remaining ten keep MAP27.COD's own start
   tiles, (11,19) (10,20) (9,18) (9,19) (11,21) (12,19) (10,21) (11,22) (11,23)
   (10,24), and none of those is (12, 17).  Nor does any enemy: all thirty-seven
   wave-0 records read their tiles from MAP27.COD's first sixty-five records and
   none of those thirty-seven is (12, 17) either. */
#define CH28_SLOT11_UNIT 11
#define CH28_SLOT11_TILE_X 10
#define CH28_SLOT11_TILE_Y 24


/* ICON26.DAT: the member the handler must NOT open, one chapter early -- it is
   chapter 27's own script.  It places nobody, walks nobody and marks unit 0
   with a value of its own. */
static unsigned char fixture28_icon26_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH27_DECOY_VALUE,
    0x00
};

/* ICON27.DAT: the member fdps_chapter_28_init names.  The two PLACE_UNITs are
   the shipped member's own first opcode of the twelve that seat the party and
   the one that seats player slot 11, with their operands and in the shipped
   order, both ahead of every walk as they are in the member; slot 11's is here
   because without it that slot keeps a MAP27.COD start tile equal to the one
   unit 0 walks to, and the comment above CH28_SLOT11_UNIT gives the detail.
   The three WALKs stand in for the shipped member's seven walks
   of unit 0, same facings and the same eight tiles in three opcodes.  There is
   no RETIRE, REVIVE or BLINK here because the shipped member has none of them
   either -- this is the chapter the whole party fights.  The SET_UNIT_TIMER
   marker at the end is this file's own addition, because the shipped member
   has none: it writes a value of its own onto the last unit the map's opening
   wave appended.  The shipped member's other twenty-odd opcodes fade, scroll
   and pose the cast, set the music, seat the other eleven party members and
   draw chapter text, and are left out for the reasons the file comment
   gives. */
static unsigned char fixture28_icon27_dat[] = {
    0x0a, 0, CH28_PLACE_TILE_X, CH28_PLACE_TILE_Y, SCRIPT_CH28_PLACE_FACING,
    0x0a, CH28_SLOT11_UNIT, CH28_SLOT11_TILE_X, CH28_SLOT11_TILE_Y,
    SCRIPT_CH28_PLACE_FACING,
    0x01, SCRIPT_CH28_WALK_HOLD_FRAMES, CH28_WALK_UP_TILES_BEFORE_TURN, 1,
    0, SCRIPT_CH28_WALK_FACING_UP,
    0x01, SCRIPT_CH28_WALK_HOLD_FRAMES, CH28_WALK_RIGHT_TILES, 1,
    0, SCRIPT_CH28_WALK_FACING_RIGHT,
    0x01, SCRIPT_CH28_WALK_HOLD_FRAMES, CH28_WALK_UP_TILES_AFTER_TURN, 1,
    0, SCRIPT_CH28_WALK_FACING_UP,
    0x12, SCRIPT_CH28_MARKER_UNIT, SCRIPT_CH28_MARKER_OPERAND,
    SCRIPT_CH28_MARKER_VALUE,
    0x00
};

/* ICON28.DAT: the member the handler must NOT open, one chapter late -- it is
   chapter 29's own script, and the one a rewrite that read the chapter number
   out of the handler's own name would reach for.  It places nobody, walks
   nobody and marks unit 0 with a value of its own. */
static unsigned char fixture28_icon28_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH29_DECOY_VALUE,
    0x00
};

static char *fixture28_names[FIXTURE_MEMBERS] = {
    "ICON26.DAT", "ICON27.DAT", "ICON28.DAT"
};

static unsigned char *fixture28_bytes[FIXTURE_MEMBERS] = {
    fixture28_icon26_dat, fixture28_icon27_dat, fixture28_icon28_dat
};

static int fixture28_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture28_icon26_dat), sizeof(fixture28_icon27_dat),
    sizeof(fixture28_icon28_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run28_state = 0;

/* Whether this half of the file created the container, and so whether it may
   remove it. */
static int fixture28_owned = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order.  It is chapter 27's list unchanged, because nobody joined in
   between. */
static int chapter_28_party[PARTY_AT_CHAPTER_28] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID, MARIAN_CHAR_ID, LANCELOT_CHAR_ID, SHAN_CHAR_ID
};

/* Everything the chapter 28 cases assert, captured the instant the handler
   returned. */
static int seen28_roster_count;
static int seen28_player_slots;
static int seen28_char_spawns;
static int seen28_unit_count;
static int seen28_party_char_ids[PARTY_AT_CHAPTER_28];
static int seen28_party_sides[PARTY_AT_CHAPTER_28];
static int seen28_party_flags[PARTY_AT_CHAPTER_28];
static int seen28_dark_priests;
static int seen28_wraiths;
static int seen28_skeletons;
static int seen28_hell_hounds;
static int seen28_off_census_units;
static int seen28_enemy_side_units;
static int seen28_guest_side_units;
static int seen28_first_enemy_char_id;
static int seen28_first_enemy_level;
static unsigned char seen28_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen28_unit0_timers[STATUS_TIMER_COUNT];
static int seen28_cursor_x;
static int seen28_cursor_y;
static int seen28_chapter_id;

/* Builds chapter 28's fixture container, or answers no.  Same refusal as the
   three stagings above: a file of that name that was already there is either
   the shipped 4.7 MB container or another test file's fixture, and neither may
   be clobbered.  The chapter 27 case that removes its own container runs
   before this one, so the four never collide. */
static int stage_chapter_28_archive(void)
{
    FILE *fp;
    long member_at;
    int i;

    if (fixture28_owned) {
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
        write_name(fp, fixture28_names[i]);
        write_dword(fp, (long) fixture28_lengths[i]);
        write_dword(fp, (long) fixture28_lengths[i]);
        fputc(0, fp);
        write_dword(fp, member_at);
        member_at += (long) fixture28_lengths[i];
    }
    for (i = 0; i < FIXTURE_MEMBERS; i++) {
        fwrite(fixture28_bytes[i], 1, (size_t) fixture28_lengths[i], fp);
    }
    fclose(fp);

    fixture28_owned = 1;
    return 1;
}

static void capture_chapter_28(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    struct fdps_unit_record *first_enemy;
    int unit_index;
    int char_id;
    int level;
    int slot;

    seen28_roster_count = data_fdps_roster_member_count;
    seen28_player_slots = data_fdps_map_player_slot_count;
    seen28_char_spawns = data_fdps_map_char_spawn_count;
    seen28_unit_count = data_fdps_map_unit_count;
    seen28_cursor_x = data_fdps_map_cursor_world_x;
    seen28_cursor_y = data_fdps_map_cursor_world_y;
    seen28_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_28; slot++) {
        seen28_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen28_party_sides[slot] = (int) unit0[slot].side;
        seen28_party_flags[slot] = (int) unit0[slot].flags;
    }

    seen28_dark_priests = 0;
    seen28_wraiths = 0;
    seen28_skeletons = 0;
    seen28_hell_hounds = 0;
    seen28_off_census_units = 0;
    seen28_enemy_side_units = 0;
    seen28_guest_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_28;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen28_enemy_side_units++;
        } else if (unit0[unit_index].side == GUEST_SIDE) {
            seen28_guest_side_units++;
        }
        if (level != MAP27_ENEMY_LEVEL) {
            seen28_off_census_units++;
        } else if (char_id == MAP27_DARK_PRIEST_CHAR_ID) {
            seen28_dark_priests++;
        } else if (char_id == MAP27_WRAITH_CHAR_ID) {
            seen28_wraiths++;
        } else if (char_id == MAP27_SKELETON_CHAR_ID) {
            seen28_skeletons++;
        } else if (char_id == MAP27_HELL_HOUND_CHAR_ID) {
            seen28_hell_hounds++;
        } else {
            seen28_off_census_units++;
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen28_marked_timers[slot] = 0;
        seen28_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* Both reads are guarded by the unit count: a run that opened a member
       leaving a shorter board would not have the index, and reading past it
       would be reading memory the allocation does not cover.  The cases below
       fail on the zero that is left instead. */
    seen28_first_enemy_char_id = 0;
    seen28_first_enemy_level = 0;
    if (data_fdps_map_unit_count > CH28_FIRST_ENEMY_MAP_UNIT) {
        first_enemy = unit0 + CH28_FIRST_ENEMY_MAP_UNIT;
        seen28_first_enemy_char_id = (int) first_enemy->char_id;
        seen28_first_enemy_level = (int) first_enemy->level;
    }

    if (data_fdps_map_unit_count > SCRIPT_CH28_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH28_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen28_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 28 handler once, against the shipped containers and this
   chapter's fixture cut-scene, and records what it left behind.  The twelve
   members the party has when the chapter opens are staged first, in join
   order, because this handler adds nobody itself. */
static void run_chapter_28_handler(void)
{
    int member;

    if (run28_state != 0) {
        return;
    }
    run28_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_chapter_28_archive()) {
        return;
    }

    stage_globals();
    data_fdps_map_cursor_world_x = CH28_STAGED_CURSOR_X;
    data_fdps_map_cursor_world_y = CH28_STAGED_CURSOR_Y;
    data_fdps_chapter_current_chapter_id = CHAPTER_28_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_28; member++) {
        fdps_roster_add_character(chapter_28_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_28_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_28();
    free_chapter_globals();
    run28_state = 1;
}

/* Nobody joins this chapter either, and every player slot has a member behind
   it.  The roster count still reading twelve is what says the handler made no
   add -- there is no fdps_roster_add_character in its body -- and the two map
   header counts are read back as well because they were staged at numbers no
   map carries: MAP27.DAT's own twelve and sixty-five say chapter 28's map is
   the one that loaded, the sixty-five being what tells it from chapter 27's
   fifty-five.  Twelve slots against twelve members means no slot falls through
   to the zeroed, retired spare fdps_build_map_unit_array writes for a slot
   with no member behind it. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_twenty_eight(void)
{
    run_chapter_28_handler();
    CHECK_EQ(run28_state, 1);
    if (run28_state != 1) {
        return;
    }

    CHECK_EQ(seen28_roster_count, PARTY_AT_CHAPTER_28);
    CHECK_EQ(seen28_player_slots, MAP27_PLAYER_SLOTS);
    CHECK_EQ(seen28_char_spawns, MAP27_CHAR_SPAWNS);
}

/* This chapter is fought with the whole party, which is what tells it from the
   three before it: ICON27.DAT carries no RETIRE_UNIT, no REVIVE_UNIT and no
   BLINK_UNITS_OUT anywhere in its 267 bytes, so all twelve player slots are on
   the board with a clear flags byte when the handler returns -- which also
   says no slot fell through to the zeroed, retired spare.  Read through the
   join order; the guide's entry for the chapter has no 己方 line of its own,
   there being nobody left off to name. */
static void chapter_twenty_eight_is_fought_with_the_whole_party(void)
{
    int slot;

    run_chapter_28_handler();
    CHECK_EQ(run28_state, 1);
    if (run28_state != 1) {
        return;
    }

    for (slot = 0; slot < PARTY_AT_CHAPTER_28; slot++) {
        CHECK_EQ(seen28_party_char_ids[slot], chapter_28_party[slot]);
        CHECK_EQ(seen28_party_sides[slot], PLAYER_SIDE);
        CHECK_EQ(seen28_party_flags[slot], 0);
    }
}

/* What the reset leaves on the map, the cut-scene adding nothing to it: the
   twelve player slots and MAP27.DAT's thirty-seven wave-0 records, for
   forty-nine.  The census behind those thirty-seven is the strategy guide's
   敵方 list for the chapter found by character id and level -- four LV25
   黑暗祭司, nine LV25 幽魂, ten LV25 骷髏兵 and fourteen LV25 地獄犬 -- and
   all thirty-seven are on the enemy side, with no guest at all.  The four
   counts sum to the thirty-seven, so the census is pinned exactly, and the
   fifth counter catches anything on the board that is none of the four.

   The map's first record is asserted at map unit 12 by id and level and not
   merely counted, because that index is what says the reset's own deploy ran
   behind the player slots and in the map's record order: it is the first unit
   behind the twelve, and record 0 is the first record of the file.

   The waves the handler must not have deployed cannot be caught by an id here
   the way they can on the chapters either side -- all three of 84, 105 and 107
   appear in the nine later waves too -- so what catches them is the exact
   counts and the total: twenty-seven more units in waves 1 to 9, three per
   wave, would move every number in this case. */
static void chapter_twenty_eight_opens_with_the_guides_enemy_group(void)
{
    run_chapter_28_handler();
    CHECK_EQ(run28_state, 1);
    if (run28_state != 1) {
        return;
    }

    CHECK_EQ(seen28_unit_count, CH28_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen28_enemy_side_units, CH28_ENEMY_SIDE_UNITS);
    CHECK_EQ(seen28_guest_side_units, CH28_GUEST_SIDE_UNITS);

    CHECK_EQ(seen28_first_enemy_char_id, MAP27_SKELETON_CHAR_ID);
    CHECK_EQ(seen28_first_enemy_level, MAP27_ENEMY_LEVEL);

    CHECK_EQ(seen28_dark_priests, MAP27_DARK_PRIEST_COUNT);
    CHECK_EQ(seen28_wraiths, MAP27_WRAITH_COUNT);
    CHECK_EQ(seen28_skeletons, MAP27_SKELETON_COUNT);
    CHECK_EQ(seen28_hell_hounds, MAP27_HELL_HOUND_COUNT);
    CHECK_EQ(seen28_off_census_units, 0);
}

/* The cut-scene the handler names is Icon27.dat and not the neighbour on
   either side of it.  The marker sits on unit 48 with the value only
   ICON27.DAT writes, and neither decoy writes anything there: each marks unit
   0 with a value of its own, so unit 0's timers are read back clear as the
   other half of the same question.  Every other timer on the marked unit is
   clear too: neither the handler nor the shipped cut-scene writes a status on
   anybody this chapter, ICON27.DAT having no SET_UNIT_TIMER in its 267 bytes.

   The chapter id still reading 27 says the run ended on chapter 28's own map.
   Like chapters 26's and 27's members and unlike chapter 25's this one has no
   SWITCH_MAP at all, so the id is the one the dispatcher put there and nothing
   in the script ever touched it; both the member name and the title-card
   graphic are chosen from it by the callees. */
static void the_chapter_28_cutscene_is_icon27_dat(void)
{
    int slot;

    run_chapter_28_handler();
    CHECK_EQ(run28_state, 1);
    if (run28_state != 1) {
        return;
    }

    CHECK_EQ(seen28_marked_timers[SCRIPT_CH28_MARKER_SLOT],
             SCRIPT_CH28_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH28_MARKER_SLOT) {
            CHECK_EQ(seen28_marked_timers[slot], 0);
        }
        CHECK_EQ(seen28_unit0_timers[slot], 0);
    }

    CHECK_EQ(seen28_chapter_id, CHAPTER_28_ID);
}

/* The cursor ends on unit 0's tile, and on this chapter that tile is entirely
   the cut-scene's: ICON27.DAT places unit 0 on (11, 24) outright, over
   MAP27.COD's own (8, 18), and then walks it five tiles up, one right and two
   more up to (12, 17).  What the case pins is the call and its operand: unit 0
   is the only unit the fixture walks, and no other unit on the board stands on
   the tile it ends on.  The ten player slots the fixture places nowhere keep
   MAP27.COD start tiles of which none is (12, 17); slot 11, whose MAP27.COD
   start tile is (12, 17), is moved off it to (10, 24) by the fixture's second
   PLACE_UNIT, which is the shipped member's own; and none of the thirty-seven
   opening-wave enemies reads a (12, 17) tile out of MAP27.COD either.  So a
   handler that named any of the other forty-eight units lands on a different
   pair -- and a handler that made no cursor call at all leaves the zeroes
   fdps_chapter_state_reset writes over both globals, with the staged (14, 10) a
   third value again. */
static void the_chapter_28_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_28_handler();
    CHECK_EQ(run28_state, 1);
    if (run28_state != 1) {
        return;
    }

    CHECK_EQ(seen28_cursor_x, CH28_CURSOR_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen28_cursor_y, CH28_CURSOR_TILE_Y * CURSOR_TILE_STEP);
}

/* Takes chapter 28's fixture container away again, so a later test file can
   stage its own.  A container this section did not create is somebody else's
   and is left where it stands, which is also the only path on which this case
   asserts nothing. */
static void the_chapter_28_fixture_container_is_removed(void)
{
    if (!fixture28_owned) {
        return;
    }

    remove(SCRIPT_ARCHIVE_FILE);
    fixture28_owned = 0;
    CHECK_EQ(file_present(SCRIPT_ARCHIVE_FILE), 0);
}

void run_chinit2b_tests(void)
{
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_twenty_five);
    RUN_TEST(chapter_twenty_five_is_fought_without_flarena);
    RUN_TEST(chapter_twenty_five_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_25_cutscene_is_icon24_dat);
    RUN_TEST(the_chapter_25_cursor_is_parked_on_unit_zero);
    RUN_TEST(the_fixture_container_is_removed);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_twenty_six);
    RUN_TEST(chapter_twenty_six_is_fought_without_flarena);
    RUN_TEST(chapter_twenty_six_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_26_cutscene_is_icon25_dat);
    RUN_TEST(the_chapter_26_cursor_is_parked_on_unit_zero);
    RUN_TEST(the_chapter_26_fixture_container_is_removed);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_twenty_seven);
    RUN_TEST(chapter_twenty_seven_is_fought_without_flarena);
    RUN_TEST(chapter_twenty_seven_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_27_cutscene_is_icon26_dat);
    RUN_TEST(the_chapter_27_cursor_is_parked_on_unit_zero);
    RUN_TEST(the_chapter_27_fixture_container_is_removed);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_twenty_eight);
    RUN_TEST(chapter_twenty_eight_is_fought_with_the_whole_party);
    RUN_TEST(chapter_twenty_eight_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_28_cutscene_is_icon27_dat);
    RUN_TEST(the_chapter_28_cursor_is_parked_on_unit_zero);
    RUN_TEST(the_chapter_28_fixture_container_is_removed);
}
