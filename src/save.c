/* save.c -- FDE.SAV: the save and load screens, the slot panel, and the
 * checksum and XOR cipher that guard the file.
 *
 * See save.h for the image's shape on disc and for the order the checksum and
 * the cipher are applied in.
 */
#include "save.h"

/* 00056898.  Hand-written assembly, not compiler output: ESI walks the image
   with LODSB, EBX is the accumulator and ECX is a LOOP count.  The C below is
   the same arithmetic, not the same registers (ADR-0001).

   SUB ECX,0x4 at 000568a5 is the whole point of the routine and the one thing
   that must not be tidied: the last four bytes of the image are the stored
   checksum dword this result is compared against, so summing the buffer whole
   fails every save the game ever wrote (rebuild_info/pitfalls.md).

   XOR EAX,EAX at 000568aa runs once, before the loop, and LODSB writes only
   AL -- so every byte enters the sum zero-extended and the upper 24 bits stay
   clear for the whole walk.  Reading the image as signed char instead would
   subtract for every byte over 0x7f and no real save would verify.  ADD
   EBX,EAX is a 32-bit add and the sum is allowed to wrap there; over the
   0x59c7 bytes the callers actually pass it cannot, the ceiling being
   0x59c7 * 0xff.

   The loop is LODSB / ADD / LOOP, which tests the count after the body and not
   before, so the count is spelled here as a do-while and not as a for.  The
   difference is only visible at size == 4, where the original decrements 0 to
   0xffffffff and walks four billion bytes; that is reproduced rather than
   guarded because it is what the function does, and no call site can reach it.

   Two instructions in the original have no effect and are not carried over:
   MOV EDI,ESI at 000568a0 loads a register nothing here reads, and EBX is used
   as the accumulator without being saved or restored even though the stack
   convention makes it callee-saved.  Both are shared shape with
   fdps_xor_crypt_buffer immediately after it at 000568b7, which does use EDI
   for its STOSB.  The unsaved EBX destroys the caller's copy, but all four
   callers push and pop EBX themselves and use it only as a divisor loaded one
   or two instructions before an IDIV, never across this call, so nothing
   observes it. */
unsigned int fdps_compute_save_checksum(unsigned char *save_image,
                                        unsigned int size)
{
    unsigned char *image_cursor;
    unsigned int bytes_remaining;
    unsigned int checksum;

    image_cursor = save_image;
    bytes_remaining = size - 4;
    checksum = 0;

    do {
        checksum += (unsigned int) *image_cursor;
        image_cursor++;
        bytes_remaining--;
    } while (bytes_remaining != 0);

    return checksum;
}

/* 000568b7.  Hand-written assembly like the checksum above it, and sharing its
   shape: ESI walks the buffer with LODSB and EDI writes back over it with
   STOSB, both loaded from the same `buffer` argument at 000568bc/000568bf, and
   ECX drives a LOOP.  The two registers never separate, so the C below walks
   one cursor; that is the same arithmetic and not the same registers
   (ADR-0001).

   The key lives in DX and every step of it is sixteen bits wide.  ADD DX,0x9014
   at 000568c9 wraps at 0x10000 and drops the carry, and ROL DX,3 at 000568ce
   rotates bits 15..13 back into bits 2..0.  Widening the key to an int and
   rotating 32 bits instead would change the very first keystream byte from
   0xcc to 0xc8 and every one after it, and no save the game ever wrote would
   decrypt (rebuild_info/pitfalls.md).

   The key is advanced before the XOR and not after, so the seed 0xa5 is never
   itself used as a keystream byte: the first byte of the buffer meets
   rol16(0xa5 + 0x9014, 3) & 0xff, which is 0xcc.  XOR AL,DL at 000568d2 takes
   the low half of the key alone; DH is carried forward but never applied.

   The keystream depends on the byte index and never on the data, which makes
   the routine its own inverse -- the save path calls it to encrypt and the
   load path calls the same routine, unchanged, to decrypt.  There is no
   separate decryptor anywhere in the image.

   LOOP tests the count after the body, so this is a do-while and not a for: a
   length of 0 decrements to 0xffffffff and walks four billion bytes rather
   than none.  That is reproduced rather than guarded because it is what the
   function does, and no call site can reach it -- all eight push the literal
   0x59cb. */
void fdps_xor_crypt_buffer(unsigned char *buffer, unsigned int length)
{
    unsigned char *byte_cursor;
    unsigned int bytes_remaining;
    unsigned short key;

    byte_cursor = buffer;
    bytes_remaining = length;
    key = 0x00a5;

    do {
        key = (unsigned short) (key + 0x9014);
        key = (unsigned short) ((key << 3) | (key >> 13));
        *byte_cursor = (unsigned char) (*byte_cursor ^ (unsigned char) key);
        byte_cursor++;
        bytes_remaining--;
    } while (bytes_remaining != 0);
}
