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

/* 000144e0.  The magic test is three compares joined with OR, and the
   assembly says so without room for doubt: CMP EAX,0x53 / JZ to the accepting
   block, then CMP EAX,0x41 / JNZ to the third test with the fall-through
   accepting, then CMP EAX,0x46 / JNZ to the rejecting block.  One matching
   byte is enough, so an image whose first byte is 'S' is accepted whatever the
   other two hold.  Writing the natural && here would start rejecting images
   the original plays, silently -- the caller sees a frame count of 0 and its
   playback loop runs zero times (rebuild_info/pitfalls.md).

   Each byte is loaded with MOV AL,byte ptr [EAX+n] / AND EAX,0xff, a
   zero-extending read, and the count with XOR EAX,EAX / MOV AX,word ptr
   [EDX+0xc], so a count of 0xffff comes back as 65535 and never as -1.  The
   caller at 000222c0 compares it with its loop counter using JL, the signed
   compare, which is what an int result gives. */
int fdps_saf_frame_count(void *saf)
{
    unsigned char *saf_base;
    int frame_count;

    saf_base = (unsigned char *) saf;
    if (saf_base[0] == 'S' || saf_base[1] == 'A' || saf_base[2] == 'F') {
        frame_count =
            *(unsigned short *) (saf_base + SAF_FRAME_COUNT_OFFSET);
    } else {
        frame_count = 0;
    }
    return frame_count;
}
