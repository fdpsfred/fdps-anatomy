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

/* Plays the entrance of the combat animation: the terrain backdrop climbs into
   place while the acting unit walks in from his own edge of the screen.  Nine
   frames, one per timer tick, each composed on an offscreen page this function
   allocates and frees for itself and presented to the mode 13h screen; the call
   returns when the ninth frame, the one that has everything at rest, has been
   shown.  That resting state is what the caller's own attack animation takes
   over from.

   Both units are named by index in the map unit array.  `attacker_unit_index`
   is the acting unit: its side byte decides which edge he comes in from -- side
   0 from the left, any other side from the right, 15 columns a frame over 120 --
   and his HP/MP panel is painted on every frame.  `defender_unit_index` is used
   for nothing but its panel, and only when `attacker_only` is 0.

   `attacker_only` non-zero drops the defender entirely: he is neither drawn nor
   gauged, AND the attacker then rides down with the climbing backdrop instead of
   moving only sideways, because the draw request's y is left holding whatever
   the previous draw put there.  fdps_combat_play_attack_exchange takes the flag
   off the attacker's own Act%03d.saf and hands over the attacker's terrain when
   it is set; fdps_combat_play_spell_on_targets always passes 0.

   `attacker_saf_cursor` and `defender_saf_cursor` are three-dword .SAF playback
   cursors (src/saf.h) for the two units' Stand%03d.saf clips.  Each is ADVANCED
   ONE TICK BEFORE the frame that reads it, so the nine frames show frames 1
   through 9 of a clip and frame 0 is never seen; the end-of-clip answer is
   discarded, so a shorter clip wraps unremarked.  The defender's is read only
   when `attacker_only` is 0, though both callers always pass a live cursor.
   `backdrop` is the Back%02d.saf terrain image, of which entry 0 is drawn.

   Nothing is returned.  The page comes from malloc and is not checked, so an
   exhausted heap faults.  data_fdps_timer_tick_counter (src/gamedata.h) paces
   the frames, so a caller running this with no timer interrupt installed stops
   on the first frame that waits, and both units' gauge art must already be
   loaded because every frame paints a panel through
   fdps_draw_unit_hp_mp_gauges (src/gauge.h). */
extern void fdps_combat_slide_in_attacker(int attacker_unit_index,
                                          int defender_unit_index,
                                          int attacker_only,
                                          int *attacker_saf_cursor,
                                          int *defender_saf_cursor,
                                          void *backdrop);
#pragma aux fdps_combat_slide_in_attacker "*" parm caller [];

/* Plays one whole physical attack on the full-screen combat animation: the
   attacker's blow and, when the defender is still standing and still able to,
   the defender's counterblow.  It is the alternative to the on-map attack
   display fdps_unit_attack_target (src/unitatk.h) puts up; both callers pick
   one or the other for the same blow, and neither reads a result.

   Both units are named by index in the map unit array.  `attacker_unit_index`
   is the acting unit throughout: its Act%03d.saf is the attack clip, its
   terrain is where a travelling attack opens, and it is the attacker in both
   counterattack tests.  `defender_unit_index` is the struck unit, whose HP the
   blow drains and whose terrain the animation normally shows.

   THE FUNCTION OWNS THE FIGHT-MODE ENVIRONMENT AND PUTS IT BACK.  On the way
   in it reads FMer1.tmp and FMer2.tmp -- the fight blend tables -- over
   data_fdps_palette_shade_ramp_table and data_fdps_inverse_palette_cube, and
   uploads the fight palette; on the way out it clears the 64000 bytes of the
   mode 13h screen, uploads the map palette and reads Mer1.tmp and Mer2.tmp
   back over the same two globals.  Both pairs are opened by bare name in the
   working directory and NEITHER fopen RESULT IS TESTED, so a missing file
   faults inside fread.  It also builds and frees the two combat gauge sheets
   data_fdps_gauge_fill_sheet_ptr and
   data_fdps_combat_gauge_sprite_sheet_ptr (src/gamedata.h), which is why
   the panels are painted for the whole animation and by nothing after it.

   Everything the animation needs comes off disk here: both units'
   Stand%03d.saf out of Fight.vfs, the attacker's Act%03d.saf out of
   FigAct.vfs, the defender's as well when the counterattack test passes, and
   one or two Back%02d.saf terrain images out of BackGrnd.vfs.  All of them are
   freed before the return.  Nothing is returned and no load is checked --
   fdps_vfs_load_entry ends the process on a miss.

   data_fdps_battle_pending_xp_credit is cleared on entry and left holding
   whatever the blow earned.  The tile-info block fdps_map_load_tile_info
   publishes into is used as scratch here and is left holding whatever the last
   blow looked up, not what the terrain images were chosen from.
   data_fdps_timer_tick_counter paces every frame the two presenters draw, so a
   caller running this with no timer interrupt installed stops inside them. */
extern void fdps_combat_play_attack_exchange(int attacker_unit_index,
                                             int defender_unit_index);
#pragma aux fdps_combat_play_attack_exchange "*" parm caller [];

#endif
