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

/* 0003c5a6.  Asks the drive where it is in the CD-DA track it is playing --
   MSCDEX IOCTL Input, device command 3, control block code 0x0c, Audio
   Q-Channel Info -- and leaves the driver's answer in the caller's own block.

   q_channel points at an eleven-byte struct fdps_cd_q_channel_block the caller
   owns.  Its control_code byte is stamped with 0x0c on the way in; every other
   field comes back filled by the driver, of which the caller wants
   track_number, the track now playing, and minute / second / frame, how far
   into that track the head has got.  The request itself declares a transfer of
   only six bytes even though eleven are staged and eleven read back, so the
   frame byte at offset 6 is one past what the drive was asked for -- see the
   note in cdaudio.c.  The only caller, fdps_cd_read_audio_position, passes the
   game's own fixed block.

   Nothing here validates the reply.  A refused request leaves the block holding
   the bytes that were sent, so a caller that treats the answer as live will
   read its own question back as a position; whether the drive answered at all
   is visible only in data_fdps_cd_last_request_status, where bit 15 is the
   driver's error flag.

   Returns nothing. */
extern void fdps_cd_read_q_channel(struct fdps_cd_q_channel_block *q_channel);
#pragma aux fdps_cd_read_q_channel "*" parm caller [];

/* 0003c6e8.  Says whether CD-DA playback has finished: issues an MSCDEX Device
   Status request through fdps_cdrom_read_device_status and returns
   fdps_cd_status_is_not_busy's reading of the status word that request leaves
   in data_fdps_cd_last_request_status -- 1 when the request header's busy bit
   0x0200 is clear, 0 when it is still set.

   Takes nothing; the drive and the staging blocks are the module's globals.
   The order is the point: the request goes out first so that the word the
   predicate reads is this call's own and not the previous request's.

   Every call costs a real device request, so it is not a free poll.  Its one
   caller, fdps_cd_music_repeat_poll, asks once every 0x4b ticks and takes a 1
   as the cue to issue its next play command.  The answer is only meaningful
   where a
   driver answered: a request that never reached one leaves the status word
   holding uninitialised frame bytes, and a request the driver refused sets its
   error bit 0x8000 without necessarily setting the busy bit, which this does
   not distinguish from idle. */
extern unsigned short fdps_cd_audio_is_idle(void);
#pragma aux fdps_cd_audio_is_idle "*" parm caller [];

/* 00069dec.  The first disc sector of the range the next Play Audio request
   will name, published by fdps_cd_resolve_track_range out of
   data_fdps_cd_track_start_sector.  Both readers, fdps_cd_play_track at
   0003c883 and fdps_cd_play_track_range at 0003c8b6, take it straight into
   fdps_cd_play_audio_range's first argument.

   Unsigned, matching the sector globals it is copied from and the argument it
   is handed to; a track whose start address the driver never answered for
   leaves 0xffffff6a here, the -150 of an all-zero Red Book address. */
extern unsigned int data_fdps_cd_play_range_start_sector;

/* 00069de4.  One past the last disc sector of that range -- the sector the
   next track starts at, or the lead-out for the last track on the disc.
   fdps_cd_play_audio_range sends the difference of the pair as its sector
   count, so this end is exclusive.

   Unsigned for the same reason as the start sector.  It sits two globals below
   the start in the original image, with data_fdps_cd_request_header_buffer
   between them; nothing indexes across the three, so the rebuild is free to
   place them wherever the linker likes (contract B). */
extern unsigned int data_fdps_cd_play_range_end_sector;

/* 0003c803.  Resolves the disc sector range of the track the CD layer is
   currently pointed at and publishes it in the two globals above, ready for
   fdps_cd_play_audio_range.

   Takes nothing: the track acted on is whichever one
   fdps_cdrom_read_track_info queried last, named by
   data_fdps_cd_track_info_track_number, and the range start is that query's
   own data_fdps_cd_track_start_sector.  Both callers,
   fdps_cd_play_track and fdps_cd_play_track_range, issue that query
   immediately before calling in.

   The range end is where the next track starts, which costs a query of its
   own, except for the last track on the disc -- and for any track number past
   the disc's highest, which nothing here refuses -- where it is
   data_fdps_cd_leadout_sector and no query is made.  The track-info globals
   are left naming the track the caller had selected: the non-last-track path
   re-queries the original track on the way out, so it costs two device
   requests where the last-track path costs none.

   Returns nothing, and reports nothing about the disc.  A track whose query no
   driver answered resolves to a range built out of that query's undefined
   reply, which the caller cannot tell from a real one. */
extern void fdps_cd_resolve_track_range(void);
#pragma aux fdps_cd_resolve_track_range "*" parm caller [];

/* 0003c85b.  Plays one CD-DA track whole: stops whatever the drive is doing,
   points the CD layer's track-info globals at the requested track, resolves
   that track's sector range and issues the Play Audio request for it.

   track is a signed word -- the only read of the argument is a MOVSX of
   [ESP+4] -- and nothing along the chain range checks it: a number past the
   disc's highest track is queried for anyway and comes back with whatever the
   driver answers, which fdps_cd_resolve_track_range then ends at the lead-out.
   The three callers, fdps_cd_set_music_track, fdps_cd_music_repeat_poll and
   fdps_cd_verify_disc_and_play_track, all pass a small positive track number.

   Returns nothing, and reports nothing: every callee is void and none of the
   four requests it costs is tested.  Playback is asynchronous, so this returns
   as soon as the driver has taken the play request and the track then runs on
   until it ends or a stop arrives; fdps_cd_audio_is_idle is what says it has
   finished.  It also leaves the track-info globals naming the requested track
   and the play-range globals holding the range that was sent. */
extern void fdps_cd_play_track(short track);
#pragma aux fdps_cd_play_track "*" parm caller [];

/* 00030bf0.  Sets the background music: settles which music the game should be
   playing, publishes that in data_fdps_audio_cd_current_music_index and makes
   the drive match it -- playing the corresponding CD track, or stopping the
   drive when the answer is "none".

   music_index is the game's own 0-based music index, or -1 for silence.  The
   CD track is one more than it, which is where the +1 in the body comes from;
   the disc's track 1 is the data track.  The six call sites pass 1
   (fdps_title_screen at 0002a572 and fdps_play_ending_credit_roll at
   0001ba53), 2 and -1 (fdps_run_village_phase) and a value the IconAni script
   opcode carried (fdps_icon_script_run at 000219d7).  A full dword goes on the
   stack at every one of them and the body stores it as a dword, so the index
   travels as an int even though fdps_cd_play_track fetches the track number
   below it as a word.

   The music-enabled setting data_fdps_audio_bgm_enabled_flag overrides the
   argument rather than short-circuiting around it: with music switched off
   this publishes -1 and stops the drive whatever the caller asked for; see
   the body's comment for why an early-out is not the same thing.

   Returns nothing, and reports nothing: neither callee reports anything and
   nothing here looks at the drive.  A music index whose track is not on the
   disc is sent down fdps_cd_play_track like any other. */
extern void fdps_cd_set_music_track(int music_index);
#pragma aux fdps_cd_set_music_track "*" parm caller [];

#endif
