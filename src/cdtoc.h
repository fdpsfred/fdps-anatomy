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

/* 00069e16.  The six reply bytes of the last Read Disk Info control block,
   copied out verbatim: lowest track, highest track, then the four bytes of the
   lead-out address.  Every field in it is also published separately in the
   globals below, so this is the undivided copy and nothing in the image reads
   it; it is declared here rather than in gamedata.h for that reason. */
extern unsigned char data_fdps_cd_disk_info_reply[6];

/* 00069e06.  The disc's first track number, as the CD-ROM driver reported it
   in the Read Disk Info reply.  fdps_cdrom_read_disk_info writes it and
   nothing in the image reads it -- the module's track walks start from
   data_fdps_cd_track_info_track_number instead -- so it is another published
   fact with no consumer.  Unsigned: a track number is a BCD-free byte count
   from 1, and the highest-track counterpart is compared with JNC. */
extern unsigned char data_fdps_cd_lowest_track_number;

/* 00069e08, 00069e09, 00069e0a.  The lead-out position as three separate
   fields, minute, second and frame, split out of the packed address in the
   Read Disk Info reply by fdps_cd_unpack_msf.  They are three adjacent bytes
   and the split writes them through three separate pointers, one byte each, so
   nothing here depends on them staying neighbours.  Nothing in the image reads
   them; what the rest of the module seeks with is the sector number in
   data_fdps_cd_leadout_sector. */
extern unsigned char data_fdps_cd_leadout_msf_minute;
extern unsigned char data_fdps_cd_leadout_second;
extern unsigned char data_fdps_cd_leadout_frame;

/* 0003bfa5.  Asks the CD-ROM driver for the disc's table-of-contents summary
   -- MSCDEX IOCTL Input, control block 0Ah Read Disk Info -- and publishes the
   first and last track numbers and the lead-out position, the last of these
   three times over: as the raw packed bytes, as minute/second/frame, and as
   the logical sector number the rest of the module seeks with.

   Takes nothing and returns nothing: the drive it asks and the two DOS blocks
   it stages through are the module's globals, and every answer leaves through
   a global too.  Whether the driver accepted the request is visible only in
   data_fdps_cd_last_request_status, whose bit 15 is error, and this routine
   records that word without testing it -- so every one of its outputs is
   rewritten on a refused request as well, and a refused request rewrites them
   with the zeroes the control block went out carrying.  That makes an
   unanswered query indistinguishable from a disc reporting track 0 to track 0,
   and it puts -150 -- 0x0a is cleared to 00:00:00, and 00:02:00 is sector 0 --
   into data_fdps_cd_leadout_sector, which is an unsigned global.

   Its four callers all discard the return value and read what they need out of
   the globals afterwards. */
extern void fdps_cdrom_read_disk_info(void);
#pragma aux fdps_cdrom_read_disk_info "*" parm caller [];

/* 00069e05.  The track control field of the track named in
   data_fdps_cd_track_info_track_number, as the Read Audio Track Info reply
   gave it and masked with 0xd0 on the way in.  Unsigned: its one reader,
   fdps_cd_track_is_audio at 0003c929, loads it with MOVZX.

   The mask keeps bit 6 -- the data-track bit -- along with bits 7 and 4, and
   drops bit 5, the copy-permitted bit.  fdps_cd_track_is_audio then compares
   the whole byte against 0x40 for equality rather than testing a bit, so what
   the mask leaves standing is the entire test. */
extern unsigned char data_fdps_cd_track_info_control_flags;

/* 0003c0c8.  Asks the CD-ROM driver where one track starts and what kind of
   track it is -- MSCDEX IOCTL Input, control block 0Bh Read Audio Track Info
   -- and publishes the answer in three globals beside the request's status
   word.

   The track number goes out as its low byte only and comes back into
   data_fdps_cd_track_info_track_number as its low sixteen bits, so the two are
   not the same field of the argument; nothing in the image passes a value
   where they differ.  Whether the driver accepted the request is visible only
   in data_fdps_cd_last_request_status, which this routine records without
   testing, so all three track globals are rewritten on a refused request too.

   Every one of its six callers discards nothing -- the function returns
   nothing -- and reads what it needs out of the globals afterwards. */
extern void fdps_cdrom_read_track_info(int track);
#pragma aux fdps_cdrom_read_track_info "*" parm caller [];

#endif
