/* btlmenu.h -- the battle screen's system menu.
 *
 * The ring menu the player opens over the map with no unit chosen: the
 * four-entry system submenu here, and the outer menu that opens it.  The ring
 * itself -- the descriptor pair, the sweep animations and the cursor loop --
 * is menu.h, and everything in this file drives it rather than drawing it.
 * The commands an acting unit opens on its own turn are btlact.h.
 */
#ifndef BTLMENU_H
#define BTLMENU_H

/* 00014ea0.  The in-battle SYSTEM submenu: four entries -- the objectives
   window, save, load and quit -- built on a ring menu and run until the player
   commits to one of them or backs out.

   Takes nothing and answers with one of three numbers: -1 when the ring was
   cancelled with Escape or keypad Del, 1 when quitting the battle was
   confirmed, and 0 for every entry that ran and came back.  Its one caller,
   fdps_battle_system_menu, tests the answer against -1 to decide whether to
   reopen its own menu.

   THE MENU IS REOPENED, NOT RESUMED, AFTER A DECLINED QUIT.  Answering no to
   the quit prompt runs the whole opening sweep again with the cursor back on
   the first entry; the entry the player had chosen is not remembered.  Every
   other entry ends the call.

   TWO ENTRIES CAN BE GREYED OUT AND EACH FOR ITS OWN REASON.  Load is greyed
   when access() says FDE.SAV is not there, and save is greyed when any unit
   still in the battle has already acted this turn -- flags bit 0 clear and bit
   7 set (src/fdpstype.h) -- so the game refuses to write a save in the middle
   of a turn.  Both are decided once, before the first frame, and never
   revisited: a menu opened before the first unit moved keeps its save entry
   selectable for as long as it is open.

   CONFIRMING QUIT ONLY RAISES A FLAG.  data_fdps_shared_quit_game_requested
   (gamedata.h) is set and the answer is 1; nothing here tears the battle down.

   SAVING WRITES FDE.SAV WHOLE, and reads the file first to do it: the existing
   image is decrypted into a buffer so that the four chapter slots behind the
   battle resume region survive, the resume region is rebuilt from the live
   globals, the checksum is recomputed and the whole 0x59cb bytes are encrypted
   and written back (savefile.h owns the layout).  When there is no file to
   read the buffer is whatever malloc handed over, with only the four slots'
   chapter bytes stamped 0xff to mark them empty -- so the slot region of a
   save written on a machine that had none is uninitialised heap, and that is
   what the original does.

   LOADING DOES NOT COME BACK HERE IN ANY USEFUL STATE.  fdps_load_savegame
   installs a whole battle over the live globals and restarts the player phase
   (savefile.h); this function then answers 0 to a caller whose own battle
   state has been replaced underneath it.

   IT DRAWS STRAIGHT TO THE ADAPTER THROUGHOUT and repaints the view itself
   after every pass of the ring, so a caller gets the screen back holding the
   map, or the message panel's last frame when an entry ran one. */
extern int fdps_battle_system_submenu(void);
#pragma aux fdps_battle_system_submenu "*" parm caller [];

#endif
