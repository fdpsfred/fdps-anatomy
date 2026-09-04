/* saf.h -- the .SAF animation container: locating things inside a loaded
 * image and playing one back.
 *
 * A .SAF holds one animation's entire material in a single file -- the frame
 * script, the tilemap layouts each frame composites, the tiles those layouts
 * are built from, and the animation's own PCM sound effects -- in four
 * sections.  resource_info/saf.md is the canon for the format; what follows is
 * only what a caller of the readers below has to know.
 *
 * The file opens with a 52-byte header whose last part, from +0x0c, is four
 * 10-byte section descriptors -- u16 item count, u32 section start, u32
 * section size -- for the frame, tilemap, tile and sound sections in that
 * order.  A section begins with `count` u32 offsets and the items follow
 * immediately after them.  Every offset stored anywhere inside a .SAF is
 * measured from the start of the file, so a loaded image is addressed
 * entirely through the one base pointer the caller loaded it at, and a
 * reader's job is to rebase the stored offset onto that pointer.
 *
 * Those descriptors are byte-packed from an even offset onto an odd boundary:
 * the u32 at +0x0e is only 2-byte aligned.  An ordinary C struct for the
 * header pads the count word out to four bytes and lands every section field
 * two bytes past where the file has it, so the readers here address the
 * header by byte offset instead (rebuild_info/pitfalls.md).
 */
#ifndef SAF_H
#define SAF_H

/* Returns a pointer to frame number frame_index of the .SAF image based at
   saf, or NULL when frame_index is negative or is not less than the frame
   count in the image's header.  A frame record is i16 sound id (-1 for none),
   i16 duration in ticks, u8 lead-in count, u8 hit marker, u16 zero, i16 layer
   count, then that many 13-byte layers.  Nothing but the passed-in image is
   read, no global is touched and nothing is called; the frame record itself is
   never dereferenced, so an out-of-range stored offset is the caller's
   problem. */
extern void *fdps_saf_get_frame(void *saf, int frame_index);
#pragma aux fdps_saf_get_frame "*" parm caller [];

/* Returns the number of frames in the .SAF image based at saf: the u16 item
   count of section descriptor 0, zero-extended, so 0..65535.  The header's
   three magic bytes are tested first, but with OR rather than AND, so any one
   of them matching passes and only an image with all three wrong is refused;
   a refusal returns 0, which a caller cannot tell from an image that really
   holds no frames.  Nothing but the passed-in image is read, no global is
   touched and nothing is called. */
extern int fdps_saf_frame_count(void *saf);
#pragma aux fdps_saf_frame_count "*" parm caller [];

/* A playback cursor is three dwords the caller owns and the player advances:
   the frame the clip is showing, how many ticks it has been showing it, and
   the image the clip lives in.  Every caller builds one on its own stack --
   fdps_saf_play_over_background fills the third dword and then passes LEA
   EAX,[EBP-0x14] -- and the block is addressed here by element index, the way
   the assembly addresses it at +0, +4 and +8 of the passed pointer.

   Element 2 holds a pointer in an int slot, which is what a 32-bit flat model
   makes possible and what the passed type says; the readers above take that
   image as a void *. */
#define SAF_CURSOR_FRAME_INDEX 0
#define SAF_CURSOR_TICKS_HELD 1
#define SAF_CURSOR_IMAGE 2
#define SAF_CURSOR_DWORDS 3

/* Advances the playback cursor by one tick and says what happened: 0 while the
   clip is still running, 1 on the tick that steps past the last frame, -1 when
   the image states no frames (which includes an image whose three magic bytes
   are all wrong).

   mode 1 is a reset -- both counters go to zero, nothing else is read, and the
   answer is 0.  Otherwise mode decides what happens at the end of the clip:
   0 restarts at frame 0, and any other value leaves the cursor on the last
   frame.  Either way the end-of-clip tick reports 1 once, so a caller that
   stops on 1 sees the clip through exactly once.

   The current frame is held for as many ticks as its duration field states,
   and the duration is signed: a negative one steps the frame on immediately.
   Only the image the cursor points at is read; no global is touched and
   nothing is called. */
extern int fdps_saf_advance_tick(int *cursor, unsigned char mode);
#pragma aux fdps_saf_advance_tick "*" parm caller [];

/* Plays the loaded .SAF image saf_image through once, full screen, over a
   copy of background_page, and returns when the clip has run out.  Both
   blocks stay the caller's: neither is freed here and neither is written to.

   background_page is a 320x200 8bpp picture with 320 bytes to the row -- the
   caller's snapshot of whatever was on the screen before the animation
   started.  It is repainted under every single frame, so the animation
   composites over a still picture rather than over its own previous frame.

   Frames are composed on a private 368x248 page, 24 pixels of margin on every
   side, and only its 320x200 window is put on the adapter: a frame whose
   layers hang up to 24 pixels off any edge of the screen is drawn into the
   margin instead of being clipped or wrapping onto the next row.  Playback is
   paced by the timer tick counter, one frame per change of it, and each
   frame's own sound effect is started as it is drawn.

   THE FIRST FRAME IS NOT PACED.  The latch the wait loop compares the tick
   counter against is deliberately never initialised, so the first frame goes
   up and moves on without waiting for a tick (rebuild_info/pitfalls.md).
   Seeding it -- with 0, or with the counter's current value -- adds one tick
   to every playback in the game. */
extern void fdps_saf_play_over_background(void *saf_image,
                                          unsigned char *background_page);
#pragma aux fdps_saf_play_over_background "*" parm caller [];

/* Plays the loaded .SAF image saf_image through once over the live scrolling
   scene, in the battle view's framed 312x192 main window, and returns when the
   clip has run out.  saf_image stays the caller's: it is not freed here, and
   both call sites free it themselves the moment this returns.

   The sibling above plays over a still picture the caller supplies; this one
   plays over the scene the map compositor draws, so there is no background
   argument.  fdps_draw_scene_layers repaints the scrolling layer table, the
   map cursor and the units into the private page before every single tick, and
   the frame goes over the top of that -- which means the scene keeps scrolling
   underneath a frame that is held for several ticks.

   Frames are composed on a private 360x240 page with a 24-pixel apron on every
   side, and only its 312x192 window is put on the adapter, at screen pixel
   (4,4).  The four-pixel border around that window and the strip to its right
   and below it are never written, so whatever the caller left there stays.

   Each frame is held for the tick count in its own record, one presented tick
   per change of the timer counter, and each frame's sound effect is started
   once per tick it is held rather than once per frame.  No information panel
   is drawn and the scene palette is not cycled for as long as the clip runs.

   THE FIRST TICK IS NOT PACED.  The latch the wait loop compares the tick
   counter against is deliberately never initialised, exactly as in the sibling
   above, so the clip's first tick goes up and moves on without waiting
   (rebuild_info/pitfalls.md).  Seeding it -- with 0, or with the counter's
   current value -- adds one tick to every playback in the game. */
extern void fdps_saf_play_over_scene(void *saf_image);
#pragma aux fdps_saf_play_over_scene "*" parm caller [];

#endif
