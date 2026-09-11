/* ending.h -- the ending sequence: the epilogue cards the party's roster is
 * walked into and the movie that closes the game (rebuild_info/code_layout.md).
 *
 * One entry point, reached from two chapter endings and owned by neither.  The
 * screens around it belong to title.h, and the globals this layer reads -- the
 * chapter text, the roster count and the two palettes among them -- belong to
 * gamedata.h, not to this file.
 */
#ifndef ENDING_H
#define ENDING_H

/* 0001ba40.  Plays the ending: one animated card per party roster member,
   each carrying that member's epilogue line, and then the "End" movie.  Both
   chapter-ending handlers that reach it -- fdps_chapter_27_end at 0003b8f0 and
   fdps_chapter_30_end at 0003ba80 -- call it with nothing and carry on
   afterwards, so it returns, and what it returns to is the black screen
   fdps_play_movie leaves behind.

   WHICH ENDING IS PLAYED IS READ OFF THE CHAPTER ID, not off an argument.  The
   epilogue lines are a block of consecutive entries in the chapter text
   data_fdps_current_chapter_text_ptr points at, and the block's first index is
   0x20 for chapter index 0x1d and 0x19 for anything else -- which in practice
   means chapter index 0x1a, the other caller.  Roster member i is captioned
   with entry base + i.  Chapter 0x1a also SKIPS ROSTER SLOT 3: the loop
   counter is moved straight from 3 to 4, so that member gets no card and no
   caption, and the caption indices jump with the counter rather than closing
   the gap.

   THE LOOP RUNS ONE PASS PAST THE END OF THE ROSTER.  The bound is
   `i <= data_fdps_roster_member_count`, and on the last pass -- where the
   record would be the one past the last member -- the sprite id is the fixed
   class 12 instead of the member's own portrait id, so the ending closes on a
   card nobody in the party owns.  Every other card takes its sprite id from
   the portrait byte of fdps_get_roster_record(i).

   Each card is five stages on a private 368x248 page, every frame of every
   stage presented on the vertical retrace and paced to one change of
   data_fdps_timer_tick_counter: the backdrop and the standing sprite slide
   into place over nine frames, they hold for fifty, the member's action clip
   plays out once, the epilogue line stands for 251 frames while the backdrop
   dissolves away under it over the first eight, and the standing sprite then
   dissolves out over nine.  The backdrop is Back%02d.saf named by i * 3, so
   consecutive members stand in front of different terrain.

   THE PAGE IS ALLOCATED PER CARD AND RELEASED ONCE, so every card but the last
   leaks about 89 KB.  That is the original's behaviour and it is not to be
   tidied (rebuild_info/pitfalls.md); the three clips are released properly at
   the end of each card.

   On the way in it swaps the ending's blend tables and palette over the map's
   and clears the screen, and on the way out it puts the map's back and hands
   the screen to fdps_play_movie("End").  The tables are the two globals every
   alpha blitter reads, so nothing else may be drawing while this runs.

   Reads data_fdps_chapter_current_chapter_id, data_fdps_roster_member_count,
   data_fdps_current_chapter_text_ptr, data_fdps_vga_fight_palette_ptr,
   data_fdps_vga_main_palette_ptr and data_fdps_timer_tick_counter, and writes
   the two blend tables from disk.  It publishes no global of its own. */
extern void fdps_play_ending_credit_roll(void);
#pragma aux fdps_play_ending_credit_roll "*" parm caller [];

#endif
