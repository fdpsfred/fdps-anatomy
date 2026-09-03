/* combat.h -- the full-screen animated attack exchange.
 *
 * This is the cut-scene side of a physical attack: the screen the game brings
 * up when two units trade blows, the slide-in of the two combatants and the
 * arithmetic that decides what each blow of that exchange did.  The map-side
 * resolver that runs with the battle map still on screen is a different
 * function, fdps_unit_resolve_attack_hit in src/unitatk.c, and neither calls
 * the other.
 *
 * Both units are named by their index in the map unit array reached through
 * data_fdps_map_unit_array_ptr (src/gamedata.h); the record layout is struct
 * fdps_unit_record in src/fdpstype.h.
 */
#ifndef COMBAT_H
#define COMBAT_H

/* Works out what one blow of the animated exchange does and writes the
   numbers into the caller's outcome block.  Nothing is returned and the
   defender's HP is NOT touched -- that is the difference from the map-side
   resolver, which writes the new HP back into the record.  The animation
   drains the bar itself from the figures below.

   `outcome` is a block of six ints, all six of which this function writes
   before it does anything else:

     outcome[0]  non-zero when the blow missed.  Set to 1 on entry and
                 cleared only when the accuracy roll lands.
     outcome[1]  non-zero when the blow was critical.
     outcome[2]  written as 0 and read by nothing in the program.
     outcome[3]  written as 0 and read by nothing in the program.
     outcome[4]  written as 0 and read by nothing in the program.
     outcome[5]  the damage.  0 on a miss, and 0 on a landed blow whose
                 stat gap was too small to produce any.

   The only caller, fdps_combat_play_blow at 000196d0, passes a block of its
   own stack frame and discards the returned register.

   Two side effects reach outside that block.  The defender's poison or
   paralysis timer is written when the attacker's weapon carries that hit
   effect and the roll lands -- and that roll happens BEFORE the accuracy
   roll, so a blow that then misses can still inflict the ailment.  And a
   player-side unit striking a side-0 unit rewrites
   data_fdps_battle_pending_xp_credit with what the blow earned; any other
   pairing of sides leaves the previous action's figure standing.

   Neither index is range checked and neither is the enemy record index the
   experience path derives from the defender's portrait id. */
extern void fdps_combat_compute_hit_outcome(int attacker_unit_index,
                                            int defender_unit_index,
                                            int *outcome);
#pragma aux fdps_combat_compute_hit_outcome "*" parm caller [];

/* Slides the combat animation view from one combatant's terrain backdrop to
   the other's and brings the incoming combatant's sprite in riding on it.
   Eight steps of 40 pixels, one screen in all, each one composed on the
   caller's offscreen page and presented to the mode 13h screen; the call
   returns when the eighth step has been shown.

   `outgoing_backdrop` is the Back%02d.saf image the view is showing when the
   call is made and it leaves; `incoming_backdrop` is the other combatant's
   and it comes to rest on the page's border corner, at x and y both 24.
   `anim_cursor` is that combatant's three-dword .SAF playback cursor
   (src/saf.h), advanced one tick per step, and it is drawn at whatever x its
   own backdrop has reached rather than at one of its own.  `direction` is +1
   or -1 and is the sign of the horizontal movement; nothing else about it is
   read, so any other magnitude scales the whole slide.

   `req` is a draw request block of DRAW_REQUEST_DWORDS ints (src/sprite.h)
   that the caller owns and has already filled in.  ITS PAGE POINTER, PITCH,
   HEIGHT AND TWO BLEND SLOTS ARE READ AND NEVER WRITTEN: the page is cleared
   and presented on the geometry the caller put there, which the callers state
   as a 368 by 248 page.  The four fields that are written -- x, y, the image
   and the item index -- are left holding the last of the three draws, so the
   block comes back naming the incoming combatant's frame at the resting x.

   The tick counter data_fdps_timer_tick_counter (src/gamedata.h) paces the
   steps, so a caller that runs this with no timer interrupt installed stops
   on the first step that waits.  Nothing else outside the request and the
   cursor is touched, and nothing is returned. */
extern void fdps_combat_slide_backdrops(void *outgoing_backdrop,
                                        void *incoming_backdrop,
                                        int *anim_cursor, int *req,
                                        int direction);
#pragma aux fdps_combat_slide_backdrops "*" parm caller [];

#endif
