/* title.h -- the front-of-house screens: the title menu and its demo, the
 * game-over screen, the ending credit roll, and the full-screen FMV playback
 * all three of them reach for (rebuild_info/code_layout.md).
 *
 * Nothing here is part of a battle or a village; these are the screens that
 * stand between them.  The globals this layer reads -- the master palette and
 * the CD path prefix among them -- belong to gamedata.h, not to this file.
 */
#ifndef TITLE_H
#define TITLE_H

/* 00030f40.  Hands the machine to the external movie player for one full-screen
   FMV and takes it back afterwards.  `movie_name` is the stem of the pair of
   stream files on the CD, without a directory and without an extension: the
   two paths are composed as "<cd prefix>\<movie_name>.Vid" and
   "<cd prefix>\<movie_name>.Aud", and the player itself is "<cd prefix>\fd.exe".
   Both call sites pass a stem of three characters -- fdps_title_screen formats
   "FD%d" into a stack buffer, fdps_play_ending_credit_roll passes the literal
   "End" -- against a prefix that is a bare drive letter, so the composed paths
   are ten characters and the three 20-byte buffers the original gives them are
   not near full.  Nothing here bounds them.

   Going out, the machine is quiesced in a fixed order: the Miles audio stack is
   shut down, the game's keyboard hook comes off vector 09h, whatever the player
   typed while the previous screen was up is drained through the CRT's own
   kbhit/getch pair, the CD is told to stop any audio track it is playing, and
   the DAC is faded flat to black by an upload of the master palette biased -64
   on all three channels -- one step, not a ramp, because the picture is about
   to be replaced wholesale.  Then fd.exe is spawned with P_WAIT and the two
   stream paths as its arguments, so this call does not return until the movie
   has played out.

   Coming back, the mode 13h frame is cleared to colour 0, the DAC is put back
   from the same master palette unbiased, the keyboard hook goes back on vector
   09h and the audio stack is brought up again at 25 ticks a second.  The
   keyboard hook and the audio stack are therefore left as they were found, and
   the screen and the DAC are not: the caller gets a black screen with the
   normal palette on it, which is what every caller draws over next.

   A spawn that fails is not distinguished from a movie that played: the result
   is not looked at, and the whole bring-up above runs either way.  That is what
   makes a missing fd.exe or a missing stream a black screen and a moment's
   pause rather than a hang or a message.

   Reads data_fdps_vga_main_palette_ptr and data_fdps_cdrom_path and writes
   neither; every other effect is on the hardware, on the DOS process and on
   the two subsystems named above. */
extern void fdps_play_movie(char *movie_name);
#pragma aux fdps_play_movie "*" parm caller [];

#endif
