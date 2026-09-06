/* shopdraw.h -- the drawing half of the village shop screen.
 *
 * shop.c owns the shop's stock and its buying flow; this file owns what the
 * shop puts on the screen: one cell of the six-cell stock list, the party row
 * the buyer is picked from, and the frame the chosen item is previewed in.
 * Nothing here is modal and nothing here reads the keyboard.
 *
 * Most of the file paints into a surface the caller hands it and returns; a
 * destination is then an 8bpp surface addressed by a byte pointer and a pitch
 * in bytes, the same pair the rest of the drawing code takes (text.h), and the
 * only caller composes the shop screen on a 312-wide offscreen page, so the
 * pitch that reaches here in play is 0x138.
 *
 * fdps_shop_render_buy_target_frame is the exception: it owns the whole frame
 * rather than a cell of one, so it allocates its own page, composes the window
 * on it and copies the result to the adapter itself.  It still reads no key
 * and makes no decision -- the caller's picker loop does that and calls here
 * once per pass.
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

/* Draws and presents one whole frame of the shop's buy-target picker: the
   window, the selection bar behind the chosen column, one 312 x 67 strip of
   the caller's pre-rendered entry grid, and the two blinking scroll arrows.
   The frame is composed on a 312 x 76 page this routine allocates and frees,
   and the visible 304 x 67 part of it is copied straight to the mode 13h
   adapter at (8, 125).

   list_bitmap is the caller's grid of already-drawn entries at pitch 0x138,
   three entries to a row of 0x43 pixels, and src_row is the pixel row of it
   the visible strip starts at -- the caller steps that in eighths of 0x43 to
   slide the list.  cursor_x is the pixel column the selection bar goes at
   inside the page.  scroll_top is the index of the first visible entry and
   decides which of the two arrows are drawn and nothing else.

   IT IS ALSO A FRAME OF PACING.  fdps_cycle_ui_palette waits for the retrace
   to begin (palcycle.h) and this routine then straddles a whole retrace of its
   own before the screen copy, so a caller that loops on it is paced by the
   display whether or not it waits on anything else.

   NOTHING IS CLIPPED OR CHECKED.  The malloc is not tested, the strip is read
   for 0x43 rows from wherever src_row points, and the arrows are placed by
   fixed page offsets; the caller is what keeps src_row inside its own grid. */
extern void fdps_shop_render_buy_target_frame(unsigned char *list_bitmap,
                                              int cursor_x, int src_row,
                                              int scroll_top);
#pragma aux fdps_shop_render_buy_target_frame "*" parm caller [];

#endif
