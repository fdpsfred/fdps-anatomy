/* aildpmi.c -- game-side DPMI service routines the Miles AIL library calls.
 *
 * What is left here is the size-taking spelling of the unlock. AIL's vendor
 * object references it by EXTDEF, so it must be defined and linked in; the
 * library does not provide it. Function 0100h, the DOS memory allocation,
 * function 0101h, the free, function 0600h, the region lock, function 0601h,
 * the unlock, and the size-taking spelling of the lock are all in src/dpmi.c.
 */
#include "aildpmi.h"
#include "dpmi.h"

/* Probe-free in the original: 0003cbaa opens with a bare PUSH/SUB ESP, no
 * PUSH n / CALL __CHK. The pinned flag set has -s, so this pragma changes
 * nothing today; it is here because this runs on AIL's driver setup and
 * interrupt paths, where a stack probe is exactly what must not appear, and a
 * build variant without -s would otherwise add one silently. */
#pragma off (check_stack)

/* `base + size` is the last byte released, so this unlocks one byte more than
   `size`. That is the original's behaviour and callers depend on it -- see
   rebuild_info/pitfalls.md. */
int fdps_dpmi_unlock_size(unsigned base, unsigned size)
{
    return fdps_dpmi_unlock_region(base, base + size);
}
