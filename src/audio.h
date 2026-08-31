/* audio.h -- digital sound effects through the Miles AIL library.
 *
 * The game plays sound effects with AIL only; music comes off the CD
 * (program_info/cd_audio.md).  A sound effect is not a loose file: every one
 * of them is embedded in the .SAF animation that plays it, as an item of that
 * file's fourth section (resource_info/saf.md), so the caller hands a loaded
 * .SAF image over together with the item number the frame script asked for.
 *
 * The vendor side of the contract -- the AIL entry points, and the clobber set
 * every one of them has to be declared with -- is libs/ailv3/ailv3.h, and the
 * link-time side is rebuild_info/ail_link.md.
 */
#ifndef AUDIO_H
#define AUDIO_H

/* 00069d30.  The eight sample handles the sound-effect mixer owns, allocated
   once at start-up.  Eight is the array's real extent, not a walk bound picked
   by a caller: every walk over it in the program runs 0..7 and there is a
   separate global at 00069d50 immediately after it. */
#define SFX_SAMPLE_SLOT_COUNT 8
extern void *data_fdps_audio_sample_handle_table[SFX_SAMPLE_SLOT_COUNT];

/* The slot argument that means "every slot" rather than one of them, tested as
   an equality against -1.  It is the only value either caller of
   fdps_audio_stop_sample ever passes. */
#define SFX_STOP_ALL_SLOTS (-1)

/* The answer a voice allocator gives when it has no slot to give: not an index
   into the table above, and the only negative value any of them returns. */
#define SFX_NO_SAMPLE_SLOT (-1)

/* 00069d50.  The AIL timer slot the audio service runs on, as
   AIL_register_timer handed it back, or -1 when the library had no slot to
   give.  It is a record of the registration and not a handle the rest of the
   game passes around: the image holds four references to it and all four are
   inside fdps_audio_timer_install, so nothing stops, re-programs or releases
   the timer by name -- AIL_shutdown is what lets it go.  It sits immediately
   after the eight-entry sample table above, and no walk over that table
   reaches it. */
extern int data_fdps_audio_timer_handle;

/* The answer AIL_register_timer gives when its timer table is full.  Every
   slot of that table is in use, so there is nothing to register on; the
   library's own allocator at 00044f4e returns it after walking all fifteen
   slots. */
#define AIL_TIMER_REGISTER_FAILED (-1)

/* 00069d5c.  The rate, in samples per second, that fdps_audio_start_sample
   hands AIL for every block it starts.  It is a single global rather than a
   per-clip figure because that function is given raw PCM and no header to read
   a rate out of; fdps_audio_set_sample_playback_rate@00030810 is what writes
   it.  Nothing in the shipped image calls either of the two, so the slot holds
   its BSS zero for the whole of a real session. */
extern int data_fdps_audio_sample_playback_rate;

/* 00069d71.  Set when the DIG driver installed successfully at start-up, so
   that "the user wants sound effects" (data_fdps_audio_sfx_enabled_flag, in
   gamedata.h) and "there is anything to play them on" stay separate answers.
   Read as a boolean; nothing compares it against a particular value. */
extern unsigned char data_fdps_audio_sfx_driver_available_flag;

/* 00069d72.  The music-side twin of the flag above, set when the MDI driver
   installed at start-up.  It is write-only state: both of its references in
   the image are the two stores in fdps_audio_init, so nothing in the game ever
   asks whether a music driver is there.  Music comes off the CD
   (program_info/cd_audio.md), which is why the answer is never wanted. */
extern unsigned char data_fdps_audio_bgm_driver_available_flag;

/* 00069d6c.  The MDI driver handle AIL_install_MDI_INI answered with, or NULL
   when no MDI.INI could be read.  All three of its references are inside
   fdps_audio_init -- the store, the test that guards the sequence allocation,
   and the argument of that allocation -- so it is never used to play anything
   and never handed to an uninstall call. */
extern void *data_fdps_audio_bgm_driver_handle;

/* 00069d60.  The one sequence handle the music driver is asked for at
   start-up.  The image holds exactly one reference to it, the store in
   fdps_audio_init, so nothing is ever loaded into it or started on it. */
extern void *data_fdps_audio_bgm_sequence_handle;

/* 00069d68.  The DIG driver handle AIL_install_DIG_INI answered with, or NULL
   when no DIG.INI could be read or the driver it names found no hardware.  It
   is what the eight sample handles are allocated from, and all three of its
   references are inside fdps_audio_init: the rest of the file works with the
   sample handles instead, and AIL_shutdown is what releases the driver. */
extern void *data_fdps_audio_sfx_dig_driver_handle;

/* Brings the Miles audio stack up and puts the game's clock on the air:
   AIL_startup, then each of the two drivers installed from its .INI, then the
   timer at `tick_rate_hz` ticks a second.  Both call sites pass 25.

   Both driver halves are optional and independent.  A driver that did not
   install leaves its available-flag at 0 and its handles untouched, and the
   timer is installed either way, so the game runs -- silently -- on a machine
   with no sound card.  Sound effects are switched on unconditionally here,
   whatever the last session's saved options said.

   The function itself releases nothing and has no "already up" test, so it is
   a bring-up and not an idempotent reset: pairing it with a teardown is the
   caller's job.  Both callers do pair it.  main@00029220 opens the session
   with it and closes with fdps_audio_shutdown, and fdps_play_movie@00030f40
   calls AIL_shutdown before it hands the machine to the external movie player
   and this on the way back, so the second bring-up of a session always follows
   a shutdown. */
extern void fdps_audio_init(int tick_rate_hz);
#pragma aux fdps_audio_init "*" parm caller [];

/* Plays item `sound_index` of the sound section of the loaded .SAF image at
   `saf` on the first of the eight sample handles that is not already playing.
   Silently does nothing when either audio flag is clear, when sound_index is
   outside the image's sound section, or when all eight handles are busy: a
   dropped effect is normal operation, not an error.  The image is only read;
   the handles and the AIL mixer state are what change. */
extern void fdps_sfx_play(void *saf, int sound_index);
#pragma aux fdps_sfx_play "*" parm caller [];

/* Tears the Miles audio stack down on the way out of the game, by calling
   AIL_shutdown and doing nothing else.  It is the counterpart of the
   AIL_startup that opens the audio system, and the two have to be paired: the
   real-mode timer vector AIL_shutdown puts back is the one AIL_startup saved,
   so shutting down a stack that was never started restores a vector that was
   never taken.  Neither the sample handles nor the two audio flags are
   touched, and the DIG driver handle is never uninstalled by name -- AIL
   releases its own driver slots. */
extern void fdps_audio_shutdown(void);
#pragma aux fdps_audio_shutdown "*" parm caller [];

/* Silences one of the eight sample slots, or every one of them when
   sample_index is SFX_STOP_ALL_SLOTS.  Any other value is used as an index
   with no bounds check of any kind; both callers in the game pass the sentinel,
   as the audio-cleanup step at the end of a battle animation.  Neither audio
   flag is consulted, so a slot is handed to AIL whether or not a driver ever
   installed, and a slot that is not playing is left as it stands -- deciding
   that is the vendor's job, not this function's. */
extern void fdps_audio_stop_sample(int sample_index);
#pragma aux fdps_audio_stop_sample "*" parm caller [];

/* Starts the raw PCM block at `pcm_data`, `pcm_len` bytes long, on the first of
   the eight sample handles that is not already playing, and answers with the
   slot it used or SFX_NO_SAMPLE_SLOT when it started nothing -- the DIG driver
   is absent, sound effects are switched off, or all eight voices are busy.
   `loop_count` goes to AIL unchanged: 1 plays the block once, 0 loops it for
   ever.

   Unlike fdps_sfx_play this one is handed the samples themselves, not a
   container to find them in: no header of any kind is parsed, so neither the
   format nor the volume is set and the block plays in whatever the handle was
   left holding.  The rate is the global above rather than anything derived from
   the block.  Nothing in the shipped image calls it. */
extern int fdps_audio_start_sample(void *pcm_data, unsigned int pcm_len,
                                   unsigned int loop_count);
#pragma aux fdps_audio_start_sample "*" parm caller [];

/* Answers 1 while the voice in slot `sample_index` is still playing and 0 for
   every other AIL status, so that a caller can hold a battle animation until
   the sound effect it started has run out.  Both callers in the game spin on
   it, and nothing else in the program is read or written.

   The slot number is not range-checked and SFX_NO_SAMPLE_SLOT is a value the
   callers really pass, because it is what fdps_audio_start_wav answers with
   when it started nothing.  In FDPS.LE that index reaches BSS padding that no
   instruction in the image ever writes, so the handle it fetches is null, and
   AIL answers 0 for a null handle -- which is what lets the waiting caller
   stop waiting for a sound that was never started. */
extern int fdps_audio_sample_is_playing(int sample_index);
#pragma aux fdps_audio_sample_is_playing "*" parm caller [];

/* The timer tick callback.  Nothing in the game calls it by name: its address
   is handed to AIL_register_timer by fdps_audio_timer_install@000307b0, and
   from then on AIL's timer interrupt calls it at the registered frequency.  It
   advances data_fdps_timer_tick_counter (gamedata.h) by one -- it is the only
   writer of the game's clock -- and draws one rand() to churn the CRT's
   generator, discarding the value.  It reads no argument, touches neither
   audio flag nor any sample handle, and cannot fail.

   It is declared here so that the installer can take its address; a caller
   that invokes it directly would be doing something the original never does. */
extern void fdps_timer_tick_handler(void);
#pragma aux fdps_timer_tick_handler "*" parm caller [];

/* Puts the game's clock on the air: hands AIL the address of the tick handler
   above, asks for `tick_rate_hz` ticks a second and starts the timer.  Both
   call sites reach it through fdps_audio_init with 25 (PUSH 0x19 at 0002930c
   and at 00031026), so a tick is 40 ms, and every wait, animation step and
   blink the game counts is counted in those.

   Neither audio flag is consulted and the DIG driver is not required: the
   caller reaches this on the way out whether or not the driver installed, so
   the clock runs on a machine with no sound card.  The one piece of state it
   leaves behind is data_fdps_audio_timer_handle.

   A registration that fails is reported on stdout and then carried on with --
   the failed handle goes to AIL_set_timer_frequency and AIL_start_timer just
   as a good one would, and the function returns normally either way. */
extern void fdps_audio_timer_install(int tick_rate_hz);
#pragma aux fdps_audio_timer_install "*" parm caller [];

#endif
