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
