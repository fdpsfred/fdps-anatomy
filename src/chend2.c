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

/* The gate on the whole recovery sweep, CMP byte ptr [0x000640e9],0x0 / JZ at
   0003b0bc: element 0x11 of the 32-entry array
   data_fdps_map_cell_event_triggered_flags based at 0x000640d8 (gamedata.h),
   the slot chapter 19 keeps 裘娜's duel in.

   THE GATE IS "THE DUEL WAS OFFERED", NOT "THE DUEL WAS FOUGHT"
   (rebuild_info/pitfalls.md).  fdps_chapter_19_post_action (chpost2.h) raises
   this byte once it has put the challenge to the player and raises it on BOTH
   answers, the refusal included; only the acceptance also stages the duel by
   marking the rest of the field retired.  A player who declines therefore
   arrives here with the byte up and nothing staged, and still gets the free
   full recovery below.  Reading the gate as "if the duel happened" -- the
   obvious reading of a step whose job is to undo duel staging -- silently
   takes that recovery away.

   fdps_chapter_state_reset clears the whole block when a chapter starts and
   fdps_chapter_19_init calls it, so a chapter 19 whose event never fired
   arrives here with the byte at 0. */
#define CH19_DUEL_LATCH_SLOT 0x11

/* Which records the recovery is for, MOV AL,[EAX+0x8] / AND EAX,0xff / CMP
   EAX,0xb / JG at 0003b0f3: every battle unit whose character id is 0x0b or
   lower.  Those twelve ids are the permanent roster characters, the only ones
   fdps_roster_add_character is ever passed, so the test is "is this record a
   party member" asked of the id rather than of the side byte.

   IT IS AN UNSIGNED COMPARE, and the AND EAX,0xff in front of it is what
   makes it one (rebuild_info/pitfalls.md).  char_id is an unsigned char in
   src/fdpstype.h and must stay one: a plain char is signed under Watcom, and
   a signed test would let every id from 0x80 up through the gate and heal the
   enemy side with the party. */
#define CH19_LAST_ROSTER_CHAR_ID 0x0b

/* What each recovered record is left holding in its flags byte, MOV byte ptr
   [EAX+0x5],0x0 at 0003b103.  IT IS A WHOLE-BYTE STORE AND NOT AN AND-NOT: it
   drops the retired bit 0x01 that fdps_unit_is_retired reads and everything
   else the byte was holding with it, the has-acted bit 0x80 included.  The
   duel staging's matching store is the same shape in the other direction. */
#define CH19_UNIT_FLAGS_CLEARED 0

/* Chapter 19's victory cut-scene, the string at 0x62194 loaded into EAX at
   0003b12a and pushed as fdps_icon_script_run's only argument.  The member is
   named for the chapter that has just been WON: chapter 19 is the 0-based id
   18, so this is Win18.dat -- the same 18 fdps_chapter_19_init passes as
   Icon18.dat.

   read_memory at 0x62188 returns 57 69 6e 31 37 2e 64 61 74 00 64 61 57 69 6e
   31 38 2e 64 61 74 00 64 61 57 69 6e 31 39 2e 64 61, so "Win17.dat" and
   "Win19.dat" sit either side of it with two bytes of filler between: a
   handler that read the operand one step in either direction would still find
   a real member name.

   The literal is the bare member name with no path and no container: see
   CH16_VICTORY_SCRIPT above for why the lower-case spelling is the original's
   and why it cannot live in read-only storage. */
#define CH19_VICTORY_SCRIPT "Win18.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x13 at 0003b13d.  The index is 0-based, so 19 is chapter
   20 -- both the village phase that runs next and the chapter loaded after it
   read this global, so this one store is what advances the game.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 18, the index of the chapter that has just been won, and this store is
   19, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH19_NEXT_CHAPTER_ID 0x13

/* Chapter 19's end handler.  The family's four calls are down to three -- this
   is one of the four handlers in the game that never sweeps the map -- and in
   front of them stands a conditional free full recovery of the whole party.

   WHY THE RECOVERY IS THERE.  Chapter 19 runs 裘娜's duel, and staging it
   costs the rest of the field its place: fdps_chapter_19_post_action marks
   every unit record from 0 to 0x4c retired except index 4, her own slot, so
   that she faces the challenger alone.  Nothing else in the chapter takes
   those marks off again, so this handler is where the field is put back.

   IT HEALS RATHER THAN JUST UN-RETIRING, which is what separates it from the
   same duel's handler four chapters earlier.  fdps_chapter_15_end clears the
   flags byte and stops; this one goes on to put current hit points and current
   magic points back on their maxima.  The stores are belt and braces --
   fdps_roster_write_back_battle_units restores both by itself once the flags
   byte is clear -- but they are what the original does and they are what a
   unit the writeback never reaches is left holding.

   BECAUSE THE GATE DOES NOT RECORD WHICH ANSWER THE PLAYER GAVE, a chapter 19
   where the duel was declined reaches this handler with the byte up and
   nothing staged, and the sweep then revives whoever genuinely fell during the
   chapter -- free, and before the paid revive screen ever sees them.  That is
   the chapter's behaviour and not a slip; see CH19_DUEL_LATCH_SLOT above.

   THE RECOVERY MUST RUN BEFORE THE WRITEBACK (rebuild_info/pitfalls.md).
   fdps_roster_write_back_battle_units masks the unit flags byte down to bit 0
   and skips restoring current hit points from the maximum whenever that bit
   survives, so banking the party first and healing it afterwards carries the
   whole party into chapter 20 recorded as dead -- and after an accepted duel,
   which stamps that bit on everyone but 裘娜, fdps_roster_revive_fallen_members
   then bills the player for reviving all of them.

   THE SWEEP WALKS THE LIVE UNIT COUNT, data_fdps_map_unit_count, and not a
   literal: CMP EAX,[0x00060150] / JL at 0003b0cf.  The sibling at chapter 15
   walks a literal nine instead, so the two loops are not interchangeable.

   THE MAP IS NEVER SWEPT.  There is no call to
   fdps_battle_destroy_remaining_enemies anywhere in the body -- chapters 1, 2,
   15 and 19 are the only handlers that leave it out -- so any enemy still
   standing when the chapter ends is still standing when the cut-scene plays.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003b13d is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_19_end(void)
{
    /* The record each of the three stores is made through, [EBP-0x4].  The
       original reloads the slot into EAX between the call and every one of
       them rather than keeping the pointer in a register. */
    struct fdps_unit_record *unit;
    /* The recovery sweep's counter, [EBP-0x8]: a battle unit index. */
    int unit_index;

    if (data_fdps_map_cell_event_triggered_flags[CH19_DUEL_LATCH_SLOT] != 0) {
        for (unit_index = 0;
             unit_index < data_fdps_map_unit_count;
             unit_index++) {
            unit = fdps_get_unit_record(unit_index);
            if (unit->char_id <= CH19_LAST_ROSTER_CHAR_ID) {
                unit->flags = CH19_UNIT_FLAGS_CLEARED;
                unit->hp_current = unit->hp_max;
                unit->mp_current = unit->mp_max;
            }
        }
    }

    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH19_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH19_NEXT_CHAPTER_ID;
}

/* Chapter 20's victory cut-scene, the string at 0x621a0 loaded into EAX at
   0003b1e6 and pushed as fdps_icon_script_run's only argument.  The member
   name is based on the chapter that has just been WON: chapter 20 is the
   0-based id 19, so this is Win19.dat.

   The literal starts exactly where the instruction says it does: read_memory
   at 0x62188 returns 57 69 6e 31 37 2e 64 61 74 00 64 61 57 69 6e 31 38 2e 64
   61 74 00 64 61 57 69 6e 31 39 2e 64 61 74 00 64 61 57 69 6e 32 30 2e 64 61
   74 00 64 61, so "Win18.dat" sits at 0x62194, 0x6219e..0x6219f is the 64 61
   filler that pads it to a four-byte boundary, 0x621a0 is the W of
   "Win19.dat" and "Win20.dat" follows it at 0x621ac.

   BOTH NEIGHBOURS ARE REAL MEMBER NAMES, so neither slip announces itself:
   0x62194 is what the sibling one chapter back really names, the member a
   body copied from fdps_chapter_19_end without changing the operand would
   open, and 0x621ac is what counting forward from the chapter number rather
   than reading the operand at 0003b1e6 would reach.

   The literal is the bare member name with no path and no container, and the
   lower-case spelling is the original's: see CH16_VICTORY_SCRIPT above for
   why it must not be tidied and why it cannot live in read-only storage. */
#define CH20_VICTORY_SCRIPT "Win19.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x14 at 0003b1f9.  The index is 0-based, so 20 is chapter
   21, 地底神殿 -- both the village phase that runs next and the chapter loaded
   after it read this global, so this one store is what advances the game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 19 of a table indexed by this same global,
   and every handler in the family stores its own literal.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 19, the index of the chapter that has just been won, and this store is
   20, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH20_NEXT_CHAPTER_ID 0x14

/* Chapter 20's end handler: the family's plain shape, four calls and one
   store, no branch and no local anywhere in the body.  It is
   fdps_chapter_16_end and fdps_chapter_17_end above instruction for
   instruction, with its own script name and its own stored index -- the three
   handlers differ in exactly two operands each.

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
   directly.  Unlike fdps_chapter_19_end just above it has no gate on a
   one-shot latch and no recovery sweep either -- the first instruction after
   the frame is the sweep call at 0003b1dc, with nothing tested before it, and
   the chapter's own staging is undone nowhere because chapter 20 stages
   nothing on the party.

   THE SWEEP IS BELT AND BRACES HERE, not the load-bearing step it is in
   chapters 3, 8 and 10.  Chapter 20's 勝利條件 is the shared end condition's
   own 敵人全滅 and fdps_chapter_20_post_action (chpost2.h) adds no end
   condition of its own -- all it does during the battle is release the map's
   held enemies three per turn -- so that condition records a clear only once
   no unit on the enemy side is still standing, and the sweep normally finds
   that side already empty.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003b1f9 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_20_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH20_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH20_NEXT_CHAPTER_ID;
}

/* Chapter 21's victory cut-scene, the string at 0x621ac loaded into EAX at
   0003b246 and pushed as fdps_icon_script_run's only argument.  The member
   name is based on the chapter that has just been WON: chapter 21 is the
   0-based id 20, so this is Win20.dat.

   The literal starts exactly where the instruction says it does: read_memory
   at 0x62190 returns 74 00 64 61 57 69 6e 31 38 2e 64 61 74 00 64 61 57 69 6e
   31 39 2e 64 61 74 00 64 61 57 69 6e 32 30 2e 64 61 74 00 64 61 57 69 6e 32
   31 2e 64 61, so "Win19.dat" sits at 0x621a0, 0x621aa..0x621ab is the 64 61
   filler that pads it to a four-byte boundary, 0x621ac is the W of
   "Win20.dat" and "Win21.dat" follows it at 0x621b8.

   BOTH NEIGHBOURS ARE REAL MEMBER NAMES, so neither slip announces itself:
   0x621a0 is what the sibling one chapter back really names, the member a
   body copied from fdps_chapter_20_end without changing the operand would
   open, and 0x621b8 is what counting forward from the stored chapter index
   rather than reading the operand at 0003b246 would reach.

   The literal is the bare member name with no path and no container, and the
   lower-case spelling is the original's: see CH16_VICTORY_SCRIPT above for
   why it must not be tidied and why it cannot live in read-only storage. */
#define CH21_VICTORY_SCRIPT "Win20.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x15 at 0003b259.  The index is 0-based, so 21 is chapter
   22, 巫湯婆婆, the chapter this one hands the game on to.  The store is an
   assignment and not a step: nothing reads the global first.

   THIS PARTICULAR INDEX IS ALSO READ BY THE SHARED END TEST.  0x15 is one of
   the two chapter ids fdps_battle_check_default_end_conditions singles out
   (btlend.h), so the chapter this store selects is one whose defeat condition
   watches unit slot 3 rather than slot 0 -- see fdps_chapter_22_post_action
   (chpost2.h).  Getting this literal wrong therefore changes more than which
   chapter plays next.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 20, the index of the chapter that has just been won, and this store is
   21, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH21_NEXT_CHAPTER_ID 0x15

/* Chapter 21's end handler: the family's plain shape, four calls and one
   store, no branch and no local anywhere in the body.  It is
   fdps_chapter_20_end immediately above it instruction for instruction, with
   its own script name and its own stored index -- the two handlers differ in
   exactly two operands.

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
   directly.  It has no gate on a one-shot latch and no recovery sweep -- the
   first instruction after the frame is the sweep call at 0003b23c, with
   nothing tested before it -- and chapter 21 stages nothing on the party for a
   recovery to undo.

   THE SWEEP IS BELT AND BRACES HERE, not the load-bearing step it is in
   chapters 3, 8 and 10.  Chapter 21's 勝利條件 is the shared end condition's
   own 敵人全滅 and fdps_chapter_21_post_action (chpost2.h) is a bare forward
   to that shared test, adding no end condition of its own, so the clear is
   recorded only once no unit on the enemy side is still standing and the sweep
   normally finds that side already empty.  The chapter's two reinforcement
   waves are position triggers rather than post-action business, so they do not
   change that.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003b259 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_21_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH21_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH21_NEXT_CHAPTER_ID;
}

/* Chapter 22's victory cut-scene, the string at 0x621b8 loaded into EAX at
   0003b2b6 and pushed as fdps_icon_script_run's only argument.  The member
   name is based on the chapter that has just been WON: chapter 22 is the
   0-based id 21, so this is Win21.dat.

   The literal starts exactly where the instruction says it does: read_memory
   at 0x621a0 returns 57 69 6e 31 39 2e 64 61 74 00 64 61 57 69 6e 32 30 2e 64
   61 74 00 64 61 57 69 6e 32 31 2e 64 61 74 00 64 61 57 69 6e 32 32 2e 64 61
   74 00 64 61, so "Win20.dat" sits at 0x621ac, 0x621b6..0x621b7 is the 64 61
   filler that pads it to a four-byte boundary, 0x621b8 is the W of
   "Win21.dat" and "Win22.dat" follows it at 0x621c4.

   BOTH NEIGHBOURS ARE REAL MEMBER NAMES, so neither slip announces itself:
   0x621ac is what the sibling one chapter back really names, the member a
   body copied from fdps_chapter_21_end without changing the operand would
   open, and 0x621c4 is what counting forward from the stored chapter index
   rather than reading the operand at 0003b2b6 would reach.

   The literal is the bare member name with no path and no container, and the
   lower-case spelling is the original's: see CH16_VICTORY_SCRIPT above for
   why it must not be tidied and why it cannot live in read-only storage. */
#define CH22_VICTORY_SCRIPT "Win21.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x16 at 0003b2c9.  The index is 0-based, so 22 is chapter
   23, 死神冥河, the chapter this one hands the game on to.  The store is an
   assignment and not a step: nothing reads the global first.

   THE INDEX THIS HANDLER WAS REACHED BY is the one it leaves behind minus
   one, 0x15, and that is the id the shared end test singles out
   (btlend.h) -- but the chapter it selects here, 0x16, is not one of the two,
   so the chapter that runs next is back on the shared test's ordinary arm.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 21, the index of the chapter that has just been won, and this store is
   22, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH22_NEXT_CHAPTER_ID 0x16

/* Chapter 22's end handler: the family's plain shape, four calls and one
   store, no branch and no local anywhere in the body.  It is
   fdps_chapter_21_end immediately above it instruction for instruction, with
   its own script name and its own stored index -- the two handlers differ in
   exactly two operands.

   VALUES USED AFTER A CALL: none.  Nothing here reads EAX after any of the
   four CALLs.  fdps_icon_script_run does leave a uint in EAX and this call
   site discards it -- the ADD ESP,0x4 at 0003b2c1 and the CALL at 0003b2c4
   are all that follow it -- so the result is not a value this handler has.

   THE SWEEP IS LOAD-BEARING HERE, which is what separates this handler from
   the chapter 20 and 21 ones it is otherwise a copy of.  Chapter 22's
   勝利條件 is 擊倒巫湯婆婆, one named boss and not 敵人全滅, and
   fdps_chapter_22_post_action (chpost2.h) is the only handler in its own
   family that does not forward to fdps_battle_check_default_end_conditions at
   all: it declares no victory, so the clear is the scripted boss-defeat
   event's to write.  The chapter is therefore reached with the boss's minions
   -- the map's 狼人戰士, 蛇魔使, 幽魂 and 骷髏兵 groups, none of which the
   clear waits on -- still standing, and the sweep is what takes them off the
   map before the cut-scene plays.  Its death pass
   really does run here: it collects the units whose retired bit is still clear
   after it has zeroed their hit points and plays them off the map (btlend.h),
   which in chapters 20 and 21 finds an empty list and here does not.

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
   directly.  It has no gate on a one-shot latch and no recovery of the field
   either -- the first instruction after the frame is the sweep call at
   0003b2ac, with nothing tested before it -- and chapter 22 stages nothing on
   the party for a recovery to undo, even though it is the chapter that deploys
   蘭迪斯以外的所有人: leaving him out of the deployment is the chapter's own
   setup and not something this handler puts back.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003b2c9 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_22_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH22_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH22_NEXT_CHAPTER_ID;
}
