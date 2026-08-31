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

/* Paints the filled run of one combat status gauge, fill_width pixels of it,
   into a gauge frame the caller has already blitted at dest.

   This is the combat panel's gauge and not the status panel's: the art is one
   of the four 125x5 fill strips of the scratch sheet
   data_fdps_gauge_fill_sheet_ptr points at (gamedata.h), strip gauge_index at
   a stride of 0x271, and the blit goes through fdps_blit_transparent_rect so
   palette index 0 lets the frame underneath show through the bar's shaped
   ends.

   ONLY THE FILLED RUN IS DRAWN.  Nothing paints the empty remainder of the
   125-pixel span and nothing erases it, so a gauge that has shortened since
   the last frame only reads correctly because the caller redraws the frame
   cel first.  Its two cousins above and in unit gauge drawing do paint the
   remainder; this one does not.

   AN OVERFULL GAUGE DRAWS NOTHING AT ALL.  fill_width above 125 skips the
   blit entirely rather than being capped at a full bar, and both callers hand
   over an unclamped ceiling that exceeds 125 whenever current exceeds max.
   Adding the natural min(125, fill_width) makes an overfull gauge read full
   where the original leaves the frame empty.  A fill_width below 0 is clamped
   up to 0, and the clamp happens before everything else, so the alignment
   below sees the clamped value.

   gauge_index BELOW 2 ALSO MEANS RIGHT-TO-LEFT.  For those two the run is
   pushed right by (125 - fill_width) and the SOURCE POINTER IS PUSHED BY THE
   SAME AMOUNT, so the bar shows the rightmost fill_width pixels of the strip
   at the right end of the span; 2 and above fill from the left as drawn.
   Right-aligning the destination while still taking the strip's leftmost
   pixels puts the wrong artwork in the mirrored panel's gauges.  The source
   pointer may be walked that way only because the four strips share one
   125-pitch buffer.  fdps_draw_unit_hp_mp_gauges picks the base index 0 or 2
   from the unit record, which is what mirrors the two panel layouts.

   dest points at the top-left pixel of the span's LEFT end in an 8bpp
   surface, before any right-alignment shift, and dest_stride is that
   surface's pitch in bytes.  Neither is clipped or bounded. */
extern void fdps_draw_gauge_fill(unsigned char *dest, int dest_stride,
                                 int gauge_index, int fill_width);
#pragma aux fdps_draw_gauge_fill "*" parm caller [];

/* Draws one combat status gauge filled in proportion to current against max,
   for a caller that holds a stat pair rather than a pixel width.

   This is fdps_draw_gauge_fill's stat-pair front end, exactly as
   fdps_draw_gauge_bar_proportional is fdps_draw_gauge_bar's: dest,
   dest_stride and gauge_index are passed straight through and mean exactly
   what they mean there, including gauge_index below 2 meaning right-to-left.

   The fill is a CEILING over the span's 125 columns, (current * 125 + max - 1)
   / max, so a unit down to its last hit point still shows a one-pixel sliver
   where the truncating current * 125 / max would show nothing.  Only max
   greater than 0 reaches the division: 0 draws an empty gauge rather than
   dividing, and so does a negative max.

   Everything is signed.  A negative current produces a negative width, which
   fdps_draw_gauge_fill's own clamp turns into an empty gauge, and a current
   above max produces a width above 125, which nothing caps -- so an overfull
   gauge draws NOTHING AT ALL, because that is what fdps_draw_gauge_fill does
   with a width past the span.  min(125, width) here would make it read full.

   The art the gauge is drawn from lives only while a combat exchange is on
   screen: fdps_combat_play_attack_exchange allocates the fill sheet on the
   way in and frees it at teardown, so this is not callable outside one.

   The shipped image has no call site of its own for this function -- the same
   scaling and call are inlined twice inside fdps_draw_unit_hp_mp_gauges, from
   the unit record's current/maximum HP and MP pairs. */
extern void fdps_draw_stat_gauge(unsigned char *dest, int dest_stride,
                                 int gauge_index, int max, int current);
#pragma aux fdps_draw_stat_gauge "*" parm caller [];

/* Draws one battle unit's 43x6 gauge bar at dst, filled in proportion to
   cur_value against max_value, for a caller that holds a stat pair rather than
   a pixel width.

   NOTE THE ORDER OF THE PAIR: the maximum is the fourth argument and the
   current value the fifth.

   dst, dst_stride, gfx_index, blit_mode and alpha are passed straight through
   to fdps_draw_unit_gauge and mean exactly what they mean there, including any
   blit_mode other than 0 and 1 being itself the tint colour index.

   The fill is a CEILING over the bar's 41-pixel interior, (cur_value * 41 +
   max_value - 1) / max_value, so a unit down to its last hit point still shows
   a one-pixel sliver where the truncating cur_value * 41 / max_value would
   show an empty bar.  Only a max_value greater than 0 reaches the division: 0
   draws an empty bar rather than dividing, and so does a negative max_value.

   Everything is signed.  A negative cur_value produces a negative width, which
   fdps_draw_unit_gauge's own clamp turns into an empty bar, and a cur_value
   above max_value produces a width above 41, which nothing caps -- see the
   note on that function below for the smear that then draws.

   The shipped image has no call site of its own for this function: the
   compiler inlined it at all eight of them, six in
   fdps_battle_show_combat_gauges and two in fdps_play_attack_animation, and
   every one passes the unit record's current and maximum HP as the pair. */
extern void fdps_draw_unit_gauge_proportional(unsigned char *dst,
                                              int dst_stride, int gfx_index,
                                              int max_value, int cur_value,
                                              int blit_mode, int alpha);
#pragma aux fdps_draw_unit_gauge_proportional "*" parm caller [];

/* Draws one battle unit's 43x6 gauge bar at dst, filled to fill_width pixels,
   in one of three painting modes.

   The art is the three 43x6 graphics of the sheet
   data_fdps_unit_gauge_sheet_ptr points at (gamedata.h): graphic 0 is the
   empty track and graphics 1 and 2 are the two filled colours, one per side of
   the battle.  The bar is painted as four segments, always in this order: the
   two-pixel left cap out of graphic gfx_index, the fill_width-wide run out of
   the same graphic, the remainder of the 41-pixel interior out of GRAPHIC 0,
   and last the two-pixel right cap out of graphic gfx_index again.  The fill
   run is skipped entirely at a fill_width of 0 or below.

   ONLY THE FILLED RUN HONOURS gfx_index.  The remainder is always graphic 0 at
   its own columns, never gfx_index advanced by fill_width; drawing the obvious
   "same graphic, later columns" paints the whole bar in the filled colour.

   THE REMAINDER RUNS UNDER THE RIGHT CAP AND THAT IS LOAD-BEARING.  It is
   41 - fill_width wide starting at column 2, so it reaches column 42 and
   covers the right cap's two columns, which the fourth blit then paints back
   over.  Trimming it to 39 - fill_width, or drawing the caps first, changes
   the picture twice over: wherever the cap's own art is transparent the
   remainder's graphic-0 pixels are what shows through, and in blit_mode 1
   those two columns are blended against a destination that already carries the
   remainder rather than against the surface underneath.

   THE CLAMP IS ONE-SIDED.  fill_width below 0 is forced to 0, but nothing caps
   it at 41: a caller whose current exceeds its maximum blits more than 41
   columns out of a 43-pitch source, which reads on into the next row of the
   art and past the right cap, and the remainder -- a negative width -- draws
   nothing.  Adding the symmetric upper clamp turns that smear into a clean
   full bar, which is not what the original draws.

   blit_mode picks the painter for all four segments and is not an enumeration
   with a default: 0 is fdps_blit_transparent_rect, 1 is
   fdps_blit_blend_transparent_rect with the destination handed over as its own
   background, and ANY OTHER VALUE is fdps_blit_tint_transparent_rect with
   blit_mode ITSELF as the tint colour index.  alpha is the blend strength the
   two blended painters take and is never read when blit_mode is 0.  Both
   blended painters composite through data_fdps_palette_shade_ramp_table and
   data_fdps_inverse_palette_cube (gamedata.h), which this function passes in
   as arguments the way every other caller of that family does.

   dst points at the bar's top-left pixel in an 8bpp surface and dst_stride is
   that surface's pitch in bytes; both are used exactly as given, with no
   clipping and no bound of any kind. */
extern void fdps_draw_unit_gauge(unsigned char *dst, int dst_stride,
                                 int gfx_index, int fill_width, int blit_mode,
                                 int alpha);
#pragma aux fdps_draw_unit_gauge "*" parm caller [];

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
