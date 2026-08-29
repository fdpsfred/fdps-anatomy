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

#endif
