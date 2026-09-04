/* tests/cmbblow.c -- cover for src/cmbblow.c.
 *
 * Every expected value below is read off the assembly of
 * fdps_combat_play_blow at 000196d0 -- the two REP MOVSD recoil templates at
 * 000196e9 and 000196fb, MOV dword ptr [EBP-0x50],0x1 at 000196fd for the
 * blow count, PUSH 0x16480 at 00019712 with the pitch and rows stored at
 * 00019725 and 0001972f, the CMP byte ptr [EAX+0x5],0x0 impact-marker count at
 * 0001978b with the CMP [EBP-0x4c],0x0 / MOV 1 forcing at 00019799, the two
 * CALL 0x00042cf8 rolls at 00019802 and 0001982e either side of CMP
 * [EBP-0x14],0x2, the DEC/CMP -1 loop head at 0001984d, the zero-extended HP
 * word load at 00019863, MOV AL,byte ptr [EDX+0x4] at 000198b2 for the lead-in,
 * the IMUL/IDIV drain at 00019a56 with the JGE floor at 00019a74 and the MOV
 * word ptr [EDX+0x40],AX store at 00019a83, the CMP byte ptr [EAX+0x6],0x0
 * pairs at 000199d5 and 00019b2a that sign the slide and the recoil, the
 * 0x28000/3 blend at 00019b55, the six PUSH 0x3c8/0x3c9 flashes at 00019cdd,
 * CMP [EBP-0x54],0x0 at 00019d82 that cancels the second blow, the CMP
 * [EBP-0x34],0xc settle loop at 00019e04, and MOV EAX,[EBP-0x54] at 00019f6e
 * for the answer -- together with the record layouts ticket 17 settled.  None
 * of them is read off the emitted C.
 *
 * HOW THE FUNCTION IS RUN.  It draws to an offscreen page it allocates and
 * frees itself, so the page cannot be inspected; what a case can see is the
 * defender's record, the answer, the two playback cursors the caller owns, the
 * VGA aperture and DAC entry 0.  Each run therefore puts the adapter into
 * mode 13h the way the game does, hooks IRQ0 so the tick waits end, captures
 * the aperture and the DAC entry, and returns to text mode.
 *
 * ONLY THE TWELFTH SETTLE FRAME IS ON THE SCREEN when a run ends, because
 * every frame memsets the page before drawing and blits the whole 320x200
 * window afterwards.  That last frame is what the screen assertions read: the
 * current backdrop, the defender's stand sprite and gauges, and -- only when
 * the attack did not travel -- the attacker's as well.  Where the sprites sat
 * on any earlier frame, and with them the recoil ramps and the blend the
 * struck sprite is drawn through, have no unit observable at all and are
 * playtest contracts.
 *
 * WHAT THE CURSORS COUNT.  Both stand clips are staged 64 frames deep at one
 * tick each, so neither cursor wraps, and a cursor's frame index is then
 * exactly how many times the run advanced it.  The defender's is advanced once
 * per frame of the blow, eight times by each backdrop slide it rides, and once
 * per settle frame; the attacker's only in the settle frames of a blow that
 * did not travel.  That is how the number of blows and the number of frames
 * are pinned.
 *
 * HOW THE RANDOMNESS IS TAKEN OUT.  Two rolls of rand() % 100 against 3 decide
 * the double strike before any blow is played, so every run seeds the generator
 * with a seed searched for at run time: one whose first two draws both miss the
 * 3 gives a single blow, and one where a chosen draw hits it gives two.  Inside
 * fdps_combat_compute_hit_outcome the fixture drives accuracy 200 against
 * evasion 0, so the blow always lands, and a stat gap of 9 gives a damage of
 * exactly 8 whose ninth is zero, so no random bonus is taken -- which is what
 * makes the whole run consume exactly two draws per blow on top of the two
 * rolls, and that count is itself asserted.
 *
 * WHAT IS STAGED.  Everything the function and its callees reach through a
 * global: the unit array, the item, class and enemy tables, the four scene
 * layers fdps_map_load_tile_info reads with their two modifier tables, the two
 * gauge sheets the panels are painted from, and an empty BaseWav container so
 * that the Miss.wav lookup of a missed blow finds nothing and plays nothing.
 * Nothing below asserts what any of those globals holds on its own -- ticket 23
 * owns that.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "cmbblow.h"

/* The strides the four accessors in src/table.c and src/unit.c multiply by. */
#define UNIT_RECORD_STRIDE 0x50
#define ITEM_RECORD_STRIDE 0x17
#define CLASS_RECORD_STRIDE 0x0a
#define ENEMY_RECORD_STRIDE 0x0a

#define STAGE_UNITS 4
#define ATTACKER 0
#define DEFENDER 1
#define STAGE_ITEMS 16
#define STAGE_CLASSES 8
#define STAGE_ENEMIES 4

/* The scene layers, in the shape src/maptile.c reads them. */
#define TERRAIN_CELLS_AT 0x0b
#define ATTR_ROWS_AT 0x11
#define EVENT_CELLS_AT 0x10
#define MAP_WIDTH 4
#define MAP_CELLS 16
#define ATTR_ROWS 16

/* AND AL,0x40 in fdps_unit_find_equipped_slot, and item type 1 is inside the
   weapon span the search accepts. */
#define ENTRY_EQUIPPED 0x40
#define WEAPON_ITEM_TYPE 1
#define FIXTURE_WEAPON_ID 1

/* hit_effect 2 is the double strike this function answers itself. */
#define EFFECT_NONE 0
#define EFFECT_DOUBLE_STRIKE 2

/* The page and the window, restated from the pushes rather than taken from a
   header: a 368 x 248 page whose pixel (24,24) is byte 0x2298, and a 320 x 200
   window taken from there.  So page pixel (24,24) is screen pixel (0,0) and a
   sprite drawn at the page's margin lands on the screen's corner. */
#define BLOW_PAGE_MARGIN 0x18
#define SCREEN_W 0x140
#define SCREEN_H 0xc8
#define SCREEN_BYTES (SCREEN_W * SCREEN_H)
#define VGA_BASE 0x000a0000

#define MODE_TEXT 0x03
#define MODE_320X200X256 0x13

/* DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the routine spins on the counter. */
#define TIMER_VECTOR 8

/* The DAC ports the critical flash drives, plus the read index the capture
   uses. */
#define DAC_WRITE_INDEX 0x3c8
#define DAC_READ_INDEX 0x3c7
#define DAC_DATA 0x3c9

/* What DAC entry 0 is set to before every run.  A run that flashed writes the
   entry back as black, so a case reads the flash off whether this survives. */
#define DAC_SENTINEL_R 0x20
#define DAC_SENTINEL_G 0x21
#define DAC_SENTINEL_B 0x22

/* What the aperture is filled with before each run, so a byte still holding it
   is a byte no frame ever blitted over. */
#define SCREEN_SENTINEL 0x5a

/* The synthetic .SAF, header offsets first (resource_info/saf.md). */
#define SAF_CELL_W_AT 0x07
#define SAF_CELL_H_AT 0x09
#define SAF_FRAME_COUNT_AT 0x0c
#define SAF_FRAME_TABLE_PTR_AT 0x0e
#define SAF_TILEMAP_COUNT_AT 0x16
#define SAF_TILEMAP_TABLE_PTR_AT 0x18
#define SAF_TILE_COUNT_AT 0x20
#define SAF_TILE_TABLE_PTR_AT 0x22

/* Where the fixture puts each section.  The frame table has room for the 64
   entries a stand clip needs and the frame records start well clear of it. */
#define SAF_TILE_TABLE_AT 0x34
#define SAF_TILE_STREAM_AT 0x38
#define SAF_TILEMAP_TABLE_AT 0x3c
#define SAF_TILEMAP_AT 0x40
#define SAF_FRAME_TABLE_AT 0x48
#define SAF_FRAME_REC_AT 0x200
#define SAF_FRAME_REC_STRIDE 0x20
#define SAF_MAX_RECORDS 8
#define SAF_IMAGE_BYTES (SAF_FRAME_REC_AT \
                         + SAF_MAX_RECORDS * SAF_FRAME_REC_STRIDE)

/* A frame record: sound at +0, duration at +2, the travelling lead-in count at
   +4, the impact marker at +5, the layer count at +8 and the single 13-byte
   layer at +0x0a. */
#define SAF_FRAME_SOUND_AT 0x00
#define SAF_FRAME_DURATION_AT 0x02
#define SAF_FRAME_LEAD_IN_AT 0x04
#define SAF_FRAME_IMPACT_AT 0x05
#define SAF_FRAME_LAYERS_AT 0x08
#define SAF_LAYER_AT 0x0a
#define SAF_LAYER_TILEMAP_AT 0x00
#define SAF_LAYER_X_AT 0x02
#define SAF_LAYER_Y_AT 0x04
#define SAF_LAYER_BLEND_AT 0x06

/* A cell small enough that four of them fit across the screen's first rows
   without meeting, and two rows tall so a draw one row out is visible. */
#define SAF_CELL_W 4
#define SAF_CELL_H 2

/* Deep enough that neither stand cursor wraps in any run below. */
#define STAND_FRAMES 64
/* Every frame is held for one tick, which is what makes the tick counter and
   the frame counter the same thing. */
#define FRAME_DURATION 1

/* Where each image's single layer sits relative to the request, and so which
   screen column its mark lands in.  Four disjoint 4-pixel runs. */
#define LAYER_X_BACKDROP 0
#define LAYER_X_DEFENDER 8
#define LAYER_X_ATTACKER 16
#define LAYER_X_ACT 24

/* One pixel value per image, none of them zero and none of them in the gauge
   sheet's 0xa0..0xa3, so a painted byte names the draw that wrote it. */
#define PIXEL_FROM_BACKDROP 0x11
#define PIXEL_TO_BACKDROP 0x22
#define PIXEL_DEFENDER 0x33
#define PIXEL_ATTACKER 0x44
#define PIXEL_ACT 0x55

/* The gauge panels, restated from src/gauge.c's assembly: a side of 0 puts the
   panel at page row 0xc7 column 0x1e and paints its HP frame from fill strip
   2, and any other side at page row 0x20 column 0xc3 from strip 0.  Both are
   turned into screen pixels by taking the page margin off. */
#define PANEL_LOW_LEFT_ROW (0xc7 - BLOW_PAGE_MARGIN)
#define PANEL_LOW_LEFT_COLUMN (0x1e - BLOW_PAGE_MARGIN)
#define PANEL_UP_RIGHT_ROW (0x20 - BLOW_PAGE_MARGIN)
#define PANEL_UP_RIGHT_COLUMN (0xc3 - BLOW_PAGE_MARGIN)

/* The gauge .CEL: four one-pixel sprites, sprite N painting the byte
   0xa0 + N, so which strip reached the screen is readable off it. */
#define CEL_SPRITES 4
#define CEL_TABLE_START 15
#define CEL_STREAM_START (CEL_TABLE_START + (CEL_SPRITES + 1) * 4)
#define CEL_STREAM_BYTES 2
#define CEL_BYTES (CEL_STREAM_START + CEL_SPRITES * CEL_STREAM_BYTES)
#define CEL_PIXEL_BASE 0xa0

/* The combat gauge fill sheet: four strips of 0x271 bytes (src/gauge.c). */
#define FILL_SHEET_BYTES 0x9c4

/* An empty container: three magic bytes, a version word, the entry table
   offset and a zero entry count at +7, so every lookup walks no entries and
   answers NULL. */
#define VFS_HEADER_BYTES 16

/* The act clip never needs more frames than this in any case below. */
#define ACT_MAX_FRAMES 4

/* The fixture's arithmetic: AP 100 against DP 91 is a gap of 9, so the damage
   is 9 * 9 / 10 = 8, and 8 / 9 is 0 so no random bonus is drawn.  Accuracy 200
   against evasion 0 lands on every draw. */
#define FIXTURE_DAMAGE 8
#define FIXTURE_ACCURACY 200
#define FIXTURE_ATTACK 100
#define FIXTURE_DEFENSE 91
/* Twice the defense, so that a critical's halving brings it back to the gap
   above and the damage stays free of the random bonus. */
#define FIXTURE_CRIT_DEFENSE 182

/* Two draws per blow inside fdps_combat_compute_hit_outcome -- the accuracy
   roll and the critical roll -- on top of the two double-strike rolls. */
#define ROLLS_PER_BLOW 2
#define DOUBLE_STRIKE_ROLLS 2

/* Long enough to hold every draw any run below can consume and then some. */
#define RAND_SEQUENCE 24

static unsigned char unit_block[STAGE_UNITS * UNIT_RECORD_STRIDE];
static unsigned char item_block[STAGE_ITEMS * ITEM_RECORD_STRIDE];
static unsigned char class_block[STAGE_CLASSES * CLASS_RECORD_STRIDE];
static unsigned char enemy_block[STAGE_ENEMIES * ENEMY_RECORD_STRIDE];

static unsigned char stage_tile_map[TERRAIN_CELLS_AT + MAP_CELLS * 2];
static unsigned char stage_attr[ATTR_ROWS_AT + ATTR_ROWS * 4];
static unsigned char stage_grid[4 + MAP_CELLS * 2];
static unsigned char stage_event[EVENT_CELLS_AT + MAP_CELLS];

static unsigned char gauge_cel[CEL_BYTES];
static unsigned char gauge_fill[FILL_SHEET_BYTES];
static unsigned char empty_wav_bank[VFS_HEADER_BYTES];

static unsigned char act_image[SAF_IMAGE_BYTES];
static unsigned char from_image[SAF_IMAGE_BYTES];
static unsigned char to_image[SAF_IMAGE_BYTES];
static unsigned char attacker_image[SAF_IMAGE_BYTES];
static unsigned char defender_image[SAF_IMAGE_BYTES];

static int attacker_cursor[3];
static int defender_cursor[3];

static unsigned char screen_copy[SCREEN_BYTES];
static int dac_entry_zero[3];
static int blow_answer;
static int blow_rand_after;
static int rand_sequence[RAND_SEQUENCE];

/* The seed the next run is started from, and the shape of the act clip it
   plays. */
static unsigned int run_seed;
static int act_frames;
static int act_lead_in;
static int act_impact[ACT_MAX_FRAMES];

static void (__interrupt __far *saved_timer)();

static void __interrupt __far blow_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(saved_timer);
}

static struct fdps_unit_record *unit(int unit_index)
{
    return (struct fdps_unit_record *)
        (unit_block + unit_index * UNIT_RECORD_STRIDE);
}

static struct fdps_item_effect *item(int item_id)
{
    return (struct fdps_item_effect *)
        (item_block + item_id * ITEM_RECORD_STRIDE);
}

static void put_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void put_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* One frame record: a sound id of -1 so nothing is ever played, one tick of
   dwell, the two marker bytes the case asked for, and one layer of the single
   tilemap at layer_x. */
static void stage_frame_record(unsigned char *image, int record, int lead_in,
                               int impact, int layer_x)
{
    int at;

    at = SAF_FRAME_REC_AT + record * SAF_FRAME_REC_STRIDE;
    put_u16(image, at + SAF_FRAME_SOUND_AT, 0xffff);
    put_u16(image, at + SAF_FRAME_DURATION_AT, FRAME_DURATION);
    image[at + SAF_FRAME_LEAD_IN_AT] = (unsigned char) lead_in;
    image[at + SAF_FRAME_IMPACT_AT] = (unsigned char) impact;
    put_u16(image, at + SAF_FRAME_LAYERS_AT, 1);
    put_u16(image, at + SAF_LAYER_AT + SAF_LAYER_TILEMAP_AT, 0);
    put_u16(image, at + SAF_LAYER_AT + SAF_LAYER_X_AT, (unsigned int) layer_x);
    put_u16(image, at + SAF_LAYER_AT + SAF_LAYER_Y_AT, 0);
    image[at + SAF_LAYER_AT + SAF_LAYER_BLEND_AT] = 0;
}

/* The header, the single 4x2 tile of `pixel` and the single cell tilemap that
   holds it.  Command 0x03 is a fill run of four pixels (resource_info/cel.md),
   so two of them make the cell's two rows. */
static void stage_image_head(unsigned char *image, unsigned char pixel)
{
    memset(image, 0, (size_t) SAF_IMAGE_BYTES);
    image[0] = 'S';
    image[1] = 'A';
    image[2] = 'F';
    put_u16(image, SAF_CELL_W_AT, SAF_CELL_W);
    put_u16(image, SAF_CELL_H_AT, SAF_CELL_H);

    put_u16(image, SAF_TILE_COUNT_AT, 1);
    put_u32(image, SAF_TILE_TABLE_PTR_AT, (unsigned long) SAF_TILE_TABLE_AT);
    put_u32(image, SAF_TILE_TABLE_AT, (unsigned long) SAF_TILE_STREAM_AT);
    image[SAF_TILE_STREAM_AT] = 0x03;
    image[SAF_TILE_STREAM_AT + 1] = pixel;
    image[SAF_TILE_STREAM_AT + 2] = 0x03;
    image[SAF_TILE_STREAM_AT + 3] = pixel;

    put_u16(image, SAF_TILEMAP_COUNT_AT, 1);
    put_u32(image, SAF_TILEMAP_TABLE_PTR_AT,
            (unsigned long) SAF_TILEMAP_TABLE_AT);
    put_u32(image, SAF_TILEMAP_TABLE_AT, (unsigned long) SAF_TILEMAP_AT);
    put_u16(image, SAF_TILEMAP_AT, 1);
    put_u16(image, SAF_TILEMAP_AT + 2, 1);
    put_u16(image, SAF_TILEMAP_AT + 4, 0);

    put_u32(image, SAF_FRAME_TABLE_PTR_AT, (unsigned long) SAF_FRAME_TABLE_AT);
}

/* A clip whose every frame-table entry names the one record: a backdrop or a
   stand clip, which carries neither marker byte. */
static void stage_plain_clip(unsigned char *image, unsigned char pixel,
                             int frames, int layer_x)
{
    int index;

    stage_image_head(image, pixel);
    put_u16(image, SAF_FRAME_COUNT_AT, (unsigned int) frames);
    for (index = 0; index < frames; index++) {
        put_u32(image, SAF_FRAME_TABLE_AT + index * 4,
                (unsigned long) SAF_FRAME_REC_AT);
    }
    stage_frame_record(image, 0, 0, 0, layer_x);
}

/* The attack clip, whose frames need records of their own: frame 0 carries the
   lead-in count and each frame its own impact marker. */
static void stage_act_clip(unsigned char *image, unsigned char pixel,
                           int frames, int lead_in, int *impact, int layer_x)
{
    int index;

    stage_image_head(image, pixel);
    put_u16(image, SAF_FRAME_COUNT_AT, (unsigned int) frames);
    for (index = 0; index < frames; index++) {
        put_u32(image, SAF_FRAME_TABLE_AT + index * 4,
                (unsigned long) (SAF_FRAME_REC_AT
                                 + index * SAF_FRAME_REC_STRIDE));
        stage_frame_record(image, index, index == 0 ? lead_in : 0,
                           impact[index], layer_x);
    }
}

static void stage_gauge_art(void)
{
    int sprite;
    int offset;

    for (offset = 0; offset < FILL_SHEET_BYTES; offset++) {
        gauge_fill[offset] = (unsigned char) (offset % 251 + 1);
    }
    data_fdps_gauge_fill_sheet_ptr = gauge_fill;

    memset(gauge_cel, 0, (size_t) CEL_BYTES);
    gauge_cel[0] = 'C';
    gauge_cel[1] = 'E';
    gauge_cel[2] = 'L';
    *(short *) (gauge_cel + 7) = 1;
    *(short *) (gauge_cel + 9) = 1;
    *(short *) (gauge_cel + 11) = CEL_SPRITES;
    for (sprite = 0; sprite < CEL_SPRITES; sprite++) {
        offset = CEL_STREAM_START + sprite * CEL_STREAM_BYTES;
        *(int *) (gauge_cel + CEL_TABLE_START + sprite * 4) = offset;
        gauge_cel[offset] = 0x00;
        gauge_cel[offset + 1] = (unsigned char) (CEL_PIXEL_BASE + sprite);
    }
    *(int *) (gauge_cel + CEL_TABLE_START + CEL_SPRITES * 4) = CEL_BYTES;
    data_fdps_combat_gauge_sprite_sheet_ptr = gauge_cel;
}

/* The standard duel, published through every global the function and its
   callees read.  The attacker is a side-1 unit with a plain equipped weapon,
   which keeps the experience path -- side 2 against side 0 -- out of the run
   entirely; the defender is a side-0 unit with 20 of 40 HP, which puts his
   panel in the lower left and the attacker's in the upper right. */
static void stage(void)
{
    int index;

    memset(unit_block, 0, sizeof(unit_block));
    memset(item_block, 0, sizeof(item_block));
    memset(class_block, 0, sizeof(class_block));
    memset(enemy_block, 0, sizeof(enemy_block));

    for (index = 0; index < TERRAIN_CELLS_AT; index++) {
        stage_tile_map[index] = 0;
    }
    *(short *) (stage_tile_map + 7) = (short) MAP_WIDTH;
    for (index = 0; index < MAP_CELLS; index++) {
        *(short *) (stage_tile_map + TERRAIN_CELLS_AT + index * 2) = 0;
    }
    memset(stage_attr, 0, sizeof(stage_attr));
    memset(stage_grid, 0, sizeof(stage_grid));
    *(short *) stage_grid = (short) MAP_WIDTH;
    memset(stage_event, 0, sizeof(stage_event));
    *(short *) (stage_event + 7) = (short) MAP_WIDTH;

    data_fdps_map_unit_array_ptr = unit_block;
    data_fdps_item_effect_table_ptr = item_block;
    data_fdps_class_table_ptr = class_block;
    data_fdps_battle_enemy_data_table_ptr = enemy_block;
    data_fdps_scene_layer_tile_map_ptrs[0] = stage_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = stage_attr;
    data_fdps_battle_move_grid_ptr = stage_grid;
    data_fdps_map_cell_event_code_layer_ptr = stage_event;
    for (index = 0; index < 6; index++) {
        data_fdps_battle_tile_attr_ap_modifier_table[index] = 0;
        data_fdps_battle_tile_attr_def_modifier_table[index] = 0;
    }

    memset(empty_wav_bank, 0, sizeof(empty_wav_bank));
    data_fdps_audio_basewav_sfx_bank_buf_ptr = empty_wav_bank;

    stage_gauge_art();

    unit(ATTACKER)->pos_x = 0;
    unit(ATTACKER)->pos_y = 0;
    unit(ATTACKER)->side = 1;
    unit(ATTACKER)->portrait_id = 5;
    unit(ATTACKER)->clazz = 0;
    unit(ATTACKER)->level = 5;
    unit(ATTACKER)->ap = FIXTURE_ATTACK;
    unit(ATTACKER)->hit = FIXTURE_ACCURACY;
    unit(ATTACKER)->hp_current = 30;
    unit(ATTACKER)->hp_max = 30;
    unit(ATTACKER)->inventory_slots[0] = ENTRY_EQUIPPED;
    unit(ATTACKER)->inventory_slots[1] = FIXTURE_WEAPON_ID;

    unit(DEFENDER)->pos_x = 1;
    unit(DEFENDER)->pos_y = 0;
    unit(DEFENDER)->side = 0;
    unit(DEFENDER)->portrait_id = 5;
    unit(DEFENDER)->clazz = 0;
    unit(DEFENDER)->level = 10;
    unit(DEFENDER)->dp = FIXTURE_DEFENSE;
    unit(DEFENDER)->ev = 0;
    unit(DEFENDER)->hp_current = 20;
    unit(DEFENDER)->hp_max = 40;

    item(FIXTURE_WEAPON_ID)->type = WEAPON_ITEM_TYPE;
    item(FIXTURE_WEAPON_ID)->hit_effect = EFFECT_NONE;
    item(FIXTURE_WEAPON_ID)->hit_effect_rate = 0;

    data_fdps_battle_pending_xp_credit = 0;

    /* One frame of the act clip, no lead-in, the impact on the middle frame.
       Cases that want another shape overwrite these three. */
    run_seed = 0;
    act_frames = 3;
    act_lead_in = 0;
    act_impact[0] = 0;
    act_impact[1] = 1;
    act_impact[2] = 0;
    act_impact[3] = 0;
}

/* The first N draws of a seed, so a case can say which of them a run took. */
static void capture_rand_sequence(unsigned int seed)
{
    int index;

    srand(seed);
    for (index = 0; index < RAND_SEQUENCE; index++) {
        rand_sequence[index] = rand();
    }
}

static int rand_position(int value)
{
    int index;

    for (index = 0; index < RAND_SEQUENCE; index++) {
        if (rand_sequence[index] == value) {
            return index;
        }
    }
    return -1;
}

/* A seed whose first two draws land the double-strike roll exactly where the
   case wants them.  Searched for rather than written down, because which seeds
   do it is a property of the library's generator and not of this function. */
static unsigned int seed_where(int first_promotes, int second_promotes)
{
    unsigned int seed;
    int first;
    int second;

    for (seed = 1u; seed < 30000u; seed++) {
        srand(seed);
        first = rand() % 100 < 3;
        second = rand() % 100 < 3;
        if (first == first_promotes && second == second_promotes) {
            return seed;
        }
    }
    return 0;
}

static void set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* One whole blow, with the adapter in the mode the game plays it in and a timer
   interrupt running, leaving the answer in blow_answer, the visible window in
   screen_copy[], DAC entry 0 in dac_entry_zero[] and the first draw the run did
   not take in blow_rand_after. */
static void blow_run(void)
{
    stage_act_clip(act_image, PIXEL_ACT, act_frames, act_lead_in, act_impact,
                   LAYER_X_ACT);
    stage_plain_clip(from_image, PIXEL_FROM_BACKDROP, 1, LAYER_X_BACKDROP);
    stage_plain_clip(to_image, PIXEL_TO_BACKDROP, 1, LAYER_X_BACKDROP);
    stage_plain_clip(attacker_image, PIXEL_ATTACKER, STAND_FRAMES,
                     LAYER_X_ATTACKER);
    stage_plain_clip(defender_image, PIXEL_DEFENDER, STAND_FRAMES,
                     LAYER_X_DEFENDER);

    attacker_cursor[0] = 0;
    attacker_cursor[1] = 0;
    attacker_cursor[2] = (int) attacker_image;
    defender_cursor[0] = 0;
    defender_cursor[1] = 0;
    defender_cursor[2] = (int) defender_image;

    set_mode(MODE_320X200X256);
    memset((void *) VGA_BASE, SCREEN_SENTINEL, (size_t) SCREEN_BYTES);
    outp(DAC_WRITE_INDEX, 0);
    outp(DAC_DATA, DAC_SENTINEL_R);
    outp(DAC_DATA, DAC_SENTINEL_G);
    outp(DAC_DATA, DAC_SENTINEL_B);

    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, blow_timer_isr);
    srand(run_seed);
    blow_answer = fdps_combat_play_blow(ATTACKER, DEFENDER, act_image,
                                        attacker_cursor, defender_cursor,
                                        from_image, to_image);
    blow_rand_after = rand();
    _dos_setvect(TIMER_VECTOR, saved_timer);

    outp(DAC_READ_INDEX, 0);
    dac_entry_zero[0] = inp(DAC_DATA);
    dac_entry_zero[1] = inp(DAC_DATA);
    dac_entry_zero[2] = inp(DAC_DATA);

    memmove(screen_copy, (void *) VGA_BASE, (size_t) SCREEN_BYTES);
    set_mode(MODE_TEXT);
}

static int screen_pixel(int row, int column)
{
    return (int) screen_copy[row * SCREEN_W + column];
}

/* The top-left pixel of the mark an image's single layer leaves, which is the
   screen column its layer offset put it in. */
static int mark(int layer_x)
{
    return screen_pixel(0, layer_x);
}

static int defender_hp(void)
{
    return (int) unit(DEFENDER)->hp_current;
}

/* Every offset the cases below reach through.  If either record were shaped
   differently, all of them would be reading other bytes. */
static void the_record_offsets_this_function_reads(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) sizeof(struct fdps_unit_record), UNIT_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, hit_effect), 9);
}

/* One landed blow of the standard duel: the clip's single impact frame pays
   the whole of the damage, 20 - 1 * 8 / 1 = 12, the record takes it and the
   answer is what the record holds. */
static void a_landed_blow_drains_the_damage_and_returns_the_hp(void)
{
    stage();
    run_seed = seed_where(0, 0);
    blow_run();

    CHECK_EQ(blow_answer, 20 - FIXTURE_DAMAGE);
    CHECK_EQ(defender_hp(), 20 - FIXTURE_DAMAGE);
}

/* Three frames of clip and twelve settle frames advance the defender's cursor
   fifteen times; the attacker's is advanced only by the settle frames, so it
   stands at twelve.  Nothing else in this run touches either. */
static void the_cursors_count_the_frames_each_unit_was_drawn_on(void)
{
    stage();
    run_seed = seed_where(0, 0);
    blow_run();

    CHECK_EQ(defender_cursor[0], 3 + 12);
    CHECK_EQ(attacker_cursor[0], 12);
}

/* What the twelfth settle frame put on the screen: the backdrop the run ended
   on, both stand sprites, both panels -- and no trace of the attack clip, which
   the settle frames do not draw at all. */
static void the_settle_frames_show_both_units_over_the_backdrop(void)
{
    stage();
    run_seed = seed_where(0, 0);
    blow_run();

    CHECK_EQ(mark(LAYER_X_BACKDROP), PIXEL_FROM_BACKDROP);
    CHECK_EQ(mark(LAYER_X_DEFENDER), PIXEL_DEFENDER);
    CHECK_EQ(mark(LAYER_X_ATTACKER), PIXEL_ATTACKER);
    CHECK_EQ(mark(LAYER_X_ACT), 0);
    CHECK_EQ(screen_pixel(PANEL_LOW_LEFT_ROW, PANEL_LOW_LEFT_COLUMN),
             CEL_PIXEL_BASE + 2);
    CHECK_EQ(screen_pixel(PANEL_UP_RIGHT_ROW, PANEL_UP_RIGHT_COLUMN),
             CEL_PIXEL_BASE);
}

/* A missed blow still walks the impact frame and still writes the record, but
   the damage it pays out is zero, so the figure that comes back is the one the
   defender started with. */
static void a_missed_blow_writes_the_hp_back_unchanged(void)
{
    stage();
    unit(ATTACKER)->hit = 0;
    run_seed = seed_where(0, 0);
    blow_run();

    CHECK_EQ(blow_answer, 20);
    CHECK_EQ(defender_hp(), 20);
}

/* A clip with no impact marker anywhere pays nothing at all: the count is
   forced to 1 so the divide is safe, but the arm that writes the record is
   never entered, so the defender comes out of the animation untouched.  What
   the call returns in that case is whatever the uninitialised slot held and is
   deliberately not asserted. */
static void a_clip_with_no_impact_frame_leaves_the_record_alone(void)
{
    stage();
    act_impact[1] = 0;
    run_seed = seed_where(0, 0);
    blow_run();

    CHECK_EQ(defender_hp(), 20);
}

/* The weapon's own double-strike attribute: two blows, each re-reading the HP
   the previous one left, so 40 loses 8 and then 8 again.  Two clips' worth of
   frames plus the settle frames advance the defender eighteen times. */
static void a_double_strike_weapon_strikes_twice(void)
{
    stage();
    item(FIXTURE_WEAPON_ID)->hit_effect = EFFECT_DOUBLE_STRIKE;
    unit(DEFENDER)->hp_current = 40;
    run_seed = seed_where(0, 0);
    blow_run();

    CHECK_EQ(blow_answer, 40 - FIXTURE_DAMAGE * 2);
    CHECK_EQ(defender_hp(), 40 - FIXTURE_DAMAGE * 2);
    CHECK_EQ(defender_cursor[0], 3 + 3 + 12);
}

/* A defender the first blow left at zero cancels the second, so a double-strike
   weapon plays one clip and not two.  The drain floors at zero rather than
   going negative. */
static void a_killed_defender_cancels_the_second_blow(void)
{
    stage();
    item(FIXTURE_WEAPON_ID)->hit_effect = EFFECT_DOUBLE_STRIKE;
    unit(DEFENDER)->hp_current = 5;
    run_seed = seed_where(0, 0);
    blow_run();

    CHECK_EQ(blow_answer, 0);
    CHECK_EQ(defender_hp(), 0);
    CHECK_EQ(defender_cursor[0], 3 + 12);
}

/* Either roll on its own promotes the blow to two, which is what says both are
   consulted and neither is the other's fall-through. */
static void either_double_strike_roll_promotes_the_blow(void)
{
    stage();
    unit(DEFENDER)->hp_current = 40;
    run_seed = seed_where(1, 0);
    blow_run();
    CHECK_EQ(blow_answer, 40 - FIXTURE_DAMAGE * 2);

    stage();
    unit(DEFENDER)->hp_current = 40;
    run_seed = seed_where(0, 1);
    blow_run();
    CHECK_EQ(blow_answer, 40 - FIXTURE_DAMAGE * 2);
}

/* And both are TAKEN even when the first has already settled the answer.  The
   seed here promotes on the first roll, so a body that stopped asking once it
   knew would leave the generator one draw earlier: two rolls plus two blows of
   two draws each is six, and the seventh draw is the one still waiting when the
   call returns. */
static void both_double_strike_rolls_are_drawn_whatever_the_first_says(void)
{
    stage();
    unit(DEFENDER)->hp_current = 40;
    run_seed = seed_where(1, 0);
    capture_rand_sequence(run_seed);
    blow_run();

    CHECK_EQ(rand_position(blow_rand_after),
             DOUBLE_STRIKE_ROLLS + 2 * ROLLS_PER_BLOW);
}

/* A single blow consumes the two rolls and one blow's two draws, so the fourth
   draw is the one left over. */
static void a_single_blow_consumes_the_two_rolls_and_two_more(void)
{
    stage();
    run_seed = seed_where(0, 0);
    capture_rand_sequence(run_seed);
    blow_run();

    CHECK_EQ(rand_position(blow_rand_after),
             DOUBLE_STRIKE_ROLLS + ROLLS_PER_BLOW);
}

/* A critical blow drives DAC entry 0 to white and then writes it back as
   black, so the sentinel the fixture put there does not survive the run.  The
   defense is doubled so that halving it gives the same stat gap, and with it
   the same damage, as every other case. */
static void a_critical_blow_leaves_dac_entry_zero_black(void)
{
    stage();
    unit(DEFENDER)->dp = FIXTURE_CRIT_DEFENSE;
    class_block[CLASS_RECORD_STRIDE + offsetof(struct fdps_class_record,
                                               critical)] = 100;
    run_seed = seed_where(0, 0);
    blow_run();

    CHECK_EQ(blow_answer, 20 - FIXTURE_DAMAGE);
    CHECK_EQ(dac_entry_zero[0], 0);
    CHECK_EQ(dac_entry_zero[1], 0);
    CHECK_EQ(dac_entry_zero[2], 0);
}

/* And a blow that is not critical leaves it alone, which is what says the
   flash is under the outcome's critical flag and not under every impact
   frame. */
static void an_ordinary_blow_leaves_dac_entry_zero_where_it_was(void)
{
    stage();
    run_seed = seed_where(0, 0);
    blow_run();

    CHECK_EQ(dac_entry_zero[0], DAC_SENTINEL_R);
    CHECK_EQ(dac_entry_zero[1], DAC_SENTINEL_G);
    CHECK_EQ(dac_entry_zero[2], DAC_SENTINEL_B);
}

/* A travelling attack: frame 0 asks for two lead-in frames, so the first two
   frames play over the outgoing backdrop, the view then slides across and the
   two arguments are swapped -- and a single blow never slides back, so the
   settle frames play over the INCOMING backdrop and the attacker is neither
   drawn nor gauged in any of them.
 *
   Both of the clip's impact markers are counted, but only the one on frame 2
   is reached once the lead-in has consumed frames 0 and 1, so the defender
   pays one share of two: 20 - 1 * 8 / 2 = 16.
 *
   The defender's cursor counts eight steps of the slide, the two frames of the
   blow that remained and the twelve settle frames; the attacker's is never
   advanced at all. */
static void a_travelling_attack_leaves_the_view_on_the_far_backdrop(void)
{
    stage();
    act_frames = 4;
    act_lead_in = 2;
    act_impact[0] = 1;
    act_impact[1] = 0;
    act_impact[2] = 1;
    act_impact[3] = 0;
    run_seed = seed_where(0, 0);
    blow_run();

    CHECK_EQ(blow_answer, 20 - FIXTURE_DAMAGE / 2);
    CHECK_EQ(defender_hp(), 20 - FIXTURE_DAMAGE / 2);
    CHECK_EQ(mark(LAYER_X_BACKDROP), PIXEL_TO_BACKDROP);
    CHECK_EQ(mark(LAYER_X_DEFENDER), PIXEL_DEFENDER);
    CHECK_EQ(mark(LAYER_X_ATTACKER), 0);
    CHECK_EQ(screen_pixel(PANEL_UP_RIGHT_ROW, PANEL_UP_RIGHT_COLUMN), 0);
    CHECK_EQ(defender_cursor[0], 8 + 2 + 12);
    CHECK_EQ(attacker_cursor[0], 0);
}

void run_cmbblow_tests(void)
{
    RUN_TEST(the_record_offsets_this_function_reads);
    RUN_TEST(a_landed_blow_drains_the_damage_and_returns_the_hp);
    RUN_TEST(the_cursors_count_the_frames_each_unit_was_drawn_on);
    RUN_TEST(the_settle_frames_show_both_units_over_the_backdrop);
    RUN_TEST(a_missed_blow_writes_the_hp_back_unchanged);
    RUN_TEST(a_clip_with_no_impact_frame_leaves_the_record_alone);
    RUN_TEST(a_double_strike_weapon_strikes_twice);
    RUN_TEST(a_killed_defender_cancels_the_second_blow);
    RUN_TEST(either_double_strike_roll_promotes_the_blow);
    RUN_TEST(both_double_strike_rolls_are_drawn_whatever_the_first_says);
    RUN_TEST(a_single_blow_consumes_the_two_rolls_and_two_more);
    RUN_TEST(a_critical_blow_leaves_dac_entry_zero_black);
    RUN_TEST(an_ordinary_blow_leaves_dac_entry_zero_where_it_was);
    RUN_TEST(a_travelling_attack_leaves_the_view_on_the_far_backdrop);
}
