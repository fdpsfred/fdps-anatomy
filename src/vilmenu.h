/* vilmenu.h -- the village's party-member picker and the item loops built on
 * it.
 *
 * The picker is the 3x2 grid of walking icons that drops across the bottom of
 * every village screen that has to be told WHICH member it is about: sell,
 * transfer, equip, status, and the shop's "who is this for".  It answers a
 * roster index and nothing else; what the caller then does with that member is
 * the caller's business.
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

#endif
