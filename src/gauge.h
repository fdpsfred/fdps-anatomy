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
 * the caller's own two ints.
 */
#ifndef GAUGE_H
#define GAUGE_H

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
