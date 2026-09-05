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

/* Steps the unit at unit_index one tile UP the screen and returns nothing.
 *
 * The six passes each move the map cursor's pixel row
 * (data_fdps_map_cursor_world_y) back by four, and each scrolls the view
 * origin (data_fdps_battle_view_window_origin_y) back by four as well while
 * two conditions both hold: the unit's STARTING pixel row is less than 48
 * pixels -- two tiles -- below the top of the view, and the view origin is
 * still at least four, so the subtraction cannot take it above the top of the
 * map.  As with the downward step the starting row is measured once before the
 * loop and never remeasured; here the view moves up underneath a fixed row, so
 * the gap GROWS by four each time it scrolls and the scrolling stops once it
 * reaches 48.
 *
 * Unlike the downward step this direction has no map extent to clamp against.
 * The view origin never runs off the top because the >= 4 test alone stops it,
 * so the terrain layer's header is not read here at all.
 *
 * The arrival tile reported to fdps_map_set_pending_tile_event is derived from
 * the map cursor globals and not from the unit record that was just
 * decremented, and the post-loop catch-up frame is drawn on scancode 2 only
 * while the in-loop suppression covers both 2 and 3 -- both exactly as in the
 * downward step, and both behaviour rather than style.
 */
extern void fdps_animate_move_step_up(int unit_index);
#pragma aux fdps_animate_move_step_up "*" parm caller [];

/* Steps the unit at unit_index one tile LEFT across the screen and returns
 * nothing.
 *
 * This is the upward step turned through ninety degrees.  The six passes each
 * move the map cursor's pixel column (data_fdps_map_cursor_world_x) back by
 * four, and each scrolls the view origin
 * (data_fdps_battle_view_window_origin_x) back by four as well while two
 * conditions both hold: the unit's STARTING pixel column is less than 48
 * pixels -- two tiles -- right of the left edge of the view, and the view
 * origin is still at least four, so the subtraction cannot take it left of the
 * map.  The starting column is measured once before the loop and never
 * remeasured, so the view moves left underneath a fixed column, the gap GROWS
 * by four each time it scrolls, and the scrolling stops once it reaches 48.
 *
 * As with the upward step there is no map extent to clamp against and none is
 * read: the >= 4 test alone stops the view, so this routine touches no map
 * layer at all.  The tile column is a plain byte and widens unsigned.
 *
 * The arrival tile reported to fdps_map_set_pending_tile_event is derived from
 * the map cursor globals and not from the unit record that was just
 * decremented, and the post-loop catch-up frame is drawn on scancode 2 only
 * while the in-loop suppression covers both 2 and 3 -- both exactly as in the
 * other directions, and both behaviour rather than style.
 */
extern void fdps_animate_move_step_left(int unit_index);
#pragma aux fdps_animate_move_step_left "*" parm caller [];

/* Steps the unit at unit_index one tile RIGHT across the screen and returns
 * nothing.
 *
 * This is the downward step turned through ninety degrees, and not the
 * leftward one: the two directions that walk towards the far edge of the map
 * are the two that need the map's own extent to stop the view, so this
 * routine reads the terrain layer's header just as the downward step does --
 * the tile WIDTH at +7 rather than the height at +9.  The six passes each
 * advance the map cursor's pixel column (data_fdps_map_cursor_world_x) by
 * four, and each scrolls the view origin
 * (data_fdps_battle_view_window_origin_x) by four as well while two conditions
 * both hold: the unit's STARTING pixel column is more than 240 pixels right of
 * the left edge of the view, and the view has not already reached the right
 * edge of the map, which is the map's pixel width less the 312-pixel view
 * width.  The starting column is measured once before the loop and is never
 * remeasured, so the first test uses a value that does not move while the view
 * underneath it does and the gap CLOSES by four each time it scrolls.
 *
 * The tile column is a plain byte and widens unsigned; the map width is read
 * signed.
 *
 * The arrival tile reported to fdps_map_set_pending_tile_event is derived from
 * the map cursor globals and not from the unit record that was just
 * incremented, and the post-loop catch-up frame is drawn on scancode 2 only
 * while the in-loop suppression covers both 2 and 3 -- both exactly as in the
 * other directions, and both behaviour rather than style.
 */
extern void fdps_animate_move_step_right(int unit_index);
#pragma aux fdps_animate_move_step_right "*" parm caller [];

#endif
