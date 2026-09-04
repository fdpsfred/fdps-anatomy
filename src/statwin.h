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


/* 00063fb0.  Which unit fdps_unit_status_window_wait_input animates while a
 * village phase is running, in place of the index its caller passed.
 *
 * fdps_draw_unit_status_panel is its only writer (MOV [0x00063fb0],EAX at
 * 00016318) and the wait loop its only reader (MOV EAX,[0x00063fb0] at
 * 0001725d); a sweep of the image for 0x00063fb0 finds those two instructions
 * and nothing else.  So it is not a general "current unit": it is one panel
 * draw handing one number to the loop that follows it, and it is read only
 * when data_fdps_village_mode_flag is set.
 *
 * A signed int, and it indexes the sprite cache's slot table with nothing
 * bounding it.  It is uninitialised until a panel has been drawn. */
extern int data_fdps_village_status_window_unit_idx;

/* 00063fc0.  The timer tick fdps_unit_status_window_wait_input last drew a
 * frame on, which is what paces the window: a pass draws only when
 * data_fdps_timer_tick_counter differs from this, and the pass ends by
 * copying the counter into it.
 *
 * Private to that one function -- all four instructions that name 0x00063fc0
 * are inside it -- but a global rather than a local, and that is observable:
 * it keeps its value between calls, so a window opened again on the same tick
 * a previous one closed on draws nothing until the timer moves.
 *
 * IT IS SIGNED, AND IT IS THE TICK THE WALK FRAME IS TAKEN FROM.  The walk
 * cycle is (this % 16) / 4 through IDIV and SAR at 000171fa and 00017206, not
 * through a shift and a mask, so the frame chosen once the counter has passed
 * 0x7fffffff is the one a signed division gives.  It is also this tick and
 * not the live counter that the frame is drawn for.
 *
 * Never cleared.  Nothing resets it when the window closes or when a chapter
 * ends. */
extern int data_fdps_unit_status_window_last_tick;

/* Holds the assembled status window on the screen until the player picks
   something, and comes back with the scancode they picked with.
   fdps_draw_status_window_anim_frame slides the window in, this keeps it
   there, and the same animation run backwards takes it away again.

   window_image is a whole 320x200 frame holding the window at its resting
   position -- what fdps_load_status_cel_image loaded and
   fdps_draw_unit_status_panel painted over.  IT IS WRITTEN AS WELL AS READ:
   the unit's 24x24 cell is stamped into it at row 10, column 161 on every
   frame drawn, so a caller that reuses the image afterwards is reusing one
   with the last walk frame in it.

   unit_index selects the sprite cache slot the walk cycle comes from -- the
   same slot number fdps_blit_unit_sprite uses -- and is IGNORED while
   data_fdps_village_mode_flag is set, when
   data_fdps_village_status_window_unit_idx is substituted for it.

   allow_idle_animation offers rather than requests: non-zero draws one rand
   and arms the idle sequence only when the value is a multiple of 200.  Both
   call sites are fixed -- the battle window passes 1, the item window 0 --
   and the difference is visible in the CRT's random state as well as on the
   screen, because a zero does not call rand at all.

   THE RESULT IS EVERY SCANCODE AT OR BELOW 0x7f, NOT A MENU CHOICE.  The loop
   filters out only the 0xff the reader answers with when nothing has been
   pressed, and every break code; deciding which of the remaining codes means
   anything is the caller's, and THE TWO CALLERS DECIDE DIFFERENTLY.  The item
   window keeps the value and compares it against 0x48, 0x50, 0x1c, 0x39, 0x1
   and 0x53, treating the rest as "keep going".  The battle window does not
   look at it at all -- EAX is overwritten by the instruction after the call
   returns (LEA EAX,[EBP-0x44] at 00016bff) -- so that window closes on any
   code at or below 0x7f, whichever key it was.

   IT DRAWS ONLY WHEN THE TIMER HAS MOVED.  Every pass polls the keyboard, and
   a pass draws a frame only when data_fdps_timer_tick_counter has left
   data_fdps_unit_status_window_last_tick behind, so the animation runs at the
   timer's rate however fast the loop spins.  A frame takes three heap blocks
   and gives all three back, checks none of them, and reads the sprite cache
   pointer without testing it for null. */
extern int fdps_unit_status_window_wait_input(unsigned char *window_image,
                                              int unit_index,
                                              char allow_idle_animation);
#pragma aux fdps_unit_status_window_wait_input "*" parm caller [];

#endif
