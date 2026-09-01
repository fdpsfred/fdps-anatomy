/* cdaudio.h -- the CD-DA playback layer: the MSCDEX audio commands the game
 * drives the music with, and the track bookkeeping around them.
 *
 * cd.c owns the device request path every command here goes out through and
 * the DOS blocks they are staged in; cdtoc.c owns the table-of-contents
 * queries and the Red Book arithmetic that turns a track number into the
 * sector range these commands take.  This file is what actually tells the
 * drive to move, play and stop.
 *
 * The globals shared with the rest of the CD code --
 * data_fdps_cd_request_header_buffer and data_fdps_cd_last_request_status
 * among them -- are declared in gamedata.h, not here.
 */
#ifndef CDAUDIO_H
#define CDAUDIO_H

/* 0003c3fa.  Moves the drive head to one logical sector and transfers nothing
   -- MSCDEX device command 0x83, Seek -- in the 24-byte extended request
   header shape the CD-ROM commands use, with the addressing-mode byte 0 that
   makes the sector field an HSG logical sector rather than a packed Red Book
   address.

   sector is counted from the start of the disc in the units
   fdps_cd_msf_to_sector produces, minute*4500 + second*75 + frame - 150, and
   goes into the header as a full 32 bits.  Nothing range-checks it and nothing
   in the image calls this routine, so no caller pins it further.

   Returns nothing.  Whether the drive accepted the seek is visible only in
   data_fdps_cd_last_request_status, which this leaves holding the status word
   the driver wrote into the request header -- bit 15 is error. */
extern void fdps_cd_seek(unsigned int sector);
#pragma aux fdps_cd_seek "*" parm caller [];

#endif
