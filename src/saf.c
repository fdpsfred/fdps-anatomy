/* saf.c -- the .SAF animation container: locating things inside a loaded
 * image and playing one back.
 *
 * See saf.h for the layout facts these readers depend on, and
 * resource_info/saf.md for the format itself.  Everything here works on a
 * caller-supplied image pointer; this file owns no state.  The player at the
 * end is the one routine that reaches outside itself: it reads the timer tick
 * counter to pace the clip and puts each finished frame straight on the
 * adapter.
 */
#include <stddef.h>
#include <stdlib.h>
#include "gamedata.h"
#include "blit.h"
#include "sprite.h"
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

/* 00014550.  The clip's whole playback state is the caller's three-dword block
   (SAF_CURSOR_* in saf.h): the assembly reads it as [EAX], [EAX+4] and [EAX+8]
   off the pointer at [EBP+0x14], and the mode byte at [EBP+0x18] is loaded
   with XOR EAX,EAX / MOV AL,byte ptr [EBP+0x18], zero-extended, so it is a
   byte-wide unsigned argument however wide the dword the caller pushed.

   The magic test and the frame lookup are written out here rather than called,
   because the assembly has no CALL in it at all.  What the original source said
   is a separate question from what to write here, and the evidence says it
   called them: both expansions carry a full copy of the callee's frame --
   argument temps copied into consecutive parameter-shaped slots, the body
   replayed under a uniform slot substitution, the result copied back out -- and
   -oe is not in the flag set, so the expansion was asked for in the source with
   _inline (rebuild_info/build_flags.md).  Writing the bodies out is still
   correct: ADR-0001 is functional equivalence and the two spellings compile to
   the same behaviour.  Chasing the original's wording is the riskier of the
   two, because the call spelling only stays equivalent while the _inline
   declaration is there to expand it -- a call without it puts a CALL here that
   the original does not have.  Both copies are read the same
   way -- the count word at +0x0c zero-extended, the three magic bytes joined
   with OR, the frame offset rebased on the image base -- so the notes on
   fdps_saf_frame_count and fdps_saf_get_frame above apply here unchanged.

   Three things in the tick arithmetic are decided by the assembly and not by
   what reads naturally:

   MOVSX EDX,word ptr [EAX+0x2] / CMP EDX,dword ptr [EAX+0x4] / JG: the
   duration is sign-extended and the compare is signed, so a duration of
   0xffff is -1 and steps the frame on at the first tick instead of holding it
   for 65535 of them.  The tick counter is incremented before the compare, so
   a frame whose duration is n is shown for exactly n ticks.

   INC dword ptr [EAX+0x4] sits after the frame-count test, not before it, so
   the -1 answer leaves the counter alone: an image with no frames does not
   accumulate ticks while a caller keeps polling it.

   The lookup's else-branch stores 0 and the duration is then read through it
   with no test -- MOV EAX,dword ptr [EBP+-0x8] / MOVSX EDX,word ptr [EAX+0x2]
   reads address 2.  Adding the NULL check that shape asks for would change
   what the original does with a cursor whose frame index is out of range, so
   it is not added; the index this function maintains stays in range on its
   own, which is why the original never had to care. */
int fdps_saf_advance_tick(int *cursor, unsigned char mode)
{
    unsigned char *saf_base;
    int frame_count;
    unsigned int frame_section_start;
    unsigned char *frame;
    int result;

    if (mode == 1) {
        cursor[SAF_CURSOR_TICKS_HELD] = 0;
        cursor[SAF_CURSOR_FRAME_INDEX] = 0;
        result = 0;
    } else {
        saf_base = (unsigned char *) cursor[SAF_CURSOR_IMAGE];
        if (saf_base[0] == 'S' || saf_base[1] == 'A' || saf_base[2] == 'F') {
            frame_count =
                *(unsigned short *) (saf_base + SAF_FRAME_COUNT_OFFSET);
        } else {
            frame_count = 0;
        }
        if (frame_count == 0) {
            result = -1;
        } else {
            cursor[SAF_CURSOR_TICKS_HELD]++;
            if (cursor[SAF_CURSOR_FRAME_INDEX]
                < *(unsigned short *) (saf_base + SAF_FRAME_COUNT_OFFSET)
                && cursor[SAF_CURSOR_FRAME_INDEX] >= 0) {
                frame_section_start =
                    *(unsigned int *) (saf_base +
                                       SAF_FRAME_SECTION_START_OFFSET);
                frame = saf_base + *(unsigned int *)
                    (saf_base + frame_section_start
                     + cursor[SAF_CURSOR_FRAME_INDEX] * 4);
            } else {
                frame = NULL;
            }
            result = 0;
            if (*(short *) (frame + 2) <= cursor[SAF_CURSOR_TICKS_HELD]) {
                cursor[SAF_CURSOR_TICKS_HELD] = 0;
                cursor[SAF_CURSOR_FRAME_INDEX]++;
                if (cursor[SAF_CURSOR_FRAME_INDEX] >= frame_count) {
                    cursor[SAF_CURSOR_TICKS_HELD] = 0;
                    if (mode == 0) {
                        cursor[SAF_CURSOR_FRAME_INDEX] = 0;
                    } else {
                        cursor[SAF_CURSOR_FRAME_INDEX] = frame_count - 1;
                    }
                    result = 1;
                }
            }
        }
    }
    return result;
}

/* The visible mode 13h screen: its linear aperture, its 320 bytes to the row
   and its 200 rows.  The window the player moves is the whole screen, so its
   width in bytes is the pitch itself and one macro serves for both. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8

/* The private page a frame is composed on: 368 by 248, which is the screen
   with 24 pixels of margin added on every side, and the 0x16480 bytes that
   comes to.  The margin is what a frame overhanging the screen is drawn into,
   so nothing has to be clipped and nothing wraps onto the next row. */
#define SAF_PAGE_PITCH 0x170
#define SAF_PAGE_ROWS 0xf8
#define SAF_PAGE_MARGIN 0x18
#define SAF_PAGE_BYTES 0x16480

/* Where the screen-sized window sits inside that page: 24 rows down and 24
   bytes in, 0x18 * 0x170 + 0x18.  It is also where the request's origin points,
   so a frame layer at offset 0,0 lands on screen pixel 0,0. */
#define SAF_PAGE_WINDOW_AT 0x2298

/* 0001ecf0.  One loop with its test at the top -- CMP dword ptr [EBP-0x4],0x0
   / JNZ to the epilogue at 0001ed5b, and JMP back to that test at 0001edf5 --
   so a clip fdps_saf_advance_tick refuses outright never draws a frame.  The
   answer the test reads is fdps_saf_advance_tick's, and the reset call before
   the loop is the one whose answer is thrown away.

   THE WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED, the same contract
   fdps_animate_turn_banner carries (anim.c).  last_tick is read at 0001edd0
   before anything has written it, so the first frame's wait ends at once
   unless the stack happened to hold the counter's current value, and the clip
   is one tick shorter than a clean reading of it would be.  Latching the
   counter before the loop -- which is what writing this tidily leads to --
   adds that tick back.  data_fdps_timer_tick_counter is volatile at its
   declaration (gamedata.h) precisely so this loop keeps reloading it.

   The two blits are the same function in its two directions: the caller's
   background into the page's window, then the page's window onto the adapter.
   Only the window travels, so the 24-pixel margin the frame may have spilled
   into is never seen.

   malloc's answer is used without a test, and there is no CALL in the function
   whose value is read other than malloc's and fdps_saf_advance_tick's --
   fdps_blit_rect and fdps_draw_composite_sprite both return void, and free's
   answer is discarded by the original as it is here.

   0xa0000 is written as a literal because it is the adapter's real linear
   address under DOS/4GW and not a symbol the rebuild places anywhere
   (rebuild_info/pitfalls.md, contract E). */
void fdps_saf_play_over_background(void *saf_image,
                                   unsigned char *background_page)
{
    /* The nine-dword draw request sprite.h describes, built once and with only
       its frame number rewritten inside the loop. */
    int request[DRAW_REQUEST_DWORDS];
    /* The three-dword playback cursor saf.h describes: which frame the clip is
       on, how long it has been there, and the image it lives in. */
    int playback_cursor[SAF_CURSOR_DWORDS];
    /* The 368x248 page every frame is composed on, so that a frame is never
       seen half-drawn on the adapter. */
    unsigned char *work_page;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;
    /* fdps_saf_advance_tick's answer: 0 while the clip is still running, and
       1 on the tick that steps past its last frame. */
    int clip_ended;

    clip_ended = 0;
    work_page = (unsigned char *) malloc((size_t) SAF_PAGE_BYTES);
    request[DRAW_REQUEST_DEST_BASE] = (int) work_page;
    request[DRAW_REQUEST_DEST_PITCH] = SAF_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = SAF_PAGE_ROWS;
    request[DRAW_REQUEST_X] = SAF_PAGE_MARGIN;
    request[DRAW_REQUEST_Y] = SAF_PAGE_MARGIN;
    request[DRAW_REQUEST_IMAGE] = (int) saf_image;
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;

    playback_cursor[SAF_CURSOR_IMAGE] = (int) saf_image;
    fdps_saf_advance_tick(playback_cursor, 1);

    while (clip_ended == 0) {
        fdps_blit_rect((unsigned int) background_page, VGA_SCREEN_PITCH,
                       work_page + SAF_PAGE_WINDOW_AT, SAF_PAGE_PITCH,
                       VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);
        request[DRAW_REQUEST_ITEM_INDEX] =
            playback_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);
        fdps_blit_rect((unsigned int) (work_page + SAF_PAGE_WINDOW_AT),
                       SAF_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
        clip_ended = fdps_saf_advance_tick(playback_cursor, 0);
    }

    free(work_page);
}
