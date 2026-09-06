/* shopdraw.h -- the drawing half of the village shop screen.
 *
 * shop.c owns the shop's stock and its buying flow; this file owns what the
 * shop puts on the screen: one cell of the six-cell stock list, the party row
 * the buyer is picked from, and the frame the chosen item is previewed in.
 * Nothing here is modal and nothing here reads the keyboard -- every routine
 * paints into a surface the caller hands it and returns.
 *
 * A destination is an 8bpp surface addressed by a byte pointer and a pitch in
 * bytes, the same pair the rest of the drawing code takes (text.h); the only
 * caller composes the shop screen on a 312-wide offscreen page, so the pitch
 * that reaches here in play is 0x138.
 */
#ifndef SHOPDRAW_H
#define SHOPDRAW_H

/* Draws one item's entry in the shop's stock list: the item's name on the
   first line, the price beside its money mark, and on the second line the one
   headline figure that matters for the item's kind -- attack power for a
   weapon, defence power for a piece of armour, the recovery amount for a
   healing item, and for everything else a "????" caption with no figure at
   all.

   item_id indexes the ITEM.DAT table and is also the item name's message id
   once 0xc9 is added; it is not range checked, and the record accessor it goes
   through does not check it either (table.h).  dest points at the top-left
   corner of the entry's cell and pitch is the surface's bytes per row.

   THE CELL THE ENTRY IS DRAWN IN EXTENDS ONE ROW ABOVE dest: the money mark is
   blitted at dest + 0x54 - pitch, so a caller that puts an entry on the very
   first row of a surface has the caption's top row land off the front of it.
   There is no clipping anywhere in the path. */
extern void fdps_shop_draw_item_entry(int item_id, unsigned char *dest,
                                      int pitch);
#pragma aux fdps_shop_draw_item_entry "*" parm caller [];

#endif
