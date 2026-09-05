/* statwin.h -- the battle unit status window.
 *
 * The window is a 291x200 block of the screen, columns 15 to 305, assembled
 * off-screen as a whole 320x200 image and then put up in four pieces: an
 * upper-left panel (columns 15..147, rows 0..107), a lower-left one (the same
 * columns, rows 108..199), an upper-right one (columns 148..305, rows 0..43)
 * and a lower-right one (the same columns, rows 44..199).  Each piece slides
 * in from the screen edge nearest it, which is what the animation below
 * draws one step of.
 */
#ifndef STATWIN_H
#define STATWIN_H

/* Draws one step of the status window's four-panel slide-in over a background
   image and puts the result on the screen.

   background is a whole 320x200 mode 13h frame -- the picture the window
   opens over -- and window_image is another whole 320x200 frame holding the
   assembled window at the position it comes to rest in.  Both are read in
   full; neither is written.

   step selects the animation frame and must be 0..8.  Nothing here range
   checks it: it indexes four nine-entry tables directly, so a step outside
   that reads whatever follows them on the stack.  The window opening runs
   step 0 to 8 in order and the window closing runs 5 down to 0, which is why
   steps 6 and 7 exist at all -- they are the one-pixel overshoot that makes
   the opening settle rather than stop.

   THE FRAME IS DRAWN OFF-SCREEN AND PRESENTED WHOLE.  A 64000-byte buffer is
   allocated and freed on every single call, the background is copied into it,
   the four panels are blitted into it and only then is it copied to the VGA
   aperture -- one call, one allocation, one present.  The return of malloc is
   not checked.

   ONE CALL IS ONE DISPLAYED FRAME.  It ends with delay(15) and then a wait
   for the end of a vertical retrace before the present, so the caller's loop
   over the nine steps is paced by this function and not by anything of its
   own.  Dropping either the delay or the retrace wait, or moving the present
   before them, changes how fast the window opens. */
extern void fdps_draw_status_window_anim_frame(void *background,
                                               void *window_image, int step);
#pragma aux fdps_draw_status_window_anim_frame "*" parm caller [];

/* Loads Status.cel out of MISC.VFS and hands back the whole 320x200 frame it
   decodes to, freshly allocated.  Takes nothing: both names are literals in
   the code, so there is no way to point it at another sheet.

   THE CALLER OWNS THE BLOCK AND MUST FREE IT.  64000 bytes are allocated on
   every call and nothing here or anywhere else remembers the pointer, so a
   caller that drops it leaks a whole frame.  It is the window image the
   animation above is driven with, which is the only thing the four call sites
   do with it.

   THE BLOCK IS NOT A BLANK SURFACE WITH A PICTURE ON IT.  Nothing clears the
   allocation and Status.cel's stream does not cover all of it: 1,212 of the
   64,000 pixels are inside skip runs and come back holding whatever the heap
   left there.  Every one of them is inside the two 117x8 gauge windows at
   block + 0x9396 (row 118, column 22) and block + 0xab56 (row 137, column
   22), 606 in each, and nowhere else -- the sheet leaves the HP and MP bars
   out on purpose.  fdps_draw_status_window_anim_frame does copy those bytes
   to the screen, so what makes them invisible is not that nobody reads them:
   it is that all four callers run fdps_draw_unit_status_panel over the block
   before the window is ever animated, and that paints both bars there.  Bar.
   cel's three graphics carry no palette index 0 at any of those 1,212
   positions, so fdps_blit_transparent_rect writes every one of them whatever
   the fill width and whichever half of the bar covers it.  Clearing the
   allocation would therefore not change a displayed pixel, but it is not
   what the original does.

   Nothing is checked.  A missing container, a container without the member or
   a failed allocation are all followed straight into the next call. */
extern void *fdps_load_status_cel_image(void);
#pragma aux fdps_load_status_cel_image "*" parm caller [];

/* 00063fb8.  Where data_fdps_ui_play_active_flag is parked for as long as the
 * status window is up.  fdps_battle_show_unit_status_window saves the live flag
 * here and zeroes it on the way in (MOV [0x00063fb8],AL at 00016aef) and
 * fdps_close_status_window copies it back on the way out (MOV AL,[0x00063fb8]
 * at 00016a84); those two instructions are every reference to the address in
 * the image.
 *
 * So it is not a second flag, it is one function's saved copy of another
 * global, and it is what keeps fdps_draw_cursor_info_panel from painting the
 * terrain panel into the scene behind the window while the window is open.
 *
 * A byte, and it holds whatever the live flag held: it is copied both ways
 * without a test, so nothing here reduces it to 0 or 1.  It is uninitialised
 * until the status window has been opened once. */
extern unsigned char data_fdps_ui_play_active_flag_saved;

/* Takes the status window away again and puts the screen back the way it was.
   The mirror of the opening the caller ran: the same cue, the same four panels
   and the same nine-step tables, walked from step 5 down to step 0 instead of
   0 up to 8.

   window_image is the assembled window -- what fdps_load_status_cel_image
   loaded and the caller painted -- and is only read.  background is a
   caller-owned 64000-byte frame and is only WRITTEN: whatever it holds on entry
   is discarded, because this function fills it with the picture that lies
   behind the window before it draws the first retreating frame over it.  A
   caller that has a background it wants preserved must keep its own copy.

   WHERE THE PICTURE BEHIND THE WINDOW COMES FROM IS
   data_fdps_village_mode_flag's decision, and the two branches differ in more
   than their source.  Clear, the scene is composed again and the 312x192 view
   window is the only part of background that is written -- the rest is zeroed
   -- and the screen is put back by fdps_render_view_frame with the view's
   four-pixel border blanked afterwards.  Set, the saved village page is copied
   whole into background and whole to the screen, and no border is blanked.

   IT DOES NOT WAIT FOR A KEY AND IT RETURNS NOTHING.  All four call sites
   follow the CALL with ADD ESP,0x8 and then free the window image they passed,
   so the block is the caller's to release and this function does not touch it.

   THE PLAY FLAG COMES BACK LAST.  data_fdps_ui_play_active_flag is restored
   from data_fdps_ui_play_active_flag_saved after the screen has been repainted,
   not before, so the repaint this function performs still runs with the cursor
   info panel switched off and the panel reappears only on the caller's next
   frame. */
extern void fdps_close_status_window(void *window_image, void *background);
#pragma aux fdps_close_status_window "*" parm caller [];

/* Opens one unit's status window, holds it there for as long as the player
   looks at it, and closes it again.  The two call sites are the battle map's
   own phase loop and the village member list, and both discard everything: the
   call returns nothing and the window leaves no state behind but the play flag
   it borrowed.

   unit_index is a position in the current battle's unit array, unchecked, and
   the record it resolves to decides both what is drawn and WHETHER ANYTHING IS.
   A record whose portrait_id is 0x24, 0x25, 0x26 or 0x27 gets no window at all
   and the call is a no-op -- not a blank window, not a cue, not even the saved
   play flag; those four ids are the only such range, and everything outside it
   opens the window whether or not the unit is one of the player's.

   ONE CALL BLOCKS FOR AS MANY KEYS AS THE UNIT HAS PAGES.  The stat panel is
   held by a wait loop; a unit that knows a spell then gets the first spell page
   behind a mosaic dissolve and a second wait loop; and one that knows more than
   eight gets a second page cross-faded in and a third.  So this returns after
   one key for most units, two for a caster and three for a well-taught one, and
   the keyboard queue is emptied before it does.

   IT LEAVES THE SCREEN AS IT FOUND IT, THROUGH ITS OWN SAVED COPY.  Outside a
   village the picture behind the window is composed once by
   fdps_render_view_frame before the copy is taken, so the frame the window
   restores afterwards is that composed picture and not whatever the caller had
   on the adapter; in a village it is the adapter's own contents.  Every heap
   block it takes -- the window image, the saved frame and the panel copy -- is
   released before it returns. */
extern void fdps_battle_show_unit_status_window(int unit_index);
#pragma aux fdps_battle_show_unit_status_window "*" parm caller [];

#endif
