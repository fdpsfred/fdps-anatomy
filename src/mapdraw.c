/* mapdraw.c -- composing the scrolling scene layers and the map units drawn
 * between them.
 *
 * See mapdraw.h for what a layer slot is and why the draw order matters.
 * Nothing here owns state: the layer arrays belong to the chapter resource
 * loader, and this file only reads them.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "unit.h"
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

/* One more fdps_blit_dispatch mode this file asks for: 0x0b, the tinting
   sprite blit, which weighs every pixel it draws toward the descriptor's tint
   colour and leaves the transparent runs alone (rleblend.h). */
#define BLIT_MODE_TINT_SPRITE 0x0b

/* The walk-animation clock.  The walk counter wraps at 0x10 -- IDIV by 0x10 at
   0002cdf6 -- and the status-icon counter at 0x19 (0002ce0f); the walk counter
   divided by four gives the frame, and the value 3 is folded back to 1 so the
   three sprites of a facing play 0, 1, 2, 1. */
#define MAP_UNIT_WALK_ANIM_MODULO 0x10
#define MAP_UNIT_STATUS_ICON_MODULO 0x19
#define MAP_UNIT_WALK_FRAME_TICKS 4
#define MAP_UNIT_WALK_FRAME_FOLDED 3
#define MAP_UNIT_WALK_FRAME_FOLD_TO 1

/* A tile is 24 pixels and a unit takes six 4-pixel steps to cross one, so the
   sub-tile step counter at record +4 is scaled by four (the four literals at
   0002cebe, 0002cecd, 0002cedc and 0002cee5).  The sprite is drawn six pixels
   above its cell because it stands taller than the tile (SUB EAX,0x6 at
   0002cefd). */
#define MAP_UNIT_STEP_PIXELS 4
#define MAP_UNIT_SPRITE_LIFT 6

/* The three facing codes record +3 is tested against, in the order the compare
   chain at 0002ceb8 takes them: 0 is +y, 1 is -x, 2 is -y and everything else
   falls into the else and goes +x.  The last is not a test, so a facing byte
   above 3 walks right. */
#define MAP_UNIT_FACING_DOWN 0
#define MAP_UNIT_FACING_LEFT 1
#define MAP_UNIT_FACING_UP 2

/* The camera window the unit's world-pixel position is clipped against.  x
   runs origin - 0x18 exclusive to origin + 0x138 exclusive (0002cf56 and
   0002cf63); the sprite pass takes y from origin - 0x18 to origin + 0xc0
   (0002d096, 0002d0a3) and the shadow pass from origin - 0x1a to origin + 0xbe
   (0002cfa8, 0002cfb5), two pixels higher at both ends because the shadow is
   laid down two scanlines below the cell. */
#define MAP_VIEW_WIDTH 0x138
#define MAP_VIEW_HEIGHT 0xc0
#define MAP_SHADOW_VIEW_TOP_MARGIN 0x1a
#define MAP_SHADOW_VIEW_HEIGHT 0xbe
#define MAP_SHADOW_DEST_ROWS 2

/* struct fdps_unit_record's flag byte at +5: bit 0 retires the unit, which
   draws nothing at all, and bit 0x80 says it has already acted this turn,
   which greys the sprite and pins both sprite and shadow to frame 1. */
#define UNIT_FLAG_RETIRED 0x01
#define UNIT_FLAG_ACTED 0x80
#define UNIT_ACTED_SPRITE_FRAME 1

/* Portrait id 0x80 marks a record that has no map sprite at all: the routine
   returns before it reads anything else off the record (0002ce72). */
#define PORTRAIT_ID_NO_MAP_SPRITE 0x80

/* The portrait ids the sprite pass draws semi-transparent instead of opaque --
   the span 0x24..0x27 and the two ids 0x69 and 0x6a (0002d0f0 onward).  They
   are also part of the set that casts no shadow.  assets/characters.md puts
   the table split at 60: an id below 0x3c indexes FRIAPRDA.DAT / FRILEVUP.DAT
   and 0x3c or above indexes ENEMYDAT.DAT at id - 60.  So these are ordinary
   entries of those two tables -- 0x24..0x27 fall inside the 0x24..0x31 run
   that shares one filler row in FRIAPRDA.DAT, and 0x69 and 0x6a are
   ENEMYDAT.DAT records 45 and 46, whose contents that document does not
   decode.  What any of them portrays is therefore not established here; the
   names say what the code does with them. */
#define TRANSLUCENT_PORTRAIT_ID_LOW 0x24
#define TRANSLUCENT_PORTRAIT_ID_HIGH 0x27
#define TRANSLUCENT_PORTRAIT_ID_A 0x69
#define TRANSLUCENT_PORTRAIT_ID_B 0x6a

/* Two further sets cast no shadow although they draw opaque: the single id
   0x8e and the enemy span 0x3c..0x3e, which is ENEMYDAT.DAT's first three
   records (0002cfc1 and 0002cfec). */
#define NO_SHADOW_PORTRAIT_ID 0x8e
#define NO_SHADOW_ENEMY_PORTRAIT_ID_LOW 0x3c
#define NO_SHADOW_ENEMY_PORTRAIT_ID_HIGH 0x3e

/* The blend levels the three non-opaque unit draws ask for: 0xa for a shadow
   (0002cfff), 8 for a unit that has already acted (0002d153) and 7 for the
   semi-transparent portrait ids (0002d10c). */
#define SHADOW_BLEND_LEVEL 0x0a
#define ACTED_UNIT_BLEND_LEVEL 8
#define TRANSLUCENT_UNIT_BLEND_LEVEL 7

/* The sprite cache at data_fdps_cel_sprite_cache_ptr holds twelve stream
   offsets per slot, four facings of three walk frames, and ITS offset table
   starts at the base rather than at +0x0f the way a .CEL file's does -- the
   cache is a table fdps_cache_cel_sprite_group builds, not a loaded sheet
   (0002d0c7). */
#define MAP_UNIT_SPRITES_PER_CACHE_SLOT 0x0c
#define MAP_UNIT_WALK_FRAMES_PER_FACING 3

/* The status icon is the one blit here that is not a 24x24 square: a 0x18 by
   0x0b strip out of the IconSts.cel sheet, put down 13 scanlines below the top
   of the unit's cell (ADD EAX,0x1248 at 0002d1eb, which is 13 * 0x168). */
#define STATUS_ICON_WIDTH 0x18
#define STATUS_ICON_ROWS 0x0b
#define STATUS_ICON_DEST_ROWS 13
#define STATUS_ICON_NONE (-1)

/* struct fdps_unit_record's status_timers[4], record +0x26: the paralysis
   counter, the same slot src/aitarget.c reads. */
#define PARALYSIS_TIMER_SLOT 4

/* 0002cda0.  One unit of the battle map, drawn into the scene buffer.  Three
   stack arguments, caller-cleaned: all three call sites push right to left and
   follow the CALL with ADD ESP,0xc, at 00015b3c, 0002d282 and 0002d2bd.

   THE ANIMATION CLOCK IS STEPPED HERE AND ONLY WHEN THE TICK HAS MOVED.  The
   latch is what holds it to one step per frame across the two passes and the
   whole unit list; see the note on data_fdps_map_unit_anim_last_tick in
   mapdraw.h.

   THE TWO PASSES CLIP AGAINST DIFFERENT VERTICAL WINDOWS.  The shadow's is two
   pixels higher at both ends because the shadow itself is blitted two
   scanlines lower, so the pair keeps a shadow and its sprite appearing and
   disappearing together at the top and bottom edges of the view.  The
   horizontal window is shared and is tested before either.

   EVERY COMPARE ON THE PORTRAIT ID IS SIGNED and the id is a zero-extended
   byte (XOR EAX,EAX / MOV AL,byte ptr [EDX + 0x7] at 0002ce46), so the value
   under test is 0..255 and signed and unsigned agree; it is held in an int
   here for that reason rather than widened from the record's unsigned char at
   each use.  The window compares are signed too, and there the sign matters: a
   view origin near zero makes origin - 0x18 negative, and an unsigned test
   would then pass every unit on the map.

   THE SUB-TILE DISPLACEMENT IS A CHAIN OF THREE TESTS AND AN ELSE, not a
   four-way switch: facing 0 is +y, 1 is -x, 2 is -y and every other value,
   3 included, is +x.

   A PARALYSED UNIT IS PINNED TO FRAME 0 AND JITTERED ONE PIXEL IN X instead of
   walking, the jitter being the walk counter modulo 2 -- a signed IDIV at
   0002cf4c, matching the signed counter.

   Both sheet reads follow the .CEL rule that a stored offset is measured from
   the start of the file, so the table entry is added back to the sheet base
   and never to the address it was read from.  The sprite cache is the
   exception noted on MAP_UNIT_SPRITES_PER_CACHE_SLOT above: its table starts
   at the base.

   Nothing is bounds-checked: neither unit_index, nor the sprite index the
   cache slot forms, nor the icon index the selector returns. */
void fdps_draw_map_unit(int unit_index, unsigned char *scene_buf,
                        unsigned char unused_flag)
{
    struct fdps_unit_record *unit;
    int blend_desc[BLEND_DESC_SLOTS];
    int portrait_id;
    int cache_slot;
    int facing;
    int walk_step;
    int status_flags;
    int step_dx;
    int step_dy;
    int draw_x;
    int draw_y;
    int walk_frame;
    int shadow_frame;
    int sprite_index;
    int status_icon;
    unsigned char *sprite_stream;
    unsigned char *dest_pixel;

    step_dx = 0;
    step_dy = 0;
    unused_flag = 0;
    blend_desc[BLEND_DESC_SHADE_RAMP] =
        (int) data_fdps_palette_shade_ramp_table;
    blend_desc[BLEND_DESC_CUBE] = (int) data_fdps_inverse_palette_cube;

    if (data_fdps_timer_tick_counter != data_fdps_map_unit_anim_last_tick) {
        data_fdps_map_unit_anim_last_tick = data_fdps_timer_tick_counter;
        data_fdps_map_unit_walk_anim_counter =
            (data_fdps_map_unit_walk_anim_counter + 1)
            % MAP_UNIT_WALK_ANIM_MODULO;
        data_fdps_map_unit_status_icon_tick_counter =
            (data_fdps_map_unit_status_icon_tick_counter + 1)
            % MAP_UNIT_STATUS_ICON_MODULO;
        if (data_fdps_map_unit_status_icon_tick_counter == 0) {
            data_fdps_map_unit_status_icon_cycle++;
        }
    }

    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
           + unit_index;
    portrait_id = unit->portrait_id;
    draw_x = unit->pos_x * SCENE_TILE_SIZE;
    draw_y = unit->pos_y * SCENE_TILE_SIZE;
    if (portrait_id == PORTRAIT_ID_NO_MAP_SPRITE) {
        return;
    }

    cache_slot = unit->sprite_cache_slot;
    facing = unit->facing;
    walk_step = unit->walk_step;
    status_flags = unit->flags;
    if ((status_flags & UNIT_FLAG_RETIRED) != 0) {
        return;
    }

    if (facing == MAP_UNIT_FACING_DOWN) {
        step_dy = MAP_UNIT_STEP_PIXELS;
    } else if (facing == MAP_UNIT_FACING_LEFT) {
        step_dx = -MAP_UNIT_STEP_PIXELS;
    } else if (facing == MAP_UNIT_FACING_UP) {
        step_dy = -MAP_UNIT_STEP_PIXELS;
    } else {
        step_dx = MAP_UNIT_STEP_PIXELS;
    }
    draw_x += step_dx * walk_step;
    draw_y += step_dy * walk_step - MAP_UNIT_SPRITE_LIFT;

    walk_frame =
        data_fdps_map_unit_walk_anim_counter / MAP_UNIT_WALK_FRAME_TICKS;
    if (walk_frame == MAP_UNIT_WALK_FRAME_FOLDED) {
        walk_frame = MAP_UNIT_WALK_FRAME_FOLD_TO;
    }
    if (unit->status_timers[PARALYSIS_TIMER_SLOT] != 0) {
        walk_frame = 0;
        draw_x += data_fdps_map_unit_walk_anim_counter % 2;
    }

    if (data_fdps_battle_view_window_origin_x - SCENE_TILE_SIZE < draw_x
        && draw_x < data_fdps_battle_view_window_origin_x + MAP_VIEW_WIDTH) {
        dest_pixel = scene_buf
            + (draw_y - data_fdps_battle_view_window_origin_y
               + SCENE_TILE_SIZE) * SCENE_BUF_PITCH
            + draw_x - data_fdps_battle_view_window_origin_x
            + SCENE_TILE_SIZE;

        if (data_fdps_map_unit_shadow_pass_flag != 0) {
            if (data_fdps_battle_view_window_origin_y
                    - MAP_SHADOW_VIEW_TOP_MARGIN < draw_y
                && draw_y < data_fdps_battle_view_window_origin_y
                    + MAP_SHADOW_VIEW_HEIGHT
                && portrait_id != NO_SHADOW_PORTRAIT_ID
                && portrait_id != TRANSLUCENT_PORTRAIT_ID_A
                && portrait_id != TRANSLUCENT_PORTRAIT_ID_B
                && (portrait_id < TRANSLUCENT_PORTRAIT_ID_LOW
                    || portrait_id > TRANSLUCENT_PORTRAIT_ID_HIGH)
                && (portrait_id < NO_SHADOW_ENEMY_PORTRAIT_ID_LOW
                    || portrait_id > NO_SHADOW_ENEMY_PORTRAIT_ID_HIGH)) {
                blend_desc[BLEND_DESC_LEVEL] = SHADOW_BLEND_LEVEL;
                shadow_frame = walk_frame;
                if ((status_flags & UNIT_FLAG_ACTED) != 0) {
                    shadow_frame = UNIT_ACTED_SPRITE_FRAME;
                }
                sprite_stream = data_fdps_shadow_sprite_sheet_ptr
                    + *(int *) (data_fdps_shadow_sprite_sheet_ptr
                                + shadow_frame * CEL_SUB_IMAGE_ENTRY_BYTES
                                + CEL_SUB_IMAGE_TABLE_OFFSET);
                fdps_blit_dispatch(sprite_stream,
                                   dest_pixel + MAP_SHADOW_DEST_ROWS
                                       * SCENE_BUF_PITCH,
                                   SCENE_TILE_SIZE, SCENE_TILE_SIZE,
                                   SCENE_BUF_PITCH, (unsigned int) blend_desc,
                                   BLIT_MODE_TRANSLUCENT);
            }
        } else if (data_fdps_battle_view_window_origin_y - SCENE_TILE_SIZE
                       < draw_y
                   && draw_y < data_fdps_battle_view_window_origin_y
                       + MAP_VIEW_HEIGHT) {
            sprite_index = cache_slot * MAP_UNIT_SPRITES_PER_CACHE_SLOT
                           + facing * MAP_UNIT_WALK_FRAMES_PER_FACING;

            if ((status_flags & UNIT_FLAG_ACTED) == 0) {
                sprite_stream = data_fdps_cel_sprite_cache_ptr
                    + *(int *) (data_fdps_cel_sprite_cache_ptr
                                + (sprite_index + walk_frame)
                                  * CEL_SUB_IMAGE_ENTRY_BYTES);
                if (portrait_id == TRANSLUCENT_PORTRAIT_ID_A
                    || portrait_id == TRANSLUCENT_PORTRAIT_ID_B
                    || (portrait_id >= TRANSLUCENT_PORTRAIT_ID_LOW
                        && portrait_id <= TRANSLUCENT_PORTRAIT_ID_HIGH)) {
                    blend_desc[BLEND_DESC_LEVEL] =
                        TRANSLUCENT_UNIT_BLEND_LEVEL;
                    fdps_blit_dispatch(sprite_stream, dest_pixel,
                                       SCENE_TILE_SIZE, SCENE_TILE_SIZE,
                                       SCENE_BUF_PITCH,
                                       (unsigned int) blend_desc,
                                       BLIT_MODE_TRANSLUCENT);
                } else {
                    fdps_blit_dispatch(sprite_stream, dest_pixel,
                                       SCENE_TILE_SIZE, SCENE_TILE_SIZE,
                                       SCENE_BUF_PITCH, 0, BLIT_MODE_OPAQUE);
                }
            } else {
                blend_desc[BLEND_DESC_LEVEL] = ACTED_UNIT_BLEND_LEVEL;
                blend_desc[BLEND_DESC_TINT_COLOR] = 0;
                sprite_stream = data_fdps_cel_sprite_cache_ptr
                    + *(int *) (data_fdps_cel_sprite_cache_ptr
                                + (sprite_index + UNIT_ACTED_SPRITE_FRAME)
                                  * CEL_SUB_IMAGE_ENTRY_BYTES);
                fdps_blit_dispatch(sprite_stream, dest_pixel, SCENE_TILE_SIZE,
                                   SCENE_TILE_SIZE, SCENE_BUF_PITCH,
                                   (unsigned int) blend_desc,
                                   BLIT_MODE_TINT_SPRITE);
            }

            status_icon = fdps_unit_select_status_icon(
                unit_index, data_fdps_map_unit_status_icon_cycle);
            if (status_icon != STATUS_ICON_NONE) {
                sprite_stream = data_fdps_unit_status_icon_sheet_ptr
                    + *(int *) (data_fdps_unit_status_icon_sheet_ptr
                                + status_icon * CEL_SUB_IMAGE_ENTRY_BYTES
                                + CEL_SUB_IMAGE_TABLE_OFFSET);
                fdps_blit_dispatch(sprite_stream,
                                   dest_pixel + STATUS_ICON_DEST_ROWS
                                       * SCENE_BUF_PITCH,
                                   STATUS_ICON_WIDTH, STATUS_ICON_ROWS,
                                   SCENE_BUF_PITCH, 0, BLIT_MODE_OPAQUE);
            }
        }
    }
}
