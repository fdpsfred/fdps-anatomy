/* unit.h -- core access to one map unit record: lookup, flags and the small
 * derived readouts the rest of the game asks for a unit at a time.
 *
 * The records live in the block reached through data_fdps_map_unit_array_ptr
 * (gamedata.h), stride sizeof(struct fdps_unit_record) == 0x50, and are laid
 * out by struct fdps_unit_record in src/fdpstype.h.  Nothing in unit.c owns
 * state of its own.
 */
#ifndef UNIT_H
#define UNIT_H

#include "fdpstype.h"

/* The map unit array's element accessor: hands back the address of the
   unit_index-th record in the block reached through
   data_fdps_map_unit_array_ptr, computed as base + unit_index * 0x50 and
   nothing else.  Reads the base global, dereferences nothing, calls nothing
   and never returns null -- with a null base it returns the offset itself.

   unit_index is a position in the current battle's unit array,
   0..data_fdps_map_unit_count-1 for a live unit, and is NOT range checked at
   either end: the count at 0x00060150 is not read here and the multiply is
   signed, so a negative index addresses memory in front of the array.  Every
   caller carries its own bound.

   The returned pointer is only valid until the array moves.
   fdps_relocate_unit_array reallocates the block, zeroes the old storage and
   frees it on every unit iteration of the battle turn loops, so a record
   pointer must be re-resolved through this function after that call rather
   than held across it. */
extern struct fdps_unit_record *fdps_get_unit_record(int unit_index);
#pragma aux fdps_get_unit_record "*" parm caller [];

/* Has this unit left the battle for good?  Returns 1 when bit 0 of the unit's
   flags byte -- struct fdps_unit_record's flags at record offset 5 -- is set,
   and 0 when it is clear; the result is narrowed to those two values and is
   never the flag byte itself, so it may be compared against 1 as well as
   tested for truth.

   Bit 0 is the retired flag proper: fdps_map_actor_behavior_step returns at
   once for an actor carrying it and sets it when a scripted walk ends.  Bit 7
   of the same byte is the short-lived per-turn redraw flag and is not part of
   this answer.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call
   after the array has moved sees the new block. */
extern int fdps_unit_is_retired(int unit_index);
#pragma aux fdps_unit_is_retired "*" parm caller [];

/* Takes this unit out of the battle for good.  ASSIGNS the whole flags byte --
   struct fdps_unit_record's flags at record offset 5 -- the value 1, so the
   retired flag ends up set and every other bit of that byte ends up clear,
   including bit 7, the acted-this-turn flag fdps_battle_mark_unit_done raises.
   It is not an OR of bit 0 and the difference is visible: a unit retired part
   way through its own turn keeps that flag under an OR.

   No other field of the record is touched, nothing is returned, and unit_index
   is a position in the current battle's unit array that is not range checked;
   the record is resolved through fdps_get_unit_record, so a call after the
   array has moved writes into the new block.

   Nothing in the shipped image calls this: the ten places that retire a unit
   all spell the same two steps out inline. */
extern void fdps_unit_mark_retired(int unit_index);
#pragma aux fdps_unit_mark_retired "*" parm caller [];

/* Does this unit travel above the terrain instead of on it?  Returns 1 when
   the unit's class -- struct fdps_unit_record's clazz at record offset 0x20 --
   is one of the five flying classes 0x16 技師, 0x17 機械伯爵, 0x18 機械大師,
   0x1f 飛兵 and 0x25 惡靈, and 0 for every other class code, 0x26 活屍
   included.  The set is a hard-coded list of those five values in the function
   and is not read from PROMAP.DAT.

   The answer gates two things: the combat resolvers give a flying unit neither
   the attack nor the defence percentage its tile's terrain type would select,
   and the two ground-shock spells 裂地術 and 封神裂震 fail against a flying
   target.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call
   after the array has moved sees the new block. */
extern int fdps_unit_is_flying(int unit_index);
#pragma aux fdps_unit_is_flying "*" parm caller [];

/* Picks which of the unit's status-effect timers gets its icon drawn over the
   unit this cycle.  Returns the icon slot 0..4 -- which is the frame index
   into the IconSts.cel sheet, and corresponds to record offsets 0x23, 0x22,
   0x24, 0x25 and 0x27 in that order -- or -1 when the unit carries no active
   effect and no icon is to be drawn.  cycle is a free-running rotation
   counter, reduced modulo the number of active effects, so an advancing cycle
   walks round the effects the unit is carrying.  unit_index is not range
   checked. */
extern int fdps_unit_select_status_icon(int unit_index, int cycle);
#pragma aux fdps_unit_select_status_icon "*" parm caller [];

/* Turns unit_index to face target_unit_index, by writing a direction code into
   struct fdps_unit_record's facing at record offset 3 of the ACTING unit.  The
   target's record is only read -- its tile x at +0 and tile y at +1 -- and
   nothing is returned.

   The codes are the ones the movement playback writes into the same byte:
   0 is +y (down), 1 is -x (left), 2 is -y (up), 3 is +x (right).  The axis is
   chosen by comparing the absolute tile differences, and the test is strict --
   horizontal only when |dx| is greater than |dy|.  A tie therefore faces the
   unit vertically, so a target on a perfect diagonal turns it up or down, and
   a call with both units on the same tile forces facing 0 rather than leaving
   the facing where it was.  There is no "already facing the right way" early
   out: the byte is written on every call.

   Both indices are positions in the current battle's unit array and neither is
   range checked; both records are resolved through fdps_get_unit_record, so a
   call after the array has moved works on the new block. */
extern void fdps_unit_face_target(int unit_index, int target_unit_index);
#pragma aux fdps_unit_face_target "*" parm caller [];

/* Recomputes the four derived combat stats of one battle-map unit -- struct
   fdps_unit_record's ap, dp, hit and ev at record offsets 0x48, 0x4a, 0x4c and
   0x4e -- from the unit's base stats, the items it currently has equipped and
   the stat buffs currently running on it, and writes all four back into the
   record.  Nothing is returned and no other field is touched.

   The sum is: attack from ap_base at +0x37, defense from dp_base at +0x39, and
   BOTH hit and evade from the one dexterity word dx_base at +0x3e -- there is
   no separate evade base.  Every one of the eight 2-byte inventory entries at
   +0x0a whose flag byte carries bit 0x40 then contributes its item's four
   modifiers, ap to attack, dp to defense, hit to hit and ev to evade, the item
   being fetched with fdps_get_item_record on the entry's unsigned id byte.

   Three of the unit's six status timers reach the result, each on any non-zero
   count: status_timers[2] at +0x24 adds 15 to the dexterity seed before it is
   shared, so it lifts hit and evade together; status_timers[0] at +0x22 scales
   the finished attack total and status_timers[1] at +0x23 the finished defense
   total, both by 1.15 as a double multiply truncated toward zero, so a total
   of 100 becomes 114 and one of 200 becomes 229.  The other three timers do
   not enter the stats.

   The totals are accumulated at int width and stored as words, so one outside
   16 bits wraps into the record rather than saturating, and nothing is
   clamped.  unit_index is a position in the current battle's unit array and is
   not range checked; the record is resolved through fdps_get_unit_record, so a
   call after the array has moved works on the new block.

   Callers run it after anything that can change a unit's equipment or status:
   the equip, shop, sell, transfer and church screens, the item and spell
   effects, the level-up, the unit build and deploy paths, and the per-turn
   status tick as each timer reaches zero.

   The roster-side counterpart fdps_roster_recompute_combat_stats (roster.h)
   does the base and equipment half of this and none of the three buffs; the
   two are not interchangeable. */
extern void fdps_unit_recompute_combat_stats(int unit_index);
#pragma aux fdps_unit_recompute_combat_stats "*" parm caller [];

/* Marks one spell as known by one unit.  The bit it sets is bit spell_id % 8 of
   struct fdps_unit_record's spells_known_bitmap[spell_id / 8] -- the five bytes
   at record offset 0x1a -- which is the same id encoding
   fdps_unit_collect_known_spells (unitstat.h) reads back, so an id set here
   comes out of that walk unchanged.  spell_id is a MAGICDAT.DAT spell number,
   0x00..0x27 (assets/spells.md), and those forty ids are exactly the bits the
   five bytes hold.

   The bit is ORed in: setting a spell the unit already knows changes nothing,
   the unit's other spells are left standing, and no other field of the record
   is written.  Nothing is returned.

   Neither argument is checked.  unit_index is a position in the current
   battle's unit array, resolved through fdps_get_unit_record on every call, so
   a call after the array has moved writes into the new block; a spell_id of 40
   or more writes past the bitmap into the record's race, class, level and
   status timer bytes, and a negative one takes its mask from in front of the
   function's own eight-entry table.  Every id the game passes is a literal or a
   zero-extended table byte, so neither case arises in the shipped image.

   The callers are the four ways a unit learns a spell: the level-up when the
   class's level/spell table names one, the spell-teaching item effect, and the
   two scripted grants -- the title demo's showcase party and the end of chapter
   one. */
extern void fdps_set_flag_bit(int unit_index, int spell_id);
#pragma aux fdps_set_flag_bit "*" parm caller [];

/* Which unit is standing under the map cursor?  Returns the zero-based index
   into the current battle's unit array of the first unit whose tile is the
   cursor's tile and which has not been retired, or -1 when there is none.
   Takes no arguments: the cursor position and the unit array both come from
   globals (gamedata.h), so a caller moves the cursor and then asks.

   The cursor globals hold map pixels and the record holds tiles, so the
   comparison is cursor pixel / 24 against struct fdps_unit_record's pos_x at
   record offset 0 and pos_y at offset 1.  The division is signed and truncates
   towards zero.

   "Retired" is fdps_unit_is_retired's answer, so a unit that has left the
   battle is passed over and a second unit standing on the same tile is
   returned in its place; when several live units share the tile the lowest
   index wins.  The walk is bounded by data_fdps_map_unit_count and the compare
   is signed, so a count of zero or less finds nothing.

   The array base is read once, at entry, and the record pointer is then walked
   by the 0x50 stride; nothing in the loop can move the array, so that is safe
   here and is not a licence to cache the base across a call that relocates it
   (see fdps_get_unit_record above). */
extern int fdps_battle_find_unit_at_cursor(void);
#pragma aux fdps_battle_find_unit_at_cursor "*" parm caller [];

/* Lets every unit in the battle act again.  Walks the whole current unit
   array -- indices 0 to data_fdps_map_unit_count-1 (gamedata.h) -- and clears
   bit 7, and only bit 7, of each record's flags byte at offset 5: the
   acted-this-turn flag fdps_battle_mark_unit_done raises.  Bit 0 of that byte
   is the retired flag and every other bit standing in it survives untouched,
   so this is a turn reset and not a reinstatement of units that have left.

   Takes no arguments and reports nothing.  The bound is read signed, so a
   count of zero or less touches no record, and the array base is re-read from
   data_fdps_map_unit_array_ptr for each record rather than cached.

   The three callers that reset a turn -- fdps_battle_advance_turn,
   fdps_battle_system_menu and fdps_chapter_15_init -- want exactly that; the
   title demo and the icon script call it for the same reason after driving
   units around outside a real turn. */
extern void fdps_units_clear_status_bit7(void);
#pragma aux fdps_units_clear_status_bit7 "*" parm caller [];

/* Which unit on the map is this character?  Returns the zero-based index into
   the current battle's unit array of the first unit whose char_id -- struct
   fdps_unit_record's byte at record offset 8, the id
   fdps_roster_add_character stamps in when the character joins -- equals
   character_id and which has not been retired, or -1 when there is no such
   unit.  The index is usable with fdps_get_unit_record.

   out_record is the address of a caller pointer and MUST NOT be null: it is
   written before anything else is done, so a null faults rather than being
   rejected.  What it is left holding is part of the answer:

     - null when no record carried the id at all;
     - the returned unit's record on a successful lookup;
     - on a -1 return after matches that were all retired, the LAST matching
       record -- retired units publish their record and let the scan carry on,
       so the pointer survives the miss.

   fdps_draw_text relies on that third case: it tests the pointer for null and
   reads the record's portrait_id before it looks at the return value at all.

   The id compare is on the zero-extended byte, so the domain is 0..255 and a
   negative character_id matches nothing.  The walk is bounded by
   data_fdps_map_unit_count (gamedata.h) with a signed compare, so a count of
   zero or less finds nothing, and the array base is read once at entry -- safe
   here because nothing the scan calls can move the array. */
extern int fdps_battle_find_unit_by_character_id(
    int character_id, struct fdps_unit_record **out_record);
#pragma aux fdps_battle_find_unit_by_character_id "*" parm caller [];

/* Moves the current battle's whole unit-record array to a fresh heap block,
   wipes the storage it came from and frees it, then publishes the new base in
   data_fdps_map_unit_array_ptr (gamedata.h).  Takes nothing, returns nothing,
   and does not touch data_fdps_map_unit_count: this is a move, not a resize.

   Every record pointer, and every cached copy of the array base, is invalid the
   moment this returns -- the old block has been zeroed and handed back to the
   heap.  The three callers (fdps_battle_unit_turn, fdps_battle_enemy_turn_phase
   and fdps_battle_npc_turn_phase) call it once per unit iteration and re-resolve
   through fdps_get_unit_record on the next line, which is the only safe shape.

   The new block is one 0x50-byte record LONGER than the live array and the copy
   runs the new block's full length, so the spare tail record is filled from
   whatever sat past the end of the old allocation; the wipe of the old block
   covers count records and so is one record shorter than the copy.  Both
   lengths are as the original has them and neither may be evened up -- see the
   comment on the definition and rebuild_info/pitfalls.md. */
extern void fdps_relocate_unit_array(void);
#pragma aux fdps_relocate_unit_array "*" parm caller [];

/* Rewrites the low nibble of struct fdps_unit_record's ai_behavior byte at
   record offset 0x34 for every unit in the INCLUSIVE index range
   first_unit_index..last_unit_index, keeping the high nibble of each byte.
   Returns nothing.

   The low nibble is the actor behaviour code fdps_map_actor_behavior_step
   dispatches on -- it masks the byte with 0x0f and compares the result against
   0x0 through 0xb -- and the high nibble carries AI flags the scorers read: bit
   0x40 in fdps_map_actor_take_best_action and bit 0x80 in
   fdps_score_targets_for_item.  Hence the merge: assigning the whole byte, the
   way the behaviour step does when it parks an actor on code 7, would clear
   those flags.

   behavior_mode is ORed in unmasked, so a value above 0x0f sets high-nibble
   bits as well; every call in the shipped image passes 0, which resets the
   behaviour code and leaves the flags standing.

   The range test is signed and inclusive, so a first index greater than the
   last writes no record at all and a negative index is walked.  Neither end is
   checked against data_fdps_map_unit_count, and each record is resolved through
   fdps_get_unit_record, so a call after the array has moved works on the new
   block. */
extern void fdps_object_set_field34_low_nibble_range(int first_unit_index,
                                                     int last_unit_index,
                                                     unsigned char
                                                         behavior_mode);
#pragma aux fdps_object_set_field34_low_nibble_range "*" parm caller [];

#endif
