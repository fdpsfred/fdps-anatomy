/* save.h -- FDE.SAV: the save and load screens and the modal slot cursor they
 * share (rebuild_info/code_layout.md).
 *
 * The page the three slots are drawn on is savepnl.h's, and the file the
 * screens read and write -- its shape on disc, its checksum and its cipher --
 * is savefile.h's.
 */
#ifndef SAVE_H
#define SAVE_H

#include "fdpstype.h"

/* How many slots the save and load panel offers, and the modulus the slot
   cursor wraps on.  MOV EBX,0x3 before each of the two IDIVs at 000246b9 and
   000246ef, and CMP dword ptr [EBP-0x8],0x3 / JGE at 0002490c bounds the loop
   in fdps_saveload_screen_build that fills the occupied flags (savepnl.h).
   It is three and not the four chapter slots the file on disc carries: the
   fourth slot of the image is not reachable from this screen. */
#define SAVE_SLOT_COUNT 3

/* 00064104.  Which of the two screens is running: zero for the save screen,
   non-zero for the load screen.  Its two writers are the screens themselves --
   MOV dword ptr [0x00064104],0x0 at 000241fa in fdps_save_game_screen and 0x1
   at 000244a3 in fdps_load_game_screen -- and its one reader is
   fdps_save_slot_select_loop, where it decides whether an empty slot may be
   confirmed.

   It is a mode, not a count, and it is never cleared on the way out: whichever
   screen ran last leaves its value behind.  Nothing else in the image looks at
   it, so that costs nothing. */
extern int data_fdps_ui_saveload_is_load_mode;

/* 000241e0.  The save screen, and the whole of what "Save" on the village and
   bar menus does: it reveals the three-slot panel, runs the slot cursor,
   writes the live game state into the slot the player picked, redraws the
   panel so the new save shows, and offers it again until the player presses
   Escape.

   `restore_page` is a 64000-byte 320x200 8bpp page the caller owns.  It is
   never written to and never freed here; it is only what the closing mosaic
   puts back on the adapter when the screen ends.  fdps_run_village_phase
   passes a freshly zeroed page, so the village fades to black behind the menu
   it is about to redraw; fdps_run_bar_shop passes the page still holding the
   bar screen, so the bar comes back.

   NOTHING IS REPORTED.  The function is void: whether a save was written,
   which slot took it, and whether the disc could be written to at all are all
   invisible to the caller.  The write stream's fopen is not tested.

   THE PLAYER MAY SAVE AS MANY TIMES AS HE LIKES IN ONE VISIT.  Every confirm
   writes the file and then rebuilds the panel, and only Escape ends the
   screen.  The slot cursor keeps its position across those passes.

   AN EMPTY SLOT MAY BE PICKED, because the mode flag above is set to 0 on the
   way in and that is what short-circuits the occupied test inside
   fdps_save_slot_select_loop.

   IT WRITES THE WHOLE OF FDE.SAV, not one slot of it: the file is read back
   into memory, the picked slot's 0xa28 bytes are replaced, the checksum is
   recomputed over the whole image and everything is encrypted and written out
   again.  A missing file is not an error -- the image is filled with 0xff, so
   the three slots the player did not pick come out reading as never written
   (rebuild_info/pitfalls.md).

   IT LEAVES THE SPRITE CACHE HOLDING THE PARTY and the mode flag at 0, both
   of them by way of fdps_saveload_screen_build; and it needs the timer
   interrupt running, because the slot cursor paces its frames on it. */
extern void fdps_save_game_screen(unsigned char *restore_page);
#pragma aux fdps_save_game_screen "*" parm caller [];

/* 00024650.  The modal loop both save/load screens run once the slot panel is
   on the page: it drives a three-entry slot cursor from the keyboard, animates
   the cursor highlight over a background the caller composed, and answers
   whether a slot was picked or the screen was backed out of.

   `background` is a whole 320x200 page the caller owns -- the panel with the
   three slots already drawn on it.  It is never written to: each frame copies
   it into a page of its own and draws over the copy.  `slot` points at the
   caller's cursor, which is read AND written: the arrow keys move it and the
   caller reads it back to find out which slot was picked.  Nothing validates it
   on entry, and a starting value outside 0..2 stays outside it -- the modulus
   only closes over the range once a key has moved the cursor at least once, and
   the highlight is drawn at 25 + 52 * whatever it holds.  It never happens:
   both call sites clear their cursor immediately before the call, MOV dword ptr
   [EBP-0xc],0x0 at 0002449c in fdps_load_game_screen and [EBP-0x14],0x0 at
   000241ec in fdps_save_game_screen, and both then index the save image with
   that same value times 0xa28.

   The answer is 1 for a confirmed slot and -1 for a cancelled screen; 0 is the
   value the loop tests to decide whether to run another pass, so it is never
   returned.

   ESCAPE IS THE ONLY CANCEL KEY.  The ring menu's second cancel key, keypad Del
   at 0x53, is not one of the six codes this loop knows (menu.c), so a player
   who backs out of every other modal screen with it finds it does nothing here.
   Space and Enter both confirm; up and left both step back; right and down both
   step forward.

   A CONFIRM ON AN EMPTY SLOT IS SILENTLY IGNORED ON THE LOAD SCREEN and
   accepted on the save screen: the test is
   data_fdps_ui_saveload_is_load_mode == 0 first, and only then the occupied
   flag.  A rejected confirm plays no sound and leaves no trace -- the pass
   costs a frame and the loop goes round again.

   ONE MORE FRAME IS ALWAYS DRAWN AFTER THE DECISION.  The arms that set the
   answer only store it; the frame below them runs regardless and the loop test
   is at the top, so the screen the caller inherits is a full frame drawn with
   the cursor on the slot that was picked.  Returning out of the branch instead
   would leave the previous frame's highlight on the adapter.

   The cursor animation is four ticks per frame, folded: ((tick >> 2) & 3) with
   3 mapped to 1, so the highlight cycles 0, 1, 2, 1 through a sheet that holds
   exactly three sprites.  Dropping the fold asks LoadKon.cel for a sprite it
   does not have and the drawer, which range checks nothing, reads a stream
   address from past the offset table (sprite.h).

   The frames are paced by the vertical retrace and by
   data_fdps_timer_tick_counter, so the loop does not return until the timer
   interrupt is running.  A container or a member that cannot be found ends the
   process inside fdps_vfs_load_entry rather than coming back (vfs.h). */
extern int fdps_save_slot_select_loop(void *background, int *slot);
#pragma aux fdps_save_slot_select_loop "*" parm caller [];

#endif
