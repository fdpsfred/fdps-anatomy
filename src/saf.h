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

#endif
