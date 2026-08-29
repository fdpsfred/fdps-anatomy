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

#endif
