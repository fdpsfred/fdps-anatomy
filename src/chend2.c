/* chend2.c -- the per-chapter end handlers, chapters 16 to 30: what the game
 * does at the moment a chapter's battle has been won, before the village phase
 * that follows it.  Chapters 1 to 11 are chend1.c and chapters 12 to 15 are
 * chend1b.c.
 *
 * These are slots of the handler table based at 00060304, indexed by the
 * 0-based chapter id and called only through it, so the dispatcher in main.c
 * is not a static caller of any of them.
 *
 * See chend2.h for what each handler closes out.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "btlend.h"
#include "icon.h"
#include "roster.h"
#include "unit.h"
#include "unititem.h"
#include "chend2.h"

/* Chapter 16's victory cut-scene, the string at 0x62164 loaded into EAX at
   0003ace6 and pushed as fdps_icon_script_run's only argument.  The member
   name is based on the chapter that has just been WON: chapter 16 is the
   0-based id 15, so this is Win15.dat.

   (Ghidra's automatic string here starts two bytes early, at 0x62162, over the
   64 61 filler that pads the preceding "Win14.dat" literal to a four-byte
   boundary, so the decompiler renders the argument as the symbol + 2.  What
   the callee is handed is 0x62164 and the characters it sees are "Win15.dat":
   read_memory at 0x62150 returns 33 2e 64 61 74 00 2e 63 57 69 6e 31 34 2e 64
   61 74 00 64 61 57 69 6e 31 35 2e 64 61 74 00.)

   The literal is the bare member name with no path and no container: the
   interpreter names IconAni.vfs itself and upper-cases the member name in
   place before the container compare (vfs.h, rebuild_info/pitfalls.md), so a
   lower-case spelling here is what the original has and is not a defect to
   tidy, and the literal cannot live in read-only storage. */
#define CH16_VICTORY_SCRIPT "Win15.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x10 at 0003acf9.  The index is 0-based, so 16 is chapter
   17, 人質的危機 -- both the village phase that runs next and the chapter
   loaded after it read this global, so this one store is what advances the
   game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 15 of a table indexed by this same global,
   and every handler in the family stores its own literal.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 15, the index of the chapter that has just been won, and this store is
   16, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH16_NEXT_CHAPTER_ID 0x10

/* Chapter 16's end handler, the family's plain shape: four calls and one
   store, no branch and no local anywhere in the body.

   THE ORDER OF THE FOUR IS THE ALGORITHM, and it is the order chapters 12 to
   15 run in.  The cut-scene runs AFTER the writeback and the revive AFTER the
   cut-scene.  The script is interpreted with the battle's unit array still
   standing, so a unit-record edit it makes lands on a party that has already
   been banked and reaches the roster only if the script itself asks for
   another writeback (opcode 0x61, src/icon.c).  The revive then reads the
   roster the writeback has just filled, which is what makes it see the
   battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it awards no spell, and it hands nothing else to
   the phase that follows: the store below is the only global it writes
   directly.  Unlike fdps_chapter_15_end it has no gate on a one-shot latch
   either -- the first instruction after the frame is the sweep call at
   0003acdc, with nothing tested before it.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003acf9 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_16_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH16_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH16_NEXT_CHAPTER_ID;
}

/* Chapter 17's victory cut-scene, the string at 0x62170 loaded into EAX at
   0003ad56 and pushed as fdps_icon_script_run's only argument.  The member
   name is based on the chapter that has just been WON: chapter 17 is the
   0-based id 16, so this is Win16.dat.

   The literal starts exactly where the instruction says it does, unlike
   chapter 16's above: read_memory at 0x62160 returns 74 00 64 61 57 69 6e 31
   35 2e 64 61 74 00 64 61 57 69 6e 31 36 2e 64 61 74 00 64 61 57 69 6e 31 37
   2d 31 2e 64 61 74 00, so 0x6216e..0x6216f is the 64 61 filler that pads
   "Win15.dat" to a four-byte boundary and 0x62170 is the W of "Win16.dat".

   The literal is the bare member name with no path and no container, and the
   lower-case spelling is the original's: see CH16_VICTORY_SCRIPT above for
   why it must not be tidied and why it cannot live in read-only storage.

   THE NEIGHBOURING LITERAL IS NOT THIS ONE.  "Win17-1.dat" sits at 0x6217c,
   immediately behind this string, and is not what this handler names -- a
   member name reached by counting forward from the chapter number rather than
   by reading the operand at 0003ad56 would land on it. */
#define CH17_VICTORY_SCRIPT "Win16.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x11 at 0003ad69.  The index is 0-based, so 17 is chapter
   18, 咆哮的獅王 -- both the village phase that runs next and the chapter
   loaded after it read this global, so this one store is what advances the
   game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 16 of a table indexed by this same global,
   and every handler in the family stores its own literal.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 16, the index of the chapter that has just been won, and this store is
   17, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH17_NEXT_CHAPTER_ID 0x11

/* Chapter 17's end handler: the family's plain shape again, four calls and one
   store, no branch and no local anywhere in the body.  It is
   fdps_chapter_16_end above instruction for instruction, with its own script
   name and its own stored index -- the two handlers differ in exactly two
   operands.

   THE ORDER OF THE FOUR IS THE ALGORITHM and it is the order the handlers
   before it run in.  The cut-scene runs AFTER the writeback and the revive
   AFTER the cut-scene.  The script is interpreted with the battle's unit array
   still standing, so a unit-record edit it makes lands on a party that has
   already been banked and reaches the roster only if the script itself asks
   for another writeback (opcode 0x61, src/icon.c).  The revive then reads the
   roster the writeback has just filled, which is what makes it see the
   battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it awards no spell, and it hands nothing else to
   the phase that follows: the store below is the only global it writes
   directly.  It has no gate on a one-shot latch either -- the first
   instruction after the frame is the sweep call at 0003ad4c, with nothing
   tested before it.

   THE SWEEP IS BELT AND BRACES HERE, not the load-bearing step it is in
   chapters 3, 8 and 10.  Chapter 17's clear is the shared end condition's own
   敵人全滅 (fdps_chapter_17_post_action, chpost2.h, adds only a defeat test on
   unit slot 3), and that condition records a clear only once no unit on the
   enemy side is still standing, so the sweep normally finds that side already
   empty.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003ad69 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_17_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH17_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH17_NEXT_CHAPTER_ID;
}

/* Who the trade is made with: PUSH 0x0 ahead of each of the three inventory
   calls, at 0003adb9, 0003ae10 and 0003ae1f.  Battle unit 0 -- the chapter's
   0x50-byte unit records are materialised from the roster in join order, and
   slot 0 is 蘭迪斯 in every chapter of the game. */
#define CH18_RANDIS_UNIT_INDEX 0

/* The two item ids the trade is between: PUSH 0xa9 at 0003adb4 is the
   神的聖印 the gate looks for and PUSH 0xdb at 0003ae1a is the 勇者徽章 that
   replaces it (assets/items.md).  0xdb is the same badge the church reads as
   the 英雄 promotion route, ITEM_HERO_BADGE in src/church.c, so what this
   handler grants is the route itself and not a trophy. */
#define CH18_SEAL_ITEM_ID 0xa9
#define CH18_HERO_BADGE_ITEM_ID 0xdb

/* What fdps_unit_find_item_slot answers when the unit is not carrying the
   item: CMP dword ptr [EBP-0x14],-0x1 / JZ at 0003adc6, which jumps clear of
   the whole gate to the four unconditional steps. */
#define CH18_ITEM_NOT_CARRIED (-1)

/* How many units the promotion sweep looks at: CMP dword ptr [EBP-0x10],0x9 /
   JL at 0003add3.

   A LITERAL NINE, AND NOT THE LIVE UNIT COUNT (rebuild_info/pitfalls.md).  The
   neighbouring chapter-end code walks data_fdps_map_unit_count; this loop does
   not read it, has no side test and no early exit.  Those nine slots are the
   roster in join order, 蘭迪斯 through 琴琴, which is exactly the set the
   church will promote -- PROMOTABLE_PORTRAIT_COUNT in src/church.c is 9 as
   well.  瑪麗安 joins in chapter 15 at slot 9 and is never inspected, so a
   loop written over every live unit or over the whole roster would let a
   promoted 瑪麗安 deny a 勇者徽章 the original grants. */
#define CH18_PROMOTION_SWEEP_UNITS 9

/* The two cut-scenes, the strings at 0x6217c and 0x62188 loaded into EAX at
   0003ae3d and 0003ae52.  Both are named for the chapter that has just been
   WON: chapter 18 is the 0-based id 17, so the ordinary scene is Win17.dat and
   the one the trade earns is the -1 variant beside it.

   read_memory at 0x6217c returns 57 69 6e 31 37 2d 31 2e 64 61 74 00 57 69 6e
   31 37 2e 64 61 74 00, so the two literals are adjacent and the longer one
   comes first: a handler that read the operand of the wrong arm would still
   find a real member name.

   The literals are bare member names with no path and no container, and the
   lower-case spelling is the original's: see CH16_VICTORY_SCRIPT above for why
   it must not be tidied and why it cannot live in read-only storage. */
#define CH18_ALT_VICTORY_SCRIPT "Win17-1.dat"
#define CH18_VICTORY_SCRIPT "Win17.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x12 at 0003ae60.  The index is 0-based, so 18 is chapter
   19, 蛇口之道 -- both the village phase that runs next and the chapter loaded
   after it read this global, so this one store is what advances the game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 17 of a table indexed by this same global,
   and every handler in the family stores its own literal.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the scripts above
   are 17, the index of the chapter that has just been won, and this store is
   18, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH18_NEXT_CHAPTER_ID 0x12

/* Chapter 18's end handler, and the only chapter end in the game that hands
   out the 英雄 class-change route.  The four calls and one store the family
   shares are all still here and still in the family's order; what stands in
   front of them is a gate on 蘭迪斯's bag.

   THE GATE HAS TWO CONDITIONS AND BOTH MUST HOLD.  The first is that
   fdps_unit_find_item_slot finds the 神的聖印 in 蘭迪斯's bag; the second is
   that the sweep over the first nine units finds nobody who has already
   changed class.  The strategy guide states the same rule for this chapter --
   身上有神的聖印，且尚未有人轉職過 -- and it is why the seal is worth
   carrying unspent through seventeen chapters.

   HOW A CHANGED CLASS IS RECOGNISED, and it is not a flag.  The test is the
   record's form id at +0x07 against its character id at +0x08:
   fdps_roster_add_character seeds both bytes with the same character id
   (src/church.c), and the class-change animation is the only routine that ever
   rewrites +0x07, taking the new form id from the character's RankUp.dat route
   while leaving +0x08 alone.  The two bytes therefore differ exactly for a
   character who has been promoted.

   THE SWEEP DOES NOT STOP EARLY.  It has no break: every one of the nine is
   resolved and tested even once the flag is up.  The only edge out of the body
   at 0003ae04 is back to the increment.

   THE TRADE IS TWO CALLS AND NOT ONE.  The removal closes the gap in the bag
   and the add takes the first entry still flagged empty, so the 勇者徽章 does
   not necessarily land in the slot the 神的聖印 came out of (src/unititem.c).
   Nothing recomputes the combat stats afterwards, and nothing needs to: both
   items are carried rather than equipped and both are all zeroes in
   assets/items.md, and the writeback two lines below recomputes the roster
   copy anyway.

   THE TRADE RUNS BEFORE THE WRITEBACK, so the bag the party is banked with is
   the bag the trade left; the other order would leave the badge on a battle
   record that is thrown away with the chapter.

   THE SWEEP OF THE MAP IS BELT AND BRACES HERE, not the load-bearing step it
   is in chapters 3, 8 and 10.  Chapter 18 wins on 敵人全滅, the shared end
   condition's own test, and fdps_chapter_18_post_action (chpost2.h) adds
   nothing to it, so by the time this handler runs the enemy side is normally
   empty already.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003ae60 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_18_end(void)
{
    /* Which of 蘭迪斯's eight bag entries holds the 神的聖印, [EBP-0x14], or
       -1 when he is not carrying it.  It is the gate's first condition and the
       operand the removal is made with. */
    int seal_slot;
    /* Whether any of the nine has already changed class, [EBP-0x8].  Held in a
       byte, as the original's MOV byte ptr [EBP-0x8] does. */
    unsigned char has_promoted;
    /* Whether the trade was made, [EBP-0x4], and so which of the two victory
       scenes plays.  Held in a byte for the same reason. */
    unsigned char got_emblem;
    /* The record the sweep is looking at, [EBP-0xc]. */
    struct fdps_unit_record *unit;
    /* The sweep's counter, [EBP-0x10]: a battle unit index. */
    int unit_index;

    has_promoted = 0;
    got_emblem = 0;

    seal_slot = fdps_unit_find_item_slot(CH18_RANDIS_UNIT_INDEX,
                                         CH18_SEAL_ITEM_ID);
    if (seal_slot != CH18_ITEM_NOT_CARRIED) {
        for (unit_index = 0;
             unit_index < CH18_PROMOTION_SWEEP_UNITS;
             unit_index++) {
            unit = fdps_get_unit_record(unit_index);
            if (unit->portrait_id != unit->char_id) {
                has_promoted = 1;
            }
        }

        if (has_promoted == 0) {
            fdps_unit_remove_item(CH18_RANDIS_UNIT_INDEX, seal_slot);
            fdps_unit_add_item(CH18_RANDIS_UNIT_INDEX,
                               CH18_HERO_BADGE_ITEM_ID);
            got_emblem = 1;
        }
    }

    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    if (got_emblem != 0) {
        fdps_icon_script_run(CH18_ALT_VICTORY_SCRIPT);
    } else {
        fdps_icon_script_run(CH18_VICTORY_SCRIPT);
    }
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH18_NEXT_CHAPTER_ID;
}
