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
