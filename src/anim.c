/* anim.c -- VFS/SAF animation playback and the turn banner.
 *
 * See anim.h for what a caller has to know and resource_info/vfs.md for the
 * container the animations come out of.  This file owns one piece of state,
 * the pointer to the member the last BaseAni.vfs lookup found; the archive
 * image itself belongs to the startup loader and is declared in gamedata.h.
 *
 * printf and sprintf come from <stdio.h>, exit from <stdlib.h> and strlen from
 * <string.h>, and all four are real calls in the original -- CALL 0x00042deb
 * and CALL 0x00042e0f at 0002a284 and 0002a28e, CALL 0x00042d41 at 0001ea9c
 * and CALL 0x00042dd2 at 0001eaaf -- because the flag set carries no -oi
 * (rebuild_info/build_flags.md), so the plain declarations are what reproduce
 * them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "vfs.h"
#include "sprite.h"
#include "anim.h"

/* 0002a240.  One branch, CMP dword ptr [0x000643ec],0x0 / JZ at 0002a267, and
   the not-found arm ends in the exit call, so the ADD ESP,0x4 and the store of
   0 into the return slot that follow it at 0002a293 are never executed.

   The global is written from EAX at 0002a262 and then re-read twice, once for
   the test at 0002a267 and once for the value returned at 0002a270: the
   returned pointer is the global's contents and not a register held across the
   test.  That is visible to a caller only in that both say the same thing here,
   and it is why the answer is published before it is known to be good.

   The size the lookup writes is discarded.  It is asked for because
   fdps_vfs_image_get_entry insists on somewhere to put it -- LEA EAX,[EBP +
   -0x8] / PUSH EAX at 0002a24c -- and the slot is never read back. */
void *fdps_baseani_get_entry_or_exit(char *name)
{
    unsigned int entry_bytes;

    data_fdps_animation_baseani_entry_ptr = fdps_vfs_image_get_entry(
        (struct fdps_vfs_image_header *) data_fdps_animation_baseani_archive_ptr,
        name, &entry_bytes);
    if (data_fdps_animation_baseani_entry_ptr != NULL) {
        return data_fdps_animation_baseani_entry_ptr;
    }
    printf("File not found: %s\n", name);
    exit(1);
    return NULL;
}

/* What the formatted turn number is written into: SUB ESP,0xc at 0001ea86
   buys twelve bytes, eight of them the buffer at [EBP-0xc] and the last four
   the loop counter at [EBP-0x4].  Eight bytes hold seven digits and the
   terminator, and nothing bounds what sprintf writes here. */
#define TURN_NUMBER_BUFFER_BYTES 8

/* What a formatted digit is turned into a sprite-bank entry index with.  It is
   0x2f and NOT '0': entry 0 of the banner bank is the word graphic the caller
   draws itself and the numerals start at entry 1, so digit d is entry d + 1
   (rebuild_info/pitfalls.md). */
#define TURN_DIGIT_ENTRY_BIAS 0x2f

/* How far the request's x moves between one digit and the next, in pixels. */
#define TURN_DIGIT_PITCH 0x1c

/* 0001ea80.  One loop and no branch inside it, cyclomatic complexity 2.

   THE BYTE IS ZERO-EXTENDED AND THE COMPARE IS UNSIGNED, and both are
   behaviour rather than spelling.  XOR EDX,EDX / MOV DL,byte ptr [EAX+EBP-0xc]
   at 0001eac9 widens the formatted character without sign, which is why the
   buffer is unsigned char here; and CMP EAX,dword ptr [EBP-0x4] / JA at
   0001eab7 is the unsigned above, strlen's own size against an unsigned
   counter, so a signed counter would compare the wrong way the moment either
   side went past 0x7fffffff.

   strlen is called afresh on every pass -- the CALL at 0001eaaf is the loop's
   own condition, reached again from the increment block at 0001eac4 -- and not
   hoisted above it.

   The request is the caller's and is modified in place.  Only two of its nine
   slots are written, the entry index before each digit is painted and x after,
   so on return x sits one pitch past the last digit and the entry index holds
   the last digit's.  The order matters to what lands on screen: x is advanced
   AFTER the call, so the first digit is painted at the x the caller set.  The
   one caller, fdps_animate_turn_banner, rewrites both slots before it uses the
   block again.

   sprintf's return value is discarded -- ADD ESP,0xc at 0001eaa1 and no read
   of EAX -- and so is everything the composite drawer might have to say, which
   is nothing: it returns void. */
void fdps_draw_turn_number(int *request)
{
    unsigned char turn_digits[TURN_NUMBER_BUFFER_BYTES];
    unsigned int digit_index;

    sprintf((char *) turn_digits, "%d", data_fdps_battle_turn_counter);

    for (digit_index = 0; digit_index < strlen((char *) turn_digits);
         digit_index++) {
        request[DRAW_REQUEST_ITEM_INDEX] =
            turn_digits[digit_index] - TURN_DIGIT_ENTRY_BIAS;
        fdps_draw_composite_sprite(request, 0);
        request[DRAW_REQUEST_X] += TURN_DIGIT_PITCH;
    }
}
