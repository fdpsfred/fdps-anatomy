/* walk.h -- animating a map unit along a movement path, one tile at a time.
 *
 * fdps_animate_move_path walks the path byte string the movement code handed
 * it and dispatches each direction code to one of four per-direction step
 * routines.  A step routine moves one unit one tile: it plays the six-frame
 * walk animation, scrolls the battle view to keep the unit on screen, commits
 * the new tile position into the unit's record and reports the arrival tile so
 * a scripted chapter event sitting there can fire.
 *
 * The four routines share a shape.  Six passes of four pixels make one
 * 24-pixel tile; the sub-step counter in the unit record drives which walk
 * frame src/mapdraw.c draws; and the scancode latch behind
 * fdps_keyboard_scancode_ptr can suppress the per-pass frame so a player
 * holding a fast-forward key does not have to sit through the animation.
 *
 * Nothing here owns state.  The unit records belong to the map unit array, the
 * view origin and the map cursor are gamedata.c's, and the map layers belong
 * to the chapter resource loader.
 */
#ifndef WALK_H
#define WALK_H

/* Steps the unit at unit_index one tile DOWN the screen and returns nothing.
 *
 * The six passes each advance the map cursor's pixel row
 * (data_fdps_map_cursor_world_y) by four, and each scrolls the view origin
 * (data_fdps_battle_view_window_origin_y) by four as well while two conditions
 * both hold: the unit's STARTING pixel row is more than 120 pixels below the
 * top of the view, and the view has not already reached the bottom of the map,
 * which is the map's pixel height less the 192-pixel view height.  The
 * starting row is measured once before the loop and is never remeasured, so
 * the first test uses a value that does not move while the view underneath it
 * does.
 *
 * Two details are behaviour and not style.
 *
 * The arrival tile reported to fdps_map_set_pending_tile_event is derived from
 * the map cursor globals and not from the unit record that was just updated.
 * The two agree only while the view is locked onto the moving unit, so
 * substituting the record's own pos_x and pos_y is a different program.
 *
 * The frame drawn after the loop is drawn on scancode 2 only, while the
 * in-loop suppression covers both 2 and 3.  Folding the two tests together
 * loses one rendered frame per tile step under key 2, or adds one under key 3.
 */
extern void fdps_walk_step_down(int unit_index);
#pragma aux fdps_walk_step_down "*" parm caller [];

#endif
