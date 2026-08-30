/* mapdraw.c -- composing the scrolling scene layers and the map units drawn
 * between them.
 *
 * See mapdraw.h for what a layer slot is and why the draw order matters.
 * Nothing here owns state: the layer arrays belong to the chapter resource
 * loader, and this file only reads them.
 */
#include "gamedata.h"
#include "mapdraw.h"

/* 0002c220.  A hand-written bubble sort over the layer slot indices, keyed on
   the depth byte, with no callee and nothing returned.

   Two things about it are behaviour rather than style.

   The key comparison is UNSIGNED.  The assembly loads the two depth bytes into
   AL and compares them with CMP AL,byte ptr [EDX+0x69cfe] / JBE at 0002c2c4,
   and data_fdps_scene_layer_draw_depth is an unsigned char array, so a depth
   of 0x80 sorts after a depth of 0x01.  Read through a signed char the same
   pair sorts the other way round and the two layers swap over each other on
   screen.

   The sort is STABLE and that is load-bearing.  Two slots carrying the same
   depth byte are left in slot-index order -- the swap only fires on a strict
   greater-than -- and fdps_draw_scene_layers blits them in exactly the order
   this list gives, so which of two equal-depth layers covers the other is
   fixed by their slot numbers.  Replacing this with qsort, which makes no
   stability promise, would reorder them silently
   (rebuild_info/pitfalls.md).

   The bounds come out of the assembly's three loop tests and are not the
   obvious ones to guess: the fill loop runs while i < count (0002c236, JL),
   the outer pass while count - 1 > i (DEC EAX / CMP / JG at 0002c26a), and the
   inner comparison while count - i - 1 > j (SUB EAX / DEC EAX / CMP / JG at
   0002c289).  So the inner loop reads draw_order[j + 1] at most at
   j + 1 == count - i - 1, one short of count, and never past the end of what
   the fill loop wrote. */
void fdps_build_scene_layer_draw_order(int *draw_order)
{
    int i;
    int j;
    int held_slot;

    for (i = 0; i < data_fdps_scene_layer_count; i++) {
        draw_order[i] = i;
    }

    for (i = 0; i < data_fdps_scene_layer_count - 1; i++) {
        for (j = 0; j < data_fdps_scene_layer_count - i - 1; j++) {
            if (data_fdps_scene_layer_draw_depth[draw_order[j]] >
                data_fdps_scene_layer_draw_depth[draw_order[j + 1]]) {
                held_slot = draw_order[j];
                draw_order[j] = draw_order[j + 1];
                draw_order[j + 1] = held_slot;
            }
        }
    }
}
