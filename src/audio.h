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

#endif
