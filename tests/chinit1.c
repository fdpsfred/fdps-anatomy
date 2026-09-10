/* tests/chinit1.c -- cover for src/chinit1.c.
 *
 * One entry handler per section, each with a run of its own that enters its
 * chapter once for real; the shared staging, the fixture container and the
 * two helpers that free the chapter globals sit at the top and are used by
 * every one of them.  fdps_chapter_01_init is first, then
 * fdps_chapter_02_init, then fdps_chapter_03_init, then
 * fdps_chapter_04_init, then fdps_chapter_05_init, then
 * fdps_chapter_06_init, then fdps_chapter_07_init, then
 * fdps_chapter_08_init, then fdps_chapter_09_init, then
 * fdps_chapter_10_init.
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
#include "roster.h"
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
#define FIXTURE_MEMBERS 10

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

/* --- what the chapter 3 run below expects ------------------------------- */

/* Chapter 3 is chapter id 2, and this handler is slot 2 of the table at
   00060074: the dword at 0006007c is 00020f30. */
#define CHAPTER_03_ID 2

/* The character PUSH 0x4 at 00020f3c puts on the roster: 亞克 the 騎士,
   character id 4 (assets/characters.md). */
#define ARC_CHAR_ID 4

/* The roster slot he lands in.  The run below puts 蘭迪斯 and 尤利安 on the
   roster first, because that is the party the game has when chapter 3 opens
   and because MAP02.DAT asks for three player slots -- the handler's own add
   is what fills the third. */
#define ARC_ROSTER_SLOT 2
#define ARC_UNIT 2

/* 亞克's own line out of FRIAPRDA.DAT and FRILEVUP.DAT
   (assets/characters.md): level 7, 64 base HP, 10 HP a level, so the record
   fdps_roster_add_character builds carries 124.  The strategy guide's opening
   line for the chapter says the same number -- LV7 騎士亞克, HP124 -- so this
   expectation is the shipped data and the guide agreeing, not an arithmetic
   the test invented. */
#define ARC_LEVEL 7
#define ARC_HP_BASE 64
#define ARC_HP_MIN 10
#define ARC_HP_MAX (ARC_HP_BASE + ARC_HP_MIN * (ARC_LEVEL - 1))

/* MAP02.DAT's own header bytes, read back to prove chapter 3's map is the one
   that loaded: three player slots at +1 and 80 scripted deployments at +2,
   against MAP00.DAT's 1 and 22 and MAP01.DAT's 2 and 27. */
#define CH2_PLAYER_SLOTS 3
#define CH2_CHAR_SPAWNS 80

/* Unlike chapters 1 and 2, MAP02.DAT tags two of its 80 deployments wave 0, so
   the chapter state reset's own opening deploy puts two units down before the
   cut-scene runs: record 0 is side 1, character 12, level 10 -- 索爾, the
   guest hero the guide lists as 友方 for this chapter -- and record 1 is the
   side-0 LV8 魔導士 the chapter is won by killing.  They land behind the three
   player slots, so 索爾 is unit 3. */
#define CH2_WAVE_ZERO_UNITS 2
#define CH2_GUEST_UNIT 3

/* Which wave the fixture ICON02.DAT below asks for and how many records
   MAP02.DAT tags with it: wave 10 is records 15, 16 and 78.  It is a wave
   neither of the other two fixtures names, so the unit count alone says which
   member the interpreter opened -- ICON00.DAT's wave 2 then wave 1 would add
   nine of this map's records and ICON01.DAT's wave 3 would add two. */
#define SCRIPT_CH03_WAVE 10
#define CH2_SCRIPT_WAVE_UNITS 3

/* How long the unit array is when the handler returns: the three player slots,
   the map's own two wave-0 records, and the three the cut-scene deploys. */
#define CH2_UNITS_AFTER_SCRIPT \
    (CH2_PLAYER_SLOTS + CH2_WAVE_ZERO_UNITS + CH2_SCRIPT_WAVE_UNITS)

/* What ICON02.DAT's SET_UNIT_TIMER writes, and where.  It marks unit 4 -- the
   second of the map's own wave-0 records, an index no other map in this file
   reaches -- and the value is one neither of the other two fixtures writes.

   THE SLOT IS NOT FREE TO CHOOSE.  fdps_unit_select_status_icon reads five of
   the six status bytes, record 0x22, 0x23, 0x24, 0x25 and 0x27, and a unit with
   any of those set draws a status icon out of
   data_fdps_unit_status_icon_sheet_ptr -- a ticket 23 global the build links
   zero-filled, which sends fdps_draw_map_unit's icon blit through a null sheet
   the moment the walk brings that unit into the view.  status_timers[4],
   record 0x26, is the one byte that selector does not read, so it is the slot a
   marker can use; the opcode's operand is measured from status_timers[3]
   (src/icon.c), which makes that operand 1. */
#define SCRIPT_CH03_MARKER_UNIT 4
#define SCRIPT_CH03_MARKER_OPERAND 1
#define SCRIPT_CH03_MARKER_SLOT 4
#define SCRIPT_CH03_MARKER_VALUE 9

/* MAP02.COD record 80 -- the placement behind the map's 80 scripted ones, and
   so the first party slot's start tile -- is (25, 17).  The record a party
   slot is put on is data_fdps_map_char_spawn_count + slot_index and not the
   slot number (src/deploy.c). */
#define CH2_PARTY_TILE_X 25
#define CH2_PARTY_TILE_Y 17

/* --- what the chapter 4 run below expects ------------------------------- */

/* Chapter 4 is chapter id 3, and this handler is slot 3 of the table at
   00060074: the dword at 00060080 is 00020f70. */
#define CHAPTER_04_ID 3

/* The character PUSH 0x1 at 00020f7c puts on the roster: 法蓮娜 the 魔導士,
   character id 1 (assets/characters.md). */
#define FLARENA_CHAR_ID 1

/* The roster slot she lands in.  The run below puts 蘭迪斯, 尤利安 and 亞克
   on the roster first, because that is the party the game has when chapter 4
   opens -- chapters 1 to 3 are the only other callers of
   fdps_roster_add_character the game reaches on the way here.  Slot 3 is one
   PAST the three player slots MAP03.DAT asks for, which is the whole point of
   the case below: she joins the roster and does NOT become a map unit. */
#define FLARENA_ROSTER_SLOT 3

/* 法蓮娜's own line out of FRIAPRDA.DAT and FRILEVUP.DAT
   (assets/characters.md): level 3, 42 base HP, 6 HP a level, so the roster
   record fdps_roster_add_character builds carries 54 at level 3.

   That is deliberately NOT the LV8 魔導士法蓮娜 HP84 the strategy guide lists
   for this chapter, and the difference is the point: the guide's line is
   MAP03.DAT's own deployment record 21 -- side 2, character 1, level 8 -- and
   the base table's level 3 is a template the roster add copies, not her join
   level (assets/characters.md says so of this very character).  The
   expectation here is what the handler's own add computes and nothing else. */
#define FLARENA_LEVEL 3
#define FLARENA_HP_BASE 42
#define FLARENA_HP_MIN 6
#define FLARENA_HP_MAX (FLARENA_HP_BASE + FLARENA_HP_MIN * (FLARENA_LEVEL - 1))

/* MAP03.DAT's own header bytes, read back to prove chapter 4's map is the one
   that loaded: three player slots at +1 and 33 scripted deployments at +2.
   The pair is MAP02.DAT's slot count with a spawn count no other map here
   carries, against MAP00.DAT's 1 and 22 and MAP01.DAT's 2 and 27. */
#define CH3_PLAYER_SLOTS 3
#define CH3_CHAR_SPAWNS 33

/* MAP03.DAT tags exactly one of its 33 deployments wave 0 -- record 0, side 1,
   character 12, level 10, the guest hero 索爾 -- so the chapter state reset's
   own opening deploy puts one unit down behind the three player slots. */
#define CH3_WAVE_ZERO_UNITS 1
#define CH3_GUEST_UNIT 3

/* Which wave the fixture ICON03.DAT below asks for and how many records
   MAP03.DAT tags with it: wave 5 is records 23, 24, 25, 26, 29, 30 and 31.
   It is a wave none of the other three fixtures names, so the unit count alone
   says which member the interpreter opened -- ICON00.DAT's wave 2 then wave 1
   would add five then fifteen of this map's records, ICON01.DAT's wave 3 would
   add one and ICON02.DAT's wave 10 none.  Every one of the seven is an enemy
   id between 88 and 102, so all seven index the staged enemy table inside its
   TABLE_ROWS rows; MAP03.DAT's wave 4 record is character 145 and would not,
   which is why that wave is not the one named here. */
#define SCRIPT_CH04_WAVE 5
#define CH3_SCRIPT_WAVE_UNITS 7

/* How long the unit array is when the handler returns: the three player slots,
   the map's one wave-0 record, and the seven the cut-scene deploys. */
#define CH3_UNITS_AFTER_SCRIPT \
    (CH3_PLAYER_SLOTS + CH3_WAVE_ZERO_UNITS + CH3_SCRIPT_WAVE_UNITS)

/* What ICON03.DAT's SET_UNIT_TIMER writes, and where.  It marks unit 8 -- an
   index that exists only because this map's wave 5 is seven records long, and
   so an index no other run in this file reaches -- with a value none of the
   other three fixtures writes.  The slot is status_timers[4], record 0x26, for
   the reason the chapter 3 fixture gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1. */
#define SCRIPT_CH04_MARKER_UNIT 8
#define SCRIPT_CH04_MARKER_OPERAND 1
#define SCRIPT_CH04_MARKER_SLOT 4
#define SCRIPT_CH04_MARKER_VALUE 12

/* MAP03.COD record 33 -- the placement behind the map's 33 scripted ones, and
   so the first party slot's start tile -- is (5, 23).  The record a party slot
   is put on is data_fdps_map_char_spawn_count + slot_index and not the slot
   number (src/deploy.c). */
#define CH3_PARTY_TILE_X 5
#define CH3_PARTY_TILE_Y 23

/* --- what the chapter 5 run below expects ------------------------------- */

/* Chapter 5 is chapter id 4, and this handler is slot 4 of the table at
   00060074: the dword at 00060084 is 00020fb0. */
#define CHAPTER_05_ID 4

/* How many members the party has when chapter 5 opens, and what an untouched
   roster slot holds.  fdps_roster_add_character is called from the chapter
   handlers alone -- chapters 1 to 4 and then chapters 7, 8, 9, 11, 15, 19 and
   24 -- so the four the run below puts on with the game's own add are the whole
   party at this point.  The handler adds nobody, and slot 4 keeping the 0xff
   stage_globals wrote there is the assertion that says so. */
#define PARTY_AT_CHAPTER_05 4
#define SPARE_ROSTER_SLOT 4
#define EMPTY_ROSTER_SLOT_SENTINEL 0xff

/* MAP04.DAT's own header bytes, read back to prove chapter 5's map is the one
   that loaded: FIVE player slots at +1 and 33 scripted deployments at +2.  The
   slot count is one no other map in this file carries -- MAP00.DAT's 1,
   MAP01.DAT's 2, MAP02.DAT's and MAP03.DAT's 3 -- and it is one MORE than the
   party has members, which is what makes the fifth slot a spare. */
#define CH4_PLAYER_SLOTS 5
#define CH4_CHAR_SPAWNS 33

/* MAP04.DAT tags exactly one of its 33 deployments wave 0 -- record 30, side
   1, character 12, level 10, the guest hero 索爾 the strategy guide lists as
   chapter 5's 友方 -- so the chapter state reset's own opening deploy puts one
   unit down behind the five player slots, at index 5. */
#define CH4_WAVE_ZERO_UNITS 1
#define CH4_GUEST_UNIT 5

/* Which wave the fixture ICON04.DAT below asks for, and what MAP04.DAT tags
   with it: wave 2 is deployment record 32 alone -- side 0, character 91, level
   8.  The record is the witness rather than the count, because a wave number
   on its own is not enough here: ICON01.DAT's wave 3 is also exactly one
   record of this map (record 31, character 101), so the run reads the deployed
   unit's own character id back.  ICON00.DAT would deploy wave 2 AND then wave
   1, twenty-seven more records; ICON02.DAT's wave 10 and ICON03.DAT's wave 5
   match nothing MAP04.DAT carries. */
#define SCRIPT_CH05_WAVE 2
#define CH4_SCRIPT_WAVE_UNITS 1
#define CH4_SCRIPT_UNIT 6
#define CH4_SCRIPT_UNIT_CHAR_ID 91
#define CH4_SCRIPT_UNIT_LEVEL 8

/* The side byte a deployment record carries for the map's own troops, against
   the 1 the guest hero carries and the 2 a roster member is put on. */
#define ENEMY_SIDE 0

/* How long the unit array is when the handler returns: the five player slots,
   the map's one wave-0 record, and the one the cut-scene deploys. */
#define CH4_UNITS_AFTER_SCRIPT     (CH4_PLAYER_SLOTS + CH4_WAVE_ZERO_UNITS + CH4_SCRIPT_WAVE_UNITS)

/* What ICON04.DAT's SET_UNIT_TIMER writes, and where.  It marks the unit its
   own DEPLOY_WAVE has just put down, with a value none of the other four
   fixtures writes.  The slot is status_timers[4], record 0x26, for the reason
   the chapter 3 fixture gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1.

   The SHIPPED ICON04.DAT writes the same slot on the guest hero instead --
   walked with src/icon.c's ladder its SET_UNIT_TIMER at script offset 74 is
   unit 5, operand 1, value 0xff, the 麻痺狀態 the strategy guide prints for
   索爾 in this chapter.  That is the answer to where chapter 5's one status
   write comes from, given this handler has no store in its body: the
   cut-scene, not the handler. */
#define SCRIPT_CH05_MARKER_UNIT 6
#define SCRIPT_CH05_MARKER_OPERAND 1
#define SCRIPT_CH05_MARKER_SLOT 4
#define SCRIPT_CH05_MARKER_VALUE 14

/* MAP04.COD record 33 -- the first record past the map's 33 scripted ones, and
   so the first party slot's start tile -- is (6, 21).  The record a party slot
   is put on is data_fdps_map_char_spawn_count + slot_index and not the slot
   number (src/deploy.c). */
#define CH4_PARTY_TILE_X 6
#define CH4_PARTY_TILE_Y 21

/* --- what the chapter 6 run below expects ------------------------------- */

/* Chapter 6 is chapter id 5, and this handler is slot 5 of the table at
   00060074: the dword at 00060088 is 00020ff0. */
#define CHAPTER_06_ID 5

/* How many members the party has when chapter 6 opens.  Chapter 5's handler
   added nobody and this one adds nobody either, so the party is still the four
   the first four handlers put on, and roster slot 4 still carries the 0xff
   stage_globals wrote there. */
#define PARTY_AT_CHAPTER_06 4

/* MAP05.DAT's own header bytes, read back to prove chapter 6's map is the one
   that loaded: FOUR player slots at +1 and 32 scripted deployments at +2.  The
   pair is one no other map in this file carries -- MAP00.DAT's 1 and 22,
   MAP01.DAT's 2 and 27, MAP02.DAT's 3 and 80, MAP03.DAT's 3 and 33,
   MAP04.DAT's 5 and 33.

   Four slots against a four-member party is the point of the first case: this
   is the first map in the run whose slot count and party size agree, so unlike
   chapter 5's fifth slot there is no retired spare anywhere in the array. */
#define CH5_PLAYER_SLOTS 4
#define CH5_CHAR_SPAWNS 32

/* MAP05.DAT's 32 deployment records are tagged in three waves -- two wave 0,
   twenty-three wave 1, seven wave 2 -- and the byte the tag is read from is
   spawn record +0x15 (struct fdps_char_spawn_record's wave_no, src/fdpstype.h).
   The chapter state reset's own opening deploy takes wave 0 alone, so two
   records go down behind the four player slots before the cut-scene is
   reached. */
#define CH5_WAVE_ZERO_UNITS 2

/* The last player slot, the one chapter 5's map left as a spare and this one
   fills. */
#define CH5_LAST_PLAYER_SLOT 3

/* Deployment record 31 -- side 1, character 12, level 10, the guest hero
   索爾 the strategy guide lists as chapter 6's 友方 -- is the second of the
   map's two wave-0 records, and the deploy walks the table in record order, so
   he lands at index 4 + 1 behind record 0's LV10 步兵. */
#define CH5_GUEST_UNIT 5

/* The census the strategy guide prints for this chapter and MAP05.DAT's own
   wave tagging are the same list read from two sides.  The guide gives the
   opposition in TWO groups -- LV10 魔導士 x3, LV10 步兵 x9, LV9 弓兵 x7,
   LV8 騎兵 x5, and then x1, x3, x2, x1 -- and the split is the map's, not the
   guide's presentation: the first group is the map's wave 0 and wave 1 counted
   together by character id and the second is its wave 2 exactly.  What is on
   the map when this handler returns is therefore the FIRST group, because the
   reset deployed wave 0 and the cut-scene wave 1.

   The four ids are all above the enemy id base of 60 and all below the
   TABLE_ROWS rows staged for the enemy table, so every one of them deploys. */
#define CH5_FOOT_CHAR_ID 98
#define CH5_FOOT_COUNT 9
#define CH5_ARCHER_CHAR_ID 93
#define CH5_ARCHER_COUNT 7
#define CH5_RIDER_CHAR_ID 88
#define CH5_RIDER_COUNT 5
#define CH5_MAGE_CHAR_ID 102
#define CH5_MAGE_COUNT 3

/* Which wave the fixture ICON05.DAT below asks for, and what the shipped
   member asks for: wave 1, with the place-exact operand 0.  Both are read off
   the shipped ICON05.DAT itself -- walked with the opcode ladder in src/icon.c,
   its one DEPLOY_WAVE at script offset 83 carries those two operands -- and
   MAP05.DAT tags twenty-three of its records wave 1, so the cut-scene is what
   puts the chapter's army on the map.

   That makes the unit count a witness of which member the interpreter opened,
   which it was not on chapter 5's map: ICON00.DAT deploys wave 2 and then wave
   1, which here is 7 + 23 for 36 units; ICON04.DAT's wave 2 alone is 13;
   ICON01.DAT's wave 3, ICON02.DAT's wave 10 and ICON03.DAT's wave 5 match
   nothing this map carries and leave 6. */
#define SCRIPT_CH06_WAVE 1
#define SCRIPT_CH06_PLACE_EXACT 0
#define CH5_SCRIPT_WAVE_UNITS 23

/* How long the unit array is when the handler returns: the four player slots,
   the map's two wave-0 records, and the twenty-three the cut-scene deploys. */
#define CH5_UNITS_AFTER_SCRIPT \
    (CH5_PLAYER_SLOTS + CH5_WAVE_ZERO_UNITS + CH5_SCRIPT_WAVE_UNITS)

/* What ICON05.DAT's SET_UNIT_TIMER writes, and where.  It marks the LAST unit
   its own DEPLOY_WAVE puts down -- wave 1's twenty-three records land in record
   order at indices 6 to 28, so index 28 is the map's record 30 -- with a value
   none of the other five fixtures writes.  The case below reads that unit's
   character id back as well, so the index is pinned to a record of the map and
   not merely to a number, and an index of 28 exists at all only because the
   cut-scene deployed: a run that opened any other member leaves the array too
   short to reach it.

   The slot is status_timers[4], record 0x26, for the reason the chapter 3
   fixture gives: it is the one status byte fdps_unit_select_status_icon does
   not read, so marking it cannot send fdps_draw_map_unit through the null
   status-icon sheet.  The opcode's operand is measured from status_timers[3]
   (src/icon.c), which makes that operand 1.

   The SHIPPED ICON05.DAT has no SET_UNIT_TIMER anywhere in its 99 bytes, which
   is the other half of the case below: chapter 5's 索爾 opens 麻痺 because the
   shipped ICON04.DAT paralyses him, and chapter 6's opens clean because
   neither the handler nor its cut-scene writes on a unit.  The guide's line for
   him this chapter carries no status either. */
#define SCRIPT_CH06_MARKER_UNIT 28
#define SCRIPT_CH06_MARKER_UNIT_CHAR_ID 98
#define SCRIPT_CH06_MARKER_OPERAND 1
#define SCRIPT_CH06_MARKER_SLOT 4
#define SCRIPT_CH06_MARKER_VALUE 21

/* MAP05.COD record 32 -- the first record past the map's 32 scripted ones, and
   so the first party slot's start tile -- is (4, 8).  The record a party slot
   is put on is data_fdps_map_char_spawn_count + slot_index and not the slot
   number (src/deploy.c). */
#define CH5_PARTY_TILE_X 4
#define CH5_PARTY_TILE_Y 8

/* --- what the chapter 7 run below expects ------------------------------- */

/* Chapter 7 is chapter id 6, and this handler is slot 6 of the table at
   00060074: the dword at 0006008c is 00021030. */
#define CHAPTER_07_ID 6

/* The character PUSH 0x3 at 0002103c puts on the roster: 裘娜 the 戰士,
   character id 3 (assets/characters.md), and the slot she lands in.  The run
   below puts 蘭迪斯, 尤利安, 亞克 and 法蓮娜 on first, because that is the
   party the game has when chapter 7 opens -- chapters 1 to 4 add one each and
   chapters 5 and 6 add nobody -- so slot 4 is hers and it is one PAST the four
   player slots MAP06.DAT asks for. */
#define JUNA_CHAR_ID 3
#define JUNA_ROSTER_SLOT 4

/* 裘娜's own line out of FRIAPRDA.DAT and FRILEVUP.DAT
   (assets/characters.md): level 15, 85 base HP, 10 HP a level, so the roster
   record fdps_roster_add_character builds carries 225 at level 15.

   Unlike 法蓮娜's, this one IS the strategy guide's line for the chapter: the
   guide's 加入 for chapter 7 is LV15 戰士裘娜 at HP225 carrying 鐵刀 and
   青鎧甲, which is the same level, the same total and item ids 0x0f and 0x66
   (assets/items.md).  The guide and the two data tables are checked against
   each other here rather than either being taken on trust. */
#define JUNA_LEVEL 15
#define JUNA_HP_BASE 85
#define JUNA_HP_MIN 10
#define JUNA_HP_MAX (JUNA_HP_BASE + JUNA_HP_MIN * (JUNA_LEVEL - 1))

/* How many members the party has when chapter 7 opens, before the handler's
   own add.  The four the run puts on with the game's own add are the whole
   party at this point. */
#define PARTY_AT_CHAPTER_07 4

/* MAP06.DAT's own header bytes, read back to prove chapter 7's map is the one
   that loaded: four player slots at +1 and FIVE scripted deployments at +2.
   Five is a spawn count no other map in this file carries -- MAP00.DAT's 22,
   MAP01.DAT's 27, MAP02.DAT's 80, MAP03.DAT's and MAP04.DAT's 33, MAP05.DAT's
   32 -- and it is the whole of the chapter's opposition, the map being 261
   bytes: the 0x83-byte header and exactly five 0x1a-byte records
   (src/deploy.c). */
#define CH6_PLAYER_SLOTS 4
#define CH6_CHAR_SPAWNS 5

/* MAP06.DAT tags NONE of its five deployments wave 0, so the chapter state
   reset's own opening deploy matches nothing and the array it leaves is the
   four player slots and no more.  This is the first map in the file with no
   wave-0 record at all, and it is why the unit count below is the player slots
   plus the cut-scene's own two waves and nothing between them. */
#define CH6_WAVE_ZERO_UNITS 0

/* The last player slot.  MAP06.DAT wants four and the party is four, so like
   chapter 6's map every slot has a member behind it and none is the retired
   spare -- the 裘娜 this handler adds is one slot further on and never reaches
   the array. */
#define CH6_LAST_PLAYER_SLOT 3

/* The five records themselves, which are the strategy guide's 敵方 line for
   the chapter read from the map file: record 0 is side 0, character 116, level
   15 -- the guide's LV15 裘娜 -- and records 1 to 4 are side 0, character 86,
   level 14, the guide's LV14 傭兵 x4.  What ties character 116 to the roster
   character 3 the handler adds is her equipment: the record carries item ids
   0x0f and 0x66, the 鐵刀 and 青鎧甲 pair FRIAPRDA.DAT gives character 3 and
   the guide prints on both lines.  Both ids are above the enemy id base of 60
   and below the TABLE_ROWS rows staged for the enemy table, so both deploy. */
#define CH6_BOSS_CHAR_ID 116
#define CH6_BOSS_LEVEL 15
#define CH6_MERC_CHAR_ID 86
#define CH6_MERC_LEVEL 14
#define CH6_MERC_COUNT 4

/* Which waves the fixture ICON06.DAT below asks for, and which the shipped
   member asks for: wave 1 and then wave 2, both with the place-exact operand
   0.  All four operands are read off the shipped ICON06.DAT itself -- walked
   with the opcode ladder in src/icon.c, its DEPLOY_WAVE at script offset 228
   is wave 1 and the one at offset 274 is wave 2 -- and MAP06.DAT tags its four
   傭兵 wave 1 and 裘娜 wave 2, so the cut-scene is what puts every enemy on
   the map.

   The pair is one no other fixture names: ICON00.DAT deploys wave 2 and THEN
   wave 1, which is the same five records in the opposite order and so a
   different character id at index 4; ICON04.DAT's wave 2 alone is one record;
   ICON05.DAT's wave 1 alone is four; ICON01.DAT's wave 3, ICON02.DAT's wave 10
   and ICON03.DAT's wave 5 match nothing this map carries. */
#define SCRIPT_CH07_FIRST_WAVE 1
#define SCRIPT_CH07_SECOND_WAVE 2
#define SCRIPT_CH07_PLACE_EXACT 0
#define CH6_SCRIPT_WAVE_UNITS 5

/* How long the unit array is when the handler returns: the four player slots,
   no wave-0 record at all, and the five the cut-scene deploys.  It is also
   exactly as many placement records as MAP06.COD carries -- 63 bytes is nine
   6-byte records behind a 9-byte header (src/deploy.c) -- five for the
   scripted deployments and four for the party's start tiles. */
#define CH6_UNITS_AFTER_SCRIPT \
    (CH6_PLAYER_SLOTS + CH6_WAVE_ZERO_UNITS + CH6_SCRIPT_WAVE_UNITS)

/* Where the two waves land.  The first appends the four 傭兵 at indices 4 to
   7 and the second appends 裘娜 behind them at index 8, so the marker below
   goes on the unit the SECOND DEPLOY_WAVE put down -- an index that exists
   only if both waves ran, and a character id that is 裘娜's only if they ran
   in the shipped order.

   The value is one none of the other six fixtures writes.  The slot is
   status_timers[4], record 0x26, for the reason the chapter 3 fixture gives:
   it is the one status byte fdps_unit_select_status_icon does not read, so
   marking it cannot send fdps_draw_map_unit through the null status-icon
   sheet.  The opcode's operand is measured from status_timers[3]
   (src/icon.c), which makes that operand 1.

   The SHIPPED ICON06.DAT has no SET_UNIT_TIMER anywhere in its 557 bytes,
   which is the other half of the case below: chapter 5's guest hero opens
   麻痺 because the shipped ICON04.DAT paralyses him, and chapter 7 opens with
   every unit on whatever its deployment computed, because neither this handler
   -- which has no store in its body -- nor its cut-scene writes on one. */
#define CH6_FIRST_MERC_UNIT 4
#define SCRIPT_CH07_MARKER_UNIT 8
#define SCRIPT_CH07_MARKER_OPERAND 1
#define SCRIPT_CH07_MARKER_SLOT 4
#define SCRIPT_CH07_MARKER_VALUE 26

/* MAP06.COD record 5 -- the first record past the map's five scripted ones,
   and so the first party slot's start tile -- is (11, 17).  The record a party
   slot is put on is data_fdps_map_char_spawn_count + slot_index and not the
   slot number (src/deploy.c). */
#define CH6_PARTY_TILE_X 11
#define CH6_PARTY_TILE_Y 17

/* --- what the chapter 8 run below expects ------------------------------- */

/* Chapter 8 is chapter id 7, and this handler is slot 7 of the table at
   00060074: the dword at 00060090 is 00021070. */
#define CHAPTER_08_ID 7

/* The character PUSH 0x2 at 0002107c puts on the roster: 費塔加 the 魔導士,
   character id 2 (assets/characters.md), and the slot he lands in.  The run
   below puts 蘭迪斯, 尤利安, 亞克, 法蓮娜 and 裘娜 on first, because that is
   the party the game has when chapter 8 opens -- chapters 1 to 4 add one each,
   chapters 5 and 6 add nobody and chapter 7 adds 裘娜 -- so slot 5 is his and
   it is one PAST the five player slots MAP07.DAT asks for. */
#define FEITAGA_CHAR_ID 2
#define FEITAGA_ROSTER_SLOT 5

/* 費塔加's own line out of FRIAPRDA.DAT and FRILEVUP.DAT
   (assets/characters.md): level 13, 78 base HP and 65 base MP, 6 HP and 7 MP a
   level, so the roster record fdps_roster_add_character builds carries 150 HP
   and 149 MP at level 13.

   This one is NOT the strategy guide's line for the chapter, and chapter 8 is
   the first place the two part company.  The guide's 加入 is LV15 魔導士費塔加
   at HP162, MP163, DX38 carrying 光之杖 and 祭司袍, which is its own 友軍 line
   for the chapter repeated -- and that line is MAP07.DAT's deployment 19, side
   1, character 2, level 15, item ids 0x33 and 0x84.  The same two tables at
   level 15 give 78 + 6 * 14 = 162, 65 + 7 * 14 = 163 and a DX of 8 + 2 * 15 =
   38, so the guide's three numbers are the map record read through
   fdps_deploy_unit and not the roster record read through
   fdps_roster_add_character.  The MP is staged here as well as the HP for that
   reason: at level 13 it is 149 and at level 15 it is 163, so the two records
   are told apart twice over. */
#define FEITAGA_LEVEL 13
#define FEITAGA_HP_BASE 78
#define FEITAGA_HP_MIN 6
#define FEITAGA_MP_BASE 65
#define FEITAGA_MP_MIN 7
#define FEITAGA_HP_MAX (FEITAGA_HP_BASE + FEITAGA_HP_MIN * (FEITAGA_LEVEL - 1))
#define FEITAGA_MP_MAX (FEITAGA_MP_BASE + FEITAGA_MP_MIN * (FEITAGA_LEVEL - 1))

/* How many members the party has when chapter 8 opens, before the handler's
   own add.  The five the run puts on with the game's own add are the whole
   party at this point. */
#define PARTY_AT_CHAPTER_08 5

/* MAP07.DAT's own header bytes, read back to prove chapter 8's map is the one
   the run ends on: five player slots at +1 and 36 scripted deployments at +2,
   against MAP48.DAT's 0 and 6.  The pair is the whole point of the census
   below, because the cut-scene the handler runs spends most of itself on map
   48 -- a run that had stopped there would read 0 and 6 back instead. */
#define CH7_PLAYER_SLOTS 5
#define CH7_CHAR_SPAWNS 36

/* The last player slot.  MAP07.DAT wants five and the party is five, so every
   slot has a member behind it and none is the retired spare -- the 費塔加 this
   handler adds is one slot further on and never reaches the array. */
#define CH7_LAST_PLAYER_SLOT 4

/* How many of MAP07.DAT's 36 deployments are tagged wave 0, and so how many
   the state reset's own opening deploy appends behind the player slots:
   records 2, 3, 5, 8, 9, 11, 12, 15, 17, 18 and 20 to 23.  They land at unit
   indices 5 to 18 in that order. */
#define CH7_WAVE_ZERO_UNITS 14

/* How long the unit array is when the handler returns: the five player slots
   and the fourteen wave-0 records, and nothing from the cut-scene at all --
   the one DEPLOY_WAVE in ICON07.DAT runs while the chapter id is 48, and the
   SWITCH_MAP that follows it rebuilds map 7 from scratch.  Nineteen is also
   how the two files divide up: MAP07.COD is 255 bytes, which is 41 six-byte
   records behind a nine-byte header (src/deploy.c), 36 for the scripted
   deployments and 5 for the party's start tiles. */
#define CH7_UNITS_AFTER_SCRIPT (CH7_PLAYER_SLOTS + CH7_WAVE_ZERO_UNITS)

/* Two of the fourteen, read back to say the wave-0 deploy really ran and ran
   in file order: the first is deployment 2, side 0, character 93, level 12,
   and the last is deployment 23, side 1, character 119, level 12.  Both
   character ids are above the enemy id base of 60 and below the TABLE_ROWS
   rows staged for the enemy table, so both deploy. */
#define CH7_FIRST_WAVE_ZERO_UNIT 5
#define CH7_FIRST_WAVE_ZERO_CHAR_ID 93
#define CH7_LAST_WAVE_ZERO_UNIT 18
#define CH7_LAST_WAVE_ZERO_CHAR_ID 119
#define CH7_LAST_WAVE_ZERO_LEVEL 12
#define CH7_LAST_WAVE_ZERO_SIDE 1

/* What the fixture ICON07.DAT below asks for, and what the shipped member asks
   for: switch to map 48, deploy that map's wave 1 with the place-exact operand
   1, switch back to map 7.  All three are read off the shipped ICON07.DAT
   itself, walked with the opcode ladder in src/icon.c -- SWITCH_MAP 0x30 at
   script offset 2, DEPLOY_WAVE 1 with operand 1 at offset 107, SWITCH_MAP 0x07
   at offset 160 -- and no DEPLOY_WAVE follows the second switch.

   MAP48.DAT is the cut-scene map that pair brackets: no player slot at all and
   six deployments, the five party members at level 2 tagged wave 0 and 費塔加
   at level 2 tagged wave 1.  So the deploy brings on the actor the shipped
   script then walks, retires and revives, and the second switch throws the
   whole array away again. */
#define SCRIPT_CH08_CUTSCENE_MAP 48
#define SCRIPT_CH08_CUTSCENE_WAVE 1
#define SCRIPT_CH08_CUTSCENE_PLACE 1
#define SCRIPT_CH08_RETURN_MAP 7

/* Where the marker goes and what it says.  Unit 18 is the last of the fourteen
   wave-0 records, an index the array reaches only if the second SWITCH_MAP
   rebuilt map 7 -- on the cut-scene map the array is six units long -- and the
   value is one none of the other seven fixtures writes.  The slot is
   status_timers[4], record 0x26, for the reason the chapter 3 fixture gives:
   it is the one status byte fdps_unit_select_status_icon does not read, so
   marking it cannot send fdps_draw_map_unit through the null status-icon
   sheet.  The opcode's operand is measured from status_timers[3]
   (src/icon.c), which makes that operand 1.

   The SHIPPED ICON07.DAT has no SET_UNIT_TIMER anywhere in its 251 bytes,
   which is the other half of the case below: chapter 8 opens with every unit
   on whatever its deployment computed, because neither this handler -- which
   has no store in its body -- nor its cut-scene writes on one. */
#define SCRIPT_CH08_MARKER_UNIT 18
#define SCRIPT_CH08_MARKER_OPERAND 1
#define SCRIPT_CH08_MARKER_SLOT 4
#define SCRIPT_CH08_MARKER_VALUE 31

/* MAP07.COD record 36 -- the first record past the map's 36 scripted ones, and
   so the first party slot's start tile -- is (3, 2).  The record a party slot
   is put on is data_fdps_map_char_spawn_count + slot_index and not the slot
   number (src/deploy.c). */
#define CH7_PARTY_TILE_X 3
#define CH7_PARTY_TILE_Y 2

/* --- what the chapter 9 run below expects ------------------------------- */

/* Chapter 9 is chapter id 8, and this handler is slot 8 of the table at
   00060074: the dword at 00060094 is 000210b0. */
#define CHAPTER_09_ID 8

/* The two characters this handler puts on the roster and the slots they land
   in: PUSH 0x8 at 000210bc is 布蘭多 the 技師 and PUSH 0x9 at 000210c6 is
   蓋亞 the 機兵 (assets/characters.md).  The run below puts 蘭迪斯, 尤利安,
   亞克, 法蓮娜, 裘娜 and 費塔加 on first, because that is the party the game
   has when chapter 9 opens, so slots 6 and 7 are theirs -- and unlike every
   chapter before this one, those are slots MAP08.DAT actually asks for. */
#define BRANDO_CHAR_ID 8
#define GAIA_CHAR_ID 9
#define BRANDO_ROSTER_SLOT 6
#define GAIA_ROSTER_SLOT 7

/* Their own lines out of FRIAPRDA.DAT and FRILEVUP.DAT
   (assets/characters.md): 布蘭多 is level 14 on 50 base HP and 0 base MP with
   9 HP and 3 MP a level, 蓋亞 is level 16 on 60 base HP and 0 base MP with 12
   HP and 3 MP a level.  fdps_roster_add_character computes base + min *
   (level - 1), so the records it builds carry 167 HP with 39 MP and 240 HP
   with 45 MP.

   Unlike chapter 8's, these two ARE the strategy guide's line for the chapter:
   its 己方 is LV14 技師布蘭多 HP167 MP39 and LV16 機兵蓋亞 HP240 MP45, the
   same four numbers.  So this pair of expectations is the shipped tables and
   the guide agreeing, and no MAP08.DAT record carries either character. */
#define BRANDO_LEVEL 14
#define BRANDO_HP_BASE 50
#define BRANDO_HP_MIN 9
#define BRANDO_MP_BASE 0
#define BRANDO_MP_MIN 3
#define BRANDO_HP_MAX (BRANDO_HP_BASE + BRANDO_HP_MIN * (BRANDO_LEVEL - 1))
#define BRANDO_MP_MAX (BRANDO_MP_BASE + BRANDO_MP_MIN * (BRANDO_LEVEL - 1))

#define GAIA_LEVEL 16
#define GAIA_HP_BASE 60
#define GAIA_HP_MIN 12
#define GAIA_MP_BASE 0
#define GAIA_MP_MIN 3
#define GAIA_HP_MAX (GAIA_HP_BASE + GAIA_HP_MIN * (GAIA_LEVEL - 1))
#define GAIA_MP_MAX (GAIA_MP_BASE + GAIA_MP_MIN * (GAIA_LEVEL - 1))

/* How many members the party has when chapter 9 opens, before the handler's
   own two adds: chapters 1 to 4 add one each, chapters 5 and 6 add nobody,
   chapter 7 adds 裘娜 and chapter 8 adds 費塔加. */
#define PARTY_AT_CHAPTER_09 6

/* MAP08.DAT's own header bytes, read back to prove chapter 9's map is the one
   the run ends on: eight player slots at +1 and 31 scripted deployments at +2,
   against MAP54.DAT's 0 and 16 and MAP55.DAT's 0 and 3.  A run that had
   stopped on either cut-scene map would read one of those pairs instead. */
#define CH8_PLAYER_SLOTS 8
#define CH8_CHAR_SPAWNS 31

/* The two slots the handler's own adds fill.  MAP08.DAT wants eight and the
   party is eight only once both adds have run, so these are the first player
   slots in the file whose occupants this handler is responsible for. */
#define CH8_BRANDO_UNIT 6
#define CH8_GAIA_UNIT 7

/* Where the two of them open the chapter.  A party slot is put on placement
   record data_fdps_map_char_spawn_count + slot_index (src/deploy.c), so
   MAP08.COD records 37 and 38 are theirs: (23, 12) and (24, 13).  Reading
   both back is what pins the ORDER of the two adds -- swapping the calls
   swaps these two tiles and nothing else about the run changes. */
#define CH8_BRANDO_TILE_X 23
#define CH8_BRANDO_TILE_Y 12
#define CH8_GAIA_TILE_X 24
#define CH8_GAIA_TILE_Y 13

/* How many of MAP08.DAT's 31 deployments are tagged wave 0, and so how many
   the state reset's own opening deploy appends behind the eight player slots:
   every record except deployment 1, which is tagged 0xff, and deployments 25
   to 30, which are tagged wave 1.  Those six are the guide's 援軍 line and no
   opcode the handler runs deploys them. */
#define CH8_WAVE_ZERO_UNITS 24

/* How long the unit array is when the handler returns: the eight player slots
   and the twenty-four wave-0 records, and nothing from the cut-scene at all --
   its one DEPLOY_WAVE runs while the chapter id is 55, and the SWITCH_MAP
   behind it rebuilds map 8 from scratch.  Thirty-two is also how the two files
   divide up: MAP08.COD is 243 bytes, which is 39 six-byte records behind a
   nine-byte header (src/deploy.c), 31 for the scripted deployments and 8 for
   the party's start tiles. */
#define CH8_UNITS_AFTER_SCRIPT (CH8_PLAYER_SLOTS + CH8_WAVE_ZERO_UNITS)

/* Two of the twenty-four, read back to say the wave-0 deploy really ran and
   ran in file order: the first is deployment 0, side 0, character 93, level
   13, and the last is deployment 24, side 0, character 76, level 13.  Both ids
   are above the enemy id base of 60 and inside the TABLE_ROWS rows staged for
   the enemy table. */
#define CH8_FIRST_WAVE_ZERO_UNIT 8
#define CH8_FIRST_WAVE_ZERO_CHAR_ID 93
#define CH8_LAST_WAVE_ZERO_UNIT 31
#define CH8_LAST_WAVE_ZERO_CHAR_ID 76
#define CH8_LAST_WAVE_ZERO_LEVEL 13
#define CH8_LAST_WAVE_ZERO_SIDE 0

/* What the fixture ICON08.DAT below asks for, and what the shipped member asks
   for: switch to map 54, switch to map 55, deploy that map's wave 1 with the
   place-exact operand 1, switch back to map 8.  All four are read off the
   shipped ICON08.DAT itself, walked with the opcode ladder in src/icon.c --
   SWITCH_MAP 0x36 at script offset 4, SWITCH_MAP 0x37 at offset 169,
   DEPLOY_WAVE 1 with operand 1 at offset 295, SWITCH_MAP 0x08 at offset 421 --
   and no DEPLOY_WAVE follows the last switch.

   MAP54.DAT and MAP55.DAT are the cut-scene maps that run brackets: neither
   has a player slot, MAP54.DAT's sixteen wave-0 actors are the party and nine
   scene-only characters all at level 2, and MAP55.DAT holds 布蘭多 and
   character 155 as wave 0 with 蓋亞 as its one wave-1 record.  So the deploy
   brings on the 蓋亞 stand-in the shipped script then walks, and the last
   switch throws the whole array away again. */
#define SCRIPT_CH09_FIRST_MAP 54
#define SCRIPT_CH09_SECOND_MAP 55
#define SCRIPT_CH09_CUTSCENE_WAVE 1
#define SCRIPT_CH09_CUTSCENE_PLACE 1
#define SCRIPT_CH09_RETURN_MAP 8

/* Where the marker goes and what it says.  Unit 31 is the last of the
   twenty-four wave-0 records, an index the array reaches only if the last
   SWITCH_MAP rebuilt map 8 -- on the two cut-scene maps the array is sixteen
   and three units long -- and the value is one none of the other eight
   fixtures writes.  The slot is status_timers[4], record 0x26, for the reason
   the chapter 3 fixture gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1.

   The SHIPPED ICON08.DAT has no SET_UNIT_TIMER anywhere in its 885 bytes,
   which is the other half of the case below: chapter 9 opens with every unit
   on whatever its deployment computed, because neither this handler -- which
   has no store in its body -- nor its cut-scene writes on one. */
#define SCRIPT_CH09_MARKER_UNIT 31
#define SCRIPT_CH09_MARKER_OPERAND 1
#define SCRIPT_CH09_MARKER_SLOT 4
#define SCRIPT_CH09_MARKER_VALUE 37

/* MAP08.COD record 31 -- the first record past the map's 31 scripted ones, and
   so the first party slot's start tile -- is (3, 18). */
#define CH8_PARTY_TILE_X 3
#define CH8_PARTY_TILE_Y 18

/* --- what the chapter 10 run below expects ------------------------------ */

/* Chapter 10 is chapter id 9, and this handler is slot 9 of the table at
   00060074: the dword at 00060098 is 00021100. */
#define CHAPTER_10_ID 9

/* How many members the party has when chapter 10 opens, and so how many the
   run below puts on the roster with the game's own add.  Chapters 1 to 4 add
   one each, chapters 5 and 6 add nobody, chapter 7 adds 裘娜, chapter 8 adds
   費塔加 and chapter 9 adds 布蘭多 and 蓋亞: eight.  This handler adds nobody,
   and the count still reading eight when it returns is the assertion that says
   so.  There is no spare roster slot to read the 0xff sentinel out of the way
   chapters 5 and 6 do -- eight members fill every slot the run stages. */
#define PARTY_AT_CHAPTER_10 8

/* MAP09.DAT's own header bytes, read back to prove chapter 10's map is the one
   that loaded: eight player slots at +1 and 48 scripted deployments at +2.
   The slot count is MAP08.DAT's, but the spawn count is one no other map this
   file touches carries -- MAP08.DAT's 31, MAP37.DAT's 12 and MAP38.DAT's 11 --
   so the pair says both that a chapter loaded and which one. */
#define CH9_PLAYER_SLOTS 8
#define CH9_CHAR_SPAWNS 48

/* MAP09.DAT tags eight of its 48 deployments wave 0 -- records 0 to 7 -- so
   the chapter state reset's own opening deploy puts eight units down behind
   the eight player slots, at indices 8 to 15.  The other forty records are
   waves 1 to 11, the reinforcements the strategy guide lists against turns 3,
   6 to 13 and 19, and nothing in this chapter's opening deploys them. */
#define CH9_WAVE_ZERO_UNITS 8

/* How long the unit array is when the handler returns: the eight player slots
   and the map's eight wave-0 records.  The shipped ICON09.DAT deploys nothing
   at all, so unlike every run above there is no cut-scene wave in this
   number. */
#define CH9_UNITS_AFTER_SCRIPT (CH9_PLAYER_SLOTS + CH9_WAVE_ZERO_UNITS)

/* The census behind that count, and it is the strategy guide's opening 敵方
   group for the chapter to the number: four LV13 步兵, two LV13 騎兵 and two
   LV15 魔導士.  The class-to-character-id mapping is the one the chapter 6 run
   above uses -- 98 步兵, 93 弓兵, 88 騎兵, 102 魔導士.  The archer count is
   asserted at zero on purpose: MAP09.DAT's wave 1 carries two of them, so a
   cut-scene that had deployed that wave would show up here. */
#define CH9_FOOT_COUNT 4
#define CH9_RIDER_COUNT 2
#define CH9_MAGE_COUNT 2
#define CH9_ARCHER_COUNT 0

/* The first and the last of those eight, read back by index because wave 0 is
   deployed in file order onto its own tiles: record 0 is a LV13 步兵 and
   record 7 a LV15 魔導士, both side 0.  Unit 15 is also the index the marker
   below lands on. */
#define CH9_FIRST_WAVE_ZERO_UNIT 8
#define CH9_FIRST_WAVE_ZERO_LEVEL 13
#define CH9_LAST_WAVE_ZERO_UNIT 15
#define CH9_LAST_WAVE_ZERO_LEVEL 15

/* What the fixture ICON09.DAT below asks for, and what the shipped member asks
   for: switch to the cut-scene map 37, switch to the cut-scene map 38, switch
   back to map 9.  All three are read off the shipped ICON09.DAT itself, walked
   with the opcode ladder in src/icon.c -- SWITCH_MAP 0x25 at script offset 4,
   SWITCH_MAP 0x26 at offset 227, SWITCH_MAP 0x09 at offset 701 -- and there is
   no DEPLOY_WAVE anywhere in its 854 bytes, which is true of no other chapter
   script this file stages.

   MAP37.DAT and MAP38.DAT are the cut-scene maps the run brackets: both ask
   for eight player slots and tag every one of their deployments wave 0 --
   twelve and eleven level-2 stand-ins -- so their arrays are twenty and
   nineteen units long against map 9's sixteen, and the last switch throwing
   both away is what the count and the marker below say. */
#define SCRIPT_CH10_FIRST_MAP 37
#define SCRIPT_CH10_SECOND_MAP 38
#define SCRIPT_CH10_RETURN_MAP 9

/* Where the marker goes and what it says.  Unit 15 is the last of the eight
   wave-0 records, and what it carries is what says the last SWITCH_MAP ran:
   on the two cut-scene maps index 15 is a level-2 stand-in -- character 90 on
   map 37 and character 98 on map 38 -- and on map 9 it is the LV15 魔導士 the
   case below reads back beside the marker.  The value is one none of the other
   nine fixtures writes.  The slot is status_timers[4], record 0x26, for the
   reason the chapter 3 fixture gives: it is the one status byte
   fdps_unit_select_status_icon does not read, so marking it cannot send
   fdps_draw_map_unit through the null status-icon sheet.  The opcode's operand
   is measured from status_timers[3] (src/icon.c), which makes that operand 1.

   The SHIPPED ICON09.DAT has no SET_UNIT_TIMER anywhere either, which is the
   other half of the case below: chapter 10 opens with every unit on whatever
   its deployment computed, because neither this handler -- which has no store
   in its body -- nor its cut-scene writes on one. */
#define SCRIPT_CH10_MARKER_UNIT 15
#define SCRIPT_CH10_MARKER_OPERAND 1
#define SCRIPT_CH10_MARKER_SLOT 4
#define SCRIPT_CH10_MARKER_VALUE 43

/* MAP09.COD record 48 -- the first record past the map's 48 scripted ones, and
   so the first party slot's start tile -- is (3, 0).  The party slots of this
   map share tiles between them, which is the shipped data and not a misread: a
   player slot is put on its placement record exactly, with no free-tile search
   (src/deploy.c). */
#define CH9_PARTY_TILE_X 3
#define CH9_PARTY_TILE_Y 0

/* Table rows wide enough for every id the run touches: character 12 indexes
   the roster tables directly, and every item id including 0xff is a valid
   index because fdps_unit_recompute_combat_stats follows the equipped flag
   into fdps_get_item_record with no bound of any kind.

   The enemy table is indexed by char_id - 0x3c (src/deploy.c), and the
   chapter 9 run below walks the two cut-scene maps ICON08.DAT switches to:
   MAP55.DAT deploys character 155, which is row 95, and MAP54.DAT reaches row
   75.  64 rows would have sent both of those past the end of the array. */
#define TABLE_ROWS 128
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

/* ICON02.DAT: the member fdps_chapter_03_init names.  Its DEPLOY_WAVE asks for
   wave 10, which on MAP02.DAT is three records and is a wave neither of the
   other two fixtures names, and its SET_UNIT_TIMER marker writes a value of its
   own onto a unit index only this chapter's map reaches.  So a run that opened
   ICON00.DAT or ICON01.DAT instead is a different unit count AND a different
   timer array. */
static unsigned char fixture_icon02_dat[] = {
    0x04, SCRIPT_CH03_WAVE, 0x01,
    0x12, SCRIPT_CH03_MARKER_UNIT, SCRIPT_CH03_MARKER_OPERAND,
    SCRIPT_CH03_MARKER_VALUE,
    0x00
};

/* ICON03.DAT: the member fdps_chapter_04_init names.  Its DEPLOY_WAVE asks for
   wave 5, which on MAP03.DAT is seven records and is a wave none of the other
   three fixtures names, and its SET_UNIT_TIMER marker writes a value of its own
   onto a unit index only a seven-record wave reaches.  So a run that opened any
   of the other three instead is a different unit count AND a different timer
   array. */
static unsigned char fixture_icon03_dat[] = {
    0x04, SCRIPT_CH04_WAVE, 0x01,
    0x12, SCRIPT_CH04_MARKER_UNIT, SCRIPT_CH04_MARKER_OPERAND,
    SCRIPT_CH04_MARKER_VALUE,
    0x00
};

/* ICON04.DAT: the member fdps_chapter_05_init names.  Its DEPLOY_WAVE asks for
   wave 2, which on MAP04.DAT is deployment record 32 alone, and its
   SET_UNIT_TIMER marker writes a value of its own onto the unit that deploy has
   just appended.  ICON01.DAT's wave 3 is also one record of this map, so it is
   the deployed unit's character id and the marker -- not the unit count -- that
   say which member the interpreter opened. */
static unsigned char fixture_icon04_dat[] = {
    0x04, SCRIPT_CH05_WAVE, 0x01,
    0x12, SCRIPT_CH05_MARKER_UNIT, SCRIPT_CH05_MARKER_OPERAND,
    SCRIPT_CH05_MARKER_VALUE,
    0x00
};

/* ICON05.DAT: the member fdps_chapter_06_init names.  Its DEPLOY_WAVE is the
   shipped member's own -- wave 1, place-exact 0 -- which on MAP05.DAT is
   twenty-three records, and its SET_UNIT_TIMER marker then writes a value of
   its own onto the last unit that deploy appended.  No other fixture names wave
   1 alone, so a run that opened one of the other five is a different unit count
   AND a different timer array. */
static unsigned char fixture_icon05_dat[] = {
    0x04, SCRIPT_CH06_WAVE, SCRIPT_CH06_PLACE_EXACT,
    0x12, SCRIPT_CH06_MARKER_UNIT, SCRIPT_CH06_MARKER_OPERAND,
    SCRIPT_CH06_MARKER_VALUE,
    0x00
};

/* ICON06.DAT: the member fdps_chapter_07_init names.  Its two DEPLOY_WAVEs are
   the shipped member's own, in the shipped order -- wave 1 then wave 2, both
   place-exact 0 -- which on MAP06.DAT is the four 傭兵 and then 裘娜, and its
   SET_UNIT_TIMER marker then writes a value of its own onto the unit the
   second of them appended.  ICON00.DAT names the same two waves the other way
   round, so it is the character id at index 8 and not the unit count that
   tells the two apart. */
static unsigned char fixture_icon06_dat[] = {
    0x04, SCRIPT_CH07_FIRST_WAVE, SCRIPT_CH07_PLACE_EXACT,
    0x04, SCRIPT_CH07_SECOND_WAVE, SCRIPT_CH07_PLACE_EXACT,
    0x12, SCRIPT_CH07_MARKER_UNIT, SCRIPT_CH07_MARKER_OPERAND,
    SCRIPT_CH07_MARKER_VALUE,
    0x00
};

/* ICON07.DAT: the member fdps_chapter_08_init names.  Its three
   state-changing opcodes are the shipped member's own, in its order and with
   its operands -- switch to the cut-scene map 48, deploy that map's wave 1
   place-exact, switch back to map 7 -- and its SET_UNIT_TIMER marker then
   writes a value of its own onto the last unit the map-7 rebuild deployed.
   The shipped member's other forty-odd opcodes walk, pose and light the actors
   and are left out for the reason the runs above give.

   It is the only fixture here that names a SWITCH_MAP, so a run that opened
   one of the other seven never leaves chapter 8's map and lands a different
   unit count AND a different timer array. */
static unsigned char fixture_icon07_dat[] = {
    0x11, SCRIPT_CH08_CUTSCENE_MAP,
    0x04, SCRIPT_CH08_CUTSCENE_WAVE, SCRIPT_CH08_CUTSCENE_PLACE,
    0x11, SCRIPT_CH08_RETURN_MAP,
    0x12, SCRIPT_CH08_MARKER_UNIT, SCRIPT_CH08_MARKER_OPERAND,
    SCRIPT_CH08_MARKER_VALUE,
    0x00
};

/* ICON08.DAT: the member fdps_chapter_09_init names.  Its four
   state-changing opcodes are the shipped member's own, in its order and with
   its operands -- switch to the cut-scene map 54, switch to the cut-scene map
   55, deploy that map's wave 1 place-exact, switch back to map 8 -- and its
   SET_UNIT_TIMER marker then writes a value of its own onto the last unit the
   map-8 rebuild deployed.  The shipped member's other hundred-odd opcodes
   walk, pose, shake and light the actors and are left out for the reason the
   runs above give.

   It is the only fixture here that names two cut-scene maps, and the one
   fixture whose marker lands as far out as unit 31, so a run that opened one
   of the other eight lands a different unit count AND a different timer
   array. */
static unsigned char fixture_icon08_dat[] = {
    0x11, SCRIPT_CH09_FIRST_MAP,
    0x11, SCRIPT_CH09_SECOND_MAP,
    0x04, SCRIPT_CH09_CUTSCENE_WAVE, SCRIPT_CH09_CUTSCENE_PLACE,
    0x11, SCRIPT_CH09_RETURN_MAP,
    0x12, SCRIPT_CH09_MARKER_UNIT, SCRIPT_CH09_MARKER_OPERAND,
    SCRIPT_CH09_MARKER_VALUE,
    0x00
};

/* ICON09.DAT: the member fdps_chapter_10_init names.  Its three
   state-changing opcodes are the shipped member's own, in its order and with
   its operands -- switch to the cut-scene map 37, switch to the cut-scene map
   38, switch back to map 9 -- and its SET_UNIT_TIMER marker then writes a
   value of its own onto the last unit the map-9 rebuild deployed.  The shipped
   member's other two hundred-odd opcodes walk, pose and light the actors and
   are left out for the reason the runs above give.

   It is the only fixture here with no DEPLOY_WAVE in it, which is the shipped
   member's own shape, so a run that opened one of the other nine lands a
   longer array AND a different timer array. */
static unsigned char fixture_icon09_dat[] = {
    0x11, SCRIPT_CH10_FIRST_MAP,
    0x11, SCRIPT_CH10_SECOND_MAP,
    0x11, SCRIPT_CH10_RETURN_MAP,
    0x12, SCRIPT_CH10_MARKER_UNIT, SCRIPT_CH10_MARKER_OPERAND,
    SCRIPT_CH10_MARKER_VALUE,
    0x00
};

static char *fixture_names[FIXTURE_MEMBERS] = {
    "ICON00.DAT", "ICON01.DAT", "ICON02.DAT", "ICON03.DAT", "ICON04.DAT",
    "ICON05.DAT", "ICON06.DAT", "ICON07.DAT", "ICON08.DAT", "ICON09.DAT"
};

static unsigned char *fixture_bytes[FIXTURE_MEMBERS] = {
    fixture_icon00_dat, fixture_icon01_dat, fixture_icon02_dat,
    fixture_icon03_dat, fixture_icon04_dat, fixture_icon05_dat,
    fixture_icon06_dat, fixture_icon07_dat, fixture_icon08_dat,
    fixture_icon09_dat
};

static int fixture_lengths[FIXTURE_MEMBERS] = {
    sizeof(fixture_icon00_dat), sizeof(fixture_icon01_dat),
    sizeof(fixture_icon02_dat), sizeof(fixture_icon03_dat),
    sizeof(fixture_icon04_dat), sizeof(fixture_icon05_dat),
    sizeof(fixture_icon06_dat), sizeof(fixture_icon07_dat),
    sizeof(fixture_icon08_dat), sizeof(fixture_icon09_dat)
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
    stage_char[ARC_CHAR_ID].level = (unsigned char) ARC_LEVEL;
    stage_char[ARC_CHAR_ID].hp_base = (short) ARC_HP_BASE;
    stage_growth[ARC_CHAR_ID].hp_min = (unsigned char) ARC_HP_MIN;
    stage_char[FLARENA_CHAR_ID].level = (unsigned char) FLARENA_LEVEL;
    stage_char[FLARENA_CHAR_ID].hp_base = (short) FLARENA_HP_BASE;
    stage_growth[FLARENA_CHAR_ID].hp_min = (unsigned char) FLARENA_HP_MIN;
    stage_char[JUNA_CHAR_ID].level = (unsigned char) JUNA_LEVEL;
    stage_char[JUNA_CHAR_ID].hp_base = (short) JUNA_HP_BASE;
    stage_growth[JUNA_CHAR_ID].hp_min = (unsigned char) JUNA_HP_MIN;
    stage_char[FEITAGA_CHAR_ID].level = (unsigned char) FEITAGA_LEVEL;
    stage_char[FEITAGA_CHAR_ID].hp_base = (short) FEITAGA_HP_BASE;
    stage_char[FEITAGA_CHAR_ID].mp_base = (short) FEITAGA_MP_BASE;
    stage_growth[FEITAGA_CHAR_ID].hp_min = (unsigned char) FEITAGA_HP_MIN;
    stage_growth[FEITAGA_CHAR_ID].mp_min = (unsigned char) FEITAGA_MP_MIN;
    stage_char[BRANDO_CHAR_ID].level = (unsigned char) BRANDO_LEVEL;
    stage_char[BRANDO_CHAR_ID].hp_base = (short) BRANDO_HP_BASE;
    stage_char[BRANDO_CHAR_ID].mp_base = (short) BRANDO_MP_BASE;
    stage_growth[BRANDO_CHAR_ID].hp_min = (unsigned char) BRANDO_HP_MIN;
    stage_growth[BRANDO_CHAR_ID].mp_min = (unsigned char) BRANDO_MP_MIN;
    stage_char[GAIA_CHAR_ID].level = (unsigned char) GAIA_LEVEL;
    stage_char[GAIA_CHAR_ID].hp_base = (short) GAIA_HP_BASE;
    stage_char[GAIA_CHAR_ID].mp_base = (short) GAIA_MP_BASE;
    stage_growth[GAIA_CHAR_ID].hp_min = (unsigned char) GAIA_HP_MIN;
    stage_growth[GAIA_CHAR_ID].mp_min = (unsigned char) GAIA_MP_MIN;

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

/* --- fdps_chapter_03_init @ 00020f30 ------------------------------------
 *
 * Five calls, straight line, no branch and no store of its own -- the same
 * shape as chapter 2's handler with two different arguments.  What it decides
 * is which character joins, the ORDER of the five calls, and which cut-scene
 * member and which unit the two arguments name, so the run below enters
 * chapter 3 once for real and reads the answers off the state it leaves.
 *
 * Expected values come from the assembly at 00020f30 and from the shipped
 * data, never from the emitted C:
 *
 *   PUSH 0x4 / CALL 0x00023bc0        character 4 joins the roster
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x6181c / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon02.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * WHY THE ROSTER IS NOT EMPTY THIS TIME.  The two runs above start from an
 * empty roster because their chapters are where 蘭迪斯 and 尤利安 join.  By
 * chapter 3 the party is those two plus the 亞克 this handler adds, and
 * MAP02.DAT asks for exactly three player slots, so the run below puts the
 * first two on the roster with the game's own fdps_roster_add_character before
 * calling the handler.  That is what makes the third slot the witness: a body
 * that reset the chapter before adding him leaves unit 2 as the zeroed,
 * retired spare fdps_build_map_unit_array writes for a player slot with no
 * member behind it, and the first two slots look right either way.
 *
 * WHAT THE MAP ITSELF DEPLOYS.  MAP02.DAT is the first of the three that tags
 * any deployment wave 0, so the chapter state reset puts two more units down
 * on its own: the guest hero 索爾 and the LV8 魔導士.  That is also why this
 * handler has no store in its body where chapter 1's has two -- chapter 1's
 * guest hero arrived from the cut-scene with nothing set on him, chapter 3's
 * arrives from the map file at the health his deployment computes.
 *
 * WHY THE CUT-SCENE IS A FIXTURE.  Same reason the two runs above give: the
 * shipped ICON02.DAT is a cinematic that switches maps, plays CD tracks and
 * draws through pointers a test image has not filled.  The staged ICON02.DAT
 * is three instructions -- a DEPLOY_WAVE for a wave the other two fixtures do
 * not name, a SET_UNIT_TIMER marker, and the terminator -- and the container it
 * lives in is the one the chapter 1 run already built.
 */

static int run3_state = 0;

static int seen3_roster_count;
static int seen3_roster_char_id;
static int seen3_player_slots;
static int seen3_char_spawns;
static int seen3_unit_count;
static int seen3_unit0_char_id;
static unsigned char seen3_unit0_timers[STATUS_TIMER_COUNT];
static int seen3_unit1_char_id;
static int seen3_arc_char_id;
static int seen3_arc_flags;
static int seen3_arc_side;
static int seen3_arc_level;
static int seen3_arc_hp_current;
static int seen3_arc_hp_max;
static int seen3_guest_char_id;
static int seen3_guest_level;
static int seen3_guest_side;
static int seen3_guest_hp_current;
static int seen3_guest_hp_max;
static unsigned char seen3_guest_timers[STATUS_TIMER_COUNT];
static unsigned char seen3_marked_timers[STATUS_TIMER_COUNT];
static int seen3_cursor_x;
static int seen3_cursor_y;
static int seen3_chapter_id;

static void capture_chapter_03(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *arc;
    struct fdps_unit_record *guest;
    struct fdps_unit_record *marked;
    int slot;

    seen3_roster_count = data_fdps_roster_member_count;
    seen3_roster_char_id = (int) stage_roster[ARC_ROSTER_SLOT].char_id;
    seen3_player_slots = data_fdps_map_player_slot_count;
    seen3_char_spawns = data_fdps_map_char_spawn_count;
    seen3_unit_count = data_fdps_map_unit_count;
    seen3_cursor_x = data_fdps_map_cursor_world_x;
    seen3_cursor_y = data_fdps_map_cursor_world_y;
    seen3_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    arc = unit0 + ARC_UNIT;
    guest = unit0 + CH2_GUEST_UNIT;
    marked = unit0 + SCRIPT_CH03_MARKER_UNIT;

    seen3_unit0_char_id = (int) unit0->char_id;
    seen3_unit1_char_id = (int) unit0[1].char_id;
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen3_unit0_timers[slot] = unit0->status_timers[slot];
    }

    seen3_arc_char_id = (int) arc->char_id;
    seen3_arc_flags = (int) arc->flags;
    seen3_arc_side = (int) arc->side;
    seen3_arc_level = (int) arc->level;
    seen3_arc_hp_current = (int) arc->hp_current;
    seen3_arc_hp_max = (int) arc->hp_max;

    seen3_guest_char_id = (int) guest->char_id;
    seen3_guest_level = (int) guest->level;
    seen3_guest_side = (int) guest->side;
    seen3_guest_hp_current = (int) guest->hp_current;
    seen3_guest_hp_max = (int) guest->hp_max;
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen3_guest_timers[slot] = guest->status_timers[slot];
        seen3_marked_timers[slot] = marked->status_timers[slot];
    }
}

/* Runs the chapter 3 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The two roster members
   the party already has when the chapter opens are put on with the game's own
   add, so the handler's own add is the only thing that can fill the map's
   third player slot.  The timer hook and the graphics mode are here for the
   reasons the chapter 1 run gives. */
static void run_chapter_03_handler(void)
{
    if (run3_state != 0) {
        return;
    }
    run3_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_03_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    fdps_roster_add_character(OPENING_CHAR_ID);
    fdps_roster_add_character(JOINING_CHAR_ID);

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_03_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_03();
    free_chapter_globals();
    run3_state = 1;
}

/* 亞克 is the third roster member and a live player-side unit on the map,
   which says the roster add ran BEFORE the state reset and that the argument
   it was handed is character 4.  His HP is the number the shipped tables and
   the strategy guide agree on, so a handler that added some other character
   into the same slot is visible here as well.  The two map counts are read
   back because they were staged at numbers no map carries and because
   MAP02.DAT's pair is neither MAP00.DAT's nor MAP01.DAT's. */
static void arc_joins_the_party_before_the_chapter_is_built(void)
{
    run_chapter_03_handler();
    CHECK_EQ(run3_state, 1);
    if (run3_state != 1) {
        return;
    }

    CHECK_EQ(seen3_roster_count, ARC_ROSTER_SLOT + 1);
    CHECK_EQ(seen3_roster_char_id, ARC_CHAR_ID);
    CHECK_EQ(seen3_player_slots, CH2_PLAYER_SLOTS);
    CHECK_EQ(seen3_char_spawns, CH2_CHAR_SPAWNS);
    CHECK_EQ(seen3_unit0_char_id, OPENING_CHAR_ID);
    CHECK_EQ(seen3_unit1_char_id, JOINING_CHAR_ID);
    CHECK_EQ(seen3_arc_char_id, ARC_CHAR_ID);
    CHECK_EQ(seen3_arc_flags, 0);
    CHECK_EQ(seen3_arc_side, PLAYER_SIDE);
    CHECK_EQ(seen3_arc_level, ARC_LEVEL);
    CHECK_EQ(seen3_arc_hp_max, ARC_HP_MAX);
    CHECK_EQ(seen3_arc_hp_current, ARC_HP_MAX);
}

/* The map's own wave-0 pair went down during the state reset, and the handler
   left both of them exactly as the deployment computed.  索爾 is unit 3 --
   side 1, character 12, level 10 -- at full HP with every status timer clear:
   chapter 1's handler poisons its guest hero and knocks his current HP down to
   100 after the cut-scene, and this one has no store in its body at all, so
   the same two fields are the assertion that says so. */
static void the_chapter_3_guest_hero_is_left_as_the_map_deployed_him(void)
{
    int slot;

    run_chapter_03_handler();
    CHECK_EQ(run3_state, 1);
    if (run3_state != 1) {
        return;
    }

    CHECK_EQ(seen3_guest_char_id, GUEST_HERO_CHAR_ID);
    CHECK_EQ(seen3_guest_level, GUEST_HERO_LEVEL);
    CHECK_EQ(seen3_guest_side, GUEST_HERO_SIDE);
    CHECK_EQ(seen3_guest_hp_max, SOL_HP_MAX);
    CHECK_EQ(seen3_guest_hp_current, SOL_HP_MAX);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        CHECK_EQ(seen3_guest_timers[slot], 0);
    }
}

/* The cut-scene the handler names is Icon02.dat and it really ran.  The unit
   count is the three player slots, the map's two wave-0 records and the three
   this member's DEPLOY_WAVE adds; ICON00.DAT would have added nine of this
   map's records and ICON01.DAT two.  Its SET_UNIT_TIMER marker is on unit 4 --
   an index that only exists because this map deployed its own wave 0 -- with a
   value neither of the other two fixtures writes, and unit 0's own timers are
   all clear, which is where ICON01.DAT's marker would have landed.  The chapter
   id is untouched, the handler neither reading nor writing it -- both the script
   number and the title-card graphic are chosen from it by the callees. */
static void the_chapter_3_cutscene_is_icon02_dat(void)
{
    int slot;

    run_chapter_03_handler();
    CHECK_EQ(run3_state, 1);
    if (run3_state != 1) {
        return;
    }

    CHECK_EQ(seen3_unit_count, CH2_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen3_marked_timers[SCRIPT_CH03_MARKER_SLOT],
             SCRIPT_CH03_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH03_MARKER_SLOT) {
            CHECK_EQ(seen3_marked_timers[slot], 0);
        }
        CHECK_EQ(seen3_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen3_chapter_id, CHAPTER_03_ID);
}

/* The cursor ends on unit 0's tile and not on the newcomer's.  The walk starts
   from the (0, 0) the state reset left, and MAP02.COD record 80 -- the first
   record past the map's 80 scripted deployments -- puts the first party slot
   on tile (25, 17), so the cursor globals are that tile scaled by the
   24-pixel step. */
static void the_chapter_3_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_03_handler();
    CHECK_EQ(run3_state, 1);
    if (run3_state != 1) {
        return;
    }

    CHECK_EQ(seen3_cursor_x, CH2_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen3_cursor_y, CH2_PARTY_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_04_init @ 00020f70 ------------------------------------
 *
 * Five calls, straight line, no branch and no store of its own -- the shape of
 * chapter 3's handler with two different arguments.  What it decides is which
 * character joins, the ORDER of the five calls, and which cut-scene member and
 * which unit the two arguments name, so the run below enters chapter 4 once for
 * real and reads the answers off the state it leaves.
 *
 * Expected values come from the assembly at 00020f70 and from the shipped
 * data, never from the emitted C:
 *
 *   PUSH 0x1 / CALL 0x00023bc0        character 1 joins the roster
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61828 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon03.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * WHAT MAKES THIS RUN DIFFERENT FROM THE THREE ABOVE.  In chapters 1 to 3 the
 * character the handler adds is the one that fills the map's last player slot,
 * so "the add ran before the reset" is visible as a live unit where a zeroed,
 * retired spare would otherwise be.  Chapter 4 is where that stops: MAP03.DAT
 * asks for three player slots, the party is already 蘭迪斯, 尤利安 and 亞克
 * when the chapter opens, and 法蓮娜 is roster slot 3 -- one past the last
 * slot fdps_build_map_unit_array fills.  The cases below assert exactly that
 * pair of facts: she IS on the roster with the record the add computes, and the
 * three map player slots are the three members who were already there.  A body
 * that added her after the reset would leave the same three units behind, which
 * is why nothing here claims the order is observable in chapter 4.
 *
 * WHERE SHE COMES ONTO THE MAP INSTEAD.  MAP03.DAT's deployment record 21 is
 * side 2, character 1, level 8 and is tagged wave 3, and the SHIPPED ICON03.DAT
 * is what asks for that wave: walked with the opcode ladder in src/icon.c its
 * bytes are SET_MUSIC, SET_VIEW_TILE, FACE_UNITS, PLAY_SAF and then
 * DEPLOY_WAVE 3 with exact placement at script offset 12.  That record is the
 * LV8 魔導士法蓮娜 the strategy guide lists, and it is the cut-scene call and
 * not the roster add that puts her down.  The FIXTURE below names wave 5
 * instead, for the reason every fixture here replaces a shipped script, so
 * nothing in this run deploys her and the roster record is the only place her
 * name appears -- which is what lets the cases separate the two roads.
 *
 * WHAT THE MAP ITSELF DEPLOYS.  MAP03.DAT tags one record wave 0 -- the guest
 * hero 索爾 again, side 1, character 12, level 10 -- so the chapter state reset
 * puts him down as unit 3, and the handler leaves him exactly as the deployment
 * computed: it has no store in its body where chapter 1's has two.
 *
 * WHY THE CUT-SCENE IS A FIXTURE.  Same reason the three runs above give: the
 * shipped ICON03.DAT is a cinematic that switches maps, plays CD tracks and
 * draws through pointers a test image has not filled.  The staged ICON03.DAT is
 * three instructions -- a DEPLOY_WAVE for a wave no other fixture names, a
 * SET_UNIT_TIMER marker, and the terminator -- and the container it lives in is
 * the one the chapter 1 run already built.
 */

static int run4_state = 0;

static int seen4_roster_count;
static int seen4_roster_char_id;
static int seen4_roster_level;
static int seen4_roster_side;
static int seen4_roster_hp_current;
static int seen4_roster_hp_max;
static int seen4_player_slots;
static int seen4_char_spawns;
static int seen4_unit_count;
static int seen4_unit0_char_id;
static int seen4_unit1_char_id;
static int seen4_unit2_char_id;
static int seen4_unit2_flags;
static int seen4_unit2_side;
static unsigned char seen4_unit0_timers[STATUS_TIMER_COUNT];
static int seen4_guest_char_id;
static int seen4_guest_level;
static int seen4_guest_side;
static int seen4_guest_hp_current;
static int seen4_guest_hp_max;
static unsigned char seen4_guest_timers[STATUS_TIMER_COUNT];
static unsigned char seen4_marked_timers[STATUS_TIMER_COUNT];
static int seen4_cursor_x;
static int seen4_cursor_y;
static int seen4_chapter_id;

static void capture_chapter_04(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *newcomer;
    struct fdps_unit_record *guest;
    struct fdps_unit_record *marked;
    int slot;

    newcomer = &stage_roster[FLARENA_ROSTER_SLOT];

    seen4_roster_count = data_fdps_roster_member_count;
    seen4_roster_char_id = (int) newcomer->char_id;
    seen4_roster_level = (int) newcomer->level;
    seen4_roster_side = (int) newcomer->side;
    seen4_roster_hp_current = (int) newcomer->hp_current;
    seen4_roster_hp_max = (int) newcomer->hp_max;

    seen4_player_slots = data_fdps_map_player_slot_count;
    seen4_char_spawns = data_fdps_map_char_spawn_count;
    seen4_unit_count = data_fdps_map_unit_count;
    seen4_cursor_x = data_fdps_map_cursor_world_x;
    seen4_cursor_y = data_fdps_map_cursor_world_y;
    seen4_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    guest = unit0 + CH3_GUEST_UNIT;
    marked = unit0 + SCRIPT_CH04_MARKER_UNIT;

    seen4_unit0_char_id = (int) unit0->char_id;
    seen4_unit1_char_id = (int) unit0[1].char_id;
    seen4_unit2_char_id = (int) unit0[2].char_id;
    seen4_unit2_flags = (int) unit0[2].flags;
    seen4_unit2_side = (int) unit0[2].side;

    seen4_guest_char_id = (int) guest->char_id;
    seen4_guest_level = (int) guest->level;
    seen4_guest_side = (int) guest->side;
    seen4_guest_hp_current = (int) guest->hp_current;
    seen4_guest_hp_max = (int) guest->hp_max;
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen4_unit0_timers[slot] = unit0->status_timers[slot];
        seen4_guest_timers[slot] = guest->status_timers[slot];
        seen4_marked_timers[slot] = marked->status_timers[slot];
    }
}

/* Runs the chapter 4 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The three roster members
   the party already has when the chapter opens are put on with the game's own
   add, which is what makes the map's three player slots full BEFORE the handler
   runs and so what makes "法蓮娜 is not one of them" an assertion rather than
   an accident.  The timer hook and the graphics mode are here for the reasons
   the chapter 1 run gives. */
static void run_chapter_04_handler(void)
{
    if (run4_state != 0) {
        return;
    }
    run4_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_04_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    fdps_roster_add_character(OPENING_CHAR_ID);
    fdps_roster_add_character(JOINING_CHAR_ID);
    fdps_roster_add_character(ARC_CHAR_ID);

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_04_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_04();
    free_chapter_globals();
    run4_state = 1;
}

/* 法蓮娜 is the fourth roster member, which says the handler's add ran and was
   handed character 1.  The record is the one fdps_roster_add_character computes
   out of FRIAPRDA.DAT's level 3 and FRILEVUP.DAT's 6 HP a level -- 54 HP, both
   fields, on the player side -- and not the level-8 line the strategy guide
   prints for this chapter, which belongs to MAP03.DAT's own deployment record.
   The two map counts are read back because they were staged at numbers no map
   carries and because MAP03.DAT's pair is none of the three above. */
static void flarena_joins_the_roster_when_chapter_four_opens(void)
{
    run_chapter_04_handler();
    CHECK_EQ(run4_state, 1);
    if (run4_state != 1) {
        return;
    }

    CHECK_EQ(seen4_roster_count, FLARENA_ROSTER_SLOT + 1);
    CHECK_EQ(seen4_roster_char_id, FLARENA_CHAR_ID);
    CHECK_EQ(seen4_roster_level, FLARENA_LEVEL);
    CHECK_EQ(seen4_roster_side, PLAYER_SIDE);
    CHECK_EQ(seen4_roster_hp_max, FLARENA_HP_MAX);
    CHECK_EQ(seen4_roster_hp_current, FLARENA_HP_MAX);
    CHECK_EQ(seen4_player_slots, CH3_PLAYER_SLOTS);
    CHECK_EQ(seen4_char_spawns, CH3_CHAR_SPAWNS);
}

/* And she is NOT one of chapter 4's map units.  MAP03.DAT's three player slots
   are filled from roster slots 0, 1 and 2 -- 蘭迪斯, 尤利安 and 亞克, the
   party that was already there -- and slot 3 is one past the last the map asks
   for.  The third slot is the witness: it is 亞克 and it is live, with the
   retired bit clear, which is what says the array was built from the roster the
   three chapters before this one had grown rather than from a roster this
   handler had just extended past the map's needs. */
static void flarena_is_not_one_of_the_chapter_four_map_units(void)
{
    run_chapter_04_handler();
    CHECK_EQ(run4_state, 1);
    if (run4_state != 1) {
        return;
    }

    CHECK_EQ(seen4_unit0_char_id, OPENING_CHAR_ID);
    CHECK_EQ(seen4_unit1_char_id, JOINING_CHAR_ID);
    CHECK_EQ(seen4_unit2_char_id, ARC_CHAR_ID);
    CHECK_EQ(seen4_unit2_flags, 0);
    CHECK_EQ(seen4_unit2_side, PLAYER_SIDE);
}

/* The map's own wave-0 record went down during the state reset, and the handler
   left it exactly as the deployment computed.  索爾 is unit 3 -- side 1,
   character 12, level 10 -- at full HP with every status timer clear: chapter
   1's handler poisons its guest hero and knocks his current HP down to 100
   after the cut-scene, and this one has no store in its body at all, so the
   same fields are the assertion that says so. */
static void the_chapter_4_guest_hero_is_left_as_the_map_deployed_him(void)
{
    int slot;

    run_chapter_04_handler();
    CHECK_EQ(run4_state, 1);
    if (run4_state != 1) {
        return;
    }

    CHECK_EQ(seen4_guest_char_id, GUEST_HERO_CHAR_ID);
    CHECK_EQ(seen4_guest_level, GUEST_HERO_LEVEL);
    CHECK_EQ(seen4_guest_side, GUEST_HERO_SIDE);
    CHECK_EQ(seen4_guest_hp_max, SOL_HP_MAX);
    CHECK_EQ(seen4_guest_hp_current, SOL_HP_MAX);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        CHECK_EQ(seen4_guest_timers[slot], 0);
    }
}

/* The cut-scene the handler names is Icon03.dat and it really ran.  The unit
   count is the three player slots, the map's one wave-0 record and the seven
   this member's DEPLOY_WAVE adds; ICON00.DAT would have added twenty of this
   map's records, ICON01.DAT one and ICON02.DAT none.  Its SET_UNIT_TIMER marker
   is on unit 8 -- an index only a seven-record wave reaches -- with a value none
   of the other three fixtures writes, and unit 0's own timers are all clear,
   which is where ICON01.DAT's marker would have landed.  The chapter id is
   untouched, the handler neither reading nor writing it -- both the script
   number and the title-card graphic are chosen from it by the callees. */
static void the_chapter_4_cutscene_is_icon03_dat(void)
{
    int slot;

    run_chapter_04_handler();
    CHECK_EQ(run4_state, 1);
    if (run4_state != 1) {
        return;
    }

    CHECK_EQ(seen4_unit_count, CH3_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen4_marked_timers[SCRIPT_CH04_MARKER_SLOT],
             SCRIPT_CH04_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH04_MARKER_SLOT) {
            CHECK_EQ(seen4_marked_timers[slot], 0);
        }
        CHECK_EQ(seen4_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen4_chapter_id, CHAPTER_04_ID);
}

/* The cursor ends on unit 0's tile.  The walk starts from the (0, 0) the state
   reset left, and MAP03.COD record 33 -- the first record past the map's 33
   scripted deployments -- puts the first party slot on tile (5, 23), so the
   cursor globals are that tile scaled by the 24-pixel step. */
static void the_chapter_4_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_04_handler();
    CHECK_EQ(run4_state, 1);
    if (run4_state != 1) {
        return;
    }

    CHECK_EQ(seen4_cursor_x, CH3_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen4_cursor_y, CH3_PARTY_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_05_init @ 00020fb0 ------------------------------------
 *
 * Four calls, straight line, no branch and no store of its own -- the handlers
 * above with their fdps_roster_add_character taken off the front.  What it
 * decides is that NOBODY joins the party this chapter, the ORDER of the four
 * calls, and which cut-scene member and which unit the two arguments name, so
 * the run below enters chapter 5 once for real and reads the answers off the
 * state it leaves.
 *
 * Expected values come from the assembly at 00020fb0 and from the shipped
 * data, never from the emitted C:
 *
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61834 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon04.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * There is no PUSH/CALL 0x00023bc0 anywhere in the body, and the map is what
 * makes that absence an assertion rather than a silence.  MAP04.DAT asks for
 * FIVE player slots while the party has four members -- 蘭迪斯, 尤利安, 亞克
 * and 法蓮娜, one from each of the four chapters before this one -- and
 * fdps_build_map_unit_array fills slot i from roster slot i only while i is
 * below the roster count, writing a zeroed record with the retired bit set for
 * the rest.  So a body that added a fifth character to fill that slot leaves a
 * live unit where this one leaves the spare, and the roster count and the
 * untouched 0xff in roster slot 4 say the same thing from the other side.
 *
 * WHAT WRITES ON A UNIT INSTEAD OF THE HANDLER.  The strategy guide lists
 * chapter 5's ally as LV10 英雄索爾 in 麻痺狀態.  He is MAP04.DAT's own
 * wave-0 record -- record 30, side 1, character 12, level 10 -- so the state
 * reset puts him down as unit 5, and the SHIPPED ICON04.DAT is what paralyses
 * him: walked with the opcode ladder in src/icon.c its SET_UNIT_TIMER at
 * script offset 74 names unit 5, timer slot operand 1 and value 0xff.  The
 * case below therefore asserts that the handler leaves that guest hero exactly
 * as the deployment computed him, every status byte clear, because the fixture
 * cut-scene does not write on him and neither does the body.
 *
 * WHY THE CUT-SCENE IS A FIXTURE.  Same reason the four runs above give: the
 * shipped ICON04.DAT is a cinematic that plays CD tracks, walks units and draws
 * chapter text through a pointer a test image has not filled.  The staged
 * ICON04.DAT is three instructions -- a DEPLOY_WAVE, a SET_UNIT_TIMER marker,
 * and the terminator -- and the container it lives in is the one the chapter 1
 * run already built.
 */

static int run5_state = 0;

static int seen5_roster_count;
static int seen5_spare_roster_char_id;
static int seen5_player_slots;
static int seen5_char_spawns;
static int seen5_unit_count;
static int seen5_unit0_char_id;
static int seen5_unit1_char_id;
static int seen5_unit2_char_id;
static int seen5_unit3_char_id;
static int seen5_unit3_side;
static int seen5_unit4_flags;
static int seen5_unit4_char_id;
static unsigned char seen5_unit0_timers[STATUS_TIMER_COUNT];
static int seen5_guest_char_id;
static int seen5_guest_level;
static int seen5_guest_side;
static int seen5_guest_hp_current;
static int seen5_guest_hp_max;
static unsigned char seen5_guest_timers[STATUS_TIMER_COUNT];
static int seen5_script_unit_char_id;
static int seen5_script_unit_level;
static int seen5_script_unit_side;
static unsigned char seen5_script_unit_timers[STATUS_TIMER_COUNT];
static int seen5_cursor_x;
static int seen5_cursor_y;
static int seen5_chapter_id;

static void capture_chapter_05(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *guest;
    struct fdps_unit_record *script_unit;
    int slot;

    seen5_roster_count = data_fdps_roster_member_count;
    seen5_spare_roster_char_id = (int) stage_roster[SPARE_ROSTER_SLOT].char_id;

    seen5_player_slots = data_fdps_map_player_slot_count;
    seen5_char_spawns = data_fdps_map_char_spawn_count;
    seen5_unit_count = data_fdps_map_unit_count;
    seen5_cursor_x = data_fdps_map_cursor_world_x;
    seen5_cursor_y = data_fdps_map_cursor_world_y;
    seen5_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    guest = unit0 + CH4_GUEST_UNIT;
    script_unit = unit0 + CH4_SCRIPT_UNIT;

    seen5_unit0_char_id = (int) unit0->char_id;
    seen5_unit1_char_id = (int) unit0[1].char_id;
    seen5_unit2_char_id = (int) unit0[2].char_id;
    seen5_unit3_char_id = (int) unit0[3].char_id;
    seen5_unit3_side = (int) unit0[3].side;
    seen5_unit4_flags = (int) unit0[4].flags;
    seen5_unit4_char_id = (int) unit0[4].char_id;

    seen5_guest_char_id = (int) guest->char_id;
    seen5_guest_level = (int) guest->level;
    seen5_guest_side = (int) guest->side;
    seen5_guest_hp_current = (int) guest->hp_current;
    seen5_guest_hp_max = (int) guest->hp_max;

    seen5_script_unit_char_id = (int) script_unit->char_id;
    seen5_script_unit_level = (int) script_unit->level;
    seen5_script_unit_side = (int) script_unit->side;

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen5_unit0_timers[slot] = unit0->status_timers[slot];
        seen5_guest_timers[slot] = guest->status_timers[slot];
        seen5_script_unit_timers[slot] = script_unit->status_timers[slot];
    }
}

/* Runs the chapter 5 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The four members the
   party has when the chapter opens are put on with the game's own add, because
   this handler adds none of them and the map's five player slots have to be
   filled from a roster the run staged honestly.  The timer hook and the
   graphics mode are here for the reasons the chapter 1 run gives. */
static void run_chapter_05_handler(void)
{
    if (run5_state != 0) {
        return;
    }
    run5_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_05_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    fdps_roster_add_character(OPENING_CHAR_ID);
    fdps_roster_add_character(JOINING_CHAR_ID);
    fdps_roster_add_character(ARC_CHAR_ID);
    fdps_roster_add_character(FLARENA_CHAR_ID);

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_05_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_05();
    free_chapter_globals();
    run5_state = 1;
}

/* Nobody joins the party this chapter.  The roster is still the four members
   the run put on before it started, roster slot 4 still carries the 0xff
   stage_globals wrote there, and the map's first four player slots are those
   four in join order -- a fifth fdps_roster_add_character in the body would
   move every one of those three answers.  The two map counts are read back as
   well, because they were staged at numbers no map carries and because
   MAP04.DAT's five slots are a count no other map in this file has. */
static void nobody_joins_the_party_when_chapter_five_opens(void)
{
    run_chapter_05_handler();
    CHECK_EQ(run5_state, 1);
    if (run5_state != 1) {
        return;
    }

    CHECK_EQ(seen5_roster_count, PARTY_AT_CHAPTER_05);
    CHECK_EQ(seen5_spare_roster_char_id, EMPTY_ROSTER_SLOT_SENTINEL);
    CHECK_EQ(seen5_player_slots, CH4_PLAYER_SLOTS);
    CHECK_EQ(seen5_char_spawns, CH4_CHAR_SPAWNS);
    CHECK_EQ(seen5_unit0_char_id, OPENING_CHAR_ID);
    CHECK_EQ(seen5_unit1_char_id, JOINING_CHAR_ID);
    CHECK_EQ(seen5_unit2_char_id, ARC_CHAR_ID);
    CHECK_EQ(seen5_unit3_char_id, FLARENA_CHAR_ID);
    CHECK_EQ(seen5_unit3_side, PLAYER_SIDE);
}

/* MAP04.DAT's fifth player slot has nobody behind it and is the zeroed,
   retired spare.  This is the same pair chapter 2's second slot shows, and here
   it is the direct consequence of the handler adding nobody: the party is four
   and the map wants five. */
static void the_fifth_player_slot_is_the_retired_spare(void)
{
    run_chapter_05_handler();
    CHECK_EQ(run5_state, 1);
    if (run5_state != 1) {
        return;
    }

    CHECK_EQ(seen5_unit4_flags, UNIT_FLAG_RETIRED);
    CHECK_EQ(seen5_unit4_char_id, 0);
}

/* The map's own wave-0 record went down during the state reset, and the handler
   left it exactly as the deployment computed.  索爾 is unit 5 -- side 1,
   character 12, level 10 -- at full HP with every status timer clear: chapter
   1's handler poisons its guest hero and knocks his current HP down after the
   cut-scene, and this one has no store in its body at all.  The paralysis the
   strategy guide prints for him in this chapter is the shipped ICON04.DAT's
   SET_UNIT_TIMER and not the handler's doing, and the fixture that stands in
   for that member here writes on a different unit, which is why every one of
   his six status bytes reads back zero. */
static void the_chapter_5_guest_hero_is_left_as_the_map_deployed_him(void)
{
    int slot;

    run_chapter_05_handler();
    CHECK_EQ(run5_state, 1);
    if (run5_state != 1) {
        return;
    }

    CHECK_EQ(seen5_guest_char_id, GUEST_HERO_CHAR_ID);
    CHECK_EQ(seen5_guest_level, GUEST_HERO_LEVEL);
    CHECK_EQ(seen5_guest_side, GUEST_HERO_SIDE);
    CHECK_EQ(seen5_guest_hp_max, SOL_HP_MAX);
    CHECK_EQ(seen5_guest_hp_current, SOL_HP_MAX);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        CHECK_EQ(seen5_guest_timers[slot], 0);
    }
}

/* The cut-scene the handler names is Icon04.dat and it really ran.  The unit
   count is the five player slots, the map's one wave-0 record and the one this
   member's DEPLOY_WAVE adds; ICON00.DAT would have added that record and then
   twenty-seven more, ICON02.DAT and ICON03.DAT none at all.  ICON01.DAT would
   have added exactly one as well, so the deployed unit is read back by
   character id -- MAP04.DAT's wave-2 record is character 91 and its wave-3
   record is character 101 -- and the marker settles it from the other side: it
   is on the deployed unit with a value none of the other four fixtures writes,
   while unit 0's own timers are all clear, which is where ICON01.DAT's marker
   would have landed.  The chapter id is untouched, the handler neither reading
   nor writing it -- both the script number and the title-card graphic are
   chosen from it by the callees. */
static void the_chapter_5_cutscene_is_icon04_dat(void)
{
    int slot;

    run_chapter_05_handler();
    CHECK_EQ(run5_state, 1);
    if (run5_state != 1) {
        return;
    }

    CHECK_EQ(seen5_unit_count, CH4_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen5_script_unit_char_id, CH4_SCRIPT_UNIT_CHAR_ID);
    CHECK_EQ(seen5_script_unit_level, CH4_SCRIPT_UNIT_LEVEL);
    CHECK_EQ(seen5_script_unit_side, ENEMY_SIDE);
    CHECK_EQ(seen5_script_unit_timers[SCRIPT_CH05_MARKER_SLOT],
             SCRIPT_CH05_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH05_MARKER_SLOT) {
            CHECK_EQ(seen5_script_unit_timers[slot], 0);
        }
        CHECK_EQ(seen5_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen5_chapter_id, CHAPTER_05_ID);
}

/* The cursor ends on unit 0's tile.  The walk starts from the (0, 0) the state
   reset left, and MAP04.COD record 33 -- the first record past the map's 33
   scripted deployments -- puts the first party slot on tile (6, 21), so the
   cursor globals are that tile scaled by the 24-pixel step. */
static void the_chapter_5_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_05_handler();
    CHECK_EQ(run5_state, 1);
    if (run5_state != 1) {
        return;
    }

    CHECK_EQ(seen5_cursor_x, CH4_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen5_cursor_y, CH4_PARTY_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_06_init @ 00020ff0 ------------------------------------
 *
 * Four calls, straight line, no branch and no store of its own -- instruction
 * for instruction fdps_chapter_05_init with one literal changed.  What it
 * decides is that nobody joins the party, the ORDER of the four calls, and
 * which cut-scene member and which unit the two arguments name, so the run
 * below enters chapter 6 once for real and reads the answers off the state it
 * leaves.
 *
 * Expected values come from the assembly at 00020ff0 and from the shipped
 * data, never from the emitted C:
 *
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61840 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon05.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * WHAT MAKES THIS CHAPTER DIFFERENT FROM THE ONE BEFORE IT.  Chapter 5 has the
 * same body and a map that wants five player slots against a four-member party,
 * so its fifth slot is the zeroed, retired spare.  MAP05.DAT wants FOUR, which
 * the party already is, so every slot here has a member behind it and the array
 * carries no spare at all -- and there is still no fdps_roster_add_character
 * anywhere in the body, which the roster count and the untouched 0xff in roster
 * slot 4 say from the other side.
 *
 * WHO PUTS THE CHAPTER'S ARMY ON THE MAP.  MAP05.DAT's 32 deployment records
 * are tagged in three waves -- two wave 0, twenty-three wave 1, seven wave 2 --
 * so the two calls divide them: the rebuild's opening deploy takes wave 0, which
 * is record 0's LV10 步兵 and record 31's side-1 LV10 英雄索爾, and the
 * cut-scene's own DEPLOY_WAVE takes wave 1.  The handler therefore returns with
 * 29 units on the map, and the census case below finds among them exactly the
 * strategy guide's FIRST enemy group -- 9 步兵, 7 弓兵, 5 騎兵, 3 魔導士.  The
 * guide's second group is the map's wave 2 to the record, which nothing this
 * handler calls deploys, so the guide and the shipped map are checked against
 * each other rather than either being taken on trust.
 *
 * WHY THE CUT-SCENE IS A FIXTURE.  Same reason the five runs above give: the
 * shipped ICON05.DAT is a cinematic that sets CD tracks, walks and turns units
 * and draws chapter text through a pointer a test image has not filled.  The
 * staged ICON05.DAT keeps the shipped member's own DEPLOY_WAVE -- wave 1, with
 * the same place-exact operand -- and adds a marker so the run can say which
 * member the interpreter opened; the container it lives in is the one the
 * chapter 1 run already built.
 */

static int run6_state = 0;

static int seen6_roster_count;
static int seen6_spare_roster_char_id;
static int seen6_player_slots;
static int seen6_char_spawns;
static int seen6_unit_count;
static int seen6_unit0_char_id;
static int seen6_unit1_char_id;
static int seen6_unit2_char_id;
static int seen6_unit3_char_id;
static int seen6_unit3_side;
static int seen6_unit3_flags;
static int seen6_foot_soldiers;
static int seen6_archers;
static int seen6_riders;
static int seen6_mages;
static int seen6_guest_char_id;
static int seen6_guest_level;
static int seen6_guest_side;
static int seen6_guest_hp_current;
static int seen6_guest_hp_max;
static unsigned char seen6_guest_timers[STATUS_TIMER_COUNT];
static int seen6_marker_unit_char_id;
static unsigned char seen6_marker_timers[STATUS_TIMER_COUNT];
static unsigned char seen6_unit0_timers[STATUS_TIMER_COUNT];
static int seen6_cursor_x;
static int seen6_cursor_y;
static int seen6_chapter_id;

static void capture_chapter_06(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *guest;
    struct fdps_unit_record *marker_unit;
    int unit_index;
    int char_id;
    int slot;

    seen6_roster_count = data_fdps_roster_member_count;
    seen6_spare_roster_char_id = (int) stage_roster[SPARE_ROSTER_SLOT].char_id;

    seen6_player_slots = data_fdps_map_player_slot_count;
    seen6_char_spawns = data_fdps_map_char_spawn_count;
    seen6_unit_count = data_fdps_map_unit_count;
    seen6_cursor_x = data_fdps_map_cursor_world_x;
    seen6_cursor_y = data_fdps_map_cursor_world_y;
    seen6_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    guest = unit0 + CH5_GUEST_UNIT;
    marker_unit = unit0 + SCRIPT_CH06_MARKER_UNIT;

    seen6_unit0_char_id = (int) unit0->char_id;
    seen6_unit1_char_id = (int) unit0[1].char_id;
    seen6_unit2_char_id = (int) unit0[2].char_id;
    seen6_unit3_char_id = (int) unit0[CH5_LAST_PLAYER_SLOT].char_id;
    seen6_unit3_side = (int) unit0[CH5_LAST_PLAYER_SLOT].side;
    seen6_unit3_flags = (int) unit0[CH5_LAST_PLAYER_SLOT].flags;

    seen6_foot_soldiers = 0;
    seen6_archers = 0;
    seen6_riders = 0;
    seen6_mages = 0;
    for (unit_index = CH5_PLAYER_SLOTS;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        if (char_id == CH5_FOOT_CHAR_ID) {
            seen6_foot_soldiers++;
        } else if (char_id == CH5_ARCHER_CHAR_ID) {
            seen6_archers++;
        } else if (char_id == CH5_RIDER_CHAR_ID) {
            seen6_riders++;
        } else if (char_id == CH5_MAGE_CHAR_ID) {
            seen6_mages++;
        }
    }

    seen6_guest_char_id = (int) guest->char_id;
    seen6_guest_level = (int) guest->level;
    seen6_guest_side = (int) guest->side;
    seen6_guest_hp_current = (int) guest->hp_current;
    seen6_guest_hp_max = (int) guest->hp_max;

    seen6_marker_unit_char_id = (int) marker_unit->char_id;

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen6_guest_timers[slot] = guest->status_timers[slot];
        seen6_marker_timers[slot] = marker_unit->status_timers[slot];
        seen6_unit0_timers[slot] = unit0->status_timers[slot];
    }
}

/* Runs the chapter 6 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The four members the
   party has when the chapter opens are put on with the game's own add, because
   neither this handler nor chapter 5's adds any of them and MAP05.DAT's four
   player slots have to be filled from a roster the run staged honestly.  The
   timer hook and the graphics mode are here for the reasons the chapter 1 run
   gives. */
static void run_chapter_06_handler(void)
{
    if (run6_state != 0) {
        return;
    }
    run6_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_06_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    fdps_roster_add_character(OPENING_CHAR_ID);
    fdps_roster_add_character(JOINING_CHAR_ID);
    fdps_roster_add_character(ARC_CHAR_ID);
    fdps_roster_add_character(FLARENA_CHAR_ID);

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_06_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_06();
    free_chapter_globals();
    run6_state = 1;
}

/* Nobody joins the party this chapter and, unlike chapter 5, nothing is left
   over.  The roster is still the four members the run put on before it started
   and roster slot 4 still carries its 0xff, so a fifth
   fdps_roster_add_character in the body would move both; MAP05.DAT's four
   player slots are those four in join order, and the last of them is a live
   player-side record rather than the zeroed, retired spare chapter 5's fifth
   slot is.  The two map counts are read back as well, because they were staged
   at numbers no map carries and because this slot-and-spawn pair is one no
   other map in this file has. */
static void nobody_joins_and_no_slot_is_left_over_in_chapter_six(void)
{
    run_chapter_06_handler();
    CHECK_EQ(run6_state, 1);
    if (run6_state != 1) {
        return;
    }

    CHECK_EQ(seen6_roster_count, PARTY_AT_CHAPTER_06);
    CHECK_EQ(seen6_spare_roster_char_id, EMPTY_ROSTER_SLOT_SENTINEL);
    CHECK_EQ(seen6_player_slots, CH5_PLAYER_SLOTS);
    CHECK_EQ(seen6_char_spawns, CH5_CHAR_SPAWNS);
    CHECK_EQ(seen6_unit0_char_id, OPENING_CHAR_ID);
    CHECK_EQ(seen6_unit1_char_id, JOINING_CHAR_ID);
    CHECK_EQ(seen6_unit2_char_id, ARC_CHAR_ID);
    CHECK_EQ(seen6_unit3_char_id, FLARENA_CHAR_ID);
    CHECK_EQ(seen6_unit3_side, PLAYER_SIDE);
    CHECK_EQ(seen6_unit3_flags, 0);
}

/* What the two calls together leave on the map: the four player slots, the two
   wave-0 records the rebuild deploys and the twenty-three wave-1 records the
   cut-scene deploys, for 29.  The census behind that count is the strategy
   guide's FIRST enemy group -- 9 步兵, 7 弓兵, 5 騎兵, 3 魔導士 -- found by
   character id in the array, and the guide's second group is the map's wave 2,
   which is still to come when this handler returns. */
static void chapter_six_opens_with_the_guides_first_enemy_group(void)
{
    run_chapter_06_handler();
    CHECK_EQ(run6_state, 1);
    if (run6_state != 1) {
        return;
    }

    CHECK_EQ(seen6_unit_count, CH5_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen6_foot_soldiers, CH5_FOOT_COUNT);
    CHECK_EQ(seen6_archers, CH5_ARCHER_COUNT);
    CHECK_EQ(seen6_riders, CH5_RIDER_COUNT);
    CHECK_EQ(seen6_mages, CH5_MAGE_COUNT);
}

/* The second of the map's two wave-0 records went down as unit 5 and the handler
   left it exactly as the deployment computed.  索爾 is side 1, character 12,
   level 10,
   at full HP with every status timer clear: chapter 5's guest hero opens
   麻痺 because the shipped ICON04.DAT paralyses him, and chapter 6's opens
   clean because the shipped ICON05.DAT has no SET_UNIT_TIMER at all and this
   handler, like chapter 5's, has no store in its body.  The guide's line for
   him this chapter carries no status either. */
static void the_chapter_6_guest_hero_is_left_as_the_map_deployed_him(void)
{
    int slot;

    run_chapter_06_handler();
    CHECK_EQ(run6_state, 1);
    if (run6_state != 1) {
        return;
    }

    CHECK_EQ(seen6_guest_char_id, GUEST_HERO_CHAR_ID);
    CHECK_EQ(seen6_guest_level, GUEST_HERO_LEVEL);
    CHECK_EQ(seen6_guest_side, GUEST_HERO_SIDE);
    CHECK_EQ(seen6_guest_hp_max, SOL_HP_MAX);
    CHECK_EQ(seen6_guest_hp_current, SOL_HP_MAX);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        CHECK_EQ(seen6_guest_timers[slot], 0);
    }
}

/* The cut-scene the handler names is Icon05.dat and it really ran.  The marker
   sits on unit 28 -- the last of the twenty-three its own DEPLOY_WAVE appended,
   an index the array does not even reach unless wave 1 was the wave asked for --
   with a value none of the other five fixtures writes, and that unit's character
   id pins the index to MAP05.DAT's record 30 rather than to a bare number.
   Unit 0's own timers are read back clear, which is where ICON00.DAT's and
   ICON01.DAT's markers would have landed.  The chapter id is untouched, the
   handler neither reading nor writing it -- both the script number and the
   title-card graphic are chosen from it by the callees. */
static void the_chapter_6_cutscene_is_icon05_dat(void)
{
    int slot;

    run_chapter_06_handler();
    CHECK_EQ(run6_state, 1);
    if (run6_state != 1) {
        return;
    }

    CHECK_EQ(seen6_marker_unit_char_id, SCRIPT_CH06_MARKER_UNIT_CHAR_ID);
    CHECK_EQ(seen6_marker_timers[SCRIPT_CH06_MARKER_SLOT],
             SCRIPT_CH06_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH06_MARKER_SLOT) {
            CHECK_EQ(seen6_marker_timers[slot], 0);
        }
        CHECK_EQ(seen6_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen6_chapter_id, CHAPTER_06_ID);
}

/* The cursor ends on unit 0's tile.  The walk starts from the (0, 0) the state
   reset left, and MAP05.COD record 32 -- the first record past the map's 32
   scripted deployments -- puts the first party slot on tile (4, 8), so the
   cursor globals are that tile scaled by the 24-pixel step. */
static void the_chapter_6_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_06_handler();
    CHECK_EQ(run6_state, 1);
    if (run6_state != 1) {
        return;
    }

    CHECK_EQ(seen6_cursor_x, CH5_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen6_cursor_y, CH5_PARTY_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_07_init @ 00021030 ------------------------------------
 *
 * Five calls, straight line, no branch and no store of its own -- the plain
 * four-call form chapters 5 and 6 have with an fdps_roster_add_character put
 * back in front of it.  What it decides is WHO joins the party, the ORDER of
 * the five calls, and which cut-scene member and which unit the two arguments
 * name, so the run below enters chapter 7 once for real and reads the answers
 * off the state it leaves.
 *
 * Expected values come from the assembly at 00021030 and from the shipped
 * data, never from the emitted C:
 *
 *   PUSH 0x3 / CALL 0x00023bc0        character 3 joins the roster
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x6184c / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon06.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * WHAT THE ADD DOES AND WHAT IT DOES NOT DO.  裘娜 is the strategy guide's
 * 加入 for this chapter and the roster record the add builds is the guide's
 * line to the number -- LV15, HP225, the two item ids -- but she is NOT one of
 * chapter 7's map units: MAP06.DAT asks for four player slots, the party is
 * already four, and she lands at roster slot 4.  So this run asserts the
 * roster from one side and the map array from the other, the same pair the
 * chapter 4 run asserts about 法蓮娜.
 *
 * SHE IS ON THE MAP AS THE ENEMY, WHICH IS A DIFFERENT RECORD.  MAP06.DAT's
 * five scripted deployments are the guide's 敵方 line -- one side-0 level-15
 * character 116 and four side-0 level-14 character 86 -- and the equipment on
 * record 0 is the same 鐵刀 and 青鎧甲 pair FRIAPRDA.DAT gives roster
 * character 3.  The census case below reads that line back off the array.
 *
 * WHY THE UNIT COUNT IS THE WHOLE STORY OF THE TWO CALLS.  MAP06.DAT tags no
 * record wave 0, so the state reset deploys nothing at all behind the four
 * player slots and every enemy on the map arrives from the cut-scene's own two
 * DEPLOY_WAVEs.  Nine units is therefore four plus zero plus five, and it is
 * also exactly the nine placement records MAP06.COD carries.
 *
 * WHY THE CUT-SCENE IS A FIXTURE.  Same reason the six runs above give: the
 * shipped ICON06.DAT is a cinematic that switches to a cut-scene map, sets CD
 * tracks, walks and turns units and draws chapter text through a pointer a
 * test image has not filled.  The staged ICON06.DAT keeps the shipped member's
 * own two DEPLOY_WAVEs, in its order and with its place-exact operands, and
 * adds a marker so the run can say which member the interpreter opened; the
 * container it lives in is the one the chapter 1 run already built.
 */

static int run7_state = 0;

static int seen7_roster_count;
static int seen7_roster_char_id;
static int seen7_roster_level;
static int seen7_roster_side;
static int seen7_roster_hp_current;
static int seen7_roster_hp_max;
static int seen7_player_slots;
static int seen7_char_spawns;
static int seen7_unit_count;
static int seen7_unit0_char_id;
static int seen7_unit1_char_id;
static int seen7_unit2_char_id;
static int seen7_unit3_char_id;
static int seen7_unit3_side;
static int seen7_unit3_flags;
static int seen7_mercenaries;
static int seen7_merc_level;
static int seen7_merc_side;
static int seen7_boss_char_id;
static int seen7_boss_level;
static int seen7_boss_side;
static unsigned char seen7_boss_timers[STATUS_TIMER_COUNT];
static unsigned char seen7_unit0_timers[STATUS_TIMER_COUNT];
static int seen7_cursor_x;
static int seen7_cursor_y;
static int seen7_chapter_id;

static void capture_chapter_07(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *boss;
    struct fdps_unit_record *member;
    int unit_index;
    int slot;

    seen7_roster_count = data_fdps_roster_member_count;

    member = &stage_roster[JUNA_ROSTER_SLOT];
    seen7_roster_char_id = (int) member->char_id;
    seen7_roster_level = (int) member->level;
    seen7_roster_side = (int) member->side;
    seen7_roster_hp_current = (int) member->hp_current;
    seen7_roster_hp_max = (int) member->hp_max;

    seen7_player_slots = data_fdps_map_player_slot_count;
    seen7_char_spawns = data_fdps_map_char_spawn_count;
    seen7_unit_count = data_fdps_map_unit_count;
    seen7_cursor_x = data_fdps_map_cursor_world_x;
    seen7_cursor_y = data_fdps_map_cursor_world_y;
    seen7_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    boss = unit0 + SCRIPT_CH07_MARKER_UNIT;

    seen7_unit0_char_id = (int) unit0->char_id;
    seen7_unit1_char_id = (int) unit0[1].char_id;
    seen7_unit2_char_id = (int) unit0[2].char_id;
    seen7_unit3_char_id = (int) unit0[CH6_LAST_PLAYER_SLOT].char_id;
    seen7_unit3_side = (int) unit0[CH6_LAST_PLAYER_SLOT].side;
    seen7_unit3_flags = (int) unit0[CH6_LAST_PLAYER_SLOT].flags;

    seen7_mercenaries = 0;
    for (unit_index = CH6_PLAYER_SLOTS;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        if ((int) unit0[unit_index].char_id == CH6_MERC_CHAR_ID) {
            seen7_mercenaries++;
        }
    }
    seen7_merc_level = (int) unit0[CH6_FIRST_MERC_UNIT].level;
    seen7_merc_side = (int) unit0[CH6_FIRST_MERC_UNIT].side;

    seen7_boss_char_id = (int) boss->char_id;
    seen7_boss_level = (int) boss->level;
    seen7_boss_side = (int) boss->side;

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen7_boss_timers[slot] = boss->status_timers[slot];
        seen7_unit0_timers[slot] = unit0->status_timers[slot];
    }
}

/* Runs the chapter 7 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The four members the
   party has when the chapter opens are put on with the game's own add, because
   the handler adds only the fifth and MAP06.DAT's four player slots have to be
   filled from a roster the run staged honestly.  The timer hook and the
   graphics mode are here for the reasons the chapter 1 run gives. */
static void run_chapter_07_handler(void)
{
    if (run7_state != 0) {
        return;
    }
    run7_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_07_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    fdps_roster_add_character(OPENING_CHAR_ID);
    fdps_roster_add_character(JOINING_CHAR_ID);
    fdps_roster_add_character(ARC_CHAR_ID);
    fdps_roster_add_character(FLARENA_CHAR_ID);

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_07_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_07();
    free_chapter_globals();
    run7_state = 1;
}

/* 裘娜 joins the roster when the chapter opens, and the record the add built
   is the strategy guide's 加入 line for this chapter: LV15, 225 HP both
   current and maximum, on the player side.  225 is FRIAPRDA.DAT's 85 base plus
   FRILEVUP.DAT's 10 a level over fourteen levels, which is the sum
   fdps_roster_add_character computes and the number the guide prints, so the
   two are checked against each other rather than either being assumed.  The
   count going from four to five is what says the add ran at all, and the two
   map header counts are read back as well because they were staged at numbers
   no map carries. */
static void juna_joins_the_roster_when_chapter_seven_opens(void)
{
    run_chapter_07_handler();
    CHECK_EQ(run7_state, 1);
    if (run7_state != 1) {
        return;
    }

    CHECK_EQ(seen7_roster_count, PARTY_AT_CHAPTER_07 + 1);
    CHECK_EQ(seen7_roster_char_id, JUNA_CHAR_ID);
    CHECK_EQ(seen7_roster_level, JUNA_LEVEL);
    CHECK_EQ(seen7_roster_side, PLAYER_SIDE);
    CHECK_EQ(seen7_roster_hp_max, JUNA_HP_MAX);
    CHECK_EQ(seen7_roster_hp_current, JUNA_HP_MAX);
    CHECK_EQ(seen7_player_slots, CH6_PLAYER_SLOTS);
    CHECK_EQ(seen7_char_spawns, CH6_CHAR_SPAWNS);
}

/* And she is NOT one of chapter 7's map units.  MAP06.DAT's four player slots
   are filled from roster slots 0 to 3 -- 蘭迪斯, 尤利安, 亞克 and 法蓮娜,
   the party that was already there -- and slot 4 is one past the last the map
   asks for.  The fourth slot is the witness: it is 法蓮娜 and it is live,
   with the retired bit clear, which says the array was built from the roster
   the four chapters before this one had grown and not from the one this
   handler had just extended past the map's needs -- and equally that no slot
   was left over as chapter 5's fifth was. */
static void juna_is_not_one_of_the_chapter_seven_map_units(void)
{
    run_chapter_07_handler();
    CHECK_EQ(run7_state, 1);
    if (run7_state != 1) {
        return;
    }

    CHECK_EQ(seen7_unit0_char_id, OPENING_CHAR_ID);
    CHECK_EQ(seen7_unit1_char_id, JOINING_CHAR_ID);
    CHECK_EQ(seen7_unit2_char_id, ARC_CHAR_ID);
    CHECK_EQ(seen7_unit3_char_id, FLARENA_CHAR_ID);
    CHECK_EQ(seen7_unit3_side, PLAYER_SIDE);
    CHECK_EQ(seen7_unit3_flags, 0);
}

/* What the two calls together leave on the map: the four player slots, nothing
   at all from the state reset because MAP06.DAT tags no record wave 0, and the
   five the cut-scene's two DEPLOY_WAVEs deploy, for nine -- which is also
   exactly the nine placement records MAP06.COD carries.  The five are the
   strategy guide's 敵方 line read back off the array: four LV14 傭兵 on the
   map's own side and, behind them, the LV15 裘娜 the second wave brings on.
   The enemy 裘娜 is character 116 while the roster 裘娜 the case above asserts
   is character 3, so the chapter really does hold both records at once. */
static void chapter_seven_opens_with_the_guides_enemy_line(void)
{
    run_chapter_07_handler();
    CHECK_EQ(run7_state, 1);
    if (run7_state != 1) {
        return;
    }

    CHECK_EQ(seen7_unit_count, CH6_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen7_mercenaries, CH6_MERC_COUNT);
    CHECK_EQ(seen7_merc_level, CH6_MERC_LEVEL);
    CHECK_EQ(seen7_merc_side, ENEMY_SIDE);
    CHECK_EQ(seen7_boss_char_id, CH6_BOSS_CHAR_ID);
    CHECK_EQ(seen7_boss_level, CH6_BOSS_LEVEL);
    CHECK_EQ(seen7_boss_side, ENEMY_SIDE);
}

/* The cut-scene the handler names is Icon06.dat and it really ran.  The marker
   sits on unit 8 -- the one the SECOND of its two DEPLOY_WAVEs appended, an
   index the array reaches only if both waves ran -- with a value none of the
   other six fixtures writes, and the case above has already pinned that unit's
   character id to MAP06.DAT's wave-2 record rather than to a bare number, which
   is what separates this member from ICON00.DAT's same two waves in the
   opposite order.  Unit 0's own timers are read back clear, which is where
   ICON00.DAT's and ICON01.DAT's markers would have landed, and every other
   timer on the marked unit is clear too: neither the handler nor the shipped
   cut-scene writes a status on anybody this chapter.  The chapter id is
   untouched, the handler neither reading nor writing it -- both the script
   number and the title-card graphic are chosen from it by the callees. */
static void the_chapter_7_cutscene_is_icon06_dat(void)
{
    int slot;

    run_chapter_07_handler();
    CHECK_EQ(run7_state, 1);
    if (run7_state != 1) {
        return;
    }

    CHECK_EQ(seen7_boss_timers[SCRIPT_CH07_MARKER_SLOT],
             SCRIPT_CH07_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH07_MARKER_SLOT) {
            CHECK_EQ(seen7_boss_timers[slot], 0);
        }
        CHECK_EQ(seen7_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen7_chapter_id, CHAPTER_07_ID);
}

/* The cursor ends on unit 0's tile.  The walk starts from the (0, 0) the state
   reset left, and MAP06.COD record 5 -- the first record past the map's five
   scripted deployments -- puts the first party slot on tile (11, 17), so the
   cursor globals are that tile scaled by the 24-pixel step. */
static void the_chapter_7_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_07_handler();
    CHECK_EQ(run7_state, 1);
    if (run7_state != 1) {
        return;
    }

    CHECK_EQ(seen7_cursor_x, CH6_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen7_cursor_y, CH6_PARTY_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_08_init @ 00021070 ------------------------------------
 *
 * Five calls, straight line, no branch and no store of its own -- the same
 * shape chapter 7 has, with a different character id and a different script
 * name.  What it decides is WHO joins the party, the ORDER of the five calls,
 * and which cut-scene member and which unit the two arguments name, so the run
 * below enters chapter 8 once for real and reads the answers off the state it
 * leaves.
 *
 * Expected values come from the assembly at 00021070 and from the shipped
 * data, never from the emitted C:
 *
 *   PUSH 0x2 / CALL 0x00023bc0        character 2 joins the roster
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61858 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon07.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * WHAT THE ADD DOES AND WHAT IT DOES NOT DO.  費塔加 is the strategy guide's
 * 加入 for this chapter, but the roster record the add builds is NOT the
 * guide's line: the add reads FRIAPRDA.DAT index 02, which is level 13 on 150
 * HP and 149 MP, while the guide prints the LV15 record at HP162 and MP163
 * that MAP07.DAT deployment 19 carries.  The run asserts the roster record it
 * really builds, and the constants above show the arithmetic that turns the
 * same two tables into the guide's numbers at the other level.
 *
 * And he is not one of chapter 8's map units either: MAP07.DAT asks for five
 * player slots, the party is already five, and he lands at roster slot 5.  His
 * map record is tagged wave 1, which nothing this handler runs deploys, so the
 * census below reads an array with no character 2 in it at all.
 *
 * WHY THE UNIT COUNT IS THE WHOLE STORY OF THE TWO MIDDLE CALLS.  The cut-scene
 * switches the chapter to map 48, deploys there, and switches back to map 7
 * before it ends, so the array the handler returns with is the one that second
 * switch rebuilt: five player slots and MAP07.DAT's fourteen wave-0 records,
 * for nineteen -- which is also exactly the 41 placement records MAP07.COD
 * carries, 36 scripted and 5 for the party.  The two header counts read back
 * are map 7's own 5 and 36 and not map 48's 0 and 6, which is what says the
 * second switch ran.
 *
 * WHY THE CUT-SCENE IS A FIXTURE.  Same reason the seven runs above give: the
 * shipped ICON07.DAT plays a .saf clip, sets CD tracks and draws chapter text
 * through a pointer a test image has not filled.  The staged ICON07.DAT keeps
 * the shipped member's three state-changing opcodes -- both SWITCH_MAPs and
 * the DEPLOY_WAVE between them, in its order and with its operands -- and adds
 * a marker so the run can say which member the interpreter opened; the
 * container it lives in is the one the chapter 1 run already built.
 */

static int run8_state = 0;

static int seen8_roster_count;
static int seen8_roster_char_id;
static int seen8_roster_level;
static int seen8_roster_side;
static int seen8_roster_hp_current;
static int seen8_roster_hp_max;
static int seen8_roster_mp_current;
static int seen8_roster_mp_max;
static int seen8_player_slots;
static int seen8_char_spawns;
static int seen8_unit_count;
static int seen8_unit0_char_id;
static int seen8_unit1_char_id;
static int seen8_unit2_char_id;
static int seen8_unit3_char_id;
static int seen8_unit4_char_id;
static int seen8_unit4_side;
static int seen8_unit4_flags;
static int seen8_feitaga_units;
static int seen8_first_wave_zero_char_id;
static int seen8_last_wave_zero_char_id;
static int seen8_last_wave_zero_level;
static int seen8_last_wave_zero_side;
static unsigned char seen8_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen8_unit0_timers[STATUS_TIMER_COUNT];
static int seen8_cursor_x;
static int seen8_cursor_y;
static int seen8_chapter_id;

static void capture_chapter_08(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    struct fdps_unit_record *member;
    int unit_index;
    int slot;

    seen8_roster_count = data_fdps_roster_member_count;

    member = &stage_roster[FEITAGA_ROSTER_SLOT];
    seen8_roster_char_id = (int) member->char_id;
    seen8_roster_level = (int) member->level;
    seen8_roster_side = (int) member->side;
    seen8_roster_hp_current = (int) member->hp_current;
    seen8_roster_hp_max = (int) member->hp_max;
    seen8_roster_mp_current = (int) member->mp_current;
    seen8_roster_mp_max = (int) member->mp_max;

    seen8_player_slots = data_fdps_map_player_slot_count;
    seen8_char_spawns = data_fdps_map_char_spawn_count;
    seen8_unit_count = data_fdps_map_unit_count;
    seen8_cursor_x = data_fdps_map_cursor_world_x;
    seen8_cursor_y = data_fdps_map_cursor_world_y;
    seen8_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    marked = unit0 + SCRIPT_CH08_MARKER_UNIT;

    seen8_unit0_char_id = (int) unit0->char_id;
    seen8_unit1_char_id = (int) unit0[1].char_id;
    seen8_unit2_char_id = (int) unit0[2].char_id;
    seen8_unit3_char_id = (int) unit0[3].char_id;
    seen8_unit4_char_id = (int) unit0[CH7_LAST_PLAYER_SLOT].char_id;
    seen8_unit4_side = (int) unit0[CH7_LAST_PLAYER_SLOT].side;
    seen8_unit4_flags = (int) unit0[CH7_LAST_PLAYER_SLOT].flags;

    seen8_feitaga_units = 0;
    for (unit_index = 0;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        if ((int) unit0[unit_index].char_id == FEITAGA_CHAR_ID) {
            seen8_feitaga_units++;
        }
    }

    seen8_first_wave_zero_char_id =
        (int) unit0[CH7_FIRST_WAVE_ZERO_UNIT].char_id;
    seen8_last_wave_zero_char_id =
        (int) unit0[CH7_LAST_WAVE_ZERO_UNIT].char_id;
    seen8_last_wave_zero_level = (int) unit0[CH7_LAST_WAVE_ZERO_UNIT].level;
    seen8_last_wave_zero_side = (int) unit0[CH7_LAST_WAVE_ZERO_UNIT].side;

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen8_marked_timers[slot] = marked->status_timers[slot];
        seen8_unit0_timers[slot] = unit0->status_timers[slot];
    }
}

/* Runs the chapter 8 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The five members the
   party has when the chapter opens are put on with the game's own add, because
   the handler adds only the sixth and MAP07.DAT's five player slots have to be
   filled from a roster the run staged honestly.  The timer hook and the
   graphics mode are here for the reasons the chapter 1 run gives. */
static void run_chapter_08_handler(void)
{
    if (run8_state != 0) {
        return;
    }
    run8_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_08_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    fdps_roster_add_character(OPENING_CHAR_ID);
    fdps_roster_add_character(JOINING_CHAR_ID);
    fdps_roster_add_character(ARC_CHAR_ID);
    fdps_roster_add_character(FLARENA_CHAR_ID);
    fdps_roster_add_character(JUNA_CHAR_ID);

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_08_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_08();
    free_chapter_globals();
    run8_state = 1;
}

/* 費塔加 joins the roster when the chapter opens, and the record the add built
   is FRIAPRDA.DAT index 02's own line and not the strategy guide's: LV13 on
   150 HP and 149 MP, both current and maximum, on the player side.  150 is the
   file's 78 base plus 6 a level over twelve levels and 149 is its 65 base plus
   7 a level over the same twelve, which is the sum fdps_roster_add_character
   computes.  The guide's LV15 line at HP162 and MP163 is the same two tables
   two levels further on -- it is MAP07.DAT's own deployment 19 -- so this pair
   of assertions is what separates the record the ADD builds from the record
   the MAP carries.  The count going from five to six is what says the add ran
   at all, and the two map header counts are read back as well because they
   were staged at numbers no map carries. */
static void feitaga_joins_the_roster_when_chapter_eight_opens(void)
{
    run_chapter_08_handler();
    CHECK_EQ(run8_state, 1);
    if (run8_state != 1) {
        return;
    }

    CHECK_EQ(seen8_roster_count, PARTY_AT_CHAPTER_08 + 1);
    CHECK_EQ(seen8_roster_char_id, FEITAGA_CHAR_ID);
    CHECK_EQ(seen8_roster_level, FEITAGA_LEVEL);
    CHECK_EQ(seen8_roster_side, PLAYER_SIDE);
    CHECK_EQ(seen8_roster_hp_max, FEITAGA_HP_MAX);
    CHECK_EQ(seen8_roster_hp_current, FEITAGA_HP_MAX);
    CHECK_EQ(seen8_roster_mp_max, FEITAGA_MP_MAX);
    CHECK_EQ(seen8_roster_mp_current, FEITAGA_MP_MAX);
    CHECK_EQ(seen8_player_slots, CH7_PLAYER_SLOTS);
    CHECK_EQ(seen8_char_spawns, CH7_CHAR_SPAWNS);
}

/* And he is NOT one of chapter 8's map units.  MAP07.DAT's five player slots
   are filled from roster slots 0 to 4 -- 蘭迪斯, 尤利安, 亞克, 法蓮娜 and
   裘娜, the party that was already there -- and slot 5 is one past the last
   the map asks for.  The fifth slot is the witness: it is 裘娜 and it is live,
   with the retired bit clear, which says the array was built from the roster
   the seven chapters before this one had grown and not from the one this
   handler had just extended past the map's needs -- and equally that no slot
   was left over as chapter 5's fifth was.  Chapter 8 is also the first map
   裘娜 is deployed on, MAP06.DAT having asked for only four slots.

   The census over the whole array is the other half: character 2 appears
   nowhere in it.  MAP07.DAT does carry him, as the side-1 level-15 deployment
   19 the guide's 友軍 line prints, but that record is tagged wave 1 and
   nothing this handler runs deploys wave 1 on map 7. */
static void feitaga_is_not_one_of_the_chapter_eight_map_units(void)
{
    run_chapter_08_handler();
    CHECK_EQ(run8_state, 1);
    if (run8_state != 1) {
        return;
    }

    CHECK_EQ(seen8_unit0_char_id, OPENING_CHAR_ID);
    CHECK_EQ(seen8_unit1_char_id, JOINING_CHAR_ID);
    CHECK_EQ(seen8_unit2_char_id, ARC_CHAR_ID);
    CHECK_EQ(seen8_unit3_char_id, FLARENA_CHAR_ID);
    CHECK_EQ(seen8_unit4_char_id, JUNA_CHAR_ID);
    CHECK_EQ(seen8_unit4_side, PLAYER_SIDE);
    CHECK_EQ(seen8_unit4_flags, 0);
    CHECK_EQ(seen8_feitaga_units, 0);
}

/* What the two calls together leave on the map: the five player slots and the
   fourteen records MAP07.DAT tags wave 0, for nineteen, with nothing from the
   cut-scene at all -- its own DEPLOY_WAVE ran while the chapter id was 48 and
   the SWITCH_MAP behind it rebuilt map 7 from scratch.  The two wave-0 units
   read back are the first and the last of the fourteen, deployments 2 and 23,
   and the last one's side of 1 is what says the map's own side codes reached
   the array rather than the player side the slots above it carry. */
static void chapter_eight_opens_on_the_wave_zero_deployments(void)
{
    run_chapter_08_handler();
    CHECK_EQ(run8_state, 1);
    if (run8_state != 1) {
        return;
    }

    CHECK_EQ(seen8_unit_count, CH7_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen8_first_wave_zero_char_id, CH7_FIRST_WAVE_ZERO_CHAR_ID);
    CHECK_EQ(seen8_last_wave_zero_char_id, CH7_LAST_WAVE_ZERO_CHAR_ID);
    CHECK_EQ(seen8_last_wave_zero_level, CH7_LAST_WAVE_ZERO_LEVEL);
    CHECK_EQ(seen8_last_wave_zero_side, CH7_LAST_WAVE_ZERO_SIDE);
}

/* The cut-scene the handler names is Icon07.dat and it really ran.  The marker
   sits on unit 18 -- the last of the fourteen wave-0 records, an index the
   array reaches only after the second SWITCH_MAP has rebuilt map 7, the
   cut-scene map's own array being six units long -- with a value none of the
   other seven fixtures writes.  Unit 0's own timers are read back clear, which
   is where ICON00.DAT's and ICON01.DAT's markers would have landed, and every
   other timer on the marked unit is clear too: neither the handler nor the
   shipped cut-scene writes a status on anybody this chapter.  The chapter id
   is back at 7, which the handler neither reads nor writes -- the script's own
   pair of switches left it as it found it, and both the script number and the
   title-card graphic are chosen from it by the callees. */
static void the_chapter_8_cutscene_is_icon07_dat(void)
{
    int slot;

    run_chapter_08_handler();
    CHECK_EQ(run8_state, 1);
    if (run8_state != 1) {
        return;
    }

    CHECK_EQ(seen8_marked_timers[SCRIPT_CH08_MARKER_SLOT],
             SCRIPT_CH08_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH08_MARKER_SLOT) {
            CHECK_EQ(seen8_marked_timers[slot], 0);
        }
        CHECK_EQ(seen8_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen8_chapter_id, CHAPTER_08_ID);
}

/* The cursor ends on unit 0's tile.  The walk starts from the (0, 0) the last
   state reset left, and MAP07.COD record 36 -- the first record past the map's
   36 scripted deployments -- puts the first party slot on tile (3, 2), so the
   cursor globals are that tile scaled by the 24-pixel step. */
static void the_chapter_8_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_08_handler();
    CHECK_EQ(run8_state, 1);
    if (run8_state != 1) {
        return;
    }

    CHECK_EQ(seen8_cursor_x, CH7_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen8_cursor_y, CH7_PARTY_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_09_init @ 000210b0 ------------------------------------
 *
 * Six calls, straight line, no branch and no store of its own -- chapter 8's
 * shape with a SECOND fdps_roster_add_character in front of it, and it is the
 * only handler in the file that adds two.  What it decides is WHO joins the
 * party, in WHICH ORDER, the order of the six calls, and which cut-scene
 * member and which unit the two arguments name, so the run below enters
 * chapter 9 once for real and reads the answers off the state it leaves.
 *
 * Expected values come from the assembly at 000210b0 and from the shipped
 * data, never from the emitted C:
 *
 *   PUSH 0x8 / CALL 0x00023bc0        character 8 joins the roster
 *   PUSH 0x9 / CALL 0x00023bc0        character 9 joins the roster
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61864 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon08.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * WHY THIS IS THE FIRST RUN WHERE THE ADDS REACH THE MAP.  Chapters 4, 7 and 8
 * each put a member on the roster one slot past the last player slot their map
 * asked for, so the three runs above assert that the newcomer is NOT a map
 * unit.  MAP08.DAT asks for eight player slots and the party is six when the
 * chapter opens, so these two adds fill the last two slots exactly, and the
 * case below asserts the opposite thing: both of them are on the map, live,
 * with the retired bit clear.
 *
 * AND IT IS THE ONLY RUN THAT CAN SEE WHICH ADD CAME FIRST.  Roster slot order
 * is player slot order and a party slot is put on placement record
 * data_fdps_map_char_spawn_count + slot_index (src/deploy.c), so MAP08.COD
 * records 37 and 38 -- (23, 12) and (24, 13) -- are read back on the two of
 * them.  Emitting the two calls the other way round swaps those tiles and
 * changes nothing else the run can see.
 *
 * WHAT THE ADDS BUILD IS THE GUIDE'S OWN LINE, which is where chapter 9 parts
 * company with chapter 8 in the other direction: the guide's 己方 for this
 * chapter is LV14 技師布蘭多 HP167 MP39 and LV16 機兵蓋亞 HP240 MP45, which is
 * FRIAPRDA.DAT and FRILEVUP.DAT read at those characters' own levels and so
 * exactly what fdps_roster_add_character computes.  MAP08.DAT carries no
 * record for either character, and the census below says so.
 *
 * WHY THE UNIT COUNT IS THE STORY OF THE TWO MIDDLE CALLS.  The cut-scene
 * switches to map 54, then to map 55, deploys there, and switches back to map
 * 8 before it ends, so the array the handler returns with is the one that last
 * switch rebuilt: eight player slots and MAP08.DAT's twenty-four wave-0
 * records, for thirty-two -- which is also exactly the 39 placement records
 * MAP08.COD carries, 31 scripted and 8 for the party.  The two header counts
 * read back are map 8's own 8 and 31 and not map 54's 0 and 16 or map 55's 0
 * and 3, which is what says the last switch ran.
 *
 * WHY THE CUT-SCENE IS A FIXTURE.  Same reason the eight runs above give: the
 * shipped ICON08.DAT plays .saf clips, sets CD tracks and draws chapter text
 * through a pointer a test image has not filled.  The staged ICON08.DAT keeps
 * the shipped member's four state-changing opcodes -- all three SWITCH_MAPs
 * and the DEPLOY_WAVE between them, in its order and with its operands -- and
 * adds a marker so the run can say which member the interpreter opened; the
 * container it lives in is the one the chapter 1 run already built.
 */

static int run9_state = 0;

static int seen9_roster_count;
static int seen9_brando_char_id;
static int seen9_brando_level;
static int seen9_brando_side;
static int seen9_brando_hp_current;
static int seen9_brando_hp_max;
static int seen9_brando_mp_current;
static int seen9_brando_mp_max;
static int seen9_gaia_char_id;
static int seen9_gaia_level;
static int seen9_gaia_side;
static int seen9_gaia_hp_current;
static int seen9_gaia_hp_max;
static int seen9_gaia_mp_current;
static int seen9_gaia_mp_max;
static int seen9_player_slots;
static int seen9_char_spawns;
static int seen9_unit_count;
static int seen9_unit0_char_id;
static int seen9_unit1_char_id;
static int seen9_unit2_char_id;
static int seen9_unit3_char_id;
static int seen9_unit4_char_id;
static int seen9_unit5_char_id;
static int seen9_unit6_char_id;
static int seen9_unit6_side;
static int seen9_unit6_flags;
static int seen9_unit6_pos_x;
static int seen9_unit6_pos_y;
static int seen9_unit7_char_id;
static int seen9_unit7_side;
static int seen9_unit7_flags;
static int seen9_unit7_pos_x;
static int seen9_unit7_pos_y;
static int seen9_brando_units;
static int seen9_gaia_units;
static int seen9_first_wave_zero_char_id;
static int seen9_last_wave_zero_char_id;
static int seen9_last_wave_zero_level;
static int seen9_last_wave_zero_side;
static unsigned char seen9_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen9_unit0_timers[STATUS_TIMER_COUNT];
static int seen9_cursor_x;
static int seen9_cursor_y;
static int seen9_chapter_id;

static void capture_chapter_09(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    struct fdps_unit_record *member;
    int unit_index;
    int slot;

    seen9_roster_count = data_fdps_roster_member_count;

    member = &stage_roster[BRANDO_ROSTER_SLOT];
    seen9_brando_char_id = (int) member->char_id;
    seen9_brando_level = (int) member->level;
    seen9_brando_side = (int) member->side;
    seen9_brando_hp_current = (int) member->hp_current;
    seen9_brando_hp_max = (int) member->hp_max;
    seen9_brando_mp_current = (int) member->mp_current;
    seen9_brando_mp_max = (int) member->mp_max;

    member = &stage_roster[GAIA_ROSTER_SLOT];
    seen9_gaia_char_id = (int) member->char_id;
    seen9_gaia_level = (int) member->level;
    seen9_gaia_side = (int) member->side;
    seen9_gaia_hp_current = (int) member->hp_current;
    seen9_gaia_hp_max = (int) member->hp_max;
    seen9_gaia_mp_current = (int) member->mp_current;
    seen9_gaia_mp_max = (int) member->mp_max;

    seen9_player_slots = data_fdps_map_player_slot_count;
    seen9_char_spawns = data_fdps_map_char_spawn_count;
    seen9_unit_count = data_fdps_map_unit_count;
    seen9_cursor_x = data_fdps_map_cursor_world_x;
    seen9_cursor_y = data_fdps_map_cursor_world_y;
    seen9_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    marked = unit0 + SCRIPT_CH09_MARKER_UNIT;

    seen9_unit0_char_id = (int) unit0->char_id;
    seen9_unit1_char_id = (int) unit0[1].char_id;
    seen9_unit2_char_id = (int) unit0[2].char_id;
    seen9_unit3_char_id = (int) unit0[3].char_id;
    seen9_unit4_char_id = (int) unit0[4].char_id;
    seen9_unit5_char_id = (int) unit0[5].char_id;

    seen9_unit6_char_id = (int) unit0[CH8_BRANDO_UNIT].char_id;
    seen9_unit6_side = (int) unit0[CH8_BRANDO_UNIT].side;
    seen9_unit6_flags = (int) unit0[CH8_BRANDO_UNIT].flags;
    seen9_unit6_pos_x = (int) unit0[CH8_BRANDO_UNIT].pos_x;
    seen9_unit6_pos_y = (int) unit0[CH8_BRANDO_UNIT].pos_y;
    seen9_unit7_char_id = (int) unit0[CH8_GAIA_UNIT].char_id;
    seen9_unit7_side = (int) unit0[CH8_GAIA_UNIT].side;
    seen9_unit7_flags = (int) unit0[CH8_GAIA_UNIT].flags;
    seen9_unit7_pos_x = (int) unit0[CH8_GAIA_UNIT].pos_x;
    seen9_unit7_pos_y = (int) unit0[CH8_GAIA_UNIT].pos_y;

    seen9_brando_units = 0;
    seen9_gaia_units = 0;
    for (unit_index = 0;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        if ((int) unit0[unit_index].char_id == BRANDO_CHAR_ID) {
            seen9_brando_units++;
        }
        if ((int) unit0[unit_index].char_id == GAIA_CHAR_ID) {
            seen9_gaia_units++;
        }
    }

    seen9_first_wave_zero_char_id =
        (int) unit0[CH8_FIRST_WAVE_ZERO_UNIT].char_id;
    seen9_last_wave_zero_char_id =
        (int) unit0[CH8_LAST_WAVE_ZERO_UNIT].char_id;
    seen9_last_wave_zero_level = (int) unit0[CH8_LAST_WAVE_ZERO_UNIT].level;
    seen9_last_wave_zero_side = (int) unit0[CH8_LAST_WAVE_ZERO_UNIT].side;

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen9_marked_timers[slot] = marked->status_timers[slot];
        seen9_unit0_timers[slot] = unit0->status_timers[slot];
    }
}

/* Runs the chapter 9 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The six members the
   party has when the chapter opens are put on with the game's own add, in join
   order, because the handler adds only the seventh and eighth and MAP08.DAT's
   eight player slots have to be filled from a roster the run staged honestly.
   The timer hook and the graphics mode are here for the reasons the chapter 1
   run gives. */
static void run_chapter_09_handler(void)
{
    if (run9_state != 0) {
        return;
    }
    run9_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_09_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    fdps_roster_add_character(OPENING_CHAR_ID);
    fdps_roster_add_character(JOINING_CHAR_ID);
    fdps_roster_add_character(ARC_CHAR_ID);
    fdps_roster_add_character(FLARENA_CHAR_ID);
    fdps_roster_add_character(JUNA_CHAR_ID);
    fdps_roster_add_character(FEITAGA_CHAR_ID);

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_09_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_09();
    free_chapter_globals();
    run9_state = 1;
}

/* Both of them join the roster when the chapter opens, and the records the two
   adds built are FRIAPRDA.DAT index 08 and index 09 read at their own levels:
   LV14 on 167 HP and 39 MP, LV16 on 240 HP and 45 MP, both current and
   maximum, both on the player side.  167 is the file's 50 base plus 9 a level
   over thirteen levels, 39 is 3 a level over the same thirteen, 240 is 60 plus
   12 over fifteen and 45 is 3 over fifteen -- and all four are the strategy
   guide's 己方 line for the chapter to the number, so this is the shipped
   tables and the guide agreeing rather than an arithmetic the test invented.
   The count going from six to eight is what says BOTH adds ran, and the two
   map header counts are read back as well because they were staged at numbers
   no map carries. */
static void brando_and_gaia_join_the_roster_when_chapter_nine_opens(void)
{
    run_chapter_09_handler();
    CHECK_EQ(run9_state, 1);
    if (run9_state != 1) {
        return;
    }

    CHECK_EQ(seen9_roster_count, PARTY_AT_CHAPTER_09 + 2);
    CHECK_EQ(seen9_brando_char_id, BRANDO_CHAR_ID);
    CHECK_EQ(seen9_brando_level, BRANDO_LEVEL);
    CHECK_EQ(seen9_brando_side, PLAYER_SIDE);
    CHECK_EQ(seen9_brando_hp_max, BRANDO_HP_MAX);
    CHECK_EQ(seen9_brando_hp_current, BRANDO_HP_MAX);
    CHECK_EQ(seen9_brando_mp_max, BRANDO_MP_MAX);
    CHECK_EQ(seen9_brando_mp_current, BRANDO_MP_MAX);
    CHECK_EQ(seen9_gaia_char_id, GAIA_CHAR_ID);
    CHECK_EQ(seen9_gaia_level, GAIA_LEVEL);
    CHECK_EQ(seen9_gaia_side, PLAYER_SIDE);
    CHECK_EQ(seen9_gaia_hp_max, GAIA_HP_MAX);
    CHECK_EQ(seen9_gaia_hp_current, GAIA_HP_MAX);
    CHECK_EQ(seen9_gaia_mp_max, GAIA_MP_MAX);
    CHECK_EQ(seen9_gaia_mp_current, GAIA_MP_MAX);
    CHECK_EQ(seen9_player_slots, CH8_PLAYER_SLOTS);
    CHECK_EQ(seen9_char_spawns, CH8_CHAR_SPAWNS);
}

/* And unlike chapters 4, 7 and 8, both of them ARE map units, in the order the
   two adds ran.  MAP08.DAT's eight player slots are filled from roster slots 0
   to 7 -- the six the party already had, in join order, and then these two --
   so slot 6 is character 8 on tile (23, 12) and slot 7 is character 9 on tile
   (24, 13), both live with the retired bit clear.  Swapping the two calls in
   the handler swaps those two tiles, and this pair of positions is the only
   thing in the run that would notice.

   The census over the whole array is the other half: each of them appears
   exactly once, which is to say MAP08.DAT itself carries no record for either.
   Chapter 8's 費塔加 was on his map as a deployment record as well as on the
   roster; these two are on it only because the adds put them there. */
static void brando_and_gaia_take_the_last_two_player_slots(void)
{
    run_chapter_09_handler();
    CHECK_EQ(run9_state, 1);
    if (run9_state != 1) {
        return;
    }

    CHECK_EQ(seen9_unit0_char_id, OPENING_CHAR_ID);
    CHECK_EQ(seen9_unit1_char_id, JOINING_CHAR_ID);
    CHECK_EQ(seen9_unit2_char_id, ARC_CHAR_ID);
    CHECK_EQ(seen9_unit3_char_id, FLARENA_CHAR_ID);
    CHECK_EQ(seen9_unit4_char_id, JUNA_CHAR_ID);
    CHECK_EQ(seen9_unit5_char_id, FEITAGA_CHAR_ID);

    CHECK_EQ(seen9_unit6_char_id, BRANDO_CHAR_ID);
    CHECK_EQ(seen9_unit6_side, PLAYER_SIDE);
    CHECK_EQ(seen9_unit6_flags, 0);
    CHECK_EQ(seen9_unit6_pos_x, CH8_BRANDO_TILE_X);
    CHECK_EQ(seen9_unit6_pos_y, CH8_BRANDO_TILE_Y);

    CHECK_EQ(seen9_unit7_char_id, GAIA_CHAR_ID);
    CHECK_EQ(seen9_unit7_side, PLAYER_SIDE);
    CHECK_EQ(seen9_unit7_flags, 0);
    CHECK_EQ(seen9_unit7_pos_x, CH8_GAIA_TILE_X);
    CHECK_EQ(seen9_unit7_pos_y, CH8_GAIA_TILE_Y);

    CHECK_EQ(seen9_brando_units, 1);
    CHECK_EQ(seen9_gaia_units, 1);
}

/* What the two middle calls together leave on the map: the eight player slots
   and the twenty-four records MAP08.DAT tags wave 0, for thirty-two, with
   nothing from the cut-scene at all -- its own DEPLOY_WAVE ran while the
   chapter id was 55 and the SWITCH_MAP behind it rebuilt map 8 from scratch.
   The two wave-0 units read back are the first and the last of the
   twenty-four, deployments 0 and 24; the six records the map tags wave 1 are
   the guide's 援軍 line and are still waiting when the handler returns, which
   is what the count of thirty-two rather than thirty-eight says. */
static void chapter_nine_opens_on_the_wave_zero_deployments(void)
{
    run_chapter_09_handler();
    CHECK_EQ(run9_state, 1);
    if (run9_state != 1) {
        return;
    }

    CHECK_EQ(seen9_unit_count, CH8_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen9_first_wave_zero_char_id, CH8_FIRST_WAVE_ZERO_CHAR_ID);
    CHECK_EQ(seen9_last_wave_zero_char_id, CH8_LAST_WAVE_ZERO_CHAR_ID);
    CHECK_EQ(seen9_last_wave_zero_level, CH8_LAST_WAVE_ZERO_LEVEL);
    CHECK_EQ(seen9_last_wave_zero_side, CH8_LAST_WAVE_ZERO_SIDE);
}

/* The cut-scene the handler names is Icon08.dat and it really ran.  The marker
   sits on unit 31 -- the last of the twenty-four wave-0 records, an index the
   array reaches only after the last SWITCH_MAP has rebuilt map 8, the two
   cut-scene maps' own arrays being sixteen and three units long -- with a
   value none of the other eight fixtures writes.  Unit 0's own timers are read
   back clear, which is where ICON00.DAT's and ICON01.DAT's markers would have
   landed, and every other timer on the marked unit is clear too: neither the
   handler nor the shipped cut-scene writes a status on anybody this chapter.
   The chapter id is back at 8, which the handler neither reads nor writes --
   the script's own run of switches left it as it found it, and both the script
   number and the title-card graphic are chosen from it by the callees. */
static void the_chapter_9_cutscene_is_icon08_dat(void)
{
    int slot;

    run_chapter_09_handler();
    CHECK_EQ(run9_state, 1);
    if (run9_state != 1) {
        return;
    }

    CHECK_EQ(seen9_marked_timers[SCRIPT_CH09_MARKER_SLOT],
             SCRIPT_CH09_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH09_MARKER_SLOT) {
            CHECK_EQ(seen9_marked_timers[slot], 0);
        }
        CHECK_EQ(seen9_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen9_chapter_id, CHAPTER_09_ID);
}

/* The cursor ends on unit 0's tile.  The walk starts from the (0, 0) the last
   state reset left, and MAP08.COD record 31 -- the first record past the map's
   31 scripted deployments -- puts the first party slot on tile (3, 18), so the
   cursor globals are that tile scaled by the 24-pixel step. */
static void the_chapter_9_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_09_handler();
    CHECK_EQ(run9_state, 1);
    if (run9_state != 1) {
        return;
    }

    CHECK_EQ(seen9_cursor_x, CH8_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen9_cursor_y, CH8_PARTY_TILE_Y * CURSOR_TILE_STEP);
}

/* --- fdps_chapter_10_init @ 00021100 ------------------------------------
 *
 * Four calls, straight line, no branch and no store of its own -- the plain
 * form chapters 5 and 6 have, with no fdps_roster_add_character in front of
 * the rebuild.  What it decides is the ORDER of the four calls and which
 * cut-scene member and which unit its two arguments name, so the run below
 * enters chapter 10 once for real and reads the answers off the state it
 * leaves.
 *
 * Expected values come from the assembly at 00021100 and from the shipped
 * data, never from the emitted C:
 *
 *   CALL 0x00022750                   the chapter state is rebuilt
 *   MOV EAX,0x61870 / PUSH EAX /
 *     CALL 0x00021650                 the cut-scene "Icon09.dat" is run
 *   CALL 0x00020c60                   the title card is shown
 *   PUSH 0x0 / CALL 0x0002da50        the cursor is parked on unit 0
 *
 * NOBODY JOINS AND NO SLOT IS LEFT OVER.  The party is eight members when the
 * chapter opens -- chapters 1 to 4 one each, chapter 7 裘娜, chapter 8
 * 費塔加, chapter 9 布蘭多 and 蓋亞 -- and MAP09.DAT asks for eight player
 * slots, so every slot has a member behind it and the array carries no zeroed,
 * retired spare of the kind chapter 5's fifth slot is.  Unlike chapters 5 and
 * 6 there is no untouched roster slot to read the 0xff sentinel out of, so
 * what says the handler added nobody is the count still reading eight.
 *
 * THE CUT-SCENE DEPLOYS NOBODY AT ALL, which is true of no other chapter
 * script this file stages.  The shipped ICON09.DAT switches to map 37, then to
 * map 38, then back to map 9, and carries no DEPLOY_WAVE in its 854 bytes, so
 * the array the handler returns with is exactly what the last switch rebuilt:
 * eight player slots and MAP09.DAT's eight wave-0 records, for sixteen.  Those
 * eight are the strategy guide's opening 敵方 group -- four LV13 步兵, two
 * LV13 騎兵 and two LV15 魔導士 -- and the map's other forty records are the
 * reinforcement waves the guide lists against turns 3, 6 to 13 and 19, which
 * are still to come when the handler returns.
 *
 * WHY THE CUT-SCENE IS A FIXTURE.  Same reason the nine runs above give: the
 * shipped ICON09.DAT walks and poses actors, plays clips and draws chapter
 * text through a pointer a test image has not filled.  The staged ICON09.DAT
 * keeps the shipped member's three SWITCH_MAPs, in its order and with its
 * operands, and adds a marker so the run can say which member the interpreter
 * opened; the container it lives in is the one the chapter 1 run already
 * built.
 */

static int run10_state = 0;

/* The party in join order, which is roster-slot order and so player-slot
   order: 蘭迪斯, 尤利安, 亞克, 法蓮娜, 裘娜, 費塔加, 布蘭多, 蓋亞.  The run
   puts them on with the game's own add and the case below reads the same
   eight back off the map. */
static int chapter_10_party[CH9_PLAYER_SLOTS] = {
    OPENING_CHAR_ID, JOINING_CHAR_ID, ARC_CHAR_ID, FLARENA_CHAR_ID,
    JUNA_CHAR_ID, FEITAGA_CHAR_ID, BRANDO_CHAR_ID, GAIA_CHAR_ID
};

static int seen10_roster_count;
static int seen10_player_slots;
static int seen10_char_spawns;
static int seen10_unit_count;
static int seen10_party_char_ids[CH9_PLAYER_SLOTS];
static int seen10_party_live_slots;
static int seen10_foot_soldiers;
static int seen10_archers;
static int seen10_riders;
static int seen10_mages;
static int seen10_first_wave_zero_char_id;
static int seen10_first_wave_zero_level;
static int seen10_last_wave_zero_char_id;
static int seen10_last_wave_zero_level;
static int seen10_last_wave_zero_side;
static unsigned char seen10_marked_timers[STATUS_TIMER_COUNT];
static unsigned char seen10_unit0_timers[STATUS_TIMER_COUNT];
static int seen10_cursor_x;
static int seen10_cursor_y;
static int seen10_chapter_id;

static void capture_chapter_10(void)
{
    struct fdps_unit_record *unit0;
    struct fdps_unit_record *marked;
    int unit_index;
    int char_id;
    int slot;

    seen10_roster_count = data_fdps_roster_member_count;
    seen10_player_slots = data_fdps_map_player_slot_count;
    seen10_char_spawns = data_fdps_map_char_spawn_count;
    seen10_unit_count = data_fdps_map_unit_count;
    seen10_cursor_x = data_fdps_map_cursor_world_x;
    seen10_cursor_y = data_fdps_map_cursor_world_y;
    seen10_chapter_id = data_fdps_chapter_current_chapter_id;

    unit0 = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    marked = unit0 + SCRIPT_CH10_MARKER_UNIT;

    seen10_party_live_slots = 0;
    for (slot = 0; slot < CH9_PLAYER_SLOTS; slot++) {
        seen10_party_char_ids[slot] = (int) unit0[slot].char_id;
        if ((int) unit0[slot].side == PLAYER_SIDE
            && (int) unit0[slot].flags == 0) {
            seen10_party_live_slots++;
        }
    }

    seen10_foot_soldiers = 0;
    seen10_archers = 0;
    seen10_riders = 0;
    seen10_mages = 0;
    for (unit_index = 0;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        char_id = (int) unit0[unit_index].char_id;
        if (char_id == CH5_FOOT_CHAR_ID) {
            seen10_foot_soldiers++;
        } else if (char_id == CH5_ARCHER_CHAR_ID) {
            seen10_archers++;
        } else if (char_id == CH5_RIDER_CHAR_ID) {
            seen10_riders++;
        } else if (char_id == CH5_MAGE_CHAR_ID) {
            seen10_mages++;
        }
    }

    seen10_first_wave_zero_char_id =
        (int) unit0[CH9_FIRST_WAVE_ZERO_UNIT].char_id;
    seen10_first_wave_zero_level =
        (int) unit0[CH9_FIRST_WAVE_ZERO_UNIT].level;
    seen10_last_wave_zero_char_id =
        (int) unit0[CH9_LAST_WAVE_ZERO_UNIT].char_id;
    seen10_last_wave_zero_level = (int) unit0[CH9_LAST_WAVE_ZERO_UNIT].level;
    seen10_last_wave_zero_side = (int) unit0[CH9_LAST_WAVE_ZERO_UNIT].side;

    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        seen10_marked_timers[slot] = marked->status_timers[slot];
        seen10_unit0_timers[slot] = unit0->status_timers[slot];
    }
}

/* Runs the chapter 10 handler once, against the shipped containers and the
   fixture cut-scene, and records what it left behind.  The eight members the
   party has when the chapter opens are put on with the game's own add, in join
   order, because this handler adds nobody and MAP09.DAT's eight player slots
   have to be filled from a roster the run staged honestly.  The timer hook and
   the graphics mode are here for the reasons the chapter 1 run gives. */
static void run_chapter_10_handler(void)
{
    int member;

    if (run10_state != 0) {
        return;
    }
    run10_state = 2;

    if (!containers_present()) {
        return;
    }
    if (!stage_fixture_archive()) {
        return;
    }

    stage_globals();
    data_fdps_chapter_current_chapter_id = CHAPTER_10_ID;
    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_entry(MISC_NAME, CURSOR_SHEET_MEMBER);

    for (member = 0; member < PARTY_AT_CHAPTER_10; member++) {
        fdps_roster_add_character(chapter_10_party[member]);
    }

    set_mode(MODE_320X200X256);
    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, tick_isr);

    fdps_chapter_10_init();

    _dos_setvect(TIMER_VECTOR, saved_timer);
    set_mode(MODE_TEXT);

    capture_chapter_10();
    free_chapter_globals();
    run10_state = 1;
}

/* Nobody joins the party this chapter and no player slot is left over.  The
   roster count is the eight the run put on and not nine, which is what says
   the handler has no fdps_roster_add_character in it; the eight player slots
   carry those same eight characters in join order, every one of them on the
   player side with the retired bit clear, so unlike chapter 5's fifth slot
   none of them is the zeroed spare fdps_build_map_unit_array writes for a slot
   with no member behind it.  The two map header counts are read back as well
   because they were staged at numbers no map carries, and MAP09.DAT's 48
   scripted deployments are a number no other map in this file has. */
static void nobody_joins_and_every_slot_is_filled_in_chapter_ten(void)
{
    int slot;

    run_chapter_10_handler();
    CHECK_EQ(run10_state, 1);
    if (run10_state != 1) {
        return;
    }

    CHECK_EQ(seen10_roster_count, PARTY_AT_CHAPTER_10);
    CHECK_EQ(seen10_player_slots, CH9_PLAYER_SLOTS);
    CHECK_EQ(seen10_char_spawns, CH9_CHAR_SPAWNS);
    for (slot = 0; slot < CH9_PLAYER_SLOTS; slot++) {
        CHECK_EQ(seen10_party_char_ids[slot], chapter_10_party[slot]);
    }
    CHECK_EQ(seen10_party_live_slots, CH9_PLAYER_SLOTS);
}

/* What the two middle calls together leave on the map: the eight player slots
   and the eight records MAP09.DAT tags wave 0, for sixteen, with nothing from
   the cut-scene at all -- it switches maps three times and deploys nowhere.
   The census behind that count is the strategy guide's opening 敵方 group
   found by character id: four 步兵, two 騎兵, two 魔導士 and no 弓兵.  The
   two archers MAP09.DAT carries are in its wave 1, the guide's turn-3
   reinforcement, so a zero here is also what says no wave but the opening one
   went down.  The first and last of the eight are read back by index: wave 0
   is deployed in file order onto its own tiles, so record 0 is unit 8 and
   record 7 unit 15. */
static void chapter_ten_opens_with_the_guides_first_enemy_group(void)
{
    run_chapter_10_handler();
    CHECK_EQ(run10_state, 1);
    if (run10_state != 1) {
        return;
    }

    CHECK_EQ(seen10_unit_count, CH9_UNITS_AFTER_SCRIPT);
    CHECK_EQ(seen10_foot_soldiers, CH9_FOOT_COUNT);
    CHECK_EQ(seen10_riders, CH9_RIDER_COUNT);
    CHECK_EQ(seen10_mages, CH9_MAGE_COUNT);
    CHECK_EQ(seen10_archers, CH9_ARCHER_COUNT);
    CHECK_EQ(seen10_first_wave_zero_char_id, CH5_FOOT_CHAR_ID);
    CHECK_EQ(seen10_first_wave_zero_level, CH9_FIRST_WAVE_ZERO_LEVEL);
    CHECK_EQ(seen10_last_wave_zero_char_id, CH5_MAGE_CHAR_ID);
    CHECK_EQ(seen10_last_wave_zero_level, CH9_LAST_WAVE_ZERO_LEVEL);
    CHECK_EQ(seen10_last_wave_zero_side, ENEMY_SIDE);
}

/* The cut-scene the handler names is Icon09.dat and it really ran.  The marker
   sits on unit 15 with a value none of the other nine fixtures writes, and the
   case above has already read that unit back as map 9's LV15 魔導士 rather
   than one of the level-2 stand-ins index 15 holds on the two cut-scene maps,
   so the last SWITCH_MAP ran and the array is the one it rebuilt.  Unit 0's
   own timers are read back clear, which is where ICON00.DAT's and ICON01.DAT's
   markers would have landed, and every other timer on the marked unit is clear
   too: neither the handler nor the shipped cut-scene writes a status on
   anybody this chapter.  The chapter id is back at 9, which the handler
   neither reads nor writes -- the script's own run of switches left it as it
   found it, and both the script number and the title-card graphic are chosen
   from it by the callees. */
static void the_chapter_10_cutscene_is_icon09_dat(void)
{
    int slot;

    run_chapter_10_handler();
    CHECK_EQ(run10_state, 1);
    if (run10_state != 1) {
        return;
    }

    CHECK_EQ(seen10_marked_timers[SCRIPT_CH10_MARKER_SLOT],
             SCRIPT_CH10_MARKER_VALUE);
    for (slot = 0; slot < STATUS_TIMER_COUNT; slot++) {
        if (slot != SCRIPT_CH10_MARKER_SLOT) {
            CHECK_EQ(seen10_marked_timers[slot], 0);
        }
        CHECK_EQ(seen10_unit0_timers[slot], 0);
    }
    CHECK_EQ(seen10_chapter_id, CHAPTER_10_ID);
}

/* The cursor ends on unit 0's tile.  MAP09.COD record 48 -- the first record
   past the map's 48 scripted deployments -- puts the first party slot on tile
   (3, 0), and a player slot is placed on its record exactly, so the cursor
   globals are that tile scaled by the 24-pixel step.  Both were staged at
   other numbers before the run. */
static void the_chapter_10_cursor_is_parked_on_unit_zero(void)
{
    run_chapter_10_handler();
    CHECK_EQ(run10_state, 1);
    if (run10_state != 1) {
        return;
    }

    CHECK_EQ(seen10_cursor_x, CH9_PARTY_TILE_X * CURSOR_TILE_STEP);
    CHECK_EQ(seen10_cursor_y, CH9_PARTY_TILE_Y * CURSOR_TILE_STEP);
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
    RUN_TEST(arc_joins_the_party_before_the_chapter_is_built);
    RUN_TEST(the_chapter_3_guest_hero_is_left_as_the_map_deployed_him);
    RUN_TEST(the_chapter_3_cutscene_is_icon02_dat);
    RUN_TEST(the_chapter_3_cursor_is_parked_on_unit_zero);
    RUN_TEST(flarena_joins_the_roster_when_chapter_four_opens);
    RUN_TEST(flarena_is_not_one_of_the_chapter_four_map_units);
    RUN_TEST(the_chapter_4_guest_hero_is_left_as_the_map_deployed_him);
    RUN_TEST(the_chapter_4_cutscene_is_icon03_dat);
    RUN_TEST(the_chapter_4_cursor_is_parked_on_unit_zero);
    RUN_TEST(nobody_joins_the_party_when_chapter_five_opens);
    RUN_TEST(the_fifth_player_slot_is_the_retired_spare);
    RUN_TEST(the_chapter_5_guest_hero_is_left_as_the_map_deployed_him);
    RUN_TEST(the_chapter_5_cutscene_is_icon04_dat);
    RUN_TEST(the_chapter_5_cursor_is_parked_on_unit_zero);
    RUN_TEST(nobody_joins_and_no_slot_is_left_over_in_chapter_six);
    RUN_TEST(chapter_six_opens_with_the_guides_first_enemy_group);
    RUN_TEST(the_chapter_6_guest_hero_is_left_as_the_map_deployed_him);
    RUN_TEST(the_chapter_6_cutscene_is_icon05_dat);
    RUN_TEST(the_chapter_6_cursor_is_parked_on_unit_zero);
    RUN_TEST(juna_joins_the_roster_when_chapter_seven_opens);
    RUN_TEST(juna_is_not_one_of_the_chapter_seven_map_units);
    RUN_TEST(chapter_seven_opens_with_the_guides_enemy_line);
    RUN_TEST(the_chapter_7_cutscene_is_icon06_dat);
    RUN_TEST(the_chapter_7_cursor_is_parked_on_unit_zero);
    RUN_TEST(feitaga_joins_the_roster_when_chapter_eight_opens);
    RUN_TEST(feitaga_is_not_one_of_the_chapter_eight_map_units);
    RUN_TEST(chapter_eight_opens_on_the_wave_zero_deployments);
    RUN_TEST(the_chapter_8_cutscene_is_icon07_dat);
    RUN_TEST(the_chapter_8_cursor_is_parked_on_unit_zero);
    RUN_TEST(brando_and_gaia_join_the_roster_when_chapter_nine_opens);
    RUN_TEST(brando_and_gaia_take_the_last_two_player_slots);
    RUN_TEST(chapter_nine_opens_on_the_wave_zero_deployments);
    RUN_TEST(the_chapter_9_cutscene_is_icon08_dat);
    RUN_TEST(the_chapter_9_cursor_is_parked_on_unit_zero);
    RUN_TEST(nobody_joins_and_every_slot_is_filled_in_chapter_ten);
    RUN_TEST(chapter_ten_opens_with_the_guides_first_enemy_group);
    RUN_TEST(the_chapter_10_cutscene_is_icon09_dat);
    RUN_TEST(the_chapter_10_cursor_is_parked_on_unit_zero);
    RUN_TEST(the_fixture_container_is_removed);
}
