/* transit.h -- full-screen picture transitions: box, slide, random blocks and
 * zoom.
 *
 * Every routine here animates the change from one 8bpp picture to another on
 * a surface addressed by a byte pointer and a pitch in bytes, the same
 * convention blit.h uses.  They all work the same way: the outgoing picture
 * is the one already sitting on the destination surface, the incoming picture
 * is handed in as a source image, and the animation is composed off-screen in
 * scratch buffers and presented one frame at a time through
 * fdps_blit_rect.
 */
#ifndef TRANSIT_H
#define TRANSIT_H

/* Plays the box transition between the picture already on dst and the one in
   src: either a rectangle of the outgoing picture shrinking toward the centre
   or a rectangle of the incoming picture growing out of it, one delayed frame
   at a time.

   src is the incoming picture, at least width x height pixels with a row
   stride of src_pitch.  dst is the target surface with a row stride of
   dst_pitch, and it must already be carrying the outgoing picture: the first
   thing the routine does is read that rectangle back off dst into a scratch
   buffer, because it is what every frame of the animation composes against.

   BOTH SCRATCH BUFFERS ARE PACKED, WHATEVER dst_pitch IS.  They are two
   malloc blocks of width * height bytes each, and every blit into or out of
   them passes width as the stride.  Only the presents to dst use dst_pitch.

   step_x and step_y are how far each vertical and each horizontal edge of the
   rectangle moves per frame, so the rectangle's width changes by twice
   step_x and its height by twice step_y.  NEITHER MAY BE ZERO: style 0 tests
   2 * step_x against a width that would never shrink, and style non-zero
   divides by 2 * step_x.

   frame_delay is handed straight to the CRT's delay() between frames.

   style 0 closes the outgoing picture in toward the centre; any other value
   opens the incoming picture out from it.

   NEITHER malloc RESULT IS CHECKED, and both extents are used exactly as
   given: nothing here clips, and a rectangle that does not fit the surfaces
   is composed and presented anyway.

   THE ANIMATION STOPS ONE STEP SHORT AND THE CLOSING BLIT FINISHES IT.  Both
   paths end with a single full fdps_blit_rect of src onto dst, outside the
   loop, and it is that blit -- not the last frame -- that puts the complete
   incoming picture up.  In the opening path the frame count deliberately
   subtracts one from either axis whose extent divides evenly by twice its
   step, so the last frame never covers the whole rectangle; writing the
   obvious count instead adds a frame that paints the finished picture, plus
   its delay(), and the closing blit then repeats it. */
extern void fdps_transition_box(unsigned char *src, int src_pitch,
                                unsigned char *dst, int dst_pitch,
                                int width, int height,
                                int step_x, int step_y,
                                int frame_delay, int style);
#pragma aux fdps_transition_box "*" parm caller [];


/* Slides the picture already on dst and the one in src past each other, one
   delayed frame at a time, in whichever of eight directions style names.

   src is the incoming picture, at least width x height pixels with a row
   stride of src_pitch.  dst is the target surface with a row stride of
   dst_pitch, and it must already be carrying the outgoing picture: the
   routine reads that rectangle back off dst before it draws anything,
   because the four odd styles compose every frame against it.

   THE EIGHT STYLES ARE FOUR DIRECTIONS TIMES TWO WAYS ROUND.  An even style
   slides the incoming picture in over an outgoing picture that stays put; the
   odd style above it slides the outgoing picture away and uncovers the
   incoming one, which does not move.  0 and 1 work along the top edge, 2 and
   3 along the bottom, 4 and 5 along the left, 6 and 7 along the right.  The
   style is range-checked unsigned, so 8 and above -- and any negative value
   -- animate nothing at all.

   step is how many rows (styles 0-3) or columns (styles 4-7) the slide
   advances per frame, and also where the first frame starts: the animation
   runs at step, 2 * step, ... and stops at the last position strictly below
   the extent, so the frame that would show the whole incoming picture is
   never drawn.  STEP 0 NEVER TERMINATES, and a step at or above the extent
   draws no frame at all.

   frame_delay is handed straight to the CRT's delay() after each frame.

   BOTH SCRATCH BUFFERS ARE PACKED, WHATEVER dst_pitch IS, and both are
   allocated on every call.  They are two malloc blocks of width * height
   bytes, and the four even styles that never look at either one still
   allocate both and still take the snapshot.  NEITHER malloc RESULT IS
   CHECKED.

   THE TRANSITION IS COMPLETED BY A BLIT OUTSIDE THE ANIMATION.  Whatever
   style ran, and whether it drew a frame or none, the call ends with one full
   fdps_blit_rect of src onto dst, so the destination always holds the whole
   incoming picture on return.

   STYLE 6 WRITES OUTSIDE THE RECTANGLE AND IS MEANT TO.  Its loop counts
   columns up to width like its three horizontal siblings, but it anchors each
   frame at dst + (height - position) instead of dst + (width - position).
   On any rectangle that is not square that draws a different picture, and on
   a rectangle taller than it is wide it writes past the right-hand edge --
   on the game's 320x200 the position runs past 200 and the anchor goes in
   front of dst.  Writing the obvious width here, or adding a bounds guard,
   changes what the player sees.

   Nothing is clipped anywhere else either: the extents and both strides are
   used exactly as handed over. */
extern void fdps_transition_slide(unsigned char *src, int src_pitch,
                                  unsigned char *dst, int dst_pitch,
                                  int width, int height,
                                  int step, int frame_delay, int style);
#pragma aux fdps_transition_slide "*" parm caller [];

#endif
