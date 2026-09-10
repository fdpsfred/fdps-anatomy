/* tests/chinit2.c -- cover for src/chinit2.c.
 *
 * One entry handler per section, in address order.  So far that is
 * fdps_chapter_16_init at 00021290, fdps_chapter_17_init at 000212d0,
 * fdps_chapter_18_init at 00021310, fdps_chapter_19_init at 00021350,
 * fdps_chapter_20_init at 00021390, fdps_chapter_21_init at 000213d0 and
 * fdps_chapter_22_init at 00021410.
 *
 * WHAT THE HANDLER IS.  fdps_chapter_16_init is four calls with no branch, no
 * loop and no store anywhere in it, so nothing about it is worth testing in
 * pieces: what it decides is WHICH cut-scene member is opened, WHICH unit the
 * cursor is parked on, and the ORDER the four calls run in.  The run below
 * therefore enters chapter 16 once for real against the shipped containers and
 * reads the answers off the state it leaves, and every case asserts against
 * that one run.
 *
 * Expected values come from the assembly at 00021290 and from the shipped
 * data, never from the emitted C:
 *
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x618b8 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon15.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * NOBODY JOINS AND NO SLOT IS LEFT OVER.  The party is ten members when the
 * chapter opens -- the eight chapters 1 to 4 and 7 to 9 put on, 琴琴 from
 * chapter 11 and 瑪麗安 from chapter 15 -- and MAP15.DAT is the first map in
 * the game to ask for ten player slots, so every slot has a member behind it
 * and the array carries no zeroed, retired spare.  There is no untouched
 * roster slot to read the 0xff sentinel out of, so what says the handler added
 * nobody is the count still reading ten.
 *
 * HALF THE PARTY IS RETIRED BY THE CUT-SCENE.  The shipped ICON15.DAT holds
 * five RETIREs in a row -- map units 3, 4, 7, 8 and 9 -- and no REVIVE
 * anywhere, and a player slot's map unit index is its roster slot, so the five
 * the player commands this chapter are roster slots 0, 1, 2, 5 and 6:
 * 蘭迪斯, 尤利安, 亞克, 費塔加 and 布蘭多, which is the strategy guide's own
 * 己方 line for the chapter.  That is the one thing this chapter's map cannot
 * express by itself, so the fixture below keeps those five opcodes verbatim.
 *
 * WHY THE CUT-SCENE IS A FIXTURE AND NOT THE SHIPPED ONE.  Same reason
 * tests/chinit1.c and tests/chinit1b.c give for their runs: the shipped
 * ICON15.DAT walks and poses actors through ninety-odd opcodes, shakes the
 * view, sets a CD music track and draws chapter text, none of which this file
 * is asking about and all of which cost a rendered frame per sub-step.  The
 * staged ICON15.DAT keeps the shipped member's five RETIREs and its four
 * DEPLOY_WAVEs -- waves 1, 2, 3 and 4 in that order, each with the place-exact
 * operand 1, which is what the shipped bytes carry when walked with the opcode
 * ladder in src/icon.c -- names the tile the shipped member's group walks
 * leave unit 0 on, and adds a marker so the run can say which member the
 * interpreter opened.
 *
 * WHY THE NEIGHBOURING MEMBERS ARE STAGED TOO.  Icon15.dat is chapter
 * SIXTEEN's script -- the number in the name is the 0-based chapter id -- so
 * the mistake this file has to be able to catch is a handler naming the member
 * one either side of its own, and the member one late is the one a rewrite
 * that read the chapter number out of the handler's own name would reach for.
 * A container holding only the member under test would turn that mistake into
 * a member the interpreter cannot find, and a member it cannot find sends the
 * interpreter into fdps_wait_any_key, which spins for a keyboard interrupt
 * that never comes.  So the container holds nine members: ICON15.DAT,
 * ICON16.DAT, ICON17.DAT, ICON18.DAT, ICON19.DAT, ICON20.DAT and ICON21.DAT,
 * the seven under test, each of which is also its neighbour's neighbour, and
 * ICON14.DAT and ICON22.DAT at the two ends, decoys that deploy nobody, retire
 * nobody, switch no map and mark unit 0 with a value of their own.  A run that
 * opened the wrong member is caught whichever it was: the two decoys leave a
 * full party on the map and a marked unit 0, and each of the seven real
 * members leaves a party state no other member produces and writes its own
 * marker value on a unit index no other member reaches.
 *
 * WHAT IS STAGED AND WHAT IS REAL.  The four data tables and the roster block
 * are staged here, because they are ticket 23 symbols the build links
 * zero-filled; the character rows are FRIAPRDA.DAT's and FRILEVUP.DAT's own
 * numbers as recorded in assets/characters.md.  Everything the chapter load
 * reads is the real container: MAP15.DAT's counts and wave tags, MAP15.COD's
 * placement records, the tile layers, ICON.CEL's sprites, MISC.VFS's title
 * card.  Nothing below asserts what any global held before the run.
 *
 * The staging REFUSES TO OVERWRITE an IconAni.vfs that is already in the run
 * directory, and removes its own again in the last case: tests/chinit1.c,
 * tests/chinit1b.c and tests/icon.c stage a container of the same name for
 * their own fixtures and each skips itself if one is already there.
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
#include "chinit2.h"

/* The shipped containers the run needs, and the sheet reader's own floor:
   fdps_cache_cel_sprite_group takes a fixed 0x2970-byte bite out of ICON.CEL's
   offset table, so a shorter file is one it runs off the end of.  Same guard
   tests/chinit1.c, tests/chinit1b.c and tests/deploy.c use. */
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
#define FIXTURE_MEMBERS 9

/* The side code the player's own units carry and the one a map deployment
   carries, and the flag bit a RETIRE raises. */
#define PLAYER_SIDE 2
#define ENEMY_SIDE 0
#define UNIT_FLAG_RETIRED 1

/* How many rows the staged tables carry.  The enemy table is indexed by
   char_id - 0x3c (src/deploy.c) and this chapter's highest id is 103, which is
   row 43; the item table is indexed by an id the roster add takes straight out
   of an inventory entry with no bound of any kind. */
#define TABLE_ROWS 128
#define ITEM_TABLE_ROWS 256

/* The roster block: one 0x50-byte record per member, ten of them in use. */
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

/* --- fdps_chapter_16_init @ 00021290 constants -------------------------- */

/* Chapter 16 is chapter id 15, and this handler is slot 15 of the table at
   00060074 -- the dword at 000600b0 is 00021290, and that data reference is
   the function's only xref. */
#define CHAPTER_16_ID 15

/* The party when chapter 16 opens: the ten members chapters 1 to 4, 7 to 9, 11
   and 15 put on.  This handler adds nobody -- there is no PUSH/CALL
   0x00023bc0 anywhere in its body. */
#define PARTY_AT_CHAPTER_16 10

/* The ten characters and their own FRIAPRDA.DAT and FRILEVUP.DAT rows
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

/* MAP15.DAT's own header bytes, read back to prove chapter 16's map is the one
   that loaded: ten player slots at +1 and twenty-five scripted deployments at
   +2 (src/rsrc.c).  Both were staged at other numbers before the run. */
#define CH15_PLAYER_SLOTS 10
#define CH15_CHAR_SPAWNS 25

/* MAP15.DAT tags thirteen of its twenty-five deployments wave 0, so the
   chapter state reset's own opening deploy puts those thirteen down behind the
   ten player slots, at indices 10 to 22. */
#define CH15_WAVE_ZERO_UNITS 13

/* What the fixture ICON15.DAT below asks for, and what the shipped member asks
   for: four DEPLOY_WAVEs, waves 1, 2, 3 and 4 in that order, each with the
   place operand 1 -- the exact-anchor form rather than the nearest-free-tile
   search (src/deploy.c).  Every number is read off the shipped ICON15.DAT
   itself, walked with the opcode ladder in src/icon.c: the four are at script
   offsets 598, 609, 628 and 636, and the member carries no SWITCH_MAP at all.
   MAP15.DAT tags two records wave 1, four wave 2, four wave 3 and two wave 4,
   which is the other twelve of its twenty-five. */
#define SCRIPT_CH16_FIRST_WAVE 1
#define SCRIPT_CH16_SECOND_WAVE 2
#define SCRIPT_CH16_THIRD_WAVE 3
#define SCRIPT_CH16_FOURTH_WAVE 4
#define SCRIPT_CH16_PLACE_EXACT 1
#define CH15_SCRIPT_WAVE_UNITS 12

/* How long the unit array is when the handler returns: the ten player slots,
   the map's thirteen wave-0 records, and the twelve the cut-scene deploys. */
#define CH15_UNITS_AFTER_SCRIPT \
    (CH15_PLAYER_SLOTS + CH15_WAVE_ZERO_UNITS + CH15_SCRIPT_WAVE_UNITS)

/* The census the twenty-five deployments come to, by character id and level,
   which is the strategy guide's 敵方 list for the chapter to the number.  The
   ids are the ones tests/chinit1b.c pins at their own levels for chapters 13
   and 14 -- 99 武士, 94 弓箭手, 96 飛兵, 89 騎士 and 103 暗魔導士 -- and the
   levels are MAP15.DAT's own record bytes:

     character 103, level 16, four  -- LV16 暗魔導士, waves 1 and 4
     character 89,  level 11, four  -- LV11 騎士, wave 0
     character 99,  level 14, nine  -- LV14 武士, five wave 0 and four wave 3
     character 94,  level 14, four  -- LV14 弓箭手, wave 0
     character 96,  level 12, four  -- LV12 飛兵, wave 2 */
#define CH15_DARK_MAGE_CHAR_ID 103
#define CH15_DARK_MAGE_LEVEL 16
#define CH15_DARK_MAGE_COUNT 4
#define CH15_KNIGHT_CHAR_ID 89
#define CH15_KNIGHT_LEVEL 11
#define CH15_KNIGHT_COUNT 4
#define CH15_SAMURAI_CHAR_ID 99
#define CH15_SAMURAI_LEVEL 14
#define CH15_SAMURAI_COUNT 9
#define CH15_ARCHER_CHAR_ID 94
#define CH15_ARCHER_LEVEL 14
#define CH15_ARCHER_COUNT 4
#define CH15_FLIER_CHAR_ID 96
#define CH15_FLIER_LEVEL 12
#define CH15_FLIER_COUNT 4
#define CH15_GUIDE_ENEMY_TOTAL 25

/* Which player slots the cut-scene retires and which it leaves standing.  The
   five RETIREs sit in a row at script offsets 438 to 446 of the shipped
   ICON15.DAT and name map units 3, 4, 7, 8 and 9; the member carries no REVIVE
   anywhere, so they are still retired when the handler returns.  A player
   slot's map unit index is its roster slot and the roster is in join order, so
   the five taken off are 法蓮娜, 裘娜, 蓋亞, 琴琴 and 瑪麗安 and the five left
   are 蘭迪斯, 尤利安, 亞克, 費塔加 and 布蘭多 -- the guide's 己方 line. */
#define CH16_RETIRED_SLOTS 5
#define CH16_LIVE_SLOTS 5

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 34 is
   the last of the thirty-five and exists only if all four DEPLOY_WAVEs went
   down -- it is MAP15.DAT's record 24, the last of wave 4 -- and the value is
   one no other member of the container writes.  The slot is status_timers[4],
   record offset 0x26, for the reason tests/chinit1.c gives: it is the one
   status byte fdps_unit_select_status_icon does not read, so marking it cannot
   send fdps_draw_map_unit through the null status-icon sheet.  The opcode's
   operand is measured from status_timers[3] (src/icon.c), which makes that
   operand 1.  The shipped member has no SET_UNIT_TIMER of its own, so this
   marker is the fixture's alone. */
#define SCRIPT_CH16_MARKER_UNIT (CH15_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH16_MARKER_OPERAND 1
#define SCRIPT_CH16_MARKER_SLOT 4
#define SCRIPT_CH16_MARKER_VALUE 87

/* What the two end decoys write, and where.  Unit 0 exists whichever member
   is opened, so a decoy's mark lands somewhere every case can read.  Neither
   value is one a real member writes.  The names are the chapters whose scripts
   they are: ICON14.DAT is chapter 15's and ICON22.DAT is chapter 23's, one
   below the first handler under test and one above the last. */
#define SCRIPT_DECOY_MARKER_UNIT 0
#define SCRIPT_DECOY_MARKER_OPERAND 1
#define SCRIPT_CH15_DECOY_VALUE 85
#define SCRIPT_CH23_DECOY_VALUE 103

/* Where the cut-scene leaves map unit 0.  MAP15.COD record 25 -- the first
   record past the map's twenty-five scripted ones, and so player slot 0's
   start tile -- is (19, 38), so a cursor that ended there would be a run whose
   script never opened.  The shipped member has no PLACE_UNIT at all: it steps
   unit 0 with nine group walks, four tiles up at script offset 5 and then
   single steps at offsets 44, 50, 89, 95, 117, 488, 501, 514, 527, 540, 555
   and 568, a net of two tiles right and five up, which is (21, 33).  The
   fixture names that tile outright, because the walks it would take to get
   there hold a rendered frame per sub-step and prove nothing this file is
   asking about.  The facing is the one the shipped member's closing FACE_UNITS
   at offset 639 leaves. */
#define SCRIPT_CH16_WALKED_UNIT 0
#define SCRIPT_CH16_WALKED_TILE_X 21
#define SCRIPT_CH16_WALKED_TILE_Y 33
#define SCRIPT_CH16_WALKED_FACING 2

/* The five map units the RETIREs name, spelled out where the fixture bytes
   need them. */
#define SCRIPT_CH16_RETIRED_UNIT_A 3
#define SCRIPT_CH16_RETIRED_UNIT_B 4
#define SCRIPT_CH16_RETIRED_UNIT_C 7
#define SCRIPT_CH16_RETIRED_UNIT_D 8
#define SCRIPT_CH16_RETIRED_UNIT_E 9

/* --- fdps_chapter_17_init @ 000212d0 constants -------------------------- */

/* Chapter 17 is chapter id 16, and this handler is slot 16 of the table at
   00060074 -- the dword at 000600b4 is 000212d0, and that data reference is
   the function's only xref. */
#define CHAPTER_17_ID 16

/* The party when chapter 17 opens: the same ten chapter 16 opened with.  This
   handler adds nobody either -- there is no PUSH/CALL 0x00023bc0 anywhere in
   its body -- so the join order above is this chapter's player-slot order
   too. */
#define PARTY_AT_CHAPTER_17 10

/* MAP16.DAT's own header bytes, read back to prove chapter 17's map is the one
   that loaded: ten player slots at +1 and thirty scripted deployments at +2
   (src/rsrc.c).  Both were staged at other numbers before the run.

   The MAP16_ prefix below is the FILE's name and not chapter 16's: a map file
   is numbered by the 0-based chapter id, so chapter 17 fights on MAP16.DAT the
   way chapter 16 fights on MAP15.DAT. */
#define MAP16_PLAYER_SLOTS 10
#define MAP16_CHAR_SPAWNS 30

/* MAP16.DAT tags twenty-three of its thirty deployments wave 0, so the chapter
   state reset's own opening deploy puts those twenty-three down behind the ten
   player slots, at indices 10 to 32.  The shipped ICON16.DAT has no
   DEPLOY_WAVE at all, so nothing is added behind them and the array is
   thirty-three units long when the handler returns. */
#define MAP16_WAVE_ZERO_UNITS 23
#define CH17_UNITS_AFTER_SCRIPT     (PARTY_AT_CHAPTER_17 + MAP16_WAVE_ZERO_UNITS)

/* What the twenty-three wave-0 records are.  Twenty carry the enemy side and
   are the strategy guide's opening 敵方 list for the chapter to the number, by
   character id and level -- the ids are the ones tests/chinit1b.c and the
   chapter 16 section above pin at their own levels:

     character 103, level 16, two   -- LV16 暗魔導士
     character 89,  level 11, six   -- LV11 騎士
     character 99,  level 15, ten   -- LV15 武士
     character 94,  level 14, two   -- LV14 弓箭手

   The other three carry side 1, the guest side (src/aitarget.c): characters 12
   and 14 at level 10 and character 91 at level 20 -- the hostages the chapter
   is named for. */
#define MAP16_DARK_MAGE_CHAR_ID 103
#define MAP16_DARK_MAGE_LEVEL 16
#define MAP16_DARK_MAGE_COUNT 2
#define MAP16_KNIGHT_CHAR_ID 89
#define MAP16_KNIGHT_LEVEL 11
#define MAP16_KNIGHT_COUNT 6
#define MAP16_SAMURAI_CHAR_ID 99
#define MAP16_SAMURAI_LEVEL 15
#define MAP16_SAMURAI_COUNT 10
#define MAP16_ARCHER_CHAR_ID 94
#define MAP16_ARCHER_LEVEL 14
#define MAP16_ARCHER_COUNT 2
#define MAP16_GUIDE_OPENING_ENEMY_TOTAL 20
#define GUEST_SIDE 1
#define MAP16_GUEST_UNITS 3

/* The seven records the reset does NOT put down, because they are tagged for
   waves the chapter's own turn events fire later: six more LV15 武士 on wave 1
   -- the guide's 第八回合 reinforcement -- and one LV14 狼人, character 83, on
   wave 2, the one that comes for the 強化套件 on the ninth.  A run whose
   cut-scene deployed anything would show them. */
#define MAP16_WOLF_CHAR_ID 83
#define MAP16_WOLF_LEVEL 14

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 32 is
   the last of the thirty-three and exists only if the reset put every wave-0
   record down -- it is MAP16.DAT's record 22 -- and the value is one no other
   member of the container writes.  The slot is status_timers[4], record offset
   0x26, for the reason tests/chinit1.c gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1.
   The shipped member has no SET_UNIT_TIMER of its own, so this marker is the
   fixture's alone. */
#define SCRIPT_CH17_MARKER_UNIT (CH17_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH17_MARKER_OPERAND 1
#define SCRIPT_CH17_MARKER_SLOT 4
#define SCRIPT_CH17_MARKER_VALUE 91

/* Which player slots the cut-scene retires and which it leaves standing -- the
   exact complement of chapter 16's five.  The five RETIREs sit in a row at
   script offsets 32 to 41 of the shipped ICON16.DAT and name map units 0, 1,
   2, 5 and 6; the member carries no REVIVE anywhere, so they are still retired
   when the handler returns.  A player slot's map unit index is its roster slot
   and the roster is in join order, so the five taken off are 蘭迪斯, 尤利安,
   亞克, 費塔加 and 布蘭多 and the five left are 法蓮娜, 裘娜, 蓋亞, 琴琴 and
   瑪麗安 -- the guide's 己方 line. */
#define CH17_RETIRED_SLOTS 5
#define CH17_LIVE_SLOTS 5

/* The five map units the RETIREs name, spelled out where the fixture bytes
   need them. */
#define SCRIPT_CH17_RETIRED_UNIT_A 0
#define SCRIPT_CH17_RETIRED_UNIT_B 1
#define SCRIPT_CH17_RETIRED_UNIT_C 2
#define SCRIPT_CH17_RETIRED_UNIT_D 5
#define SCRIPT_CH17_RETIRED_UNIT_E 6

/* Where the cut-scene leaves the unit the cursor call names.  The shipped
   ICON16.DAT places map units 3, 4, 7, 8 and 9 on (18, 18) at script offsets 7
   to 31, moves the group to (1, 6) at offsets 84 to 108, then walks unit 3 one
   tile right at offset 122 -- facing operand 3, the right arm of the walk
   commit -- and one tile down at offset 134, which leaves it on (2, 7) facing
   down.  The fixture names that tile outright, because the walks it would take
   to get there hold a rendered frame per sub-step and prove nothing this file
   is asking about. */
#define SCRIPT_CH17_WALKED_UNIT 3
#define SCRIPT_CH17_WALKED_TILE_X 2
#define SCRIPT_CH17_WALKED_TILE_Y 7
#define SCRIPT_CH17_WALKED_FACING 0

/* What the cursor globals are staged at before the chapter 17 run.  Not
   stage_globals' own 48 and 72: 48 is 2 * 24, which is this chapter's own
   answer on the x axis, so leaving it there would let a run that never made
   the cursor call pass the x assertion. */
#define CH17_STAGED_CURSOR_X 480
#define CH17_STAGED_CURSOR_Y 456

/* --- fdps_chapter_18_init @ 00021310 constants -------------------------- */

/* Chapter 18 is chapter id 17, and this handler is slot 17 of the table at
   00060074 -- the dword at 000600b8 is 00021310, and that data reference is
   the function's only xref. */
#define CHAPTER_18_ID 17

/* The party when chapter 18 opens: the same ten chapters 16 and 17 opened
   with.  This handler adds nobody either -- there is no PUSH/CALL 0x00023bc0
   anywhere in its body -- so the join order above is this chapter's
   player-slot order too. */
#define PARTY_AT_CHAPTER_18 10

/* MAP17.DAT's own header bytes, read back to prove chapter 18's map is the one
   that loaded: ten player slots at +1 and sixty-five scripted deployments at
   +2 (src/rsrc.c).  Both were staged at other numbers before the run.

   The MAP17_ prefix is the FILE's name and not chapter 17's: a map file is
   numbered by the 0-based chapter id. */
#define MAP17_PLAYER_SLOTS 10
#define MAP17_CHAR_SPAWNS 65

/* What the reset's own opening deploy puts down, and what the cut-scene adds.
   MAP17.DAT tags exactly two of its sixty-five records wave 0 -- records 31
   and 33, the scene's own actors -- so the opening deploy is two units and not
   an enemy group; the cut-scene's DEPLOY_WAVE 2 at script offset 394 adds
   MAP17.DAT's single wave-2 record 32, and its DEPLOY_WAVE 1 at offset 753
   adds the fifteen wave-1 records that are the chapter's opposition. */
#define MAP17_WAVE_ZERO_UNITS 2
#define MAP17_SCRIPT_WAVE_TWO_UNITS 1
#define MAP17_SCRIPT_WAVE_ONE_UNITS 15
#define CH18_UNITS_AFTER_SCRIPT \
    (PARTY_AT_CHAPTER_18 + MAP17_WAVE_ZERO_UNITS \
     + MAP17_SCRIPT_WAVE_TWO_UNITS + MAP17_SCRIPT_WAVE_ONE_UNITS)

/* The two waves the cut-scene deploys and how each is placed: wave 2 with the
   place operand 1, the exact anchor, and wave 1 with the place operand 0, the
   nearest-free-tile search (src/deploy.c).  Both are read off the shipped
   ICON17.DAT itself, walked with the opcode ladder in src/icon.c. */
#define SCRIPT_CH18_SCENE_WAVE 2
#define SCRIPT_CH18_SCENE_WAVE_PLACE_EXACT 1
#define SCRIPT_CH18_ENEMY_WAVE 1
#define SCRIPT_CH18_ENEMY_WAVE_PLACE_SEARCH 0

/* The three units the cut-scene acts with, and what they are.  Map units 10
   and 11 are MAP17.DAT's two wave-0 records: record 31, character 12 at level
   10 on side 1, the guest side (src/aitarget.c), and record 33, character 117
   at level 5.  Map unit 12 is the wave-2 record 32, character 67 at level 5.
   All three carry the retired bit when the handler returns: the member retires
   10 and 11 at script offsets 386 and 388, revives 10 at 392, and retires 12
   at 676 and 10 again at 735. */
#define CH18_SCENE_ACTOR_UNITS 3
#define CH18_GUEST_CHAR_ID 12
#define CH18_GUEST_LEVEL 10
#define CH18_SCENE_CHAR_ID_A 117
#define CH18_SCENE_CHAR_ID_B 67
#define CH18_SCENE_LEVEL 5

/* The census the cut-scene's wave 1 comes to, by character id and level, which
   is the strategy guide's opening 敵方 list for the chapter to the number.
   The two ids the file already pins elsewhere are here at this chapter's own
   levels -- 89 騎士 and 94 弓箭手 -- and 103 暗魔導士 is the same id chapters
   16 and 17 carry:

     character 90,  level 14, five  -- LV14 衛兵
     character 94,  level 15, two   -- LV15 弓箭手
     character 89,  level 13, five  -- LV13 騎士
     character 77,  level 14, two   -- LV14 暗黑騎士
     character 103, level 16, one   -- LV16 暗魔導士 */
#define MAP17_GUARD_CHAR_ID 90
#define MAP17_GUARD_LEVEL 14
#define MAP17_GUARD_COUNT 5
#define MAP17_ARCHER_CHAR_ID 94
#define MAP17_ARCHER_LEVEL 15
#define MAP17_ARCHER_COUNT 2
#define MAP17_KNIGHT_CHAR_ID 89
#define MAP17_KNIGHT_LEVEL 13
#define MAP17_KNIGHT_COUNT 5
#define MAP17_DARK_KNIGHT_CHAR_ID 77
#define MAP17_DARK_KNIGHT_LEVEL 14
#define MAP17_DARK_KNIGHT_COUNT 2
#define MAP17_DARK_MAGE_CHAR_ID 103
#define MAP17_DARK_MAGE_LEVEL 16
#define MAP17_DARK_MAGE_COUNT 1

/* The reinforcement the chapter's own turn events bring in later and this run
   must not see: sixteen LV15 飛兵 on waves 4, 6, 8 and 10 -- the guide's
   第四、六、八、十回合 flights -- with sixteen more LV13 騎士 on waves 5, 7, 9
   and 11 behind them, and a fifteen-record wave 13 that repeats the opening
   group on 第十三回合. */
#define MAP17_FLIER_CHAR_ID 96
#define MAP17_FLIER_LEVEL 15

/* How many units are on each side when the handler returns.  The fifteen the
   cut-scene deploys plus map units 11 and 12, the two scene actors that carry
   the enemy side, are seventeen on side 0; map unit 10 is the one guest. */
#define CH18_ENEMY_SIDE_UNITS 17
#define CH18_GUEST_SIDE_UNITS 1

/* Which player slots the cut-scene takes off the board and puts back.  The
   five RETIREs at script offsets 37 to 45 name map units 3, 4, 7, 8 and 9 --
   chapter 16's own five -- and the five REVIVEs at offsets 429 to 437 name the
   same five, which is what makes this member the only one of the three that
   leaves every player slot standing.  A player slot's map unit index is its
   roster slot and the roster is in join order, so the ten the player commands
   are the whole party, and the guide names nobody sitting the chapter out. */
#define SCRIPT_CH18_RETIRED_UNIT_A 3
#define SCRIPT_CH18_RETIRED_UNIT_B 4
#define SCRIPT_CH18_RETIRED_UNIT_C 7
#define SCRIPT_CH18_RETIRED_UNIT_D 8
#define SCRIPT_CH18_RETIRED_UNIT_E 9

/* The three scene actors the fixture retires and revives in the shipped
   member's own order. */
#define SCRIPT_CH18_ACTOR_UNIT_GUEST 10
#define SCRIPT_CH18_ACTOR_UNIT_SECOND 11
#define SCRIPT_CH18_ACTOR_UNIT_THIRD 12

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 27 is
   the last of the twenty-eight and exists only if both of the member's
   DEPLOY_WAVEs went down -- it is MAP17.DAT's record 14, the last of wave 1 --
   and the value is one no other member of the container writes.  The slot is
   status_timers[4], record offset 0x26, for the reason tests/chinit1.c gives:
   it is the one status byte fdps_unit_select_status_icon does not read, so
   marking it cannot send fdps_draw_map_unit through the null status-icon
   sheet.  The opcode's operand is measured from status_timers[3] (src/icon.c),
   which makes that operand 1.  The shipped member has no SET_UNIT_TIMER of its
   own, so this marker is the fixture's alone. */
#define SCRIPT_CH18_MARKER_UNIT (CH18_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH18_MARKER_OPERAND 1
#define SCRIPT_CH18_MARKER_SLOT 4
#define SCRIPT_CH18_MARKER_VALUE 93

/* Where the cut-scene leaves the unit the cursor call names.  MAP17.COD gives
   all ten player slots the same start tile, (3, 18) -- records 65 to 74, the
   first records past the map's sixty-five scripted ones -- and the shipped
   member places map unit 0 on (17, 24) at script offset 108 and then walks it
   five tiles up, three at offset 126 and one each at 140 and 154, which leaves
   it on (17, 19) facing up.  The fixture names that tile outright, because the
   walks it would take to get there hold a rendered frame per sub-step and
   prove nothing this file is asking about. */
#define SCRIPT_CH18_WALKED_UNIT 0
#define SCRIPT_CH18_WALKED_TILE_X 17
#define SCRIPT_CH18_WALKED_TILE_Y 19
#define SCRIPT_CH18_WALKED_FACING 2

/* What the cursor globals are staged at before the chapter 18 run: neither the
   tile the cut-scene leaves unit 0 on nor the tile MAP17.COD gave it, so a run
   that never made the cursor call and a run that made it before the script are
   both told apart from the real answer. */
#define CH18_STAGED_CURSOR_X 600
#define CH18_STAGED_CURSOR_Y 624

/* --- fdps_chapter_19_init @ 00021350 constants -------------------------- */

/* Chapter 19 is chapter id 18, and this handler is slot 18 of the table at
   00060074 -- the dword at 000600bc is 00021350, and that data reference is
   the function's only xref. */
#define CHAPTER_19_ID 18

/* The party when chapter 19 opens, BEFORE the handler's own add: the same ten
   chapters 16 to 18 were fought with. */
#define PARTY_AT_CHAPTER_19 10

/* Who the handler puts on the roster and what the add computes for him.
   PUSH 0xb at 0002135c is 蘭斯洛特 the 聖騎士, and FRIAPRDA.DAT row 11 with
   FRILEVUP.DAT row 11 is level 15 on 420 base HP and 0 base MP with 14 HP and
   0 MP a level (assets/characters.md), so fdps_roster_add_character's
   hp_base + hp_min * (level - 1) comes to 616 HP and 0 MP.  The strategy
   guide's 備註 for the chapter prints the same pair -- it says that finishing
   before the turn-6 arrival event still leaves 蘭斯洛特 in the party at LV15
   with HP616 and MP0 -- so the two are checked against each other rather than
   either being assumed.  He lands at roster slot 10, behind the ten. */
#define LANCELOT_CHAR_ID 11
#define LANCELOT_ROSTER_SLOT 10
#define LANCELOT_LEVEL 15
#define LANCELOT_HP_BASE 420
#define LANCELOT_MP_BASE 0
#define LANCELOT_HP_MIN 14
#define LANCELOT_MP_MIN 0
#define LANCELOT_HP_MAX 616
#define LANCELOT_MP_MAX 0

/* MAP18.DAT's own header bytes, read back to prove chapter 19's map is the one
   that loaded: ten player slots at +1 and sixty-eight scripted deployments at
   +2 (src/rsrc.c).  Both were staged at other numbers before the run.

   Ten player slots against a roster the add has just made eleven is what keeps
   蘭斯洛特 off this chapter's board: fdps_build_map_unit_array fills slot i
   from roster slot i only while i is below the roster count, and slot 10 is
   one past the last slot the map asks for (src/deploy.c).

   The MAP18_ prefix is the FILE's name and not chapter 18's: a map file is
   numbered by the 0-based chapter id. */
#define MAP18_PLAYER_SLOTS 10
#define MAP18_CHAR_SPAWNS 68

/* What the reset's own opening deploy puts down and what the cut-scene adds.
   MAP18.DAT tags five of its sixty-eight records wave 0 -- LV15 暗黑騎士 --
   which the reset appends as map units 10 to 14, and the shipped ICON18.DAT's
   single DEPLOY_WAVE at script offset 196 asks for wave 5 with the place
   operand 0, the nearest-free-tile search rather than the exact anchor
   (src/deploy.c), which is thirty-nine more.  Both numbers are read off the
   shipped files themselves, the script walked with the opcode ladder in
   src/icon.c. */
#define MAP18_WAVE_ZERO_UNITS 5
#define SCRIPT_CH19_ENEMY_WAVE 5
#define SCRIPT_CH19_ENEMY_WAVE_PLACE_SEARCH 0
#define MAP18_SCRIPT_WAVE_FIVE_UNITS 39
#define CH19_UNITS_AFTER_SCRIPT \
    (PARTY_AT_CHAPTER_19 + MAP18_WAVE_ZERO_UNITS \
     + MAP18_SCRIPT_WAVE_FIVE_UNITS)

/* The census the two deploys come to, by character id and level, which is the
   strategy guide's opening 敵方 list for the chapter to the number.  Three of
   the ids the file already pins elsewhere are here at this chapter's own
   levels -- 94 弓箭手, 96 飛兵 and 103 暗魔導士 -- and 77 暗黑騎士 is the id
   chapter 18 carries at level 14:

     character 94,  level 17, fourteen -- LV17 弓箭手
     character 103, level 18, seven    -- LV18 暗魔導士
     character 96,  level 17, six      -- LV17 飛兵
     character 80,  level 17, five     -- LV17 野蠻戰士
     character 77,  level 15, twelve   -- LV15 暗黑騎士, five of them the
                                         reset's wave 0 and seven the script's
                                         wave 5

   Every one of the forty-four carries side 0, so the guest side is empty this
   chapter. */
#define MAP18_ARCHER_CHAR_ID 94
#define MAP18_ARCHER_LEVEL 17
#define MAP18_ARCHER_COUNT 14
#define MAP18_DARK_MAGE_CHAR_ID 103
#define MAP18_DARK_MAGE_LEVEL 18
#define MAP18_DARK_MAGE_COUNT 7
#define MAP18_FLIER_CHAR_ID 96
#define MAP18_FLIER_LEVEL 17
#define MAP18_FLIER_COUNT 6
#define MAP18_BARBARIAN_CHAR_ID 80
#define MAP18_BARBARIAN_LEVEL 17
#define MAP18_BARBARIAN_COUNT 5
#define MAP18_DARK_KNIGHT_CHAR_ID 77
#define MAP18_DARK_KNIGHT_LEVEL 15
#define MAP18_DARK_KNIGHT_COUNT 12
#define CH19_ENEMY_SIDE_UNITS \
    (MAP18_WAVE_ZERO_UNITS + MAP18_SCRIPT_WAVE_FIVE_UNITS)
#define CH19_GUEST_SIDE_UNITS 0

/* The reinforcement the chapter's own turn events bring in later and this run
   must not see: MAP18.DAT's wave 6 is fourteen LV17 飛兵 and eight LV15
   武鬥家, the group the guide has arriving on the left, right and top when the
   party crosses the line above the blue chest.  The 武鬥家 id appears nowhere
   in the opening board, so a run that fired that wave early is caught by a
   non-zero count of it. */
#define MAP18_BRAWLER_CHAR_ID 109
#define MAP18_BRAWLER_LEVEL 15

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 53 is
   the last of the fifty-four and exists only if the member's DEPLOY_WAVE went
   down -- it is MAP18.DAT's record 44, the last of wave 5 -- and the value is
   one no other member of the container writes.  The slot is status_timers[4],
   record offset 0x26, for the reason tests/chinit1.c gives: it is the one
   status byte fdps_unit_select_status_icon does not read, so marking it cannot
   send fdps_draw_map_unit through the null status-icon sheet.  The opcode's
   operand is measured from status_timers[3] (src/icon.c), which makes that
   operand 1.  The shipped member has no SET_UNIT_TIMER of its own -- it has no
   unit-state opcode at all -- so this marker is the fixture's alone. */
#define SCRIPT_CH19_MARKER_UNIT (CH19_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH19_MARKER_OPERAND 1
#define SCRIPT_CH19_MARKER_SLOT 4
#define SCRIPT_CH19_MARKER_VALUE 95

/* Where the cut-scene leaves the unit the cursor call names.  MAP18.COD gives
   the ten player slots ten different start tiles -- records 68 to 77, the
   first records past the map's sixty-eight scripted ones -- and slot 0's is
   (12, 2), so a cursor that ended there would be a run whose script never
   opened.  The shipped member has no PLACE_UNIT at all: eight of its
   nine WALK_UNITS opcodes list map unit 0, at script offsets 14, 20, 44, 68,
   92, 121, 160 and 166, and their facings come to one tile left and four tiles
   down net, which is (11, 6) facing up.  The fixture names that tile outright,
   because the walks it would take to get there hold a rendered frame per
   sub-step and prove nothing this file is asking about. */
#define SCRIPT_CH19_WALKED_UNIT 0
#define SCRIPT_CH19_WALKED_TILE_X 11
#define SCRIPT_CH19_WALKED_TILE_Y 6
#define SCRIPT_CH19_WALKED_FACING 2

/* What the cursor globals are staged at before the chapter 19 run: neither the
   tile the cut-scene leaves unit 0 on nor the tile MAP18.COD gave it, so a run
   that never made the cursor call and a run that made it before the script are
   both told apart from the real answer. */
#define CH19_STAGED_CURSOR_X 336
#define CH19_STAGED_CURSOR_Y 240


/* --- fdps_chapter_20_init @ 00021390 constants -------------------------- */

/* Chapter 20 is chapter id 19, and this handler is slot 19 of the table at
   00060074 -- the dword at 000600c0 is 00021390, and that data reference is
   the function's only xref. */
#define CHAPTER_20_ID 19

/* The party when chapter 20 opens: the ten chapters 16 to 19 were fought with
   plus 蘭斯洛特, whom fdps_chapter_19_init added at roster slot 10. */
#define PARTY_AT_CHAPTER_20 11

/* MAP19.DAT's own header bytes, read back to prove chapter 20's map is the one
   that loaded: eleven player slots at +1 and fifty-five scripted deployments
   at +2 (src/rsrc.c).  Both were staged at other numbers before the run.

   ELEVEN is the number that makes 蘭斯洛特 a map unit.  MAP18.DAT asked for
   ten against the same eleven-member roster, so chapter 19 left roster slot 10
   off the board; MAP19.DAT is the first map in the game to ask for eleven, and
   fdps_build_map_unit_array fills slot i from roster slot i for every i below
   the roster count (src/deploy.c), so every slot has a member behind it and
   none falls through to the zeroed, retired spare.

   The MAP19_ prefix is the FILE's name and not chapter 19's: a map file is
   numbered by the 0-based chapter id. */
#define MAP19_PLAYER_SLOTS 11
#define MAP19_CHAR_SPAWNS 55

/* What is on the board when the handler returns, and why it is the map's doing
   alone.  MAP19.DAT tags fifty-four of its fifty-five records wave 0, which a
   chapter state reset appends behind the player slots.  The cut-scene adds
   nothing to that: everything it deploys, retires and revives happens while it
   has the chapter switched to 58, and its closing SWITCH_MAP back to 19
   rebuilds the whole board from the map again (src/icon.c).  So the count is
   the same whether the script ran or not, and what says it ran is the marker
   below. */
#define MAP19_WAVE_ZERO_UNITS 54
#define CH20_UNITS_AFTER_SCRIPT (PARTY_AT_CHAPTER_20 + MAP19_WAVE_ZERO_UNITS)

/* The census those fifty-four come to, by character id and level, which is the
   strategy guide's 敵方 list for the chapter to the number:

     character 75,  level 17, twenty-three -- LV17 蛇魔使
     character 80,  level 18, thirteen     -- LV18 野蠻戰士
     character 83,  level 17, ten          -- LV17 狼人戰士
     character 101, level 28, eight        -- LV28 冰魔導士

   Two of the ids the file already pins elsewhere are here at this chapter's
   own levels: 80 is chapter 19's 野蠻戰士 a level higher, and 83 is the id
   chapter 17's map carries.  Every one of the fifty-four is on side 0, so the
   guest side is empty this chapter. */
#define MAP19_SNAKE_CHAR_ID 75
#define MAP19_SNAKE_LEVEL 17
#define MAP19_SNAKE_COUNT 23
#define MAP19_BARBARIAN_CHAR_ID 80
#define MAP19_BARBARIAN_LEVEL 18
#define MAP19_BARBARIAN_COUNT 13
#define MAP19_WOLF_CHAR_ID 83
#define MAP19_WOLF_LEVEL 17
#define MAP19_WOLF_COUNT 10
#define MAP19_ICE_MAGE_CHAR_ID 101
#define MAP19_ICE_MAGE_LEVEL 28
#define MAP19_ICE_MAGE_COUNT 8
#define CH20_ENEMY_SIDE_UNITS MAP19_WAVE_ZERO_UNITS
#define CH20_GUEST_SIDE_UNITS 0

/* The one record MAP19.DAT does not tag wave 0: a level-17 character 67 on
   wave 1, which belongs to the chapter's own turn events.  The guide's 敵方
   list does not carry it and the id is on no other unit of the opening board,
   so a run that fired that wave early is caught by a non-zero count. */
#define MAP19_WAVE_ONE_CHAR_ID 67
#define MAP19_WAVE_ONE_LEVEL 17

/* The stage the cut-scene plays itself out on, and the chapter it puts back.
   ICON19.DAT has two SWITCH_MAPs, at script offsets 4 and 191.  Nine of the
   thirty opening scripts carry the opcode -- ICON00, ICON06 to ICON09, ICON11,
   ICON19, ICON24 and ICON29, every member walked with the opcode ladder in
   src/icon.c -- and ICON19.DAT is the first of them past ICON11.DAT, so it is
   the first one this file meets.  MAP58.DAT declares NO player slots and holds four records -- 蘭迪斯, 法蓮娜
   and 費塔加 at level 2 on wave 0, and a level-2 character 123 on wave 1 --
   so the actors the scene retires and revives are map units 0 to 3 of a board
   that exists only while the scene is running. */
#define SCRIPT_CH20_SCENE_MAP 58
#define SCRIPT_CH20_CHAPTER_MAP 19
#define SCRIPT_CH20_ACTOR_UNIT_RANDIS 0
#define SCRIPT_CH20_ACTOR_UNIT_FLARENA 1
#define SCRIPT_CH20_ACTOR_UNIT_FEITAGA 2
#define SCRIPT_CH20_ACTOR_UNIT_ARRIVAL 3
#define SCRIPT_CH20_SCENE_WAVE 1
#define SCRIPT_CH20_SCENE_WAVE_PLACE_EXACT 1

/* The character id only the scene's own stage carries.  It is on MAP58.DAT and
   on no map the party ever fights on, so counting it in the array is the one
   check that says the closing SWITCH_MAP really took the stage away rather
   than leaving its actors standing among chapter 20's units. */
#define SCRIPT_CH20_SCENE_ONLY_CHAR_ID 123

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 64 is
   the last of the sixty-five and it is written AFTER the closing SWITCH_MAP,
   so it exists only if the second rebuild ran -- a run that opened a member
   with no SWITCH_MAP in it leaves the value on whatever unit 64 the first
   rebuild made, and a run that opened one of the end decoys never writes it at
   all.  The value is one no other member of the container writes.  The slot is
   status_timers[4], record offset 0x26, for the reason tests/chinit1.c gives:
   it is the one status byte fdps_unit_select_status_icon does not read, so
   marking it cannot send fdps_draw_map_unit through the null status-icon
   sheet.  The opcode's operand is measured from status_timers[3] (src/icon.c),
   which makes that operand 1.  The shipped member has no SET_UNIT_TIMER of its
   own, so this marker is the fixture's alone. */
#define SCRIPT_CH20_MARKER_UNIT (CH20_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH20_MARKER_OPERAND 1
#define SCRIPT_CH20_MARKER_SLOT 4
#define SCRIPT_CH20_MARKER_VALUE 99

/* Where the cursor ends, and it is the map's tile and not the cut-scene's.
   Every walk and place in ICON19.DAT is before the closing SWITCH_MAP, so the
   second rebuild puts unit 0 back on MAP19.COD's player-slot-0 start tile --
   record 55, the first record past the map's fifty-five scripted ones -- which
   is (5, 4).  Chapter 20 is the only member of the family whose cursor call
   lands on the map's own tile.

   A chapter state reset zeroes both cursor globals (src/chapter.c) and the
   script's closing SWITCH_MAP performs one, so a handler that skipped the
   cursor call would leave them at (0, 0) rather than at what stage_globals
   staged. */
#define CH20_CURSOR_TILE_X 5
#define CH20_CURSOR_TILE_Y 4


/* --- fdps_chapter_21_init @ 000213d0 constants -------------------------- */

/* Chapter 21 is chapter id 20, and this handler is slot 20 of the table at
   00060074 -- the dword at 000600c4 is 000213d0, and that data reference is
   the function's only xref. */
#define CHAPTER_21_ID 20

/* The party when chapter 21 opens: the same eleven chapter 20 was fought with.
   Neither chapter 20's handler nor this one adds anybody, and 法蓮娜 is still
   one of the eleven -- the guide has her leave the party after this chapter's
   three-battle run, not before it. */
#define PARTY_AT_CHAPTER_21 11

/* MAP20.DAT's own header bytes, read back to prove chapter 21's map is the one
   that loaded: eleven player slots at +1 and seventy scripted deployments at
   +2 (src/rsrc.c).  Both were staged at other numbers before the run.

   Eleven is the same count MAP19.DAT asked for, against the same eleven-member
   roster, so every slot has a member behind it and none falls through to the
   zeroed, retired spare fdps_build_map_unit_array writes (src/deploy.c).

   The MAP20_ prefix is the FILE's name and not chapter 20's: a map file is
   numbered by the 0-based chapter id. */
#define MAP20_PLAYER_SLOTS 11
#define MAP20_CHAR_SPAWNS 70

/* What is on the board when the handler returns.  MAP20.DAT tags thirty-eight
   of its seventy records wave 0, which a chapter state reset appends behind
   the player slots, and the cut-scene adds nothing to that: ICON20.DAT has no
   DEPLOY_WAVE, no RETIRE_UNIT, no REVIVE_UNIT and no SWITCH_MAP anywhere in
   its 259 bytes. */
#define MAP20_WAVE_ZERO_UNITS 38
#define CH21_UNITS_AFTER_SCRIPT (PARTY_AT_CHAPTER_21 + MAP20_WAVE_ZERO_UNITS)

/* The census those thirty-eight come to, by character id and level, which is
   the first five lines of the strategy guide's 敵方 list for the chapter to
   the number:

     character 104, level 17, one          -- LV17 黑暗祭司
     character 101, level 28, five         -- LV28 冰魔導士
     character  81, level 16, three        -- 狂戰士
     character  83, level 18, seven        -- LV18 狼人戰士
     character  75, level 18, twenty-two   -- LV18 蛇魔使

   Three of the ids the file already pins elsewhere are here at this chapter's
   own levels: 75 is chapter 20's 蛇魔使 a level higher, 83 its 狼人戰士 a
   level higher, and 101 its 冰魔導士 at the same LV28.  Every one of the
   thirty-eight is on side 0, so the guest side is empty this chapter.

   THE 狂戰士 ARE LEVEL 16 AND THE GUIDE SAYS LV18.  The map records say 16 and
   the guide's own HP figure for them settles it: ENEMYDAT.DAT row 81 carries
   an HP coefficient of 45, and 45 x 16 is the 720 the guide prints, where 18
   would be 810 (assets/characters.md gives the coefficient form).  So the
   expected value below is the file's. */
#define MAP20_DARK_PRIEST_CHAR_ID 104
#define MAP20_DARK_PRIEST_LEVEL 17
#define MAP20_DARK_PRIEST_COUNT 1
#define MAP20_ICE_MAGE_CHAR_ID 101
#define MAP20_ICE_MAGE_LEVEL 28
#define MAP20_ICE_MAGE_COUNT 5
#define MAP20_BERSERKER_CHAR_ID 81
#define MAP20_BERSERKER_LEVEL 16
#define MAP20_BERSERKER_COUNT 3
#define MAP20_WOLF_CHAR_ID 83
#define MAP20_WOLF_LEVEL 18
#define MAP20_WOLF_COUNT 7
#define MAP20_SNAKE_CHAR_ID 75
#define MAP20_SNAKE_LEVEL 18
#define MAP20_SNAKE_COUNT 22
#define CH21_ENEMY_SIDE_UNITS MAP20_WAVE_ZERO_UNITS
#define CH21_GUEST_SIDE_UNITS 0

/* The five character ids MAP20.DAT uses for its two reinforcement waves and
   for nothing else.  Waves 1 and 2 are sixteen records each -- four 騎士 (89),
   four 衛兵 (90), three 弓箭手 (94), four 武鬥家 (109) and one 暗魔導士 (103),
   the wave-2 暗魔導士 at level 17 where the wave-1 one is level 18 -- and both
   come in on a tile trigger, fdps_chapter_21_event_deploy_wave_1 and
   fdps_chapter_21_event_deploy_wave_2 (src/chevt4.c).  None of the five ids is
   on any wave-0 record, so a single unit carrying one of them in the array is
   a wave that fired before the player moved. */
#define MAP20_KNIGHT_CHAR_ID 89
#define MAP20_GUARD_CHAR_ID 90
#define MAP20_ARCHER_CHAR_ID 94
#define MAP20_DARK_MAGE_CHAR_ID 103
#define MAP20_MONK_CHAR_ID 109

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 48 is
   the last of the forty-nine, so it exists only if the rebuild put the map's
   wave-0 records down; a run that opened one of the end decoys never writes it
   at all.  The value is one no other member of the container writes.  The slot
   is status_timers[4], record offset 0x26, for the reason tests/chinit1.c
   gives: it is the one status byte fdps_unit_select_status_icon does not read,
   so marking it cannot send fdps_draw_map_unit through the null status-icon
   sheet.  The opcode's operand is measured from status_timers[3]
   (src/icon.c), which makes that operand 1.  The shipped member has no
   SET_UNIT_TIMER of its own, so this marker is the fixture's alone. */
#define SCRIPT_CH21_MARKER_UNIT (CH21_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH21_MARKER_OPERAND 1
#define SCRIPT_CH21_MARKER_SLOT 4
#define SCRIPT_CH21_MARKER_VALUE 101

/* Where the cut-scene leaves map unit 0, and it is the script's tile and not
   the map's.  MAP20.COD record 70 -- the first record past the map's seventy
   scripted ones, and so player slot 0's start tile -- is (17, 20), so a cursor
   that ended there would be a run whose script never opened.  The shipped
   member places unit 0 at (19, 24) at script offset 63 and then walks it four
   times: two tiles up, two left, two up and one left, which is (16, 20).  The
   third walk is what leaves it on the map's own tile in passing, so the two
   answers are one square apart and the fourth walk is the whole difference.
   The fixture names the finishing tile outright, because the walks that reach
   it hold a rendered frame per sub-step and prove nothing this file is asking
   about.

   The facing is the member's closing one and NOT the walks'.  The fourth walk
   is a left one and fdps_icon_script_walk_units writes the list entry's second
   byte into the facing field on every pass (src/icon.c), so the walks leave
   unit 0 facing 1, left.  What turns it to 2, up, is the FACE_UNITS at script
   offset 249, the last opcode the member has over unit 0, and it comes after
   four earlier FACE_UNITS that turn it through 2, 1, 3 and 0 in turn.  The
   fixture's single PLACE_UNIT stands in for the whole member and so has to
   name the state the whole member finishes in, which is why the constant is
   the closing facing rather than the walked one.  Nothing asserts on facing;
   it is here so the staged unit is not left in a state the shipped member
   never leaves it in. */
#define SCRIPT_CH21_WALKED_UNIT 0
#define SCRIPT_CH21_WALKED_TILE_X 16
#define SCRIPT_CH21_WALKED_TILE_Y 20
#define SCRIPT_CH21_CLOSING_FACING 2
#define CH21_CURSOR_TILE_X SCRIPT_CH21_WALKED_TILE_X
#define CH21_CURSOR_TILE_Y SCRIPT_CH21_WALKED_TILE_Y


/* --- fdps_chapter_22_init @ 00021410 constants -------------------------- */

/* Chapter 22 is chapter id 21, and this handler is slot 21 of the table at
   00060074 -- the dword at 000600c8 is 00021410, and that data reference is
   the function's only xref. */
#define CHAPTER_22_ID 21

/* The party when chapter 22 opens: the same eleven chapters 20 and 21 were
   fought with.  Neither chapter 21's handler nor this one adds anybody, so the
   join order above is this chapter's player-slot order too. */
#define PARTY_AT_CHAPTER_22 11

/* MAP21.DAT's own header bytes, read back to prove chapter 22's map is the one
   that loaded: eleven player slots at +1 and fifty-five scripted deployments
   at +2 (src/rsrc.c).  Both were staged at other numbers before the run.

   Eleven is the same count MAP19.DAT and MAP20.DAT asked for, against the same
   eleven-member roster, so every slot has a member behind it and none falls
   through to the zeroed, retired spare fdps_build_map_unit_array writes
   (src/deploy.c).

   The MAP21_ prefix below is the FILE's name and not chapter 21's: a map file
   is numbered by the 0-based chapter id. */
#define MAP21_PLAYER_SLOTS 11
#define MAP21_CHAR_SPAWNS 55

/* What is on the board when the handler returns.  MAP21.DAT tags exactly ONE
   of its fifty-five records wave 0, which the chapter state reset appends
   behind the player slots, and the cut-scene adds nothing to that: ICON21.DAT
   has no DEPLOY_WAVE and no SWITCH_MAP anywhere in its 283 bytes.  Twelve
   units is the shortest opening board any chapter under test has. */
#define MAP21_WAVE_ZERO_UNITS 1
#define CH22_UNITS_AFTER_SCRIPT (PARTY_AT_CHAPTER_22 + MAP21_WAVE_ZERO_UNITS)

/* The single wave-0 record: character 73 at level 20, which is the strategy
   guide's LV20 巫湯婆婆, the boss its 勝利條件 for the chapter names.  The
   guide's HP6000 and MP2000 are ENEMYDAT.DAT row 13's per-level coefficients
   of 300 and 100 times twenty, and its MV3 is the row's absolute
   (assets/characters.md), so the id and the level are checked against the
   guide rather than either being assumed.  It is on the enemy side, and there
   is no guest on this map at all. */
#define MAP21_BOSS_CHAR_ID 73
#define MAP21_BOSS_LEVEL 20
#define MAP21_BOSS_COUNT 1
#define CH22_ENEMY_SIDE_UNITS MAP21_WAVE_ZERO_UNITS
#define CH22_GUEST_SIDE_UNITS 0

/* The four character ids MAP21.DAT uses for its five reinforcement waves and
   for nothing else.  Wave 1 is eight LV19 狼人戰士 (83) with eight LV18 蛇魔使
   (75), wave 2 four of each, wave 3 four LV17 幽魂 (105), wave 5 four LV17
   骷髏兵 (84), and wave 4 the last twenty-two -- seven of each of the first
   two and four of each of the second two.  Which of 105 and 84 is which is the
   guide's own HP figures: ENEMYDAT.DAT row 45 carries 25 HP a level, so at
   LV17 that is the 幽魂's HP425, and row 24 carries 38, which is the 骷髏兵's
   HP646.  The guide has the waves arriving on the first, third, fifth and
   eighth player turns, so a single unit carrying one of the four ids in the
   array is a wave that came in before the player moved. */
#define MAP21_WOLF_CHAR_ID 83
#define MAP21_SNAKE_CHAR_ID 75
#define MAP21_GHOST_CHAR_ID 105
#define MAP21_SKELETON_CHAR_ID 84

/* Which player slot the cut-scene takes off the board.  ICON21.DAT's one and
   only unit-state opcode is the RETIRE_UNIT at script offset 278, and it names
   map unit 0; there is no REVIVE anywhere behind it, so the slot is still
   retired when the handler returns.  A player slot's map unit index is its
   roster slot and the roster is in join order, so the member taken off is
   蘭迪斯 and the ten left are the guide's 己方 line, 蘭迪斯以外的所有人. */
#define SCRIPT_CH22_RETIRED_UNIT 0
#define CH22_RETIRED_SLOTS 1
#define CH22_LIVE_SLOTS (PARTY_AT_CHAPTER_22 - CH22_RETIRED_SLOTS)

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 11 is
   the last of the twelve, so it exists only if the rebuild put the map's one
   wave-0 record down; a run that opened one of the end decoys never writes it
   at all.  The value is one no other member of the container writes.  The slot
   is status_timers[4], record offset 0x26, for the reason tests/chinit1.c
   gives: it is the one status byte fdps_unit_select_status_icon does not read,
   so marking it cannot send fdps_draw_map_unit through the null status-icon
   sheet.  The opcode's operand is measured from status_timers[3]
   (src/icon.c), which makes that operand 1.  The shipped member has no
   SET_UNIT_TIMER of its own, so this marker is the fixture's alone. */
#define SCRIPT_CH22_MARKER_UNIT (CH22_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH22_MARKER_OPERAND 1
#define SCRIPT_CH22_MARKER_SLOT 4
#define SCRIPT_CH22_MARKER_VALUE 97

/* Where the cut-scene leaves the unit the cursor call names, and it is the
   script's tile and not the map's.  MAP21.COD record 58 -- player slot 3's,
   the fourth record past the map's fifty-five scripted ones -- is (2, 24), so
   a cursor that ended there would be a run whose script never opened.  The
   shipped member stands unit 3 at (23, 23) with the rest of the party at
   script offset 52, moves it to (3, 24) at offset 125, and then walks it four
   times: four tiles up, two right, one up and one right, which is (6, 19).

   The facing is the member's closing one and NOT the walks'.  The fourth walk
   is a right one and fdps_icon_script_walk_units writes the list entry's
   second byte into the facing field on every pass (src/icon.c), so the walks
   leave unit 3 facing 3, right.  What turns it to 2, up, are the FACE_UNITS at
   script offsets 170, 190 and 230, the last of them the final opcode the
   member has over it.  The fixture's single PLACE_UNIT stands in for the whole
   member and so has to name the state the whole member finishes in, which is
   why the constant is the closing facing rather than the walked one.  Nothing
   asserts on facing; it is here so the staged unit is not left in a state the
   shipped member never leaves it in. */
#define SCRIPT_CH22_WALKED_UNIT 3
#define SCRIPT_CH22_WALKED_TILE_X 6
#define SCRIPT_CH22_WALKED_TILE_Y 19
#define SCRIPT_CH22_CLOSING_FACING 2
#define CH22_CURSOR_TILE_X SCRIPT_CH22_WALKED_TILE_X
#define CH22_CURSOR_TILE_Y SCRIPT_CH22_WALKED_TILE_Y


static struct fdps_unit_record stage_roster[ROSTER_SLOTS];
static struct fdps_character_base_record stage_char[TABLE_ROWS];
static struct fdps_character_growth stage_growth[TABLE_ROWS];
static struct fdps_enemy_data stage_enemy[TABLE_ROWS];
static struct fdps_item_effect stage_items[ITEM_TABLE_ROWS];
static unsigned char stage_palette[DAC_ENTRIES * 3];

/* ICON14.DAT: the member the handler must NOT open, one chapter early -- it is
   chapter 15's own script.  It deploys nobody, retires nobody and marks unit 0
   with a value of its own. */
static unsigned char fixture_icon14_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH15_DECOY_VALUE,
    0x00
};

/* ICON15.DAT: the member fdps_chapter_16_init names.  The PLACE_UNIT at the
   front stands in for the shipped member's nine group walks of unit 0 and
   leaves it on the tile they leave it on; the five RETIREs are the shipped
   member's own, in its order and with its operands, and it carries no REVIVE
   behind them; the four DEPLOY_WAVEs are the shipped member's own too, waves
   1, 2, 3 and 4, each place-exact, which on MAP15.DAT is two, four, four and
   two records.  The SET_UNIT_TIMER marker at the end is this file's own
   addition, because the shipped member has none: it writes a value of its own
   onto the last unit the fourth wave appended.  The shipped member's other
   ninety-odd opcodes walk and pose the cast, scroll and shake the view, set
   the music and draw chapter text, and are left out for the reason the file
   comment gives. */
static unsigned char fixture_icon15_dat[] = {
    0x0a, SCRIPT_CH16_WALKED_UNIT, SCRIPT_CH16_WALKED_TILE_X,
    SCRIPT_CH16_WALKED_TILE_Y, SCRIPT_CH16_WALKED_FACING,
    0x0b, SCRIPT_CH16_RETIRED_UNIT_A,
    0x0b, SCRIPT_CH16_RETIRED_UNIT_B,
    0x0b, SCRIPT_CH16_RETIRED_UNIT_C,
    0x0b, SCRIPT_CH16_RETIRED_UNIT_D,
    0x0b, SCRIPT_CH16_RETIRED_UNIT_E,
    0x04, SCRIPT_CH16_FIRST_WAVE, SCRIPT_CH16_PLACE_EXACT,
    0x04, SCRIPT_CH16_SECOND_WAVE, SCRIPT_CH16_PLACE_EXACT,
    0x04, SCRIPT_CH16_THIRD_WAVE, SCRIPT_CH16_PLACE_EXACT,
    0x04, SCRIPT_CH16_FOURTH_WAVE, SCRIPT_CH16_PLACE_EXACT,
    0x12, SCRIPT_CH16_MARKER_UNIT, SCRIPT_CH16_MARKER_OPERAND,
    SCRIPT_CH16_MARKER_VALUE,
    0x00
};

/* ICON16.DAT: the member fdps_chapter_17_init names, and the member
   fdps_chapter_16_init must NOT open -- it is the one a rewrite that read the
   chapter number out of the handler's own name would reach for.  Its five
   RETIREs are the shipped member's own, in its order and with its operands,
   and it carries no REVIVE behind them; it carries no DEPLOY_WAVE either,
   because the shipped member carries none at all -- MAP16.DAT's twenty-three
   wave-0 records are the whole opening board.  The PLACE_UNIT at the front
   stands in for the shipped member's group places and walks of unit 3 and
   leaves it on the tile they leave it on.  The SET_UNIT_TIMER marker at the
   end is this file's own addition, because the shipped member has none: it
   writes a value of its own onto the last unit the reset appended.  The
   shipped member's other thirty-odd opcodes place and walk the rest of the
   five, pose them, scroll the view, set the music, fade and draw chapter text,
   and are left out for the reason the file comment gives. */
static unsigned char fixture_icon16_dat[] = {
    0x0a, SCRIPT_CH17_WALKED_UNIT, SCRIPT_CH17_WALKED_TILE_X,
    SCRIPT_CH17_WALKED_TILE_Y, SCRIPT_CH17_WALKED_FACING,
    0x0b, SCRIPT_CH17_RETIRED_UNIT_A,
    0x0b, SCRIPT_CH17_RETIRED_UNIT_B,
    0x0b, SCRIPT_CH17_RETIRED_UNIT_C,
    0x0b, SCRIPT_CH17_RETIRED_UNIT_D,
    0x0b, SCRIPT_CH17_RETIRED_UNIT_E,
    0x12, SCRIPT_CH17_MARKER_UNIT, SCRIPT_CH17_MARKER_OPERAND,
    SCRIPT_CH17_MARKER_VALUE,
    0x00
};

/* ICON17.DAT: the member fdps_chapter_18_init names, and the member
   fdps_chapter_17_init must NOT open.  Every unit-state and deployment opcode
   the shipped member carries is here in its order and with its operands: the
   five RETIREs of the player slots chapter 16 fought without, the two RETIREs
   and one REVIVE of the scene's own actors, the exact-anchor DEPLOY_WAVE that
   puts the third actor down, the five REVIVEs that put the whole party back on
   the board, the two RETIREs that take the actors off again, and the
   nearest-free-tile DEPLOY_WAVE that brings the chapter's opposition in.  The
   PLACE_UNIT at the front stands in for the shipped member's walks of unit 0
   and leaves it on the tile they leave it on.  The SET_UNIT_TIMER marker at
   the end is this file's own addition, because the shipped member has none: it
   writes a value of its own onto the last unit the enemy wave appended.  The
   shipped member's other hundred-odd opcodes walk and pose the cast, shake and
   scroll the view, play a SAF, set the music and draw chapter text, and are
   left out for the reason the file comment gives. */
static unsigned char fixture_icon17_dat[] = {
    0x0a, SCRIPT_CH18_WALKED_UNIT, SCRIPT_CH18_WALKED_TILE_X,
    SCRIPT_CH18_WALKED_TILE_Y, SCRIPT_CH18_WALKED_FACING,
    0x0b, SCRIPT_CH18_RETIRED_UNIT_A,
    0x0b, SCRIPT_CH18_RETIRED_UNIT_B,
    0x0b, SCRIPT_CH18_RETIRED_UNIT_C,
    0x0b, SCRIPT_CH18_RETIRED_UNIT_D,
    0x0b, SCRIPT_CH18_RETIRED_UNIT_E,
    0x0b, SCRIPT_CH18_ACTOR_UNIT_GUEST,
    0x0b, SCRIPT_CH18_ACTOR_UNIT_SECOND,
    0x0c, SCRIPT_CH18_ACTOR_UNIT_GUEST,
    0x04, SCRIPT_CH18_SCENE_WAVE, SCRIPT_CH18_SCENE_WAVE_PLACE_EXACT,
    0x0c, SCRIPT_CH18_RETIRED_UNIT_A,
    0x0c, SCRIPT_CH18_RETIRED_UNIT_B,
    0x0c, SCRIPT_CH18_RETIRED_UNIT_C,
    0x0c, SCRIPT_CH18_RETIRED_UNIT_D,
    0x0c, SCRIPT_CH18_RETIRED_UNIT_E,
    0x0b, SCRIPT_CH18_ACTOR_UNIT_THIRD,
    0x0b, SCRIPT_CH18_ACTOR_UNIT_GUEST,
    0x04, SCRIPT_CH18_ENEMY_WAVE, SCRIPT_CH18_ENEMY_WAVE_PLACE_SEARCH,
    0x12, SCRIPT_CH18_MARKER_UNIT, SCRIPT_CH18_MARKER_OPERAND,
    SCRIPT_CH18_MARKER_VALUE,
    0x00
};

/* ICON18.DAT: the member fdps_chapter_19_init names, and the member
   fdps_chapter_18_init must NOT open.  The shipped member carries no
   unit-state opcode of any kind -- no RETIRE_UNIT, no REVIVE_UNIT and no
   SET_UNIT_TIMER anywhere in its 231 bytes -- and exactly one deployment
   opcode, the DEPLOY_WAVE at script offset 196 that asks for wave 5 with the
   nearest-free-tile place operand, which is here with its own operands.  The
   PLACE_UNIT at the front stands in for the shipped member's nine group walks
   of unit 0 and leaves it on the tile they leave it on.  The SET_UNIT_TIMER
   marker at the end is this file's own addition: it writes a value of its own
   onto the last unit the wave appended.  The shipped member's other twenty-odd
   opcodes walk the party into position, walk the five 暗黑騎士 down the map,
   scroll the view four times, fade, set the music and draw chapter text, and
   are left out for the reason the file comment gives. */
static unsigned char fixture_icon18_dat[] = {
    0x0a, SCRIPT_CH19_WALKED_UNIT, SCRIPT_CH19_WALKED_TILE_X,
    SCRIPT_CH19_WALKED_TILE_Y, SCRIPT_CH19_WALKED_FACING,
    0x04, SCRIPT_CH19_ENEMY_WAVE, SCRIPT_CH19_ENEMY_WAVE_PLACE_SEARCH,
    0x12, SCRIPT_CH19_MARKER_UNIT, SCRIPT_CH19_MARKER_OPERAND,
    SCRIPT_CH19_MARKER_VALUE,
    0x00
};

/* ICON19.DAT: the member fdps_chapter_20_init names, and the member
   fdps_chapter_19_init must NOT open.  Every map, unit-state and deployment
   opcode the shipped member carries is here in its order and with its
   operands: the SWITCH_MAP to the cut-scene stage 58, the RETIRE of the third
   actor, the RETIRE and REVIVE of the second, the RETIRE of the third again,
   the exact-anchor DEPLOY_WAVE that brings the stage's fourth actor on, the
   RETIRE that takes it off, and the SWITCH_MAP back to chapter 19 that throws
   the whole stage away and rebuilds chapter 20's board from MAP19.DAT.

   Nothing here stands in for a walk, because there is nothing to stand in for:
   every walk the shipped member makes is on the stage, and the closing
   SWITCH_MAP puts every unit back where the map says.  The SET_UNIT_TIMER
   marker at the end is this file's own addition, and it is deliberately the
   last opcode: it writes onto a unit that exists only after the second
   rebuild.  The shipped member's other thirty-odd opcodes walk and pose the
   three actors, shake the view and reset the view origin, play a SAF, set the
   music twice, fade twice and draw seven pages of chapter text, and are left
   out for the reason the file comment gives. */
static unsigned char fixture_icon19_dat[] = {
    0x11, SCRIPT_CH20_SCENE_MAP,
    0x0b, SCRIPT_CH20_ACTOR_UNIT_FEITAGA,
    0x0b, SCRIPT_CH20_ACTOR_UNIT_FLARENA,
    0x0c, SCRIPT_CH20_ACTOR_UNIT_FLARENA,
    0x0b, SCRIPT_CH20_ACTOR_UNIT_FEITAGA,
    0x04, SCRIPT_CH20_SCENE_WAVE, SCRIPT_CH20_SCENE_WAVE_PLACE_EXACT,
    0x0b, SCRIPT_CH20_ACTOR_UNIT_ARRIVAL,
    0x11, SCRIPT_CH20_CHAPTER_MAP,
    0x12, SCRIPT_CH20_MARKER_UNIT, SCRIPT_CH20_MARKER_OPERAND,
    SCRIPT_CH20_MARKER_VALUE,
    0x00
};

/* ICON20.DAT: the member fdps_chapter_21_init names, and the member
   fdps_chapter_20_init must NOT open.  The shipped member carries no map,
   unit-state or deployment opcode of any kind -- no SWITCH_MAP, no
   RETIRE_UNIT, no REVIVE_UNIT, no DEPLOY_WAVE and no SET_UNIT_TIMER anywhere
   in its 259 bytes -- so there is nothing here to carry over from it.  The
   PLACE_UNIT at the front stands in for the shipped member's twenty-two places
   and nine group walks and leaves unit 0 on the tile its four walks leave it
   on.  The SET_UNIT_TIMER marker at the end is this file's own addition: it
   writes a value of its own onto the last unit the reset appended.  The
   shipped member's other forty-odd opcodes stand the eleven party units up,
   walk them into position, turn them, scroll the view twice, set the music
   twice and draw one page of chapter text, and are left out for the reason the
   file comment gives. */
static unsigned char fixture_icon20_dat[] = {
    0x0a, SCRIPT_CH21_WALKED_UNIT, SCRIPT_CH21_WALKED_TILE_X,
    SCRIPT_CH21_WALKED_TILE_Y, SCRIPT_CH21_CLOSING_FACING,
    0x12, SCRIPT_CH21_MARKER_UNIT, SCRIPT_CH21_MARKER_OPERAND,
    SCRIPT_CH21_MARKER_VALUE,
    0x00
};

/* ICON21.DAT: the member fdps_chapter_22_init names, and the member
   fdps_chapter_21_init must NOT open.  The shipped member carries no map or
   deployment opcode at all -- no SWITCH_MAP and no DEPLOY_WAVE anywhere in its
   283 bytes -- and exactly one unit-state opcode, the RETIRE_UNIT at script
   offset 278 that takes 蘭迪斯 off the board, which is here with its own
   operand.  The PLACE_UNIT at the front stands in for the shipped member's
   twenty-one places and eight group walks and leaves unit 3 on the tile its
   four walks leave it on.  The SET_UNIT_TIMER marker at the end is this file's
   own addition: it writes a value of its own onto the last unit the reset
   appended.  The shipped member's other forty-seven opcodes stand the eleven
   party units up, walk and turn them, shake the view four times and reset the
   view tile four times, scroll once, fade twice, set the music twice and draw
   one page of chapter text, and are left out for the reason the file comment
   gives. */
static unsigned char fixture_icon21_dat[] = {
    0x0a, SCRIPT_CH22_WALKED_UNIT, SCRIPT_CH22_WALKED_TILE_X,
    SCRIPT_CH22_WALKED_TILE_Y, SCRIPT_CH22_CLOSING_FACING,
    0x0b, SCRIPT_CH22_RETIRED_UNIT,
    0x12, SCRIPT_CH22_MARKER_UNIT, SCRIPT_CH22_MARKER_OPERAND,
    SCRIPT_CH22_MARKER_VALUE,
    0x00
};

/* ICON22.DAT: the member fdps_chapter_22_init must NOT open, one chapter late
   -- it is chapter 23's own script, and it is the one a rewrite that read the
   chapter number out of the handler's own name would reach for.  It deploys
   nobody, retires nobody, switches no map and marks unit 0 with a value of its
   own. */
static unsigned char fixture_icon22_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH23_DECOY_VALUE,
    0x00
};

static char *fixture_names[FIXTURE_MEMBERS] = {
    "ICON14.DAT", "ICON15.DAT", "ICON16.DAT", "ICON17.DAT", "ICON18.DAT",
    "ICON19.DAT", "ICON20.DAT", "ICON21.DAT", "ICON22.DAT"
};

static unsigned char *fixture_bytes[FIXTURE_MEMBERS] = {
    fixture_icon14_dat, fixture_icon15_dat, fixture_icon16_dat,
    fixture_icon17_dat, fixture_icon18_dat, fixture_icon19_dat,
    fixture_icon20_dat, fixture_icon21_dat, fixture_icon22_dat
};

static int fixture_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_icon14_dat), sizeof(fixture_icon15_dat),
    sizeof(fixture_icon16_dat), sizeof(fixture_icon17_dat),
    sizeof(fixture_icon18_dat), sizeof(fixture_icon19_dat),
    sizeof(fixture_icon20_dat), sizeof(fixture_icon21_dat),
    sizeof(fixture_icon22_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run16_state = 0;

/* Whether this file created the container, and so whether it may remove it. */
static int fixture_owned = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order: chapters 1 to 4 one each, chapter 7 裘娜, chapter 8 費塔加, chapter 9
   布蘭多 and 蓋亞, chapter 11 琴琴 and chapter 15 瑪麗安. */
static int chapter_16_party[PARTY_AT_CHAPTER_16] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID, MARIAN_CHAR_ID
};

/* The five map units the cut-scene retires, and the five it leaves. */
static int chapter_16_retired[CH16_RETIRED_SLOTS] = { 3, 4, 7, 8, 9 };
static int chapter_16_live[CH16_LIVE_SLOTS] = { 0, 1, 2, 5, 6 };

/* Everything the cases assert, captured the instant the handler returned. */
static int seen_roster_count;
static int seen_player_slots;
static int seen_char_spawns;
static int seen_unit_count;
static int seen_party_char_ids[PARTY_AT_CHAPTER_16];
static int seen_party_sides[PARTY_AT_CHAPTER_16];
static int seen_party_flags[PARTY_AT_CHAPTER_16];
static int seen_dark_mages;
static int seen_knights;
static int seen_samurai;
static int seen_archers;
static int seen_fliers;
static int seen_enemy_side_units;
static unsigned char seen_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen_unit0_timers[STATUS_TIMER_COUNT];
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

    /* Both are staged at numbers MAP15.DAT does not carry -- it says 10 and 25
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

static void capture_chapter_16(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    int unit_index;
    int char_id;
    int level;
    int slot;

    seen_roster_count = data_fdps_roster_member_count;
    seen_player_slots = data_fdps_map_player_slot_count;
    seen_char_spawns = data_fdps_map_char_spawn_count;
    seen_unit_count = data_fdps_map_unit_count;
    seen_cursor_x = data_fdps_map_cursor_world_x;
    seen_cursor_y = data_fdps_map_cursor_world_y;
    seen_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_16; slot++) {
        seen_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen_party_sides[slot] = (int) unit0[slot].side;
        seen_party_flags[slot] = (int) unit0[slot].flags;
    }

    seen_dark_mages = 0;
    seen_knights = 0;
    seen_samurai = 0;
    seen_archers = 0;
    seen_fliers = 0;
    seen_enemy_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_16;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen_enemy_side_units++;
        }
        if (char_id == CH15_DARK_MAGE_CHAR_ID
            && level == CH15_DARK_MAGE_LEVEL) {
            seen_dark_mages++;
        } else if (char_id == CH15_KNIGHT_CHAR_ID
                   && level == CH15_KNIGHT_LEVEL) {
            seen_knights++;
        } else if (char_id == CH15_SAMURAI_CHAR_ID
                   && level == CH15_SAMURAI_LEVEL) {
            seen_samurai++;
        } else if (char_id == CH15_ARCHER_CHAR_ID
                   && level == CH15_ARCHER_LEVEL) {
            seen_archers++;
        } else if (char_id == CH15_FLIER_CHAR_ID
                   && level == CH15_FLIER_LEVEL) {
            seen_fliers++;
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen_marked_timers[slot] = 0;
        seen_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The marker sits at index 34, which exists only if all four waves went
       down.  A run that opened a decoy stops at twenty-three units, and
       reading past the array to say so would be reading memory the allocation
       does not cover -- so the timers are left at zero and the case below
       fails on the marker value instead. */
    if (data_fdps_map_unit_count > SCRIPT_CH16_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH16_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 16 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The ten members the
   party has when the chapter opens are put on with the game's own add, in join
   order, so the ten player slots MAP15.DAT asks for are filled from the same
   ten. */
static void run_chapter_16_handler(void)
{
    int member;

    if (run16_state != 0) {
        return;
    }
    run16_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_16_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_16; member++) {
        fdps_roster_add_character(chapter_16_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_16_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_16();
    free_chapter_globals();
    run16_state = 1;
}

/* Nobody joins and every slot is filled.  The roster count still reads the ten
   the run put on, so no fdps_roster_add_character ran inside the handler, and
   MAP15.DAT's own two header counts come back where the staging left 99 and 0
   -- ten player slots against twenty-five scripted deployments, the first map
   in the game to ask for ten.  The ten player slots are then read off the map
   in join order and every one of them is on the player side, which is what
   says no slot fell through to the zeroed spare fdps_build_map_unit_array
   writes for a player slot with no roster member behind it. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_sixteen(void)
{
    int slot;

    run_chapter_16_handler();
    CHECK_EQ(run16_state, 1);
    if (run16_state != 1) {
        return;
    }

    CHECK_EQ(seen_roster_count, PARTY_AT_CHAPTER_16);
    CHECK_EQ(seen_player_slots, CH15_PLAYER_SLOTS);
    CHECK_EQ(seen_char_spawns, CH15_CHAR_SPAWNS);

    for (slot = 0; slot < PARTY_AT_CHAPTER_16; slot++) {
        CHECK_EQ(seen_party_char_ids[slot], chapter_16_party[slot]);
        CHECK_EQ(seen_party_sides[slot], PLAYER_SIDE);
    }
}

/* The chapter is fought with half the party, and it is the cut-scene that
   decides which half.  Map units 3, 4, 7, 8 and 9 carry the retired bit when
   the handler returns -- the five RETIREs the shipped ICON15.DAT runs in a
   row, with no REVIVE anywhere behind them -- and units 0, 1, 2, 5 and 6 carry
   a clear flags byte.  Read through the join order that is 法蓮娜, 裘娜, 蓋亞,
   琴琴 and 瑪麗安 off the board and 蘭迪斯, 尤利安, 亞克, 費塔加 and 布蘭多 on
   it, which is the strategy guide's 己方 line for the chapter.  Only a run
   that opened this member can produce it: neither of the two decoys retires
   anybody. */
static void chapter_sixteen_is_fought_with_half_the_party(void)
{
    int listed;

    run_chapter_16_handler();
    CHECK_EQ(run16_state, 1);
    if (run16_state != 1) {
        return;
    }

    for (listed = 0; listed < CH16_RETIRED_SLOTS; listed++) {
        CHECK_EQ(seen_party_flags[chapter_16_retired[listed]],
                 UNIT_FLAG_RETIRED);
    }
    for (listed = 0; listed < CH16_LIVE_SLOTS; listed++) {
        CHECK_EQ(seen_party_flags[chapter_16_live[listed]], 0);
    }

    CHECK_EQ(seen_party_char_ids[chapter_16_live[0]], RANDIS_CHAR_ID);
    CHECK_EQ(seen_party_char_ids[chapter_16_live[1]], JULIAN_CHAR_ID);
    CHECK_EQ(seen_party_char_ids[chapter_16_live[2]], ARC_CHAR_ID);
    CHECK_EQ(seen_party_char_ids[chapter_16_live[3]], FEITAGA_CHAR_ID);
    CHECK_EQ(seen_party_char_ids[chapter_16_live[4]], BRANDO_CHAR_ID);
}

/* What the reset and the cut-scene together leave on the map: the ten player
   slots, the thirteen MAP15.DAT tags wave 0, and the twelve the four
   DEPLOY_WAVEs put down, for thirty-five.  The census behind those twenty-five
   is the strategy guide's 敵方 list for the chapter found by character id and
   level -- four LV16 暗魔導士, four LV11 騎士, nine LV14 武士, four LV14
   弓箭手 and four LV12 飛兵 -- and every one of them is on the enemy side,
   which is the whole opposition down before the player's first turn. */
static void chapter_sixteen_opens_with_the_guides_enemy_group(void)
{
    run_chapter_16_handler();
    CHECK_EQ(run16_state, 1);
    if (run16_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit_count, CH15_UNITS_AFTER_SCRIPT);

    CHECK_EQ(seen_dark_mages, CH15_DARK_MAGE_COUNT);
    CHECK_EQ(seen_knights, CH15_KNIGHT_COUNT);
    CHECK_EQ(seen_samurai, CH15_SAMURAI_COUNT);
    CHECK_EQ(seen_archers, CH15_ARCHER_COUNT);
    CHECK_EQ(seen_fliers, CH15_FLIER_COUNT);
    CHECK_EQ(seen_enemy_side_units, CH15_GUIDE_ENEMY_TOTAL);
}

/* The cut-scene the handler names is Icon15.dat and not the neighbour on
   either side of it.  The marker sits on unit 34 with the value only
   ICON15.DAT writes -- the neighbour one late is chapter 17's own member and
   writes its marker on unit 32 with a value of its own -- and unit 0's timers
   are read back clear, which is where ICON14.DAT's decoy marker would have
   landed.  Every other timer on the
   marked unit is clear too: neither the handler nor the shipped cut-scene
   writes a status on anybody this chapter.  The chapter id is still 15, which
   the handler neither reads nor writes and which the script cannot disturb
   either -- ICON15.DAT carries no SWITCH_MAP -- and both the script number and
   the title-card graphic are chosen from it by the callees. */
static void the_chapter_16_cutscene_is_icon15_dat(void)
{
    int slot;

    run_chapter_16_handler();
    CHECK_EQ(run16_state, 1);
    if (run16_state != 1) {
        return;
    }

    CHECK_EQ(seen_marked_timers[SCRIPT_CH16_MARKER_SLOT],
             SCRIPT_CH16_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH16_MARKER_SLOT) {
            CHECK_EQ(seen_marked_timers[slot], 0);
        }
        CHECK_EQ(seen_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen_chapter_id, CHAPTER_16_ID);
}

/* The cursor ends on unit 0's tile, and unit 0 is where the cut-scene left it
   rather than where the map put it.  MAP15.COD record 25 -- the first record
   past the map's twenty-five scripted deployments -- puts player slot 0 on
   tile (19, 38), and the shipped member's group walks then step it to
   (21, 33); the cursor globals are that second tile scaled by the 24-pixel
   step, which only a handler that ran the cursor call after the script can
   produce.  Both globals were staged at 48 and 72 before the run. */
static void the_chapter_16_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_16_handler();
    CHECK_EQ(run16_state, 1);
    if (run16_state != 1) {
        return;
    }

    CHECK_EQ(seen_cursor_x, SCRIPT_CH16_WALKED_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen_cursor_y, SCRIPT_CH16_WALKED_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_17_init @ 000212d0 ------------------------------------ */

/* WHAT THE HANDLER IS.  The same four calls as the section above, in the same
   order, with two operands of its own: the cut-scene is Icon16.dat and the
   cursor is parked on unit 3 rather than unit 0.  Expected values come from
   the assembly at 000212d0 and from the shipped data:

     CALL 0x00022750                   the chapter state is rebuilt
     MOV EAX,0x618c4 / PUSH EAX /
       CALL 0x00021650                 the cut-scene "Icon16.dat" is run
     CALL 0x00020c60                   the title card is shown
     PUSH 0x3 / CALL 0x0002da50        the cursor is parked on unit 3

   The unit the cursor is parked on is what makes this handler different from
   twenty-seven of the thirty, so it is what the last case below is about. */

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run17_state = 0;

/* The five map units the cut-scene retires, and the five it leaves -- the
   complement of chapter 16's two lists. */
static int chapter_17_retired[CH17_RETIRED_SLOTS] = { 0, 1, 2, 5, 6 };
static int chapter_17_live[CH17_LIVE_SLOTS] = { 3, 4, 7, 8, 9 };

/* Everything the chapter 17 cases assert, captured the instant the handler
   returned. */
static int seen17_roster_count;
static int seen17_player_slots;
static int seen17_char_spawns;
static int seen17_unit_count;
static int seen17_party_char_ids[PARTY_AT_CHAPTER_17];
static int seen17_party_sides[PARTY_AT_CHAPTER_17];
static int seen17_party_flags[PARTY_AT_CHAPTER_17];
static int seen17_dark_mages;
static int seen17_knights;
static int seen17_samurai;
static int seen17_archers;
static int seen17_wolves;
static int seen17_enemy_side_units;
static int seen17_guest_side_units;
static unsigned char seen17_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen17_unit0_timers[STATUS_TIMER_COUNT];
static int seen17_cursor_x;
static int seen17_cursor_y;
static int seen17_chapter_id;

static void capture_chapter_17(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    int unit_index;
    int char_id;
    int level;
    int slot;

    seen17_roster_count = data_fdps_roster_member_count;
    seen17_player_slots = data_fdps_map_player_slot_count;
    seen17_char_spawns = data_fdps_map_char_spawn_count;
    seen17_unit_count = data_fdps_map_unit_count;
    seen17_cursor_x = data_fdps_map_cursor_world_x;
    seen17_cursor_y = data_fdps_map_cursor_world_y;
    seen17_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_17; slot++) {
        seen17_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen17_party_sides[slot] = (int) unit0[slot].side;
        seen17_party_flags[slot] = (int) unit0[slot].flags;
    }

    seen17_dark_mages = 0;
    seen17_knights = 0;
    seen17_samurai = 0;
    seen17_archers = 0;
    seen17_wolves = 0;
    seen17_enemy_side_units = 0;
    seen17_guest_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_17;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen17_enemy_side_units++;
        } else if (unit0[unit_index].side == GUEST_SIDE) {
            seen17_guest_side_units++;
        }
        if (char_id == MAP16_DARK_MAGE_CHAR_ID
            && level == MAP16_DARK_MAGE_LEVEL) {
            seen17_dark_mages++;
        } else if (char_id == MAP16_KNIGHT_CHAR_ID
                   && level == MAP16_KNIGHT_LEVEL) {
            seen17_knights++;
        } else if (char_id == MAP16_SAMURAI_CHAR_ID
                   && level == MAP16_SAMURAI_LEVEL) {
            seen17_samurai++;
        } else if (char_id == MAP16_ARCHER_CHAR_ID
                   && level == MAP16_ARCHER_LEVEL) {
            seen17_archers++;
        } else if (char_id == MAP16_WOLF_CHAR_ID
                   && level == MAP16_WOLF_LEVEL) {
            seen17_wolves++;
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen17_marked_timers[slot] = 0;
        seen17_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The marker sits at index 32, which exists only if the reset put all
       twenty-three wave-0 records down.  A run that opened a decoy lands
       thirty-three units as well -- the decoys deploy nothing either -- so
       what separates them is the value in the slot and not whether the unit
       is there; a run that opened ICON15.DAT lands thirty-three plus whatever
       MAP16.DAT's waves 1 to 4 add. */
    if (data_fdps_map_unit_count > SCRIPT_CH17_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH17_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen17_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 17 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The ten members the
   party has when the chapter opens are the same ten in the same join order the
   chapter 16 run puts on, so chapter_16_party is the list here too. */
static void run_chapter_17_handler(void)
{
    int member;

    if (run17_state != 0) {
        return;
    }
    run17_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_map_cursor_world_x = CH17_STAGED_CURSOR_X;
    data_fdps_map_cursor_world_y = CH17_STAGED_CURSOR_Y;
    data_fdps_chapter_current_chapter_id = CHAPTER_17_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_17; member++) {
        fdps_roster_add_character(chapter_16_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_17_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_17();
    free_chapter_globals();
    run17_state = 1;
}

/* Nobody joins and every slot is filled.  The roster count still reads the ten
   the run put on, so no fdps_roster_add_character ran inside the handler, and
   MAP16.DAT's own two header counts come back where the staging left 99 and 0
   -- ten player slots against thirty scripted deployments.  The ten player
   slots are then read off the map in join order and every one of them is on
   the player side, which is what says no slot fell through to the zeroed spare
   fdps_build_map_unit_array writes for a player slot with no roster member
   behind it. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_seventeen(void)
{
    int slot;

    run_chapter_17_handler();
    CHECK_EQ(run17_state, 1);
    if (run17_state != 1) {
        return;
    }

    CHECK_EQ(seen17_roster_count, PARTY_AT_CHAPTER_17);
    CHECK_EQ(seen17_player_slots, MAP16_PLAYER_SLOTS);
    CHECK_EQ(seen17_char_spawns, MAP16_CHAR_SPAWNS);

    for (slot = 0; slot < PARTY_AT_CHAPTER_17; slot++) {
        CHECK_EQ(seen17_party_char_ids[slot], chapter_16_party[slot]);
        CHECK_EQ(seen17_party_sides[slot], PLAYER_SIDE);
    }
}

/* The chapter is fought with the half chapter 16 sat out, and it is the
   cut-scene that decides which half.  Map units 0, 1, 2, 5 and 6 carry the
   retired bit when the handler returns -- the five RETIREs the shipped
   ICON16.DAT runs in a row, with no REVIVE anywhere behind them -- and units
   3, 4, 7, 8 and 9 carry a clear flags byte.  Read through the join order that
   is 蘭迪斯, 尤利安, 亞克, 費塔加 and 布蘭多 off the board and 法蓮娜, 裘娜,
   蓋亞, 琴琴 and 瑪麗安 on it, which is the strategy guide's 己方 line for the
   chapter and the exact complement of the chapter 16 case above.  Only a run
   that opened this member can produce it: the two decoys retire nobody,
   ICON15.DAT retires the other five, and ICON17.DAT retires those same other
   five and then revives them. */
static void chapter_seventeen_is_fought_with_the_other_half(void)
{
    int listed;

    run_chapter_17_handler();
    CHECK_EQ(run17_state, 1);
    if (run17_state != 1) {
        return;
    }

    for (listed = 0; listed < CH17_RETIRED_SLOTS; listed++) {
        CHECK_EQ(seen17_party_flags[chapter_17_retired[listed]],
                 UNIT_FLAG_RETIRED);
    }
    for (listed = 0; listed < CH17_LIVE_SLOTS; listed++) {
        CHECK_EQ(seen17_party_flags[chapter_17_live[listed]], 0);
    }

    CHECK_EQ(seen17_party_char_ids[chapter_17_live[0]], FLARENA_CHAR_ID);
    CHECK_EQ(seen17_party_char_ids[chapter_17_live[1]], JUNA_CHAR_ID);
    CHECK_EQ(seen17_party_char_ids[chapter_17_live[2]], GAIA_CHAR_ID);
    CHECK_EQ(seen17_party_char_ids[chapter_17_live[3]], QINQIN_CHAR_ID);
    CHECK_EQ(seen17_party_char_ids[chapter_17_live[4]], MARIAN_CHAR_ID);
}

/* What the reset alone leaves on the map, because the cut-scene deploys
   nobody: the ten player slots and the twenty-three records MAP16.DAT tags
   wave 0, for thirty-three.  Twenty of the twenty-three are enemy-side and are
   the strategy guide's opening 敵方 list found by character id and level --
   two LV16 暗魔導士, six LV11 騎士, ten LV15 武士 and two LV14 弓箭手 -- and
   the other three are the guest-side hostages the chapter is named for.

   The two counts that say the cut-scene deployed nothing are the last pair:
   the 武士 census stops at ten and not sixteen, and no 狼人 is on the board at
   all.  Those seven are MAP16.DAT's waves 1 and 2, which the chapter's own
   turn events bring in on the eighth and ninth rounds. */
static void chapter_seventeen_opens_with_the_guides_enemy_group(void)
{
    run_chapter_17_handler();
    CHECK_EQ(run17_state, 1);
    if (run17_state != 1) {
        return;
    }

    CHECK_EQ(seen17_unit_count, CH17_UNITS_AFTER_SCRIPT);

    CHECK_EQ(seen17_dark_mages, MAP16_DARK_MAGE_COUNT);
    CHECK_EQ(seen17_knights, MAP16_KNIGHT_COUNT);
    CHECK_EQ(seen17_archers, MAP16_ARCHER_COUNT);
    CHECK_EQ(seen17_enemy_side_units, MAP16_GUIDE_OPENING_ENEMY_TOTAL);
    CHECK_EQ(seen17_guest_side_units, MAP16_GUEST_UNITS);

    CHECK_EQ(seen17_samurai, MAP16_SAMURAI_COUNT);
    CHECK_EQ(seen17_wolves, 0);
}

/* The cut-scene the handler names is Icon16.dat and not the neighbour on
   either side of it.  The marker sits on unit 32 with the value only
   ICON16.DAT writes -- the neighbour one early is chapter 16's own member and
   marks unit 34, the neighbour one late is chapter 18's and marks unit 27 --
   and unit 0's own timers are read back clear, which is where ICON14.DAT's
   decoy marker would have landed.  Every other timer on the
   marked unit is clear too: neither the handler nor the shipped cut-scene
   writes a status on anybody this chapter.  The chapter id is still 16, which
   the handler neither reads nor writes and which the script cannot disturb
   either -- ICON16.DAT carries no SWITCH_MAP -- and both the script number and
   the title-card graphic are chosen from it by the callees. */
static void the_chapter_17_cutscene_is_icon16_dat(void)
{
    int slot;

    run_chapter_17_handler();
    CHECK_EQ(run17_state, 1);
    if (run17_state != 1) {
        return;
    }

    CHECK_EQ(seen17_marked_timers[SCRIPT_CH17_MARKER_SLOT],
             SCRIPT_CH17_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH17_MARKER_SLOT) {
            CHECK_EQ(seen17_marked_timers[slot], 0);
        }
        CHECK_EQ(seen17_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen17_chapter_id, CHAPTER_17_ID);
}

/* The cursor ends on unit 3's tile and not unit 0's, which is the one thing
   that separates this handler from twenty-seven of the thirty: PUSH 0x3 at
   000212f4.  Unit 3 is 法蓮娜, and the cut-scene has left her on (2, 7); the
   cursor globals are that tile scaled by the 24-pixel step.  Unit 0 is on the
   tile MAP16.COD gave player slot 0 and the cut-scene never moved it, so a
   handler that passed 0 lands somewhere else entirely -- and both globals were
   staged at numbers neither unit stands on. */
static void the_chapter_17_cursor_is_parked_on_unit_three(void)
{
    run_chapter_17_handler();
    CHECK_EQ(run17_state, 1);
    if (run17_state != 1) {
        return;
    }

    CHECK_EQ(seen17_cursor_x, SCRIPT_CH17_WALKED_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen17_cursor_y, SCRIPT_CH17_WALKED_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_18_init @ 00021310 ------------------------------------ */

/* WHAT THE HANDLER IS.  The same four calls as the two sections above, in the
   same order, with one operand of its own: the cut-scene is Icon17.dat.  The
   cursor goes back to unit 0.  Expected values come from the assembly at
   00021310 and from the shipped data:

     CALL 0x00022750                   the chapter state is rebuilt
     MOV EAX,0x618d0 / PUSH EAX /
       CALL 0x00021650                 the cut-scene "Icon17.dat" is run
     CALL 0x00020c60                   the title card is shown
     PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0

   WHAT MAKES THIS CHAPTER DIFFERENT FROM ITS TWO NEIGHBOURS.  MAP17.DAT tags
   only two of its sixty-five deployment records wave 0, and both are the
   cut-scene's own actors, so the reset alone leaves no opposition on the board
   at all: the fifteen enemies the chapter opens with arrive through the
   member's own closing DEPLOY_WAVE.  And the member is the only one of the
   three that REVIVEs -- it takes the same five player slots off that chapter
   16's member takes off, and puts all five back before it ends -- so the whole
   party is standing when the handler returns. */

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run18_state = 0;

/* The three units the cut-scene acts with, all three retired when it ends. */
static int chapter_18_actors[CH18_SCENE_ACTOR_UNITS] = {
    SCRIPT_CH18_ACTOR_UNIT_GUEST, SCRIPT_CH18_ACTOR_UNIT_SECOND,
    SCRIPT_CH18_ACTOR_UNIT_THIRD
};

/* Everything the chapter 18 cases assert, captured the instant the handler
   returned. */
static int seen18_roster_count;
static int seen18_player_slots;
static int seen18_char_spawns;
static int seen18_unit_count;
static int seen18_party_char_ids[PARTY_AT_CHAPTER_18];
static int seen18_party_sides[PARTY_AT_CHAPTER_18];
static int seen18_party_flags[PARTY_AT_CHAPTER_18];
static int seen18_actor_flags[CH18_SCENE_ACTOR_UNITS];
static int seen18_actor_char_ids[CH18_SCENE_ACTOR_UNITS];
static int seen18_actor_levels[CH18_SCENE_ACTOR_UNITS];
static int seen18_guards;
static int seen18_archers;
static int seen18_knights;
static int seen18_dark_knights;
static int seen18_dark_mages;
static int seen18_fliers;
static int seen18_enemy_side_units;
static int seen18_guest_side_units;
static unsigned char seen18_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen18_unit0_timers[STATUS_TIMER_COUNT];
static int seen18_cursor_x;
static int seen18_cursor_y;
static int seen18_chapter_id;

static void capture_chapter_18(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    int unit_index;
    int char_id;
    int level;
    int slot;
    int actor;

    seen18_roster_count = data_fdps_roster_member_count;
    seen18_player_slots = data_fdps_map_player_slot_count;
    seen18_char_spawns = data_fdps_map_char_spawn_count;
    seen18_unit_count = data_fdps_map_unit_count;
    seen18_cursor_x = data_fdps_map_cursor_world_x;
    seen18_cursor_y = data_fdps_map_cursor_world_y;
    seen18_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_18; slot++) {
        seen18_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen18_party_sides[slot] = (int) unit0[slot].side;
        seen18_party_flags[slot] = (int) unit0[slot].flags;
    }

    /* The three actors sit right behind the player slots, so they are inside
       any array the reset built; a run that never got that far is caught by
       the unit count first. */
    for (actor = 0; actor < CH18_SCENE_ACTOR_UNITS; actor++) {
        seen18_actor_flags[actor] = 0;
        seen18_actor_char_ids[actor] = 0;
        seen18_actor_levels[actor] = 0;
        if (chapter_18_actors[actor] < data_fdps_map_unit_count) {
            seen18_actor_flags[actor] =
                (int) unit0[chapter_18_actors[actor]].flags;
            seen18_actor_char_ids[actor] =
                (int) unit0[chapter_18_actors[actor]].char_id;
            seen18_actor_levels[actor] =
                (int) unit0[chapter_18_actors[actor]].level;
        }
    }

    seen18_guards = 0;
    seen18_archers = 0;
    seen18_knights = 0;
    seen18_dark_knights = 0;
    seen18_dark_mages = 0;
    seen18_fliers = 0;
    seen18_enemy_side_units = 0;
    seen18_guest_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_18;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen18_enemy_side_units++;
        } else if (unit0[unit_index].side == GUEST_SIDE) {
            seen18_guest_side_units++;
        }
        if (char_id == MAP17_GUARD_CHAR_ID && level == MAP17_GUARD_LEVEL) {
            seen18_guards++;
        } else if (char_id == MAP17_ARCHER_CHAR_ID
                   && level == MAP17_ARCHER_LEVEL) {
            seen18_archers++;
        } else if (char_id == MAP17_KNIGHT_CHAR_ID
                   && level == MAP17_KNIGHT_LEVEL) {
            seen18_knights++;
        } else if (char_id == MAP17_DARK_KNIGHT_CHAR_ID
                   && level == MAP17_DARK_KNIGHT_LEVEL) {
            seen18_dark_knights++;
        } else if (char_id == MAP17_DARK_MAGE_CHAR_ID
                   && level == MAP17_DARK_MAGE_LEVEL) {
            seen18_dark_mages++;
        } else if (char_id == MAP17_FLIER_CHAR_ID
                   && level == MAP17_FLIER_LEVEL) {
            seen18_fliers++;
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen18_marked_timers[slot] = 0;
        seen18_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The marker sits at index 27, which exists only if both DEPLOY_WAVEs went
       down.  A run that opened a decoy stops at twelve units, and reading past
       the array to say so would be reading memory the allocation does not
       cover -- so the timers are left at zero and the case below fails on the
       marker value instead. */
    if (data_fdps_map_unit_count > SCRIPT_CH18_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH18_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen18_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 18 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The ten members the
   party has when the chapter opens are the same ten in the same join order the
   chapter 16 run puts on, so chapter_16_party is the list here too. */
static void run_chapter_18_handler(void)
{
    int member;

    if (run18_state != 0) {
        return;
    }
    run18_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_map_cursor_world_x = CH18_STAGED_CURSOR_X;
    data_fdps_map_cursor_world_y = CH18_STAGED_CURSOR_Y;
    data_fdps_chapter_current_chapter_id = CHAPTER_18_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_18; member++) {
        fdps_roster_add_character(chapter_16_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_18_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_18();
    free_chapter_globals();
    run18_state = 1;
}

/* Nobody joins and every slot is filled.  The roster count still reads the ten
   the run put on, so no fdps_roster_add_character ran inside the handler, and
   MAP17.DAT's own two header counts come back where the staging left 99 and 0
   -- ten player slots against sixty-five scripted deployments, where chapter
   17's own map carries thirty.  The ten player slots are then
   read off the map in join order and every one of them is on the player side,
   which is what says no slot fell through to the zeroed spare
   fdps_build_map_unit_array writes for a player slot with no roster member
   behind it. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_eighteen(void)
{
    int slot;

    run_chapter_18_handler();
    CHECK_EQ(run18_state, 1);
    if (run18_state != 1) {
        return;
    }

    CHECK_EQ(seen18_roster_count, PARTY_AT_CHAPTER_18);
    CHECK_EQ(seen18_player_slots, MAP17_PLAYER_SLOTS);
    CHECK_EQ(seen18_char_spawns, MAP17_CHAR_SPAWNS);

    for (slot = 0; slot < PARTY_AT_CHAPTER_18; slot++) {
        CHECK_EQ(seen18_party_char_ids[slot], chapter_16_party[slot]);
        CHECK_EQ(seen18_party_sides[slot], PLAYER_SIDE);
    }
}

/* The chapter is fought with the whole party, and the three the cut-scene acts
   with are off the board.  Every one of the ten player slots carries a clear
   flags byte when the handler returns -- ICON17.DAT retires map units 3, 4, 7,
   8 and 9 for the length of the scene and REVIVEs all five before it ends,
   which no other member of the container does -- and map units 10, 11 and 12,
   the guest-side character 12 and the two scene actors, all three carry the
   retired bit.  Reading the ten through the join order that is the whole
   party, which is the strategy guide's 己方 line for the chapter: it names
   nobody sitting out. */
static void chapter_eighteen_is_fought_with_the_whole_party(void)
{
    int slot;
    int actor;

    run_chapter_18_handler();
    CHECK_EQ(run18_state, 1);
    if (run18_state != 1) {
        return;
    }

    for (slot = 0; slot < PARTY_AT_CHAPTER_18; slot++) {
        CHECK_EQ(seen18_party_flags[slot], 0);
    }

    for (actor = 0; actor < CH18_SCENE_ACTOR_UNITS; actor++) {
        CHECK_EQ(seen18_actor_flags[actor], UNIT_FLAG_RETIRED);
    }

    CHECK_EQ(seen18_actor_char_ids[0], CH18_GUEST_CHAR_ID);
    CHECK_EQ(seen18_actor_levels[0], CH18_GUEST_LEVEL);
    CHECK_EQ(seen18_actor_char_ids[1], CH18_SCENE_CHAR_ID_A);
    CHECK_EQ(seen18_actor_levels[1], CH18_SCENE_LEVEL);
    CHECK_EQ(seen18_actor_char_ids[2], CH18_SCENE_CHAR_ID_B);
    CHECK_EQ(seen18_actor_levels[2], CH18_SCENE_LEVEL);
}

/* What the reset and the cut-scene together leave on the map: the ten player
   slots, MAP17.DAT's two wave-0 actors, the one the member's exact-anchor
   DEPLOY_WAVE adds, and the fifteen its nearest-free-tile DEPLOY_WAVE adds,
   for twenty-eight.  The census behind those fifteen is the strategy guide's
   opening 敵方 list for the chapter found by character id and level -- five
   LV14 衛兵, two LV15 弓箭手, five LV13 騎士, two LV14 暗黑騎士 and one LV16
   暗魔導士 -- and seventeen units are on the enemy side, the fifteen plus the
   two scene actors that carry it, against the one guest.

   The count that says none of the chapter's turn events has fired is the
   飛兵 census: MAP17.DAT holds sixteen of them on waves 4, 6, 8 and 10, the
   guide's 第四、六、八、十回合 flights, and not one is on the board. */
static void chapter_eighteen_opens_with_the_guides_enemy_group(void)
{
    run_chapter_18_handler();
    CHECK_EQ(run18_state, 1);
    if (run18_state != 1) {
        return;
    }

    CHECK_EQ(seen18_unit_count, CH18_UNITS_AFTER_SCRIPT);

    CHECK_EQ(seen18_guards, MAP17_GUARD_COUNT);
    CHECK_EQ(seen18_archers, MAP17_ARCHER_COUNT);
    CHECK_EQ(seen18_knights, MAP17_KNIGHT_COUNT);
    CHECK_EQ(seen18_dark_knights, MAP17_DARK_KNIGHT_COUNT);
    CHECK_EQ(seen18_dark_mages, MAP17_DARK_MAGE_COUNT);
    CHECK_EQ(seen18_enemy_side_units, CH18_ENEMY_SIDE_UNITS);
    CHECK_EQ(seen18_guest_side_units, CH18_GUEST_SIDE_UNITS);

    CHECK_EQ(seen18_fliers, 0);
}

/* The cut-scene the handler names is Icon17.dat and not the neighbour on
   either side of it.  The marker sits on unit 27 with the value only
   ICON17.DAT writes -- the neighbour one early is chapter 17's own member and
   marks unit 32, the neighbour one late is chapter 19's own member and marks
   unit 53, an index this chapter's twenty-eight-unit map never reaches -- and
   unit 0's own timers are read back clear.  Every other timer on the marked
   unit is clear too: neither the handler nor the shipped cut-scene writes a
   status on anybody this chapter.  The chapter id is still 17, which the handler neither
   reads nor writes and which the script cannot disturb either -- ICON17.DAT
   carries no SWITCH_MAP -- and both the script number and the title-card
   graphic are chosen from it by the callees. */
static void the_chapter_18_cutscene_is_icon17_dat(void)
{
    int slot;

    run_chapter_18_handler();
    CHECK_EQ(run18_state, 1);
    if (run18_state != 1) {
        return;
    }

    CHECK_EQ(seen18_marked_timers[SCRIPT_CH18_MARKER_SLOT],
             SCRIPT_CH18_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH18_MARKER_SLOT) {
            CHECK_EQ(seen18_marked_timers[slot], 0);
        }
        CHECK_EQ(seen18_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen18_chapter_id, CHAPTER_18_ID);
}

/* The cursor ends on unit 0's tile, and unit 0 is where the cut-scene left it
   rather than where the map put it.  MAP17.COD gives all ten player slots the
   same start tile, (3, 18), and the shipped member places unit 0 on (17, 24)
   and walks it five tiles up to (17, 19); the cursor globals are that second
   tile scaled by the 24-pixel step, which only a handler that ran the cursor
   call after the script can produce.  Both globals were staged at numbers
   neither tile stands on. */
static void the_chapter_18_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_18_handler();
    CHECK_EQ(run18_state, 1);
    if (run18_state != 1) {
        return;
    }

    CHECK_EQ(seen18_cursor_x, SCRIPT_CH18_WALKED_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen18_cursor_y, SCRIPT_CH18_WALKED_TILE_Y * CURSOR_TILE_STEP);
}


/* --- fdps_chapter_19_init @ 00021350 ------------------------------------ */

/* WHAT THE HANDLER IS.  The four calls of the three sections above with a
   fifth in front of them, the roster add, and two operands of its own: the
   character who joins is 11 and the cut-scene is Icon18.dat.  The cursor goes
   back to unit 0.  Expected values come from the assembly at 00021350 and from
   the shipped data:

     PUSH 0xb / CALL 0x00023bc0        character 11 joins the roster
     CALL 0x00022750                   the chapter state is rebuilt
     MOV EAX,0x618dc / PUSH EAX /
       CALL 0x00021650                 the cut-scene "Icon18.dat" is run
     CALL 0x00020c60                   the title card is shown
     PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0

   WHAT THE ADD IS WORTH ASSERTING.  蘭斯洛特 is roster slot 10 and MAP18.DAT
   asks for ten player slots, so the add changes nothing about this chapter's
   board -- what it changes is the roster, and the roster is where the case
   below reads it.  The record it builds is level 15 at 616 HP, while the unit
   the player watches arrive on turn 6 is MAP18.DAT's own wave-1 record at
   level 2; both are checked, the first in the roster block and the second by
   its absence from the map. */

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run19_state = 0;

/* Everything the chapter 19 cases assert, captured the instant the handler
   returned. */
static int seen19_roster_count;
static int seen19_joined_char_id;
static int seen19_joined_level;
static int seen19_joined_side;
static int seen19_joined_hp_current;
static int seen19_joined_hp_max;
static int seen19_joined_mp_current;
static int seen19_joined_mp_max;
static int seen19_player_slots;
static int seen19_char_spawns;
static int seen19_unit_count;
static int seen19_party_char_ids[PARTY_AT_CHAPTER_19];
static int seen19_party_sides[PARTY_AT_CHAPTER_19];
static int seen19_party_flags[PARTY_AT_CHAPTER_19];
static int seen19_lancelot_map_units;
static int seen19_archers;
static int seen19_dark_mages;
static int seen19_fliers;
static int seen19_barbarians;
static int seen19_dark_knights;
static int seen19_brawlers;
static int seen19_enemy_side_units;
static int seen19_guest_side_units;
static unsigned char seen19_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen19_unit0_timers[STATUS_TIMER_COUNT];
static int seen19_cursor_x;
static int seen19_cursor_y;
static int seen19_chapter_id;

static void capture_chapter_19(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    struct fdps_unit_record *joined;
    int unit_index;
    int char_id;
    int level;
    int slot;

    seen19_roster_count = data_fdps_roster_member_count;

    joined = &stage_roster[LANCELOT_ROSTER_SLOT];
    seen19_joined_char_id = (int) joined->char_id;
    seen19_joined_level = (int) joined->level;
    seen19_joined_side = (int) joined->side;
    seen19_joined_hp_current = (int) joined->hp_current;
    seen19_joined_hp_max = (int) joined->hp_max;
    seen19_joined_mp_current = (int) joined->mp_current;
    seen19_joined_mp_max = (int) joined->mp_max;

    seen19_player_slots = data_fdps_map_player_slot_count;
    seen19_char_spawns = data_fdps_map_char_spawn_count;
    seen19_unit_count = data_fdps_map_unit_count;
    seen19_cursor_x = data_fdps_map_cursor_world_x;
    seen19_cursor_y = data_fdps_map_cursor_world_y;
    seen19_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_19; slot++) {
        seen19_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen19_party_sides[slot] = (int) unit0[slot].side;
        seen19_party_flags[slot] = (int) unit0[slot].flags;
    }

    seen19_lancelot_map_units = 0;
    seen19_archers = 0;
    seen19_dark_mages = 0;
    seen19_fliers = 0;
    seen19_barbarians = 0;
    seen19_dark_knights = 0;
    seen19_brawlers = 0;
    seen19_enemy_side_units = 0;
    seen19_guest_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_19;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen19_enemy_side_units++;
        } else if (unit0[unit_index].side == GUEST_SIDE) {
            seen19_guest_side_units++;
        }
        if (char_id == LANCELOT_CHAR_ID) {
            seen19_lancelot_map_units++;
        }
        if (char_id == MAP18_ARCHER_CHAR_ID && level == MAP18_ARCHER_LEVEL) {
            seen19_archers++;
        } else if (char_id == MAP18_DARK_MAGE_CHAR_ID
                   && level == MAP18_DARK_MAGE_LEVEL) {
            seen19_dark_mages++;
        } else if (char_id == MAP18_FLIER_CHAR_ID
                   && level == MAP18_FLIER_LEVEL) {
            seen19_fliers++;
        } else if (char_id == MAP18_BARBARIAN_CHAR_ID
                   && level == MAP18_BARBARIAN_LEVEL) {
            seen19_barbarians++;
        } else if (char_id == MAP18_DARK_KNIGHT_CHAR_ID
                   && level == MAP18_DARK_KNIGHT_LEVEL) {
            seen19_dark_knights++;
        } else if (char_id == MAP18_BRAWLER_CHAR_ID
                   && level == MAP18_BRAWLER_LEVEL) {
            seen19_brawlers++;
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen19_marked_timers[slot] = 0;
        seen19_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The marker sits at index 53, which exists only if the DEPLOY_WAVE went
       down.  A run that opened a decoy stops at fifteen units, and reading
       past the array to say so would be reading memory the allocation does not
       cover -- so the timers are left at zero and the case below fails on the
       marker value instead. */
    if (data_fdps_map_unit_count > SCRIPT_CH19_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH19_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen19_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 19 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The ten members the
   party has when the chapter opens are the same ten in the same join order the
   chapter 16 run puts on, so chapter_16_party is the list here too -- the
   eleventh is the handler's own business and is deliberately not staged. */
static void run_chapter_19_handler(void)
{
    int member;

    if (run19_state != 0) {
        return;
    }
    run19_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_map_cursor_world_x = CH19_STAGED_CURSOR_X;
    data_fdps_map_cursor_world_y = CH19_STAGED_CURSOR_Y;
    data_fdps_chapter_current_chapter_id = CHAPTER_19_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_19; member++) {
        fdps_roster_add_character(chapter_16_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_19_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_19();
    free_chapter_globals();
    run19_state = 1;
}

/* 蘭斯洛特 joins the roster when the chapter opens, and the record the add
   built is the one the strategy guide's 備註 prints for a chapter finished
   before his arrival event fires: LV15, 616 HP both current and maximum, 0 MP
   both ways, on the player side.  616 is FRIAPRDA.DAT's 420 base plus
   FRILEVUP.DAT's 14 a level over fourteen levels, which is the sum
   fdps_roster_add_character computes and the number the guide prints, so the
   two are checked against each other rather than either being assumed.  The
   count going from ten to eleven is what says the add ran at all, and the two
   map header counts are read back as well because they were staged at numbers
   no map carries. */
static void lancelot_joins_the_roster_when_chapter_nineteen_opens(void)
{
    run_chapter_19_handler();
    CHECK_EQ(run19_state, 1);
    if (run19_state != 1) {
        return;
    }

    CHECK_EQ(seen19_roster_count, PARTY_AT_CHAPTER_19 + 1);
    CHECK_EQ(seen19_joined_char_id, LANCELOT_CHAR_ID);
    CHECK_EQ(seen19_joined_level, LANCELOT_LEVEL);
    CHECK_EQ(seen19_joined_side, PLAYER_SIDE);
    CHECK_EQ(seen19_joined_hp_max, LANCELOT_HP_MAX);
    CHECK_EQ(seen19_joined_hp_current, LANCELOT_HP_MAX);
    CHECK_EQ(seen19_joined_mp_max, LANCELOT_MP_MAX);
    CHECK_EQ(seen19_joined_mp_current, LANCELOT_MP_MAX);
    CHECK_EQ(seen19_player_slots, MAP18_PLAYER_SLOTS);
    CHECK_EQ(seen19_char_spawns, MAP18_CHAR_SPAWNS);
}

/* And he is NOT one of chapter 19's map units.  MAP18.DAT's ten player slots
   are filled from roster slots 0 to 9 -- the ten the chapter opened with, read
   back in join order and every one of them on the player side with a clear
   flags byte, which says no slot fell through to the zeroed, retired spare
   fdps_build_map_unit_array writes for a player slot with no roster member
   behind it.  Roster slot 10 is one past the last slot the map asks for, so
   character 11 appears nowhere in the array at all: the unit the player sees
   arrive is MAP18.DAT's wave-1 record and the turn-6 event deploys it, not
   this handler. */
static void lancelot_is_not_one_of_the_chapter_nineteen_map_units(void)
{
    int slot;

    run_chapter_19_handler();
    CHECK_EQ(run19_state, 1);
    if (run19_state != 1) {
        return;
    }

    for (slot = 0; slot < PARTY_AT_CHAPTER_19; slot++) {
        CHECK_EQ(seen19_party_char_ids[slot], chapter_16_party[slot]);
        CHECK_EQ(seen19_party_sides[slot], PLAYER_SIDE);
        CHECK_EQ(seen19_party_flags[slot], 0);
    }

    CHECK_EQ(seen19_lancelot_map_units, 0);
}

/* What the reset and the cut-scene together leave on the map: the ten player
   slots, MAP18.DAT's five wave-0 暗黑騎士, and the thirty-nine the member's
   nearest-free-tile DEPLOY_WAVE adds, for fifty-four.  The census behind those
   forty-four is the strategy guide's opening 敵方 list for the chapter found
   by character id and level -- fourteen LV17 弓箭手, seven LV18 暗魔導士, six
   LV17 飛兵, five LV17 野蠻戰士 and twelve LV15 暗黑騎士 -- and all forty-four
   are on the enemy side, with no guest at all.

   The count that says none of the chapter's turn events has fired is the
   武鬥家 census: MAP18.DAT holds eight of them on wave 6, the reinforcement
   the guide has arriving when the party crosses the line above the blue chest,
   and not one is on the board. */
static void chapter_nineteen_opens_with_the_guides_enemy_group(void)
{
    run_chapter_19_handler();
    CHECK_EQ(run19_state, 1);
    if (run19_state != 1) {
        return;
    }

    CHECK_EQ(seen19_unit_count, CH19_UNITS_AFTER_SCRIPT);

    CHECK_EQ(seen19_archers, MAP18_ARCHER_COUNT);
    CHECK_EQ(seen19_dark_mages, MAP18_DARK_MAGE_COUNT);
    CHECK_EQ(seen19_fliers, MAP18_FLIER_COUNT);
    CHECK_EQ(seen19_barbarians, MAP18_BARBARIAN_COUNT);
    CHECK_EQ(seen19_dark_knights, MAP18_DARK_KNIGHT_COUNT);
    CHECK_EQ(seen19_enemy_side_units, CH19_ENEMY_SIDE_UNITS);
    CHECK_EQ(seen19_guest_side_units, CH19_GUEST_SIDE_UNITS);

    CHECK_EQ(seen19_brawlers, 0);
}

/* The cut-scene the handler names is Icon18.dat and not the neighbour on
   either side of it.  The marker sits on unit 53 with the value only
   ICON18.DAT writes -- the neighbour one early is chapter 18's own member and
   marks unit 27, the neighbour one late is chapter 20's member, which switches
   the map twice and marks unit 64 -- and unit 0's own timers are read back
   clear, which is where either end decoy's marker would have landed.  Every other timer on the marked unit is
   clear too: neither the handler nor the shipped cut-scene writes a status on
   anybody this chapter, and the shipped member has no unit-state opcode of any
   kind.  The chapter id is still 18, which the handler neither reads nor
   writes and which the script cannot disturb either -- ICON18.DAT carries no
   SWITCH_MAP -- and both the script number and the title-card graphic are
   chosen from it by the callees. */
static void the_chapter_19_cutscene_is_icon18_dat(void)
{
    int slot;

    run_chapter_19_handler();
    CHECK_EQ(run19_state, 1);
    if (run19_state != 1) {
        return;
    }

    CHECK_EQ(seen19_marked_timers[SCRIPT_CH19_MARKER_SLOT],
             SCRIPT_CH19_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH19_MARKER_SLOT) {
            CHECK_EQ(seen19_marked_timers[slot], 0);
        }
        CHECK_EQ(seen19_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen19_chapter_id, CHAPTER_19_ID);
}

/* The cursor ends on unit 0's tile, and unit 0 is where the cut-scene left it
   rather than where the map put it.  MAP18.COD gives player slot 0 the start
   tile (12, 2), and the shipped member walks unit 0 one tile left and four
   down to (11, 6); the cursor globals are that second tile scaled by the
   24-pixel step, which only a handler that ran the cursor call after the
   script can produce.  Both globals were staged at numbers neither tile stands
   on. */
static void the_chapter_19_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_19_handler();
    CHECK_EQ(run19_state, 1);
    if (run19_state != 1) {
        return;
    }

    CHECK_EQ(seen19_cursor_x, SCRIPT_CH19_WALKED_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen19_cursor_y, SCRIPT_CH19_WALKED_TILE_Y * CURSOR_TILE_STEP);
}


/* --- fdps_chapter_20_init @ 00021390 ------------------------------------ */

/* WHAT THE HANDLER IS.  The plain four calls again, with no roster add in
   front of them and two operands of its own: the cut-scene is Icon19.dat and
   the cursor goes to unit 0.  Expected values come from the assembly at
   00021390 and from the shipped data:

     CALL 0x00022750                   the chapter state is rebuilt
     MOV EAX,0x618e8 / PUSH EAX /
       CALL 0x00021650                 the cut-scene "Icon19.dat" is run
     CALL 0x00020c60                   the title card is shown
     PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0

   WHY THIS ONE IS NOT ANOTHER COPY OF CHAPTER 16.  Two things are true here
   that are true of none of the four sections above.  The first is the
   roster: MAP19.DAT asks for
   ELEVEN player slots against the eleven-member roster chapter 19 left, so
   蘭斯洛特 stops being a spare and becomes map unit 10.  The second is the
   cut-scene: ICON19.DAT switches the chapter to 58, plays itself out on a
   stage with no player slots, and switches back to 19 -- which rebuilds the
   chapter state a second time and throws every unit the scene touched away.
   So the board this handler returns on is the map's alone, the cursor lands on
   MAP19.COD's own start tile rather than on a tile a walk reached, and the
   chapter id being 19 at the end is the script putting it back rather than
   nobody having moved it.  The cases below assert all three. */

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run20_state = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order: the ten chapter 16 opened with, and 蘭斯洛特 behind them. */
static int chapter_20_party[PARTY_AT_CHAPTER_20] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID, MARIAN_CHAR_ID, LANCELOT_CHAR_ID
};

/* Everything the chapter 20 cases assert, captured the instant the handler
   returned. */
static int seen20_roster_count;
static int seen20_player_slots;
static int seen20_char_spawns;
static int seen20_unit_count;
static int seen20_party_char_ids[PARTY_AT_CHAPTER_20];
static int seen20_party_sides[PARTY_AT_CHAPTER_20];
static int seen20_party_flags[PARTY_AT_CHAPTER_20];
static int seen20_snakes;
static int seen20_barbarians;
static int seen20_wolves;
static int seen20_ice_mages;
static int seen20_wave_one_units;
static int seen20_scene_only_units;
static int seen20_enemy_side_units;
static int seen20_guest_side_units;
static unsigned char seen20_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen20_unit0_timers[STATUS_TIMER_COUNT];
static int seen20_cursor_x;
static int seen20_cursor_y;
static int seen20_chapter_id;

static void capture_chapter_20(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    int unit_index;
    int char_id;
    int level;
    int slot;

    seen20_roster_count = data_fdps_roster_member_count;
    seen20_player_slots = data_fdps_map_player_slot_count;
    seen20_char_spawns = data_fdps_map_char_spawn_count;
    seen20_unit_count = data_fdps_map_unit_count;
    seen20_cursor_x = data_fdps_map_cursor_world_x;
    seen20_cursor_y = data_fdps_map_cursor_world_y;
    seen20_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_20; slot++) {
        seen20_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen20_party_sides[slot] = (int) unit0[slot].side;
        seen20_party_flags[slot] = (int) unit0[slot].flags;
    }

    seen20_snakes = 0;
    seen20_barbarians = 0;
    seen20_wolves = 0;
    seen20_ice_mages = 0;
    seen20_wave_one_units = 0;
    seen20_scene_only_units = 0;
    seen20_enemy_side_units = 0;
    seen20_guest_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_20;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen20_enemy_side_units++;
        } else if (unit0[unit_index].side == GUEST_SIDE) {
            seen20_guest_side_units++;
        }
        if (char_id == SCRIPT_CH20_SCENE_ONLY_CHAR_ID) {
            seen20_scene_only_units++;
        }
        if (char_id == MAP19_SNAKE_CHAR_ID && level == MAP19_SNAKE_LEVEL) {
            seen20_snakes++;
        } else if (char_id == MAP19_BARBARIAN_CHAR_ID
                   && level == MAP19_BARBARIAN_LEVEL) {
            seen20_barbarians++;
        } else if (char_id == MAP19_WOLF_CHAR_ID
                   && level == MAP19_WOLF_LEVEL) {
            seen20_wolves++;
        } else if (char_id == MAP19_ICE_MAGE_CHAR_ID
                   && level == MAP19_ICE_MAGE_LEVEL) {
            seen20_ice_mages++;
        } else if (char_id == MAP19_WAVE_ONE_CHAR_ID
                   && level == MAP19_WAVE_ONE_LEVEL) {
            seen20_wave_one_units++;
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen20_marked_timers[slot] = 0;
        seen20_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The marker sits at index 64.  A run that opened a member which never
       switched the map back would leave the array shorter than that, and
       reading past it to say so would be reading memory the allocation does
       not cover -- so the timers are left at zero and the case below fails on
       the marker value instead. */
    if (data_fdps_map_unit_count > SCRIPT_CH20_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH20_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen20_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 20 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The eleven members the
   party has when the chapter opens are staged in the order the game joins
   them: the ten the chapter 16 run puts on, then 蘭斯洛特, whom chapter 19's
   own handler added and whom this handler does not add again. */
static void run_chapter_20_handler(void)
{
    int member;

    if (run20_state != 0) {
        return;
    }
    run20_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_20_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_20; member++) {
        fdps_roster_add_character(chapter_20_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_20_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_20();
    free_chapter_globals();
    run20_state = 1;
}

/* Nobody joins, and for the first time in the game every one of ELEVEN player
   slots has a member behind it.  The roster count is still eleven, which is
   what says the handler ran no fdps_roster_add_character of its own; MAP19.DAT
   asks for eleven slots and fifty-five deployments, both read back because
   both were staged at numbers no map carries; and the eleven map units are the
   roster in join order, every one on the player side with a clear flags byte,
   which says no slot fell through to the zeroed, retired spare
   fdps_build_map_unit_array writes for a player slot with no roster member
   behind it.  Slot 10 is 蘭斯洛特: chapter 19 added him to a roster its own
   map asked only ten slots of, and this is the chapter he first stands on the
   board in. */
static void nobody_joins_and_lancelot_is_a_map_unit_in_chapter_twenty(void)
{
    int slot;

    run_chapter_20_handler();
    CHECK_EQ(run20_state, 1);
    if (run20_state != 1) {
        return;
    }

    CHECK_EQ(seen20_roster_count, PARTY_AT_CHAPTER_20);
    CHECK_EQ(seen20_player_slots, MAP19_PLAYER_SLOTS);
    CHECK_EQ(seen20_char_spawns, MAP19_CHAR_SPAWNS);

    for (slot = 0; slot < PARTY_AT_CHAPTER_20; slot++) {
        CHECK_EQ(seen20_party_char_ids[slot], chapter_20_party[slot]);
        CHECK_EQ(seen20_party_sides[slot], PLAYER_SIDE);
        CHECK_EQ(seen20_party_flags[slot], 0);
    }

    CHECK_EQ(seen20_party_char_ids[LANCELOT_ROSTER_SLOT], LANCELOT_CHAR_ID);
}

/* What the handler leaves on the map: the eleven player slots and MAP19.DAT's
   fifty-four wave-0 records, sixty-five units.  The census behind those
   fifty-four is the strategy guide's 敵方 list for the chapter found by
   character id and level -- twenty-three LV17 蛇魔使, thirteen LV18 野蠻戰士,
   ten LV17 狼人戰士 and eight LV28 冰魔導士 -- and all fifty-four are on the
   enemy side, with no guest at all.

   The count that says none of the chapter's turn events has fired is MAP19.DAT
   record 40, its only wave-1 record: a level-17 character 67, an id no unit of
   the opening board carries. */
static void chapter_twenty_opens_with_the_guides_enemy_group(void)
{
    run_chapter_20_handler();
    CHECK_EQ(run20_state, 1);
    if (run20_state != 1) {
        return;
    }

    CHECK_EQ(seen20_unit_count, CH20_UNITS_AFTER_SCRIPT);

    CHECK_EQ(seen20_snakes, MAP19_SNAKE_COUNT);
    CHECK_EQ(seen20_barbarians, MAP19_BARBARIAN_COUNT);
    CHECK_EQ(seen20_wolves, MAP19_WOLF_COUNT);
    CHECK_EQ(seen20_ice_mages, MAP19_ICE_MAGE_COUNT);
    CHECK_EQ(seen20_enemy_side_units, CH20_ENEMY_SIDE_UNITS);
    CHECK_EQ(seen20_guest_side_units, CH20_GUEST_SIDE_UNITS);

    CHECK_EQ(seen20_wave_one_units, 0);
}

/* The cut-scene's stage is gone when the handler returns.  ICON19.DAT switches
   the chapter to 58, retires and revives actors of that map, deploys its
   wave-1 fourth actor and retires that too -- and then switches back to 19,
   which rebuilds the board from MAP19.DAT and leaves nothing of the stage
   behind.  Character 123 is on MAP58.DAT and on no map the party fights on, so
   a single one of it in the array would be a stage the second rebuild failed
   to take away.  The chapter id reading 19 is the second SWITCH_MAP putting it
   back: the first one wrote 58 over it, and the title card the handler drew
   afterwards is chosen from that global by its callee. */
static void the_chapter_20_cutscene_stage_is_gone_when_it_returns(void)
{
    run_chapter_20_handler();
    CHECK_EQ(run20_state, 1);
    if (run20_state != 1) {
        return;
    }

    CHECK_EQ(seen20_scene_only_units, 0);
    CHECK_EQ(seen20_chapter_id, CHAPTER_20_ID);
}

/* The cut-scene the handler names is Icon19.dat and not the neighbour on
   either side of it.  The marker sits on unit 64 with the value only
   ICON19.DAT writes, and it is written after that member's closing SWITCH_MAP:
   the neighbour one early is chapter 19's own member, which switches no map
   and marks unit 53, and the neighbour one late is chapter 21's ICON20.DAT,
   which switches no map either and marks unit 48 with a value of its own.
   Unit 0's own timers are read back clear as well: the container's two end
   decoys are the only members that mark unit 0 at all, and neither of them is
   a neighbour of this one.  Every other timer on the marked unit is clear too:
   neither the handler nor the shipped cut-scene writes a status on anybody who
   is still on the board when it returns. */
static void the_chapter_20_cutscene_is_icon19_dat(void)
{
    int slot;

    run_chapter_20_handler();
    CHECK_EQ(run20_state, 1);
    if (run20_state != 1) {
        return;
    }

    CHECK_EQ(seen20_marked_timers[SCRIPT_CH20_MARKER_SLOT],
             SCRIPT_CH20_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH20_MARKER_SLOT) {
            CHECK_EQ(seen20_marked_timers[slot], 0);
        }
        CHECK_EQ(seen20_unit0_timers[slot], 0);
    }
}

/* The cursor ends on unit 0's tile, and here that is the tile MAP19.COD gave
   player slot 0 -- (5, 4), record 55 -- because the cut-scene's closing
   SWITCH_MAP rebuilt the board after every walk it made -- the last walk is at
   script offset 179 and the opcode is at 191.  The four handlers above all end
   on a tile their script walked to, because none of their scripts switches
   map.  The globals are that tile scaled by the 24-pixel step; a handler that skipped
   the cursor call would leave them at (0, 0), which is what the rebuild zeroes
   them to. */
static void the_chapter_20_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_20_handler();
    CHECK_EQ(run20_state, 1);
    if (run20_state != 1) {
        return;
    }

    CHECK_EQ(seen20_cursor_x, CH20_CURSOR_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen20_cursor_y, CH20_CURSOR_TILE_Y * CURSOR_TILE_STEP);
}


/* --- fdps_chapter_21_init @ 000213d0 ------------------------------------ */

/* WHAT THE HANDLER IS.  The plain four calls again, with no roster add in
   front of them and two operands of its own: the cut-scene is Icon20.dat and
   the cursor goes to unit 0.  Expected values come from the assembly at
   000213d0 and from the shipped data:

     CALL 0x00022750                   the chapter state is rebuilt
     MOV EAX,0x618f4 / PUSH EAX /
       CALL 0x00021650                 the cut-scene "Icon20.dat" is run
     CALL 0x00020c60                   the title card is shown
     PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0

   WHY THIS ONE IS NOT ANOTHER COPY OF CHAPTER 20.  Its cut-scene is the other
   kind: ICON20.DAT switches no map, so the chapter id it returns with is the
   one it was entered with rather than one the script put back, and the tile
   the cursor lands on is the one the script's own walks reached and not
   MAP20.COD's.  Those two tiles are one square apart here -- the script's
   third walk passes through the map's tile and its fourth walk is the whole
   difference -- so the cursor case is what says every walk of the member ran.
   The board is the map's alone for the opposite reason to chapter 20's: not
   because a closing rebuild threw the script's work away, but because the
   script never deploys, retires or revives anybody.  The cases below assert
   all three. */

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run21_state = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order: the same eleven chapter 20 was fought with. */
static int chapter_21_party[PARTY_AT_CHAPTER_21] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID, MARIAN_CHAR_ID, LANCELOT_CHAR_ID
};

/* Everything the chapter 21 cases assert, captured the instant the handler
   returned. */
static int seen21_roster_count;
static int seen21_player_slots;
static int seen21_char_spawns;
static int seen21_unit_count;
static int seen21_party_char_ids[PARTY_AT_CHAPTER_21];
static int seen21_party_sides[PARTY_AT_CHAPTER_21];
static int seen21_party_flags[PARTY_AT_CHAPTER_21];
static int seen21_dark_priests;
static int seen21_ice_mages;
static int seen21_berserkers;
static int seen21_wolves;
static int seen21_snakes;
static int seen21_reinforcement_units;
static int seen21_enemy_side_units;
static int seen21_guest_side_units;
static unsigned char seen21_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen21_unit0_timers[STATUS_TIMER_COUNT];
static int seen21_cursor_x;
static int seen21_cursor_y;
static int seen21_chapter_id;

static void capture_chapter_21(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    int unit_index;
    int char_id;
    int level;
    int slot;

    seen21_roster_count = data_fdps_roster_member_count;
    seen21_player_slots = data_fdps_map_player_slot_count;
    seen21_char_spawns = data_fdps_map_char_spawn_count;
    seen21_unit_count = data_fdps_map_unit_count;
    seen21_cursor_x = data_fdps_map_cursor_world_x;
    seen21_cursor_y = data_fdps_map_cursor_world_y;
    seen21_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_21; slot++) {
        seen21_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen21_party_sides[slot] = (int) unit0[slot].side;
        seen21_party_flags[slot] = (int) unit0[slot].flags;
    }

    seen21_dark_priests = 0;
    seen21_ice_mages = 0;
    seen21_berserkers = 0;
    seen21_wolves = 0;
    seen21_snakes = 0;
    seen21_reinforcement_units = 0;
    seen21_enemy_side_units = 0;
    seen21_guest_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_21;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen21_enemy_side_units++;
        } else if (unit0[unit_index].side == GUEST_SIDE) {
            seen21_guest_side_units++;
        }
        if (char_id == MAP20_KNIGHT_CHAR_ID
            || char_id == MAP20_GUARD_CHAR_ID
            || char_id == MAP20_ARCHER_CHAR_ID
            || char_id == MAP20_DARK_MAGE_CHAR_ID
            || char_id == MAP20_MONK_CHAR_ID) {
            seen21_reinforcement_units++;
        }
        if (char_id == MAP20_DARK_PRIEST_CHAR_ID
            && level == MAP20_DARK_PRIEST_LEVEL) {
            seen21_dark_priests++;
        } else if (char_id == MAP20_ICE_MAGE_CHAR_ID
                   && level == MAP20_ICE_MAGE_LEVEL) {
            seen21_ice_mages++;
        } else if (char_id == MAP20_BERSERKER_CHAR_ID
                   && level == MAP20_BERSERKER_LEVEL) {
            seen21_berserkers++;
        } else if (char_id == MAP20_WOLF_CHAR_ID
                   && level == MAP20_WOLF_LEVEL) {
            seen21_wolves++;
        } else if (char_id == MAP20_SNAKE_CHAR_ID
                   && level == MAP20_SNAKE_LEVEL) {
            seen21_snakes++;
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen21_marked_timers[slot] = 0;
        seen21_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The marker sits at index 48, the last of the forty-nine.  A run that
       opened a member which left a shorter array would not have that index,
       and reading past it to say so would be reading memory the allocation
       does not cover -- so the timers are left at zero and the case below
       fails on the marker value instead. */
    if (data_fdps_map_unit_count > SCRIPT_CH21_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH21_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen21_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 21 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The eleven members the
   party has when the chapter opens are staged in the order the game joins
   them, which is the order the chapter 20 run stages them in: neither
   chapter's handler adds anybody. */
static void run_chapter_21_handler(void)
{
    int member;

    if (run21_state != 0) {
        return;
    }
    run21_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_21_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_21; member++) {
        fdps_roster_add_character(chapter_21_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_21_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_21();
    free_chapter_globals();
    run21_state = 1;
}

/* Nobody joins, and all eleven player slots have a member behind them.  The
   roster count is still eleven, which is what says the handler ran no
   fdps_roster_add_character of its own; MAP20.DAT asks for eleven slots and
   seventy deployments, both read back because both were staged at numbers no
   map carries; and the eleven map units are the roster in join order, every
   one on the player side with a clear flags byte, which says no slot fell
   through to the zeroed, retired spare fdps_build_map_unit_array writes for a
   player slot with no roster member behind it.  法蓮娜 is slot 3 and is still
   on the board: the guide has her leave the party after this chapter's
   three-battle run, and nothing in this handler is what takes her off. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_twenty_one(void)
{
    int slot;

    run_chapter_21_handler();
    CHECK_EQ(run21_state, 1);
    if (run21_state != 1) {
        return;
    }

    CHECK_EQ(seen21_roster_count, PARTY_AT_CHAPTER_21);
    CHECK_EQ(seen21_player_slots, MAP20_PLAYER_SLOTS);
    CHECK_EQ(seen21_char_spawns, MAP20_CHAR_SPAWNS);

    for (slot = 0; slot < PARTY_AT_CHAPTER_21; slot++) {
        CHECK_EQ(seen21_party_char_ids[slot], chapter_21_party[slot]);
        CHECK_EQ(seen21_party_sides[slot], PLAYER_SIDE);
        CHECK_EQ(seen21_party_flags[slot], 0);
    }
}

/* What the handler leaves on the map: the eleven player slots and MAP20.DAT's
   thirty-eight wave-0 records, forty-nine units.  The census behind those
   thirty-eight is the first five lines of the strategy guide's 敵方 list for
   the chapter found by character id and level -- one LV17 黑暗祭司, five LV28
   冰魔導士, three LV16 狂戰士, seven LV18 狼人戰士 and twenty-two LV18 蛇魔使
   -- and all thirty-eight are on the enemy side, with no guest at all.

   The count that says neither reinforcement wave has fired is the five
   character ids MAP20.DAT uses on waves 1 and 2 and nowhere else: 騎士, 衛兵,
   弓箭手, 武鬥家 and 暗魔導士.  Both waves are tile triggers, so a single unit
   carrying one of those ids would be a wave that came in before the player
   moved. */
static void chapter_twenty_one_opens_with_the_guides_enemy_group(void)
{
    run_chapter_21_handler();
    CHECK_EQ(run21_state, 1);
    if (run21_state != 1) {
        return;
    }

    CHECK_EQ(seen21_unit_count, CH21_UNITS_AFTER_SCRIPT);

    CHECK_EQ(seen21_dark_priests, MAP20_DARK_PRIEST_COUNT);
    CHECK_EQ(seen21_ice_mages, MAP20_ICE_MAGE_COUNT);
    CHECK_EQ(seen21_berserkers, MAP20_BERSERKER_COUNT);
    CHECK_EQ(seen21_wolves, MAP20_WOLF_COUNT);
    CHECK_EQ(seen21_snakes, MAP20_SNAKE_COUNT);
    CHECK_EQ(seen21_enemy_side_units, CH21_ENEMY_SIDE_UNITS);
    CHECK_EQ(seen21_guest_side_units, CH21_GUEST_SIDE_UNITS);

    CHECK_EQ(seen21_reinforcement_units, 0);
}

/* The cut-scene the handler names is Icon20.dat and not the neighbour on
   either side of it.  The marker sits on unit 48 with the value only
   ICON20.DAT writes: the neighbour one early is chapter 20's own member, which
   switches the map to 58 and back to 19 and marks unit 64, and the neighbour
   one late is ICON21.DAT, chapter 22's own member, which marks unit 11 with a
   value of its own and retires unit 0.  Either neighbour therefore fails this
   case on the marker, and ICON21.DAT fails the roster case above on unit 0's
   flags byte as well.  Unit 0's own timers are read back clear, which is where
   the container's two end decoys write: no member the chapter 21 board can
   reach puts a status on it.  Every other timer on the marked unit is clear
   too: neither the handler nor the shipped cut-scene writes a status on
   anybody.

   The chapter id still reading 20 is the other half of the same question.
   ICON20.DAT carries no SWITCH_MAP, so nothing in the run touches the global
   the dispatcher set -- where a run that opened chapter 20's member would come
   back with 19 in it, that member's own closing SWITCH_MAP having written
   chapter 20's number there. */
static void the_chapter_21_cutscene_is_icon20_dat(void)
{
    int slot;

    run_chapter_21_handler();
    CHECK_EQ(run21_state, 1);
    if (run21_state != 1) {
        return;
    }

    CHECK_EQ(seen21_marked_timers[SCRIPT_CH21_MARKER_SLOT],
             SCRIPT_CH21_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH21_MARKER_SLOT) {
            CHECK_EQ(seen21_marked_timers[slot], 0);
        }
        CHECK_EQ(seen21_unit0_timers[slot], 0);
    }

    CHECK_EQ(seen21_chapter_id, CHAPTER_21_ID);
}

/* The cursor ends on unit 0's tile, and here that is the tile the cut-scene
   walked it to -- (16, 20) -- because ICON20.DAT switches no map and nothing
   rebuilds the board behind its walks.  MAP20.COD's own player-slot-0 start
   tile is (17, 20), record 70, one square to the right, so a handler that
   deployed the board and never opened the script would land one square away.
   What this case separates against the fixture, which reaches (16, 20) with a
   single PLACE_UNIT and no walks at all, is a handler that made the cursor
   call from one that did not, and Icon20.dat from the ICON19.DAT neighbour --
   it cannot tell a full run from one that dropped the last walk, because the
   fixture does not walk.  The globals are that tile scaled by the 24-pixel
   step; a handler that skipped the cursor call would leave them at (0, 0),
   which is what the chapter state reset zeroes them to. */
static void the_chapter_21_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_21_handler();
    CHECK_EQ(run21_state, 1);
    if (run21_state != 1) {
        return;
    }

    CHECK_EQ(seen21_cursor_x, CH21_CURSOR_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen21_cursor_y, CH21_CURSOR_TILE_Y * CURSOR_TILE_STEP);
}


/* --- fdps_chapter_22_init @ 00021410 ------------------------------------ */

/* WHAT THE HANDLER IS.  The plain four calls again, with no roster add in
   front of them and two operands of its own: the cut-scene is Icon21.dat and
   the cursor goes to unit 3.  Expected values come from the assembly at
   00021410 and from the shipped data:

     CALL 0x00022750                   the chapter state is rebuilt
     MOV EAX,0x61900 / PUSH EAX /
       CALL 0x00021650                 the cut-scene "Icon21.dat" is run
     CALL 0x00020c60                   the title card is shown
     PUSH 0x3 / CALL 0x0002da50        the cursor is parked on unit 3

   WHY THIS ONE IS NOT ANOTHER COPY OF CHAPTER 21.  Two things separate it.
   The cursor operand is 3 and not 0, which is the chapter 17 case again: the
   guide's 失敗條件 for this chapter is 法蓮娜死亡 and she is roster slot 3.
   And the cut-scene takes a player slot off the board -- its single
   RETIRE_UNIT names map unit 0, 蘭迪斯 -- where chapter 21's member touches no
   unit state at all.  The board underneath is the thinnest of the family:
   MAP21.DAT tags one single record wave 0, so the twelve units the handler
   returns on are the eleven player slots and the boss.  The cases below assert
   all three. */

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run22_state = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order: the same eleven chapters 20 and 21 were fought with. */
static int chapter_22_party[PARTY_AT_CHAPTER_22] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID, MARIAN_CHAR_ID, LANCELOT_CHAR_ID
};

/* Everything the chapter 22 cases assert, captured the instant the handler
   returned. */
static int seen22_roster_count;
static int seen22_player_slots;
static int seen22_char_spawns;
static int seen22_unit_count;
static int seen22_party_char_ids[PARTY_AT_CHAPTER_22];
static int seen22_party_sides[PARTY_AT_CHAPTER_22];
static int seen22_party_flags[PARTY_AT_CHAPTER_22];
static int seen22_bosses;
static int seen22_reinforcement_units;
static int seen22_enemy_side_units;
static int seen22_guest_side_units;
static unsigned char seen22_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen22_unit0_timers[STATUS_TIMER_COUNT];
static int seen22_cursor_x;
static int seen22_cursor_y;
static int seen22_chapter_id;

static void capture_chapter_22(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    int unit_index;
    int char_id;
    int level;
    int slot;

    seen22_roster_count = data_fdps_roster_member_count;
    seen22_player_slots = data_fdps_map_player_slot_count;
    seen22_char_spawns = data_fdps_map_char_spawn_count;
    seen22_unit_count = data_fdps_map_unit_count;
    seen22_cursor_x = data_fdps_map_cursor_world_x;
    seen22_cursor_y = data_fdps_map_cursor_world_y;
    seen22_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_22; slot++) {
        seen22_party_char_ids[slot] = (int) unit0[slot].char_id;
        seen22_party_sides[slot] = (int) unit0[slot].side;
        seen22_party_flags[slot] = (int) unit0[slot].flags;
    }

    seen22_bosses = 0;
    seen22_reinforcement_units = 0;
    seen22_enemy_side_units = 0;
    seen22_guest_side_units = 0;
    for (unit_index = PARTY_AT_CHAPTER_22;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        level = (int) unit0[unit_index].level;
        if (unit0[unit_index].side == ENEMY_SIDE) {
            seen22_enemy_side_units++;
        } else if (unit0[unit_index].side == GUEST_SIDE) {
            seen22_guest_side_units++;
        }
        if (char_id == MAP21_WOLF_CHAR_ID
            || char_id == MAP21_SNAKE_CHAR_ID
            || char_id == MAP21_GHOST_CHAR_ID
            || char_id == MAP21_SKELETON_CHAR_ID) {
            seen22_reinforcement_units++;
        }
        if (char_id == MAP21_BOSS_CHAR_ID && level == MAP21_BOSS_LEVEL) {
            seen22_bosses++;
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen22_marked_timers[slot] = 0;
        seen22_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The marker sits at index 11, the last of the twelve.  A run that opened
       a member which left a shorter array would not have that index, and
       reading past it to say so would be reading memory the allocation does
       not cover -- so the timers are left at zero and the case below fails on
       the marker value instead. */
    if (data_fdps_map_unit_count > SCRIPT_CH22_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH22_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen22_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 22 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The eleven members the
   party has when the chapter opens are staged in the order the game joins
   them, which is the order the chapter 21 run stages them in: neither
   chapter's handler adds anybody. */
static void run_chapter_22_handler(void)
{
    int member;

    if (run22_state != 0) {
        return;
    }
    run22_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_22_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_22; member++) {
        fdps_roster_add_character(chapter_22_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_22_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_22();
    free_chapter_globals();
    run22_state = 1;
}

/* Nobody joins, and all eleven player slots have a member behind them.  The
   roster count is still eleven, which is what says the handler ran no
   fdps_roster_add_character of its own; MAP21.DAT asks for eleven slots and
   fifty-five deployments, both read back because both were staged at numbers
   no map carries; and the eleven map units are the roster in join order, every
   one of them on the player side, which says no slot fell through to the
   zeroed, retired spare fdps_build_map_unit_array writes for a player slot
   with no roster member behind it. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_twenty_two(void)
{
    int slot;

    run_chapter_22_handler();
    CHECK_EQ(run22_state, 1);
    if (run22_state != 1) {
        return;
    }

    CHECK_EQ(seen22_roster_count, PARTY_AT_CHAPTER_22);
    CHECK_EQ(seen22_player_slots, MAP21_PLAYER_SLOTS);
    CHECK_EQ(seen22_char_spawns, MAP21_CHAR_SPAWNS);

    for (slot = 0; slot < PARTY_AT_CHAPTER_22; slot++) {
        CHECK_EQ(seen22_party_char_ids[slot], chapter_22_party[slot]);
        CHECK_EQ(seen22_party_sides[slot], PLAYER_SIDE);
    }
}

/* The chapter is fought without 蘭迪斯, and it is the cut-scene that takes him
   off.  Map unit 0 carries the retired bit when the handler returns -- the one
   RETIRE the shipped ICON21.DAT runs, with no REVIVE behind it -- and the
   other ten player slots carry a clear flags byte.  Read through the join
   order that is the strategy guide's 己方 line for the chapter,
   蘭迪斯以外的所有人.  Only a run that opened this member can produce it:
   neither of the two end decoys retires anybody, and neither does the
   ICON20.DAT neighbour. */
static void chapter_twenty_two_is_fought_without_randis(void)
{
    int slot;

    run_chapter_22_handler();
    CHECK_EQ(run22_state, 1);
    if (run22_state != 1) {
        return;
    }

    CHECK_EQ(seen22_party_char_ids[SCRIPT_CH22_RETIRED_UNIT], RANDIS_CHAR_ID);
    CHECK_EQ(seen22_party_flags[SCRIPT_CH22_RETIRED_UNIT], UNIT_FLAG_RETIRED);

    for (slot = CH22_RETIRED_SLOTS; slot < PARTY_AT_CHAPTER_22; slot++) {
        CHECK_EQ(seen22_party_flags[slot], 0);
    }
}

/* What the handler leaves on the map: the eleven player slots and MAP21.DAT's
   single wave-0 record, twelve units.  That one record is the guide's LV20
   巫湯婆婆 found by character id and level, it is on the enemy side, and there
   is no guest at all.

   The count that says none of the five reinforcement waves has fired is the
   four character ids MAP21.DAT uses on waves 1 to 5 and nowhere else: 狼人戰士,
   蛇魔使, 幽魂 and 骷髏兵.  The guide has every one of those waves arriving on
   a player turn, so a single unit carrying one of those ids would be a wave
   that came in before the player moved. */
static void chapter_twenty_two_opens_with_the_guides_boss_alone(void)
{
    run_chapter_22_handler();
    CHECK_EQ(run22_state, 1);
    if (run22_state != 1) {
        return;
    }

    CHECK_EQ(seen22_unit_count, CH22_UNITS_AFTER_SCRIPT);

    CHECK_EQ(seen22_bosses, MAP21_BOSS_COUNT);
    CHECK_EQ(seen22_enemy_side_units, CH22_ENEMY_SIDE_UNITS);
    CHECK_EQ(seen22_guest_side_units, CH22_GUEST_SIDE_UNITS);

    CHECK_EQ(seen22_reinforcement_units, 0);
}

/* The cut-scene the handler names is Icon21.dat and not the neighbour on
   either side of it.  The marker sits on unit 11 with the value only
   ICON21.DAT writes: the neighbour one early is chapter 21's own member, which
   marks unit 48 -- an index this twelve-unit board does not have -- and the
   neighbour one late is the ICON22.DAT decoy, which marks unit 0.  Unit 0's
   own timers are read back clear, which is where that decoy's marker would
   have landed.  Every other timer on the marked unit is clear too: neither the
   handler nor the shipped cut-scene writes a status on anybody.

   The chapter id still reading 21 is the other half of the same question.
   ICON21.DAT carries no SWITCH_MAP, so nothing in the run touches the global
   the dispatcher set -- where a run that opened chapter 20's member would come
   back with 19 in it, that member's own closing SWITCH_MAP having written
   chapter 20's number there. */
static void the_chapter_22_cutscene_is_icon21_dat(void)
{
    int slot;

    run_chapter_22_handler();
    CHECK_EQ(run22_state, 1);
    if (run22_state != 1) {
        return;
    }

    CHECK_EQ(seen22_marked_timers[SCRIPT_CH22_MARKER_SLOT],
             SCRIPT_CH22_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH22_MARKER_SLOT) {
            CHECK_EQ(seen22_marked_timers[slot], 0);
        }
        CHECK_EQ(seen22_unit0_timers[slot], 0);
    }

    CHECK_EQ(seen22_chapter_id, CHAPTER_22_ID);
}

/* The cursor ends on unit 3's tile and not unit 0's, and that tile is the one
   the cut-scene walked it to -- (6, 19) -- because ICON21.DAT switches no map
   and nothing rebuilds the board behind its walks.  MAP21.COD's own
   player-slot-3 start tile is (2, 24), record 58, four tiles away, so a
   handler that deployed the board and never opened the script would land
   there.  The globals are the tile scaled by the 24-pixel step; a handler that
   skipped the cursor call would leave them at (0, 0), which is what the
   chapter state reset zeroes them to. */
static void the_chapter_22_cursor_is_parked_on_unit_three(void)
{
    run_chapter_22_handler();
    CHECK_EQ(run22_state, 1);
    if (run22_state != 1) {
        return;
    }

    CHECK_EQ(seen22_cursor_x, CH22_CURSOR_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen22_cursor_y, CH22_CURSOR_TILE_Y * CURSOR_TILE_STEP);
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

void run_chinit2_tests(void)
{
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_sixteen);
    RUN_TEST(chapter_sixteen_is_fought_with_half_the_party);
    RUN_TEST(chapter_sixteen_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_16_cutscene_is_icon15_dat);
    RUN_TEST(the_chapter_16_cursor_is_parked_on_unit_zero);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_seventeen);
    RUN_TEST(chapter_seventeen_is_fought_with_the_other_half);
    RUN_TEST(chapter_seventeen_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_17_cutscene_is_icon16_dat);
    RUN_TEST(the_chapter_17_cursor_is_parked_on_unit_three);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_eighteen);
    RUN_TEST(chapter_eighteen_is_fought_with_the_whole_party);
    RUN_TEST(chapter_eighteen_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_18_cutscene_is_icon17_dat);
    RUN_TEST(the_chapter_18_cursor_is_parked_on_unit_zero);
    RUN_TEST(lancelot_joins_the_roster_when_chapter_nineteen_opens);
    RUN_TEST(lancelot_is_not_one_of_the_chapter_nineteen_map_units);
    RUN_TEST(chapter_nineteen_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_19_cutscene_is_icon18_dat);
    RUN_TEST(the_chapter_19_cursor_is_parked_on_unit_zero);
    RUN_TEST(nobody_joins_and_lancelot_is_a_map_unit_in_chapter_twenty);
    RUN_TEST(chapter_twenty_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_20_cutscene_stage_is_gone_when_it_returns);
    RUN_TEST(the_chapter_20_cutscene_is_icon19_dat);
    RUN_TEST(the_chapter_20_cursor_is_parked_on_unit_zero);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_twenty_one);
    RUN_TEST(chapter_twenty_one_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_21_cutscene_is_icon20_dat);
    RUN_TEST(the_chapter_21_cursor_is_parked_on_unit_zero);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_twenty_two);
    RUN_TEST(chapter_twenty_two_is_fought_without_randis);
    RUN_TEST(chapter_twenty_two_opens_with_the_guides_boss_alone);
    RUN_TEST(the_chapter_22_cutscene_is_icon21_dat);
    RUN_TEST(the_chapter_22_cursor_is_parked_on_unit_three);
    RUN_TEST(the_fixture_container_is_removed);
}
