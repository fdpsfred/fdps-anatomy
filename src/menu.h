/* menu.h -- the four-way command menus the battle screens put a cursor on.
 *
 * A menu is described by four ints, one per entry, in the order the entries sit
 * on screen: 0 up, 1 left, 2 right, 3 down.  Zero means the entry is
 * selectable, non-zero means it is greyed out.  The descriptor is supplied by
 * the caller -- fdps_battle_item_menu copies a static one and patches it,
 * fdps_battle_action_menu fills its own from the per-command legality probes.
 */
#ifndef MENU_H
#define MENU_H

/* Returns the index 0..3 of the first selectable entry, or -1 when all four
   are greyed out.  Only reads the descriptor. */
extern int fdps_menu_find_first_enabled_entry(int *cmd_disabled);
#pragma aux fdps_menu_find_first_enabled_entry "*" parm caller [];

/* Composes and presents one whole frame of the ring menu: the battle view with
   the four command buttons standing radius pixels out from the map cursor, the
   one at cursor_dir on the highlighted plate.  Returns nothing and writes none
   of its arguments back.

   cmd_icons and cmd_disabled are two four-int arrays in slot order up, left,
   right, down.  cmd_icons[slot] is the Command.cel sub-image of that slot's
   icon; cmd_disabled[slot] is 0 when the entry may be chosen and 1 when it is
   greyed out, and the flag both bumps the plate sub-image by one and moves the
   icon a whole bank of 0x24 sub-images along the sheet.

   radius is the ring's pixel radius AND, times 2 * 3.14159 / 96, the angle of
   the sweep, so one value drives the spiral the open and close animations run:
   the slot offsets are cos and sin of that angle scaled by radius.  0x18 is the
   resting ring.

   cursor_dir picks the highlighted slot, 0 up to 3 down.  A value outside 0..3
   simply matches no slot and leaves all four on the plain plate.

   IT DRAWS STRAIGHT TO THE ADAPTER.  The frame goes down on a page this
   function allocates and frees itself, and the last thing it does before
   freeing is blit that page's 312x192 window over the live mode 13h screen.
   There is no offscreen page to hand back and nothing to present afterwards. */
extern void fdps_render_ring_menu_frame(int *cmd_icons, int *cmd_disabled,
                                        int radius, int cursor_dir);
#pragma aux fdps_render_ring_menu_frame "*" parm caller [];

/* Plays the ring menu's opening sweep: the window cue once, then eight frames
   of fdps_render_ring_menu_frame at radii 1, 5, 9, 0xd, 0x11, 0x15, 0x18 and
   0x19.  Returns nothing, and hands cmd_icons, cmd_disabled and cursor_dir to
   every frame exactly as they came in -- see the notes above for what each of
   the three means and for the fact that radius drives the angle as well as the
   distance, which is what makes the sweep a spiral rather than a growing ring.

   THE SWEEP ENDS PAST THE RESTING RING, NOT ON IT.  The last frame is drawn at
   radius 0x19 and not at the 0x18 the menu rests at, so the picture the player
   is left looking at while fdps_menu_cursor_input_loop takes over has the four
   buttons a pixel off their resting places until the first repaint moves them
   back.  The frames are also not erased between one another, because
   fdps_render_ring_menu_frame composes each one on a fresh uncleared page.

   It draws straight to the adapter, exactly as the frame it calls does, and it
   presents nothing afterwards. */
extern void fdps_menu_animate_open(int *cmd_icons, int *cmd_disabled,
                                   int cursor_dir);
#pragma aux fdps_menu_animate_open "*" parm caller [];

#endif
