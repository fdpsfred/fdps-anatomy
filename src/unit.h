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
