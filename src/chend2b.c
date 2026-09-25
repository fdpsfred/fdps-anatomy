/* chend2b.c -- the per-chapter end handlers, chapters 25 to 30: what the game
 * does at the moment a chapter's battle has been won, before the village phase
 * that follows it.  Chapters 1 to 11 are chend1.c, 12 to 15 are chend1b.c and
 * 16 to 24 are chend2.c.  These six are the run-up to the ending, and two of
 * them, chapters 27 and 30, are the only handlers that can end the game.
 *
 * These are slots of the handler table based at 00060304, indexed by the
 * 0-based chapter id and called only through it, so the dispatcher in main.c
 * is not a static caller of any of them.
 *
 * See chend2b.h for what each handler closes out.  Nothing here owns state.
 */
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "btlend.h"
#include "ending.h"
#include "icon.h"
#include "roster.h"
#include "text.h"
#include "unititem.h"
#include "chend2b.h"

/* Chapter 25's victory cut-scene, the string at 0x621dc loaded into EAX at
   0003b736 and pushed as fdps_icon_script_run's only argument.  The member
   name is based on the chapter that has just been WON: chapter 25 is the
   0-based id 24, so this is Win24.dat.  read_memory at 0x621d0 returns
   57 69 6e 32 33 2e 64 61 74 00 64 61 57 69 6e 32 34 2e 64 61 74 00, so
   0x621da..0x621db is the 64 61 filler padding "Win23.dat" to a four-byte
   boundary and 0x621dc is the W of "Win24.dat".

   The literal is the bare member name with no path and no container: the
   interpreter names IconAni.vfs itself and upper-cases the member name in
   place before the container compare (vfs.h, rebuild_info/pitfalls.md), so a
   lower-case spelling here is what the original has and is not a defect to
   tidy, and the literal cannot live in read-only storage. */
#define CH25_VICTORY_SCRIPT "Win24.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x19 at 0003b749.  The index is 0-based, so 25 is chapter
   26 -- both the village phase that runs next and the chapter loaded after it
   read this global, so this one store is what advances the game.

   It is an assignment of the chapter's successor and not an increment, like
   every handler in the family.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 24, the index of the chapter that has just been won, and this store is
   25, the index of the one that comes next. */
#define CH25_NEXT_CHAPTER_ID 0x19

/* Chapter 25's end handler, the family's plain shape: four calls and one
   store, no branch and no local anywhere in the body (the frame is the
   four-push prologue with SUB ESP,0x0).

   THE ORDER OF THE FOUR IS THE ALGORITHM, the order chapter 16 runs in.  The
   sweep comes first, and here it matters: the chapter is won by three named
   warlords leaving the field, so the rest of the enemy side is still standing
   when the handler is entered.  The cut-scene runs AFTER the writeback, so an
   edit it makes to a battle unit record lands on a party that has already
   been banked, and the revive runs AFTER the cut-scene and reads the roster
   the writeback has just filled.

   Every call is to a void function and nothing after any CALL reads EAX: the
   only argument is the script name pushed at 0003b73b and cleaned by the
   caller's ADD ESP,0x4 at 0003b741.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003b749 is the handler's last act. */
void fdps_chapter_25_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH25_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH25_NEXT_CHAPTER_ID;
}

/* Chapter 26's victory cut-scene, the string at 0x621e8 loaded into EAX at
   0003b86e and pushed as fdps_icon_script_run's only argument.  Named, like
   every member in the family, after the 0-based id of the chapter just WON:
   chapter 26 is id 25.  read_memory at 0x621e0 returns
   34 2e 64 61 74 00 64 61 57 69 6e 32 35 2e 64 61 74 00, so 0x621e6..0x621e7
   is the 64 61 filler behind "Win24.dat" and 0x621e8 is the W of "Win25.dat".
   Lower case and writable for the same reason as chapter 25's. */
#define CH26_VICTORY_SCRIPT "Win25.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x1a at 0003b881, 0-based, so chapter 27.  One more than
   the script's 25, for the reason chapter 25's pair differs by one. */
#define CH26_NEXT_CHAPTER_ID 0x1a

/* Battle unit 0, 蘭迪斯: PUSH 0x0 before each of the three inventory calls at
   0003b811, 0003b824 and 0003b85a.  The deployment lays the roster down first
   and he is always its first record. */
#define CH26_RANDIS_UNIT_INDEX 0

/* The sword whose presence cancels the gift, PUSH 0xa2 at 0003b80c: 真炎龍劍,
   the end of the upgrade chain chapter 25's fire god completes
   (fdps_chapter_25_event_upgrade_randis_sword, chevt5.h; assets/items.md). */
#define CH26_TRUE_DRAGON_SWORD_ITEM_ID 0xa2

/* The sword that is given, PUSH 0x62 at 0003b858: 炎龍劍 (assets/items.md). */
#define CH26_DRAGON_SWORD_ITEM_ID 0x62

/* What fdps_unit_find_item_slot answers when the item is not in the bag:
   CMP dword ptr [EBP-0x4],-0x1 / JNZ at 0003b81e. */
#define CH26_ITEM_NOT_CARRIED (-1)

/* A full bag, CMP EAX,0x8 / JNZ at 0003b82e: the eight entries
   fdps_unit_item_count (unititem.h) counts over. */
#define CH26_BAG_FULL_COUNT 8

/* The entry of the loaded chapter's text block the gift line is drawn from,
   PUSH 0x18 at 0003b848. */
#define CH26_GIFT_TEXT_ID 0x18

/* The destination and row stride of the draw, PUSH 0xa0000 / PUSH 0x140 at
   0003b83e..0003b843: the top-left corner of the visible mode-13h page.  An
   address inside the display adapter's aperture and not anything the linker
   places, so it stays a literal (rebuild_info/pitfalls.md, contract E). */
#define CH26_TEXT_DEST 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* 0003b800.  Chapter 26's end handler: the 炎龍劍 gift, then the family's four
   closing steps and the store.

   The frame is the four-push one with SUB ESP,0x4, and that one local is the
   search result at [EBP-0x4]; nothing is pushed by the dispatcher and the RET
   carries no immediate.

   THE GIFT HAS TWO GATES, TESTED IN ORDER AND SHORT-CIRCUITED.  The search
   runs first, unconditionally; only when it came back -1 is the bag counted,
   and a JNZ on either test (0003b822 to the JMP at 0003b833, or falling onto
   it from 0003b831) goes straight to the closing steps.  The count gates the
   message as well as the add, so a full bag draws nothing -- relying on
   fdps_unit_add_item's own -1 would paint the gift line for a sword never
   handed over.

   THE GIFT RUNS BEFORE THE WRITEBACK.  fdps_unit_add_item edits the battle
   record, and fdps_roster_write_back_battle_units is what copies that record
   onto the roster, so the gift at the end of the handler would be thrown away
   with the map.

   Values used after a CALL: fdps_unit_find_item_slot's EAX, stored to
   [EBP-0x4] at 0003b81b and compared at 0003b81e; fdps_unit_item_count's EAX,
   compared directly at 0003b82e.  fdps_draw_text's cursor and
   fdps_unit_add_item's 1-or--1 are discarded (the next instruction after each
   CALL is its ADD ESP), and the remaining four callees return nothing. */
void fdps_chapter_26_end(void)
{
    /* Where 真炎龍劍 sits in 蘭迪斯's bag, or -1 when he does not carry it. */
    int true_sword_slot;

    true_sword_slot = fdps_unit_find_item_slot(CH26_RANDIS_UNIT_INDEX,
                                               CH26_TRUE_DRAGON_SWORD_ITEM_ID);
    if (true_sword_slot == CH26_ITEM_NOT_CARRIED
            && fdps_unit_item_count(CH26_RANDIS_UNIT_INDEX)
               != CH26_BAG_FULL_COUNT) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr, CH26_GIFT_TEXT_ID,
                       (unsigned char *) CH26_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_unit_add_item(CH26_RANDIS_UNIT_INDEX, CH26_DRAGON_SWORD_ITEM_ID);
    }

    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH26_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH26_NEXT_CHAPTER_ID;
}

/* Chapter 27's two closing cut-scenes, both run by the IconAni.vfs
   interpreter.  read_memory at 0x621f0 returns 74 00 00 00 57 69 6e 47 41 32
   36 2e 64 61 74 00 57 69 6e 32 36 2e 64 61 74 00, so 0x621f4 is the W of
   "WinGA26.dat" (MOV EAX,0x621f4 at 0003b94f) and 0x62200 the W of
   "Win26.dat" (MOV EAX,0x62200 at 0003b96e).  Both carry the 0-based id 26 of
   the chapter just won, like every member of the family.  WinGA26.dat is the
   alternative scene in which 平衡之神 is revived and 法蓮娜 freed; Win26.dat is
   the scene in which the revival fails.  Lower case and writable for the same
   reason as chapter 25's. */
#define CH27_HIDDEN_ROUTE_SCRIPT "WinGA26.dat"
#define CH27_ENDING_SCRIPT "Win26.dat"

/* The two carriers, by fixed battle-unit index: PUSH 0x0 at 0003b906 and
   0003b937, PUSH 0x3 at 0003b918 and 0003b945.  Unit 0 is 蘭迪斯 and unit 3 is
   法蓮娜, the roster's first and fourth records in join order.  They are
   indices into the battle array and not character ids. */
#define CH27_RANDIS_UNIT_INDEX 0
#define CH27_FARENA_UNIT_INDEX 3

/* The two items the hidden route spends (assets/items.md): 魔精石碎片, PUSH
   0xb3 at 0003b901, looked for on 蘭迪斯, and 反禁制器, PUSH 0xdc at 0003b913,
   looked for on 法蓮娜 -- the item chapter 23 hands her (chevt4.c). */
#define CH27_MANA_SHARD_ITEM_ID 0xb3
#define CH27_SEAL_BREAKER_ITEM_ID 0xdc

/* fdps_unit_find_item_slot's not-carried answer: CMP dword ptr [EBP-0x8],-0x1
   at 0003b925 and CMP dword ptr [EBP-0x4],-0x1 at 0003b92b. */
#define CH27_ITEM_NOT_CARRIED (-1)

/* The hidden route's store, MOV dword ptr [0x00069cf4],0x1b at 0003b962: the
   0-based index 27, chapter 28.  The ending route stores nothing here. */
#define CH27_HIDDEN_NEXT_CHAPTER_ID 0x1b

/* The ending route's store, MOV byte ptr [0x000643eb],0x1 at 0003b981: the
   return-to-title request.  Every test of the flag is against zero, so this is
   simply "raised" (gamedata.h). */
#define CH27_QUIT_REQUESTED 1

/* 0003b8f0.  Chapter 27's end handler, the gate on the hidden chapters.

   The frame is the four-push one with SUB ESP,0x8; the two locals are the two
   search results, [EBP-0x8] for the 魔精石碎片 and [EBP-0x4] for the 反禁制器.
   Nothing is pushed by the dispatcher and the RET carries no immediate.

   THE SWEEP IS UNCONDITIONAL AND FIRST, CALL 0x00039e10 at 0003b8fc, ahead of
   both searches and on both routes.

   BOTH SEARCHES RUN BEFORE ANYTHING IS DECIDED.  The two CALLs at 0003b908 and
   0003b91a are back to back and only then do the compares start, so neither
   item is removed unless both were found.  The test is an OR of the two misses:
   CMP [EBP-0x8],-0x1 / JZ lands on the JMP at 0003b931 that goes to the ending
   route at 0003b96e, and CMP [EBP-0x4],-0x1 / JNZ at 0003b92f is the only way
   into the hidden route at 0003b933.  So the condition below is written the
   same way round: the hidden route is the else-arm reached only when neither
   search missed.  The short-circuit is on two locals already filled, so it has
   no side effect to order.

   THE HIDDEN ROUTE: both items removed, 蘭迪斯's first, each with the slot its
   own search returned (MOV EAX,[EBP-0x8] at 0003b933, MOV EAX,[EBP-0x4] at
   0003b941); WinGA26.dat interpreted; the fallen revived; the chapter index
   set to 27.  There is no roster write-back call anywhere in the handler --
   unlike every other slot of the table -- so none is written here.  The
   route's one write-back happens inside the cut-scene instead: WinGA26.dat
   runs opcode 0x61, which awards 法蓮娜 her experience and then writes the
   battle units back to the roster (src/icon.c), before the revive below.

   THE ENDING ROUTE: Win26.dat interpreted, the ending sequence played, and the
   return-to-title flag raised.  It leaves data_fdps_chapter_current_chapter_id
   as it found it, and the two items stay where they are.

   Values used after a CALL: fdps_unit_find_item_slot's EAX twice, stored at
   0003b910 and 0003b922 and read by the compares and by the two removal pushes.
   Nothing else is read out of EAX after a CALL -- the ADD ESP after each
   argumented call is the next instruction, and the other callees return
   nothing. */
void fdps_chapter_27_end(void)
{
    /* Where the 魔精石碎片 sits in 蘭迪斯's bag, or -1. */
    int mana_shard_slot;
    /* Where the 反禁制器 sits in 法蓮娜's bag, or -1. */
    int seal_breaker_slot;

    fdps_battle_destroy_remaining_enemies();
    mana_shard_slot = fdps_unit_find_item_slot(CH27_RANDIS_UNIT_INDEX,
                                               CH27_MANA_SHARD_ITEM_ID);
    seal_breaker_slot = fdps_unit_find_item_slot(CH27_FARENA_UNIT_INDEX,
                                                 CH27_SEAL_BREAKER_ITEM_ID);

    if (mana_shard_slot == CH27_ITEM_NOT_CARRIED
            || seal_breaker_slot == CH27_ITEM_NOT_CARRIED) {
        fdps_icon_script_run(CH27_ENDING_SCRIPT);
        fdps_play_ending_credit_roll();
        data_fdps_shared_quit_game_requested = CH27_QUIT_REQUESTED;
    } else {
        fdps_unit_remove_item(CH27_RANDIS_UNIT_INDEX, mana_shard_slot);
        fdps_unit_remove_item(CH27_FARENA_UNIT_INDEX, seal_breaker_slot);
        fdps_icon_script_run(CH27_HIDDEN_ROUTE_SCRIPT);
        fdps_roster_revive_fallen_members();
        data_fdps_chapter_current_chapter_id = CH27_HIDDEN_NEXT_CHAPTER_ID;
    }
}

/* Chapter 28's victory cut-scene, the string at 0x6220c loaded into EAX at
   0003b9c6 and pushed as fdps_icon_script_run's only argument.  Named after
   the 0-based id of the chapter just WON: chapter 28 is id 27.  read_memory
   at 0x62200 returns 57 69 6e 32 36 2e 64 61 74 00 2e 63 57 69 6e 32 37 2e
   64 61 74 00, so 0x6220a..0x6220b is the 2e 63 filler behind "Win26.dat" and
   0x6220c is the W of "Win27.dat".  Lower case and writable for the same
   reason as chapter 25's. */
#define CH28_VICTORY_SCRIPT "Win27.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x1c at 0003b9d9, 0-based, so chapter 29.  One more than
   the script's 27, for the reason chapter 25's pair differs by one. */
#define CH28_NEXT_CHAPTER_ID 0x1c

/* 0003b9b0.  Chapter 28's end handler: chapter 25's plain shape, four calls
   and one store with no branch and no local (the four-push prologue with
   SUB ESP,0x0), in the same order -- sweep, writeback, cut-scene, revive --
   for the same reasons given at fdps_chapter_25_end.

   It is reached only down the hidden route chapter 27 opens, and unlike that
   handler it banks the party again (CALL 0x00023980 at 0003b9c1).

   Every call is to a void function and nothing after any CALL reads EAX: the
   only argument is the script name pushed at 0003b9cb and cleaned by the
   caller's ADD ESP,0x4 at 0003b9d1.  The store at 0003b9d9 is the handler's
   last act. */
void fdps_chapter_28_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH28_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH28_NEXT_CHAPTER_ID;
}

/* Chapter 29's victory cut-scene, the string at 0x62218 loaded into EAX at
   0003ba26 and pushed as fdps_icon_script_run's only argument.  Named after
   the 0-based id of the chapter just WON: chapter 29 is id 28.  read_memory
   at 0x62210 returns 37 2e 64 61 74 00 64 61 57 69 6e 32 38 2e 64 61 74 00,
   so 0x62216..0x62217 is the 64 61 filler behind "Win27.dat" and 0x62218 is
   the W of "Win28.dat".  Lower case and writable for the same reason as
   chapter 25's. */
#define CH29_VICTORY_SCRIPT "Win28.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x1d at 0003ba39, 0-based, so chapter 30.  One more than
   the script's 28, for the reason chapter 25's pair differs by one. */
#define CH29_NEXT_CHAPTER_ID 0x1d

/* 0003ba10.  Chapter 29's end handler: chapter 28's shape exactly, four calls
   and one store with no branch and no local (the four-push prologue with
   SUB ESP,0x0), in the same order -- sweep at 0003ba1c, writeback at
   0003ba21, cut-scene at 0003ba2c, revive at 0003ba34 -- for the reasons
   given at fdps_chapter_25_end.  Reached through table slot 28 (the pointer
   at 0x00060374).

   Every call is to a void function and nothing after any CALL reads EAX: the
   only argument is the script name pushed at 0003ba2b and cleaned by the
   caller's ADD ESP,0x4 at 0003ba31.  The store at 0003ba39 is the handler's
   last act. */
void fdps_chapter_29_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH29_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH29_NEXT_CHAPTER_ID;
}

/* Chapter 30's victory cut-scene, the string at 0x62224 loaded into EAX at
   0003ba96 and pushed as fdps_icon_script_run's only argument.  Named after
   the 0-based id of the chapter just WON: chapter 30 is id 29.  read_memory
   at 0x62218 returns 57 69 6e 32 38 2e 64 61 74 00 64 61 57 69 6e 32 39 2e
   64 61 74 00 64 61 47 6f 6f 64 45 6e 64 2e 64 61 74 00, so 0x62222..0x62223
   is the 64 61 filler behind "Win28.dat", 0x62224 is the W of "Win29.dat",
   0x6222e..0x6222f is the filler behind it and 0x62230 is the G of
   "GoodEnd.dat".  Lower case and writable for the same reason as chapter
   25's. */
#define CH30_VICTORY_SCRIPT "Win29.dat"

/* The closing scene, the string at 0x62230 loaded into EAX at 0003bac4 and
   pushed as fdps_icon_script_run's only argument on its second
   call. */
#define CH30_EPILOGUE_SCRIPT "GoodEnd.dat"

/* The two info-panel gates, MOV byte ptr [0x00060158],0x1 at 0003baa4 and
   MOV byte ptr [0x00060159],0x1 at 0003baab.  Both are one-byte booleans
   tested only against zero (gamedata.h), so 1 is simply "raised". */
#define CH30_PANEL_OPTION_ON 1
#define CH30_PLAY_ACTIVE 1

/* The hold between the ending sequence and the closing scene: PUSH 0x7530 at
   0003bab7 into the CRT's delay, thirty thousand milliseconds.  Nothing is
   polled across it. */
#define CH30_ENDING_HOLD_MS 30000

/* The return-to-title request, MOV byte ptr [0x000643eb],0x1 at 0003bad2, the
   store chapter 27's ending route also ends on (gamedata.h). */
#define CH30_QUIT_REQUESTED 1

/* 0003ba80.  Chapter 30's end handler, the end of the game.  The four-push
   prologue with SUB ESP,0x0: no local, no branch, and nothing pushed by the
   dispatcher (table slot 29, the pointer at 0x00060378, the table's last).

   THE ORDER IS THE ALGORITHM: sweep (CALL 0x00039e10 at 0003ba8c), writeback
   (CALL 0x00023980 at 0003ba91), Win29.dat (CALL 0x00021650 at 0003ba9c), the
   two panel gates raised, the ending sequence (CALL 0x0001ba40 at 0003bab2),
   the thirty-second hold (CALL 0x0003d370, the delay thunk, at 0003babc),
   GoodEnd.dat (CALL 0x00021650 at 0003baca), and the return-to-title flag.

   UNLIKE EVERY OTHER HANDLER OF THE TABLE it neither revives the fallen nor
   stores a next chapter index into data_fdps_chapter_current_chapter_id:
   there is no chapter 31, and the game ends by unwinding to the title screen
   on data_fdps_shared_quit_game_requested instead.

   THE TWO PANEL STORES ARE NOT A SAVE/RESTORE.  Both gates are set to 1 and
   neither is put back: data_fdps_ui_terrain_hud_user_enabled is the player's
   own options-menu setting, and this handler forces it on.

   Values used after a CALL: none.  Each argumented CALL (0003ba9c, 0003babc,
   0003baca) is followed directly by the caller's ADD ESP,0x4, and no
   instruction in the body reads EAX, EDX or any other register a callee
   could have left a result in; the stores are immediates. */
void fdps_chapter_30_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH30_VICTORY_SCRIPT);
    data_fdps_ui_terrain_hud_user_enabled = CH30_PANEL_OPTION_ON;
    data_fdps_ui_play_active_flag = CH30_PLAY_ACTIVE;
    fdps_play_ending_credit_roll();
    delay(CH30_ENDING_HOLD_MS);
    fdps_icon_script_run(CH30_EPILOGUE_SCRIPT);
    data_fdps_shared_quit_game_requested = CH30_QUIT_REQUESTED;
}
