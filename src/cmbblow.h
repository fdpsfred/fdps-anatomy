/* cmbblow.h -- one blow of the full-screen animated attack exchange.
 *
 * The exchange itself lives in src/combat.h: it loads the clips, brings the
 * attacker in and then calls this once for the blow and, if the defender is
 * still standing, once more with the two units swapped for the counterblow.
 * The arithmetic that decides what a blow did is
 * fdps_combat_compute_hit_outcome, also in src/combat.h; this function is the
 * playback, and it is the only thing in the animation that writes a record.
 *
 * Both units are named by their index in the map unit array reached through
 * data_fdps_map_unit_array_ptr (src/gamedata.h); the record layout is struct
 * fdps_unit_record in src/fdpstype.h.
 */
#ifndef CMBBLOW_H
#define CMBBLOW_H

/* Plays one unit's blow on the combat animation and drains the damage out of
   the defender as the impact frames go by, returning the defender's remaining
   HP.  The whole animation is composed on a 368 x 248 page this function
   allocates and frees for itself, and every frame is put on the mode 13h
   screen; the call returns when the twelve settle frames that close the
   animation have been shown.

   THE DEFENDER'S HP RECORD IS WRITTEN HERE, at word +0x40, once per impact
   frame: start_hp - impact_frames_paid * damage / impact_frames_in_the_clip,
   floored at zero.  The figure the last of those stores left is what comes
   back, so a caller reading the answer is reading the record.  A defender left
   at 0 cancels a second blow.

   THE BLOW IS STRUCK TWICE when the attacker's equipped weapon carries the
   double-strike attribute -- hit_effect 2 of ITEM.DAT -- or when either of two
   independent 3-in-100 rolls comes up, and each blow re-reads the HP and asks
   for a fresh outcome.  Both rolls are always taken; see the note in
   src/cmbblow.c.

   `act_clip` is the attacker's Act%03d.saf attack animation.  Byte 4 of its
   frame 0 is the travelling-attack lead-in count: when it is non-zero that many
   frames play over `from_backdrop` before fdps_combat_slide_backdrops carries
   the view across to `to_backdrop`, and while it is non-zero the attacker is
   neither drawn nor gauged in the frames that follow.  Byte 5 of each frame
   marks the impact frames the damage is paid over; a clip with none pays
   nothing and leaves the record alone.

   `attacker_stand_cursor` and `defender_stand_cursor` are three-dword .SAF
   playback cursors (src/saf.h) over the two units' Stand%03d.saf clips, primed
   by the caller.  The defender's is advanced and drawn on every frame; the
   attacker's is advanced and drawn only in the settle frames of a blow that
   did not travel, and it is the sprite that rides in on the slide back.

   `from_backdrop` is the Back%02d.saf terrain the animation opens on and
   `to_backdrop` the other one; only a travelling attack reads the second, and
   fdps_combat_play_attack_exchange passes the same pointer for both on the
   counterblow.  Both are swapped in their own argument slots after each slide,
   so a blow that travels and does not kill leaves the view on `to_backdrop`.

   The page comes from malloc and is not checked, so an exhausted heap faults.
   data_fdps_timer_tick_counter (src/gamedata.h) paces every frame, so a caller
   running this with no timer interrupt installed stops on the first frame that
   waits, and both units' gauge art must already be loaded because every frame
   paints a panel through fdps_draw_unit_hp_mp_gauges (src/gauge.h). */
extern int fdps_combat_play_blow(int attacker_unit_index,
                                 int defender_unit_index, void *act_clip,
                                 int *attacker_stand_cursor,
                                 int *defender_stand_cursor,
                                 void *from_backdrop, void *to_backdrop);
#pragma aux fdps_combat_play_blow "*" parm caller [];

#endif
