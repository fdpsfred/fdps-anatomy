/* tests/chevt1.c -- cover for src/chevt1.c.
 *
 * Expected values come from the assembly of fdps_chapter_event_set_game_over
 * at 00036cd0, which is thirty-four bytes of straight line with no compare and
 * no branch anywhere in it: PUSH EBX/ESI/EDI/EBP and MOV EBP,ESP, then MOV
 * dword ptr [EBP + 0x14],0x0 at 00036cdc over the argument slot, then MOV
 * dword ptr [0x00069da0],0x1 at 00036ce3, then the four POPs and RET.  1 is a
 * literal in the instruction, not a value read from anywhere, and there is no
 * instruction in front of the store that could skip it.
 *
 * That the code means defeat comes from the dispatch at 0002936b, which loads
 * the same global and separates 1 from 2 with unsigned compares at 00029377
 * and 0002937d; the chapter-cleared code 2 is what the chapter 15, 22 and 23
 * boss-defeat handlers store, and 0 is what the chapter state reset installs.
 *
 * Nothing below asserts what the global holds before a call: it is a
 * ticket 23 symbol and the build links it zero-filled for now, so every case
 * writes the state it wants to see changed.
 */
#include "testharn.h"
#include "gamedata.h"
#include "chevt1.h"

/* The three codes the battle-end global carries, from the stores across the
   image: 0 from the chapter state reset at 00022766, 1 from this handler at
   00036ce3, 2 from the boss-defeat handlers at 00037caf and 00038936. */
#define END_CODE_RUNNING 0
#define END_CODE_DEFEAT  1
#define END_CODE_CLEARED 2

/* The store is reached from a running battle, which is the state the loops
   keep the global in, and the defeat code lands in it. */
static void set_game_over_from_running(void)
{
    data_fdps_chapter_event_or_battle_end_code = END_CODE_RUNNING;
    fdps_chapter_event_set_game_over(0);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
}

/* No test guards the store, so a chapter already marked cleared is overwritten
   into a defeat rather than left alone.  This is the case that would come out
   differently if the emitted C had grown a compare the assembly does not
   have. */
static void set_game_over_overwrites_cleared(void)
{
    data_fdps_chapter_event_or_battle_end_code = END_CODE_CLEARED;
    fdps_chapter_event_set_game_over(0);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
}

/* Storing a literal over the same address twice leaves the same value, so the
   handler firing again on a later event does not accumulate or toggle. */
static void set_game_over_is_idempotent(void)
{
    data_fdps_chapter_event_or_battle_end_code = END_CODE_RUNNING;
    fdps_chapter_event_set_game_over(0);
    fdps_chapter_event_set_game_over(0);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
}

/* The argument slot is written and never read, and no unit record is resolved
   anywhere in the body, so the index the dispatcher passes cannot reach the
   result.  The turn-event dispatcher pushes a literal 0 at 0002e13e; the cell
   search and the death-script runner push a real unit index, and a scripted
   index out of the map file is not range checked on the way in.  All three
   have to leave the global holding the defeat code. */
static void set_game_over_ignores_unit_index(void)
{
    data_fdps_chapter_event_or_battle_end_code = END_CODE_RUNNING;
    fdps_chapter_event_set_game_over(7);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);

    data_fdps_chapter_event_or_battle_end_code = END_CODE_RUNNING;
    fdps_chapter_event_set_game_over(-1);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);

    data_fdps_chapter_event_or_battle_end_code = END_CODE_RUNNING;
    fdps_chapter_event_set_game_over(30000);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
}

void run_chevt1_tests(void)
{
    RUN_TEST(set_game_over_from_running);
    RUN_TEST(set_game_over_overwrites_cleared);
    RUN_TEST(set_game_over_is_idempotent);
    RUN_TEST(set_game_over_ignores_unit_index);
}
