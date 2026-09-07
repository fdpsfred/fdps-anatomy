/* shop.h -- the village shop's stock, item picker and buying flow.
 *
 * The three shops -- item, weapon and secret -- are one body of code told
 * apart by a shop_index of 0, 1 or 2, which selects a row of the chapter's
 * SHOP%02d.DAT table (data_fdps_shop_stock_table_ptr, gamedata.h).  Nothing
 * here owns the stock: it is read out of that table on every entry, so a
 * chapter change reaches the shops with no other bookkeeping.
 */
#ifndef SHOP_H
#define SHOP_H

/* Collects the item ids shop shop_index is stocking this chapter into
   out_item_ids and returns how many were written, 0 to 12.

   All twelve bytes of the row are walked.  0xff is an empty slot that is
   skipped wherever it sits, including between two stocked entries, so the scan
   never stops early and the written ids are packed down against the front of
   out_item_ids with the holes removed -- the destination index is the write
   counter, not the slot index.

   shop_index is not range checked and neither is out_item_ids, which must have
   room for twelve ints because a full row writes twelve. */
extern int fdps_shop_collect_stock_items(int shop_index, int *out_item_ids);
#pragma aux fdps_shop_collect_stock_items "*" parm caller [];

/* 000601ac.  Which of the shop's stocked entries the picker's selection bar is
   standing on, counted over the packed list fdps_shop_collect_stock_items
   builds and not over the twelve slots of the stock row.

   IT SURVIVES BETWEEN VISITS AND THAT IS THE POINT.  fdps_shop_select_item is
   the only code in the image that touches it, and it never seeds it on entry:
   the picker reopens on the entry the player last stood on, in the same shop or
   in a different one.  The one thing that clears it is the entry test against
   the stock count -- a saved cursor at or past the end of THIS shop's stock
   puts both this and the window top back to 0, so a smaller shop cannot be
   entered with the bar off the end of its list. */
extern int data_fdps_shop_item_picker_cursor_idx;

/* 000601a8.  The entry drawn in the picker's top left cell: the window over the
   stock list, six entries at a time in two columns of three.

   It moves in steps of two, one row, and only when the cursor leaves the six it
   shows -- forward when the cursor reaches this + 6, back when it drops below
   this.  Like the cursor above it survives between visits and is reset only by
   the cursor's own entry test, so it is NOT independently clamped: a saved top
   that is past the end of a smaller shop's stock stays where it is as long as
   the saved cursor is still inside that stock, and the picker then draws a page
   with no entries on it. */
extern int data_fdps_shop_item_list_scroll_offset;

/* Runs the shop's item picker to a decision and answers the id of the entry the
   player confirmed, in the Item.dat numbering fdps_get_item_record takes, or -1
   when the player backed out with Escape.

   shop_index is 0 the item shop, 1 the weapon shop and 2 the secret shop, and
   goes straight to fdps_shop_collect_stock_items; the picker offers whatever
   that answers and nothing else.

   IT IS MODAL AND IT PACES ITSELF.  The call does not return until Escape,
   Enter or Space is read: each pass polls the CD music, takes one scancode
   through the auto-repeat filter (keybd.h), rebuilds the whole window into a
   fresh page and ends by waiting for the timer tick to advance.  A caller gets
   one number back and no partial state.

   The cursor and the window top it leaves behind are the two globals above, so
   the answer for a given key script depends on where the last visit finished.

   AN EMPTY SHOP WOULD STILL RUN.  A stock count of 0 resets both globals and
   then draws six empty cells; Enter on it answers whatever the uninitialised
   first slot of the picker's own stack array holds.  Neither the picker nor its
   one caller, fdps_shop_buy_loop at 00033b80, tests for it.  What keeps it from
   happening is the shipped data: no row of any of the thirty SHOP%02d.DAT
   images is entirely 0xff, so every shop the game can open stocks something. */
extern int fdps_shop_select_item(int shop_index);
#pragma aux fdps_shop_select_item "*" parm caller [];

/* 000601b4.  Which roster member the buy-target picker's selection bar is
   standing on, indexed over the party roster exactly as
   data_fdps_roster_member_count counts it.

   IT SURVIVES BETWEEN VISITS AND THAT IS THE POINT.
   fdps_shop_select_buy_target is the only code in the image that touches it and
   it neither seeds nor clamps it on entry, so the picker reopens on the member
   bought for last time -- and unlike the item picker above there is no entry
   test at all, so a party that has shrunk since can be opened with the bar off
   the end of it. */
extern int data_fdps_shop_buy_target_cursor_idx;

/* 000601b0.  The roster index drawn in the buy-target list's leftmost visible
   cell: the window over the roster, one row of three entries at a time.

   It moves in steps of three, one row, and only when the cursor leaves the
   three it shows -- forward when the cursor reaches this + 3, back when it
   drops below this -- so it is always the cursor rounded down to a multiple of
   three.  Like the cursor above it survives between visits and is never
   independently clamped. */
extern int data_fdps_shop_buy_target_scroll_offset;

/* Runs the shop's buy-target picker to a decision and answers the roster index
   of the member the player confirmed, 0 to data_fdps_roster_member_count - 1,
   or -1 when the player backed out.

   item_id is the ITEM.DAT id of the item the shop is offering, and it decides
   which of two pickers the player actually gets.  An item whose type byte is
   0x01 to 0x27 -- equipment -- gets this file's own picker, a row of three
   entries showing what each member's four combat stats would become with the
   item on.  Anything else, type 0 and the consumables and plot items above
   0x27, has no stats to preview and is handed whole to
   fdps_village_select_member (vilmenu.h), whose answer is returned unchanged
   and means the same thing.

   IT IS MODAL AND IT PACES ITSELF.  On the equipment path the call does not
   return until a confirm or a cancel is read: each pass polls the CD music,
   takes one scancode through the auto-repeat filter (keybd.h), redraws every
   member's entry into a fresh 312 x 335 grid, presents one row of it and ends
   by waiting for the timer tick to advance.  Escape and Delete both cancel;
   Enter and Space both confirm.

   FROM CHAPTER INDEX 0x17 ON, ROSTER SLOT 3 CANNOT BE CONFIRMED, silently: the
   cursor still stops on the entry and the entry is still drawn, and the confirm
   simply does nothing for that pass.  The same slot is locked out of
   fdps_village_select_member from the same chapter, so a non-equipment item is
   no way around it.

   The two globals above are what it leaves behind, and where they start is
   where it opens. */
extern int fdps_shop_select_buy_target(int item_id);
#pragma aux fdps_shop_select_buy_target "*" parm caller [];

#endif
