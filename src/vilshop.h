/* vilshop.h -- the village's church, bar, weapon shop, secret menu and
 * lottery screens.
 *
 * Each of these is one whole visit to a building the signboard menu opens:
 * the screen loads its own backdrop, publishes its own page, runs a row of
 * command icons over it and leaves when the player backs out of that row.
 * They take nothing and answer nothing; everything they change, they change
 * through the party and dialogue globals in gamedata.h.
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
