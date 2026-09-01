/* cdtoc.c -- the CD table-of-contents layer: the MSCDEX disk- and track-info
 * queries and the Red Book MSF/sector arithmetic built on their answers.
 *
 * cd.c owns the device request path this module's queries go out through, and
 * the register and request blocks they share; this file owns what the driver's
 * replies mean.
 *
 * Note on this module's original build flags: like the rest of the block from
 * 0003bade to 0003c96x it was not compiled with the flag set the rest of the
 * game was -- every function in it opens with PUSH <frame size> / CALL __CHK,
 * the stack probe that -s removes and that no other game function carries.
 * The rebuild has one flag set for every unit (rebuild_info/build_flags.md),
 * so what it builds from this file is the same code without the probe, and
 * the probe is not written out below.
 */
#include "cdtoc.h"

/* 0003bc3f.  Three byte stores straight out of the packed argument, in
   least-significant-field-first order: the low byte to *frame, bits 8-15 to
   *second, bits 16-23 to *minute.  There is no branch and no arithmetic beyond
   the mask and shift of each field, and bits 24-31 never leave the register.

   Each field is isolated with AND and then brought down with SHR, a logical
   shift, so nothing here depends on the sign of the argument -- the mask has
   already cleared every bit above the field by the time the shift happens.

   The stores are byte-wide: MOV byte ptr [EDX],AL for each of the three, so a
   caller may point the three parameters at three adjacent bytes -- which
   fdps_cdrom_read_disk_info does, at the lead-out minute, second and frame
   globals -- without the writes reaching past them. */
void fdps_cd_unpack_msf(unsigned int msf_packed, unsigned char *minute,
                        unsigned char *second, unsigned char *frame)
{
    *frame = (unsigned char) msf_packed;
    *second = (unsigned char) ((msf_packed & 0xff00) >> 8);
    *minute = (unsigned char) ((msf_packed & 0xff0000) >> 16);
}
