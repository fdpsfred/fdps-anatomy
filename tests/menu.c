/* tests/menu.c -- cover for src/menu.c.
 *
 * Expected values come from the assembly at 000160e0 and from the descriptor
 * bytes the image actually holds at 00024d60; none of them is read off the
 * emitted C.
 */
#include "testharn.h"
#include "menu.h"

/* CMP dword ptr [EAX],0 / JNZ on the very first iteration: entry 0 selectable
   short-circuits the whole scan. */
static void menu_first_entry_wins(void)
{
    int desc[4];

    desc[0] = 0;
    desc[1] = 1;
    desc[2] = 1;
    desc[3] = 1;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 0);
}

/* The scan walks upward and returns on the first hit, so entry 1 is the answer
   even though entry 2 is selectable too -- INC [EBP-8] / JMP back to the test,
   with the store to the result slot only on the taken branch. */
static void menu_returns_lowest_index(void)
{
    int desc[4];

    desc[0] = 1;
    desc[1] = 0;
    desc[2] = 0;
    desc[3] = 1;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 1);
}

/* Index 3 is inside the bound: JL is against 4, so the fourth entry is still
   tested before the loop falls out. */
static void menu_last_entry_reachable(void)
{
    int desc[4];

    desc[0] = 1;
    desc[1] = 1;
    desc[2] = 1;
    desc[3] = 0;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 3);
}

/* Falling out of the loop stores 0xffffffff into the result slot. */
static void menu_all_disabled(void)
{
    int desc[4];

    desc[0] = 1;
    desc[1] = 1;
    desc[2] = 1;
    desc[3] = 1;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), -1);
}

/* The entry test is JNZ, not a sign test: -1 and 7 are both "greyed out", so
   the first selectable entry is 2.  A "> 0 means disabled" reading would
   answer 0 here. */
static void menu_any_nonzero_is_disabled(void)
{
    int desc[4];

    desc[0] = -1;
    desc[1] = 7;
    desc[2] = 0;
    desc[3] = 0;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 2);
}

/* The bound is a hard-coded 4 and does not come from the caller: a selectable
   entry sitting at index 4 is never looked at. */
static void menu_bound_is_four(void)
{
    int desc[6];

    desc[0] = 1;
    desc[1] = 1;
    desc[2] = 1;
    desc[3] = 1;
    desc[4] = 0;
    desc[5] = 0;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), -1);
}

/* The descriptor is only ever loaded from -- there is no store through the
   parameter anywhere in the function -- and fdps_battle_item_menu reuses its
   copy for the cursor-move legality checks afterwards. */
static void menu_does_not_write_descriptor(void)
{
    int desc[4];

    desc[0] = 1;
    desc[1] = 1;
    desc[2] = 0;
    desc[3] = 1;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 2);
    CHECK_EQ(desc[0], 1);
    CHECK_EQ(desc[1], 1);
    CHECK_EQ(desc[2], 0);
    CHECK_EQ(desc[3], 1);
}

/* The item menu's static descriptor: the sixteen bytes at 00024d60 that
   fdps_battle_item_menu copies onto its stack are all zero, so every entry is
   selectable and the cursor starts on entry 0. */
static void menu_item_menu_descriptor(void)
{
    int desc[4];

    desc[0] = 0;
    desc[1] = 0;
    desc[2] = 0;
    desc[3] = 0;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 0);
}

/* fdps_battle_item_menu patches entry 1 to 1 when the list it just built came
   back empty (MOV dword ptr [EBP-0x50],1 at 00025340, which is the second int
   of that copy).  The cursor still starts on entry 0. */
static void menu_item_menu_patched(void)
{
    int desc[4];

    desc[0] = 0;
    desc[1] = 1;
    desc[2] = 0;
    desc[3] = 0;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 0);
}

void run_menu_tests(void)
{
    RUN_TEST(menu_first_entry_wins);
    RUN_TEST(menu_returns_lowest_index);
    RUN_TEST(menu_last_entry_reachable);
    RUN_TEST(menu_all_disabled);
    RUN_TEST(menu_any_nonzero_is_disabled);
    RUN_TEST(menu_bound_is_four);
    RUN_TEST(menu_does_not_write_descriptor);
    RUN_TEST(menu_item_menu_descriptor);
    RUN_TEST(menu_item_menu_patched);
}
