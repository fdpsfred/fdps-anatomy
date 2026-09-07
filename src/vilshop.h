/* vilshop.h -- the village's church, weapon shop and secret shop screens.
 *
 * Each of these is one whole visit to a building the signboard menu opens:
 * the screen loads its own backdrop, publishes its own page, runs a row of
 * command icons over it and leaves when the player backs out of that row.
 * Every command in those rows opens one of the party's shared counters --
 * promote, buy, sell, hand over, equip, status.  They take nothing and answer
 * nothing; everything they change, they change through the party and dialogue
 * globals in gamedata.h.
 */
#ifndef VILSHOP_H
#define VILSHOP_H

/* 00035aa0.  The church screen the village phase enters when the player picks
   the church: load its backdrop, zoom it in, and run a row of four command
   icons until the player backs out of the row.  It takes nothing, answers
   nothing and its only exit is that cancel.

   IT OWNS THE PAGE AND IT PUBLISHES IT.  A fresh 64,000-byte page is taken
   from the heap, the "Church.cel" member of MISC.VFS is drawn into it, and the
   pointer is stored in data_fdps_village_backdrop_page_ptr for the window and
   status code to find (gamedata.h).  The page is freed on the way out and THE
   GLOBAL IS NOT CLEARED, so it names freed storage from the return until the
   next village screen allocates its own -- which is what the village mode flag
   rather than a null test guards against.

   THE FOUR COMMANDS ARE A LOCAL TABLE OF Command.cel SUB-IMAGE IDS, in the
   order the row draws them left to right, and the answer is an index into that
   table: 0 reprints the screen's line, 1 is the promotion counter, 2 the
   item hand-over and 3 the status browser.  Entry 0 opens no submenu at all --
   it closes and reopens the window frame and draws entry 7 of the loaded
   chapter's text block, which is this screen's own line; the five village
   screens take entries 4 to 8 of that block, one each.

   THE ROW REOPENS WHERE THE PLAYER LEFT IT.  The answer slot is kept across
   passes and handed back to the row every time, so a command that returns puts
   the cursor back on the icon it was invoked from.  It is seeded to 0 only
   because 0 is not the cancel the entry test looks for: the row overwrites it
   before the dispatch is reached, so the seed never selects an arm.

   WHAT IT NEEDS IN PLACE.  MISC.VFS has to hold "Church.cel" -- a container or
   a member that cannot be found ends the process inside fdps_vfs_load_entry
   (vfs.h) -- and the adapter has to be in mode 13h already, because the zoom
   transition, the window frame, the icon row and every message go straight to
   the aperture.  The sheets and tables the frame, the gold readout and the
   messages draw through are the village phase's and are not checked here.

   IT LEAVES THE SCREEN BLANK.  The closing transition runs the zoom the other
   way and clears the aperture (transit.h), so the caller gets a black screen
   and not the picture the screen was showing. */
extern void fdps_run_church_screen(void);
#pragma aux fdps_run_church_screen "*" parm caller [];

/* 00035fd0.  The weapon shop the village phase enters when the player picks it:
   load its backdrop, zoom it in, and run a row of five command icons until the
   player backs out of the row.  It takes nothing, answers nothing and its only
   exit is that cancel.

   IT OWNS THE PAGE AND IT PUBLISHES IT, exactly as the church screen above
   does: a fresh 64,000-byte page is taken from the heap, the "Weapon.cel"
   member of MISC.VFS is drawn into it, and the pointer is stored in
   data_fdps_village_backdrop_page_ptr for the window and status code to find
   (gamedata.h).  The page is freed on the way out and THE GLOBAL IS NOT
   CLEARED, so it names freed storage from the return until the next village
   screen allocates its own.

   THE FIVE COMMANDS ARE A LOCAL TABLE OF Command.cel SUB-IMAGE IDS, in the
   order the row draws them left to right, and the answer is an index into that
   table: 0 reprints the screen's line, 1 is the buy counter, 2 the sell
   counter, 3 the item hand-over and 4 the equip counter.  Entry 0 opens no
   submenu at all -- it closes and reopens the window frame and draws entry 5 of
   the loaded chapter's text block, which is this screen's own line; the five
   village screens take entries 4 to 8 of that block, one each.

   THE BUY COUNTER IS OPENED AGAINST SHOP ROW 1.  fdps_shop_buy_loop takes the
   shop index as its second argument (shop.h) and this screen is shop 1, so the
   stock offered is row 1 of the chapter's SHOP%02d.DAT table and not the item
   screen's row 0.

   THE ROW REOPENS WHERE THE PLAYER LEFT IT.  The answer slot is kept across
   passes and handed back to the row every time, so a command that returns puts
   the cursor back on the icon it was invoked from.  It is seeded to 0 only
   because 0 is not the cancel the entry test looks for: the row overwrites it
   before the dispatch is reached, so the seed never selects an arm.

   WHAT IT NEEDS IN PLACE.  MISC.VFS has to hold "Weapon.cel" -- a container or
   a member that cannot be found ends the process inside fdps_vfs_load_entry
   (vfs.h) -- and the adapter has to be in mode 13h already, because the zoom
   transition, the window frame, the icon row and every message go straight to
   the aperture.  The sheets and tables the frame, the gold readout, the shop
   stock and the messages draw through are the village phase's and are not
   checked here.

   IT LEAVES THE SCREEN BLANK.  The closing transition runs the zoom the other
   way and clears the aperture (transit.h), so the caller gets a black screen
   and not the picture the screen was showing. */
extern void fdps_run_weapon_shop(void);
#pragma aux fdps_run_weapon_shop "*" parm caller [];

/* 00036210.  The secret shop the village phase enters when the player picks
   it: load its backdrop, zoom it in, and run a row of six command icons until
   the player backs out of the row.  It takes nothing, answers nothing and its
   only exit is that cancel.

   IT OWNS THE PAGE AND IT PUBLISHES IT, exactly as the two screens above do: a
   fresh 64,000-byte page is taken from the heap, the "Secret.cel" member of
   MISC.VFS is drawn into it, and the pointer is stored in
   data_fdps_village_backdrop_page_ptr for the window and status code to find
   (gamedata.h).  The page is freed on the way out and THE GLOBAL IS NOT
   CLEARED, so it names freed storage from the return until the next village
   screen allocates its own.

   THE SIX COMMANDS ARE A LOCAL TABLE OF Command.cel SUB-IMAGE IDS, in the
   order the row draws them left to right, and the answer is an index into that
   table: 0 reprints the screen's line, 1 is the buy counter, 2 the sell
   counter, 3 the item hand-over, 4 the equip counter and 5 the status browser.
   It is the weapon shop's five commands with the status browser added on the
   end, so this is the widest of the village rows.  Entry 0 opens no submenu at
   all -- it closes and reopens the window frame and draws entry 8 of the loaded
   chapter's text block, which is this screen's own line; the five village
   screens take entries 4 to 8 of that block, one each.

   THE BUY COUNTER IS OPENED AGAINST SHOP ROW 2.  fdps_shop_buy_loop takes the
   shop index as its second argument (shop.h) and this screen is shop 2, so the
   stock offered is row 2 of the chapter's SHOP%02d.DAT table -- neither the
   item screen's row 0 nor the weapon shop's row 1.

   THE ROW REOPENS WHERE THE PLAYER LEFT IT.  The answer slot is kept across
   passes and handed back to the row every time, so a command that returns puts
   the cursor back on the icon it was invoked from.  It is seeded to 0 only
   because 0 is not the cancel the entry test looks for: the row overwrites it
   before the dispatch is reached, so the seed never selects an arm.

   WHAT IT NEEDS IN PLACE.  MISC.VFS has to hold "Secret.cel" -- a container or
   a member that cannot be found ends the process inside fdps_vfs_load_entry
   (vfs.h) -- and the adapter has to be in mode 13h already, because the zoom
   transition, the window frame, the icon row and every message go straight to
   the aperture.  The sheets and tables the frame, the gold readout, the shop
   stock and the messages draw through are the village phase's and are not
   checked here.

   IT LEAVES THE SCREEN BLANK.  The closing transition runs the zoom the other
   way and clears the aperture (transit.h), so the caller gets a black screen
   and not the picture the screen was showing. */
extern void fdps_run_secret_menu(void);
#pragma aux fdps_run_secret_menu "*" parm caller [];

#endif
