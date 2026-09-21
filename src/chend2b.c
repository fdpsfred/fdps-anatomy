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
#include "fdpstype.h"
#include "gamedata.h"
#include "btlend.h"
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
