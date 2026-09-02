/* aildpmi.c -- game-side DPMI service routines the Miles AIL library calls.
 *
 * What is left here are the two size-taking spellings of the lock pair. AIL's
 * vendor object references each of them by EXTDEF, so they must be defined and
 * linked in; the library does not provide them. Function 0100h, the DOS memory
 * allocation, function 0101h, the free, function 0600h, the region lock, and
 * function 0601h, the unlock, are all in src/dpmi.c.
 */
#include "aildpmi.h"
#include "dpmi.h"

/* Probe-free in the original: 0003cb93 and 0003cbaa open with a bare PUSH/SUB
 * ESP, no PUSH n / CALL __CHK. The pinned flag set has -s, so this pragma
 * changes nothing today; it is here because these run on AIL's driver setup
 * and interrupt paths, where a stack probe is exactly what must not appear,
 * and a build variant without -s would otherwise add one silently. */
#pragma off (check_stack)

/* `base + size` is the last byte locked, so these lock one byte more than
   `size`. That is the original's behaviour and callers depend on it -- see
   rebuild_info/pitfalls.md. */
int fdps_dpmi_lock_size(unsigned base, unsigned size)
{
    return fdps_dpmi_lock_region(base, base + size);
}

int fdps_dpmi_unlock_size(unsigned base, unsigned size)
{
    return fdps_dpmi_unlock_region(base, base + size);
}
