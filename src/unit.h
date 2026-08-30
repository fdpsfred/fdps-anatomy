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

#endif
