/* btlturn.h -- the battle's turn and phase engine: the enemy, NPC and player
 * phases, the per-unit turn, and the advance from one turn to the next.
 *
 * The unit records these work on live in the one 0x50-byte array reached
 * through data_fdps_map_unit_array_ptr (src/gamedata.h), bounded by
 * data_fdps_map_unit_count; the record layout is struct fdps_unit_record in
 * src/fdpstype.h.  Nothing in this file owns state of its own.
 *
 * Byte +5 of a record, struct field `flags`, is the turn engine's own status
 * byte: bit 0 is the retired flag and bit 7 is the acted-this-turn flag.  The
 * phase engine tests both at once as flags & 0x81 and passes over any unit for
 * which that is non-zero, so a unit that is retired and a unit that has already
 * acted stop the phase waiting on them in exactly the same way.
 */
#ifndef BTLTURN_H
#define BTLTURN_H

/* Raises the acted-this-turn flag, bit 7 of the status byte, on the unit at
   unit_index, so the player phase stops waiting on it.  No other field is
   touched and nothing is returned.  unit_index is NOT range-checked against
   data_fdps_map_unit_count: the record address is formed and written whatever
   it holds.  The bit is cleared again for every unit when the next turn
   opens. */
extern void fdps_battle_mark_unit_done(int unit_index);
#pragma aux fdps_battle_mark_unit_done "*" parm caller [];

/* Fires the chapter script's turn-scheduled events for one side's phase of the
   current battle turn.  All sixteen entries of the turn-event table in the
   resident MAP%02d.DAT block are walked; an entry whose scheduled turn equals
   data_fdps_battle_turn_counter and whose side equals `side` calls its handler
   out of data_fdps_chapter_event_handler_table (chapter.h) with 0 as the unit
   index.  The walk does not stop at the first match, writes no global and
   returns nothing.

   `side` is in the encoding of unit record byte +6: 0 enemy, 1 friendly NPC,
   2 player.  fdps_battle_advance_turn is the only caller and passes 1 before
   the NPC pass, 0 before the enemy pass and 2 as it opens the next player
   phase. */
extern void fdps_battle_run_turn_events(int side);
#pragma aux fdps_battle_run_turn_events "*" parm caller [];

/* Runs the enemy side's whole phase of the current battle turn.  Takes
   nothing, returns nothing; whether the battle ended part-way through is left
   in data_fdps_chapter_event_or_battle_end_code (gamedata.h), which
   fdps_battle_advance_turn -- the only caller -- tests the instant this
   returns.

   The array is swept TWICE.  The first sweep lets an enemy act only when
   fdps_map_actor_score_best_spell or fdps_map_actor_score_best_item (aiscore.h)
   scores at least 6; the second gives a behaviour step to every enemy still
   eligible, which is exactly those the first sweep's gate turned away.  So the
   enemies with something worth casting or drinking all move first, and every
   enemy still gets one turn.

   An enemy is eligible when its side byte is 0, neither bit 0 (retired) nor
   bit 7 (already acted) is set in its status byte, and its paralysis counter
   at status_timers[4] is zero.  Enemies deployed by an event part-way through
   either sweep are reached, because the bound is re-read from
   data_fdps_map_unit_count every iteration. */
extern void fdps_battle_enemy_turn_phase(void);
#pragma aux fdps_battle_enemy_turn_phase "*" parm caller [];

/* Runs the friendly NPC side's whole phase of the current battle turn.  Takes
   nothing, returns nothing; whether the battle ended part-way through is left
   in data_fdps_chapter_event_or_battle_end_code (gamedata.h), which
   fdps_battle_advance_turn -- the only caller -- tests the instant this
   returns.

   ONE sweep, not the enemy phase's two, and no score gate: every eligible NPC
   is given a behaviour step in index order, and neither
   fdps_map_actor_score_best_spell nor fdps_map_actor_score_best_item is called
   anywhere in this phase.

   An NPC is eligible when its side byte is 1, neither bit 0 (retired) nor bit
   7 (already acted) is set in its status byte, and its paralysis counter at
   status_timers[4] is zero.  Units deployed by an event part-way through are
   reached, because the bound is re-read from data_fdps_map_unit_count every
   iteration.

   The map-cursor overlay is put away once before the sweep starts as well as
   once per unit, so a battle whose NPC side is empty still has it hidden. */
extern void fdps_battle_npc_turn_phase(void);
#pragma aux fdps_battle_npc_turn_phase "*" parm caller [];

/* 00060004.  Battle rule flag read by fdps_battle_unit_turn below, the only
   function that references it (two reads, CMP dword ptr [0x00060004],0x0 at
   000156b2 and 000157ae, each straight after fdps_battle_action_menu answered
   -1).  Zero makes a cancelled action menu a take-back: a unit that walked is
   put back on its start tile and the turn loop runs again.  Non-zero makes the
   cancel spend the turn where the unit stands.  No instruction in the image
   writes it. */
extern int data_fdps_battle_action_cancel_ends_turn_flag;

/* Runs one player-controlled unit's whole turn: the movement range and the
   destination cursor, the walk, and the action menu, looping on every
   take-back until the turn is spent or the player backs out of the range
   cursor.  unit_index is the unit's index in the battle unit array; nothing
   is returned.  fdps_battle_player_phase_loop is the only caller.

   Backing out of the range cursor ends the call with the turn NOT spent --
   bit 7 of the status byte is left clear, so the phase will offer the unit
   again.  Every other way out goes through fdps_battle_mark_unit_done.
   Whichever way it ends, the cursor overlay is left in mode 1, a pending
   chapter event fires with unit_index, and the chapter's post-action handler
   runs.

   The start tile is read off the map cursor, not the record: the range is
   flooded, the path traced back to and every take-back returned to whatever
   tile the cursor is on when the call is made. */
extern void fdps_battle_unit_turn(int unit_index);
#pragma aux fdps_battle_unit_turn "*" parm caller [];

/* Ends the player phase and runs the battle round to the start of the next
   one.  Takes nothing and returns nothing.

   In order: player units that are idle, unhurt by poison or paralysis and not
   at exactly their maximum HP rest for a fifth of it, flashed white in one
   presented frame with REST.WAV if anyone rested; then the NPC side's turn
   events, status ticks and phase; the enemy banner, the acted-this-turn bit
   cleared on everyone, the enemy side's events, ticks, music and phase; then
   data_fdps_battle_turn_counter is bumped, the player banner shown, the bit
   cleared again, the player music started and the player side's events and
   ticks run.  Last comes the per-turn MP regeneration -- unit 4 with an
   equipped 妖刀村正 or 妖刀正宗, unit 8 with an equipped 形見指環, and any unit
   holding 魔精石碎片 -- after which the cursor is shown again over unit 0
   (unless it has retired) and the play-active flag raised.

   The play-active flag, data_fdps_ui_play_active_flag, is lowered on entry.
   A non-zero data_fdps_chapter_event_or_battle_end_code seen after the NPC
   ticks, the NPC phase, the enemy ticks or the enemy phase returns at once and
   leaves it lowered and the turn counter unbumped. */
extern void fdps_battle_advance_turn(void);
#pragma aux fdps_battle_advance_turn "*" parm caller [];

/* Runs the player phase on the battle map until the battle ends or the
   system menu answers non-zero: one frame a pass, reading the latched make
   code and moving the cursor with the arrows, cycling to the next player unit
   that can still be given a turn on ESC, Z, keypad 5 or Delete, and on Enter
   or Space handing the unit under the cursor its turn (a player unit that has
   not acted and is not paralysed), its status window (any other unit) or, with
   no unit there, the system menu.  F1 opens the map overview and F2 or Home
   the status window, both only while a key is held between its second and
   sixth frame.  Takes nothing, returns nothing; main is the only caller and
   reads data_fdps_chapter_event_or_battle_end_code (gamedata.h) afterwards.

   A battle-end code is tested only after each frame is drawn, and the system
   menu's answer only at the top of the next pass, so either way one more
   frame is drawn before the call returns. */
extern void fdps_battle_player_phase_loop(void);
#pragma aux fdps_battle_player_phase_loop "*" parm caller [];

/* Ends the player phase if there is nobody left to move: when no unit on side
   2 has neither bit 0 (retired) nor bit 7 (already acted) of its status byte
   set and a zero paralysis counter, fdps_battle_advance_turn runs the battle
   round to the next player phase.  Otherwise nothing happens.  Takes nothing,
   returns nothing, writes nothing itself.

   A paralysed player unit does not hold the phase open; a poisoned one that
   has not acted does.  Units of the other two sides are never looked at, and
   the walk stops at data_fdps_map_unit_count. */
extern void fdps_battle_end_phase_if_all_units_done(void);
#pragma aux fdps_battle_end_phase_if_all_units_done "*" parm caller [];

#endif
