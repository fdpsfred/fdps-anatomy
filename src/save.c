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
