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

/* 0003bc78.  Turns a packed Red Book address, the same 0x00MMSSFF the MSCDEX
   driver reports, into the logical sector number of that position: minutes
   times 4500 plus seconds times 75 plus frames, less the 150 frames of
   lead-in that Red Book addressing puts before logical sector 0.

   The result is signed and is not clamped.  00:02:00 is sector 0, and an
   address below it -- 00:00:00 among them, which is what an unanswered driver
   query leaves in the reply block -- comes back negative.  Both callers store
   what they get straight into a dword global without looking at the sign.

   Writing the arithmetic without the 150, as a plain frame count, gives every
   caller a sector number 150 too large. */
extern int fdps_cd_msf_to_sector(unsigned int msf_packed);
#pragma aux fdps_cd_msf_to_sector "*" parm caller [];

/* 00069e0f.  The seven-byte packed UPC/EAN field the CD-ROM driver answered
   the UPC Code control block with, all zero when the driver reported that the
   disc carries no media catalog number.  fdps_cdrom_read_upc writes it and
   nothing in the image ever reads it, so it is a published fact with no
   consumer; it is declared here rather than in gamedata.h for that reason. */
extern unsigned char data_fdps_cd_media_catalog_number[7];

/* 0003bec1.  Asks the CD-ROM driver for the disc's media catalog number --
   MSCDEX IOCTL Input, control block 0Eh UPC Code -- and publishes the seven
   packed UPC/EAN bytes it gets back in data_fdps_cd_media_catalog_number.

   Takes nothing: the drive it asks and the two DOS blocks it stages through
   are the module's globals.  Returns 1 always, so the return value cannot tell
   a refused request from an accepted one; what the driver said about the
   request is in data_fdps_cd_last_request_status, whose bit 15 is error, and
   this routine records that word without testing it.  The catalog number is
   therefore rewritten on every call, including one the driver refused.

   The control block's CONTROL/ADR byte goes out preset to 2 and is tested for
   zero on the way back, so the "no catalog number" case is one the driver has
   to assert; sending that byte as zero instead would make every drive that
   leaves it alone look like a disc without a catalog number.  Nothing in the
   image calls this function. */
extern int fdps_cdrom_read_upc(void);
#pragma aux fdps_cdrom_read_upc "*" parm caller [];

#endif
