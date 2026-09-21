/* chapter.h -- the chapter frame: the state a chapter starts from, the title
 * card it opens with and the four chapter-event dispatch tables
 * (rebuild_info/code_layout.md).
 *
 * Nothing here owns state of its own.  Everything the frame touches lives in
 * the game-state globals gamedata.c owns, and the chapter this file works on
 * is always the one data_fdps_chapter_current_chapter_id names.
 */
#ifndef CHAPTER_H
#define CHAPTER_H

/* Puts the battle map back to the state a chapter's first frame expects and
   rebuilds the unit array under it.

   Three things happen in one call.  The cursor overlay is switched off, the
   map's unit array is rebuilt for the chapter data_fdps_chapter_current_
   chapter_id names -- which loads that chapter's resources, so this is the
   expensive half -- and then the per-battle counters are put back: the
   per-cell event flags cleared, the view window and the cursor moved to the
   map origin, the battle-end code cleared to "still running", the turn counter
   set to turn 1 and the cursor overlay switched back on in mode 1.  Whatever
   scancodes were queued while all that ran are discarded.

   It takes no argument and returns nothing: the chapter is the global, not a
   parameter, and every one of the thirty-two call sites in the original relies
   on that.  It is not a "reset to zero" -- two of the values it installs are
   1, and the unit array it leaves behind is a full chapter's deployment. */
extern void fdps_chapter_state_reset(void);
#pragma aux fdps_chapter_state_reset "*" parm caller [];

/* Shows the current chapter's title card and returns when it has finished:
   the chapter's graphic fades up out of black, stands at full brightness for
   a second, fades back down to black, and the screen is left cleared.  The
   whole thing takes a little over three and a half seconds of real time and
   nothing can interrupt it -- no key is read and no flag is tested.

   It takes no argument and returns nothing.  WHICH card is drawn comes from
   data_fdps_chapter_current_chapter_id (gamedata.h) as a 0-based index into
   Chapter.saf's thirty entries.  The thirty callers -- the chapter entry
   handlers fdps_chapter_01_init .. fdps_chapter_30_init -- do not set that
   global themselves; it already holds the chapter by the time one of them
   runs.  The image writes the global in thirty-four places and none of them
   is inside an entry handler.  Twenty-nine are the chapter-end handlers
   fdps_chapter_01_end .. fdps_chapter_29_end, each storing the next
   chapter's index as a literal as its last act before the epilogue -- 1 at
   0003a440 up to 29 at 0003ba39, in order; fdps_chapter_30_end has no such
   write, chapter 30 being the last.  The other five enter a chapter from
   outside that sequence: fdps_title_screen (0, starting a new game),
   fdps_load_savegame and fdps_load_game_screen (the chapter byte out of the
   save record), fdps_title_demo (25, the demo's fixed chapter) and
   fdps_icon_script_run (the script opcode that advances the story).

   The card is loaded, drawn and freed inside the call: Chapter.saf and
   Chapter.pal come out of MISC.VFS, the picture is composed on a private
   368x248 page, and all three allocations are released before the return, so
   nothing is left behind and nothing is cached between chapters.

   WHAT THE CALLER INHERITS.  The mode 13h aperture is cleared to palette
   index 0 and the DAC is left holding the master palette
   data_fdps_vga_main_palette_ptr names at no bias -- a live palette over a
   blank screen.  The caller has to repaint; it does not have to fade back
   in. */
extern void fdps_show_chapter_title_card(void);
#pragma aux fdps_show_chapter_title_card "*" parm caller [];

/* 000601c4.  The fifty scripted chapter-event handlers, indexed by the event
   slot the map data names.  Eight indirect call sites in seven functions
   reach it, and all eight spell the same call: the slot scaled by four,
   CALL dword ptr [reg+0x601c4] with exactly ONE dword pushed, and ADD
   ESP,0x4 afterwards.  The seven are fdps_battle_enemy_turn_phase (twice),
   fdps_battle_npc_turn_phase, fdps_battle_system_menu,
   fdps_battle_unit_turn, fdps_battle_search_cell_at_cursor,
   fdps_battle_run_turn_events and fdps_run_death_scripts (death.h), which
   reaches it on death-script opcode 2.

   The argument is a battle unit index, and 48 of the 49 fdps_chapter_*
   handlers declared across chevt1.h .. chevt6.h take exactly that one int;
   the handlers that have no unit to work on ignore it.  Seven of the eight
   sites push an index they hold; only fdps_battle_run_turn_events pushes a
   literal 0.

   Ghidra types the slots as void_fn, its placeholder for a code pointer of
   unknown shape; the pushed argument is what makes them handlers of a unit
   index. */
extern void (*data_fdps_chapter_event_handler_table[50])(int unit_index);

/* 0006028c.  The thirty per-chapter post-action scripts, indexed by the
   chapter id in data_fdps_chapter_current_chapter_id (gamedata.h).  It sits
   immediately behind data_fdps_chapter_event_handler_table above -- fifty
   slots of four bytes end exactly at 0006028c -- and is the second of the two
   tables a unit's action goes through: the event table fires what the unit
   stepped on, this one then gives the chapter itself a look at the new state.

   Five indirect call sites reach it and all five spell it the same way: MOV
   EAX,[0x00069cf4] / LEA EAX,[EAX*0x4 + 0x0] / CALL dword ptr [EAX+0x6028c]
   with nothing pushed and no ADD ESP afterwards, so the handlers take no
   argument and return nothing.  They are fdps_battle_enemy_turn_phase at
   00012a48 and 00012aff -- once per sweep -- fdps_battle_npc_turn_phase at
   00012beb, fdps_battle_unit_turn at 0001584e and
   fdps_battle_tick_status_effects at 0001fb50.  No site range-checks the id.

   Ghidra types the slots as void_fn, its placeholder for a code pointer of
   unknown shape; that no argument is pushed at any of the five sites is what
   fixes them as void (*)(void). */
extern void (*data_fdps_chapter_post_action_handler_table[30])(void);

/* 00060074.  The thirty per-chapter setup scripts, indexed by the chapter id
   the game is entering.  Entry n is fdps_chapter_NN_init for NN = n + 1 --
   entry 0 is fdps_chapter_01_init at 00020e90 and entry 29 is
   fdps_chapter_30_init at 00021610 -- because the id is 0-based while the
   chapter the player is shown is one more.  Thirty is the whole table: its 120
   bytes run from 00060074 to 000600ec, where the "IconAni.vfs" literal starts.

   Two indirect call sites reach it, CALL dword ptr [EAX + 0x60074] at
   0002a85e in fdps_title_screen and at 00031505 in fdps_run_village_phase, and
   both spell it the same way: the id scaled by four, nothing pushed and
   nothing added to ESP afterwards, so the handlers take no argument and return
   nothing.  Neither site range-checks the id -- a chapter past 29 calls
   whatever the bytes after the table decode to.

   Ghidra types the slots as void_fn, its placeholder for a code pointer of
   unknown shape; that no argument is pushed at either site is what fixes them
   as void (*)(void).

   It is the SECOND of the two tables a chapter goes through.  This one runs
   when the chapter is entered, out of the village phase or straight off the
   title screen; data_fdps_chapter_event_handler_table above is the scripted
   events inside the battle that follows. */
extern void (*data_fdps_chapter_init_handler_table[30])(void);

/* 00060304.  The thirty per-chapter wrap-up scripts, run when a battle ends
   with the chapter cleared.  Entry n is fdps_chapter_NN_end for NN = n + 1 --
   entry 0 is fdps_chapter_01_end at 0003a410 and entry 29 is
   fdps_chapter_30_end at 0003ba80 -- and the 120 bytes end at 0006037c,
   where a zero dword follows.

   One site reaches it, in main: MOV EAX,[0x00069cf4] / LEA EAX,[EAX*0x4 + 0x0]
   / CALL dword ptr [EAX + 0x60304] at 00029395-000293a1, nothing pushed and no
   ADD ESP afterwards, so the handlers take no argument and return nothing.
   The chapter id is not range-checked.  Ghidra types the slots as void_fn;
   the bare call is what fixes them as void (*)(void). */
extern void (*data_fdps_chapter_end_handler_table[30])(void);

#endif
