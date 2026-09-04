/* mapcur.c -- the battle map cursor: drawing its overlay, the information
 * panel beside it, the select loop and the animated move between tiles.
 *
 * See mapcur.h for what each cursor mode paints.  Everything here works out of
 * the shared cursor globals in gamedata.h; the one global the file owns is the
 * column the information panel is parked at, declared in mapcur.h.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "mapcur.h"
#include "maptile.h"
#include "movegrid.h"
#include "sprite.h"
#include "text.h"
#include "unit.h"

/* One map tile is 24 world pixels on each axis: the 0x18 that every displaced
   call site adds or subtracts, and the divisor of the mode 6 arm. */
#define CURSOR_TILE_STEP 0x18

/* The movement grid's cells start after its four-byte header (movegrid.h). */
#define MOVE_GRID_CELL_BASE 4

/* 0002c6a0.  A chain of six CMP dword ptr [0x00069cd0] / JNZ tests at
   0002c6ac, 0002c6d4, 0002c6fc, 0002c79c, 0002c930 and 0002cbc1, each arm
   ending in a JMP to the shared epilogue at 0002cc12.  A mode the chain does
   not name -- 0, or anything above 6 -- falls out of the last JNZ straight
   onto that epilogue, so it is silently nothing rather than an error.

   THE SHAPES ARE CALL LISTS, NOT LOOPS.  Every arm is written out one
   fdps_blit_cursor_tile call per cell in the original, and they are not the
   shapes a loop over a diamond would produce: mode 5 leaves the four tiles
   orthogonally adjacent to the centre blank, and its two lower diagonal cells
   take sprite 0x11 on the left and 0x10 on the right -- the only place in the
   whole list where the sprite ids do not run in reading order.  Folding any of
   this into a loop over a radius, or assuming the ids ascend with position,
   changes what the player sees (rebuild_info/pitfalls.md).

   The cursor position globals are re-read at every call site rather than being
   copied into the frame once (MOV EAX,[0x00069ccc] appears at each displaced
   push), and fdps_blit_cursor_tile writes to neither of them, so a single copy
   would be the same value each time.  They are spelled as the reads the
   original makes.

   MODE 6 IS THE TRAP.  This function is called from the frame compositor
   fdps_draw_scene_layers, and mode 6 draws nothing whatever: it divides both
   cursor coordinates by the tile size and clears byte 1 of the movement grid
   cell under the cursor -- the marker byte fdps_map_grid_reset fills with the
   0xff unreachable sentinel and fdps_map_grid_collect_marked_tiles reads to
   decide a cell is marked.  fdps_map_cursor_move_to draws a frame at every
   step of a cursor move while the mode is non-zero, so the tiles the cursor
   travels across are marked as a side effect of rendering them.  Hoisting the
   mode 6 arm out of the renderer, or drawing a different number of frames
   during a move, silently changes which tiles end up marked.

   Both divisions are signed -- SAR EDX,0x1f / IDIV EBX at 0002cbda and
   0002cbfd -- so a cursor pixel left of or above the map origin truncates
   towards zero and lands on tile 0, where an unsigned division would produce a
   tile index of hundreds of millions and write off the end of the block.  The
   grid width comes out of the header with MOVSX at 0002cbe5, signed for the
   same reason.

   The grid pointer is NOT checked for null here, unlike every function in
   src/movegrid.c: the original loads it at 0002cbdf and dereferences it
   immediately.  Nothing sets mode 6 before a chapter has been loaded, so the
   check the original leaves out is left out. */
void fdps_draw_map_cursor(unsigned char *scene_buffer)
{
    struct fdps_move_grid_cell *grid_cells;
    int grid_width;

    if (data_fdps_map_cursor_draw_mode == 1) {
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              0, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 2) {
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              1, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 3) {
        /* Radius-1 diamond: centre, then above, left, right, below. */
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              0x0e, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              2, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              3, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              4, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              5, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 4) {
        /* Radius-2 diamond, 13 tiles: centre, the four tips two tiles out,
           the four diagonals, then the four tiles one out. */
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              1, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              2, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              3, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              4, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              5, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              6, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              7, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              8, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              9, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              0x0a, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              0x0b, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              0x0c, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              0x0d, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 5) {
        /* Radius-3 diamond, 21 tiles: centre, the four tips three tiles out,
           the eight (1,2) and (2,1) cells of that outer ring in four sprite
           pairs, the four tips two tiles out, and the four diagonals one out
           -- the last of which take 0x0e / 0x0f above and 0x11 / 0x10 below,
           out of numeric order.  The four tiles orthogonally adjacent to the
           centre get no call at all. */
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              1, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 3,
                              2, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 3,
                              data_fdps_map_cursor_world_y,
                              3, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 3,
                              data_fdps_map_cursor_world_y,
                              4, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 3,
                              5, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              6, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              6, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              7, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              7, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              8, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              8, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              9, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              9, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              0x0a, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              0x0b, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              0x0c, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              0x0d, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              0x0e, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              0x0f, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              0x11, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              0x10, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 6) {
        grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
        grid_cells = (struct fdps_move_grid_cell *)
                     (data_fdps_battle_move_grid_ptr + MOVE_GRID_CELL_BASE);
        grid_cells[(data_fdps_map_cursor_world_y / CURSOR_TILE_STEP)
                       * grid_width
                   + data_fdps_map_cursor_world_x / CURSOR_TILE_STEP].marker =
            (unsigned char) 0;
    }
}

/* The panel is drawn into fdps_render_view_frame's own scene buffer, which is
   360 bytes to the row -- PUSH 0x168 ahead of every one of the five drawing
   calls -- and 240 rows tall (the malloc of 0x15180 at 0002bebd). */
#define PANEL_BUF_PITCH 0x168

/* The two columns the panel is parked at: 0x124 puts it against the right edge
   and 0x19 against the left (MOV word ptr [0x0006016c] at 0002dd8b and
   0002dda4). */
#define PANEL_COLUMN_RIGHT 0x124
#define PANEL_COLUMN_LEFT 0x19

/* Where the panel dodges to, in tiles of the visible window: the cursor has to
   be below row 4 before the panel moves at all, and then column 0 or 1 sends
   it right and column 11 or beyond sends it left (CMP [EBP-0x4],0x4 with JLE
   at 0002dd7d and 0002dd96, CMP [EBP-0x8],0x2 with JL at 0002dd83 and
   CMP [EBP-0x8],0xa with JG at 0002dd9c).  Every one of those is a signed
   jump. */
#define PANEL_DODGE_ROW 4
#define PANEL_DODGE_COL_LEFT 2
#define PANEL_DODGE_COL_RIGHT 10

/* The panel's own layout, as rows and columns of the scene buffer.  The two
   cel blits carry their position as arguments -- PUSH 0xa0 and PUSH 0xaf for
   the rows, the panel column and the panel column plus 9 for the columns -- and
   the three that take a pixel pointer carry it folded into one displacement:
   0xe6b3, 0xf621, 0x10b39 and 0x11d8b, which are row * 0x168 + column for the
   rows and columns below.

   The unit sprite lands on exactly the cell the terrain tile was drawn into,
   which is what makes the terrain tile the sprite's backdrop rather than a
   second picture beside it. */
#define PANEL_TOP_ROW 0xa0
#define PANEL_TILE_ROW 0xaf
#define PANEL_TILE_COL 9
#define PANEL_AP_ROW 164
#define PANEL_AP_COL 19
#define PANEL_HP_ROW 190
#define PANEL_HP_COL 9
#define PANEL_DEF_ROW 203
#define PANEL_DEF_COL 19

/* The two percentages are drawn at their natural width with a leading '+' on a
   figure that is not negative; the HP figure is zero-padded to four digits and
   never carries a sign (PUSH 0x0 / MOV EAX,0x1 at 0002de0a and 0002de43
   against PUSH 0x4 / XOR EAX,EAX at 0002df4e). */
#define PANEL_MODIFIER_DIGITS 0
#define PANEL_MODIFIER_SHOW_PLUS 1
#define PANEL_HP_DIGITS 4
#define PANEL_HP_SHOW_PLUS 0

/* Number.cel's third colour row, which a unit below full HP has its figure
   drawn in (MOV dword ptr [0x0006000c],0x3 at 0002df44). */
#define PANEL_HP_HURT_COLOR_ROW 3

/* The unit's map sprite comes out of the .CEL sprite cache, whose offset table
   starts at the block's own base rather than at a .CEL's +0xf, twelve entries
   to a cache slot: four facings of three walk frames (0002dee0, and the same
   layout src/mapdraw.c walks). */
#define PANEL_SPRITES_PER_CACHE_SLOT 0x0c
#define CEL_SUB_IMAGE_ENTRY_BYTES 4

/* 0002dcf0.  The panel is composited over the finished scene by
   fdps_render_view_frame, its only caller, which hands it the same buffer it
   has just had fdps_draw_scene_layers fill.

   THE TWO TILE COORDINATES ARE NOT THE SAME PAIR.  The cursor's position
   within the visible window -- cursor minus view origin, over 24 -- decides
   where the panel is parked, while the tile whose information is shown is the
   cursor's absolute map tile, cursor over 24 with no origin subtracted
   (0002dcfc and 0002dd17 against 0002dd49 and 0002dd5f).  Using either pair
   for both jobs puts the panel on the wrong side the moment the map is
   scrolled.  All four divisions are SAR EDX,0x1f / IDIV EBX, so they truncate
   towards zero and a cursor above or left of the origin lands on tile 0 rather
   than on an enormous positive one.

   THE PANEL COLUMN IS ONLY EVER MOVED, NEVER RESET.  Neither arm of the dodge
   is an else of the other in the sense that one of them always runs: a cursor
   in the top five rows, or in the middle columns, leaves
   data_fdps_ui_terrain_hud_panel_offset holding whatever the last dodge put
   there.  That is the behaviour -- the panel stays where it was until the
   cursor actually reaches a corner it would sit under (mapcur.h).

   THE ATTACK AND DEFENSE FIGURES ARE DRAWN IN THE AMBIENT COLOUR ROW.  Nothing
   here writes data_fdps_number_glyph_color_row before those two calls, so they
   come out in whatever row the previous drawing call left behind; only the HP
   figure gets a row chosen for it, and only when the unit is hurt.  The store
   of 0 afterwards is unconditional and runs even when the HP figure was drawn
   in the ambient row (0002df74), so this function always leaves the row at 0
   once it has reached a unit -- and never touches it at all when it has not.

   The terrain class indexes both modifier tables past their six declared
   entries on shipped maps, and unlike the two combat readers this function has
   no precondition that keeps it inside them; what the original reads there is
   set out where those tables are declared (gamedata.h). */
void fdps_draw_cursor_info_panel(unsigned char *scene_buffer)
{
    struct fdps_unit_record *unit;
    unsigned char *unit_sprite_stream;
    int cursor_view_col;
    int cursor_view_row;
    int unit_index;
    int walk_frame;
    int sprite_cache_entry;
    int hp_current;
    int hp_max;

    cursor_view_col = (data_fdps_map_cursor_world_x
                       - data_fdps_battle_view_window_origin_x)
                      / CURSOR_TILE_STEP;
    cursor_view_row = (data_fdps_map_cursor_world_y
                       - data_fdps_battle_view_window_origin_y)
                      / CURSOR_TILE_STEP;

    if (data_fdps_ui_terrain_hud_user_enabled == 0
        || data_fdps_ui_play_active_flag == 0) {
        return;
    }

    fdps_map_load_tile_info(data_fdps_map_cursor_world_x / CURSOR_TILE_STEP,
                            data_fdps_map_cursor_world_y / CURSOR_TILE_STEP);

    if (cursor_view_row > PANEL_DODGE_ROW
        && cursor_view_col < PANEL_DODGE_COL_LEFT) {
        data_fdps_ui_terrain_hud_panel_offset = PANEL_COLUMN_RIGHT;
    } else if (cursor_view_row > PANEL_DODGE_ROW
               && cursor_view_col > PANEL_DODGE_COL_RIGHT) {
        data_fdps_ui_terrain_hud_panel_offset = PANEL_COLUMN_LEFT;
    }

    fdps_cel_blit_sprite(data_fdps_ui_terrain_hud_panel_sheet_ptr, 0,
                         scene_buffer, PANEL_BUF_PITCH,
                         data_fdps_ui_terrain_hud_panel_offset,
                         PANEL_TOP_ROW, 0, 0);
    fdps_cel_blit_sprite(data_fdps_scene_layer_tile_sheet_ptrs[0],
                         data_fdps_map_tile_info_tile_id,
                         scene_buffer, PANEL_BUF_PITCH,
                         data_fdps_ui_terrain_hud_panel_offset
                             + PANEL_TILE_COL,
                         PANEL_TILE_ROW, 0, 0);

    fdps_draw_number(scene_buffer + data_fdps_ui_terrain_hud_panel_offset
                         + PANEL_AP_ROW * PANEL_BUF_PITCH + PANEL_AP_COL,
                     PANEL_BUF_PITCH,
                     data_fdps_battle_tile_attr_ap_modifier_table[
                         data_fdps_map_tile_terrain_type],
                     PANEL_MODIFIER_DIGITS, PANEL_MODIFIER_SHOW_PLUS);
    fdps_draw_number(scene_buffer + data_fdps_ui_terrain_hud_panel_offset
                         + PANEL_DEF_ROW * PANEL_BUF_PITCH + PANEL_DEF_COL,
                     PANEL_BUF_PITCH,
                     data_fdps_battle_tile_attr_def_modifier_table[
                         data_fdps_map_tile_terrain_type],
                     PANEL_MODIFIER_DIGITS, PANEL_MODIFIER_SHOW_PLUS);

    unit_index = fdps_battle_find_unit_at_cursor();
    if (unit_index == -1) {
        return;
    }

    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
           + unit_index;

    /* 0, 1, 2, 1 as the tick counter runs: the top of the two-bit phase is
       folded back onto 1 rather than wrapping to 0, so the walk cycle rocks
       between the two outer frames instead of jumping (SHR EAX,0x2 /
       AND EAX,0x3 at 0002deb4 and the CMP ...,0x3 at 0002debd).  The sprite is
       always taken from the first facing's three frames. */
    walk_frame = (int) ((data_fdps_timer_tick_counter >> 2) & 3);
    if (walk_frame == 3) {
        walk_frame = 1;
    }

    sprite_cache_entry = unit->sprite_cache_slot
                             * PANEL_SPRITES_PER_CACHE_SLOT
                         + walk_frame;
    unit_sprite_stream = data_fdps_cel_sprite_cache_ptr
        + *(int *) (data_fdps_cel_sprite_cache_ptr
                    + sprite_cache_entry * CEL_SUB_IMAGE_ENTRY_BYTES);
    fdps_blit_dispatch(unit_sprite_stream,
                       scene_buffer + data_fdps_ui_terrain_hud_panel_offset
                           + PANEL_TILE_ROW * PANEL_BUF_PITCH
                           + PANEL_TILE_COL,
                       CURSOR_TILE_STEP, CURSOR_TILE_STEP, PANEL_BUF_PITCH,
                       0, 0);

    hp_current = unit->hp_current;
    hp_max = unit->hp_max;
    if (hp_current != hp_max) {
        data_fdps_number_glyph_color_row = PANEL_HP_HURT_COLOR_ROW;
    }
    fdps_draw_number(scene_buffer + data_fdps_ui_terrain_hud_panel_offset
                         + PANEL_HP_ROW * PANEL_BUF_PITCH + PANEL_HP_COL,
                     PANEL_BUF_PITCH, hp_current,
                     PANEL_HP_DIGITS, PANEL_HP_SHOW_PLUS);
    data_fdps_number_glyph_color_row = 0;
}
