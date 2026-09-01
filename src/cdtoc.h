/* cdtoc.h -- the CD table-of-contents layer: the MSCDEX disk- and track-info
 * queries, and the Red Book address arithmetic that turns their answers into
 * something the audio code can seek with.
 *
 * The globals this module fills in from the driver's replies live partly here
 * and partly in gamedata.h, depending on whether anything outside this
 * translation unit reads them; cd.c owns the request path itself.
 */
#ifndef CDTOC_H
#define CDTOC_H

/* 0003bc3f.  Splits a packed Red Book address, 0x00MMSSFF as the MSCDEX device
   driver reports track starts and the lead-out position, into its three
   separate byte fields.  Bits 24-31 are ignored (AND 0xff0000 is the widest
   mask the body applies), nothing is returned and no global is touched: the
   three results leave through the out-parameters and the caller owns them.

   The parameter order is most-significant field first -- packed, minute,
   second, frame -- while the body stores least-significant first.  Writing a
   call with the pointers in the order the stores appear swaps minutes and
   frames, and since nothing range-checks the fields the swap does not fault,
   it just names a position on the disc 4500 frames per minute away from the
   intended one. */
extern void fdps_cd_unpack_msf(unsigned int msf_packed, unsigned char *minute,
                               unsigned char *second, unsigned char *frame);
#pragma aux fdps_cd_unpack_msf "*" parm caller [];

#endif
