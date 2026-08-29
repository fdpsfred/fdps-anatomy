/* palcycle.h -- the palette-cycling animations: scene backdrops and the
 * animated UI colours.
 *
 * A cycle animation never redraws anything.  It rewrites VGA DAC entries
 * underneath pixels that were drawn once and never touched again, so the
 * picture moves while the frame buffer stands still.  The UI cycle owns DAC
 * entries 8..15 and the phase it walks them with; the scene cycle is a
 * separate animation and arrives with its own state.
 *
 * This file owns the two UI-cycle globals below.  Their definitions arrive
 * with ticket 23; until then the build links a zero-filled stub for each.
 */
#ifndef PALCYCLE_H
#define PALCYCLE_H

/* 00060014.  Where the sliding window over the three colour ramps currently
   starts: 0..15, stepped down by one every serviced tick and wrapped back to
   15 from -1.  Signed, and it has to be -- the wrap is a compare against -1
   after the decrement, so an unsigned phase would never reach it.  Initialised
   data, not bss: the original starts it at 15. */
extern int data_fdps_ui_palette_cycle_phase;

/* 00063fbc.  The last value of data_fdps_timer_tick_counter this animation was
   serviced on.  It is a latch, not a count: the only thing anyone asks of it
   is whether it still equals the tick counter, which is how the animation
   advances once per tick however many times per tick it is called. */
extern unsigned int data_fdps_ui_palette_last_cycle_tick;

/* Advances the UI palette animation by one frame and paces the caller to the
   display.  Two things happen, in this order and deliberately not merged:

   It first busy-waits for the VGA vertical retrace to begin, on every call,
   whether or not any colour is going to change.  All nine callers are modal
   input loops -- menus, lists, the status and spell windows, the save-slot
   picker -- that call it once per iteration and have no other frame pace, so
   this wait is what stops them spinning at the emulator's full speed.  Hoisting
   the tick test in front of it would return immediately on most calls and take
   that pace away (rebuild_info/pitfalls.md).

   It then rewrites DAC entries 8..15, but only once per timer tick: an
   8-entry window over three 24-byte colour ramps, starting at
   data_fdps_ui_palette_cycle_phase, which then steps down by one so the wave
   marches across the eight entries.  Nothing is read from the caller and
   nothing is returned; the DAC and the two globals above are the whole
   effect. */
extern void fdps_cycle_ui_palette(void);
#pragma aux fdps_cycle_ui_palette "*" parm caller [];

#endif
