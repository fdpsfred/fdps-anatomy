/* palcycle.h -- the palette-cycling animations: scene backdrops and the
 * animated UI colours.
 *
 * A cycle animation never redraws anything.  It rewrites VGA DAC entries
 * underneath pixels that were drawn once and never touched again, so the
 * picture moves while the frame buffer stands still.  The UI cycle owns DAC
 * entries 8..15 and the phase it walks them with; the scene cycle is a
 * separate animation and arrives with its own state.
 *
 * This file owns the four globals below -- two for each cycle.  Their
 * definitions arrive with ticket 23; until then the build links a zero-filled
 * stub for each.
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

/* 00069d24.  Where the sliding window over the scene ramps currently starts,
   counted in ticks rather than in colours: it runs 0 .. colour count * 4 - 1
   and the window sits at a quarter of it, so the picture steps one colour
   every four serviced ticks.  Stepped up by one and taken modulo the arm's
   own period, which makes it the only piece of the animation's state.

   Signed, and the arithmetic on it is signed too -- IDIV for the modulus and
   the SAR-with-carry-fixup idiom for the division by four -- so a phase that
   somehow went negative would divide towards zero rather than towards minus
   infinity.  Nothing in the image can put a negative value here: the image
   ships it holding 0 and every write is a remainder of a positive dividend.

   One phase is shared by all nine chapter arms rather than one per chapter,
   so a chapter change carries the previous chapter's phase into the new arm
   and the new modulus is what brings it back into range. */
extern int data_fdps_scene_palette_cycle_phase;

/* 00069d20.  The last value of data_fdps_timer_tick_counter the scene cycle
   was serviced on -- the same kind of latch as the UI cycle's, and equally
   compared for equality only, so a wrapped tick counter costs it nothing.

   It is latched on every call that gets past the guard, including the calls
   whose chapter index matches no arm and change no colour at all. */
extern unsigned int data_fdps_scene_palette_last_cycle_tick;

/* 0002eab0.  Advances the scene backdrop's colour cycling by one frame.  The
   four callers -- fdps_render_view_frame, fdps_menu_cursor_input_loop,
   fdps_prompt_two_choice and fdps_icon_script_walk_units -- call it once per
   pass of a loop that is showing the map or a scene.

   Which colours move is decided by data_fdps_chapter_current_chapter_id:
   nine of the thirty chapters animate a run of DAC entries at the top of the
   palette, and the rest animate nothing.  The animated run is always inside
   entries 240..254, so nothing the game draws with the lower 240 entries is
   affected.

   It polls the CD music before anything else -- fdps_cd_music_repeat_poll on
   every call, ahead of its own tick guard -- so a loop that calls this is
   keeping the background music looping as well as the picture moving.  Taking
   this out of a loop stops the music restarting at the end of its track.

   Beyond that it works once per timer tick: a second call in the same tick
   returns having changed nothing.  Takes nothing, returns nothing, and
   reports nothing about which of the two it did. */
extern void fdps_cycle_scene_palette(void);
#pragma aux fdps_cycle_scene_palette "*" parm caller [];

#endif
