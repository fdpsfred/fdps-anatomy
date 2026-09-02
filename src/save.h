/* save.h -- FDE.SAV: the save and load screens, the slot panel they draw, and
 * the two primitives that guard the file itself -- the integrity checksum and
 * the XOR stream cipher the image is stored under
 * (rebuild_info/code_layout.md).
 *
 * The file on disc is one 0x59cb-byte image holding a fixed header followed by
 * four 0xa28-byte chapter slots (struct fdps_save_slot in fdpstype.h), with a
 * 32-bit checksum dword at +0x59c7 that overlaps the tail of the unreachable
 * fourth slot.  Everything on disc is ciphertext; the read path decrypts the
 * whole image in place and then verifies the checksum over the plaintext, and
 * the write path computes the checksum first and encrypts afterwards.
 */
#ifndef SAVE_H
#define SAVE_H

/* 00056898.  The FDE.SAV integrity checksum: the sum of every byte of the
   save image except the trailing four, which are the stored checksum field
   itself and must not take part in the sum that is compared against them.

   `save_image` points at the plaintext image and `size` is its whole length
   counting that field; all four call sites pass the buffer and the literal
   0x59cb.  Bytes are summed zero-extended, and the sum wraps at 32 bits.

   `size` is a raw loop count, not a checked length: a size below 4 wraps the
   count and walks the heap, and a size of exactly 4 walks 2^32 bytes.  Neither
   is reachable from the four call sites and neither is guarded against. */
extern unsigned int fdps_compute_save_checksum(unsigned char *save_image,
                                               unsigned int size);
#pragma aux fdps_compute_save_checksum "*" parm caller [];

#endif
