/* menu.h -- the four-way command menus the battle screens put a cursor on.
 *
 * A menu is described by four ints, one per entry, in the order the entries sit
 * on screen: 0 up, 1 left, 2 right, 3 down.  Zero means the entry is
 * selectable, non-zero means it is greyed out.  The descriptor is supplied by
 * the caller -- fdps_battle_item_menu copies a static one and patches it,
 * fdps_battle_action_menu fills its own from the per-command legality probes.
 */
#ifndef MENU_H
#define MENU_H

/* Returns the index 0..3 of the first selectable entry, or -1 when all four
   are greyed out.  Only reads the descriptor. */
extern int fdps_menu_find_first_enabled_entry(int *cmd_disabled);
#pragma aux fdps_menu_find_first_enabled_entry "*" parm caller [];

#endif
