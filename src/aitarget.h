/* aitarget.h -- collecting the units a targeting mode accepts, and deciding
 * whether a counter-attack is possible.
 *
 * Every collector here walks the map unit array and answers "which units does
 * this targeting mode accept", either as a count alone or as a list of unit
 * indices written into a caller-supplied byte array.  The select_mode
 * numbering is NOT shared between the collectors even though the same ITEM.DAT
 * and MAGICDAT.DAT bytes are passed to more than one of them; each function
 * documents its own table.
 */
#ifndef AITARGET_H
#define AITARGET_H

/* Counts the units within an exclusive Manhattan distance of one tile that
   select_mode accepts, appending each match's unit index to out_indices as one
   byte when that pointer is non-NULL.  Pass NULL to count only; the count
   advances either way.  Returns the number of matches.

   select_mode: 0 keeps side 0, 1 keeps every non-zero side, 2 keeps side 2
   that has already acted this turn, 3 keeps side 2, anything else keeps
   nothing. */
extern int fdps_collect_targets_in_area(int tile_x, int tile_y, int max_dist,
                                        unsigned char *out_indices,
                                        int select_mode);
#pragma aux fdps_collect_targets_in_area "*" parm caller [];

/* Collects the units standing on the straight line a line-shaped item or spell
   sweeps out from tile (origin_x, origin_y) toward tile (aim_x, aim_y),
   appending each match's unit index to out_indices as one byte, and returns
   how many were written.  out_indices is written unconditionally: NULL is not
   a "count only" request here and is not checked, and neither is the buffer
   length against line_length.

   Only the direction of the aim tile is used, never its distance, and the walk
   is strictly axis-priority: when origin_x == aim_x it steps one tile per
   iteration along y toward aim_y, and otherwise it steps along x toward aim_x
   and discards the y difference entirely.  It is therefore never diagonal even
   though the player's two call sites pass a freely placed map cursor.

   line_length is how many tiles the line covers counted from the tile NEXT TO
   the origin: the step is applied before the tile is examined, so the origin
   tile itself is never looked at, and 0 examines nothing at all.  It is the
   ITEM.DAT use_distance byte at +0x10, or the MAGICDAT.DAT distance byte at
   +0x03, minus 0x10.  Every caller compares that byte as a number, not as a
   mask: from 0x10 up it is a line, below 0x10 it goes to
   fdps_collect_targets_in_range instead, and line_length is the byte less
   0x10, never a bit test or a low-nibble extract.

   A tile outside the map -- the grid header's tile extents at
   data_fdps_battle_move_grid_ptr, times 0x18 -- is skipped rather than ending
   the walk.

   select_enemy_side is NOT the select_mode of the two collectors above and on
   the values they share it runs the opposite way round: zero keeps the
   non-zero sides (guest NPC side 1 and the player's party side 2), and
   non-zero keeps side 0, the enemy side.  The reachable values are 0, 1 and 5.

   The map cursor globals data_fdps_map_cursor_world_x and
   data_fdps_map_cursor_world_y are driven across the walk, because they are
   the only channel into fdps_battle_find_unit_at_cursor (unit.h), and restored
   before the return.  Retired units are already filtered out inside that
   finder and are never reported here. */
extern int fdps_collect_targets_in_line(int aim_x, int aim_y,
                                        unsigned char *out_indices,
                                        int origin_x, int origin_y,
                                        int line_length, int select_enemy_side);
#pragma aux fdps_collect_targets_in_line "*" parm caller [];

/* Marks the tiles an action used from (tile_x, tile_y) can reach into the
   marker byte of every movement grid cell, then counts the units standing on a
   marked tile that select_mode accepts, appending each match's unit index to
   out_indices as one byte when that pointer is non-NULL.  Pass NULL to count
   only; the count advances either way.  Returns the number of matches.

   The grid has to arrive as fdps_map_grid_reset (movegrid.h) leaves it -- every
   marker at the 0xff sentinel -- and is left dirty on return; the caller resets
   it again.

   range_code carries the reach and the shape together.  Below 0x10 it is that
   many movement points spread by fdps_move_grid_flood_fill_range over the
   PROMAP.DAT default class row, whose eight terrain costs are all 1, so it is a
   tile count that walls still cut short; min_dist then removes every tile whose
   Manhattan distance from the centre is below it, exclusively.  From 0x10 up it
   is a straight-line cross with arms of range_code - 0x10 tiles along the
   centre's row and column, inclusive, and min_dist is ignored.

   select_mode: 0 keeps side 0, 1 keeps every non-zero side, 2 keeps side 1, 3
   keeps side 2, anything else keeps nothing.  Mode 2 is NOT what
   fdps_collect_targets_in_area reads from the same ITEM.DAT byte.

   Nothing is bounds checked: neither the unit coordinates against the grid
   header, nor out_indices against the number of matches, nor the grid pointer
   against null. */
extern int fdps_collect_targets_in_range(int tile_x, int tile_y,
                                         unsigned char *out_indices,
                                         int range_code, int min_dist,
                                         int select_mode);
#pragma aux fdps_collect_targets_in_range "*" parm caller [];

/* Would unit defender_unit strike back at an attacker standing on tile
   (attacker_x, attacker_y)?  Returns 1 when it would and -1 -- NOT 0 -- on
   every one of the four refusals, so the answer must be compared against 1 and
   never used as a bare predicate: `if (fdps_check_can_counter_attack_from_tile
   (...))` is true in both directions.  The sole caller,
   fdps_map_actor_score_best_attack, does CMP EAX,1.

   The four tests, all of them on the DEFENDER except the second:
     - struct fdps_unit_record's status_timers[4] at record offset 0x26, the
       paralysis counter, must be zero;
     - the Manhattan distance between the defender's own tile (pos_x, pos_y)
       and the passed tile must be exactly 1, so a diagonal never counts and
       neither does the defender's own tile;
     - the defender must have a weapon equipped, fdps_unit_find_equipped_slot
       (unititem.h) with want_armor 0 answering something other than -1;
     - that weapon's ITEM.DAT range_min at item record +0x0b must be below 2.

   The last test is where this function and the unit-index twin
   fdps_check_can_counter_attack part company: that one requires range_min to
   be exactly 1, this one accepts 0 as well.  The difference is visible on item
   0x63 光束砲, the one weapon-type ITEM.DAT entry whose range is 0-0
   (assets/items.md): the map AI predicts a counterattack from a unit holding
   it that the fight itself will not deliver.  Folding the two functions into a
   shared helper erases that.

   attacker_x and attacker_y are tile coordinates in the same 0-based units as
   the record's own pos_x and pos_y.  defender_unit is a position in the
   current battle's unit array and is not range checked; the record is resolved
   through fdps_get_unit_record, so a call after the array has moved sees the
   new block.  Nothing is written. */
extern int fdps_check_can_counter_attack_from_tile(int defender_unit,
                                                   int attacker_x,
                                                   int attacker_y);
#pragma aux fdps_check_can_counter_attack_from_tile "*" parm caller [];

/* Would unit defender_unit strike back at unit attacker_unit?  Returns 1 when
   it would and -1 -- NOT 0 -- on every one of the four refusals, so the answer
   must be compared against 1 and never used as a bare predicate; all six call
   sites, in the attack exchange, the two attack displays and the map AI, do
   CMP EAX,1.

   The four tests, all of them on the DEFENDER except the second:
     - struct fdps_unit_record's status_timers[4] at record offset 0x26, the
       paralysis counter, must be zero;
     - the Manhattan distance between the two units' tiles (pos_x, pos_y) must
       be exactly 1, so a diagonal never counts and neither does the two of them
       standing on one tile;
     - the defender must have a weapon equipped, fdps_unit_find_equipped_slot
       (unititem.h) with want_armor 0 answering something other than -1;
     - that weapon's ITEM.DAT range_min at item record +0x0b must be exactly 1.

   The last test is where this function and the tile-shaped twin
   fdps_check_can_counter_attack_from_tile part company: this one requires
   range_min to be exactly 1, that one accepts 0 as well.  The difference is
   visible on item 0x63 光束砲, the one weapon-type ITEM.DAT entry whose range
   is 0-0 (assets/items.md).  Folding the two functions into a shared helper
   erases that.

   Nothing about the attacker is read but its two tile bytes: its own weapon,
   side and status timers do not enter, and the two arguments are therefore not
   interchangeable.  Neither index is range checked; both records are resolved
   through fdps_get_unit_record, so a call after the array has moved sees the
   new block.  Nothing is written. */
extern int fdps_check_can_counter_attack(int attacker_unit, int defender_unit);
#pragma aux fdps_check_can_counter_attack "*" parm caller [];

#endif
