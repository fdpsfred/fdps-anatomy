/* chapter.h -- the chapter frame: the state a chapter starts from, the title
 * card it opens with and the four chapter-event dispatch tables
 * (rebuild_info/code_layout.md).
 *
 * Nothing here owns state of its own.  Everything the frame touches lives in
 * the game-state globals gamedata.c owns, and the chapter this file works on
 * is always the one data_fdps_chapter_current_chapter_id names.
 */
#ifndef CHAPTER_H
#define CHAPTER_H

/* Puts the battle map back to the state a chapter's first frame expects and
   rebuilds the unit array under it.

   Three things happen in one call.  The cursor overlay is switched off, the
   map's unit array is rebuilt for the chapter data_fdps_chapter_current_
   chapter_id names -- which loads that chapter's resources, so this is the
   expensive half -- and then the per-battle counters are put back: the
   per-cell event flags cleared, the view window and the cursor moved to the
   map origin, the battle-end code cleared to "still running", the turn counter
   set to turn 1 and the cursor overlay switched back on in mode 1.  Whatever
   scancodes were queued while all that ran are discarded.

   It takes no argument and returns nothing: the chapter is the global, not a
   parameter, and every one of the thirty-two call sites in the original relies
   on that.  It is not a "reset to zero" -- two of the values it installs are
   1, and the unit array it leaves behind is a full chapter's deployment. */
extern void fdps_chapter_state_reset(void);
#pragma aux fdps_chapter_state_reset "*" parm caller [];

#endif
