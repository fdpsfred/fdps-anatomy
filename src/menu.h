/* menu.h -- the four-way command menus the battle screens put a cursor on.
 *
 * A menu is described by four ints, one per entry, in the order the entries sit
 * on screen: 0 up, 1 left, 2 right, 3 down.  Zero means the entry is
 * selectable, non-zero means it is greyed out.  The descriptor is supplied by
 * the caller -- fdps_battle_item_menu copies a static one and patches it,
 * fdps_battle_action_menu fills its own from the per-command legality probes.
 *
 * The village screens' horizontal command-icon strip is here too.  It shares
 * the Command.cel plates and icons with the ring menu but not its descriptor:
 * it takes a count and a plain array of icon ids, and greys nothing out.
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

/* Plays the ring menu's closing retraction: the same window cue once, then six
   frames of fdps_render_ring_menu_frame at radii 0x17, 0x13, 0xf, 0xb, 7 and 3.
   Returns nothing, and hands cmd_icons, cmd_disabled and cursor_dir to every
   frame exactly as they came in; cursor_dir is the entry the player was last
   on, so that entry stays highlighted all the way in.

   THE RETRACTION NEITHER REACHES THE CURSOR NOR ERASES THE BUTTONS.  The last
   frame is drawn at radius 3, three pixels out, and the function returns with
   the four buttons still on the adapter.  Which caller clears them and when is
   the caller's business: fdps_battle_system_submenu, fdps_battle_action_menu
   and fdps_battle_item_menu repaint the view on the next instruction, and
   fdps_options_menu leaves them standing until the menu is opened again.

   It draws straight to the adapter, exactly as the frame it calls does, and it
   presents nothing afterwards. */
extern void fdps_menu_animate_close(int *cmd_icons, int *cmd_disabled,
                                    int cursor_dir);
#pragma aux fdps_menu_animate_close "*" parm caller [];

/* Runs the ring menu until the player chooses: -1 when the menu was cancelled
   with Escape or keypad Del, 1 when it was confirmed with Enter or Space.  It
   never answers 0 -- that is the value it loops on.

   cmd_icons and cmd_disabled are the same two four-int arrays the frame and
   the two animations take, in slot order up, left, right, down; see the notes
   on fdps_render_ring_menu_frame above.  cmd_icons is only forwarded to the
   repaint, and neither array is written.

   cursor_dir is IN AND OUT: it comes in holding the slot the menu opens on and
   goes out holding the slot the player finished on, which is the entry the
   caller must act on when the answer is 1.  It is written on every accepted
   arrow key, so it has already moved even when the answer is -1.

   IT IS A FRAME LOOP AND IT NEVER BLOCKS.  One pass reads one scancode out of
   the ring (keybd.h) -- 0xff and all -- then cycles the scene and UI palettes
   and repaints the ring at the resting radius 0x18, so the picture is redrawn
   and the palettes advanced on every vertical retrace whether or not a key was
   pending.  The pass that chooses the answer repaints as well before this
   returns.  A caller cannot therefore treat the screen as untouched across
   this call, and must not expect the queue to be drained: exactly one code is
   consumed per frame, so a burst arrives one frame at a time.

   AN ARROW INTO A GREYED-OUT ENTRY DOES NOTHING AT ALL.  A non-zero
   cmd_disabled[slot] leaves cursor_dir where it was rather than skipping to
   the next selectable entry, so a menu with three entries greyed out cannot be
   moved off the fourth.  Nothing validates the incoming cursor_dir either.

   The only keys it knows are those eight.  Every other code, the empty-queue
   marker included, falls through the chain and costs one frame. */
extern int fdps_menu_cursor_input_loop(int *cmd_icons, int *cmd_disabled,
                                       int *cursor_dir);
#pragma aux fdps_menu_cursor_input_loop "*" parm caller [];

/* Runs the in-game options menu until the player cancels out of it.  Takes
   nothing and returns nothing: the four switches it drives are
   data_fdps_audio_bgm_enabled_flag, data_fdps_audio_sfx_enabled_flag,
   data_fdps_ui_battle_animation_enabled and
   data_fdps_ui_terrain_hud_user_enabled (gamedata.h), in slot order up, left,
   right, down.

   IT DOES NOT RETURN UNTIL ESCAPE OR KEYPAD DEL.  Confirming a switch toggles
   it and reopens the menu; only a cancel ends the call, so a caller gets
   control back knowing only that the player left, never which switches moved.

   THE MUSIC SWITCH GOES THROUGH THE DISC CHECK IN BOTH DIRECTIONS.  Toggling
   it calls fdps_cd_verify_disc_and_play_track for the current chapter's track
   whether the switch went on or off (cdaudio.h), so the insert-the-other-disc
   prompt and its blocking getch can be raised from inside this menu.

   IT LEAVES THE RETRACTED RING ON THE ADAPTER.  Nothing is drawn after
   fdps_menu_animate_close, and that stops three pixels out without erasing, so
   the caller has to repaint the view itself.  It also draws the whole menu
   straight to the adapter throughout and presents nothing afterwards, exactly
   as the primitives it is built from do. */
extern void fdps_options_menu(void);
#pragma aux fdps_options_menu "*" parm caller [];

/* Draws the row of command icons a village screen puts along the bottom of the
   picture, with the entry at selected_index on the highlighted cell frame, and
   then cycles the UI palette.  Returns nothing and writes none of its
   arguments back.

   icon_ids is an array of icon_count Command.cel sub-image ids, one per menu
   entry, left to right; the callers build it on their own stack.  icon_count is
   the number of entries, and sets the strip's left edge as well as the loop
   bound: the left edge is 0x12b - 26 * icon_count, so however many entries
   there are the row always ends at the same right-hand column.
   selected_index is the entry to highlight, or -1 for none.  The test is an
   equality against the loop index and not a range check, so any value outside
   0..icon_count-1 simply highlights nothing.

   IT DRAWS STRAIGHT TO THE ADAPTER AND TAKES NO PAGE.  The destination is the
   mode 13h aperture at row 106, not a surface the caller chose, and the
   neighbouring village-screen routines that do take a page pointer are no
   guide: nothing presents a page while the icon-strip loop is running, so a
   strip composed into the caller's page would never reach the display.

   NOTHING CLEARS THE STRIP AND NOTHING NEEDS TO.  Both cell frames cover the
   whole 25 x 22 cell opaquely, so each pass paints out the icon the previous
   pass left behind.  The one-column seam between neighbouring cells is never
   written and keeps whatever the screen already held.

   IT IS ALSO A FRAME OF PACING.  The closing fdps_cycle_ui_palette waits for
   the vertical retrace (palcycle.h), so one call is one displayed frame and it
   is what holds the caller's input loop to the refresh rate.  A caller cannot
   treat the DAC as untouched across this call. */
extern void fdps_menu_draw_command_icons(int *icon_ids, int icon_count,
                                         int selected_index);
#pragma aux fdps_menu_draw_command_icons "*" parm caller [];

#endif
