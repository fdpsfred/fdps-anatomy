/* saf.c -- the .SAF animation container: locating things inside a loaded
 * image and playing one back.
 *
 * See saf.h for the layout facts these readers depend on, and
 * resource_info/saf.md for the format itself.  Everything here works on a
 * caller-supplied image pointer; this file owns no state.
 */
#include <stddef.h>
#include "saf.h"

/* Section descriptor 0 is the frame section, and it is the first of the four,
   so it starts at header offset 0x0c: u16 item count at +0x0c, u32 section
   start at +0x0e.  Both are addressed as byte offsets from the image base
   rather than through a header struct, because the u32 sits on an odd 2-byte
   boundary that a struct would pad away. */
#define SAF_FRAME_COUNT_OFFSET 0x0c
#define SAF_FRAME_SECTION_START_OFFSET 0x0e

/* 000140e0.  The count is loaded with MOV AX,word ptr [EAX+0xc] / AND
   EAX,0xffff -- a zero-extended 16-bit read, so a count of 0xffff is 65535 and
   not -1 -- and then compared with CMP EAX,dword ptr [EBP+0x18] / JLE, the
   signed compare, which is what the promotion of an unsigned short to int
   gives.  The lower bound is a separate CMP dword ptr [EBP+0x18],0x0 / JGE
   afterwards, in that order.

   The address arithmetic is three separate adds onto the image base: the
   section start read at +0x0e gives the offset table, LEA EAX,[EAX*0x4] steps
   it, and the entry read out of the table is added to the image base again --
   MOV EAX,dword ptr [EBP+0x14] / ADD EAX,dword ptr [EDX] -- not to the table
   address.  Stored offsets are file-relative, so rebasing them on anything but
   the base is wrong by however far into the file the table happens to sit. */
void *fdps_saf_get_frame(void *saf, int frame_index)
{
    unsigned char *saf_base;
    unsigned int frame_section_start;
    void *frame;

    saf_base = (unsigned char *) saf;
    if (frame_index < *(unsigned short *) (saf_base + SAF_FRAME_COUNT_OFFSET)
        && frame_index >= 0) {
        frame_section_start =
            *(unsigned int *) (saf_base + SAF_FRAME_SECTION_START_OFFSET);
        frame = saf_base + *(unsigned int *)
            (saf_base + frame_section_start + frame_index * 4);
    } else {
        frame = NULL;
    }
    return frame;
}
