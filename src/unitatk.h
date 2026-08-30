/* unitatk.h -- the on-map physical attack: one blow resolved against one
 * target, and the state the blow leaves behind.
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
