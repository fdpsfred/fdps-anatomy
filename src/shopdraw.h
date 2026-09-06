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

/* Draws one party member's entry in the buy-target list: the member's animated
   walk icon on its mound, the member's name, and the four combat stats that
   member would have with the item on offer, each figure coloured against the
   stat the member has now.

   dst is the first pixel of this member's cell in the caller's entry grid and
   pitch is that grid's bytes per row; roster_index is a position in the party
   roster and doubles as the sprite-set number in the .CEL sprite cache; and
   item_id is the ITEM.DAT id of the item the shop is offering.  Nothing is
   range checked and nothing is clipped.

   THE NAME IS HANDED A PITCH OF 0x138 AND NOT THE pitch ARGUMENT.  Where the
   name goes still comes from the argument; what does not is the surface stride
   fdps_draw_text is told to step its own rows by, which is the literal 312.
   The cannot-equip line on the other path is handed the argument.  The only
   caller composes its grid at 312 so the two agree in play, and on any other
   surface a glyph's rows would come apart from the entry around them.

   WHICH FIGURE IS WHICH IS DECIDED BY THE ARTWORK.  Command.cel sprite 0x2c
   carries "EV :" over "HIT:" and 0x2f "AP :" over "DP :", so the left column
   is evade over hit and the right one attack over defence -- which is not the
   order fdps_roster_preview_combat_stats_with_item writes its four ints in
   (attack, defense, hit, evade, roster.h) and not the order of the record's
   own stat fields either.

   IT MOVES data_fdps_number_glyph_color_row AND LEAVES IT AT 0 -- but only on
   the path that draws figures.  The colour before each figure is 2 when the
   previewed value is below the one the member has now, 3 when it is above and
   0 when they are equal; the cannot-equip path draws no figure and does not
   touch the global at all, so a caller that had set it keeps it.

   ONE MEMBER'S ICON IS DRAWN TRANSLUCENT.  From chapter index 0x17 on, a
   member whose record carries character id 1 has the icon blended rather than
   blitted opaque.  There is no upper bound on the chapter test. */
extern void fdps_shop_draw_member_entry(unsigned char *dst, int pitch,
                                        int roster_index, int item_id);
#pragma aux fdps_shop_draw_member_entry "*" parm caller [];

#endif
