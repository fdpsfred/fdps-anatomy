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

#endif
