/* unitatk.h -- the on-map physical attack: a whole weapon swing, the single
 * blow it is made of, and the state the blow leaves behind.
 *
 * This is the map-side resolver, the one that runs while the battle map is on
 * screen and the attacker's sprite swings.  The full-screen animated exchange
 * is a different pair of functions in combat.c and neither calls the other.
 *
 * Both units are named by their index in the map unit array reached through
 * data_fdps_map_unit_array_ptr (src/gamedata.h); the record layout is struct
 * fdps_unit_record in src/fdpstype.h.
 */
#ifndef UNITATK_H
#define UNITATK_H

#include "fdpstype.h"

/* 00064010.  Set to 1 as fdps_unit_resolve_attack_hit is entered and cleared
   to 0 the moment the accuracy roll lands, so after the call it is 1 for a
   miss and 0 for a hit.

   Nothing in the image reads it.  A sweep of every reference to 00064010
   finds exactly two, and both are the stores inside that one function, so the
   flag is written for a reader that no longer exists.  It is still emitted
   because the stores are two of the function's observable effects and because
   an absent global is not the same program as an unread one. */
extern unsigned char data_fdps_battle_last_hit_or_miss_flag;

/* Runs one unit's whole weapon swing at another and hands back the target's
   current HP afterwards -- 0 when the target is dead.  Both units are named by
   their index in the map unit array and neither is range checked; gauge_pos
   points at two ints, the screen x and y of the target's 43x6 HP bar, which is
   painted at (x + 4, y + 4) straight into the mode 13h frame buffer.  The
   caller passes the pair block fdps_battle_show_combat_gauges hands back, or
   that block plus 8 for the counter-attack, so each combatant's bar has its
   own place.

   One swing lands one blow, or two.  The strike count starts at 1 and two
   independent tests raise it to 2: a flat 3 percent roll made on EVERY attack
   whatever the weapon, and a hit effect of 2 -- the double strike the
   equipment tables name -- which is taken without consulting the weapon's
   effect percentage at all, so a double-strike weapon always lands both.
   Passing both tests still lands two blows and not three.

   Each blow, in order: reads the target's HP and maximum out of the record,
   works out the bar width that gives; plays the attack animation
   (fdps_play_attack_animation, anim.h); calls fdps_unit_resolve_attack_hit,
   which writes the new HP into the record and returns it; then walks the bar
   down one pixel at a time from the old width to the new, painting each step
   with fdps_draw_unit_gauge (gauge.h) and waiting 8 ms between them.  Which
   graphic fills the bar comes from the TARGET's side byte: side 0 takes
   graphic 2 and every other side graphic 1.

   Three of those are contracts a reader would otherwise get wrong.  The
   3 percent roll is made before, and independently of, the double-strike test,
   so folding the two into one condition would short-circuit the rand() call
   away and shift the whole pseudo-random stream (rebuild_info/pitfalls.md).
   The count-down test is >=, so a blow that takes nothing off -- a miss --
   still repaints the bar once.  And a second blow is skipped when the first
   one leaves the target on 0 HP, which is what makes the return value usable
   as "is the target still there to counter-attack". */
extern int fdps_unit_attack_target(int attacker_unit_index,
                                   int target_unit_index, int *gauge_pos);
#pragma aux fdps_unit_attack_target "*" parm caller [];

/* Resolves one blow of an on-map weapon attack and hands back the target's
   current HP afterwards -- 0 when the blow killed it.  Both arguments are
   indices into the map unit array and neither is range checked.
   fdps_unit_attack_target is the only caller; it drains the target's HP bar
   down to the returned value and calls again for a double-strike weapon.

   What one call does, in the order it does it:

     - raises data_fdps_battle_last_hit_or_miss_flag above;
     - takes the attacker's AP and HIT and the target's DP, EV, HP and max HP
       out of the two records, and the attacker's critical rate out of its
       class record (PROMAP.DAT row clazz + 1, byte +8);
     - resolves the attacker's equipped weapon and reads its hit effect and
       that effect's percentage;
     - for each combatant that is not flying, loads the tile it stands on and
       scales that combatant's stat by the terrain's percentage;
     - applies the weapon's hit effect: a critical weapon raises the critical
       rate, while a poison or paralysis weapon rolls its percentage and, on a
       target whose class is not 0x19, writes a duration into the target's
       status timer and flashes the screen green twice;
     - rolls the accuracy, and on a hit rolls the critical, takes the damage
       off the target's HP and writes the HP back;
     - when the attacker is on the player's side and the target is an enemy
       record, works out what the blow earned into
       data_fdps_battle_pending_xp_credit.

   Three of those are contracts a reader would otherwise get wrong.  The status
   roll happens BEFORE the accuracy roll and is not conditioned on it, so a
   poisoned or paralysed target stays poisoned or paralysed even though the
   blow missed.  The target's HP is written back on a miss as well as on a hit,
   with the value it already had.  And the experience figure is assigned rather
   than added, so it is this blow's award and not a running total.

   Everything random comes from rand(), which the game never seeds
   (rebuild_info/pitfalls.md), so a given battle replays identically. */
extern int fdps_unit_resolve_attack_hit(int attacker_unit_index,
                                        int target_unit_index);
#pragma aux fdps_unit_resolve_attack_hit "*" parm caller [];

#endif
