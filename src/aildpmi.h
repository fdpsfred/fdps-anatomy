/* aildpmi.h -- game-side DPMI service routines.
 *
 * These routines are FDPS's own code, not Miles AIL's: the vendor object
 * inside the AIL library references them with EXTDEF records, so the call
 * direction is library -> game. See program_info/code_pools.md.
 * fdps_dpmi_alloc_dos_memory, fdps_dpmi_free_dos_memory,
 * fdps_dpmi_lock_region and fdps_dpmi_unlock_region are declared in
 * src/dpmi.h.
 */
#ifndef AILDPMI_H
#define AILDPMI_H

/* Every symbol here is exported undecorated so the vendor object's EXTDEF
   records resolve against it verbatim. */
extern int  fdps_dpmi_lock_size(unsigned base, unsigned size);
#pragma aux fdps_dpmi_lock_size "*";

extern int  fdps_dpmi_unlock_size(unsigned base, unsigned size);
#pragma aux fdps_dpmi_unlock_size "*";

#endif
