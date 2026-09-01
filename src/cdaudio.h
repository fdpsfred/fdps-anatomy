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

/* 0003c452.  Starts CD-DA playback over one range of disc sectors -- MSCDEX
   device command 0x84, Play Audio -- in the 22-byte request shape that command
   uses, with the addressing-mode byte 0 that makes its sector fields HSG
   logical sectors rather than packed Red Book addresses.

   start_sector is the first sector to play, in the units fdps_cd_msf_to_sector
   produces.  end_sector is one past the last: what goes out to the drive is
   the count end_sector - start_sector, as a full 32 bits.  Nothing
   range-checks either of them and nothing keeps end_sector above start_sector,
   so a reversed pair is handed to the driver as a count near 2^32.  Both
   callers take the pair from data_fdps_cd_play_range_start_sector and
   data_fdps_cd_play_range_end_sector, which fdps_cd_resolve_track_range fills
   in from the table of contents.

   Playback is asynchronous.  The request finishes as soon as the driver has
   accepted it, and the range then plays on until it runs out or a stop command
   arrives; fdps_cd_audio_is_idle is what tells the game it has ended.

   Returns nothing.  Whether the drive accepted the request is visible only in
   data_fdps_cd_last_request_status, which this leaves holding the status word
   the driver wrote into the request header -- bit 15 is error. */
extern void fdps_cd_play_audio_range(unsigned int start_sector,
                                     unsigned int end_sector);
#pragma aux fdps_cd_play_audio_range "*" parm caller [];

/* 0003c4a7.  Stops CD-DA playback -- MSCDEX device command 0x85, Stop Audio --
   in the bare 13-byte device request header, the shortest request the module
   builds.  The command takes no parameters: there is no addressing mode, no
   sector and no range, so the drive is told to stop wherever it currently is.

   Takes nothing and returns nothing.  Every one of the eight callers -- main,
   fdps_title_screen, fdps_play_movie, fdps_cd_set_music_track,
   fdps_cd_verify_disc_and_play_track, fdps_cd_play_track,
   fdps_cd_play_track_range and fdps_cd_play_whole_disc -- calls it
   unconditionally and moves straight on, so it is safe to issue when nothing
   is playing and the game never learns whether the drive obeyed.  Whether the
   drive accepted the request is visible only in
   data_fdps_cd_last_request_status, which this leaves holding the status word
   the driver wrote into the request header -- bit 15 is error. */
extern void fdps_cd_stop_audio(void);
#pragma aux fdps_cd_stop_audio "*" parm caller [];

/* 0003c4ff.  Resumes CD-DA playback the drive was told to hold -- MSCDEX
   device command 0x88, Resume Audio Play -- in the same bare 13-byte device
   request header the stop command uses, and with the same three stores.  The
   command takes no parameters: playback picks up where the pause left it.

   Takes nothing and returns nothing.  Nothing in the image calls it, so no
   caller pins its behaviour.  Whether the drive accepted the request is
   visible only in data_fdps_cd_last_request_status, which this leaves holding
   the status word the driver wrote into the request header -- bit 15 is
   error. */
extern void fdps_cd_resume_audio(void);
#pragma aux fdps_cd_resume_audio "*" parm caller [];

#endif
