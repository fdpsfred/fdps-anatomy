/* btlend.h -- deciding a battle is over, and the window that reports it.
 *
 * The tallies here are what the win/fail test and the result window are
 * phrased in: how many units of a given side are still standing, and the
 * standard end test that turns that into the battle-end code main dispatches
 * on.  Nothing in btlend.c owns state -- it reads the battle unit array
 * through unit.h, and reads and writes the unit count, the chapter id and the
 * battle-end code through gamedata.h.
 */
#ifndef BTLEND_H
#define BTLEND_H

/* How many units of one side are still in the battle?  Walks battle unit
   indices 0..data_fdps_map_unit_count-1 (gamedata.h) and counts the ones whose
   side byte -- struct fdps_unit_record's side at record offset 6 -- equals
   side and which fdps_unit_is_retired (unit.h) reports have not left the
   battle.  Retired units are passed over, so the answer is a survivor tally
   and not a roster size; it is 0 when nothing matches, and 0 when the unit
   count is 0 or below because the bound is tested before the body runs.

   side is a side code, not a truth value, and is compared for equality against
   the record byte widened to 0..255: 0 is the enemy side, 2 is the player
   side, and 1 is the third side the result window reports separately.  A side
   code no unit carries is not an error and simply counts nothing.

   The bound is the live unit count and the index is not otherwise range
   checked; each record is resolved through fdps_get_unit_record as the walk
   reaches it, so no record pointer is held across the loop. */
extern int fdps_battle_count_remaining_units_on_side(int side);
#pragma aux fdps_battle_count_remaining_units_on_side "*" parm caller [];

/* Applies the game's standard battle end conditions and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h), where 0 means the
   battle carries on, 1 defeat and 2 chapter cleared.  Takes nothing and
   returns nothing: the verdict is the whole answer, and main reads it after
   each pass of the player phase.

   It does nothing at all unless that code is still 0, so an outcome already
   recorded by a chapter event or by an earlier handler is never recomputed.

   Otherwise it writes 2 up front and then looks for a reason not to keep it.
   The victory test is a walk over battle unit indices
   0..data_fdps_map_unit_count-1 (gamedata.h): a unit on side 0 -- the enemy --
   whose retirement bit is still clear puts the code back to 0.  The walk runs
   to the end of the array rather than stopping at the first survivor, and
   nothing restores the 2, so one live enemy anywhere is enough.

   The defeat test then runs whatever the walk concluded, and is NOT an else
   branch of it: in chapters 17 and 22 -- chapter ids 0x10 and 0x15, which are
   0-based -- it asks whether unit slot 3 has retired, and in every other
   chapter unit slot 0, and stores 1 when it has.  Because that store carries
   no guard of its own, a defeat overrides a victory the walk recorded moments
   earlier in the same call: when the last enemy falls in the same action that
   retires the must-survive unit, the answer is 1 and not 2.

   Both indices are used unchecked against the live unit count, so the defeat
   test reads slot 0 or slot 3 of the array whether or not the battle deployed
   that many units. */
extern void fdps_battle_check_default_end_conditions(void);
#pragma aux fdps_battle_check_default_end_conditions "*" parm caller [];

/* Shows the chapter's victory and defeat conditions over the live battle map
   and waits for a keypress.  Entry 0 of the in-battle system submenu, and its
   only caller.  Takes nothing, returns nothing and leaves no state behind: the
   panel is built, animated and released inside the call, and the screen it
   was drawn over is not restored -- whatever repaints the map next is what
   puts it back.

   The panel is a 209 x 133 page built from scratch on every call.  MISC.VFS is
   opened, WinFail.Cel is taken out of it as the artwork, and six numbers are
   stamped onto it: the chapter number (the chapter id plus one, so it reads
   1-based) in two digits, the battle turn counter in three, the party's gold
   in eight, and the units still standing on side 0, side 2 and side 1 in two
   each, from fdps_battle_count_remaining_units_on_side above.  Entries 2 and 3
   of the chapter's own text block -- the victory and the defeat condition --
   are drawn under them.

   OPENING THE CONTAINER RELOADS THE NINE GLOBAL DATA TABLES AND LEAKS THE
   PREVIOUS SET.  That is not a side effect of showing the panel that a caller
   could avoid; it is what the function does, and a build that hoisted the load
   out of it or freed the old tables would behave differently
   (rebuild_info/pitfalls.md).  A missing MISC.VFS prints a line and ends the
   process, and so does a container without WinFail.Cel in it.

   The panel is then slid down over the map in four horizontal strips, held,
   and slid off: 19 slide-in frames, then as many hold frames as the player
   leaves it up for, then 13 slide-out frames.  Every frame recomposes the
   whole battle map behind the panel with fdps_draw_scene_layers (mapdraw.h) on
   a page of its own, is presented in the vertical blanking interval, and is
   paced to one timer tick, so the panel animates over a map that goes on
   moving underneath it.

   THE HOLD ENDS ON A MAKE CODE AND NOTHING ELSE.  The queue is emptied first,
   so anything pressed during the slide-in is discarded, and the loop then runs
   for as long as the scancode it reads is 0x80 or above -- which covers both
   the 0xff an empty queue reports and a key RELEASE code.  A key held down
   from before the panel opened therefore does not dismiss it, and the release
   of that key does not either: it takes a fresh press.

   Nothing about the display is restored on the way out.  The last slide-out
   frame is left on the screen, the keyboard queue is emptied a second time,
   and the caller repaints. */
extern void fdps_battle_show_win_fail_window(void);
#pragma aux fdps_battle_show_win_fail_window "*" parm caller [];

#endif
