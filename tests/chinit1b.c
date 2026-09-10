/* tests/chinit1b.c -- cover for src/chinit1b.c.
 *
 * One entry handler per section, in address order: fdps_chapter_11_init at
 * 00021140, fdps_chapter_12_init at 00021180, fdps_chapter_13_init at
 * 000211c0 and fdps_chapter_14_init at 00021200.
 *
 * WHAT THE HANDLER IS.  fdps_chapter_11_init at 00021140 is five calls with no
 * branch, no loop and no store anywhere in it, so nothing about it is worth
 * testing in pieces: what it decides is WHICH character joins, WHICH cut-scene
 * member is opened, and the ORDER the five calls run in.  The run below
 * therefore enters chapter 11 once for real against the shipped containers and
 * reads the answers off the state it leaves, and every case asserts against
 * that one run.
 *
 * Expected values come from the assembly at 00021140 and from the shipped
 * data, never from the emitted C:
 *
 *   PUSH 0x7 / CALL 0x00023bc0        character 7 joins the roster
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x6187c / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon10.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * WHY THE ORDER OF THE FIRST TWO CALLS IS THE POINT OF THIS FILE.  MAP10.DAT
 * declares NINE player slots and the party is eight members when the chapter
 * opens -- chapters 1 to 4 one each, chapter 7 裘娜, chapter 8 費塔加,
 * chapter 9 布蘭多 and 蓋亞, chapter 10 nobody -- so player slot 8 exists for
 * 琴琴 and for nobody else.  fdps_build_map_unit_array fills a player slot
 * from the roster only while the slot index is below the roster count and
 * writes a zeroed, retired spare otherwise (src/deploy.c), so with the reset
 * run first slot 8 would be that spare.  The case below reads map unit 8 back
 * as a live player-side unit carrying her roster record, which is the
 * assertion that the add ran first.
 *
 * WHY THE CUT-SCENE IS A FIXTURE AND NOT THE SHIPPED ONE.  Same reason
 * tests/chinit1.c gives for its ten runs: the shipped ICON10.DAT walks and
 * poses actors, plays a .saf clip and draws chapter text through a pointer a
 * test image has not filled.  The staged ICON10.DAT keeps the shipped member's
 * three DEPLOY_WAVEs -- waves 1, 2 and 3, in that order, each with the
 * place-exact operand 0, which is what the shipped bytes carry when walked
 * with the opcode ladder in src/icon.c -- and adds a marker so the run can say
 * which member the interpreter opened.
 *
 * WHY EVERY NEIGHBOURING MEMBER IS STAGED TOO.  Icon10.dat is chapter
 * ELEVEN's script and Icon11.dat chapter TWELVE's -- the number in the name is
 * the 0-based chapter id -- so the mistake this file has to be able to catch is
 * a handler naming the member one either side of its own.  A container holding
 * only the members under test would turn that mistake into a member the
 * interpreter cannot find, and a member it cannot find sends the interpreter
 * into fdps_wait_any_key, which spins for a keyboard interrupt that never
 * comes.  So the six members ICON09.DAT to ICON14.DAT are all staged: the four
 * in the middle are the real fixtures the four handlers name, and the two on
 * the outside are decoys that deploy nobody and mark unit 0 with a value of
 * their own, so a run that opened one lands a unit array of the wrong length
 * AND a marked unit 0.
 *
 * WHAT THE CHAPTER 12 SECTION ADDS.  Its handler is the plain four-call form
 * with no roster add at all, and its cut-scene is the one script in the game
 * that asks the player a question: ICON11.DAT ends on a DEPLOY_WAVE whose wave
 * operand is 0xff, the form that takes the wave from the answer ASK_THREE_WAY
 * left behind (icon.h), and MAP11.DAT's three records are keyed wave 1, 2 and
 * 3 -- the guide's three rooms of 火神的宮殿.  The fixture cannot put that
 * question to a player, so it names one of the three waves outright and the
 * section pins the room that answer opens; which of the three the answer picks
 * is fdps_icon_script_prompt_three_way_choice's business and tests/icon.c's.
 *
 * WHAT IS STAGED AND WHAT IS REAL.  The four data tables and the roster block
 * are staged here, because they are ticket 23 symbols the build links
 * zero-filled; the character rows are FRIAPRDA.DAT's and FRILEVUP.DAT's own
 * numbers as recorded in assets/characters.md.  Everything the chapter load
 * reads is the real container: each chapter's own MAP%02d.DAT counts and wave
 * tags, its MAP%02d.COD placement records, the tile layers, ICON.CEL's
 * sprites, MISC.VFS's title card.  Nothing below asserts what any global held
 * before the run.
 *
 * The staging REFUSES TO OVERWRITE an IconAni.vfs that is already in the run
 * directory, and removes its own again in the last case: tests/chinit1.c and
 * tests/icon.c stage a container of the same name for their own fixtures and
 * each skips itself if one is already there.
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
#include "chinit1b.h"

/* The shipped containers the run needs, and the sheet reader's own floor:
   fdps_cache_cel_sprite_group takes a fixed 0x2970-byte bite out of ICON.CEL's
   offset table, so a shorter file is one it runs off the end of.  Same guard
   tests/chinit1.c, tests/chapter.c and tests/deploy.c use. */
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
#define FIXTURE_MEMBERS 6

/* Chapter 11 is chapter id 10: the entry-handler table slot number is the
   0-based id, and this handler is slot 10 of the table at 00060074 -- the
   dword at 0006009c is 00021140. */
#define CHAPTER_11_ID 10

/* The character PUSH 0x7 at 0002114c puts on the roster: 琴琴 the 武道家,
   character id 7 (assets/characters.md). */
#define QINQIN_CHAR_ID 7

/* The roster slot she lands in and the map unit she becomes.  The run below
   puts the chapter's eight sitting members on with the game's own add first,
   so the handler's own add is the ninth, and MAP10.DAT's ninth player slot is
   the one it fills. */
#define QINQIN_ROSTER_SLOT 8
#define QINQIN_UNIT 8

/* 琴琴's own line out of FRIAPRDA.DAT and FRILEVUP.DAT
   (assets/characters.md): level 15, 60 base HP, 12 base MP, 12 HP and 2 MP a
   level, so the roster record fdps_roster_add_character builds carries 228 HP
   and 40 MP.  The strategy guide's line for this chapter says the same two
   numbers -- LV15 武道家琴琴（HP228,MP40）-- so this expectation is the
   shipped data and the guide agreeing, not an arithmetic the test invented. */
#define QINQIN_LEVEL 15
#define QINQIN_HP_BASE 60
#define QINQIN_MP_BASE 12
#define QINQIN_HP_MIN 12
#define QINQIN_MP_MIN 2
#define QINQIN_HP_MAX (QINQIN_HP_BASE + QINQIN_HP_MIN * (QINQIN_LEVEL - 1))
#define QINQIN_MP_MAX (QINQIN_MP_BASE + QINQIN_MP_MIN * (QINQIN_LEVEL - 1))

/* The eight members the party has when chapter 11 opens, in join order, which
   is roster-slot order and so player-slot order.  fdps_roster_add_character is
   called from the chapter handlers alone, and the handlers the game reaches
   before this one add 蘭迪斯, 尤利安, 亞克, 法蓮娜, 裘娜, 費塔加, 布蘭多 and
   蓋亞 -- chapter 10 adds nobody.  Their own table rows go in below for the
   same reason 琴琴's do. */
#define PARTY_AT_CHAPTER_11 8

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

/* MAP10.DAT's own header bytes, read back to prove chapter 11's map is the one
   that loaded: nine player slots at +1 and 49 scripted deployments at +2.
   Both were staged at other numbers before the run. */
#define CH10_PLAYER_SLOTS 9
#define CH10_CHAR_SPAWNS 49

/* MAP10.DAT tags three of its 49 deployments wave 0 -- records 30, 31 and 32,
   all of them the same level-1 character -- so the chapter state reset's own
   opening deploy puts three units down behind the nine player slots, at
   indices 9, 10 and 11.  They are the stand-ins the shipped cut-scene retires
   again part way through. */
#define CH10_WAVE_ZERO_UNITS 3
#define CH10_FIRST_WAVE_ZERO_UNIT 9
#define CH10_STANDIN_CHAR_ID 83
#define CH10_STANDIN_LEVEL 1

/* Which waves the fixture ICON10.DAT below asks for, and what the shipped
   member asks for: waves 1, 2 and 3, each with the place-exact operand 0.  All
   four numbers are read off the shipped ICON10.DAT itself, walked with the
   opcode ladder in src/icon.c -- its three DEPLOY_WAVEs are at script offsets
   129, 142 and 155 and it carries no SWITCH_MAP at all.  MAP10.DAT tags 7, 3
   and 4 of its records with those three waves. */
#define SCRIPT_CH11_FIRST_WAVE 1
#define SCRIPT_CH11_SECOND_WAVE 2
#define SCRIPT_CH11_THIRD_WAVE 3
#define SCRIPT_CH11_PLACE_EXACT 0
#define CH10_SCRIPT_WAVE_UNITS 14

/* How long the unit array is when the handler returns: the nine player slots,
   the map's three wave-0 records, and the fourteen the cut-scene deploys. */
#define CH10_UNITS_AFTER_SCRIPT \
    (CH10_PLAYER_SLOTS + CH10_WAVE_ZERO_UNITS + CH10_SCRIPT_WAVE_UNITS)

/* The census those fourteen come to, by character id, and the strategy guide's
   opening 敵方 group for the chapter read from the other side: LV15 魔導士 x3,
   LV15 冰魔導士 x2, LV14 野武士, LV13 狼人 x2, LV13 拳士 x3 and LV13 弓兵 x3.
   Character 102 is the 魔導士 and 93 the 弓兵 the chapter 5 and 6 runs in
   tests/chinit1.c already pin by the same means.

   Every one of the six ids is above the enemy id base of 60 and below the
   TABLE_ROWS rows staged for the enemy table, so all fourteen deploy. */
#define CH10_MAGE_CHAR_ID 102
#define CH10_MAGE_COUNT 3
#define CH10_ICE_MAGE_CHAR_ID 101
#define CH10_ICE_MAGE_COUNT 2
#define CH10_RONIN_CHAR_ID 79
#define CH10_RONIN_COUNT 1
#define CH10_WOLF_CHAR_ID 82
#define CH10_WOLF_COUNT 2
#define CH10_FIST_CHAR_ID 108
#define CH10_FIST_COUNT 3
#define CH10_ARCHER_CHAR_ID 93
#define CH10_ARCHER_COUNT 3

/* The last unit the three waves leave behind, read back by index.  A wave is
   deployed in file order, so wave 3's four records -- 0, 1, 2 and then 5 --
   land at indices 22 to 25 and record 5 is the last of them: side 0,
   character 93, level 13.  It is the one index that exists only if all three
   waves went down in the shipped order. */
#define CH10_LAST_UNIT 25
#define CH10_LAST_UNIT_LEVEL 13

/* What the fixture ICON10.DAT's SET_UNIT_TIMER writes, and where.  It marks
   that same last unit, an index neither neighbouring fixture reaches, with a
   value neither of them writes.  The slot is status_timers[4], record 0x26,
   for the reason tests/chinit1.c gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1. */
#define SCRIPT_CH11_MARKER_UNIT CH10_LAST_UNIT
#define SCRIPT_CH11_MARKER_OPERAND 1
#define SCRIPT_CH11_MARKER_SLOT 4
#define SCRIPT_CH11_MARKER_VALUE 57

/* What the two decoy members write.  Each marks unit 0 -- a unit that exists on
   any run at all -- with a value of its own and deploys nobody.  ICON09.DAT is
   chapter 10's script, the neighbour one chapter early of the chapter 11 run;
   ICON14.DAT is chapter 15's, the neighbour one chapter late of the chapter 14
   run.  The four members between them are real fixtures, because each of them
   is the script one of the four handlers under test names. */
#define SCRIPT_DECOY_MARKER_UNIT 0
#define SCRIPT_DECOY_MARKER_OPERAND 1
#define SCRIPT_CH10_DECOY_VALUE 41
#define SCRIPT_CH15_DECOY_VALUE 53

/* MAP10.COD record 49 -- the first record past the map's 49 scripted ones, and
   so the first party slot's start tile -- is (10, 21).  The record a party
   slot is put on is data_fdps_map_char_spawn_count + slot_index and not the
   slot number, and a player slot is placed on its record exactly
   (src/deploy.c), so the cursor globals are that tile scaled by the 24-pixel
   step. */
#define CH10_PARTY_TILE_X 10
#define CH10_PARTY_TILE_Y 21
#define CURSOR_TILE_STEP 24

/* --- fdps_chapter_12_init @ 00021180 constants -------------------------- */

/* Chapter 12 is chapter id 11, and this handler is slot 11 of the table at
   00060074 -- the dword at 000600a0 is 00021180. */
#define CHAPTER_12_ID 11

/* The party when chapter 12 opens: the eight chapter 11 opened with, and
   琴琴, whom fdps_chapter_11_init added.  This handler adds nobody -- there is
   no PUSH/CALL 0x00023bc0 anywhere in its 0x33 bytes -- so the count the run
   reads back is the nine the run itself put on. */
#define PARTY_AT_CHAPTER_12 9

/* MAP11.DAT's own header bytes, read back to prove chapter 12's map is the one
   that loaded: nine player slots at +1 and three scripted deployments at +2.
   Both were staged at other numbers before the run.  Nine slots for a nine
   member party, so every slot has somebody behind it and no zeroed, retired
   spare is written. */
#define CH11_PLAYER_SLOTS 9
#define CH11_CHAR_SPAWNS 3

/* MAP11.DAT tags NONE of its three deployments wave 0 -- they are keyed 1, 2
   and 3 -- so the chapter state reset's own opening deploy puts nobody down and
   the player slots are the whole array until the cut-scene deploys. */
#define CH11_WAVE_ZERO_UNITS 0

/* The three rooms of 火神的宮殿, one deployment record each: characters 113,
   114 and 115, all level 20, keyed waves 1, 2 and 3 in MAP11.DAT's record
   order.  The strategy guide prints one 敵方 line and one map per room --
   左邊房間 LV20 修佩魯, 中間房間 LV20 雷德, 右邊房間 LV20 亞德尼恩 -- which
   is the same three from the other side. */
#define CH11_ROOM_1_CHAR_ID 113
#define CH11_ROOM_2_CHAR_ID 114
#define CH11_ROOM_3_CHAR_ID 115
#define CH11_ROOM_BOSS_LEVEL 20

/* Which of the three the fixture opens, and the unit index the one opponent
   lands at: the deploy appends, and the nine player slots are already down. */
#define SCRIPT_CH12_ROOM_WAVE 1
#define CH11_ROOM_UNIT 9
#define CH11_ROOM_CHAR_ID CH11_ROOM_1_CHAR_ID

/* How long the unit array is when the handler returns: the nine player slots,
   no wave-0 record at all, and the single opponent the room wave deploys. */
#define CH11_UNITS_AFTER_SCRIPT     (CH11_PLAYER_SLOTS + CH11_WAVE_ZERO_UNITS + 1)

/* What the fixture ICON11.DAT does, read off the shipped member itself, walked
   with the opcode ladder in src/icon.c: SWITCH_MAP 0x34 at script offset 4,
   DEPLOY_WAVE 1 and DEPLOY_WAVE 2 both place-nearest at offsets 15 and 25,
   SWITCH_MAP 0x28 at offset 217, DEPLOY_WAVE 1 place-exact at offset 272,
   SWITCH_MAP 0x0b at offset 792 and a last DEPLOY_WAVE at offset 802.

   That last one is the only opcode the fixture cannot copy: its wave operand is
   0xff, the form that takes the wave from the answer the ASK_THREE_WAY at
   offset 708 left behind (icon.h), and there is no player here to answer.  The
   fixture names wave 1 outright instead, so this section pins the room that
   answer opens and not the answering itself. */
#define SCRIPT_CH12_FIRST_CUTSCENE_MAP 52
#define SCRIPT_CH12_SECOND_CUTSCENE_MAP 40
#define SCRIPT_CH12_CHAPTER_MAP 11
#define SCRIPT_CH12_FIRST_WAVE 1
#define SCRIPT_CH12_SECOND_WAVE 2
/* The DEPLOY_WAVE place operand.  icon.c's DEPLOY_WAVE arm passes script[+2] to
   fdps_deploy_wave, which hands it down as fdps_deploy_unit's third parameter,
   and the test on it is CMP byte ptr [EBP + 0x1c],0x0 / JNZ 0x00023446 at
   00023360.  Non-zero takes 00023446, which copies the anchor tile straight
   through into the chosen tile -- so 1 is placement exactly on the anchor.
   Zero falls through into the scan at 0002336a, the nearest-free-walkable-tile
   search.  src/deploy.c's place_exact == 0 arm reads it the same way. */
#define SCRIPT_CH12_PLACE_NEAREST 0
#define SCRIPT_CH12_PLACE_EXACT 1

/* What the fixture ICON11.DAT's SET_UNIT_TIMER writes, and where.  It marks the
   one opponent -- unit 9, an index that exists only if the last SWITCH_MAP and
   the room deploy both ran -- with a value no other member of the container
   writes.  The slot is status_timers[4], record 0x26, for the reason
   tests/chinit1.c gives: it is the one status byte fdps_unit_select_status_icon
   does not read, so marking it cannot send fdps_draw_map_unit through the null
   status-icon sheet.  The opcode's operand is measured from status_timers[3]
   (src/icon.c), which makes that operand 1. */
#define SCRIPT_CH12_MARKER_UNIT CH11_ROOM_UNIT
#define SCRIPT_CH12_MARKER_OPERAND 1
#define SCRIPT_CH12_MARKER_SLOT 4
#define SCRIPT_CH12_MARKER_VALUE 63

/* MAP11.COD record 3 -- the first record past the map's three scripted ones,
   and so the first party slot's start tile -- is (0, 0).  The record a party
   slot is put on is data_fdps_map_char_spawn_count + slot_index and not the
   slot number, and a player slot is placed on its record exactly
   (src/deploy.c), so the cursor globals end at that tile scaled by the 24-pixel
   step.  Both were staged at other numbers before the run, so reading zero back
   is the write and not the absence of one. */
#define CH11_PARTY_TILE_X 0
#define CH11_PARTY_TILE_Y 0

/* --- fdps_chapter_13_init @ 000211c0 constants -------------------------- */

/* Chapter 13 is chapter id 12, and this handler is slot 12 of the table at
   00060074 -- the dword at 000600a4 is 000211c0. */
#define CHAPTER_13_ID 12

/* The party when chapter 13 opens: the same nine chapter 12 opened with.
   Neither chapter 12's handler nor this one has a PUSH/CALL 0x00023bc0
   anywhere in its body, so nobody has joined since 琴琴. */
#define PARTY_AT_CHAPTER_13 9

/* MAP12.DAT's own header bytes, read back to prove chapter 13's map is the one
   that loaded: nine player slots at +1 and thirty-seven scripted deployments
   at +2.  Both were staged at other numbers before the run.  Nine slots for a
   nine member party, so every slot has somebody behind it and no zeroed,
   retired spare is written. */
#define CH12_PLAYER_SLOTS 9
#define CH12_CHAR_SPAWNS 37

/* MAP12.DAT tags EVERY ONE of its thirty-seven deployments wave 0 -- the only
   map either of these two files stages that does -- so the chapter state
   reset's own opening deploy puts all thirty-seven down and the array is
   forty-six units before the cut-scene's first opcode.  ICON12.DAT then
   deploys nobody: it carries no DEPLOY_WAVE at all. */
#define CH12_WAVE_ZERO_UNITS CH12_CHAR_SPAWNS
#define CH12_UNITS_AFTER_SCRIPT (CH12_PLAYER_SLOTS + CH12_WAVE_ZERO_UNITS)

/* The census MAP12.DAT's thirty-seven records come to, by character id and
   level, and the strategy guide's 敵方 list for the chapter read from the
   other side.  Every count below matches the guide except the actor: the guide
   lists thirty-six units and the map holds thirty-seven, and the difference is
   the one the cut-scene retires.

   Which id is behind which guide line is settled by matching the guide's
   printed HP, DX and MV against that id's ENEMYDAT.DAT row -- the three
   coefficients are multiplied by the level and every one of the nine rows
   agrees exactly, while the guide's AP and DP are the equipped numbers and do
   not:

     character 68, level 20, one   -- LV20 薩達特 (HP1200, DX60, MV6)
     character 69, level 20, one   -- LV20 巴魯   (HP1300, DX40, MV6)
     character 70, level 20, one   -- LV20 席拉   (HP1000, DX80, MV6), so
       records 0, 1 and 2 are NOT the order the guide prints the three in
     character 76, level 15, nine  -- LV15 暗黑騎兵 x9
     character 94, level 11, seven -- LV11 弓箭手 x7
     character 108, level 16, seven -- LV16 拳士 x7, the same 拳士 id the
       chapter 11 section above pins at level 13
     character 89, level 13, four  -- LV13 騎士 x4 (HP338, MV7)
     character 103, level 13, four -- LV13 暗魔導士 x4 (HP260, MV4)
     character 88, level 17, two   -- LV17 騎兵 x2
     character 13, level 15, one   -- the actor, in no guide line at all */
#define CH12_BOSS_1_CHAR_ID 68
#define CH12_BOSS_2_CHAR_ID 69
#define CH12_BOSS_3_CHAR_ID 70
#define CH12_BOSS_LEVEL 20
#define CH12_BOSS_COUNT 3
#define CH12_DARK_RIDER_CHAR_ID 76
#define CH12_DARK_RIDER_LEVEL 15
#define CH12_DARK_RIDER_COUNT 9
#define CH12_ARCHER_CHAR_ID 94
#define CH12_ARCHER_LEVEL 11
#define CH12_ARCHER_COUNT 7
#define CH12_FIST_CHAR_ID 108
#define CH12_FIST_LEVEL 16
#define CH12_FIST_COUNT 7
#define CH12_KNIGHT_CHAR_ID 89
#define CH12_KNIGHT_LEVEL 13
#define CH12_KNIGHT_COUNT 4
#define CH12_DARK_MAGE_CHAR_ID 103
#define CH12_DARK_MAGE_LEVEL 13
#define CH12_DARK_MAGE_COUNT 4
#define CH12_RIDER_CHAR_ID 88
#define CH12_RIDER_LEVEL 17
#define CH12_RIDER_COUNT 2
#define CH12_GUIDE_ENEMY_TOTAL 36

/* The actor the cut-scene takes off the board, and where it sits.  Record 20
   is character 13 at level 15 on the enemy side, and the nine player slots
   come first, so it is map unit 29.  The shipped ICON12.DAT's RETIRE at script
   offset 125 names that index and no REVIVE anywhere in the member names it
   again, which is why the guide's list is thirty-six where the map is
   thirty-seven. */
#define CH12_ACTOR_UNIT 29
#define CH12_ACTOR_CHAR_ID 13
#define CH12_ACTOR_LEVEL 15
#define SCRIPT_CH13_RETIRED_UNIT CH12_ACTOR_UNIT

/* Where the cut-scene walks map unit 0 to: the shipped member's first
   PLACE_UNIT, at script offset 10, with its own three operands.  MAP12.COD
   record 37 -- the first record past the map's thirty-seven scripted ones, and
   so player slot 0's start tile -- is (17, 16), so a cursor that ended there
   would be a run whose script never opened. */
#define SCRIPT_CH13_WALKED_UNIT 0
#define SCRIPT_CH13_WALKED_TILE_X 4
#define SCRIPT_CH13_WALKED_TILE_Y 19
#define SCRIPT_CH13_WALKED_FACING 0

/* What the fixture ICON12.DAT's SET_UNIT_TIMER writes, and where.  It marks
   the last unit of the forty-six -- index 45, which exists only if all
   thirty-seven wave-0 records went down -- with a value no other member of the
   container writes.  The slot is status_timers[4], record 0x26, for the reason
   tests/chinit1.c gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1.
   The shipped member has no SET_UNIT_TIMER of its own, so this marker is the
   fixture's alone and the section says so. */
#define SCRIPT_CH13_MARKER_UNIT (CH12_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH13_MARKER_OPERAND 1
#define SCRIPT_CH13_MARKER_SLOT 4
#define SCRIPT_CH13_MARKER_VALUE 71

/* --- fdps_chapter_14_init @ 00021200 constants -------------------------- */

/* Chapter 14 is chapter id 13, and this handler is slot 13 of the table at
   00060074 -- the dword at 000600a8 is 00021200. */
#define CHAPTER_14_ID 13

/* The party when chapter 14 opens: the same nine chapters 12 and 13 opened
   with.  None of the three handlers between 琴琴's join and this one has a
   PUSH/CALL 0x00023bc0 anywhere in its body, and this one has none either in
   its 0x33 bytes. */
#define PARTY_AT_CHAPTER_14 9

/* MAP13.DAT's own header bytes, read back to prove chapter 14's map is the one
   that loaded: nine player slots at +1 and thirty-seven scripted deployments
   at +2.  Both were staged at other numbers before the run.  Nine slots for a
   nine member party, so every slot has somebody behind it and no zeroed,
   retired spare is written. */
#define CH13_PLAYER_SLOTS 9
#define CH13_CHAR_SPAWNS 37

/* MAP13.DAT tags NONE of its thirty-seven deployments wave 0 -- eighteen are
   wave 1, two are wave 2 and seventeen are wave 4 -- so the chapter state
   reset's own opening deploy puts nobody down and the nine player slots are
   the whole array until the cut-scene's first DEPLOY_WAVE. */
#define CH13_WAVE_ZERO_UNITS 0

/* What the fixture ICON13.DAT below asks for, and what the shipped member asks
   for: two DEPLOY_WAVEs, wave 2 and then wave 1, both with the place operand
   0, which is the nearest-free-walkable-tile search.  All four numbers are
   read off the shipped ICON13.DAT itself, walked with the opcode ladder in
   src/icon.c -- its DEPLOY_WAVEs are at script offsets 563 and 663 and it
   carries no SWITCH_MAP at all.  MAP13.DAT tags two and eighteen of its
   records with those two waves. */
#define SCRIPT_CH14_FIRST_WAVE 2
#define SCRIPT_CH14_SECOND_WAVE 1
#define SCRIPT_CH14_PLACE_NEAREST 0
#define CH13_FIRST_WAVE_UNITS 2
#define CH13_SECOND_WAVE_UNITS 18

/* How long the unit array is when the handler returns: the nine player slots,
   nothing from a wave-0 tag because MAP13.DAT has none, and the twenty the two
   DEPLOY_WAVEs put down. */
#define CH13_UNITS_AFTER_SCRIPT     (CH13_PLAYER_SLOTS + CH13_WAVE_ZERO_UNITS + CH13_FIRST_WAVE_UNITS      + CH13_SECOND_WAVE_UNITS)

/* The census those twenty come to, by character id and level, and the strategy
   guide's opening 敵方 group for the chapter read from the other side.  Which
   id is behind which guide line is settled by matching the guide's printed HP,
   MP, DX and MV against that id's ENEMYDAT.DAT row -- the coefficients are
   multiplied by the level and every one of the four rows agrees exactly, while
   the guide's AP and DP are the equipped numbers and do not:

     character 99, level 13, five  -- LV13 武士 (HP364, DX26, MV4)
     character 96, level 12, six   -- LV12 飛兵 (HP240, DX36, MV7)
     character 94, level 11, five  -- LV11 弓箭手 (HP253, DX44, MV4), the same
       弓箭手 id the chapter 13 section above pins at the same level
     character 103, level 13, four -- LV13 暗魔導士 (HP260, MP234, DX26, MV4),
       likewise chapter 13's

   The 武士 and the 飛兵 each come out of both waves: MAP13.DAT keys record 0
   (a 武士) and record 14 (a 飛兵) wave 2, and the other four 武士 and five
   飛兵 wave 1. */
#define CH13_SAMURAI_CHAR_ID 99
#define CH13_SAMURAI_LEVEL 13
#define CH13_SAMURAI_COUNT 5
#define CH13_FLIER_CHAR_ID 96
#define CH13_FLIER_LEVEL 12
#define CH13_FLIER_COUNT 6
#define CH13_ARCHER_CHAR_ID 94
#define CH13_ARCHER_LEVEL 11
#define CH13_ARCHER_COUNT 5
#define CH13_DARK_MAGE_CHAR_ID 103
#define CH13_DARK_MAGE_LEVEL 13
#define CH13_DARK_MAGE_COUNT 4
#define CH13_GUIDE_ENEMY_TOTAL 20

/* The map's wave 4, which no part of this handler deploys: sixteen more LV13
   武士 and one LV17 狼人, character 82 -- the same 狼人 id the chapter 11
   section above pins at level 13.  It is the strategy guide's own turn-six
   event, the reinforcements that appear below and the wolf that comes up on
   the left for the treasure chests, so a run that had deployed it would read
   back twenty-one 武士 rather than five and a wolf that should not be there
   yet. */
#define CH13_REINFORCEMENT_SAMURAI 16
#define CH13_WOLF_CHAR_ID 82
#define CH13_WOLF_LEVEL 17
#define CH13_WOLF_COUNT 0

/* Which unit the fixture's SET_UNIT_TIMER marks, and with what.  Index 28 is
   the last of the twenty-nine and exists only if both DEPLOY_WAVEs went down
   in full -- it is MAP13.DAT's record 19, the last of wave 1 -- and the value
   is one no other member of the container writes.  The slot is
   status_timers[4], record 0x26, for the reason tests/chinit1.c gives: it is
   the one status byte fdps_unit_select_status_icon does not read, so marking
   it cannot send fdps_draw_map_unit through the null status-icon sheet.  The
   opcode's operand is measured from status_timers[3] (src/icon.c), which makes
   that operand 1.  The shipped member has no SET_UNIT_TIMER of its own, so
   this marker is the fixture's alone and the section says so. */
#define SCRIPT_CH14_MARKER_UNIT (CH13_UNITS_AFTER_SCRIPT - 1)
#define SCRIPT_CH14_MARKER_OPERAND 1
#define SCRIPT_CH14_MARKER_SLOT 4
#define SCRIPT_CH14_MARKER_VALUE 79

/* Where the cut-scene walks map unit 0 to: the shipped member's PLACE_UNIT for
   unit 0, at script offset 603, with its own three operands.  MAP13.COD record
   37 -- the first record past the map's thirty-seven scripted ones, and so
   player slot 0's start tile -- is (20, 4), so a cursor that ended there would
   be a run whose script never opened.  Nothing after that opcode touches unit
   0's position: the ten PLACE_UNITs that follow it name units 1 to 10, and the
   opcodes past the last DEPLOY_WAVE only turn unit 0 and scroll the view. */
#define SCRIPT_CH14_WALKED_UNIT 0
#define SCRIPT_CH14_WALKED_TILE_X 7
#define SCRIPT_CH14_WALKED_TILE_Y 9
#define SCRIPT_CH14_WALKED_FACING 2

/* The side code the player's own units carry and the one a map deployment
   carries, and the flag bit fdps_build_map_unit_array sets on a player slot
   with no roster member behind it. */
#define PLAYER_SIDE 2
#define ENEMY_SIDE 0
#define UNIT_FLAG_RETIRED 1

/* How many rows the staged tables carry.  The enemy table is indexed by
   char_id - 0x3c (src/deploy.c) and this chapter's highest id is 108, which is
   row 48; the item table is indexed by an id the roster add takes straight out
   of an inventory entry with no bound of any kind. */
#define TABLE_ROWS 128
#define ITEM_TABLE_ROWS 256

/* The roster block: one 0x50-byte record per member, nine of them in use. */
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

static struct fdps_unit_record stage_roster[ROSTER_SLOTS];
static struct fdps_character_base_record stage_char[TABLE_ROWS];
static struct fdps_character_growth stage_growth[TABLE_ROWS];
static struct fdps_enemy_data stage_enemy[TABLE_ROWS];
static struct fdps_item_effect stage_items[ITEM_TABLE_ROWS];
static unsigned char stage_palette[DAC_ENTRIES * 3];

/* ICON09.DAT: the member the handler must NOT open, one chapter early.  It
   deploys nobody and marks unit 0. */
static unsigned char fixture_icon09_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH10_DECOY_VALUE,
    0x00
};

/* ICON10.DAT: the member fdps_chapter_11_init names.  Its three DEPLOY_WAVEs
   are the shipped member's own, in its order and with its operands -- wave 1,
   wave 2, wave 3, all place-exact 0 -- which on MAP10.DAT is seven, three and
   four records, and its SET_UNIT_TIMER marker then writes a value of its own
   onto the last unit the third of them appended.  The shipped member's other
   thirty-odd opcodes walk, pose, retire, revive and re-place the actors and
   are left out for the reason the file comment gives. */
static unsigned char fixture_icon10_dat[] = {
    0x04, SCRIPT_CH11_FIRST_WAVE, SCRIPT_CH11_PLACE_EXACT,
    0x04, SCRIPT_CH11_SECOND_WAVE, SCRIPT_CH11_PLACE_EXACT,
    0x04, SCRIPT_CH11_THIRD_WAVE, SCRIPT_CH11_PLACE_EXACT,
    0x12, SCRIPT_CH11_MARKER_UNIT, SCRIPT_CH11_MARKER_OPERAND,
    SCRIPT_CH11_MARKER_VALUE,
    0x00
};

/* ICON11.DAT: the member fdps_chapter_12_init names, and the member
   fdps_chapter_11_init must NOT open -- it is the one a rewrite that read the
   chapter number out of the handler's own name would reach for.  Its three
   SWITCH_MAPs and its first three DEPLOY_WAVEs are the shipped member's own, in
   its order and with its operands: out to cut-scene map 52 and its waves 1 and
   2, on to cut-scene map 40 and its wave 1 placed exactly, and back to map 11,
   whose reset rebuilds the chapter from scratch.  The shipped member's last
   DEPLOY_WAVE asks for the wave the player's answer chose and this one names
   wave 1, for the reason the constants above give, and the SET_UNIT_TIMER
   marker then writes a value of its own onto the opponent that deploy
   appended.  The shipped member's other hundred-odd opcodes walk, pose, retire,
   revive and re-place the actors and are left out for the reason the file
   comment gives. */
static unsigned char fixture_icon11_dat[] = {
    0x11, SCRIPT_CH12_FIRST_CUTSCENE_MAP,
    0x04, SCRIPT_CH12_FIRST_WAVE, SCRIPT_CH12_PLACE_NEAREST,
    0x04, SCRIPT_CH12_SECOND_WAVE, SCRIPT_CH12_PLACE_NEAREST,
    0x11, SCRIPT_CH12_SECOND_CUTSCENE_MAP,
    0x04, SCRIPT_CH12_FIRST_WAVE, SCRIPT_CH12_PLACE_EXACT,
    0x11, SCRIPT_CH12_CHAPTER_MAP,
    0x04, SCRIPT_CH12_ROOM_WAVE, SCRIPT_CH12_PLACE_NEAREST,
    0x12, SCRIPT_CH12_MARKER_UNIT, SCRIPT_CH12_MARKER_OPERAND,
    SCRIPT_CH12_MARKER_VALUE,
    0x00
};

/* ICON12.DAT: the member fdps_chapter_13_init names, and the member
   fdps_chapter_12_init must NOT open.  It carries no DEPLOY_WAVE and no
   SWITCH_MAP because the shipped member carries neither -- every one of
   MAP12.DAT's thirty-seven records is tagged wave 0, so the reset has already
   put the whole opposition down before the first opcode runs.  The three
   opcodes kept are the shipped member's own, in its order and with its
   operands: the PLACE_UNIT at script offset 10 that walks map unit 0 off its
   start tile onto (4, 19), the RETIRE at offset 125 that takes map unit 29 --
   record 20, the actor -- off the board and is the one RETIRE in the member
   with no REVIVE behind it, and then a SET_UNIT_TIMER marker of this file's
   own so the run can say which member the interpreter opened.  The shipped
   member's other thirty-odd opcodes pose the party, scroll the view, play a
   .saf clip, draw chapter text and trigger a cell event, and are left out for
   the reason the file comment gives. */
static unsigned char fixture_icon12_dat[] = {
    0x0a, SCRIPT_CH13_WALKED_UNIT, SCRIPT_CH13_WALKED_TILE_X,
    SCRIPT_CH13_WALKED_TILE_Y, SCRIPT_CH13_WALKED_FACING,
    0x0b, SCRIPT_CH13_RETIRED_UNIT,
    0x12, SCRIPT_CH13_MARKER_UNIT, SCRIPT_CH13_MARKER_OPERAND,
    SCRIPT_CH13_MARKER_VALUE,
    0x00
};

/* ICON13.DAT: the member fdps_chapter_14_init names, and the member
   fdps_chapter_13_init must NOT open.  Its two DEPLOY_WAVEs are the shipped
   member's own, in its order and with its operands -- wave 2 place-nearest and
   then wave 1 place-nearest -- which on MAP13.DAT is two records and eighteen,
   and between them sits the shipped member's PLACE_UNIT for unit 0, the one
   that walks it off its start tile onto (7, 9).  The SET_UNIT_TIMER marker at
   the end is this file's own addition, because the shipped member has none:
   it writes a value of its own onto the last unit the second wave appended.
   The shipped member's other eighty-odd opcodes walk, pose and place the rest
   of the cast, scroll the view, draw chapter text and set the music, and are
   left out for the reason the file comment gives. */
static unsigned char fixture_icon13_dat[] = {
    0x04, SCRIPT_CH14_FIRST_WAVE, SCRIPT_CH14_PLACE_NEAREST,
    0x0a, SCRIPT_CH14_WALKED_UNIT, SCRIPT_CH14_WALKED_TILE_X,
    SCRIPT_CH14_WALKED_TILE_Y, SCRIPT_CH14_WALKED_FACING,
    0x04, SCRIPT_CH14_SECOND_WAVE, SCRIPT_CH14_PLACE_NEAREST,
    0x12, SCRIPT_CH14_MARKER_UNIT, SCRIPT_CH14_MARKER_OPERAND,
    SCRIPT_CH14_MARKER_VALUE,
    0x00
};

/* ICON14.DAT: the member the chapter 14 handler must NOT open, one chapter
   late, and the one a rewrite that read the number out of its own name would
   reach for.  It deploys nobody and marks unit 0 with a value of its own. */
static unsigned char fixture_icon14_dat[] = {
    0x12, SCRIPT_DECOY_MARKER_UNIT, SCRIPT_DECOY_MARKER_OPERAND,
    SCRIPT_CH15_DECOY_VALUE,
    0x00
};

static char *fixture_names[FIXTURE_MEMBERS] = {
    "ICON09.DAT", "ICON10.DAT", "ICON11.DAT", "ICON12.DAT", "ICON13.DAT",
    "ICON14.DAT"
};

static unsigned char *fixture_bytes[FIXTURE_MEMBERS] = {
    fixture_icon09_dat, fixture_icon10_dat, fixture_icon11_dat,
    fixture_icon12_dat, fixture_icon13_dat, fixture_icon14_dat
};

static int fixture_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_icon09_dat), sizeof(fixture_icon10_dat),
    sizeof(fixture_icon11_dat), sizeof(fixture_icon12_dat),
    sizeof(fixture_icon13_dat), sizeof(fixture_icon14_dat)
};

/* 0 not attempted, 1 the run happened and the snapshot below is good,
   2 unavailable and every case skips. */
static int run11_state = 0;

/* Whether this file created the container, and so whether it may remove it. */
static int fixture_owned = 0;

/* The party in join order, put on with the game's own add before the handler
   runs. */
static int chapter_11_party[PARTY_AT_CHAPTER_11] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID
};

/* Everything the cases assert, captured the instant the handler returned. */
static int seen_roster_count;
static int seen_roster_char_id;
static int seen_roster_hp_max;
static int seen_roster_mp_max;
static int seen_player_slots;
static int seen_char_spawns;
static int seen_unit_count;
static int seen_party_char_ids[PARTY_AT_CHAPTER_11];
static int seen_qinqin_char_id;
static int seen_qinqin_level;
static int seen_qinqin_side;
static int seen_qinqin_flags;
static int seen_qinqin_hp_current;
static int seen_qinqin_hp_max;
static int seen_qinqin_mp_max;
static int seen_standin_char_ids[CH10_WAVE_ZERO_UNITS];
static int seen_standin_levels[CH10_WAVE_ZERO_UNITS];
static int seen_mages;
static int seen_ice_mages;
static int seen_ronin;
static int seen_wolves;
static int seen_fists;
static int seen_archers;
static int seen_last_unit_char_id;
static int seen_last_unit_level;
static int seen_last_unit_side;
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

    /* Both are staged at numbers no chapter's map carries -- MAP10.DAT says 9
       and 49, MAP11.DAT says 9 and 3, MAP12.DAT says 9 and 37 -- so reading a
       map's own pair back says the chapter really loaded. */
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

static void capture_chapter_11(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *qinqin;
    struct fdps_unit_record *marked;
    int unit_index;
    int char_id;
    int slot;

    seen_roster_count = data_fdps_roster_member_count;
    seen_roster_char_id = (int) stage_roster[QINQIN_ROSTER_SLOT].char_id;
    seen_roster_hp_max = (int) stage_roster[QINQIN_ROSTER_SLOT].hp_max;
    seen_roster_mp_max = (int) stage_roster[QINQIN_ROSTER_SLOT].mp_max;
    seen_player_slots = data_fdps_map_player_slot_count;
    seen_char_spawns = data_fdps_map_char_spawn_count;
    seen_unit_count = data_fdps_map_unit_count;
    seen_cursor_x = data_fdps_map_cursor_world_x;
    seen_cursor_y = data_fdps_map_cursor_world_y;
    seen_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    for (slot = 0; slot < PARTY_AT_CHAPTER_11; slot++) {
        seen_party_char_ids[slot] = (int) unit0[slot].char_id;
    }

    qinqin = unit0 + QINQIN_UNIT;
    seen_qinqin_char_id = (int) qinqin->char_id;
    seen_qinqin_level = (int) qinqin->level;
    seen_qinqin_side = (int) qinqin->side;
    seen_qinqin_flags = (int) qinqin->flags;
    seen_qinqin_hp_current = (int) qinqin->hp_current;
    seen_qinqin_hp_max = (int) qinqin->hp_max;
    seen_qinqin_mp_max = (int) qinqin->mp_max;

    for (slot = 0; slot < CH10_WAVE_ZERO_UNITS; slot++) {
        seen_standin_char_ids[slot] =
            (int) unit0[CH10_FIRST_WAVE_ZERO_UNIT + slot].char_id;
        seen_standin_levels[slot] =
            (int) unit0[CH10_FIRST_WAVE_ZERO_UNIT + slot].level;
    }

    seen_mages = 0;
    seen_ice_mages = 0;
    seen_ronin = 0;
    seen_wolves = 0;
    seen_fists = 0;
    seen_archers = 0;
    for (unit_index = 0;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        if (char_id == CH10_MAGE_CHAR_ID) {
            seen_mages++;
        } else if (char_id == CH10_ICE_MAGE_CHAR_ID) {
            seen_ice_mages++;
        } else if (char_id == CH10_RONIN_CHAR_ID) {
            seen_ronin++;
        } else if (char_id == CH10_WOLF_CHAR_ID) {
            seen_wolves++;
        } else if (char_id == CH10_FIST_CHAR_ID) {
            seen_fists++;
        } else if (char_id == CH10_ARCHER_CHAR_ID) {
            seen_archers++;
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen_marked_timers[slot] = 0;
        seen_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The last unit and the marker sit at index 25, which exists only if all
       three waves went down.  A run that opened a decoy stops at twelve units,
       and reading past the array to say so would be reading memory the
       allocation does not cover -- so the three fields are left at values no
       correct run produces and the cases below fail on those instead. */
    seen_last_unit_char_id = -1;
    seen_last_unit_level = -1;
    seen_last_unit_side = -1;
    if (data_fdps_map_unit_count > CH10_LAST_UNIT) {
        marked = unit0 + CH10_LAST_UNIT;
        seen_last_unit_char_id = (int) marked->char_id;
        seen_last_unit_level = (int) marked->level;
        seen_last_unit_side = (int) marked->side;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 11 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The eight members the
   party has when the chapter opens are put on with the game's own add, in join
   order, so that the ninth player slot MAP10.DAT asks for is the one the
   handler's own add has to fill. */
static void run_chapter_11_handler(void)
{
    int member;

    if (run11_state != 0) {
        return;
    }
    run11_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_11_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_11; member++) {
        fdps_roster_add_character(chapter_11_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_11_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_11();
    free_chapter_globals();
    run11_state = 1;
}

/* 琴琴 joins the party, and she joins it BEFORE the chapter is built.  The
   roster count is nine where the run put eight on, and roster slot 8 carries
   character 7 with the 228 HP and 40 MP her own table rows compute -- the
   numbers the strategy guide prints for her.  MAP10.DAT's own two header
   counts are read back because they were staged at numbers it does not carry,
   and the nine player slots are then read off the map: the first eight are the
   join-order party and slot 8 is her, on the player side with the retired bit
   clear and her roster HP intact.  Had the state reset run first, slot 8 would
   be the zeroed spare fdps_build_map_unit_array writes for a player slot with
   no roster member behind it. */
static void qinqin_joins_before_the_chapter_is_built(void)
{
    int slot;

    run_chapter_11_handler();
    CHECK_EQ(run11_state, 1);
    if (run11_state != 1) {
        return;
    }

    CHECK_EQ(seen_roster_count, PARTY_AT_CHAPTER_11 + 1);
    CHECK_EQ(seen_roster_char_id, QINQIN_CHAR_ID);
    CHECK_EQ(seen_roster_hp_max, QINQIN_HP_MAX);
    CHECK_EQ(seen_roster_mp_max, QINQIN_MP_MAX);

    CHECK_EQ(seen_player_slots, CH10_PLAYER_SLOTS);
    CHECK_EQ(seen_char_spawns, CH10_CHAR_SPAWNS);
    for (slot = 0; slot < PARTY_AT_CHAPTER_11; slot++) {
        CHECK_EQ(seen_party_char_ids[slot], chapter_11_party[slot]);
    }

    CHECK_EQ(seen_qinqin_char_id, QINQIN_CHAR_ID);
    CHECK_EQ(seen_qinqin_level, QINQIN_LEVEL);
    CHECK_EQ(seen_qinqin_side, PLAYER_SIDE);
    CHECK_EQ(seen_qinqin_flags, 0);
    CHECK_EQ(seen_qinqin_hp_current, QINQIN_HP_MAX);
    CHECK_EQ(seen_qinqin_hp_max, QINQIN_HP_MAX);
    CHECK_EQ(seen_qinqin_mp_max, QINQIN_MP_MAX);
}

/* What the reset and the cut-scene together leave on the map: nine player
   slots, the three level-1 stand-ins MAP10.DAT tags wave 0, and the fourteen
   the three DEPLOY_WAVEs put down, for twenty-six.  The census behind those
   fourteen is the strategy guide's opening 敵方 group found by character id --
   three 魔導士, two 冰魔導士, one 野武士, two 狼人, three 拳士 and three
   弓兵 -- and the last of them is read back by index as well, because index 25
   exists only if all three waves went down in the shipped order. */
static void chapter_eleven_opens_with_the_guides_enemy_group(void)
{
    int stand_in;

    run_chapter_11_handler();
    CHECK_EQ(run11_state, 1);
    if (run11_state != 1) {
        return;
    }

    CHECK_EQ(seen_unit_count, CH10_UNITS_AFTER_SCRIPT);

    for (stand_in = 0; stand_in < CH10_WAVE_ZERO_UNITS; stand_in++) {
        CHECK_EQ(seen_standin_char_ids[stand_in], CH10_STANDIN_CHAR_ID);
        CHECK_EQ(seen_standin_levels[stand_in], CH10_STANDIN_LEVEL);
    }

    CHECK_EQ(seen_mages, CH10_MAGE_COUNT);
    CHECK_EQ(seen_ice_mages, CH10_ICE_MAGE_COUNT);
    CHECK_EQ(seen_ronin, CH10_RONIN_COUNT);
    CHECK_EQ(seen_wolves, CH10_WOLF_COUNT);
    CHECK_EQ(seen_fists, CH10_FIST_COUNT);
    CHECK_EQ(seen_archers, CH10_ARCHER_COUNT);

    CHECK_EQ(seen_last_unit_char_id, CH10_ARCHER_CHAR_ID);
    CHECK_EQ(seen_last_unit_level, CH10_LAST_UNIT_LEVEL);
    CHECK_EQ(seen_last_unit_side, ENEMY_SIDE);
}

/* The cut-scene the handler names is Icon10.dat and not the neighbour on
   either side of it.  The marker sits on unit 25 with the value only
   ICON10.DAT writes, and unit 0's own timers are read back clear -- that is
   where ICON09.DAT's marker would have landed, and ICON11.DAT, chapter 12's
   own script, would have shown up in the chapter id below instead: it ends on
   a SWITCH_MAP that leaves the id at 11.  So a handler that had spelled the
   script name out of its own chapter number shows up here either way.  Every
   other timer on the marked unit is clear too: neither the handler nor the
   shipped cut-scene writes a status on anybody this chapter.  The chapter id
   is still 10, which the handler neither reads nor writes and which the
   script cannot disturb either -- ICON10.DAT carries no SWITCH_MAP -- and both
   the script number and the title-card graphic are chosen from it by the
   callees. */
static void the_chapter_11_cutscene_is_icon10_dat(void)
{
    int slot;

    run_chapter_11_handler();
    CHECK_EQ(run11_state, 1);
    if (run11_state != 1) {
        return;
    }

    CHECK_EQ(seen_marked_timers[SCRIPT_CH11_MARKER_SLOT],
             SCRIPT_CH11_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH11_MARKER_SLOT) {
            CHECK_EQ(seen_marked_timers[slot], 0);
        }
        CHECK_EQ(seen_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen_chapter_id, CHAPTER_11_ID);
}

/* The cursor ends on unit 0's tile.  MAP10.COD record 49 -- the first record
   past the map's 49 scripted deployments -- puts the first party slot on tile
   (10, 21), and a player slot is placed on its record exactly, so the cursor
   globals are that tile scaled by the 24-pixel step.  Both were staged at
   other numbers before the run. */
static void the_chapter_11_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_11_handler();
    CHECK_EQ(run11_state, 1);
    if (run11_state != 1) {
        return;
    }

    CHECK_EQ(seen_cursor_x, CH10_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen_cursor_y, CH10_PARTY_TILE_Y * CURSOR_TILE_STEP);
}


/* --- fdps_chapter_12_init @ 00021180 ------------------------------------
 *
 * Four calls, straight line, no branch and no store of its own -- the plain
 * form, with no fdps_roster_add_character in front of the rebuild.  What it
 * decides is the ORDER of the four calls and which cut-scene member and which
 * unit its two arguments name, so the run below enters chapter 12 once for real
 * and reads the answers off the state it leaves.
 *
 * Expected values come from the assembly at 00021180 and from the shipped
 * data, never from the emitted C:
 *
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61888 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon11.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * NOBODY JOINS AND NO SLOT IS LEFT OVER.  The party is nine members when the
 * chapter opens -- the eight chapters 1 to 4 and 7 to 9 put on, and 琴琴 from
 * chapter 11 -- and MAP11.DAT asks for nine player slots, so every slot has a
 * member behind it and the array carries no zeroed, retired spare.  There is no
 * untouched roster slot to read the 0xff sentinel out of, so what says the
 * handler added nobody is the count still reading nine.
 *
 * THE MAP CARRIES NO WAVE-0 RECORD AT ALL, which is true of no other chapter
 * map these two files stage: MAP11.DAT's three deployments are keyed waves 1,
 * 2 and 3, the guide's three rooms, so the state reset's own opening deploy
 * puts nobody down and the array is nine player slots until the cut-scene's
 * last DEPLOY_WAVE appends the one opponent the player's answer chose.
 */

static int run12_state = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order.  The run puts them on with the game's own add and the case below reads
   the same nine back off the map. */
static int chapter_12_party[PARTY_AT_CHAPTER_12] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID
};

static int seen12_roster_count;
static int seen12_player_slots;
static int seen12_char_spawns;
static int seen12_unit_count;
static int seen12_party_char_ids[PARTY_AT_CHAPTER_12];
static int seen12_party_live_slots;
static int seen12_room_char_id;
static int seen12_room_level;
static int seen12_room_side;
static unsigned char seen12_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen12_unit0_timers[STATUS_TIMER_COUNT];
static int seen12_cursor_x;
static int seen12_cursor_y;
static int seen12_chapter_id;

static void capture_chapter_12(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    int slot;

    seen12_roster_count = data_fdps_roster_member_count;
    seen12_player_slots = data_fdps_map_player_slot_count;
    seen12_char_spawns = data_fdps_map_char_spawn_count;
    seen12_unit_count = data_fdps_map_unit_count;
    seen12_cursor_x = data_fdps_map_cursor_world_x;
    seen12_cursor_y = data_fdps_map_cursor_world_y;
    seen12_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    seen12_party_live_slots = 0;
    for (slot = 0; slot < PARTY_AT_CHAPTER_12; slot++) {
        seen12_party_char_ids[slot] = (int) unit0[slot].char_id;
        if ((int) unit0[slot].side == PLAYER_SIDE
            && (int) unit0[slot].flags == 0) {
            seen12_party_live_slots++;
        }
    }

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen12_marked_timers[slot] = 0;
        seen12_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The opponent and the marker both sit at index 9, which exists only if the
       last SWITCH_MAP rebuilt map 11 and the room wave then deployed.  A run
       that opened a decoy stops at nine units, and reading past the array to
       say so would be reading memory the allocation does not cover -- so the
       three fields are left at values no correct run produces and the cases
       below fail on those instead. */
    seen12_room_char_id = -1;
    seen12_room_level = -1;
    seen12_room_side = -1;
    if (data_fdps_map_unit_count > CH11_ROOM_UNIT) {
        marked = unit0 + CH11_ROOM_UNIT;
        seen12_room_char_id = (int) marked->char_id;
        seen12_room_level = (int) marked->level;
        seen12_room_side = (int) marked->side;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen12_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 12 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The nine members the
   party has when the chapter opens are put on with the game's own add, in join
   order, because this handler adds nobody and MAP11.DAT's nine player slots
   have to be filled from a roster the run staged honestly.  The timer hook and
   the graphics mode are here for the reasons the file comment gives. */
static void run_chapter_12_handler(void)
{
    int member;

    if (run12_state != 0) {
        return;
    }
    run12_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_12_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_12; member++) {
        fdps_roster_add_character(chapter_12_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_12_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_12();
    free_chapter_globals();
    run12_state = 1;
}

/* Nobody joins the party this chapter and no player slot is left over.  The
   roster count is the nine the run put on and not ten, which is what says the
   handler has no fdps_roster_add_character in it; the nine player slots carry
   those same nine characters in join order, every one of them on the player
   side with the retired bit clear, so none of them is the zeroed spare
   fdps_build_map_unit_array writes for a slot with no member behind it.
   MAP11.DAT's own two header counts are read back as well because they were
   staged at numbers it does not carry. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_twelve(void)
{
    int slot;

    run_chapter_12_handler();
    CHECK_EQ(run12_state, 1);
    if (run12_state != 1) {
        return;
    }

    CHECK_EQ(seen12_roster_count, PARTY_AT_CHAPTER_12);
    CHECK_EQ(seen12_player_slots, CH11_PLAYER_SLOTS);
    CHECK_EQ(seen12_char_spawns, CH11_CHAR_SPAWNS);
    for (slot = 0; slot < PARTY_AT_CHAPTER_12; slot++) {
        CHECK_EQ(seen12_party_char_ids[slot], chapter_12_party[slot]);
    }
    CHECK_EQ(seen12_party_live_slots, PARTY_AT_CHAPTER_12);
}

/* What the reset and the cut-scene together leave on the map: the nine player
   slots, nothing from a wave-0 tag because MAP11.DAT has none, and the single
   opponent the room wave deploys, for ten.  That opponent is read back by index
   as MAP11.DAT's wave-1 record -- character 113, level 20, on the enemy side --
   which is the strategy guide's 左邊房間 LV20 修佩魯.  A ten that came out as
   nine would be a run whose script deployed nobody. */
static void chapter_twelve_opens_on_the_one_room_boss(void)
{
    run_chapter_12_handler();
    CHECK_EQ(run12_state, 1);
    if (run12_state != 1) {
        return;
    }

    CHECK_EQ(seen12_unit_count, CH11_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen12_room_char_id, CH11_ROOM_CHAR_ID);
    CHECK_EQ(seen12_room_level, CH11_ROOM_BOSS_LEVEL);
    CHECK_EQ(seen12_room_side, ENEMY_SIDE);
}

/* The cut-scene the handler names is Icon11.dat and not the neighbour on either
   side of it.  The marker sits on unit 9 with the value only ICON11.DAT writes,
   so neither neighbour can produce it: ICON12.DAT, chapter 13's own script,
   deploys nobody and marks unit 45, an index a nine-unit array does not reach,
   and ICON10.DAT, chapter 11's own script, deploys three waves MAP11.DAT keys
   one record each, so a run that opened it would land twelve units and no
   marker at all on unit 9.  Unit 0's own timers are read back clear as well.  Every other timer on the
   marked unit is clear too: neither the handler nor the shipped cut-scene
   writes a status on anybody this chapter.  The chapter id is back at 11, which
   the handler neither reads nor writes -- the script's last SWITCH_MAP put it
   there, after two cut-scene maps of its own -- and both the script number and
   the title-card graphic are chosen from it by the callees. */
static void the_chapter_12_cutscene_is_icon11_dat(void)
{
    int slot;

    run_chapter_12_handler();
    CHECK_EQ(run12_state, 1);
    if (run12_state != 1) {
        return;
    }

    CHECK_EQ(seen12_marked_timers[SCRIPT_CH12_MARKER_SLOT],
             SCRIPT_CH12_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH12_MARKER_SLOT) {
            CHECK_EQ(seen12_marked_timers[slot], 0);
        }
        CHECK_EQ(seen12_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen12_chapter_id, CHAPTER_12_ID);
}

/* The cursor ends on unit 0's tile.  MAP11.COD record 3 -- the first record
   past the map's three scripted deployments -- puts the first party slot on
   tile (0, 0), and a player slot is placed on its record exactly, so the cursor
   globals are that tile scaled by the 24-pixel step.  Both were staged at 48
   and 72 before the run, so the zeroes are the walk arriving and not a global
   nobody wrote. */
static void the_chapter_12_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_12_handler();
    CHECK_EQ(run12_state, 1);
    if (run12_state != 1) {
        return;
    }

    CHECK_EQ(seen12_cursor_x, CH11_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen12_cursor_y, CH11_PARTY_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_13_init @ 000211c0 ------------------------------------
 *
 * Four calls, straight line, no branch and no store of its own -- the same
 * plain form as chapter 12's, with no fdps_roster_add_character in front of the
 * rebuild.  What it decides is the ORDER of the four calls and which cut-scene
 * member and which unit its two arguments name, so the run below enters
 * chapter 13 once for real and reads the answers off the state it leaves.
 *
 * Expected values come from the assembly at 000211c0 and from the shipped
 * data, never from the emitted C:
 *
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61894 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon12.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * THE MAP TAGS EVERY RECORD WAVE 0, which is true of no other chapter map this
 * file stages.  MAP12.DAT's thirty-seven deployments are all keyed wave 0, so
 * the state reset's own opening deploy puts the entire opposition down and the
 * array is forty-six units before the script runs at all; ICON12.DAT carries no
 * DEPLOY_WAVE and no SWITCH_MAP.  What the script leaves behind is one unit
 * retired and the party walked off its start tiles, and those are the two
 * things the section below pins.
 *
 * WHY THE CURSOR CASE IS THE ORDERING CASE HERE.  In the chapter 11 and 12
 * sections the cursor lands on the tile the map file gave player slot 0, so
 * they pin the call and its argument and nothing about when it ran.  Chapter
 * 13's cut-scene walks map unit 0 off (17, 16) and onto (4, 19), so the tile
 * the cursor ends on says the fourth call ran AFTER the second.
 */

static int run13_state = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order: the same nine chapter 12 opened with. */
static int chapter_13_party[PARTY_AT_CHAPTER_13] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID
};

static int seen13_roster_count;
static int seen13_player_slots;
static int seen13_char_spawns;
static int seen13_unit_count;
static int seen13_party_char_ids[PARTY_AT_CHAPTER_13];
static int seen13_party_live_slots;
static int seen13_bosses;
static int seen13_dark_riders;
static int seen13_archers;
static int seen13_fists;
static int seen13_knights;
static int seen13_dark_mages;
static int seen13_riders;
static int seen13_live_enemies;
static int seen13_actor_char_id;
static int seen13_actor_level;
static int seen13_actor_flags;
static unsigned char seen13_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen13_unit0_timers[STATUS_TIMER_COUNT];
static int seen13_cursor_x;
static int seen13_cursor_y;
static int seen13_chapter_id;

/* Counts one character id and level among the units still in play: the retired
   bit is what the cut-scene's RETIRE sets, and the guide's list counts only
   what the player meets. */
static int count_live_units(struct fdps_unit_record *units, int unit_count,
                            int char_id, int level)
{
    int index;
    int found;

    found = 0;
    for (index = 0; index < unit_count; index++) {
        if ((int) units[index].char_id == char_id
            && (int) units[index].level == level
            && ((int) units[index].flags & UNIT_FLAG_RETIRED) == 0) {
            found++;
        }
    }
    return found;
}

static void capture_chapter_13(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *actor;
    struct fdps_unit_record *marked;
    int slot;

    seen13_roster_count = data_fdps_roster_member_count;
    seen13_player_slots = data_fdps_map_player_slot_count;
    seen13_char_spawns = data_fdps_map_char_spawn_count;
    seen13_unit_count = data_fdps_map_unit_count;
    seen13_cursor_x = data_fdps_map_cursor_world_x;
    seen13_cursor_y = data_fdps_map_cursor_world_y;
    seen13_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    seen13_party_live_slots = 0;
    for (slot = 0; slot < PARTY_AT_CHAPTER_13; slot++) {
        seen13_party_char_ids[slot] = (int) unit0[slot].char_id;
        if ((int) unit0[slot].side == PLAYER_SIDE
            && (int) unit0[slot].flags == 0) {
            seen13_party_live_slots++;
        }
    }

    seen13_bosses =
        count_live_units(unit0, seen13_unit_count, CH12_BOSS_1_CHAR_ID,
                         CH12_BOSS_LEVEL)
        + count_live_units(unit0, seen13_unit_count, CH12_BOSS_2_CHAR_ID,
                           CH12_BOSS_LEVEL)
        + count_live_units(unit0, seen13_unit_count, CH12_BOSS_3_CHAR_ID,
                           CH12_BOSS_LEVEL);
    seen13_dark_riders =
        count_live_units(unit0, seen13_unit_count, CH12_DARK_RIDER_CHAR_ID,
                         CH12_DARK_RIDER_LEVEL);
    seen13_archers =
        count_live_units(unit0, seen13_unit_count, CH12_ARCHER_CHAR_ID,
                         CH12_ARCHER_LEVEL);
    seen13_fists =
        count_live_units(unit0, seen13_unit_count, CH12_FIST_CHAR_ID,
                         CH12_FIST_LEVEL);
    seen13_knights =
        count_live_units(unit0, seen13_unit_count, CH12_KNIGHT_CHAR_ID,
                         CH12_KNIGHT_LEVEL);
    seen13_dark_mages =
        count_live_units(unit0, seen13_unit_count, CH12_DARK_MAGE_CHAR_ID,
                         CH12_DARK_MAGE_LEVEL);
    seen13_riders =
        count_live_units(unit0, seen13_unit_count, CH12_RIDER_CHAR_ID,
                         CH12_RIDER_LEVEL);
    seen13_live_enemies = seen13_bosses + seen13_dark_riders + seen13_archers
                          + seen13_fists + seen13_knights + seen13_dark_mages
                          + seen13_riders;

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen13_marked_timers[slot] = 0;
        seen13_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The actor sits at index 29 and the marker at index 45, and both exist
       only if all thirty-seven wave-0 records went down.  A run that opened a
       neighbouring member lands a different array entirely, and reading past
       the allocation to say so would be reading memory it does not cover -- so
       the three fields are left at values no correct run produces and the
       cases below fail on those instead. */
    seen13_actor_char_id = -1;
    seen13_actor_level = -1;
    seen13_actor_flags = -1;
    if (seen13_unit_count > SCRIPT_CH13_MARKER_UNIT) {
        actor = unit0 + CH12_ACTOR_UNIT;
        seen13_actor_char_id = (int) actor->char_id;
        seen13_actor_level = (int) actor->level;
        seen13_actor_flags = (int) actor->flags;

        marked = unit0 + SCRIPT_CH13_MARKER_UNIT;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen13_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 13 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The nine members the
   party has when the chapter opens are put on with the game's own add, in join
   order, because this handler adds nobody and MAP12.DAT's nine player slots
   have to be filled from a roster the run staged honestly.  The timer hook and
   the graphics mode are here for the reasons the file comment gives. */
static void run_chapter_13_handler(void)
{
    int member;

    if (run13_state != 0) {
        return;
    }
    run13_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_13_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_13; member++) {
        fdps_roster_add_character(chapter_13_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_13_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_13();
    free_chapter_globals();
    run13_state = 1;
}

/* Nobody joins the party this chapter and no player slot is left over.  The
   roster count is the nine the run put on and not ten, which is what says the
   handler has no fdps_roster_add_character in it; the nine player slots carry
   those same nine characters in join order, every one of them on the player
   side with the retired bit clear, so none of them is the zeroed spare
   fdps_build_map_unit_array writes for a slot with no member behind it.
   MAP12.DAT's own two header counts are read back as well because they were
   staged at numbers it does not carry. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_thirteen(void)
{
    int slot;

    run_chapter_13_handler();
    CHECK_EQ(run13_state, 1);
    if (run13_state != 1) {
        return;
    }

    CHECK_EQ(seen13_roster_count, PARTY_AT_CHAPTER_13);
    CHECK_EQ(seen13_player_slots, CH12_PLAYER_SLOTS);
    CHECK_EQ(seen13_char_spawns, CH12_CHAR_SPAWNS);
    for (slot = 0; slot < PARTY_AT_CHAPTER_13; slot++) {
        CHECK_EQ(seen13_party_char_ids[slot], chapter_13_party[slot]);
    }
    CHECK_EQ(seen13_party_live_slots, PARTY_AT_CHAPTER_13);
}

/* What the reset leaves on the map and what the cut-scene takes off it again.
   The array is forty-six units -- the nine player slots and all thirty-seven of
   MAP12.DAT's wave-0 records, because the map tags nothing any other wave and
   the script deploys nobody.  Thirty-six of the thirty-seven are still in play
   and they are the strategy guide's enemy list to the number, counted by
   character id and level; the thirty-seventh is map unit 29, character 13 at
   level 15, which the script's one unpaired RETIRE takes off the board and
   which appears in no guide line. */
static void chapter_thirteen_opens_with_the_guides_enemy_group(void)
{
    run_chapter_13_handler();
    CHECK_EQ(run13_state, 1);
    if (run13_state != 1) {
        return;
    }

    CHECK_EQ(seen13_unit_count, CH12_UNITS_AFTER_SCRIPT);

    CHECK_EQ(seen13_bosses, CH12_BOSS_COUNT);
    CHECK_EQ(seen13_dark_riders, CH12_DARK_RIDER_COUNT);
    CHECK_EQ(seen13_archers, CH12_ARCHER_COUNT);
    CHECK_EQ(seen13_fists, CH12_FIST_COUNT);
    CHECK_EQ(seen13_knights, CH12_KNIGHT_COUNT);
    CHECK_EQ(seen13_dark_mages, CH12_DARK_MAGE_COUNT);
    CHECK_EQ(seen13_riders, CH12_RIDER_COUNT);
    CHECK_EQ(seen13_live_enemies, CH12_GUIDE_ENEMY_TOTAL);

    CHECK_EQ(seen13_actor_char_id, CH12_ACTOR_CHAR_ID);
    CHECK_EQ(seen13_actor_level, CH12_ACTOR_LEVEL);
    CHECK_EQ(seen13_actor_flags, UNIT_FLAG_RETIRED);
}

/* The cut-scene the handler names is Icon12.dat and not the neighbour on either
   side of it.  The marker sits on unit 45 with the value only ICON12.DAT
   writes, an index no other member of the container reaches; ICON13.DAT,
   chapter 14's own script, would have left it clear -- its own marker lands on
   unit 28 -- and would have walked unit 0 onto (7, 9) rather than (4, 19) and
   left map unit 29 unretired, and ICON11.DAT, chapter 12's own script, ends on
   a SWITCH_MAP that would have left the chapter id at 11 and an array of a
   different length entirely.  Every
   other timer on the marked unit is clear too, and so is every one of unit 0's:
   neither the handler nor the shipped cut-scene writes a status on anybody this
   chapter -- the shipped ICON12.DAT has no SET_UNIT_TIMER at all, which is why
   the marker here is the fixture's own addition.  The chapter id is still 12,
   which the handler neither reads nor writes and which this member cannot
   disturb either, and both the script number and the title-card graphic are
   chosen from it by the callees. */
static void the_chapter_13_cutscene_is_icon12_dat(void)
{
    int slot;

    run_chapter_13_handler();
    CHECK_EQ(run13_state, 1);
    if (run13_state != 1) {
        return;
    }

    CHECK_EQ(seen13_marked_timers[SCRIPT_CH13_MARKER_SLOT],
             SCRIPT_CH13_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH13_MARKER_SLOT) {
            CHECK_EQ(seen13_marked_timers[slot], 0);
        }
        CHECK_EQ(seen13_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen13_chapter_id, CHAPTER_13_ID);
}

/* The cursor ends on unit 0's tile, and unit 0 is where the cut-scene left it
   rather than where the map put it.  MAP12.COD record 37 -- the first record
   past the map's thirty-seven scripted deployments -- puts player slot 0 on
   tile (17, 16), and the script's first PLACE_UNIT then walks it to (4, 19);
   the cursor globals are that second tile scaled by the 24-pixel step, which
   only a handler that ran the cursor call after the script can produce.  Both
   globals were staged at 48 and 72 before the run. */
static void the_chapter_13_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_13_handler();
    CHECK_EQ(run13_state, 1);
    if (run13_state != 1) {
        return;
    }

    CHECK_EQ(seen13_cursor_x, SCRIPT_CH13_WALKED_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen13_cursor_y, SCRIPT_CH13_WALKED_TILE_Y * CURSOR_TILE_STEP);
}


/* --- fdps_chapter_14_init @ 00021200 ------------------------------------
 *
 * Four calls, straight line, no branch and no store of its own -- the same
 * plain form as chapters 12 and 13, with no fdps_roster_add_character in front
 * of the rebuild.  What it decides is the ORDER of the four calls and which
 * cut-scene member and which unit its two arguments name, so the run below
 * enters chapter 14 once for real and reads the answers off the state it
 * leaves.
 *
 * Expected values come from the assembly at 00021200 and from the shipped
 * data, never from the emitted C:
 *
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x618a0 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon13.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * NOBODY JOINS AND NO SLOT IS LEFT OVER.  The party is nine members when the
 * chapter opens -- the eight chapters 1 to 4 and 7 to 9 put on, and 琴琴 from
 * chapter 11 -- and MAP13.DAT asks for nine player slots, so every slot has a
 * member behind it and the array carries no zeroed, retired spare.  There is
 * no untouched roster slot to read the 0xff sentinel out of, so what says the
 * handler added nobody is the count still reading nine.
 *
 * THE MAP CARRIES NO WAVE-0 RECORD AND THE CUT-SCENE BRINGS THE WHOLE OPENING
 * OPPOSITION.  MAP13.DAT's thirty-seven deployments are keyed 1, 2 and 4, so
 * the state reset puts nobody down; the script's two DEPLOY_WAVEs then add
 * wave 2's two records and wave 1's eighteen, twenty opponents that are the
 * strategy guide's opening 敵方 group to the number.  The seventeen records
 * keyed wave 4 are the guide's turn-six reinforcements and nothing in this
 * handler deploys them, so the wolf that comes for the treasure chests must
 * not be on the map when it returns.
 */

static int run14_state = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order.  The run puts them on with the game's own add and the case below
   reads the same nine back off the map. */
static int chapter_14_party[PARTY_AT_CHAPTER_14] = {
    RANDIS_CHAR_ID, JULIAN_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID,
    QINQIN_CHAR_ID
};

static int seen14_roster_count;
static int seen14_player_slots;
static int seen14_char_spawns;
static int seen14_unit_count;
static int seen14_party_char_ids[PARTY_AT_CHAPTER_14];
static int seen14_party_live_slots;
static int seen14_samurai;
static int seen14_fliers;
static int seen14_archers;
static int seen14_dark_mages;
static int seen14_wolves;
static int seen14_live_enemies;
static int seen14_last_unit_char_id;
static int seen14_last_unit_level;
static int seen14_last_unit_side;
static unsigned char seen14_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen14_unit0_timers[STATUS_TIMER_COUNT];
static int seen14_cursor_x;
static int seen14_cursor_y;
static int seen14_chapter_id;

static void capture_chapter_14(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    int slot;

    seen14_roster_count = data_fdps_roster_member_count;
    seen14_player_slots = data_fdps_map_player_slot_count;
    seen14_char_spawns = data_fdps_map_char_spawn_count;
    seen14_unit_count = data_fdps_map_unit_count;
    seen14_cursor_x = data_fdps_map_cursor_world_x;
    seen14_cursor_y = data_fdps_map_cursor_world_y;
    seen14_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;

    seen14_party_live_slots = 0;
    for (slot = 0; slot < PARTY_AT_CHAPTER_14; slot++) {
        seen14_party_char_ids[slot] = (int) unit0[slot].char_id;
        if ((int) unit0[slot].side == PLAYER_SIDE
            && (int) unit0[slot].flags == 0) {
            seen14_party_live_slots++;
        }
    }

    seen14_samurai =
        count_live_units(unit0, seen14_unit_count, CH13_SAMURAI_CHAR_ID,
                         CH13_SAMURAI_LEVEL);
    seen14_fliers =
        count_live_units(unit0, seen14_unit_count, CH13_FLIER_CHAR_ID,
                         CH13_FLIER_LEVEL);
    seen14_archers =
        count_live_units(unit0, seen14_unit_count, CH13_ARCHER_CHAR_ID,
                         CH13_ARCHER_LEVEL);
    seen14_dark_mages =
        count_live_units(unit0, seen14_unit_count, CH13_DARK_MAGE_CHAR_ID,
                         CH13_DARK_MAGE_LEVEL);
    seen14_wolves =
        count_live_units(unit0, seen14_unit_count, CH13_WOLF_CHAR_ID,
                         CH13_WOLF_LEVEL);
    seen14_live_enemies = seen14_samurai + seen14_fliers + seen14_archers
                          + seen14_dark_mages + seen14_wolves;

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen14_marked_timers[slot] = 0;
        seen14_unit0_timers[slot] = unit0->status_timers[slot];
    }

    /* The last unit and the marker both sit at index 28, which exists only if
       both DEPLOY_WAVEs went down in full.  A run that opened a decoy stops at
       nine units, and reading past the array to say so would be reading memory
       the allocation does not cover -- so the three fields are left at values
       no correct run produces and the cases below fail on those instead. */
    seen14_last_unit_char_id = -1;
    seen14_last_unit_level = -1;
    seen14_last_unit_side = -1;
    if (seen14_unit_count > SCRIPT_CH14_MARKER_UNIT) {
        marked = unit0 + SCRIPT_CH14_MARKER_UNIT;
        seen14_last_unit_char_id = (int) marked->char_id;
        seen14_last_unit_level = (int) marked->level;
        seen14_last_unit_side = (int) marked->side;
        for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
            seen14_marked_timers[slot] = marked->status_timers[slot];
        }
    }
}

/* Runs the chapter 14 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The nine members the
   party has when the chapter opens are put on with the game's own add, in join
   order, because this handler adds nobody and MAP13.DAT's nine player slots
   have to be filled from a roster the run staged honestly.  The timer hook and
   the graphics mode are here for the reasons the file comment gives. */
static void run_chapter_14_handler(void)
{
    int member;

    if (run14_state != 0) {
        return;
    }
    run14_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_14_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_14; member++) {
        fdps_roster_add_character(chapter_14_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_14_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_14();
    free_chapter_globals();
    run14_state = 1;
}

/* Nobody joins the party this chapter and no player slot is left over.  The
   roster count is the nine the run put on and not ten, which is what says the
   handler has no fdps_roster_add_character in it; the nine player slots carry
   those same nine characters in join order, every one of them on the player
   side with the retired bit clear, so none of them is the zeroed spare
   fdps_build_map_unit_array writes for a slot with no member behind it.
   MAP13.DAT's own two header counts are read back as well because they were
   staged at numbers it does not carry. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_fourteen(void)
{
    int slot;

    run_chapter_14_handler();
    CHECK_EQ(run14_state, 1);
    if (run14_state != 1) {
        return;
    }

    CHECK_EQ(seen14_roster_count, PARTY_AT_CHAPTER_14);
    CHECK_EQ(seen14_player_slots, CH13_PLAYER_SLOTS);
    CHECK_EQ(seen14_char_spawns, CH13_CHAR_SPAWNS);
    for (slot = 0; slot < PARTY_AT_CHAPTER_14; slot++) {
        CHECK_EQ(seen14_party_char_ids[slot], chapter_14_party[slot]);
    }
    CHECK_EQ(seen14_party_live_slots, PARTY_AT_CHAPTER_14);
}

/* What the reset and the cut-scene together leave on the map: the nine player
   slots, nothing from a wave-0 tag because MAP13.DAT has none, and the twenty
   the two DEPLOY_WAVEs put down, for twenty-nine.  Those twenty are the
   strategy guide's opening 敵方 group counted by character id and level -- five
   LV13 武士, six LV12 飛兵, five LV11 弓箭手 and four LV13 暗魔導士 -- and the
   wolf the guide puts on the map at turn six is NOT among them, because it is
   one of the seventeen records MAP13.DAT keys wave 4 and no DEPLOY_WAVE here
   asks for that wave.  The last unit is read back by index as well: index 28
   is MAP13.DAT's record 19, the last of wave 1, and it exists only if both
   waves went down in the shipped order. */
static void chapter_fourteen_opens_with_the_guides_enemy_group(void)
{
    run_chapter_14_handler();
    CHECK_EQ(run14_state, 1);
    if (run14_state != 1) {
        return;
    }

    CHECK_EQ(seen14_unit_count, CH13_UNITS_AFTER_SCRIPT);

    CHECK_EQ(seen14_samurai, CH13_SAMURAI_COUNT);
    CHECK_EQ(seen14_fliers, CH13_FLIER_COUNT);
    CHECK_EQ(seen14_archers, CH13_ARCHER_COUNT);
    CHECK_EQ(seen14_dark_mages, CH13_DARK_MAGE_COUNT);
    CHECK_EQ(seen14_wolves, CH13_WOLF_COUNT);
    CHECK_EQ(seen14_live_enemies, CH13_GUIDE_ENEMY_TOTAL);

    CHECK_EQ(seen14_last_unit_char_id, CH13_FLIER_CHAR_ID);
    CHECK_EQ(seen14_last_unit_level, CH13_FLIER_LEVEL);
    CHECK_EQ(seen14_last_unit_side, ENEMY_SIDE);
}

/* The cut-scene the handler names is Icon13.dat and not the neighbour on
   either side of it.  The marker sits on unit 28 with the value only
   ICON13.DAT writes, an index no other member of the container reaches:
   ICON14.DAT, chapter 15's own script, deploys nobody and marks unit 0, so a
   run that opened it would land nine units and a marked unit 0, and
   ICON12.DAT, chapter 13's own script, carries no DEPLOY_WAVE at all and would
   have left the array at those same nine.  Unit 0's own timers are read back
   clear as well.  Every other timer on the marked unit is clear too: neither
   the handler nor the shipped cut-scene writes a status on anybody this
   chapter -- the shipped ICON13.DAT has no SET_UNIT_TIMER at all, which is why
   the marker here is the fixture's own addition.  The chapter id is still 13,
   which the handler neither reads nor writes and which this member cannot
   disturb either -- it carries no SWITCH_MAP -- and both the script number and
   the title-card graphic are chosen from it by the callees. */
static void the_chapter_14_cutscene_is_icon13_dat(void)
{
    int slot;

    run_chapter_14_handler();
    CHECK_EQ(run14_state, 1);
    if (run14_state != 1) {
        return;
    }

    CHECK_EQ(seen14_marked_timers[SCRIPT_CH14_MARKER_SLOT],
             SCRIPT_CH14_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH14_MARKER_SLOT) {
            CHECK_EQ(seen14_marked_timers[slot], 0);
        }
        CHECK_EQ(seen14_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen14_chapter_id, CHAPTER_14_ID);
}

/* The cursor ends on unit 0's tile, and unit 0 is where the cut-scene left it
   rather than where the map put it.  MAP13.COD record 37 -- the first record
   past the map's thirty-seven scripted deployments -- puts player slot 0 on
   tile (20, 4), and the script's PLACE_UNIT for unit 0 then moves it to
   (7, 9); the cursor globals are that second tile scaled by the 24-pixel step,
   which only a handler that ran the cursor call after the script can produce.
   Both globals were staged at 48 and 72 before the run. */
static void the_chapter_14_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_14_handler();
    CHECK_EQ(run14_state, 1);
    if (run14_state != 1) {
        return;
    }

    CHECK_EQ(seen14_cursor_x, SCRIPT_CH14_WALKED_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen14_cursor_y, SCRIPT_CH14_WALKED_TILE_Y * CURSOR_TILE_STEP);
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

void run_chinit1b_tests(void)
{
    RUN_TEST(qinqin_joins_before_the_chapter_is_built);
    RUN_TEST(chapter_eleven_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_11_cutscene_is_icon10_dat);
    RUN_TEST(the_chapter_11_cursor_is_parked_on_unit_zero);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_twelve);
    RUN_TEST(chapter_twelve_opens_on_the_one_room_boss);
    RUN_TEST(the_chapter_12_cutscene_is_icon11_dat);
    RUN_TEST(the_chapter_12_cursor_is_parked_on_unit_zero);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_thirteen);
    RUN_TEST(chapter_thirteen_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_13_cutscene_is_icon12_dat);
    RUN_TEST(the_chapter_13_cursor_is_parked_on_unit_zero);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_fourteen);
    RUN_TEST(chapter_fourteen_opens_with_the_guides_enemy_group);
    RUN_TEST(the_chapter_14_cutscene_is_icon13_dat);
    RUN_TEST(the_chapter_14_cursor_is_parked_on_unit_zero);
    RUN_TEST(the_fixture_container_is_removed);
}
