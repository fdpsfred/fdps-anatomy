/* table.h -- record accessors for the game's static data tables.
 *
 * The nine files fdps_load_data_tables reads out of the VFS container at
 * startup are each held whole in one heap block, addressed through a pointer
 * global that gamedata.c owns (see gamedata.h).  Every one of them is a flat
 * array of fixed-stride records, and this file holds the one accessor per
 * table that turns a record id into a pointer into that block.
 *
 * They are all the same shape: multiply the id by the table's stride, add the
 * base pointer, return it.  None of them bounds-checks, none of them tests the
 * base for null, and none of them touches the record it hands back -- the
 * range test lives in the caller that knows which table an id belongs to.
 *
 * The record layouts are struct definitions in fdpstype.h, byte-packed there
 * because the original's strides are the file's own and not what an aligning
 * compiler would pick (rebuild_info/pitfalls.md).
 */
#ifndef TABLE_H
#define TABLE_H

#include "fdpstype.h"

/* Returns a pointer to record char_id of the FRIAPRDA.DAT character base-stat
   table: race, class, starting level, base HP/MP/AP/DP/DX, movement, the
   initial learned-spell bitmap and the initial equipment, in the 24 bytes of
   struct fdps_character_base_record.

   char_id is the character's portrait id, 0..59, the same id that indexes the
   per-level growth table through fdps_get_growth_record.  Nothing is checked:
   the table holds 60 records and any id outside that range addresses memory
   past one end of it or the other, because the multiply is signed.  The bound
   is the caller's -- fdps_deploy_unit compares the id against 0x3c and sends
   0x3c and above to fdps_get_enemy_record instead.  The returned pointer is
   never null in the sense of being tested here; it is the table base plus an
   offset, and it is whatever the base holds before fdps_load_data_tables has
   run.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_character_base_record *fdps_get_character_base_record(int char_id);
#pragma aux fdps_get_character_base_record "*" parm caller [];

/* Returns a pointer to record char_id of the FRILEVUP.DAT per-level growth
   table: the five {min, exclusive max} byte pairs the level-up roll draws each
   of AP, DP, DX, HP and MP from, then the GETMGTAB.DAT spell-learning index of
   this character form, in the 11 bytes of struct fdps_character_growth.

   char_id is the same portrait id that indexes the base table through
   fdps_get_character_base_record: 0..59, of which 0x00-0x0b are the twelve
   playable characters in their starting form and 0x0f-0x23 their promoted
   forms.  Nothing is checked -- the table holds 60 records and any id outside
   that range addresses memory past one end of it or the other, because the
   multiply is signed -- and the base is not tested for null either.

   The four callers read it both ways round: fdps_roster_add_character takes
   hp_min and mp_min as the starting HP and MP of a unit it is enrolling,
   fdps_unit_award_exp_and_level_up takes each {min, max} pair as the range of
   one level-up gain, fdps_deploy_unit reads it while placing a scripted unit,
   and fdps_church_promote_loop re-reads it for the form a promotion turns the
   character into.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_character_growth *fdps_get_growth_record(int char_id);
#pragma aux fdps_get_growth_record "*" parm caller [];

/* Returns a pointer to record enemy_index of the ENEMYDAT.DAT enemy stat
   table: race and class, the per-level HP coefficient, the per-level MP, AP,
   DP and DX coefficients, the absolute movement allowance and the experience
   multiplier the kill reward is scaled by, in the 10 bytes of struct
   fdps_enemy_data.

   enemy_index is the portrait id minus 0x3c, 0..90 over the 91 records the
   910-byte file holds (resource_info/data_tables.md): portrait ids 0x00-0x3b
   are the playable characters and go to the two tables above instead, and
   0x3c upwards are the enemy forms this table describes.  Nothing is checked
   -- no bound at either end, and the multiply is signed, so an index below
   zero addresses memory in front of the table just as one past 90 addresses
   memory behind it -- and the base is not tested for null either.

   The subtraction is the caller's and so is the guard that makes it safe:
   fdps_unit_apply_damage and fdps_combat_compute_hit_outcome reach it only
   after testing the unit's side byte, and fdps_deploy_unit only after
   comparing the portrait id itself against 0x3c.  What the callers want out of
   the record is the experience multiplier at +0x9, which both damage paths
   read straight after the call.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_enemy_data *fdps_get_enemy_record(int enemy_index);
#pragma aux fdps_get_enemy_record "*" parm caller [];

/* Returns a pointer to record item_id of the ITEM.DAT item table: the type
   code that says whether the entry is a weapon, a piece of armour, a
   consumable or a promotion badge, the four signed equipment stat modifiers
   ap/hit/dp/ev, the weapon hit effect and its rate, the attack reach, the use
   effect with its amount, distance, target mode and radius, the shop price and
   the selection mode, in the 23 bytes of struct fdps_item_effect.

   item_id is the item number, the same byte an equipment or bag slot of a unit
   record carries and the same number the shop and sell lists hold.  The table
   has 251 records, ids 0x00-0xFA, of which 0x00-0xE1 carry content and the
   remainder are blank (assets/items.md).  Nothing is checked -- no bound at
   either end, and the multiply is signed -- and the base is not tested for
   null either.  The absence of the bound is behaviour and not an oversight:
   item id 0xFF occurs in play, its record lies past the end of the table, and
   the varying values read there are the guide's "FF BUG item".

   Callers read byte +0x00 to classify the item, the four stat words to move a
   unit's combat totals, byte +0x0d to dispatch a use effect and the word at
   +0x13 for the shop price.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_item_effect *fdps_get_item_record(int item_id);
#pragma aux fdps_get_item_record "*" parm caller [];

#endif
