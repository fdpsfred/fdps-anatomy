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

/* 0003c892.  Plays a run of consecutive CD-DA tracks as one continuous range:
   stops whatever the drive is doing, resolves where first_track begins and
   where last_track ends, and issues a single Play Audio request spanning the
   two.

   first_track and last_track are 1-based CD track numbers in the same
   numbering fdps_cd_play_track and fdps_cdrom_read_track_info take, both read
   as signed words.  last_track is played in full, since what bounds the range
   is the sector its own track ends at.  Neither is checked against the disc and
   neither is checked against the other: a number past the disc's highest track
   ends at the lead-out, and a last_track below first_track produces a range
   that runs backwards, which fdps_cd_play_audio_range hands to the driver as a
   sector count near 2^32.

   Returns the length of the range in CD sectors -- the end sector less the
   start sector, and therefore negative for a range that runs backwards.  It
   reports nothing about the drive: playback is asynchronous and no request
   status is looked at, so this returns as soon as the driver has taken the play
   request.  Like fdps_cd_play_track it leaves the track-info globals naming
   last_track and the play-range globals holding last_track's own range, which
   is not the range that was sent.

   Nothing in the image calls it.  A sweep for the entry address -- xrefs, an
   operand search over all 89,420 instructions and a byte search for the
   little-endian pointer -- finds it referenced from nowhere, so no caller pins
   its behaviour further. */
extern int fdps_cd_play_track_range(short first_track, short last_track);
#pragma aux fdps_cd_play_track_range "*" parm caller [];

/* 0003c8e6.  Plays the whole CD as audio: stops whatever the drive is doing,
   refreshes the disc summary and issues one Play Audio request that starts at
   the disc's very first sector and runs for as many sectors as the lead-out
   sits at.

   Takes nothing and returns nothing.  It reports nothing about the drive
   either: playback is asynchronous and no request status is looked at, so this
   returns as soon as the driver has taken the play request.  It leaves the
   track-info globals naming track 1, which the query it makes and discards puts
   there, and data_fdps_cd_leadout_sector holding the summary it just refreshed;
   the play-range globals fdps_cd_play_track publishes are not touched, because
   no fdps_cd_resolve_track_range call is made here.

   The range begins at sector 0 and not where track 1's audio begins, so on this
   game's mixed-mode disc the data track is inside what is sent to the driver.
   Nothing bounds the range at the other end either: on a drive that refuses the
   summary query the lead-out reads as the bit pattern of -150 and the request
   goes out with a sector count near 2^32.

   Nothing in the image calls it.  A sweep for the entry address -- xrefs and an
   operand search over all 89,420 instructions -- finds it referenced from
   nowhere, so no caller pins its behaviour further. */
extern void fdps_cd_play_whole_disc(void);
#pragma aux fdps_cd_play_whole_disc "*" parm caller [];

/* 00069e56.  The eleven-byte MSCDEX Audio Q-Channel control block that
   fdps_cd_read_audio_position stages its one request in: byte 0 carries the
   control code out to the driver and the driver's reply -- which CD-DA track
   is playing and how far into it the drive has got -- comes back over the
   whole block.

   It is a block this file owns rather than a buffer the caller supplies:
   0003c944 pushes its address as the literal 0x69e56 and the four loads that
   follow read it back absolutely.  Nothing else in the image touches it -- an
   operand sweep over all 89,420 instructions finds it named only from that
   one function, at the base and at +2, +4, +5 and +6 -- so a call overwrites
   whatever the previous one left in it and no other code can be reading it in
   between.

   Declared as the record and not as eleven loose bytes because fdpstype.h owns
   that layout and the offsets are what decide which byte means what. */
extern struct fdps_cd_q_channel_block data_fdps_cd_q_channel_block;

/* 0003c93a.  Reads how far CD-DA playback has got: asks the drive for the
   audio Q-channel through fdps_cd_read_q_channel and hands back the track
   playing now together with the running time inside that track.

   track receives the Q-channel TNO field, the number of the track playing now.
   minute, second and frame receive the running time within that track --
   seconds 0 to 59 and frames 0 to 74, a frame being 1/75 second.  That triple
   is the position inside the track; the driver also reports the absolute
   position on the disc, at block offsets 8 to 10, and this routine never looks
   at it.

   All four are written on every call and there is no branch and no error path
   in the body.  The reply is staged in data_fdps_cd_q_channel_block, so a
   request the driver refused leaves the block holding whatever was in it
   before and those bytes are handed back as though they were an answer.  What
   distinguishes the two is data_fdps_cd_last_request_status, which the callee
   publishes and whose bit 15 is error; this routine returns nothing.

   The stores go out in the order track, frame, second, minute, which is
   observable: nothing stops a caller passing one address twice, and then the
   last store is the one that survives.

   Nothing in the image calls it.  A sweep for the entry address -- xrefs and
   an operand search over all 89,420 instructions -- finds it referenced from
   nowhere, so no caller pins its behaviour further. */
extern void fdps_cd_read_audio_position(unsigned char *track,
                                        unsigned char *minute,
                                        unsigned char *second,
                                        unsigned char *frame);
#pragma aux fdps_cd_read_audio_position "*" parm caller [];

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

/* 00060170.  How many timer ticks fdps_cd_music_repeat_poll has seen since it
   last asked the drive anything.  It is stepped once per tick rather than once
   per call -- the poll's own tick latch is above it -- and it is the throttle
   that keeps fdps_cd_audio_is_idle's real MSCDEX device request off the
   per-frame path: the drive is asked at 0x4b of them and the counter is then
   cleared, whichever of the poll's arms was taken.

   Signed, matching the dword accesses: the three in the image are an INC, a
   CMP against 0x4b decided by JNZ and a store of 0, so no compare here is
   sign-sensitive and nothing reads it as a byte.  Nothing outside
   fdps_cd_music_repeat_poll touches it, and the image ships it holding 0. */
extern int data_fdps_audio_cd_repeat_tick_counter;

/* 00069d74.  The value of data_fdps_timer_tick_counter that
   fdps_cd_music_repeat_poll finished its last pass on, which is what makes
   that poll run at most once per tick however often a game loop calls it.

   Unsigned, matching the counter it is a copy of, and compared for equality
   only -- CMP EAX,dword ptr [0x00069d64] / JZ -- so a wrapped tick counter
   costs it nothing.  Not volatile: the timer interrupt writes the counter,
   never this latch, and the poll is the only reader and the only writer in the
   image. */
extern unsigned int data_fdps_audio_cd_repeat_last_tick;

/* 00030c50.  The background music's keep-alive, called by every modal game
   loop on every pass: once every 0x4b timer ticks it asks the drive whether
   CD-DA playback has finished and, if it has, plays the selected track again.
   A CD-DA track plays once and stops, so this poll is what makes the music
   loop.

   Takes nothing and returns nothing; everything it reads and writes is a
   global, and it reports neither what the drive said nor whether it played
   anything.  It is safe to call as often as a loop likes -- the tick latch
   data_fdps_audio_cd_repeat_last_tick makes a second call in the same tick do
   nothing at all -- but not free: on the pass that reaches the drive it costs
   one MSCDEX device request, and four more if it restarts the track.

   Nothing is restarted while data_fdps_audio_cd_current_music_index is -1 or
   data_fdps_audio_bgm_enabled_flag is clear, and both are tested on every
   firing rather than latched, so switching the music off stops the restarts
   from the next firing on. */
extern void fdps_cd_music_repeat_poll(void);
#pragma aux fdps_cd_music_repeat_poll "*" parm caller [];

/* 00030cc0.  Makes sure the disc the chapter needs is in the drive and then
   starts that chapter's music: it stops whatever is playing, waits -- with no
   timeout and no way out but the right disc -- until "%s\Pack.vfs" is
   readable and the Pass.Dat member inside it names the matching disc, and then
   publishes the chapter's track in data_fdps_audio_cd_current_music_index and
   sends it to the drive.

   chapter is the chapter index, and it is what decides the disc as well as the
   track: 0 to 17 need disc 1 and 18 upwards need disc 2
   (resource_info/disc_images.md).  Only chapters 0 to 29 have a row in the
   table, and nothing here range-checks the index -- a chapter past the end
   reads the frame beyond the table.  track_slot picks one of that chapter's
   two entries; four of the five call sites pass 0, and fdps_battle_advance_turn
   passes 1 at 0001e5db before passing 0 at 0001e620.

   IT IS MODAL AND IT DRAWS.  While the wrong disc is in, it opens the message
   panel, waits on getch and holds for twenty seconds after each keypress, so a
   caller has to be somewhere the panel may appear and the game may stop.  It
   also takes the keyboard hook down for the duration and puts it back on the
   way out, which is what makes getch read through DOS while it waits.

   Returns nothing.  With music switched off it publishes -1 and stops the
   drive whatever the chapter's table entry says, exactly as
   fdps_cd_set_music_track does with its argument. */
extern void fdps_cd_verify_disc_and_play_track(int chapter, int track_slot);
#pragma aux fdps_cd_verify_disc_and_play_track "*" parm caller [];

#endif
