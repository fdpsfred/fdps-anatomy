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

/* Shows the current chapter's title card and returns when it has finished:
   the chapter's graphic fades up out of black, stands at full brightness for
   a second, fades back down to black, and the screen is left cleared.  The
   whole thing takes a little over three and a half seconds of real time and
   nothing can interrupt it -- no key is read and no flag is tested.

   It takes no argument and returns nothing.  WHICH card is drawn comes from
   data_fdps_chapter_current_chapter_id (gamedata.h) as a 0-based index into
   Chapter.saf's thirty entries.  The thirty callers -- the chapter entry
   handlers fdps_chapter_01_init .. fdps_chapter_30_init -- do not set that
   global themselves; it already holds the chapter by the time one of them
   runs.  The image writes the global in thirty-four places and none of them
   is inside an entry handler.  Twenty-nine are the chapter-end handlers
   fdps_chapter_01_end .. fdps_chapter_29_end, each storing the next
   chapter's index as a literal as its last act before the epilogue -- 1 at
   0003a440 up to 29 at 0003ba39, in order; fdps_chapter_30_end has no such
   write, chapter 30 being the last.  The other five enter a chapter from
   outside that sequence: fdps_title_screen (0, starting a new game),
   fdps_load_savegame and fdps_load_game_screen (the chapter byte out of the
   save record), fdps_title_demo (25, the demo's fixed chapter) and
   fdps_icon_script_run (the script opcode that advances the story).

   The card is loaded, drawn and freed inside the call: Chapter.saf and
   Chapter.pal come out of MISC.VFS, the picture is composed on a private
   368x248 page, and all three allocations are released before the return, so
   nothing is left behind and nothing is cached between chapters.

   WHAT THE CALLER INHERITS.  The mode 13h aperture is cleared to palette
   index 0 and the DAC is left holding the master palette
   data_fdps_vga_main_palette_ptr names at no bias -- a live palette over a
   blank screen.  The caller has to repaint; it does not have to fade back
   in. */
extern void fdps_show_chapter_title_card(void);
#pragma aux fdps_show_chapter_title_card "*" parm caller [];

#endif
