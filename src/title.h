/* title.h -- the front-of-house screens: the title menu and its demo, the
 * game-over screen, and the full-screen FMV playback all of them reach for
 * (rebuild_info/code_layout.md).  The ending is ending.h's, and it reaches for
 * the same movie player.
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

/* 0002a960.  Runs the whole game-over screen and returns when the player has
   pressed a key.  main@00029220 is the only caller, and it carries on
   afterwards, so this is a pause and not a way out of the program.

   Whatever is on the adapter when the call is made is the backdrop: the
   320x200 frame is copied out of 0xa0000 before anything is drawn, and every
   frame of what follows is composed over that copy on a private 368x248 page
   with the picture drawn at (24, 24), the same page geometry saf.h's player
   uses.  Only the page's 320x200 window is ever put on the adapter.

   GameOver.saf is pulled out of MISC.VFS here and released here.  It plays
   once, FROM FRAME 1 -- the cursor is seeded directly rather than reset
   through fdps_saf_advance_tick, so frame 0 is skipped by the animation --
   paced one drawn frame per change of the timer tick counter and presented on
   the vertical retrace.  The clip is 31 frames of two ticks each, so the
   animation stands for a little over three seconds.

   Then sixteen fade steps, on the same pacing.  At step n the backdrop is
   redrawn from the untouched snapshot tinted toward palette colour 0x6f at
   alpha n, entry 0 of the same .SAF -- the still game-over picture -- is
   composited over it at translucency level 16 - n, and entry 31 is drawn
   opaque on top of both.  So the backdrop drains away as the picture comes up,
   the sign over it never fades, and the step counter never reaches 16: the
   last step is alpha 15 against level 1, not a fully tinted backdrop under a
   fully solid picture.

   The finished picture is LEFT STANDING on the adapter.  Nothing here clears
   the frame or touches the DAC, so the caller inherits the last fade step and
   draws over it.  The keyboard ring is emptied and then waited on, which is
   what holds the screen; a key pressed during the animation or the fade does
   not dismiss it.

   Reads data_fdps_timer_tick_counter for its pacing and the two palette
   blending tables, data_fdps_palette_shade_ramp_table and
   data_fdps_inverse_palette_cube, for the tint and the translucency; writes no
   global at all.  Its three allocations -- the snapshot, the .SAF and the page
   -- are all released before it waits. */
extern void fdps_show_game_over(void);
#pragma aux fdps_show_game_over "*" parm caller [];

/* 0002ac10.  Runs the title screen's attract-mode demo and returns when it is
   over.  The idle timer in fdps_title_screen is the only caller and it passes
   nothing: the demo takes no argument, reports nothing, and everything it does
   is to the game-state globals and to the screen.

   What it stages is a showcase party that cannot lose.  The chapter global is
   set to 25 -- the map the demo battle is fought on -- the roster is emptied
   and twelve characters are enrolled into it in a fixed order, and the chapter
   state is then rebuilt on top of that roster, so the map's twelve player slots
   come out holding those twelve characters in enrol order.  Every one of the
   twelve is then given 2000 HP, 800 MP, 800 attack and 400 hit, put on the
   guest/NPC side 1 rather than the player's own side 2 -- which is what takes
   them off player control and is forwarded as the acting side below -- and
   reset to behaviour 0; one of them is moved to the left edge of its row;
   and two of them are handed a spell they would not otherwise know.  Defense
   and evade are NOT written -- the twelve keep whatever
   fdps_roster_add_character derived for them.

   Then it drives the map AI: two passes over the whole unit array, each actor
   through fdps_map_actor_behavior_step unless it has retired, with the acted-
   this-turn flags cleared at the end of every pass.  A make code in the
   scancode ring ends the demo at the actor it is noticed on -- the pass counter
   is set to 1 rather than to 0, so the pass still finishes its turn reset
   before the loop leaves.

   ON THE WAY OUT IT FREES THE ROSTER BLOCK AND DOES NOT REPLACE IT.
   fdps_load_global_resources allocates that block once at startup and this is
   the only other free of it in the image, so every roster access after a demo
   has run goes through a dangling pointer.  That is the original's behaviour
   and it is not to be tidied (rebuild_info/pitfalls.md).  The member count is
   put back to 0, the terrain panel is switched back on, the mode 13h frame is
   blanked and the scancode ring is flushed.

   Writes data_fdps_chapter_current_chapter_id, data_fdps_ui_play_active_flag,
   data_fdps_roster_member_count and data_fdps_map_cursor_draw_mode, reads
   data_fdps_map_unit_count and data_fdps_roster_array_ptr, and publishes no
   global of its own. */
extern void fdps_title_demo(void);
#pragma aux fdps_title_demo "*" parm caller [];

#endif
