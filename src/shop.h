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

#endif
