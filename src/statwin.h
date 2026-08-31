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

#endif
