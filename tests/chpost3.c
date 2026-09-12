/* tests/chpost3.c -- cover for src/chpost3.c.  One section per handler, in the
 * order the handlers appear in the source, each introduced by what its
 * chapter's rules are and which instructions the cases below it pin.
 *
 * These six handlers are the last six slots of the table based at 0006028c,
 * and what they have in common is the map underneath them: chapters 25 to 30
 * all field twelve player slots, so the deployment records begin at unit slot
 * 0x0c and the 魔戰將軍 and 魔導王 the victory tests name stand there.
 * Chapters 25, 26 and 27 each stage an array at their own map's full width,
 * taken from that file's header counts; chapters 28, 29 and 30 name no slot
 * above 0 and share the narrow eight-slot block instead.
 *
 * Four of the six -- chapters 25, 26, 27 and 30 -- never call the shared
 * default end test at 0003a2e0, because their 勝利條件 is a named boss and not
 * 敵人全滅; the cases for those are about the stores those handlers make
 * themselves, and about the sweep they must NOT be doing.  Chapters 28 and 29
 * are bare forwards, and their cases are about the forward really happening.
 *
 * The expected verdicts are read off the handlers' assembly rather than off
 * the emitted C, and for the two forwarding chapters off the shared test's
 * assembly at 0003a2e0: the CMP dword ptr [0x00069da0],0x0 / JNZ at 0003a2ec
 * that abandons the body for a code that is already non-zero, the MOV dword
 * ptr [0x00069da0],0x2 at 0003a2f9 that writes the cleared verdict up front,
 * the CMP byte ptr [EAX+0x6],0x0 / JNZ at 0003a331 and MOV AL,byte ptr
 * [EAX+0x5] / AND AL,0x1 at 0003a33a that put it back to 0 for a live enemy,
 * and the PUSH 0x0 / unguarded MOV dword ptr [0x00069da0],0x1 at
 * 0003a382..0003a390 that makes a retired unit slot 0 a defeat in every
 * chapter but 0x10 and 0x15.  The 0/1/2 meanings of the code are
 * program_info/architecture.md.
 *
 * The unit array is staged here rather than read from a game file: the
 * handlers take no arguments at all, so the array global, the unit count and
 * the chapter id are their entire input.  Nothing below asserts what any of
 * those globals holds on its own -- ticket 23 owns that.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "chpost3.h"

/* Eight slots so every index the "no condition of its own" sweep touches has
   a record of its own.  Chapters 28, 29 and 30 stage on this block; chapters
   25, 26 and 27 have map-sized blocks of their own further down. */
#define STAGE_UNITS 8

/* Side codes, from the record's side byte at offset 6. */
#define SIDE_ENEMY  0
#define SIDE_PLAYER 2

/* Bit 0 of the flags byte at offset 5 is the retirement flag. */
#define FLAG_RETIRED 0x01

/* Chapter 25 is chapter id 24 (0x18), the first of the six and the id the
   shared stager below leaves behind; chapters 28, 29 and 30 each overwrite it
   in their own stager before calling their handler. */
#define CHAPTER_25_ID 24

/* The two chapter ids the shared end test singles out, staged by the "the
   chapter id is never consulted" case in each section below.  Chapter 17's id
   is the 0x10 that test compares against at 0003a356 and chapter 22's is the
   0x15 at 0003a35f, so they are the two ids that would change the answer had
   the handler under test forwarded to it.  Neither chapter's handler lives
   here; both are in tests/chpost2.c. */
#define CHAPTER_17_ID 16
#define CHAPTER_22_ID 21

/* Slot 3 is 法蓮娜: unit slot i is roster slot i and the roster is in join
   order.  She is the one member these chapters leave undeployed, so the slot
   is reserved and empty on every map here. */
#define FARLENA_SLOT 3

static struct fdps_unit_record stage_units[STAGE_UNITS];

/* Zero every slot and publish the block, then set the chapter id and the
   incoming battle-end code.  Every unit starts as a non-retired member of
   side 0, and each case edits only the fields it is about. */
static void stage(int live_unit_count, int battle_end_code)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
    data_fdps_map_unit_count = live_unit_count;
    data_fdps_chapter_current_chapter_id = CHAPTER_25_ID;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static void stage_unit(int unit_index, int side, int flags)
{
    stage_units[unit_index].side = (unsigned char) side;
    stage_units[unit_index].flags = (unsigned char) flags;
}

static int end_code(void)
{
    return (int) data_fdps_chapter_event_or_battle_end_code;
}

/* --------------------------------------------------------------------------
 * Chapter 25, 魔戰將軍 -- fdps_chapter_25_post_action at 0003b6b0.
 *
 * The first handler in this file with no CALL 0x0003a2e0, and the first of the
 * three here -- chapters 25, 26 and 27 -- that declare a victory of their own.  The body is PUSH 0xc / CALL 0x000109b0 /
 * ADD ESP,0x4 / TEST EAX,EAX / JZ 0003b6d8 at 0003b6bc, the same shape for 0xd
 * at 0003b6ca with JNZ 0003b6da and for 0xe at 0003b6da with JNZ 0003b6ea, the
 * MOV dword ptr [0x00069da0],0x2 at 0003b6ea those three reach together, then
 * PUSH 0x0 / CALL 0x000109b0 / TEST EAX,EAX / JZ 0003b70c at 0003b6f4 and the
 * MOV dword ptr [0x00069da0],0x1 at 0003b702.
 *
 * So four things are worth pinning: that all three of slots 12, 13 and 14 are
 * required for the clear and each one alone holds it back, that the clear does
 * not need the rest of the map dead -- there is no sweep here at all -- that
 * both stores are unguarded, and that the defeat store runs after the victory
 * store rather than as its else, which is what makes a 蘭迪斯 who falls on the
 * same action outrank the clear.
 *
 * The expected verdicts come from those instructions and from the guide's
 * chapter 25 entry, 魔戰將軍: 勝利條件：魔戰將軍死亡 and 失敗條件：蘭迪斯死亡,
 * with 己方：法蓮娜以外的所有人.  The 0/1/2 meanings of the code are
 * program_info/architecture.md.
 *
 * The array is staged at its widest: twelve player slots at 0..11, then all 59
 * of MAP24.DAT's deployment records at 12..70.  Twelve is the map's own
 * player-slot count, its header byte +1, and 59 is the record count at byte +2,
 * the file being a 131-byte header and 26 bytes each; 59 matches the guide's
 * enemy list exactly.  The map does not reach that width in one step: 41 of the
 * records are wave 0 (records 0..40, at slots 12..52), so the array holds 53
 * units when the map opens, and the other 18 are wave 1 -- the LV16 天空騎士
 * x18 the chapter's reinforcement event brings on, all character id 97 --
 * appended at 53..70 to make 71.  The chapter's eighteen 鎧甲武士 are character
 * id 100 and are all wave 0; chapter 24 tells the two 18-strong groups apart,
 * since MAP23.DAT's id 100 x6 and id 97 x15 match its guide list 鎧甲武士x6 /
 * 天空騎士x15.  Both are real states of the
 * array and the staging is the later one, which is why the sweep case below
 * says 56 of 59 rather than 38 of 41.  The three warlords are wave-0 records
 * 0, 1 and 2, the file's only level-30 units, so they stand at 12, 13 and 14 in
 * either state -- which is what the cases here are about.
 *
 * The chapter id staged below is 24, the 0-based id whose table slot -- the
 * dword at 000602ec, twenty-four entries into the table based at 0006028c --
 * holds 0003b6b0, and that table entry is the function's only xref.  Nothing in
 * the body reads it, and one case below asserts exactly that.
 * ------------------------------------------------------------------------ */

/* Twelve player slots then all 59 of MAP24.DAT's deployment records: the 41 of
   wave 0 at 12..52 and the 18 of wave 1 at 53..70. */
#define CH25_PARTY_SLOTS 12
#define CH25_SPAWN_RECORDS 59
#define CH25_UNITS (CH25_PARTY_SLOTS + CH25_SPAWN_RECORDS)

/* The three 魔戰將軍: wave-0 deployment records 0, 1 and 2, so slots 12, 13
   and 14. */
#define CH25_WARLORD_1 12
#define CH25_WARLORD_2 13
#define CH25_WARLORD_3 14

/* 蘭迪斯 is roster slot 0, as in every chapter that deploys him. */
#define CH25_RANDIS_SLOT 0

static struct fdps_unit_record stage25_units[CH25_UNITS];

/* Zero the block and publish it: the roster on side 2, the deployment records on
   side 0, nobody retired.  Each case then retires only the slots it is about,
   so unless a case says otherwise the map is full of live enemies -- which is
   what makes the clear cases discriminating, because a body that swept for one
   would answer 0 instead. */
static void stage25(int battle_end_code)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage25_units;
    for (i = 0; i < (int) sizeof(stage25_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH25_UNITS; i++) {
        stage25_units[i].side =
            (unsigned char) (i < CH25_PARTY_SLOTS ? SIDE_PLAYER : SIDE_ENEMY);
    }

    data_fdps_map_unit_array_ptr = (unsigned char *) stage25_units;
    data_fdps_map_unit_count = CH25_UNITS;
    data_fdps_chapter_current_chapter_id = CHAPTER_25_ID;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static void retire25(int unit_index)
{
    stage25_units[unit_index].flags = FLAG_RETIRED;
}

/* 勝利條件：魔戰將軍死亡.  All three warlords retired writes the 2 at 0003b6ea
   with 56 enemies still standing, which is the case that separates this handler
   from every forwarding one in the file: the shared test's sweep would answer 0
   on this map. */
static void chapter_25_the_three_warlords_falling_clears_the_chapter(void)
{
    stage25(0);
    retire25(CH25_WARLORD_1);
    retire25(CH25_WARLORD_2);
    retire25(CH25_WARLORD_3);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Each warlord alone holds the chapter open, which pins all three indices and
   the && chain: any one of the three tests dropped, or an index off by one,
   would clear the chapter in one of these three stagings. */
static void chapter_25_one_standing_warlord_holds_the_chapter_open(void)
{
    stage25(0);
    retire25(CH25_WARLORD_2);
    retire25(CH25_WARLORD_3);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 0);

    stage25(0);
    retire25(CH25_WARLORD_1);
    retire25(CH25_WARLORD_3);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 0);

    stage25(0);
    retire25(CH25_WARLORD_1);
    retire25(CH25_WARLORD_2);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The rest of the map dying settles nothing.  Every deployment record but the
   three warlords is retired here -- 56 of the 59, the map after its wave-1
   reinforcement -- and the code stays 0, because there is no 敵人全滅 sweep in
   this handler and chapter 25 is not won that way. */
static void chapter_25_killing_everything_but_the_warlords_settles_nothing(void)
{
    int slot;

    stage25(0);
    for (slot = CH25_PARTY_SLOTS; slot < CH25_UNITS; slot++) {
        if (slot == CH25_WARLORD_1 || slot == CH25_WARLORD_2 ||
            slot == CH25_WARLORD_3) {
            continue;
        }
        retire25(slot);
    }
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 0);
}

/* 失敗條件：蘭迪斯死亡.  With the warlords standing the victory test writes
   nothing and the slot-0 test at 0003b6f4 is what answers. */
static void chapter_25_a_retired_randis_is_a_defeat(void)
{
    stage25(0);
    retire25(CH25_RANDIS_SLOT);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The case the rebuild note is about.  Both stores run on this staging and the
   defeat's is the later one, so the answer is 1: writing the two tests as
   if/else, as an else-if, or with the shared test's "only while the code is
   still 0" guard on the defeat store would leave the 2 here and clear a chapter
   the original ends with a Game Over. */
static void chapter_25_randis_falling_with_the_last_warlord_is_a_defeat(void)
{
    stage25(0);
    retire25(CH25_WARLORD_1);
    retire25(CH25_WARLORD_2);
    retire25(CH25_WARLORD_3);
    retire25(CH25_RANDIS_SLOT);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The other unguarded store, seen from the other side: a defeat a chapter event
   recorded before this handler ran is overwritten by the clear, because the
   victory store consults nothing either.  Guarding it on the code still being 0
   would leave the 1 standing. */
static void chapter_25_a_recorded_defeat_is_overwritten_by_the_clear(void)
{
    stage25(1);
    retire25(CH25_WARLORD_1);
    retire25(CH25_WARLORD_2);
    retire25(CH25_WARLORD_3);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 2);
}

/* With a warlord standing and 蘭迪斯 alive neither store is reached, so a
   verdict already in the code comes back untouched -- including on a map whose
   enemies are all dead but for the warlords, which a body that swept would have
   recomputed. */
static void chapter_25_a_recorded_verdict_survives_an_undecided_action(void)
{
    stage25(2);
    retire25(CH25_WARLORD_1);
    retire25(CH25_WARLORD_2);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 2);

    stage25(1);
    retire25(CH25_WARLORD_1);
    retire25(CH25_WARLORD_2);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No slot but 0, 12, 13 and 14 is consulted at all.  Every other slot of the 71
   is retired on its own with the four that matter left standing, and the code
   has to stay 0 each time.  Slot 3 is in that sweep and is the one worth
   naming: it is 法蓮娜's, the index chapters 17 and 22 pass, and she is not even
   deployed here -- 己方：法蓮娜以外的所有人.  Slots 11 and 15 are in it too, so
   a warlord index that drifted either way is caught. */
static void chapter_25_no_other_slot_ends_the_battle(void)
{
    int retired_slot;

    for (retired_slot = 1; retired_slot < CH25_UNITS; retired_slot++) {
        if (retired_slot == CH25_WARLORD_1 || retired_slot == CH25_WARLORD_2 ||
            retired_slot == CH25_WARLORD_3) {
            continue;
        }
        stage25(0);
        retire25(retired_slot);
        fdps_chapter_25_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* An undecided action leaves the code alone on both sides: nothing is stored
   before the first CALL and nothing after the last one.  It is called twice
   because the dispatchers run it after every unit action, and a handler that
   only behaved on its first call would still pass every case above. */
static void chapter_25_an_open_battle_stays_open(void)
{
    stage25(0);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The verdict is stable across calls on the decided paths as well. */
static void chapter_25_the_verdict_is_stable_across_calls(void)
{
    stage25(0);
    retire25(CH25_WARLORD_1);
    retire25(CH25_WARLORD_2);
    retire25(CH25_WARLORD_3);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 2);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 2);

    stage25(0);
    retire25(CH25_RANDIS_SLOT);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 1);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The body contains no CMP against data_fdps_chapter_current_chapter_id, so the
   answer cannot depend on it.  Staging chapter 17's id and then chapter 22's --
   the two the shared test singles out, and the two that would change the answer
   had this handler forwarded to it -- changes nothing. */
static void chapter_25_the_chapter_id_is_never_consulted(void)
{
    stage25(0);
    data_fdps_chapter_current_chapter_id = CHAPTER_17_ID;
    retire25(CH25_WARLORD_1);
    retire25(CH25_WARLORD_2);
    retire25(CH25_WARLORD_3);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 2);

    stage25(0);
    data_fdps_chapter_current_chapter_id = CHAPTER_22_ID;
    retire25(FARLENA_SLOT);
    fdps_chapter_25_post_action();
    CHECK_EQ(end_code(), 0);
}

/* --------------------------------------------------------------------------
 * Chapter 26, 狂信人之塔 -- fdps_chapter_26_post_action at 0003b760.
 *
 * The second handler in this file with no CALL 0x0003a2e0, and the only one with
 * a test that is switched on part-way through the map.  The body is PUSH 0xc /
 * CALL 0x000109b0 / ADD ESP,0x4 / TEST EAX,EAX / JZ 0003b788 at 0003b76c, the
 * same shape for 0xd at 0003b77a, 0xe at 0003b78a and 0xf at 0003b79a, the MOV
 * dword ptr [0x00069da0],0x2 at 0003b7aa those four reach together, then PUSH
 * 0x0 / CALL 0x000109b0 / TEST EAX,EAX / JZ 0003b7cc at 0003b7b4 with the MOV
 * dword ptr [0x00069da0],0x1 at 0003b7c2, and finally XOR EAX,EAX / MOV AL,byte
 * ptr [0x000640e8] / CMP EAX,0x1 / JNZ 0003b7e6 at 0003b7cc guarding PUSH 0x5b /
 * CALL 0x000109b0 / TEST EAX,EAX / JNZ at 0003b7d8 and the MOV dword ptr
 * [0x00069da0],0x1 at 0003b7e8.
 *
 * So five things are worth pinning: that all four of slots 12..15 are required
 * for the clear and each one alone holds it back, that the clear does not need
 * the rest of the map dead -- there is no sweep here at all -- that all three
 * stores are unguarded and run in the order victory, slot 0, slot 0x5b, that the
 * third test is skipped entirely unless the latch byte reads exactly 1, and that
 * the slot it then asks about is 0x5b and no other ally slot.
 *
 * The expected verdicts come from those instructions and from the guide's
 * chapter 26 entry, 狂信人之塔: 勝利條件：擊倒魔戰將軍 and 失敗條件：蘭迪斯死亡，
 * 索爾死亡, with 己方：法蓮娜以外的所有人 and 友方：LV40英雄索爾 plus LV40侍衛x4.
 * The 0/1/2 meanings of the code are program_info/architecture.md.
 *
 * The array is staged at its widest: twelve player slots at 0..11, then all 80
 * of MAP25.DAT's deployment records at 12..91.  Twelve is the map's own
 * player-slot count, its header byte +1, and 80 is the record count at byte +2,
 * the file being a 131-byte header and 26 bytes each.  The map does not reach
 * that width in one step: 68 of the records are wave 0 and land at 12..79 when
 * the map opens, and the remaining twelve arrive together when
 * fdps_chapter_26_event_deploy_waves_2_and_3 fires -- wave 2 first, its seven
 * side-0 records at 0x50..0x56, then wave 3, its five side-1 records at
 * 0x57..0x5b.  Slot 0x5b therefore exists only in the later state, which is the
 * state staged here; the latch is what the handler uses to tell the two apart
 * and the cases below drive it directly.
 *
 * Record 62, the first of wave 3, is character id 0x0c at level 40 and records
 * 63..66 are four copies of character id 0x3b at level 40, so 索爾 is slot 0x57
 * and his four 侍衛 are 0x58..0x5b.  That identification is FRIAPRDA.DAT row
 * 0x0c plus FRILEVUP.DAT's all-ones growth for the same row run through the unit
 * builder's HP = hp_base + (LV-1) * hp_min and AP = ap_base + LV * ap_min: at
 * level 40 they give the guide's 索爾 in every field, HP999 MP519 DX200 and
 * AP740 DP310 once 炎龍劍 and 大地鎧甲 are counted.  The handler watches 0x5b,
 * the last 侍衛, and not 0x57.
 *
 * The chapter id staged below is 25, the 0-based id whose table slot -- the
 * dword at 000602f0, twenty-five entries into the table based at 0006028c --
 * holds 0003b760, and that table entry is the function's only xref.  Nothing in
 * the body reads it, and one case below asserts exactly that.
 * ------------------------------------------------------------------------ */

/* Chapter 26 is chapter id 25 (0x19). */
#define CHAPTER_26_ID 25

/* Twelve player slots then all 80 of MAP25.DAT's deployment records: the 68 of
   wave 0 at 12..79, the 7 of wave 2 at 80..86 and the 5 of wave 3 at 87..91. */
#define CH26_PARTY_SLOTS 12
#define CH26_SPAWN_RECORDS 80
#define CH26_UNITS (CH26_PARTY_SLOTS + CH26_SPAWN_RECORDS)

/* The first side-1 slot: wave 3 begins at 0x57 and runs to the end of the
   array. */
#define CH26_FIRST_ALLY_SLOT 0x57

/* The four 魔戰將軍: wave-0 deployment records 0, 1, 2 and 3, so slots 12, 13,
   14 and 15. */
#define CH26_WARLORD_1 0x0c
#define CH26_WARLORD_2 0x0d
#define CH26_WARLORD_3 0x0e
#define CH26_WARLORD_4 0x0f

/* 蘭迪斯 is roster slot 0, as in every chapter that deploys him. */
#define CH26_RANDIS_SLOT 0

/* 索爾 is wave-3 record 62, the first of the five allies. */
#define CH26_SOL_SLOT 0x57

/* The slot the handler actually watches: the last of 索爾's four 侍衛, which is
   also the last unit slot the map ever holds. */
#define CH26_ALLY_GUARD_LAST_SLOT 0x5b

/* Side 1 is the allied/neutral side, the side byte the five wave-3 records
   carry. */
#define SIDE_ALLY 1

/* Element 0x10 of data_fdps_map_cell_event_triggered_flags: the one-shot latch
   fdps_chapter_26_event_deploy_waves_2_and_3 sets to 1 at 0003939c. */
#define CH26_LATCH_SLOT 0x10

static struct fdps_unit_record stage26_units[CH26_UNITS];

/* Zero the block and publish it: the roster on side 2, the wave-0 and wave-2
   deployment records on side 0, the five wave-3 records on side 1, nobody
   retired.  The whole latch array is cleared and then the one slot the handler
   reads is set, so a handler that read a neighbouring element would see 0.

   Each case retires only the slots it is about, so unless a case says otherwise
   the map is full of live enemies -- which is what makes the clear cases
   discriminating, because a body that swept for one would answer 0 instead. */
static void stage26(int battle_end_code, int latch_value)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage26_units;
    for (i = 0; i < (int) sizeof(stage26_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH26_UNITS; i++) {
        if (i < CH26_PARTY_SLOTS) {
            stage26_units[i].side = (unsigned char) SIDE_PLAYER;
        } else if (i < CH26_FIRST_ALLY_SLOT) {
            stage26_units[i].side = (unsigned char) SIDE_ENEMY;
        } else {
            stage26_units[i].side = (unsigned char) SIDE_ALLY;
        }
    }
    for (i = 0; i < 32; i++) {
        data_fdps_map_cell_event_triggered_flags[i] = 0;
    }
    data_fdps_map_cell_event_triggered_flags[CH26_LATCH_SLOT] =
        (unsigned char) latch_value;

    data_fdps_map_unit_array_ptr = (unsigned char *) stage26_units;
    data_fdps_map_unit_count = CH26_UNITS;
    data_fdps_chapter_current_chapter_id = CHAPTER_26_ID;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static void retire26(int unit_index)
{
    stage26_units[unit_index].flags = FLAG_RETIRED;
}

/* 勝利條件：擊倒魔戰將軍.  All four warlords retired writes the 2 at 0003b7aa
   with 72 enemies still standing, which is the case that separates this handler
   from every forwarding one in the file: the shared test's sweep would answer 0
   on this map. */
static void chapter_26_the_four_warlords_falling_clears_the_chapter(void)
{
    stage26(0, 0);
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_3);
    retire26(CH26_WARLORD_4);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Each warlord alone holds the chapter open, which pins all four indices and the
   && chain: any one of the four tests dropped, or an index off by one, would
   clear the chapter in one of these four stagings.  Chapter 25's handler asks
   about three slots and this one about four, so a copy of that handler carried
   forward would clear here on the fourth staging. */
static void chapter_26_one_standing_warlord_holds_the_chapter_open(void)
{
    stage26(0, 0);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_3);
    retire26(CH26_WARLORD_4);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);

    stage26(0, 0);
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_3);
    retire26(CH26_WARLORD_4);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);

    stage26(0, 0);
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_4);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);

    stage26(0, 0);
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_3);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The rest of the map dying settles nothing.  Every deployment record but the
   four warlords is retired here -- 76 of the 80, the map after both waves have
   landed -- and the code stays 0, because there is no 敵人全滅 sweep in this
   handler and chapter 26 is not won that way.  The latch is left at 0 so the
   retired allies in that block cannot answer instead. */
static void chapter_26_killing_everything_but_the_warlords_settles_nothing(void)
{
    int slot;

    stage26(0, 0);
    for (slot = CH26_PARTY_SLOTS; slot < CH26_UNITS; slot++) {
        if (slot == CH26_WARLORD_1 || slot == CH26_WARLORD_2 ||
            slot == CH26_WARLORD_3 || slot == CH26_WARLORD_4) {
            continue;
        }
        retire26(slot);
    }
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);
}

/* 失敗條件：蘭迪斯死亡.  With the warlords standing the victory test writes
   nothing and the slot-0 test at 0003b7b4 is what answers. */
static void chapter_26_a_retired_randis_is_a_defeat(void)
{
    stage26(0, 0);
    retire26(CH26_RANDIS_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The first half of the rebuild note.  Both the victory store and the slot-0
   store run on this staging and the defeat's is the later one, so the answer is
   1: writing the two as if/else, as an else-if, or with a "only while the code
   is still 0" guard on the defeat store would leave the 2 here and clear a
   chapter the original ends with a Game Over. */
static void chapter_26_randis_falling_with_the_last_warlord_is_a_defeat(void)
{
    stage26(0, 0);
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_3);
    retire26(CH26_WARLORD_4);
    retire26(CH26_RANDIS_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The same store seen from the other side: a defeat a chapter event recorded
   before this handler ran is overwritten by the clear, because the victory store
   consults nothing either.  Guarding it on the code still being 0 would leave
   the 1 standing. */
static void chapter_26_a_recorded_defeat_is_overwritten_by_the_clear(void)
{
    stage26(1, 0);
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_3);
    retire26(CH26_WARLORD_4);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The gate at 0003b7cc.  With the latch still 0 -- the state of the map before
   fdps_chapter_26_event_deploy_waves_2_and_3 has brought the relief force on --
   a retired slot 0x5b decides nothing, because the PUSH 0x5b is never reached.
   That is what keeps the read inside the unit array in real play: the array is
   80 units wide until that event runs, and fdps_unit_is_retired bounds
   nothing. */
static void chapter_26_the_ally_test_is_skipped_before_the_latch_is_set(void)
{
    int slot;

    stage26(0, 0);
    retire26(CH26_ALLY_GUARD_LAST_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);

    stage26(0, 0);
    for (slot = CH26_FIRST_ALLY_SLOT; slot < CH26_UNITS; slot++) {
        retire26(slot);
    }
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);
}

/* 失敗條件 索爾死亡, as the code actually spells it.  Once the latch reads 1 a
   retired slot 0x5b is a defeat. */
static void chapter_26_a_retired_ally_guard_is_a_defeat_once_the_latch_is_set(
    void)
{
    stage26(0, 1);
    retire26(CH26_ALLY_GUARD_LAST_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The gate is an equality against 1, not a non-zero test: CMP EAX,0x1 / JNZ at
   0003b7d3, against the CMP byte ptr [0x000640e8],0x0 / JNZ that
   fdps_chapter_05_event_enemies_advance uses on the same byte.  Every writer in
   the image stores 1, so this pins the emitted comparison rather than a game
   behaviour -- but a rewrite that copied the event handlers' guard would answer
   1 on both stagings below. */
static void chapter_26_the_latch_test_is_an_equality_against_one(void)
{
    stage26(0, 2);
    retire26(CH26_ALLY_GUARD_LAST_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);

    stage26(0, 0xff);
    retire26(CH26_ALLY_GUARD_LAST_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The latch byte is read zero-extended -- XOR EAX,EAX / MOV AL,[0x000640e8] at
   0003b7cc -- so 0x81, whose low byte would compare equal to 1 only if the
   value were sign-extended or narrowed wrongly, skips the test like any other
   non-1 value. */
static void chapter_26_the_latch_byte_is_zero_extended(void)
{
    stage26(0, 0x81);
    retire26(CH26_ALLY_GUARD_LAST_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The watched slot is 0x5b and no other ally.  索爾 himself is 0x57 -- the first
   wave-3 record, character id 0x0c at level 40 -- and the three 侍衛 before the
   last are 0x58, 0x59 and 0x5a; retiring any of them with the latch set leaves
   the battle open.  This is the case that separates the code's rule from the
   guide's stated 索爾死亡, which would have watched 0x57. */
static void chapter_26_no_ally_slot_but_the_last_ends_the_battle(void)
{
    int slot;

    for (slot = CH26_FIRST_ALLY_SLOT; slot < CH26_ALLY_GUARD_LAST_SLOT;
         slot++) {
        stage26(0, 1);
        retire26(slot);
        fdps_chapter_26_post_action();
        CHECK_EQ(end_code(), 0);
    }

    stage26(0, 1);
    retire26(CH26_SOL_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The second half of the rebuild note: the escort store is the LAST of the
   three, so an action that retires the final warlord and slot 0x5b together is a
   Game Over rather than a clear.  A handler that stopped after the slot-0 test,
   or that made the third test the victory's else, would answer 2 here. */
static void chapter_26_the_ally_defeat_outranks_the_clear(void)
{
    stage26(0, 1);
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_3);
    retire26(CH26_WARLORD_4);
    retire26(CH26_ALLY_GUARD_LAST_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 1);
}

/* With a warlord standing, 蘭迪斯 alive and the escort alive, none of the three
   stores is reached, so a verdict already in the code comes back untouched --
   including on a map whose enemies are all dead but for the warlords, which a
   body that swept would have recomputed. */
static void chapter_26_a_recorded_verdict_survives_an_undecided_action(void)
{
    stage26(2, 1);
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_3);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 2);

    stage26(1, 1);
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_3);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No slot but 0, 12, 13, 14, 15 and 0x5b is consulted at all.  Every other slot
   of the 92 is retired on its own, with the latch set so the escort test is live
   and the four that matter left standing, and the code has to stay 0 each time.
   Slot 3 is in that sweep and is the one worth naming: it is 法蓮娜's, the index
   chapters 17 and 22 pass, and she is not even deployed here --
   己方：法蓮娜以外的所有人.  Slots 11 and 16 are in it too, so a warlord index
   that drifted either way is caught, and so are 0x50..0x56, the wave-2 enemies
   that sit between the garrison and the allies. */
static void chapter_26_no_other_slot_ends_the_battle(void)
{
    int retired_slot;

    for (retired_slot = 1; retired_slot < CH26_UNITS; retired_slot++) {
        if (retired_slot == CH26_WARLORD_1 || retired_slot == CH26_WARLORD_2 ||
            retired_slot == CH26_WARLORD_3 || retired_slot == CH26_WARLORD_4 ||
            retired_slot == CH26_ALLY_GUARD_LAST_SLOT) {
            continue;
        }
        stage26(0, 1);
        retire26(retired_slot);
        fdps_chapter_26_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* The handler reads one element of the latch array and no other.  Every other
   element is set to 1 with element 0x10 left at 0, and the escort test stays
   shut, so neither a neighbouring index nor a folded base is being read. */
static void chapter_26_only_latch_slot_16_gates_the_ally_test(void)
{
    int flag_slot;

    stage26(0, 0);
    for (flag_slot = 0; flag_slot < 32; flag_slot++) {
        if (flag_slot != CH26_LATCH_SLOT) {
            data_fdps_map_cell_event_triggered_flags[flag_slot] = 1;
        }
    }
    retire26(CH26_ALLY_GUARD_LAST_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);
}

/* An undecided action leaves the code alone on both sides: nothing is stored
   before the first CALL and nothing after the last one.  It is called twice
   because the dispatchers run it after every unit action, and a handler that
   only behaved on its first call would still pass every case above. */
static void chapter_26_an_open_battle_stays_open(void)
{
    stage26(0, 1);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The verdict is stable across calls on the decided paths as well, and the
   handler leaves the latch it read alone. */
static void chapter_26_the_verdict_is_stable_across_calls(void)
{
    stage26(0, 1);
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_3);
    retire26(CH26_WARLORD_4);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 2);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 2);

    stage26(0, 1);
    retire26(CH26_ALLY_GUARD_LAST_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 1);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26_LATCH_SLOT], 1);
}

/* The body contains no CMP against data_fdps_chapter_current_chapter_id, so the
   answer cannot depend on it.  Staging chapter 17's id and then chapter 22's --
   the two the shared test singles out, and the two that would change the answer
   had this handler forwarded to it -- changes nothing. */
static void chapter_26_the_chapter_id_is_never_consulted(void)
{
    stage26(0, 1);
    data_fdps_chapter_current_chapter_id = CHAPTER_17_ID;
    retire26(CH26_WARLORD_1);
    retire26(CH26_WARLORD_2);
    retire26(CH26_WARLORD_3);
    retire26(CH26_WARLORD_4);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 2);

    stage26(0, 1);
    data_fdps_chapter_current_chapter_id = CHAPTER_22_ID;
    retire26(FARLENA_SLOT);
    fdps_chapter_26_post_action();
    CHECK_EQ(end_code(), 0);
}

/* --------------------------------------------------------------------------
 * Chapter 27, 魔導士的野望 -- fdps_chapter_27_post_action at 0003b8a0.
 *
 * The third handler in this file with no CALL 0x0003a2e0, and the narrowest:
 * the body is PUSH 0xc / CALL 0x000109b0 / ADD ESP,0x4 / TEST EAX,EAX / JZ
 * 0003b8c4 at 0003b8ac, the MOV dword ptr [0x00069da0],0x2 at 0003b8ba that
 * guards, then PUSH 0x0 / CALL 0x000109b0 / ADD ESP,0x4 / TEST EAX,EAX / JZ
 * 0003b8dc at 0003b8c4 and the MOV dword ptr [0x00069da0],0x1 at 0003b8d2.
 *
 * So four things are worth pinning: that slot 12 alone clears the chapter, that
 * the clear does not need the rest of the map dead -- there is no sweep here at
 * all -- that both stores are unguarded, and that the defeat store runs after
 * the victory store rather than as its else, which is what makes a 蘭迪斯 who
 * falls on the same action outrank the clear.
 *
 * The expected verdicts come from those instructions and from the guide's
 * chapter 27 entry, 魔導士的野望: 勝利條件：魔導王死亡 and 失敗條件：蘭迪斯死亡,
 * with 己方：法蓮娜以外的所有人.  The 0/1/2 meanings of the code are
 * program_info/architecture.md.
 *
 * The array is staged at its widest: twelve player slots at 0..11, then all 55
 * of MAP26.DAT's deployment records at 12..66.  Twelve is the map's own
 * player-slot count, its header byte +1, and 55 is the record count at byte +2,
 * the file being a 131-byte header and 26 bytes each; 55 matches the guide's
 * enemy list exactly -- one LV40, four LV30 and fifty LV18, which is the level
 * byte's own histogram.  The map does not reach that width in one step: 25 of
 * the records are wave 0 and land at slots 12..36 when the map opens, and the
 * other 30 are wave 1, the reinforcements the guide says arrive once the four
 * 魔戰將軍 are down, appended at 37..66.  Both are real states of the array and
 * the staging is the later one, which is why the sweep case below retires 54 of
 * 55.  魔導王吉歐 is wave-0 record 0, the file's only level-40 unit, so he
 * stands at slot 12 in either state.
 *
 * Slots 13..16 are the four 魔戰將軍, wave-0 records 1..4.  They matter here
 * only because nothing in this handler asks about them: the sweep case retires
 * each of them on its own and requires the code to stay 0, which is what catches
 * chapter 26's four-slot victory chain copied into this chapter.
 *
 * The chapter id staged below is 26, the 0-based id whose table slot -- the
 * dword at 000602f4, twenty-six entries into the table based at 0006028c --
 * holds 0003b8a0, and that table entry is the function's only xref.  Nothing in
 * the body reads it, and one case below asserts exactly that.
 * ------------------------------------------------------------------------ */

/* Chapter 27 is chapter id 26 (0x1a). */
#define CHAPTER_27_ID 26

/* Twelve player slots then all 55 of MAP26.DAT's deployment records: the 25 of
   wave 0 at 12..36 and the 30 of wave 1 at 37..66. */
#define CH27_PARTY_SLOTS 12
#define CH27_SPAWN_RECORDS 55
#define CH27_UNITS (CH27_PARTY_SLOTS + CH27_SPAWN_RECORDS)

/* LV40魔導王吉歐: wave-0 deployment record 0, so slot 12. */
#define CH27_MAGE_KING 12

/* The four 魔戰將軍, wave-0 records 1..4.  Not tested by this handler. */
#define CH27_WARLORD_FIRST 13
#define CH27_WARLORD_LAST 16

/* 蘭迪斯 is roster slot 0, as in every chapter that deploys him. */
#define CH27_RANDIS_SLOT 0

static struct fdps_unit_record stage27_units[CH27_UNITS];

/* Zero the block and publish it: the roster on side 2, the deployment records on
   side 0, nobody retired.  Each case then retires only the slots it is about, so
   unless a case says otherwise the map is full of live enemies -- which is what
   makes the clear cases discriminating, because a body that swept for one would
   answer 0 instead. */
static void stage27(int battle_end_code)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage27_units;
    for (i = 0; i < (int) sizeof(stage27_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH27_UNITS; i++) {
        stage27_units[i].side =
            (unsigned char) (i < CH27_PARTY_SLOTS ? SIDE_PLAYER : SIDE_ENEMY);
    }

    data_fdps_map_unit_array_ptr = (unsigned char *) stage27_units;
    data_fdps_map_unit_count = CH27_UNITS;
    data_fdps_chapter_current_chapter_id = CHAPTER_27_ID;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static void retire27(int unit_index)
{
    stage27_units[unit_index].flags = FLAG_RETIRED;
}

/* 勝利條件：魔導王死亡.  A retired slot 12 writes the 2 at 0003b8ba with the
   other 54 enemies still standing, which is the case that separates this
   handler from every forwarding one in the file: the shared test's sweep would
   answer 0 on this map. */
static void chapter_27_the_mage_king_falling_clears_the_chapter(void)
{
    stage27(0);
    retire27(CH27_MAGE_KING);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The rest of the map dying settles nothing.  Every deployment record but 吉歐
   is retired here -- 54 of the 55, the map after its wave-1 reinforcement -- and
   the code stays 0, because there is no 敵人全滅 sweep in this handler and
   chapter 27 is not won that way.  The four 魔戰將軍 are in that sweep, so a
   victory test carried over from chapter 26 fails here. */
static void chapter_27_killing_everything_but_the_mage_king_settles_nothing(void)
{
    int slot;

    stage27(0);
    for (slot = CH27_PARTY_SLOTS; slot < CH27_UNITS; slot++) {
        if (slot == CH27_MAGE_KING) {
            continue;
        }
        retire27(slot);
    }
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 0);
}

/* 失敗條件：蘭迪斯死亡.  With 吉歐 standing the victory test writes nothing and
   the slot-0 test at 0003b8c4 is what answers. */
static void chapter_27_a_retired_randis_is_a_defeat(void)
{
    stage27(0);
    retire27(CH27_RANDIS_SLOT);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The case the rebuild note is about.  Both stores run on this staging and the
   defeat's is the later one, so the answer is 1: writing the two tests as
   if/else, as an else-if, or with the shared test's "only while the code is
   still 0" guard on the defeat store would leave the 2 here and clear a chapter
   the original ends with a Game Over. */
static void chapter_27_randis_falling_with_the_mage_king_is_a_defeat(void)
{
    stage27(0);
    retire27(CH27_MAGE_KING);
    retire27(CH27_RANDIS_SLOT);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The other unguarded store, seen from the other side: a defeat a chapter event
   recorded before this handler ran is overwritten by the clear, because the
   victory store consults nothing either.  Guarding it on the code still being 0
   would leave the 1 standing. */
static void chapter_27_a_recorded_defeat_is_overwritten_by_the_clear(void)
{
    stage27(1);
    retire27(CH27_MAGE_KING);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 2);
}

/* With 吉歐 standing and 蘭迪斯 alive neither store is reached, so a verdict
   already in the code comes back untouched -- including on a map whose enemies
   are all dead but for 吉歐, which a body that swept would have recomputed. */
static void chapter_27_a_recorded_verdict_survives_an_undecided_action(void)
{
    int slot;

    stage27(2);
    for (slot = CH27_WARLORD_FIRST; slot <= CH27_WARLORD_LAST; slot++) {
        retire27(slot);
    }
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 2);

    stage27(1);
    for (slot = CH27_WARLORD_FIRST; slot <= CH27_WARLORD_LAST; slot++) {
        retire27(slot);
    }
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No slot but 0 and 12 is consulted at all.  Every other slot of the 67 is
   retired on its own with those two left standing, and the code has to stay 0
   each time.  Slot 3 is in that sweep and is the one worth naming: it is
   法蓮娜's, the index chapters 17 and 22 pass, and she is not even deployed here
   -- 己方：法蓮娜以外的所有人.  Slots 11 and 13 are in it too, so a boss index
   that drifted either way is caught. */
static void chapter_27_no_other_slot_ends_the_battle(void)
{
    int retired_slot;

    for (retired_slot = 1; retired_slot < CH27_UNITS; retired_slot++) {
        if (retired_slot == CH27_MAGE_KING) {
            continue;
        }
        stage27(0);
        retire27(retired_slot);
        fdps_chapter_27_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* An undecided action leaves the code alone on both sides: nothing is stored
   before the first CALL and nothing after the last one.  It is called twice
   because the dispatchers run it after every unit action, and a handler that
   only behaved on its first call would still pass every case above. */
static void chapter_27_an_open_battle_stays_open(void)
{
    stage27(0);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The verdict is stable across calls on the decided paths as well. */
static void chapter_27_the_verdict_is_stable_across_calls(void)
{
    stage27(0);
    retire27(CH27_MAGE_KING);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 2);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 2);

    stage27(0);
    retire27(CH27_RANDIS_SLOT);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 1);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The body contains no CMP against data_fdps_chapter_current_chapter_id, so the
   answer cannot depend on it.  Staging chapter 17's id and then chapter 22's --
   the two the shared test singles out, and the two that would change the answer
   had this handler forwarded to it -- changes nothing. */
static void chapter_27_the_chapter_id_is_never_consulted(void)
{
    stage27(0);
    data_fdps_chapter_current_chapter_id = CHAPTER_17_ID;
    retire27(CH27_MAGE_KING);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 2);

    stage27(0);
    data_fdps_chapter_current_chapter_id = CHAPTER_22_ID;
    retire27(FARLENA_SLOT);
    fdps_chapter_27_post_action();
    CHECK_EQ(end_code(), 0);
}

/* ------------------------------------------------------------------------
 * Chapter 28's handler, 0003b990: PUSH EBX/ESI/EDI/EBP at 0003b990..0003b993,
 * MOV EBP,ESP at 0003b994, SUB ESP,0x0 at 0003b996, CALL 0x0003a2e0 at
 * 0003b99c, then the four POPs at 0003b9a1..0003b9a4 and the RET at 0003b9a5.
 * Like chapters 16's, 18's and 21's it has no store, no compare and no second
 * call, so the two things worth pinning are that the shared default end test
 * really runs and that nothing else does.
 *
 * The expected verdicts are the shared test's, read off its assembly at
 * 0003a2e0: the CMP dword ptr [0x00069da0],0x0 / JNZ at 0003a2ec that abandons
 * the body for a code that is already non-zero, the MOV dword ptr
 * [0x00069da0],0x2 at 0003a2f9 that writes the cleared verdict up front, the
 * CMP byte ptr [EAX+0x6],0x0 / JNZ at 0003a331 and MOV AL,byte ptr [EAX+0x5] /
 * AND AL,0x1 at 0003a33a that put it back to 0 for a live enemy, and the PUSH
 * 0x0 / unguarded MOV dword ptr [0x00069da0],0x1 at 0003a382..0003a390 that
 * makes a retired unit slot 0 a defeat in every chapter but 0x10 and 0x15.
 *
 * The chapter id staged throughout is 27, because the table is indexed by the
 * 0-based chapter id and this handler is slot 27: the dword at 000602f8,
 * twenty-seven entries into the table based at 0006028c, is 0003b990, and that
 * table entry is the function's only xref.
 *
 * The chapter's own conditions really are the shared test's two and nothing
 * more: the guide's chapter 28 entry, 異界之封印, states 勝利條件：敵人全滅
 * and 失敗條件：蘭迪斯死亡.  Its only scripted business is the three
 * reinforcements that arrive along the top edge at the end of the player phase
 * on the turns the same entry enumerates, 第二、四、六、七、十、十二、十四、
 * 十六、十八回合 -- 2, 4, 6, 7, 10, 12, 14, 16, 18, an enumeration rather than
 * the even turns, since 7 is in it and 8 is not.  That is a turn event, so none
 * of it may show up as a verdict from this handler.
 *
 * The unit array is staged rather than read from the map file: the handler
 * takes no arguments, so the array, the unit count and the chapter id are its
 * entire input.  Nothing below asserts what any of those globals holds on its
 * own -- ticket 23 owns that.
 * ------------------------------------------------------------------------ */

/* Chapter 28 is chapter id 27 (0x1b). */
#define CHAPTER_28_ID 27

static void stage28(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_28_ID;
}

/* 勝利條件：敵人全滅.  The CALL is really taken: with every enemy retired the
   shared test's up front 2 survives, and a handler whose body did nothing would
   leave the 0 it was given. */
static void chapter_28_clears_when_every_enemy_is_retired(void)
{
    stage28(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_28_post_action();
    CHECK_EQ(end_code(), 2);
}

/* One live enemy puts the code back to 0, so the walk inside the shared test is
   reached through this handler and not short circuited by anything in front of
   the CALL. */
static void chapter_28_a_live_enemy_keeps_the_battle_going(void)
{
    stage28(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_28_post_action();
    CHECK_EQ(end_code(), 0);
}

/* 失敗條件：蘭迪斯死亡.  Chapter id 27 is neither 0x10 nor 0x15, so the shared
   test watches unit slot 0, and its store carries no guard: the defeat stands
   even in the same call that emptied the enemy side. */
static void chapter_28_a_retired_randis_is_a_defeat(void)
{
    stage28(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_28_post_action();
    CHECK_EQ(end_code(), 1);

    stage28(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_28_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No unit slot but 0 ends this battle from code.  Slots 1 to 6 are retired in
   turn with slot 7 left as a live enemy holding the battle open, so the only
   thing that could turn any of these into a non-zero code is a defeat test the
   handler does not have -- slot 3 above all, which is what the shared test
   would watch had this chapter's id been one of the two it singles out. */
static void chapter_28_no_other_slot_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        stage28(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_28_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* A verdict already recorded by a chapter event survives the handler untouched:
   the shared test's gate returns before anything is examined, and this handler
   adds no store of its own on either side of the CALL.  Each value below would
   be overwritten by a body that ran -- the array holds a live enemy, which would
   settle the code at 0. */
static void chapter_28_a_recorded_verdict_is_left_alone(void)
{
    stage28(2, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_28_post_action();
    CHECK_EQ(end_code(), 1);

    stage28(2, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_28_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The handler stores nothing of its own before the CALL either: a battle that
   is still open and has nothing to decide comes back still open.  It is called
   twice because the dispatchers run it after every unit action, and a handler
   that only behaved on its first call would still pass every case above. */
static void chapter_28_an_open_battle_stays_open(void)
{
    stage28(2, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_28_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_28_post_action();
    CHECK_EQ(end_code(), 0);
}

/* ------------------------------------------------------------------------
 * Chapter 29's handler, 0003b9f0: PUSH EBX/ESI/EDI/EBP at 0003b9f0..0003b9f3,
 * MOV EBP,ESP at 0003b9f4, SUB ESP,0x0 at 0003b9f6, CALL 0x0003a2e0 at
 * 0003b9fc, then the four POPs at 0003ba01..0003ba04 and the one-byte RET at
 * 0003ba05.  Like chapters 16's, 18's, 21's and 28's it has no store, no
 * compare and no second call, so the two things worth pinning are that the
 * shared default end test really runs and that nothing else does.
 *
 * The expected verdicts are the shared test's, read off its assembly at
 * 0003a2e0: the CMP dword ptr [0x00069da0],0x0 / JNZ at 0003a2ec that abandons
 * the body for a code that is already non-zero, the MOV dword ptr
 * [0x00069da0],0x2 at 0003a2f9 that writes the cleared verdict up front, the
 * CMP byte ptr [EAX+0x6],0x0 / JNZ at 0003a331 and MOV AL,byte ptr [EAX+0x5] /
 * AND AL,0x1 at 0003a33a that put it back to 0 for a live enemy, and the PUSH
 * 0x0 / unguarded MOV dword ptr [0x00069da0],0x1 at 0003a382..0003a390 that
 * makes a retired unit slot 0 a defeat in every chapter but 0x10 and 0x15.
 *
 * The chapter id staged throughout is 28, because the table is indexed by the
 * 0-based chapter id and this handler is slot 28: the dword at 000602fc,
 * twenty-eight entries into the table based at 0006028c, is 0003b9f0, and that
 * table entry is the function's only xref.
 *
 * The chapter's own conditions really are the shared test's two and nothing
 * more: the guide's chapter 29 entry, 守護魔龍, states 勝利條件：敵人全滅 and
 * 失敗條件：蘭迪斯死亡.  Its two scripted events -- 當己方接近中央路口前的
 * 垂直線時，右邊、右下與中上敵群開始出擊 and 當己方到達上邊房間的入口時，
 * 所有敵人開始總攻擊 -- are tile-triggered position events, so neither may show
 * up as a verdict from this handler.  Nor may the two LV30 守護魔龍: they are
 * the last two deployment records and the chapter is won by clearing the map,
 * not by killing them, so no named slot other than 0 may settle anything.
 *
 * The unit array is staged rather than read from the map file: the handler
 * takes no arguments, so the array, the unit count and the chapter id are its
 * entire input.  Nothing below asserts what any of those globals holds on its
 * own -- ticket 23 owns that.
 * ------------------------------------------------------------------------ */

/* Chapter 29 is chapter id 28 (0x1c). */
#define CHAPTER_29_ID 28

static void stage29(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_29_ID;
}

/* 勝利條件：敵人全滅.  The CALL is really taken: with every enemy retired the
   shared test's up front 2 survives, and a handler whose body did nothing would
   leave the 0 it was given. */
static void chapter_29_clears_when_every_enemy_is_retired(void)
{
    stage29(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_29_post_action();
    CHECK_EQ(end_code(), 2);
}

/* One live enemy puts the code back to 0, so the walk inside the shared test is
   reached through this handler and not short circuited by anything in front of
   the CALL. */
static void chapter_29_a_live_enemy_keeps_the_battle_going(void)
{
    stage29(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_29_post_action();
    CHECK_EQ(end_code(), 0);
}

/* 失敗條件：蘭迪斯死亡.  Chapter id 28 is neither 0x10 nor 0x15, so the shared
   test watches unit slot 0, and its store carries no guard: the defeat stands
   even in the same call that emptied the enemy side. */
static void chapter_29_a_retired_randis_is_a_defeat(void)
{
    stage29(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_29_post_action();
    CHECK_EQ(end_code(), 1);

    stage29(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_29_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No unit slot but 0 ends this battle from code.  Slots 1 to 6 are retired in
   turn with slot 7 left as a live enemy holding the battle open, so the only
   thing that could turn any of these into a non-zero code is a defeat test the
   handler does not have -- slot 3 above all, which is what the shared test
   would watch had this chapter's id been one of the two it singles out. */
static void chapter_29_no_other_slot_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        stage29(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_29_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* A verdict already recorded by a chapter event survives the handler untouched:
   the shared test's gate returns before anything is examined, and this handler
   adds no store of its own on either side of the CALL.  Each value below would
   be overwritten by a body that ran -- the array holds a live enemy, which would
   settle the code at 0. */
static void chapter_29_a_recorded_verdict_is_left_alone(void)
{
    stage29(2, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_29_post_action();
    CHECK_EQ(end_code(), 1);

    stage29(2, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_29_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The handler stores nothing of its own before the CALL either: a battle that
   is still open and has nothing to decide comes back still open.  It is called
   twice because the dispatchers run it after every unit action, and a handler
   that only behaved on its first call would still pass every case above. */
static void chapter_29_an_open_battle_stays_open(void)
{
    stage29(2, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_29_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_29_post_action();
    CHECK_EQ(end_code(), 0);
}

/* --------------------------------------------------------------------------
 * Chapter 30, 最終聖戰 -- fdps_chapter_30_post_action at 0003ba50.
 *
 * The fourth handler in this file with no CALL 0x0003a2e0 -- chapters 25, 26
 * and 27 are the others here, and chapter 22's is a fifth, in tests/chpost2.c
 * -- and the last slot of the table.  PUSH 0x0 / CALL
 * 0x000109b0 / ADD ESP,0x4 at 0003ba5c..0003ba65,
 * TEST EAX,EAX / JZ 0003ba74 at 0003ba66 and MOV dword ptr [0x00069da0],0x1 at
 * 0003ba6a are the entire body, so there are exactly two verdicts it can leave:
 * a 1 when unit slot 0 is retired, and whatever it was handed when slot 0 is
 * standing.
 *
 * That the shared default end test is absent is what most of the cases below
 * pin, because it is the one thing a plausible wrong emit would restore -- every
 * other handler after chapter 27 in the table forwards to it, and unlike
 * chapter 22 this one watches the very slot the shared test watches, so the
 * index alone would not give the mistake away.  The guide gives 第30章
 * 最終聖戰 勝利條件 擊倒平衡之神 and 失敗條件 蘭迪斯死亡, and its
 * reinforcements are 永遠清不完 -- two ghosts and two 白骨戰士 are put back as
 * fast as they are killed -- so 敵人全滅 is not a way to win this chapter and
 * a 2 must never come out of this handler.
 *
 * The chapter id staged below is 29, the 0-based id whose table slot -- the
 * dword at 00060300, twenty-nine entries into the table based at 0006028c --
 * holds 0003ba50.  Nothing in the body reads it, and the last case here asserts
 * exactly that, staging the two ids the shared test singles out.
 *
 * Unit slot 0 is 蘭迪斯: unit slot i is roster slot i and the roster is in join
 * order, and chapter 30 names no exclusion from its 己方.
 * ------------------------------------------------------------------------ */

/* Chapter 30 is chapter id 29 (0x1d). */
#define CHAPTER_30_ID 29

static void stage30(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_30_ID;
}

/* 失敗條件：蘭迪斯死亡.  A retired slot 0 puts a 1 in the code: the TEST/JZ
   at 0003ba66 falls through and 0003ba6a stores it. */
static void chapter_30_a_retired_randis_is_a_defeat(void)
{
    stage30(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_30_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The case that separates this handler from the forwarding ones: with every
   enemy retired and slot 0 standing, the code stays 0.  A body that called the
   shared test at 0003a2e0 would answer 2 here, because that test's sweep is
   precisely 敵人全滅 -- and chapter 30 is not won that way, the guide's
   敵方援軍是永遠清不完 saying the sweep could not empty in the first place. */
static void chapter_30_wiping_the_enemy_out_does_not_clear_the_chapter(void)
{
    stage30(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_30_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The ordinary path: a live enemy, a standing slot 0, nothing decided.  The JZ
   at 0003ba66 is taken and the body writes nothing at all.  It is called twice
   because the dispatchers run it after every unit action, and a handler that
   only behaved on its first call would still pass every other case here. */
static void chapter_30_an_open_battle_stays_open(void)
{
    stage30(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_30_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_30_post_action();
    CHECK_EQ(end_code(), 0);
}

/* No slot but 0 ends this battle, and the sweep also catches an argument that
   drifted by one.  Every other slot is retired in turn with slot 0 left
   standing, and the code has to stay 0 each time -- slot 3 above all, the index
   the shared test would substitute under a chapter id this handler never
   reads. */
static void chapter_30_no_slot_but_randis_ends_the_battle(void)
{
    int retired_slot;
    int other_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS; retired_slot++) {
        stage30(STAGE_UNITS, 0);
        for (other_slot = 0; other_slot < STAGE_UNITS; other_slot++) {
            stage_unit(other_slot, SIDE_PLAYER, 0);
        }
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_30_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* The store consults nothing, so a clear the death script already recorded --
   the third 平衡之神 falling, which is this chapter's only victory -- loses to a
   defeat detected on the same action.  Gating the store on the code still being
   0, the guard the shared test puts on its own writes, would leave the 2
   standing here and clear a chapter the original ends with a Game Over. */
static void chapter_30_a_recorded_clear_still_loses_to_a_retired_randis(void)
{
    stage30(3, 2);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_30_post_action();
    CHECK_EQ(end_code(), 1);
}

/* With slot 0 standing there is no store on any path, so a verdict already in
   the code survives whatever else the map looks like -- including the wiped out
   enemy side that would have made the shared test recompute a 2 rather than
   leave the 1 below. */
static void chapter_30_a_recorded_verdict_survives_a_standing_randis(void)
{
    stage30(3, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_30_post_action();
    CHECK_EQ(end_code(), 2);

    stage30(3, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_30_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The body contains no CMP against data_fdps_chapter_current_chapter_id, so the
   answer cannot depend on it.  Staging chapter 17's id and then chapter 22's --
   0x10 and 0x15, the two the shared test's own comparison singles out -- changes
   neither answer, and the wiped out enemy side in each half would have come back
   as a 2 from a handler that reached the shared test at all. */
static void chapter_30_the_chapter_id_is_never_consulted(void)
{
    stage30(3, 0);
    data_fdps_chapter_current_chapter_id = CHAPTER_17_ID;
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_30_post_action();
    CHECK_EQ(end_code(), 1);

    stage30(3, 0);
    data_fdps_chapter_current_chapter_id = CHAPTER_22_ID;
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_30_post_action();
    CHECK_EQ(end_code(), 0);
}

void run_chpost3_tests(void)
{
    RUN_TEST(chapter_25_the_three_warlords_falling_clears_the_chapter);
    RUN_TEST(chapter_25_one_standing_warlord_holds_the_chapter_open);
    RUN_TEST(chapter_25_killing_everything_but_the_warlords_settles_nothing);
    RUN_TEST(chapter_25_a_retired_randis_is_a_defeat);
    RUN_TEST(chapter_25_randis_falling_with_the_last_warlord_is_a_defeat);
    RUN_TEST(chapter_25_a_recorded_defeat_is_overwritten_by_the_clear);
    RUN_TEST(chapter_25_a_recorded_verdict_survives_an_undecided_action);
    RUN_TEST(chapter_25_no_other_slot_ends_the_battle);
    RUN_TEST(chapter_25_an_open_battle_stays_open);
    RUN_TEST(chapter_25_the_verdict_is_stable_across_calls);
    RUN_TEST(chapter_25_the_chapter_id_is_never_consulted);
    RUN_TEST(chapter_26_the_four_warlords_falling_clears_the_chapter);
    RUN_TEST(chapter_26_one_standing_warlord_holds_the_chapter_open);
    RUN_TEST(chapter_26_killing_everything_but_the_warlords_settles_nothing);
    RUN_TEST(chapter_26_a_retired_randis_is_a_defeat);
    RUN_TEST(chapter_26_randis_falling_with_the_last_warlord_is_a_defeat);
    RUN_TEST(chapter_26_a_recorded_defeat_is_overwritten_by_the_clear);
    RUN_TEST(chapter_26_the_ally_test_is_skipped_before_the_latch_is_set);
    RUN_TEST(chapter_26_a_retired_ally_guard_is_a_defeat_once_the_latch_is_set);
    RUN_TEST(chapter_26_the_latch_test_is_an_equality_against_one);
    RUN_TEST(chapter_26_the_latch_byte_is_zero_extended);
    RUN_TEST(chapter_26_no_ally_slot_but_the_last_ends_the_battle);
    RUN_TEST(chapter_26_the_ally_defeat_outranks_the_clear);
    RUN_TEST(chapter_26_a_recorded_verdict_survives_an_undecided_action);
    RUN_TEST(chapter_26_no_other_slot_ends_the_battle);
    RUN_TEST(chapter_26_only_latch_slot_16_gates_the_ally_test);
    RUN_TEST(chapter_26_an_open_battle_stays_open);
    RUN_TEST(chapter_26_the_verdict_is_stable_across_calls);
    RUN_TEST(chapter_26_the_chapter_id_is_never_consulted);
    RUN_TEST(chapter_27_the_mage_king_falling_clears_the_chapter);
    RUN_TEST(chapter_27_killing_everything_but_the_mage_king_settles_nothing);
    RUN_TEST(chapter_27_a_retired_randis_is_a_defeat);
    RUN_TEST(chapter_27_randis_falling_with_the_mage_king_is_a_defeat);
    RUN_TEST(chapter_27_a_recorded_defeat_is_overwritten_by_the_clear);
    RUN_TEST(chapter_27_a_recorded_verdict_survives_an_undecided_action);
    RUN_TEST(chapter_27_no_other_slot_ends_the_battle);
    RUN_TEST(chapter_27_an_open_battle_stays_open);
    RUN_TEST(chapter_27_the_verdict_is_stable_across_calls);
    RUN_TEST(chapter_27_the_chapter_id_is_never_consulted);
    RUN_TEST(chapter_28_clears_when_every_enemy_is_retired);
    RUN_TEST(chapter_28_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(chapter_28_a_retired_randis_is_a_defeat);
    RUN_TEST(chapter_28_no_other_slot_ends_the_battle);
    RUN_TEST(chapter_28_a_recorded_verdict_is_left_alone);
    RUN_TEST(chapter_28_an_open_battle_stays_open);
    RUN_TEST(chapter_29_clears_when_every_enemy_is_retired);
    RUN_TEST(chapter_29_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(chapter_29_a_retired_randis_is_a_defeat);
    RUN_TEST(chapter_29_no_other_slot_ends_the_battle);
    RUN_TEST(chapter_29_a_recorded_verdict_is_left_alone);
    RUN_TEST(chapter_29_an_open_battle_stays_open);
    RUN_TEST(chapter_30_a_retired_randis_is_a_defeat);
    RUN_TEST(chapter_30_wiping_the_enemy_out_does_not_clear_the_chapter);
    RUN_TEST(chapter_30_an_open_battle_stays_open);
    RUN_TEST(chapter_30_no_slot_but_randis_ends_the_battle);
    RUN_TEST(chapter_30_a_recorded_clear_still_loses_to_a_retired_randis);
    RUN_TEST(chapter_30_a_recorded_verdict_survives_a_standing_randis);
    RUN_TEST(chapter_30_the_chapter_id_is_never_consulted);
}
