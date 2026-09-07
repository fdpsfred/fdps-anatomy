/* vilmenu.h -- the village's party-member picker and the item loops built on
 * it.
 *
 * The picker is the 3x2 grid of walking icons that drops across the bottom of
 * every village screen that has to be told WHICH member it is about: sell,
 * transfer, equip, status, and the shop's "who is this for".  It answers a
 * roster index and nothing else; what the caller then does with that member is
 * the caller's business.
 *
 * The item screen at the end of the file is the one whole village screen that
 * lives here: it owns a backdrop page and a row of command icons and dispatches
 * to the loops above it.
 *
 * The cursor and the scroll top are this file's own two globals below.  They
 * are declared here rather than in gamedata.h because no other translation
 * unit reads them.
 */
#ifndef VILMENU_H
#define VILMENU_H

/* 000601bc.  Which roster entry the picker's highlight is standing on, indexed
   over the party roster exactly as data_fdps_roster_member_count counts it --
   this is a roster index and not a position within the six visible cells.

   IT SURVIVES BETWEEN VISITS AND THAT IS THE POINT.  fdps_village_select_member
   is the only code in the image that touches it and it does not seed it on
   entry, not even to clamp it against the current member count, so the grid
   reopens on the member picked last time -- including when that pick was made
   from a different village screen.  Making it a local seeded to 0 would open
   the grid on the party leader every time. */
extern int data_fdps_village_member_select_cursor_idx;

/* 000601b8.  The roster entry drawn in the grid's top left cell: the window
   over the roster, six entries at a time in three columns of two.

   It moves in steps of three, one row, and only when the cursor leaves the six
   it shows -- forward when the cursor reaches this + 6, back when it drops
   below this.  Like the cursor above it is never seeded and never clamped on
   its own, so a party that has shrunk since the last visit can be opened with
   the window past the end of it and the grid then paints bare cells. */
extern int data_fdps_village_member_grid_scroll_offset;

/* Runs the village party-member grid to a decision and answers the roster
   index of the member the player confirmed, 0 to data_fdps_roster_member_count
   - 1, or -1 when the player backed out.

   IT IS MODAL AND IT PACES ITSELF.  The call does not return until a confirm
   or a cancel is read: each pass polls the CD music, takes one scancode
   through the auto-repeat filter (keybd.h), rebuilds the whole window into a
   fresh page, presents it on the retrace and ends by waiting for the timer
   tick to advance.  Escape and Delete both cancel; Enter and Space both
   confirm.

   FROM CHAPTER INDEX 0x17 ON, ROSTER SLOT 3 CANNOT BE CONFIRMED.  That slot is
   法蓮娜, who has left the party by then.  The refusal is silent and it is
   only a refusal: the cursor still stops on the entry, the entry is still
   drawn, and the confirm simply does nothing for that pass.  The same member
   is also drawn ghosted from that chapter on, but by a separate test on the
   record's character id rather than on the slot -- the two conditions are not
   one condition (rebuild_info/pitfalls.md).

   Reads data_fdps_roster_member_count for every bound, so a caller that has
   changed the party size does not have to tell the picker anything. */
extern int fdps_village_select_member(void);
#pragma aux fdps_village_select_member "*" parm caller [];

/* 00033f80.  The status browser the item, church and secret screens all offer:
   open the member picker, show the member the player confirms, and start over
   until the player cancels out of the picker.  It answers nothing, and the
   only way it ever returns is that cancel.

   screen_page is the caller's own 320x200 8bpp page holding that screen's
   backdrop.  It is only read.  The window animation composes every frame on a
   private page, and this loop copies the backdrop straight onto the adapter
   before each status window -- because the status window snapshots the live
   screen and restores that snapshot when it closes (statwin.h), so what is on
   the adapter at that moment is what the player is left with afterwards.

   IT SHOWS NOTHING FOR PORTRAIT IDS 0x24..0x27.
   fdps_battle_show_unit_status_window returns without drawing for those, and
   this loop neither tests for it nor is told, so such a pick simply reopens
   the picker.

   IT NEEDS THE VILLAGE'S UNIT-ARRAY ALIAS IN PLACE.  The picker's answer is a
   roster index and the status window resolves it against
   data_fdps_map_unit_array_ptr, which fdps_load_field_chapter_resources has
   pointed at the roster base for the duration of the phase (gamedata.h).
   Called with that alias undone, the window shows a different member. */
extern void fdps_village_member_status_loop(unsigned char *screen_page);
#pragma aux fdps_village_member_status_loop "*" parm caller [];

/* 00034000.  The sell counter the item screen, the weapon shop and the secret
   screen all offer: open the member picker, run the chosen member's bag, buy
   one entry back at three quarters of its listed price, and start over until
   the player cancels out of the picker.  It answers nothing, and the only way
   it ever returns is that cancel -- a completed sale sends the player to the
   member picker again rather than back into the same member's bag.

   screen_page is the caller's own 320x200 8bpp page holding that screen's
   backdrop.  It is only read: the window animation composes every frame on a
   private page, and this loop copies the backdrop straight onto the adapter
   before it opens the inventory list.

   A MEMBER CARRYING NOTHING IS REFUSED AND THE REFUSAL IS ALL THAT HAPPENS.
   The member can still be confirmed in the picker; what follows is message
   0x1fb with the member's name substituted into it, and then the picker again.

   THREE QUARTERS, TRUNCATED, OF THE LISTED PRICE.  The offer is the ITEM.DAT
   price times three divided by four in int arithmetic, so a price that is not
   a multiple of four loses the remainder -- 101 is offered at 75.  It is left
   in data_fdps_dialog_last_action_value_param for the confirmation message to
   print and is read back FROM THERE when the sale settles, so the purse and
   the printed figure cannot disagree.

   ONLY THE LEFT CELL OF THE PROMPT SELLS.  A cancel and the right cell both
   leave the entry where it is; the offer already published in the dialogue
   globals stays there either way, because it is written before the prompt runs
   and nothing puts it back.

   IT NEEDS THE VILLAGE'S UNIT-ARRAY ALIAS IN PLACE, exactly as the status
   browser above does: the picker's answer is a roster index and every
   fdps_unit_* accessor here resolves it against data_fdps_map_unit_array_ptr
   (gamedata.h). */
extern void fdps_village_item_sell_loop(unsigned char *screen_page);
#pragma aux fdps_village_item_sell_loop "*" parm caller [];

/* 00034210.  The item hand-over the item screen, the church, the weapon shop
   and the secret screen all offer: open the member picker for a giver, run
   that member's bag, open the picker again for a receiver, move the chosen
   item across, and start over until the player cancels out of the picker.  It
   answers nothing, and the only way it ever returns is that cancel -- a
   completed hand-over sends the player back to the giver picker rather than
   into the same bag again.  The window is open when it returns, because the
   cancel comes out of a picker that has just drawn its grid into it.

   screen_page is the caller's own 320x200 8bpp page holding that screen's
   backdrop.  It is only read: the window animation composes every frame on a
   private page, and this loop copies the backdrop straight onto the adapter
   before it opens the inventory list.

   A GIVER CARRYING NOTHING IS REFUSED AND THE REFUSAL IS ALL THAT HAPPENS.
   The member can still be confirmed in the picker; what follows is message
   0x1fb with the member's name substituted into it, and then the picker again.

   THE GIVER'S NAME IS PUBLISHED ON EVERY CONFIRMED PICK, not only on the
   refusal: data_fdps_dialog_last_action_text_id_param takes the giver's
   character id plus one before the bag is counted, and it is still that name
   the "who is it for" message expands two calls later.  The item's own name
   goes into data_fdps_dialog_subst_text_id_2 as its id plus 0xc9 once the list
   has been answered, and neither slot is put back afterwards.

   A FULL RECEIVER IS REFUSED ON AN EXACT EIGHT.  The bag-full message 0x1fa is
   shown and nothing moves; the giver keeps the item and no stats are reworked.

   THE ITEM CROSSES AS A BARE ID AND SO ARRIVES UNEQUIPPED.  What the giver had
   equipped the receiver has merely got, because the addition zeroes the
   receiving entry's flag byte -- and only the giver's derived stats are
   recomputed, so an entry that carried the equipped bit across would give the
   receiver its modifiers for nothing.

   IT NEEDS THE VILLAGE'S UNIT-ARRAY ALIAS IN PLACE, exactly as the two loops
   above do: both pickers answer roster indices and every fdps_unit_* accessor
   here resolves them against data_fdps_map_unit_array_ptr (gamedata.h). */
extern void fdps_village_item_transfer_loop(unsigned char *screen_page);
#pragma aux fdps_village_item_transfer_loop "*" parm caller [];

/* 00034420.  The equip counter the weapon shop and the secret screen offer:
   open the member picker, run the chosen member's equip screen, and start over
   until the player cancels out of the picker.  It answers nothing, and the
   only way it ever returns is that cancel -- leaving the picker's window OPEN,
   because the cancel comes out of a picker that has just drawn its grid into
   it.

   screen_page is the caller's own 320x200 8bpp page holding that screen's
   backdrop.  It is only read: the window animation composes every frame on a
   private page, and this loop copies the backdrop straight onto the adapter
   before it opens the equip screen.

   A MEMBER CARRYING NOTHING IS REFUSED AND THE REFUSAL IS ALL THAT HAPPENS.
   The member can still be confirmed in the picker; what follows is message
   0x1fb with the member's name substituted into it, and then the picker again.
   The equip screen has an empty-bag exit of its own (unititem.h) and this
   refusal is why it is never reached from here.

   THE NAME IS PUBLISHED ONLY ON THE REFUSAL.  Unlike the hand-over above,
   which writes data_fdps_dialog_last_action_text_id_param on every confirmed
   pick, this loop writes it inside the empty-bag arm alone -- so a member who
   is carrying something reaches the equip screen with whatever the previous
   message left in that slot.

   THE WINDOW IS REOPENED RATHER THAN CLOSED BEFORE THE REFUSAL.  The open
   animation rebuilds each step from screen_page, so replaying it on a window
   that is already open is what clears the picker's grid off the frame the
   message is then written into; the other two loops reach the same clean
   window by closing first and opening after (rebuild_info/pitfalls.md).

   IT NEEDS THE VILLAGE'S UNIT-ARRAY ALIAS IN PLACE, exactly as the three loops
   above do: the picker answers a roster index and the bag count, the record
   lookup and the equip screen all resolve it against
   data_fdps_map_unit_array_ptr (gamedata.h). */
extern void fdps_village_member_equip_loop(unsigned char *screen_page);
#pragma aux fdps_village_member_equip_loop "*" parm caller [];

/* 00035860.  The item screen the village phase enters when the player picks the
   item shop: load its backdrop, zoom it in, and run a row of five command icons
   until the player backs out of the row.  It takes nothing, answers nothing and
   its only exit is that cancel.

   IT OWNS THE PAGE AND IT PUBLISHES IT.  A fresh 64,000-byte page is taken from
   the heap, the "Item.cel" member of MISC.VFS is drawn into it, and the pointer
   is stored in data_fdps_village_backdrop_page_ptr for the window and status
   code to find (gamedata.h).  The page is freed on the way out and THE GLOBAL IS
   NOT CLEARED, so it names freed storage from the return until the next village
   screen allocates its own -- which is what the mode flag rather than a null
   test guards against.

   THE FIVE COMMANDS ARE A LOCAL TABLE OF Command.cel SUB-IMAGE IDS, in the order
   the row draws them left to right, and the answer is an index into that table:
   0 reprints the screen's line, 1 is the shop counter, 2 the sell counter, 3 the
   hand-over and 4 the status browser.  Entry 0 opens no submenu at all -- it
   closes and reopens the window frame and draws entry 4 of the loaded chapter's
   text block, which is this screen's own line; the five village screens take
   entries 4 to 8 of that block, one each.

   THE ROW REOPENS WHERE THE PLAYER LEFT IT.  The answer slot is kept across
   passes and handed back to the row every time, so a command that returns puts
   the cursor back on the icon it was invoked from.  It is seeded to 0 only
   because 0 is not the cancel the entry test looks for: the row overwrites it
   before the dispatch is reached, so the seed never selects an arm.

   WHAT IT NEEDS IN PLACE.  MISC.VFS has to hold "Item.cel" -- a container or a
   member that cannot be found ends the process inside fdps_vfs_load_entry
   (vfs.h) -- and the adapter has to be in mode 13h already, because the zoom
   transition, the window frame, the icon row and every message go straight to
   the aperture.  The sheets and tables the frame, the gold readout and the
   messages draw through are the village phase's and are not checked here.

   IT LEAVES THE SCREEN BLANK.  The closing transition runs the zoom the other
   way and clears the aperture (transit.h), so the caller gets a black screen and
   not the picture the screen was showing. */
extern void fdps_village_item_menu(void);
#pragma aux fdps_village_item_menu "*" parm caller [];

#endif
