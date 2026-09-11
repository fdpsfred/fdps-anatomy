/* btlmenu.h -- the battle screen's own command menus.
 *
 * These are the ring menus the player opens over the map: the four-entry
 * system submenu here, and the command and search menus that share the file.
 * The ring itself -- the descriptor pair, the sweep animations and the cursor
 * loop -- is menu.h, and everything in this file drives it rather than drawing
 * it.
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

/* 000184f0.  Offers the unit that has just finished acting the chance to
   search the special map cell standing under the map cursor, and hands over
   whatever that cell holds.  Takes the searching unit's index in the battle
   unit array and returns nothing; everything it does is to that unit's bag,
   to the party's gold, to the cell's own record and to the screen.  Its one
   caller, fdps_battle_action_menu, passes the unit whose turn it is.

   WHICH CELLS IT WILL SEARCH.  The cursor's world pixel is divided by the
   24-pixel tile size and handed to fdps_map_load_tile_info (maptile.h); the
   cell is searchable when the 0x60 field of its tile attribute byte is 0x20,
   the chest class, or 0x40, the buried-treasure class, and 0x00 and 0x60 are
   both refused.  A cell whose event code is already flagged in
   data_fdps_map_cell_event_triggered_flags (gamedata.h) is refused as well.
   On a refusal nothing at all happens: no window is opened and no global is
   written.

   WHAT A CELL HOLDS is a three-byte record in the resident MAP%02d.DAT block,
   at offset 0x53 plus three times the cell's event code -- directly behind the
   two-byte per-cell event table maptile.h describes at 0x33.  Byte 0 is the
   kind and the word at byte 1 is the payload:

     kind 0, an item: the payload is the ITEM.DAT id, added to the searching
       unit's bag.  When it fits, the cell is flagged searched and
       fdps_map_apply_triggered_cell_changes turns its tile over.
     kind 1, money: the payload is added to data_fdps_shared_party_total_gold,
       and the cell is flagged and turned over the same way.  A payload of zero
       is a legal cell with its own line, not an empty one.
     kind 2 and above, scripted: the payload -- not the kind -- indexes
       data_fdps_chapter_event_handler_table (chapter.h), and that handler is
       called with the same unit index after a 200 ms pause and the panel
       coming down.  The flag and the tile are left entirely to it.

   THE ITEM-FOR-ITEM TRADE DOES NOT CONSUME THE CELL.  When the bag is full the
   player is asked again, and on yes the item list opens on the unit's own bag:
   the entry chosen is removed, the cell's item takes its place, and the id of
   the item given away is written back into the cell record's payload word.
   The searched flag is NOT set on this path and the map is NOT repainted, so
   the cell is still searchable and now holds the traded-away item -- flagging
   the cell whenever the player accepts an item, which is the obvious way to
   write it, loses that.

   THE "FOUND IT" LINE IS DRAWN BEFORE THE BAG IS TRIED, so it appears even on
   the run that turns out to have no room and goes on to offer the trade.

   IT IS MODAL THROUGHOUT: two prompts through fdps_prompt_two_choice and, on
   the trade path, the whole item list window (unititem.h).  It leaves the
   message panel closed on every path that opened it, and it repaints nothing
   of the map itself except through
   fdps_map_apply_triggered_cell_changes. */
extern void fdps_battle_search_cell_at_cursor(int unit_index);
#pragma aux fdps_battle_search_cell_at_cursor "*" parm caller [];

/* 00015d00.  Runs one unit's action menu: works out which of the four commands
   it may use this turn, opens the ring on them, and carries out the one the
   player chooses -- looping back and reopening the ring whenever the chosen
   command came back without spending the turn.

   unit_index is the acting unit's index in the battle unit array.  cmd_icons
   and cmd_disabled are the ring's two four-int arrays in slot order up, left,
   right, down (menu.h); the slots here are 0 attack, 1 spell, 2 item and 3 the
   fall-through, which searches the cell under the cursor.  unit_has_moved is
   the caller's answer to "did this unit walk before opening the menu": its
   only effect is that a unit that did NOT move rests when the fall-through
   command runs.

   It answers 1 when a command was carried out and the unit's turn is over, and
   -1 when the player backed out of the ring with nothing done -- except that a
   hand-over inside the item command turns a later back-out into 1 as well, see
   below.

   cmd_disabled IS AN IN AND OUT ARRAY AND ONLY SLOT 0 IS REBUILT.  Every pass
   of the loop writes cmd_disabled[0] = 0 and then probes the attack command
   afresh, while slots 1, 2 and 3 are only ever SET to 1 and never cleared.  So
   the array arrives holding whatever the caller put in it -- fdps_battle_unit_turn
   copies a template once, before its own loop -- and a command greyed out on
   one pass stays greyed for every later pass and for every later call of this
   function in the same turn.  Writing the obvious "clear all four at the top of
   the pass" puts back commands the original leaves greyed.

   WHICH PROBE GREYS WHICH SLOT.  Attack is greyed when the unit has no weapon
   equipped, or when nothing its weapon can reach is standing in range; item is
   greyed when the bag is empty; spell is greyed when the unit knows no spell,
   and again, by a second and independent test, when its 封魔咒術 timer
   status_timers[5] is running (unitstat.h).  The fall-through slot is never
   probed at all: nothing here can grey it out.

   THE FALL-THROUGH ARM IS THE ELSE OF THREE EQUALITY TESTS, NOT A TEST FOR
   SLOT 3.  Any cursor value that is not 0, 1 or 2 runs it, and -1 is one of
   them: a call in which all four entries are greyed opens the ring on -1
   (menu.h leaves the cursor wherever it starts) and a confirm there rests,
   searches and ends the turn.

   HOW A COMMAND ENDS THE CALL.  Attack ends it once a target has been
   confirmed; spell and item end it on any answer but -1; the fall-through arm
   always ends it.  Every other answer falls out of the bottom of the pass and
   the ring is opened again, with the probes re-run -- which is why cancelling
   the attack's target cursor puts the player back on the menu rather than
   ending the turn.  The one thing carried across those passes is the item
   command's 2, a hand-over: it makes the eventual back-out answer 1 instead of
   -1, so the unit's turn is spent even though the ring was cancelled
   (item.h).

   THE CURSOR IS PUT BACK WHERE IT STARTED ONLY ON A CANCELLED ATTACK.  The
   attack arm saves data_fdps_map_cursor_world_x / _y before it hands the
   cursor to the player and walks it back on a cancel; no other arm saves or
   restores anything, so a command that moved the cursor and then came back
   leaves it moved.

   IT IS MODAL AND IT DRAWS THROUGHOUT: the ring's own sweeps and cursor loop,
   a whole view repaint after every pass of the ring, and then whichever
   command was chosen.  data_fdps_battle_pending_xp_credit is zeroed at the top
   of every pass and again on the item command's successful arm, and the attack
   arm scales what the exchange banked in it by 15/10 before paying it out. */
extern int fdps_battle_action_menu(int unit_index, int *cmd_icons,
                                   int *cmd_disabled, int unit_has_moved);
#pragma aux fdps_battle_action_menu "*" parm caller [];

#endif
