/* anim.c -- VFS/SAF animation playback and the turn banner.
 *
 * See anim.h for what a caller has to know and resource_info/vfs.md for the
 * container the animations come out of.  This file owns one piece of state,
 * the pointer to the member the last BaseAni.vfs lookup found; the archive
 * image itself belongs to the startup loader and is declared in gamedata.h.
 *
 * printf comes from <stdio.h> and exit from <stdlib.h>, and both are real
 * calls in the original -- CALL 0x00042deb and CALL 0x00042e0f at 0002a284 and
 * 0002a28e -- because the flag set carries no -oi.
 */
#include <stdio.h>
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "vfs.h"
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
