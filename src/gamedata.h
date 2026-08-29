/* gamedata.h -- the externs of the game-state globals src/gamedata.c owns.
 *
 * A global that more than one file reads is defined in gamedata.c and declared
 * here, exactly once (rebuild_info/code_layout.md).  A file that needs one
 * includes this header; nobody writes a private extern of their own, because a
 * second copy of a declaration does not follow the definition when its type
 * changes and the linker says nothing.
 *
 * The definitions themselves arrive with ticket 23.  Until then the build
 * links a zero-filled stub for every name here that nothing defines yet.
 */
#ifndef GAMEDATA_H
#define GAMEDATA_H

/* 00060150.  How many unit records the current battle holds, and so the bound
   of every walk over the array below.  Signed: the loop compares are JL. */
extern int data_fdps_map_unit_count;

/* 00069cd8.  Base of the map's unit record array.  The stride is 0x50, the
   size of struct fdps_unit_record in src/fdpstype.h; the original types the
   base as a byte pointer and scales the index itself, so a reader casts. */
extern unsigned char *data_fdps_map_unit_array_ptr;

/* 00060144.  Base of the battle map's working movement grid: a four-byte
   header of two signed 16-bit dimensions followed by width*height two-byte
   cells (see src/movegrid.h).  Null until a chapter has been loaded, and the
   readers all test it before use.  The original types it as a byte pointer and
   does the header and cell arithmetic itself, so a reader casts. */
extern unsigned char *data_fdps_battle_move_grid_ptr;

/* 00069d64.  The free-running tick counter fdps_timer_tick_handler increments
   from the timer interrupt, and the game's only clock: 31 files read it, for
   animation pacing, input repeat and every "once per tick" guard.  Unsigned.
   The one reader emitted so far, fdps_cycle_ui_palette, latches a copy and
   tests it for equality rather than for order, so wrapping costs it nothing;
   whether that holds of the other readers is theirs to state.

   volatile is not decoration here.  The animations spin on this counter
   waiting for it to move, and nothing inside those loops writes it -- an
   optimiser is entitled to hoist the load out and the game stops dead at the
   first wait (rebuild_info/pitfalls.md).  It is qualified at the one
   declaration rather than at each spinning reader so that no reader has to
   remember; ticket 23's definition has to carry the same qualifier. */
extern volatile unsigned int data_fdps_timer_tick_counter;

/* 00069d70.  The player's sound-effect toggle, one of the four option flags
   the options menu writes and a save file carries (the sfx_enabled_flag field
   of struct fdps_save_slot).  Read as a boolean; it says what the player
   asked for, not whether a driver was found -- that is
   data_fdps_audio_sfx_driver_available_flag, in audio.h. */
extern unsigned char data_fdps_audio_sfx_enabled_flag;

#endif
