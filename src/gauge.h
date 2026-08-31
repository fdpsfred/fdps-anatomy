/* gauge.h -- the game's gauge bars and where a combat gauge is placed.
 *
 * A gauge here is the small filled bar the game draws for a unit's HP and MP:
 * the drawing routines put one at a destination the caller worked out, and the
 * placement routine below is what works one out for a unit that is in the
 * middle of a combat animation.
 *
 * Nothing in gauge.c owns state.  The placement routine reads the unit record
 * through fdps_get_unit_record (unit.h) and the view scroll position through
 * the two window-origin globals gamedata.h declares, and writes nothing but
 * the caller's own two ints; the drawing routines read the gauge art through
 * the sheet pointers gamedata.h declares and write only into the destination
 * surface the caller hands them.
 */
#ifndef GAUGE_H
#define GAUGE_H

/* Draws one 117x8 status gauge bar at dst, filled in proportion to current
   against max, for a caller that holds a stat pair rather than a pixel width.

   The fill is a CEILING over the bar's 117 columns, (current * 117 + max - 1)
   / max, so a unit down to its last hit point still shows a one-pixel sliver
   where the truncating current * 117 / max would show an empty bar.  Only max
   greater than 0 reaches the division: 0 draws an empty bar rather than
   dividing, and so does a negative max.

   Everything is signed.  A negative current produces a negative width, which
   fdps_draw_gauge_bar's own clamp turns into an empty bar, and a current above
   max produces a width above 117, which nothing caps -- see the note on that
   function below for what an overfull bar then draws.

   dst, dst_stride and bar_index are passed straight through to
   fdps_draw_gauge_bar and mean exactly what they mean there.  In the shipped
   game the callers are the HP and MP bars of the unit status panel, with
   bar_index 1 and 2 and a stride of 320. */
extern void fdps_draw_gauge_bar_proportional(unsigned char *dst,
                                             int dst_stride, int bar_index,
                                             int max, int current);
#pragma aux fdps_draw_gauge_bar_proportional "*" parm caller [];

/* Draws one 117x8 status gauge bar, filled to fill_width pixels, at dst.

   The art is the three 117x8 graphics of the sheet
   data_fdps_status_gauge_bar_sheet_ptr points at (gamedata.h): graphic 0 is
   the empty track and graphics 1 and 2 are the filled colours, HP and MP.
   The filled part is drawn first, fill_width columns wide taken from graphic
   bar_index, and the remainder is then drawn from graphic 0 -- from the same
   column onwards, so the empty track's own pixels line up with where they
   would have been.  Both halves go through fdps_blit_transparent_rect, so
   palette index 0 in the art leaves the destination pixel underneath alone
   and the bar's rounded ends do not carry a background with them.

   ONLY THE FILLED HALF HONOURS bar_index.  The remainder is always graphic 0,
   never bar_index advanced by fill_width columns; drawing the obvious "same
   graphic, later columns" paints the whole bar in the filled colour.

   THE CLAMP IS ONE-SIDED.  fill_width below 0 is forced to 0 and an empty bar
   is drawn, but nothing caps it at 117: a caller whose current exceeds its
   maximum blits more than 117 columns out of a 117-pitch source, which reads
   on into the next row of the art, and no remainder is drawn at all.  Adding
   the symmetric upper clamp turns that smear into a clean full bar, which is
   not what the original draws.

   dst points at the bar's top-left pixel in an 8bpp surface and dst_stride is
   that surface's pitch in bytes; both are used exactly as given, with no
   clipping and no bound of any kind. */
extern void fdps_draw_gauge_bar(unsigned char *dst, int dst_stride,
                                int bar_index, int fill_width);
#pragma aux fdps_draw_gauge_bar "*" parm caller [];

/* Works out where one battle unit's HP gauge goes on screen while a combat
   animation is playing, and writes the position through out_position.

   The unit's tile is converted to view pixels exactly as the unit's own
   sprite is -- tile * 0x18 minus the view window origin, plus a 4-pixel
   shift on x -- and the gauge is then stepped off the unit to the side
   OPPOSITE the way it faces, so two combatants facing each other get their
   gauges on the two outer sides rather than stacked on top of each other
   between them.  Facings 0 (down) and 1 (left) place the gauge above and to
   the right; facings 2 (up) and 3 (right) place it below and to the left.

   Each of the four steps has its own fallback for when the preferred one
   would leave the 320x200 screen, and the fallbacks are not the mirror of the
   steps: the two vertical ones are both a small downward nudge, so a gauge
   that cannot take its preferred vertical offset ends up beside or over the
   unit rather than on the other side of it, and only x ever flips sides.

   out_position receives x then y, in view pixels and BEFORE the caller adds
   the composite page's 0x18 border -- callers turn the pair into a
   destination as buffer + (y + 0x18) * 0x168 + x + 0x18.  Both elements are
   written unconditionally, so the caller need not initialise them.

   unit_index is a position in the current battle's unit array and is not
   range checked, and neither is the resulting position: a unit scrolled off
   the visible map still gets a position computed for it, and only the four
   edge fallbacks constrain the answer at all. */
extern void fdps_battle_compute_unit_gauge_position(int *out_position,
                                                    int unit_index);
#pragma aux fdps_battle_compute_unit_gauge_position "*" parm caller [];

#endif
