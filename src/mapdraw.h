/* mapdraw.h -- composing the scrolling scene: the six parallax layer slots,
 * the order they are blitted in, and the map units drawn between them.
 *
 * A loaded chapter fills data_fdps_scene_layer_count of the six layer slots
 * (gamedata.h), each slot being one row across the parallel arrays there: a
 * tile map, a tileset, an attribute table, a scroll accumulator and step pair
 * per axis, a parallax factor per axis, and a depth byte.
 *
 * Drawing the scene is two passes over the same depth-sorted slot list with
 * the map units drawn in between, which is what makes the order matter: the
 * slots whose depth byte is below 10 go down first as background, the units
 * go on top of them, and the slots above 10 go over the units as foreground.
 */
#ifndef MAPDRAW_H
#define MAPDRAW_H

/* Fills draw_order with the layer slot indices 0, 1, ... sorted by ascending
   depth byte, and returns nothing: the list is the caller's array.

   draw_order must have room for data_fdps_scene_layer_count ints; nothing here
   checks it, and the only caller passes a six-element stack array to match the
   six-slot width of the layer table. */
extern void fdps_build_scene_layer_draw_order(int *draw_order);
#pragma aux fdps_build_scene_layer_draw_order "*" parm caller [];

#endif
