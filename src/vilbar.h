/* vilbar.h -- the village bar and the lucky draw it opens.
 *
 * The bar is one whole visit to the building the signboard menu opens: it
 * loads its own backdrop, publishes its own page, runs a row of command icons
 * over it and leaves when the player backs out of that row.  Its commands are
 * the save screen, the load screen and the quit-to-title question rather than
 * any party counter, so this is the village phase's door into the save system
 * and the one screen a visit can end the game from.  The draw below is opened
 * on the way in, before the row is drawn, and the bar is its only caller.
 */
#ifndef VILBAR_H
#define VILBAR_H

/* 00036460.  The bar's hidden lucky draw: a ten-frame reel that spins up, is
   stopped by a key press, spins down again, hands out a prize and announces
   what it landed on.  It takes nothing, answers nothing, and its one call site
   is fdps_run_bar_shop, which reaches it unconditionally on the way into the
   bar and cleans no stack after the CALL.

   IT ALMOST NEVER RUNS.  The body sits under one four-part test -- the
   run-once flag data_fdps_bonus_lottery_drawn_flag still 0 AND the DOS system
   date exactly year 1998, month 1, day 28 -- and the whole function is a
   return on any other day.  The flag is set only on the way out of a draw that
   completed, so a session can take the draw once; it is cleared by
   fdps_title_screen and travels in the save record (gamedata.h), so a saved
   game carries the fact that the draw has been used.  The date is read with
   _dos_getdate BEFORE the flag is looked at, which is the only thing this
   function does on any other day.

   IT OWNS EVERYTHING IT ALLOCATES EXCEPT THE FANFARE.  Two 64,000-byte screen
   snapshots and one 368x248 work page are taken from the heap and all three
   are freed on the way out, as is the loaded reel clip.  The Bonus.wav image
   the top prize plays is loaded and never freed -- a leak of 128,216 bytes
   that happens at most once per run and that the original does not clean up.

   THE PRIZE HANDED OUT IS NOT THE PRIZE ANNOUNCED.  The four-way test that
   awards the prize reads a stack slot nothing has written yet, and the only
   store to that slot happens after the test, so what the player receives is
   whatever the frame's storage happened to hold while the message that follows
   names the class the reel really stopped on (rebuild_info/pitfalls.md).  The
   four classes are 0 a 斬鐵劍 for every roster member, one of the three power
   tiers picked by the chapter's decade; 1 ten 水晶粒 for every member; 2
   20,000 gold onto data_fdps_shared_party_total_gold; and anything else one
   藥草 for every member, which is what the unwritten slot in practice
   selects.

   WHAT IT NEEDS IN PLACE.  MISC.VFS has to hold "Bonus-1.saf" and, for the top
   prize, "Bonus.wav" -- a container or a member that cannot be found ends the
   process inside fdps_vfs_load_entry (vfs.h).  The adapter has to be in mode
   13h already: the snapshots are taken straight off the aperture and every
   frame of the reel is blitted straight onto it.  The timer tick counter has
   to be running, because every presented frame waits for it to change, and the
   scancode ring has to be fed, because the reel runs until
   fdps_read_keyboard_queue answers with a make code of 0x7f or below.

   IT LEAVES THE REEL ON THE SCREEN.  Nothing repaints the picture the draw
   opened over, so the caller gets the stopped reel with the closing message's
   window swept shut over it. */
extern void fdps_run_bonus_lottery(void);
#pragma aux fdps_run_bonus_lottery "*" parm caller [];

#endif
