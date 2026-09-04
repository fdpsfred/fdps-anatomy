/* mapdraw.c -- composing the scrolling scene layers and the map units drawn
 * between them.
 *
 * See mapdraw.h for what a layer slot is and why the draw order matters.
 * Nothing here owns state: the layer arrays belong to the chapter resource
 * loader, and this file only reads them.
 */
#include "gamedata.h"
#include "blit.h"
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

/* The 24x24 tile the whole scene is built out of, the 360-byte pitch of the
   scene buffer it is composed into, and the 14x9 window of tiles the visible
   view covers.  All four are literals in the assembly: IMUL EAX,EAX,0x168 at
   0002c454 for the pitch, the PUSH 0x18 pairs at every call site for the blit
   rectangle, and the CMP ...,0x9 and CMP ...,0xe loop bounds at 0002c437 and
   0002c46d. */
#define SCENE_TILE_SIZE 0x18
#define SCENE_BUF_PITCH 0x168
#define SCENE_WINDOW_ROWS 9
#define SCENE_WINDOW_COLS 0x0e

/* The tile map's header: signed 16-bit tile width at +7, signed 16-bit tile
   height at +9, and the 16-bit tile ids from +0xb, as gamedata.h states for
   data_fdps_scene_layer_tile_map_ptrs.  Both dimensions are read MOVSX at
   0002c3b3 and 0002c3bd and the tile id MOVSX at 0002c4f9. */
#define TILE_MAP_WIDTH_OFFSET 7
#define TILE_MAP_HEIGHT_OFFSET 9
#define TILE_MAP_TILE_ID_OFFSET 0x0b

/* A tile map's id and a movement grid's cell are both two bytes wide, which is
   why the one offset the assembly forms at 0002c4e4 -- (y * width + x) * 2 --
   serves as the byte offset into both. */
#define SCENE_CELL_ENTRY_BYTES 2

/* The movement grid's four-byte header sits in front of its cells and byte 1
   of a cell is the flood fill's marker, so the marker belonging to
   cell_byte_offset is at +5 (MOV AL,byte ptr [EAX + 0x5] at 0002c5be).  0xff
   is what fdps_map_grid_reset writes into every cell and so means "unmarked";
   the compare is unsigned (AND EAX,0xff / CMP EAX,0xff). */
#define MOVE_GRID_CELL_BASE 4
#define MOVE_GRID_CELL_MARKER 1
#define MOVE_GRID_MARKER_UNREACHABLE 0xff

/* The attribute table's 0x11-byte header and its 4-byte rows -- struct
   fdps_tile_attr_entry -- of which byte 0 is the flag byte and byte 1 the
   fixed blend level. */
#define TILE_ATTR_HEADER_BYTES 0x11
#define TILE_ATTR_ENTRY_BYTES 4

/* The tileset is a .CEL sheet: a dword offset table at +0xf indexed by tile
   id, each entry the offset from the sheet base to that tile's RLE stream. */
#define CEL_SUB_IMAGE_TABLE_OFFSET 0x0f
#define CEL_SUB_IMAGE_ENTRY_BYTES 4

/* The low three bits of a tile's flag byte pick its animation, and bit 0x80
   asks for the fixed-level translucent draw.  The four-frame animation's
   frames sit 0x60 tile ids apart in the sheet and advance every fourth tick. */
#define TILE_ANIM_KIND_MASK 7
#define TILE_ANIM_TWO_FRAME 1
#define TILE_ANIM_FOUR_FRAME 2
#define TILE_ANIM_FOUR_FRAME_TICK_SHIFT 2
#define TILE_ANIM_FOUR_FRAME_MASK 3
#define TILE_ANIM_FOUR_FRAME_STRIDE 0x60
#define TILE_FLAG_TRANSLUCENT 0x80

/* The blend levels the movement-range highlight pulses through, and how the
   phase counter picks one: the 14-byte table at 0002b280, indexed by the phase
   divided by four, with the phase itself running 0..0x37. */
#define BLEND_RAMP_ENTRIES 14
#define BLEND_PHASE_MODULO 0x38
#define BLEND_PHASE_PER_RAMP_STEP 4

/* How far data_fdps_timer_tick_counter has to move past the latch before the
   two-frame tile animation flips: CMP EAX,0x3 / JBE at 0002c39a, so strictly
   more than three, and the compare is unsigned. */
#define TILE_ANIM_TICK_INTERVAL 3

/* The four slots of the blend descriptor fdps_blit_dispatch modes 9 and 0x0a
   read, named as src/rleblend.c names them. */
#define BLEND_DESC_SHADE_RAMP 0
#define BLEND_DESC_LEVEL 1
#define BLEND_DESC_CUBE 2
#define BLEND_DESC_TINT_COLOR 3
#define BLEND_DESC_SLOTS 4

/* The three fdps_blit_dispatch modes this file asks for. */
#define BLIT_MODE_OPAQUE 0
#define BLIT_MODE_TRANSLUCENT 9
#define BLIT_MODE_TINT_SPRITE_AND_BACKDROP 0x0a

/* 0002c330.  One layer of the scrolling scene: the visible 14x9 window of
   24x24 tiles blitted into the scene buffer, with the tiles the layer's
   attribute table marks animated and the tiles the movement grid marks
   pulsing.  Eight stack arguments, caller-cleaned -- both call sites in
   fdps_draw_scene_layers push all eight and follow the CALL with ADD ESP,0x20,
   at 0002c0e6 and 0002c202.

   THE HIGHLIGHT PHASE STEPS ONCE PER LAYER, NOT ONCE PER FRAME.  The
   data_fdps_marked_tile_blend_phase advance is at the top of THIS function,
   which fdps_draw_scene_layers calls once per active layer, so the
   movement-range highlight pulses faster on a map carrying more layers.
   Moving the step up beside the data_fdps_scene_tile_anim_phase flip -- which
   really is once per frame, because its tick guard swallows the extra calls --
   changes the pulse rate and decouples it from the layer count
   (rebuild_info/pitfalls.md).

   THE SCROLL SPLIT FLOORS BY HAND AND THE FIX-UP DOES NOT LOOK AT THE
   REMAINDER.  C division truncates toward zero, so a negative pixel offset is
   corrected with a decrement of the tile count and a + 24 on the remainder
   whatever that remainder was -- including an exact multiple, where it was
   already 0 and becomes 24.  That is not a defect: tile n plus 24 pixels is
   tile n + 1 plus 0 pixels, so the origin lands in the same place and only
   which fourteen columns the window covers shifts by one.  A real floor moves
   that window.

   THE FLAG BYTE IS READ AT THE UNANIMATED TILE AND THE FIXED BLEND LEVEL AT
   THE ANIMATED ONE.  tile_attr[tile * 4] is fetched at 0002c50a before the
   animation adds to tile_id and tile_attr[tile * 4 + 1] at 0002c579 after, so
   an animated translucent tile takes its flags from the unanimated row and its
   translucency level from the row of the frame actually being drawn.  Reading
   both from one row -- either row -- agrees only while the frames' levels
   match.

   BOTH CELL COORDINATES ARE WRAPPED WITH THE MAP'S DIMENSIONS AND THE MOVEMENT
   GRID'S OWN HEADER IS NEVER READ.  The single cell_byte_offset the map's tile
   id comes from is the same offset the grid's marker comes from, so a grid
   allocated to dimensions other than this layer's would be indexed as though
   it were the layer's.  The battle map is layer 0 and they agree there;
   nothing here checks it.

   THE MODULO IS SKIPPED WHEN THE COORDINATE IS ALREADY IN RANGE, which is what
   keeps the common case off the divider -- and is also the only thing standing
   between a map dimension of 0 and a divide by zero, for every coordinate the
   range test lets through.

   scroll_x and scroll_y are this layer's view origin in pixels and may be
   negative; the caller forms each as its scroll accumulator plus the view
   origin times the layer's parallax factor, arithmetic-shifted right by three.
   layer_mode is data_fdps_scene_layer_tile_attr_mode[slot] widened unsigned:
   0 draws every tile opaque and touches neither tile_attr nor move_grid, 1
   tints the marked and translucent tiles toward
   data_fdps_scene_marked_tile_tint_color, and any other non-zero value blends
   them with what the buffer already holds.

   tile_attr is advanced past its header on every call, whether or not
   layer_mode ever lets a row of it be read. */
void fdps_draw_scene_layer(unsigned char *scene_buf, unsigned char *map_layer,
                           unsigned char *tileset, unsigned char *move_grid,
                           int scroll_x, int scroll_y,
                           unsigned char *tile_attr, int layer_mode)
{
    /* The 14 blend levels the highlight pulses through, copied onto the stack
       from the table at 0002b280 by the three MOVSD and the MOVSW at
       0002c344. */
    unsigned char blend_ramp[BLEND_RAMP_ENTRIES] = {
        2, 3, 4, 5, 6, 7, 7, 7, 6, 5, 4, 3, 2, 2
    };
    int blend_desc[BLEND_DESC_SLOTS];
    int map_tile_width;
    int map_tile_height;
    int scroll_tile_x;
    int scroll_tile_y;
    int scroll_pixel_x;
    int scroll_pixel_y;
    int window_row;
    int window_col;
    int cell_x;
    int cell_y;
    int cell_byte_offset;
    int tile_id;
    unsigned char *row_base;
    unsigned char *tile_stream;
    unsigned char tile_flags;
    unsigned char tile_anim_kind;
    unsigned char blit_mode;

    tile_attr += TILE_ATTR_HEADER_BYTES;
    data_fdps_marked_tile_blend_phase =
        (data_fdps_marked_tile_blend_phase + 1) % BLEND_PHASE_MODULO;

    blend_desc[BLEND_DESC_SHADE_RAMP] =
        (int) data_fdps_palette_shade_ramp_table;
    blend_desc[BLEND_DESC_CUBE] = (int) data_fdps_inverse_palette_cube;
    blend_desc[BLEND_DESC_TINT_COLOR] = data_fdps_scene_marked_tile_tint_color;

    if (data_fdps_scene_tile_anim_last_flip_tick == 0) {
        data_fdps_scene_tile_anim_last_flip_tick = data_fdps_timer_tick_counter;
    }
    if (data_fdps_timer_tick_counter - data_fdps_scene_tile_anim_last_flip_tick
            > TILE_ANIM_TICK_INTERVAL) {
        data_fdps_scene_tile_anim_phase ^= 1;
        data_fdps_scene_tile_anim_last_flip_tick = data_fdps_timer_tick_counter;
    }

    map_tile_width = *(short *) (map_layer + TILE_MAP_WIDTH_OFFSET);
    map_tile_height = *(short *) (map_layer + TILE_MAP_HEIGHT_OFFSET);

    scroll_tile_x = scroll_x / SCENE_TILE_SIZE;
    scroll_pixel_x = scroll_x % SCENE_TILE_SIZE;
    scroll_tile_y = scroll_y / SCENE_TILE_SIZE;
    scroll_pixel_y = scroll_y % SCENE_TILE_SIZE;
    if (scroll_x < 0) {
        scroll_tile_x--;
        scroll_pixel_x += SCENE_TILE_SIZE;
    }
    if (scroll_y < 0) {
        scroll_tile_y--;
        scroll_pixel_y += SCENE_TILE_SIZE;
    }

    for (window_row = 0; window_row < SCENE_WINDOW_ROWS; window_row++) {
        row_base = scene_buf
            + (window_row * SCENE_TILE_SIZE + SCENE_TILE_SIZE - scroll_pixel_y)
              * SCENE_BUF_PITCH
            + SCENE_TILE_SIZE - scroll_pixel_x;

        for (window_col = 0; window_col < SCENE_WINDOW_COLS; window_col++) {
            cell_y = window_row + scroll_tile_y;
            cell_x = window_col + scroll_tile_x;

            if (cell_x < 0 || cell_x >= map_tile_width) {
                cell_x = cell_x % map_tile_width;
                if (cell_x < 0) {
                    cell_x += map_tile_width;
                }
            }
            if (cell_y < 0 || cell_y >= map_tile_height) {
                cell_y = cell_y % map_tile_height;
                if (cell_y < 0) {
                    cell_y += map_tile_height;
                }
            }

            cell_byte_offset = (cell_y * map_tile_width + cell_x)
                               * SCENE_CELL_ENTRY_BYTES;
            tile_id = *(short *) (map_layer + cell_byte_offset
                                  + TILE_MAP_TILE_ID_OFFSET);

            if (layer_mode == 0) {
                tile_stream = tileset
                    + *(int *) (tileset
                                + tile_id * CEL_SUB_IMAGE_ENTRY_BYTES
                                + CEL_SUB_IMAGE_TABLE_OFFSET);
                fdps_blit_dispatch(tile_stream,
                                   row_base + window_col * SCENE_TILE_SIZE,
                                   SCENE_TILE_SIZE, SCENE_TILE_SIZE,
                                   SCENE_BUF_PITCH, 0, BLIT_MODE_OPAQUE);
            } else {
                tile_flags = tile_attr[tile_id * TILE_ATTR_ENTRY_BYTES];
                tile_anim_kind =
                    (unsigned char) (tile_flags & TILE_ANIM_KIND_MASK);
                if (tile_anim_kind == TILE_ANIM_TWO_FRAME) {
                    tile_id += data_fdps_scene_tile_anim_phase;
                } else if (tile_anim_kind == TILE_ANIM_FOUR_FRAME) {
                    tile_id += (int) ((data_fdps_timer_tick_counter
                                       >> TILE_ANIM_FOUR_FRAME_TICK_SHIFT)
                                      & TILE_ANIM_FOUR_FRAME_MASK)
                               * TILE_ANIM_FOUR_FRAME_STRIDE;
                }

                tile_stream = tileset
                    + *(int *) (tileset
                                + tile_id * CEL_SUB_IMAGE_ENTRY_BYTES
                                + CEL_SUB_IMAGE_TABLE_OFFSET);

                if ((tile_flags & TILE_FLAG_TRANSLUCENT) != 0) {
                    blend_desc[BLEND_DESC_LEVEL] =
                        tile_attr[tile_id * TILE_ATTR_ENTRY_BYTES + 1];
                    fdps_blit_dispatch(tile_stream,
                                       row_base + window_col * SCENE_TILE_SIZE,
                                       SCENE_TILE_SIZE, SCENE_TILE_SIZE,
                                       SCENE_BUF_PITCH,
                                       (unsigned int) blend_desc,
                                       BLIT_MODE_TRANSLUCENT);
                } else if (move_grid[cell_byte_offset + MOVE_GRID_CELL_BASE
                                     + MOVE_GRID_CELL_MARKER]
                           == MOVE_GRID_MARKER_UNREACHABLE) {
                    fdps_blit_dispatch(tile_stream,
                                       row_base + window_col * SCENE_TILE_SIZE,
                                       SCENE_TILE_SIZE, SCENE_TILE_SIZE,
                                       SCENE_BUF_PITCH, 0, BLIT_MODE_OPAQUE);
                } else {
                    blend_desc[BLEND_DESC_LEVEL] =
                        blend_ramp[data_fdps_marked_tile_blend_phase
                                   / BLEND_PHASE_PER_RAMP_STEP];
                    if (layer_mode == 1) {
                        blit_mode = BLIT_MODE_TINT_SPRITE_AND_BACKDROP;
                    } else {
                        blit_mode = BLIT_MODE_TRANSLUCENT;
                    }
                    fdps_blit_dispatch(tile_stream,
                                       row_base + window_col * SCENE_TILE_SIZE,
                                       SCENE_TILE_SIZE, SCENE_TILE_SIZE,
                                       SCENE_BUF_PITCH,
                                       (unsigned int) blend_desc, blit_mode);
                }
            }
        }
    }
}
