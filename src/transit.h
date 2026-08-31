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

#endif
