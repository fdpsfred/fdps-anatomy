/* savefile.h -- FDE.SAV itself: reading a saved game back into the live game
 * state, and the two primitives that guard the image on disc -- the integrity
 * checksum and the XOR stream cipher.
 *
 * The file is one 0x59cb-byte image holding a fixed header followed by four
 * 0xa28-byte chapter slots (struct fdps_save_slot in fdpstype.h), with a
 * 32-bit checksum dword at +0x59c7 that overlaps the tail of the unreachable
 * fourth slot.  Everything on disc is ciphertext: the read path decrypts the
 * whole image in place and then verifies the checksum over the plaintext, and
 * the write path computes the checksum first and encrypts afterwards.
 *
 * The screens that put slots in front of the player are save.h; the page they
 * draw the slots on is savepnl.h.
 */
#ifndef SAVEFILE_H
#define SAVEFILE_H

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

/* 000568b7.  The FDE.SAV stream cipher, applied in place over `length` bytes
   starting at `buffer`.  A sixteen-bit key seeded with 0xa5 is advanced once
   per byte -- add 0x9014, rotate left by three, both modulo 0x10000 -- and its
   low half is XORed into the byte.

   The keystream is a function of the byte's index alone, so the routine is its
   own inverse and there is no separate decryptor: the write path calls it
   after computing the checksum, the read path calls it before verifying one.

   `length` is a raw loop count and is not checked: a length of 0 wraps it and
   walks 2^32 bytes.  All eight call sites pass the whole 0x59cb-byte image. */
extern void fdps_xor_crypt_buffer(unsigned char *buffer, unsigned int length);
#pragma aux fdps_xor_crypt_buffer "*" parm caller [];

#endif
