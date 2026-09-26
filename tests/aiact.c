/* tests/aiact.c -- cover for src/aiact.c.
 *
 * Two fixtures, one per entry point: the attack action's below, and the spell
 * cast's beside its own cases further down.  The second builds on the first --
 * it calls aa_stage and then moves the units onto the geometry a blast needs
 * and adds the MAGICDAT.DAT record the search is claimed to have chosen.
 *
 * fdps_map_actor_move_and_attack is a sequencer: every decision it makes it
 * makes by driving src/mapcur.c, src/movegrid.c, src/unit.c, src/aitarget.c,
 * src/gauge.c, src/unitatk.c, src/combat.c, src/death.c and src/unitstat.c,
 * and the only things a caller can see afterwards are the two unit records,
 * the globals it left behind and the screen.  So every case below is run end
 * to end against the real callees, and what it asserts is read back off the
 * records and the globals.
 *
 * Expected values come from the assembly at 00012e50 -- the two stores of 0
 * and the two of 1 into [0x00069cd0], the push order at 00012e72 that makes
 * [0x00063f80] the x and [0x00063f84] the y of the walk, PUSH 0x64 at
 * 00012eb8, MOV dword ptr [0x00069cec],0x0 at 00012ec2, CMP byte ptr
 * [0x00060010],0x0 / JNZ at 00012ecc, the two CMP EAX,0x1 at 00012f04 and
 * 00012f6c, the TEST EAX,EAX at 00012f46 and the TEST [EBP-0x1c],EAX at
 * 00012f84 that AND the two temporaries, ADD EAX,0x8 at 00012fb0, the argument
 * order of the two fdps_unit_attack_target calls, PUSH dword ptr [0x00063f74]
 * at 0001301c, and IMUL ...,0xf / IDIV 10 at 00013004 -- and from the
 * documented behaviour of the callees, which is where the arithmetic of a blow
 * and of an experience award comes from.  None of them is read off the emitted
 * C.
 *
 * HOW A BLOW IS MADE CERTAIN.  The duel is the one tests/unitatk.c uses: AP
 * 100 against DP 91 is a gap of 9 and so a damage of exactly 8, whose ninth is
 * 0 and which therefore takes no random bonus; accuracy 200 against evasion 0
 * lands on every draw; the class critical rate is 0 and the weapon's hit
 * effect is 0, so neither of those rolls can fire.  The only roll left is the
 * flat 3 percent that turns a swing into two blows, and the fixture seeds the
 * generator with the lowest seed whose first 24 draws ALL answer 3 or more to
 * rand() % 100 -- so no swing in a run can land a second blow, whichever draw
 * of the stream its roll happens to fall on.
 *
 * WHAT THE EXPERIENCE FIGURES ARE.  The actor is an enemy-side unit and the
 * target a player one, so the actor's own blow pays nothing at all -- the
 * credit is only written when a player unit strikes an enemy record -- and
 * every figure below is the COUNTERBLOW's: the award is the defender's level
 * times its enemy record's reward over the attacker's level, and a defender
 * that survives pays that award scaled by the fraction of its maximum HP the
 * blow took off.  With the actor at level 20, enemy record 0 rewarding 70 and
 * the actor surviving with 8 off 40, a target at level 40 earns 20 * 70 / 40 =
 * 35, scaled to 35 * 8 / 40 = 7, and a target at level 20 earns 70 scaled to
 * 14.  This function then multiplies by 15 and divides by 10: 7 becomes 10 and
 * not 10.5, and 14 becomes 21.
 *
 * WHY THE TARGET IS AT THE LEVEL CAP IN MOST CASES.  fdps_unit_award_exp_and_
 * level_up refuses a unit already at its cap and returns WITHOUT clearing the
 * accumulator, so the scaled credit is still readable after the call and no
 * frame of the floating figure is drawn.  The one case that lets the award run
 * to the end drops the target to level 20 and reads the paid figure back out
 * of the record's exp_carry byte.
 *
 * WHAT IS STAGED, AND WHAT IS DELIBERATELY EMPTY.  The battle-time environment
 * the walk and the fight need is built here: the terrain layer, the attribute
 * table, the movement grid, the cell-event layer, the PROMAP.DAT class table,
 * the unit array, the item, enemy and growth tables, the cursor kit, the unit
 * gauge sheet, the resident animation container and the master palette.
 * data_fdps_map_unit_count is left at 0 on purpose: no unit is drawn into a
 * composed frame, no death script is collected and no destruction sequence is
 * played, which keeps these cases about the sequencer rather than about the
 * three death routines -- their own cover is tests/death.c.  The walk's own
 * frames are suppressed the way tests/movegrid.c suppresses them, with
 * data_fdps_input_last_scancode parked on the skip code 3.
 *
 * THE TIMER INTERRUPT IS NOT OPTIONAL.  Every frame the cursor walk, the gauge
 * display and the attack animation present ends waiting for
 * data_fdps_timer_tick_counter to change, so each run installs a handler on
 * IRQ0 that moves it and chains to the one that was there.  Without it the
 * first presented frame never returns.
 *
 * Nothing below asserts what any staged global holds on its own; ticket 23
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
#include "audio.h"
#include "gauge.h"
#include "keybd.h"
#include "aiact.h"

/* The scene layers, in the shape src/maptile.c reads them: a terrain layer
   whose tile width is at +7 and whose 16-bit tile ids start at +0x0b, an
   attribute table whose 4-byte rows start at +0x11, a movement grid of 2-byte
   cells after a 4-byte header, and an event layer whose width is at +7 and
   whose cell bytes start at +0x10. */
#define AA_TERRAIN_CELLS_AT 0x0b
#define AA_ATTR_ROWS_AT 0x11
#define AA_EVENT_CELLS_AT 0x10

#define AA_MAP_W 5
#define AA_MAP_H 5
#define AA_CELLS (AA_MAP_W * AA_MAP_H)
#define AA_ATTR_ROWS 32
#define AA_TILE_PX 24

/* Attribute row byte 0: flags 0x60 is what keeps fdps_map_set_pending_tile_
   event out of the tile-event table during the walk. */
#define AA_ATTR_FLAGS 0x60
/* Byte 3 is the combat backdrop id.  7 is above zero, so the full-screen
   exchange decrements it and names Back06.saf, the file tests/combat.c reaches
   for with the same id. */
#define AA_ATTR_BACKDROP_AT 3
#define AA_BACKDROP_ID 7

#define AA_UNITS 4
#define AA_CLASS_ROWS 8
#define AA_ITEMS 8
#define AA_ENEMIES 4
#define AA_GROWTHS 64

/* Three of the four staged records are used.  The bystander sits well away
   from the duel so that a run which strayed off the two indices the search
   named would be visible in its record. */
#define AA_ACTOR 0
#define AA_BYSTANDER 1
#define AA_TARGET 2

/* Where everyone starts, and the tile the attack search claims to have picked.
   The attack tile's x and y differ, and (1, 3) is as reachable as (3, 1), so a
   body that read the two globals the other way round lands the actor on a real
   tile rather than failing to move at all. */
#define AA_ACTOR_START_X 1
#define AA_ACTOR_START_Y 1
#define AA_ATTACK_TILE_X 3
#define AA_ATTACK_TILE_Y 1
#define AA_TARGET_X 4
#define AA_TARGET_Y 1
#define AA_BYSTANDER_X 0
#define AA_BYSTANDER_Y 4

/* The duel's numbers, all of them from tests/unitatk.c's fixture. */
#define AA_ATTACK_POWER 100
#define AA_DEFENSE 91
#define AA_ACCURACY 200
#define AA_START_HP 40
#define AA_BLOW_DAMAGE 8
#define AA_ACTOR_MOVE 3

/* AND AL,0x40 in fdps_unit_find_equipped_slot, and an item type inside the
   weapon span 1..0x15. */
#define AA_ENTRY_EQUIPPED 0x40
#define AA_WEAPON_TYPE 1
#define AA_WEAPON_ID 1
/* fdps_check_can_counter_attack takes a range_min of exactly 1 and nothing
   else. */
#define AA_WEAPON_RANGE 1

/* CMP EAX,0x2 on the attacker's side byte and CMP EAX,0x3c on the defender's
   portrait id, the pair that gates an experience award. */
#define AA_PLAYER_SIDE 2
#define AA_ENEMY_SIDE 0
#define AA_FIRST_ENEMY_PORTRAIT 0x3c
#define AA_TARGET_PORTRAIT 5

/* The level cap fdps_unit_award_exp_and_level_up refuses at, for every
   portrait id but the machine soldier's. */
#define AA_LEVEL_CAP 40
#define AA_ACTOR_LEVEL 20
#define AA_PAYABLE_LEVEL 20
#define AA_ENEMY_EXP_REWARD 70

/* The counterblow's award and what this function makes of it.  20 * 70 / 40 is
   35 and 35 * 8 / 40 is 7, which 15/10 turns into 10 and not 10.5; with the
   target payable the divisor is 20, so the award is 70, the scaled figure 14
   and the paid figure 21. */
#define AA_CREDIT_AGAINST_CAPPED 7
#define AA_SCALED_AGAINST_CAPPED 10
#define AA_PAID_TO_PAYABLE 21

/* 0xff at record +0x31 is the "no death script" sentinel, so nothing this
   fixture kills owes a script. */
#define AA_NO_DEATH_SCRIPT 0xff

/* What data_fdps_map_cursor_draw_mode holds going in: neither of the two
   values the body stores, so "left in the box mode" is a store and not an
   inheritance. */
#define AA_DRAW_MODE_SENTINEL 7

/* What the gauge position pairs hold going in.  No placement this fixture can
   produce is either number, so a pair still holding one says the gauge call
   was never made. */
#define AA_POISON_X 200
#define AA_POISON_Y 111

/* The flat 3 percent second-strike roll, and how many draws of the stream a
   seed has to keep clear of it.  Two swings spend six draws between them; 24
   is well past the end of anything a run can consume before the last blow has
   landed. */
#define AA_BONUS_STRIKE_PERCENT 3
#define AA_DRAWS_WATCHED 24
#define AA_SEED_LIMIT 20000

/* The cel kit fdps_blit_cursor_tile draws the cursor box out of: it hardwires
   24 by 24 and reads only the offset table at 0x0f, so the header stays blank.
   One flat fill run per row is the encoding src/rle.c decodes, a command byte
   of len - 1 followed by the pixel. */
#define AA_CEL_TABLE_AT 0x0f
#define AA_CURSOR_STREAM_AT 0x80
#define AA_CURSOR_BYTES (AA_CURSOR_STREAM_AT + AA_TILE_PX * 2)
#define AA_CURSOR_COLOR 0x21

/* The label plate the experience figure is drawn on comes out of Command.cel
   through fdps_cel_blit_sprite, which takes the sprite's size from the sheet's
   own header.  A one-pixel sprite is enough to carry that call: nothing here
   asserts anything about the plate. */
#define AA_PLATE_ENTRIES 64
#define AA_PLATE_STREAM_AT (AA_CEL_TABLE_AT + AA_PLATE_ENTRIES * 4)
#define AA_PLATE_BYTES (AA_PLATE_STREAM_AT + 2)
#define AA_PLATE_COLOR 0x22

/* The number glyph sheet fdps_draw_number reads: entry (colour row * 13 +
   glyph) of the same offset table, and a glyph is 6 by 8. */
#define AA_GLYPH_ENTRIES 16
#define AA_GLYPH_W 6
#define AA_GLYPH_ROWS 8
#define AA_GLYPH_STREAM_AT (AA_CEL_TABLE_AT + AA_GLYPH_ENTRIES * 4)
#define AA_GLYPH_BYTES (AA_GLYPH_STREAM_AT + AA_GLYPH_ROWS * 2)
#define AA_GLYPH_COLOR 0x23

/* The unit gauge sheet: three 43 by 6 graphics 0x102 bytes apart, filled one
   flat value each the way tests/unitatk.c fills them. */
#define AA_GAUGE_GRAPHIC_STRIDE 0x102
#define AA_GAUGE_BYTES (AA_GAUGE_GRAPHIC_STRIDE * 3)

/* The resident BaseAni container, in the shape src/vfs.c reads it: a 35-byte
   header whose table offset is at 5 and whose entry count is at 7, then 26-byte
   entries carrying a 13-byte name, a size at 0x0d and a start at 0x16.  The one
   member is the attack animation, whose .SAF header declares no frames, so
   fdps_play_attack_animation resolves it and composes nothing -- which is what
   keeps these cases about the sequencer.  The name is stored upper case
   because the lookup folds only the query. */
#define AA_VFS_TABLE_OFFSET_AT 5
#define AA_VFS_COUNT_AT 7
#define AA_VFS_TABLE_AT 35
#define AA_VFS_ENTRY_BYTES 26
#define AA_VFS_ENTRY_SIZE_AT 0x0d
#define AA_VFS_ENTRY_START_AT 0x16
#define AA_VFS_MEMBER_AT (AA_VFS_TABLE_AT + AA_VFS_ENTRY_BYTES)
#define AA_SAF_BYTES 0x20
#define AA_SAF_FRAME_COUNT_AT 0x0c
#define AA_VFS_BYTES (AA_VFS_MEMBER_AT + AA_SAF_BYTES)
#define AA_ANIMATION_MEMBER "EASYANI.SAF"

/* A chapter index the palette cycler's switch does not list, so the frames
   these cases present change no DAC entry. */
#define AA_INERT_CHAPTER 1

#define AA_DAC_ENTRIES 256

/* The adapter and the two modes a run switches between. */
#define AA_MODE_TEXT 0x03
#define AA_MODE_320X200X256 0x13
#define AA_TIMER_VECTOR 8

/* The two portrait ids the full-screen exchange composes its clip names out
   of: ACT000.SAF and STAND000.SAF for the attacker, STAND001.SAF for the
   defender, the same pair tests/combat.c drives that function with. */
#define AA_EXCHANGE_ACTOR_PORTRAIT 0
#define AA_EXCHANGE_TARGET_PORTRAIT 1

static struct fdps_unit_record aa_units[AA_UNITS];
static unsigned char aa_items[(AA_ITEMS + 1) * sizeof(struct fdps_item_effect)];
static unsigned char aa_classes[AA_CLASS_ROWS * sizeof(struct fdps_class_record)];
static unsigned char aa_enemies[AA_ENEMIES * sizeof(struct fdps_enemy_data)];
static unsigned char aa_growths[AA_GROWTHS * sizeof(struct fdps_character_growth)];

static unsigned char aa_tilemap[AA_TERRAIN_CELLS_AT + AA_CELLS * 2];
static unsigned char aa_attr[AA_ATTR_ROWS_AT + AA_ATTR_ROWS * 4];
static unsigned char aa_grid[4 + AA_CELLS * 2];
static unsigned char aa_event[AA_EVENT_CELLS_AT + AA_CELLS];

static unsigned char aa_cursor_kit[AA_CURSOR_BYTES];
static unsigned char aa_plate[AA_PLATE_BYTES];
static unsigned char aa_glyphs[AA_GLYPH_BYTES];
static unsigned char aa_gauge_sheet[AA_GAUGE_BYTES];
static unsigned char aa_vfs[AA_VFS_BYTES];
static unsigned char aa_palette[AA_DAC_ENTRIES * 3];

static void (__interrupt __far *aa_saved_timer)();

/* Only ever read for its field offsets and sizes. */
static struct fdps_unit_record aa_layout_probe;

static void __interrupt __far aa_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(aa_saved_timer);
}

static void aa_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void aa_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

static struct fdps_item_effect *aa_item(int item_id)
{
    return (struct fdps_item_effect *)
           (aa_items + (item_id + 1) * sizeof(struct fdps_item_effect));
}

static struct fdps_class_record *aa_class_row(int row_index)
{
    return (struct fdps_class_record *)
           (aa_classes + row_index * sizeof(struct fdps_class_record));
}

static struct fdps_enemy_data *aa_enemy_row(int enemy_index)
{
    return (struct fdps_enemy_data *)
           (aa_enemies + enemy_index * sizeof(struct fdps_enemy_data));
}

/* The lowest seed none of whose first AA_DRAWS_WATCHED draws fires the 3
   percent second-strike roll.  Roughly half of all seeds qualify, so the
   search never runs far. */
static unsigned int aa_quiet_seed(void)
{
    unsigned int seed;
    int draw;
    int quiet;

    for (seed = 1; seed < AA_SEED_LIMIT; seed++) {
        srand(seed);
        quiet = 1;
        for (draw = 0; draw < AA_DRAWS_WATCHED; draw++) {
            if (rand() % 100 < AA_BONUS_STRIKE_PERCENT) {
                quiet = 0;
            }
        }
        if (quiet != 0) {
            return seed;
        }
    }
    return 0;
}

/* One flat fill run per row, which is command byte (len - 1) followed by the
   pixel value. */
static void aa_fill_rows(unsigned char *stream, int rows, int width,
                         int color)
{
    int row;

    for (row = 0; row < rows; row++) {
        stream[row * 2] = (unsigned char) (width - 1);
        stream[row * 2 + 1] = (unsigned char) color;
    }
}

static void aa_stage_sheets(void)
{
    int entry;
    int offset;

    memset(aa_cursor_kit, 0, (size_t) AA_CURSOR_BYTES);
    aa_u32(aa_cursor_kit, AA_CEL_TABLE_AT, (unsigned long) AA_CURSOR_STREAM_AT);
    aa_fill_rows(aa_cursor_kit + AA_CURSOR_STREAM_AT, AA_TILE_PX, AA_TILE_PX,
                 AA_CURSOR_COLOR);

    memset(aa_plate, 0, (size_t) AA_PLATE_BYTES);
    ((struct fdps_cel_header *) aa_plate)->sprite_width = 1;
    ((struct fdps_cel_header *) aa_plate)->sprite_height = 1;
    for (entry = 0; entry < AA_PLATE_ENTRIES; entry++) {
        aa_u32(aa_plate, AA_CEL_TABLE_AT + entry * 4,
               (unsigned long) AA_PLATE_STREAM_AT);
    }
    aa_fill_rows(aa_plate + AA_PLATE_STREAM_AT, 1, 1, AA_PLATE_COLOR);

    memset(aa_glyphs, 0, (size_t) AA_GLYPH_BYTES);
    for (entry = 0; entry < AA_GLYPH_ENTRIES; entry++) {
        aa_u32(aa_glyphs, AA_CEL_TABLE_AT + entry * 4,
               (unsigned long) AA_GLYPH_STREAM_AT);
    }
    aa_fill_rows(aa_glyphs + AA_GLYPH_STREAM_AT, AA_GLYPH_ROWS, AA_GLYPH_W,
                 AA_GLYPH_COLOR);

    for (offset = 0; offset < AA_GAUGE_GRAPHIC_STRIDE; offset++) {
        aa_gauge_sheet[offset] = 10;
        aa_gauge_sheet[AA_GAUGE_GRAPHIC_STRIDE + offset] = 20;
        aa_gauge_sheet[2 * AA_GAUGE_GRAPHIC_STRIDE + offset] = 30;
    }

    memset(aa_vfs, 0, (size_t) AA_VFS_BYTES);
    aa_u16(aa_vfs, AA_VFS_TABLE_OFFSET_AT, (unsigned int) AA_VFS_TABLE_AT);
    aa_u32(aa_vfs, AA_VFS_COUNT_AT, 1UL);
    strcpy((char *) aa_vfs + AA_VFS_TABLE_AT, AA_ANIMATION_MEMBER);
    aa_u32(aa_vfs, AA_VFS_TABLE_AT + AA_VFS_ENTRY_SIZE_AT,
           (unsigned long) AA_SAF_BYTES);
    aa_u32(aa_vfs, AA_VFS_TABLE_AT + AA_VFS_ENTRY_START_AT,
           (unsigned long) AA_VFS_MEMBER_AT);
    aa_vfs[AA_VFS_MEMBER_AT] = 'S';
    aa_vfs[AA_VFS_MEMBER_AT + 1] = 'A';
    aa_vfs[AA_VFS_MEMBER_AT + 2] = 'F';
    aa_u16(aa_vfs, AA_VFS_MEMBER_AT + AA_SAF_FRAME_COUNT_AT, 0);

    for (entry = 0; entry < AA_DAC_ENTRIES * 3; entry++) {
        aa_palette[entry] = 0;
    }
}

/* One combatant, on the tile it starts the case on. */
static void aa_place(int unit_index, int x, int y, int side, int portrait,
                     int level)
{
    aa_units[unit_index].pos_x = (unsigned char) x;
    aa_units[unit_index].pos_y = (unsigned char) y;
    aa_units[unit_index].side = (unsigned char) side;
    aa_units[unit_index].portrait_id = (unsigned char) portrait;
    aa_units[unit_index].level = (unsigned char) level;
    aa_units[unit_index].clazz = 0;
    aa_units[unit_index].move = AA_ACTOR_MOVE;
    aa_units[unit_index].ap = AA_ATTACK_POWER;
    aa_units[unit_index].dp = AA_DEFENSE;
    aa_units[unit_index].hit = AA_ACCURACY;
    aa_units[unit_index].ev = 0;
    aa_units[unit_index].hp_current = AA_START_HP;
    aa_units[unit_index].hp_max = AA_START_HP;
    aa_units[unit_index].death_script_opcode = AA_NO_DEATH_SCRIPT;
    aa_units[unit_index].inventory_slots[0] = AA_ENTRY_EQUIPPED;
    aa_units[unit_index].inventory_slots[1] = AA_WEAPON_ID;
}

/* The whole battle-time environment, plus the decision the attack search is
   claimed to have published. */
static void aa_stage(void)
{
    int i;
    int terrain;

    memset(aa_units, 0, sizeof(aa_units));
    memset(aa_items, 0, (size_t) sizeof(aa_items));
    memset(aa_classes, 0, (size_t) sizeof(aa_classes));
    memset(aa_enemies, 0, (size_t) sizeof(aa_enemies));
    memset(aa_growths, 0, (size_t) sizeof(aa_growths));

    memset(aa_tilemap, 0xaa, (size_t) AA_TERRAIN_CELLS_AT);
    aa_u16(aa_tilemap, 7, (unsigned int) AA_MAP_W);
    aa_u16(aa_tilemap, 9, (unsigned int) AA_MAP_H);
    for (i = 0; i < AA_CELLS; i++) {
        aa_u16(aa_tilemap, AA_TERRAIN_CELLS_AT + i * 2, (unsigned int) i);
    }

    memset(aa_attr, 0xaa, (size_t) AA_ATTR_ROWS_AT);
    for (i = 0; i < AA_ATTR_ROWS; i++) {
        aa_attr[AA_ATTR_ROWS_AT + i * 4] = AA_ATTR_FLAGS;
        aa_attr[AA_ATTR_ROWS_AT + i * 4 + 1] = 0;
        aa_attr[AA_ATTR_ROWS_AT + i * 4 + 2] = 0;
        aa_attr[AA_ATTR_ROWS_AT + i * 4 + AA_ATTR_BACKDROP_AT] =
            AA_BACKDROP_ID;
    }

    aa_u16(aa_grid, 0, (unsigned int) AA_MAP_W);
    aa_u16(aa_grid, 2, (unsigned int) AA_MAP_H);
    for (i = 0; i < AA_CELLS; i++) {
        aa_grid[4 + i * 2] = 0x00;
        aa_grid[4 + i * 2 + 1] = 0xff;
    }

    memset(aa_event, 0xaa, (size_t) AA_EVENT_CELLS_AT);
    aa_u16(aa_event, 7, (unsigned int) AA_MAP_W);
    for (i = 0; i < AA_CELLS; i++) {
        aa_event[AA_EVENT_CELLS_AT + i] = 0;
    }

    for (i = 0; i < AA_CLASS_ROWS; i++) {
        for (terrain = 0; terrain < 8; terrain++) {
            aa_class_row(i)->move_cost[terrain] = 1;
        }
        aa_class_row(i)->critical = 0;
    }

    aa_item(AA_WEAPON_ID)->type = AA_WEAPON_TYPE;
    aa_item(AA_WEAPON_ID)->range_min = AA_WEAPON_RANGE;
    aa_item(AA_WEAPON_ID)->range_max = AA_WEAPON_RANGE;
    aa_item(AA_WEAPON_ID)->hit_effect = 0;
    aa_item(AA_WEAPON_ID)->hit_effect_rate = 0;

    aa_enemy_row(0)->exp_reward = AA_ENEMY_EXP_REWARD;

    aa_stage_sheets();

    data_fdps_map_unit_array_ptr = (unsigned char *) aa_units;
    data_fdps_item_effect_table_ptr =
        aa_items + sizeof(struct fdps_item_effect);
    data_fdps_class_table_ptr = aa_classes;
    data_fdps_battle_enemy_data_table_ptr = aa_enemies;
    data_fdps_battle_character_growth_table_ptr = aa_growths;
    data_fdps_scene_layer_tile_map_ptrs[0] = aa_tilemap;
    data_fdps_scene_layer_tile_attr_ptr[0] = aa_attr;
    data_fdps_battle_move_grid_ptr = aa_grid;
    data_fdps_map_cell_event_code_layer_ptr = aa_event;
    data_fdps_cursor_highlight_sprite_sheet_ptr = aa_cursor_kit;
    data_fdps_command_sprite_sheet_ptr = aa_plate;
    data_fdps_number_glyph_sheet_ptr = aa_glyphs;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_unit_gauge_sheet_ptr = aa_gauge_sheet;
    data_fdps_animation_baseani_archive_ptr = aa_vfs;
    data_fdps_vga_main_palette_ptr = aa_palette;

    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_chapter_current_chapter_id = AA_INERT_CHAPTER;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_input_last_scancode = 3;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    for (i = 0; i < SFX_SAMPLE_SLOT_COUNT; i++) {
        data_fdps_audio_sample_handle_table[i] = NULL;
    }

    for (i = 0; i < 6; i++) {
        data_fdps_battle_tile_attr_ap_modifier_table[i] = 0;
        data_fdps_battle_tile_attr_def_modifier_table[i] = 0;
    }

    aa_place(AA_ACTOR, AA_ACTOR_START_X, AA_ACTOR_START_Y, AA_ENEMY_SIDE,
             AA_FIRST_ENEMY_PORTRAIT, AA_ACTOR_LEVEL);
    aa_place(AA_TARGET, AA_TARGET_X, AA_TARGET_Y, AA_PLAYER_SIDE,
             AA_TARGET_PORTRAIT, AA_LEVEL_CAP);
    aa_place(AA_BYSTANDER, AA_BYSTANDER_X, AA_BYSTANDER_Y, AA_PLAYER_SIDE,
             AA_TARGET_PORTRAIT, AA_LEVEL_CAP);

    data_fdps_map_cursor_world_x = AA_ACTOR_START_X * AA_TILE_PX;
    data_fdps_map_cursor_world_y = AA_ACTOR_START_Y * AA_TILE_PX;

    data_fdps_battle_ai_best_physical_target_idx = AA_TARGET;
    data_fdps_battle_ai_best_physical_target_x = AA_ATTACK_TILE_X;
    data_fdps_battle_ai_best_attack_tile_y = AA_ATTACK_TILE_Y;
    data_fdps_ui_battle_animation_enabled = 0;

    data_fdps_map_cursor_draw_mode = AA_DRAW_MODE_SENTINEL;
    data_fdps_battle_pending_xp_credit = 0;
    data_fdps_battle_combat_gauge_pos_pairs[0] = AA_POISON_X;
    data_fdps_battle_combat_gauge_pos_pairs[1] = AA_POISON_Y;
    data_fdps_battle_combat_gauge_pos_pairs[2] = AA_POISON_X;
    data_fdps_battle_combat_gauge_pos_pairs[3] = AA_POISON_Y;

    srand(aa_quiet_seed());
}

/* Take the defender's weapon out of its hand without emptying the bag: the
   entry is still carried, so the equipped search finds nothing to strike back
   with. */
static void aa_disarm(int unit_index)
{
    aa_units[unit_index].inventory_slots[0] = 0;
}

static void aa_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* One whole action, played in mode 13h with the tick moving under it. */
static int aa_run(void)
{
    int acted;

    aa_set_mode(AA_MODE_320X200X256);
    aa_saved_timer = _dos_getvect(AA_TIMER_VECTOR);
    _dos_setvect(AA_TIMER_VECTOR, aa_timer_isr);

    acted = fdps_map_actor_move_and_attack(AA_ACTOR, AA_ENEMY_SIDE);

    _dos_setvect(AA_TIMER_VECTOR, aa_saved_timer);
    aa_set_mode(AA_MODE_TEXT);
    return acted;
}

/* Put back the globals whose blocks the game's own loaders free, for the
   reason tests/menu.c gives: a pointer left aiming at a static in this file is
   a free() of storage that never came from the heap, made by whichever later
   test calls one of those loaders.

   THE ANIMATION FLAG GOES BACK TOO, and that one is not about free().  The
   case below that turns it on is the only writer of it in this file, and a
   later test file that reaches the map AI's attack arm with it still set plays
   the full-screen exchange instead -- which composes its clip names out of a
   portrait id and blocks on a member that file's fixture does not hold.  The
   symptom lands in tests/mapai.c, nowhere near here. */
static void aa_unstage(void)
{
    data_fdps_ui_battle_animation_enabled = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_class_table_ptr = NULL;
    data_fdps_battle_enemy_data_table_ptr = NULL;
    data_fdps_battle_character_growth_table_ptr = NULL;
    data_fdps_unit_gauge_sheet_ptr = NULL;
    data_fdps_cursor_highlight_sprite_sheet_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_number_glyph_sheet_ptr = NULL;
    data_fdps_animation_baseani_archive_ptr = NULL;
    data_fdps_vga_main_palette_ptr = NULL;
    data_fdps_scene_layer_tile_map_ptrs[0] = NULL;
    data_fdps_scene_layer_tile_attr_ptr[0] = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
}

/* Every expected value above is a field of one of these records at one of
   these offsets, and the +8 of the counterblow's gauge argument is two ints
   inside one four-int array rather than a step off the end of a two-int one
   (rebuild_info/pitfalls.md, contract B). */
static void aa_the_record_layouts_this_action_reads(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 0x01);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 0x06);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 0x07);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, death_script_opcode),
             0x31);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, level), 0x21);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, move), 0x3b);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, exp_carry), 0x3c);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
    CHECK_EQ((int) sizeof(aa_layout_probe.hp_current), 2);

    CHECK_EQ((int) offsetof(struct fdps_item_effect, range_min), 0x0b);
    CHECK_EQ((int) offsetof(struct fdps_enemy_data, exp_reward), 0x09);

    CHECK_EQ((int) sizeof(data_fdps_battle_combat_gauge_pos_pairs),
             4 * (int) sizeof(int));
}

/* The answer is the constant 1, and the cursor is left in the plain box mode
   -- the last thing the body stores into it, and not the mode the caller was
   in.  The award is refused here (the target is at its level cap), so that
   store is this function's own and not the award routine's. */
static void aa_returns_one_and_leaves_the_box_cursor(void)
{
    int acted;

    aa_stage();
    acted = aa_run();

    CHECK_EQ(acted, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
    aa_unstage();
}

/* The walk goes to the tile the attack search named, x out of 00063f80 and y
   out of 00063f84.  The second half asks for the transposed tile and gets the
   transposed answer, which is what a body that read the two globals the other
   way round would produce for the first. */
static void aa_walks_the_actor_to_the_tile_the_search_named(void)
{
    aa_stage();
    aa_run();

    CHECK_EQ((int) aa_units[AA_ACTOR].pos_x, AA_ATTACK_TILE_X);
    CHECK_EQ((int) aa_units[AA_ACTOR].pos_y, AA_ATTACK_TILE_Y);

    aa_stage();
    data_fdps_battle_ai_best_physical_target_x = AA_ATTACK_TILE_Y;
    data_fdps_battle_ai_best_attack_tile_y = AA_ATTACK_TILE_X;
    aa_run();

    CHECK_EQ((int) aa_units[AA_ACTOR].pos_x, AA_ATTACK_TILE_Y);
    CHECK_EQ((int) aa_units[AA_ACTOR].pos_y, AA_ATTACK_TILE_X);
    aa_unstage();
}

/* The unit fought is the one 00063f74 names and not the neighbouring index:
   the target loses one blow's worth of hit points and the bystander's record
   is untouched. */
static void aa_fights_the_unit_the_search_named(void)
{
    aa_stage();
    aa_run();

    CHECK_EQ((int) aa_units[AA_TARGET].hp_current,
             AA_START_HP - AA_BLOW_DAMAGE);
    CHECK_EQ((int) aa_units[AA_BYSTANDER].hp_current, AA_START_HP);
    CHECK_EQ((int) aa_units[AA_BYSTANDER].pos_x, AA_BYSTANDER_X);
    CHECK_EQ((int) aa_units[AA_BYSTANDER].pos_y, AA_BYSTANDER_Y);
    aa_unstage();
}

/* A defender standing one tile away with a range-1 weapon answers 1 to both
   counter-attack tests, so the counterblow is played and the actor takes a
   blow of its own. */
static void aa_an_armed_defender_strikes_back(void)
{
    aa_stage();
    aa_run();

    CHECK_EQ((int) aa_units[AA_ACTOR].hp_current,
             AA_START_HP - AA_BLOW_DAMAGE);
    CHECK_EQ((int) aa_units[AA_TARGET].hp_current,
             AA_START_HP - AA_BLOW_DAMAGE);
    aa_unstage();
}

/* A defender with nothing in its hand answers -1, and -1 is not 1.  This is
   the case the natural `if (fdps_check_can_counter_attack(...))` gets wrong:
   the actor would take a blow it never takes in the original. */
static void aa_an_unarmed_defender_never_strikes_back(void)
{
    aa_stage();
    aa_disarm(AA_TARGET);
    aa_run();

    CHECK_EQ((int) aa_units[AA_ACTOR].hp_current, AA_START_HP);
    CHECK_EQ((int) aa_units[AA_TARGET].hp_current,
             AA_START_HP - AA_BLOW_DAMAGE);
    aa_unstage();
}

/* The other operand of the AND: an armed defender the actor's blow left on
   zero hit points does not strike back, and no experience is credited because
   the counterblow that would have earned it never happened. */
static void aa_a_dead_defender_never_strikes_back(void)
{
    aa_stage();
    aa_units[AA_TARGET].hp_current = AA_BLOW_DAMAGE;
    aa_run();

    CHECK_EQ((int) aa_units[AA_TARGET].hp_current, 0);
    CHECK_EQ((int) aa_units[AA_ACTOR].hp_current, AA_START_HP);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    aa_unstage();
}

/* The credit the counterblow earned is multiplied by 15 and divided by 10,
   signed and truncating: an award of 7 comes out as 10 and not as 10.5 or 11.
   It is still readable because the unit it is offered to is at its level cap,
   which refuses the award without clearing the accumulator. */
static void aa_the_credit_is_scaled_by_fifteen_tenths(void)
{
    aa_stage();
    aa_run();

    CHECK_EQ(data_fdps_battle_pending_xp_credit, AA_SCALED_AGAINST_CAPPED);
    aa_unstage();
}

/* The experience is paid to the unit that was attacked and not to the actor.
   With the target below its cap the award runs to the end: the scaled figure
   lands in the target's exp_carry, the accumulator is cleared, and the actor
   -- which is not capped either, and would have taken the payment had its own
   index been handed over -- is left carrying nothing. */
static void aa_the_experience_is_paid_to_the_target(void)
{
    aa_stage();
    aa_units[AA_TARGET].level = AA_PAYABLE_LEVEL;
    aa_run();

    CHECK_EQ((int) aa_units[AA_TARGET].exp_carry, AA_PAID_TO_PAYABLE);
    CHECK_EQ((int) aa_units[AA_ACTOR].exp_carry, 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    aa_unstage();
}

/* The accumulator is cleared on the way in, before a blow can add to it, so
   whatever the previous action left there is not paid out again. */
static void aa_the_pending_credit_is_cleared_on_entry(void)
{
    aa_stage();
    aa_disarm(AA_TARGET);
    data_fdps_battle_pending_xp_credit = 999;
    aa_run();

    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    aa_unstage();
}

/* The animation setting is read as a byte against 0.  Clear, and the fight is
   played on the map: the gauge call runs and writes both position pairs over
   the fixture's poison.  Set, and the whole exchange goes to the full-screen
   animation instead -- both pairs still hold the poison, so no gauge was put
   on the map, and both records lost hit points, so the exchange really did
   play the fight and its own counter-attack test. */
static void aa_the_animation_flag_picks_the_full_screen_fight(void)
{
    aa_stage();
    aa_run();

    CHECK_EQ(data_fdps_battle_combat_gauge_pos_pairs[0] != AA_POISON_X, 1);
    CHECK_EQ(data_fdps_battle_combat_gauge_pos_pairs[1] != AA_POISON_Y, 1);

    aa_stage();
    data_fdps_ui_battle_animation_enabled = 1;
    aa_units[AA_ACTOR].portrait_id = AA_EXCHANGE_ACTOR_PORTRAIT;
    aa_units[AA_TARGET].portrait_id = AA_EXCHANGE_TARGET_PORTRAIT;
    aa_run();

    CHECK_EQ(data_fdps_battle_combat_gauge_pos_pairs[0], AA_POISON_X);
    CHECK_EQ(data_fdps_battle_combat_gauge_pos_pairs[1], AA_POISON_Y);
    CHECK_EQ((int) aa_units[AA_TARGET].hp_current < AA_START_HP, 1);
    CHECK_EQ((int) aa_units[AA_ACTOR].hp_current < AA_START_HP, 1);
    aa_unstage();
}

/* ---- fdps_map_actor_cast_chosen_spell, 00013c90 -----------------------
 *
 * The cast is a sequencer as well, so the same rule holds: every case runs it
 * end to end against the real callees and against the real MISC.VFS,
 * FIGHT.VFS, BACKGRND.VFS and blend tables staged by tests/gamefile.lst, and
 * reads its answers off the unit records, the movement grid and the globals.
 *
 * Expected values come from the assembly at 00013c90 -- CMP byte ptr
 * [EAX + 0x6],0x0 at 00013cb6 with the stores of 1 and 0 that follow it, the
 * widened MOV AL,byte ptr [EDX + 0x6] on the other arm, CMP dword ptr
 * [0x00063f88],0x6 / JL at 00013cd9, the push order at 00013cf2-00013d0e that
 * makes [0x00063f9c] the x of the aim and [0x00063fa0] the y, PUSH 0x0 at
 * 00013cf6, the CALL to fdps_map_grid_reset at 00013d1f AFTER the collect, the
 * two IMUL by 0x18 at 00013d37 and 00013d3f, CMP EAX,0x1 / JNZ at 00013d72,
 * MOV dword ptr [0x00069cec],0x0 at 00013ddc and MOV dword ptr
 * [0x00069cd0],0x0 at 00013de6 -- and from the documented behaviour of the
 * callees.  None of them is read off the emitted C.
 *
 * WHY THE SPELL IS 0x05 ON THE MAP CASES AND 0x00 ON THE ANIMATION ONE.
 * MISC.VFS holds EMG05.SAF, so the map presentation finds the clip it composes
 * out of the id, and a member it cannot find ends the process inside
 * fdps_vfs_load_entry rather than failing an assertion.  Record 5's MP cost is
 * also the only non-zero one in the staged table, which is what makes the
 * caster's MP a witness that the id forwarded to the presentation is the one
 * the search left in 00063f90.  The full-screen case has to use the id
 * tests/cmbspell.c already drives that presentation with -- MISC.VFS ships
 * MB00/ML00/ME00 and FIGHT.VFS MAGIC000.SAF, STAND000.SAF and STAND001.SAF --
 * because the fight screen composes four further names of its own.
 *
 * THE GEOMETRY.  The blast is reach 1, a diamond of the aim tile and its four
 * orthogonal neighbours, aimed at (4, 1).  The target stands on the aim tile
 * on the player's side and the ally on (3, 1) on the enemy's, so the two are
 * both inside the blast and only the side filter can tell them apart; the
 * caster sits at (0, 0) and the witness at (1, 4), both outside it.  Aiming at
 * the transposed tile (1, 4) instead puts the witness inside the blast and the
 * other two outside, which is what a body that read the two tile globals the
 * other way round would produce for the first aim. */

/* The 0x28 records MAGICDAT.DAT holds, at the stride fdps_get_spell_record
   multiplies by. */
#define AS_SPELL_TABLE_ENTRIES 0x28
#define AS_SPELL_RECORD_STRIDE 7

#define AS_SPELL_MAP 0x05
#define AS_SPELL_FIGHT 0x00

/* A power of 10 against a magic-resist complement of 100 is the unscaled
   figure tests/spell.c pins on fdps_spell_damage_unit, and a hit rate of 100
   makes rand() % 100 < hit_rate true on every draw.  What each case reads is
   only whether a record lost hit points, so the figure itself is that file's
   assertion and not one made here. */
#define AS_SPELL_POWER 10
#define AS_ALWAYS_HITS 100
#define AS_NO_RESISTANCE 100

/* The only non-zero MP cost in the staged table. */
#define AS_SPELL_MP_COST 3
#define AS_CASTER_MP 40
#define AS_MP_AFTER_CAST (AS_CASTER_MP - AS_SPELL_MP_COST)

/* The record's reach byte. */
#define AS_BLAST_REACH 1

/* Either side of the JL at 00013cd9. */
#define AS_SCORE_PASSES 6
#define AS_SCORE_REFUSED 5

/* side_select as the two phases pass it. */
#define AS_ENEMY_PHASE 0
#define AS_NPC_PHASE 1

/* The authored target-side byte at record +6.  0 is the player-facing value
   the enemy phase inverts; 2 is an authored value that is neither 0 nor a
   select_mode the collector reads the same way, and 3 is the mode that keeps
   side 2. */
#define AS_SIDE_BYTE_PLAYER 0
#define AS_SIDE_BYTE_TWO 2
#define AS_SIDE_BYTE_THREE 3

/* The fourth staged unit: the caster's own side, standing inside the blast. */
#define AS_ALLY 3

#define AS_CASTER_X 0
#define AS_CASTER_Y 0
#define AS_CAST_TILE_X 4
#define AS_CAST_TILE_Y 1
#define AS_ALLY_X 3
#define AS_ALLY_Y 1
#define AS_WITNESS_X 1
#define AS_WITNESS_Y 4

/* Where the cursor is parked before each run: a tile that is neither the
   caster's nor the aim, so a run that moved it and a run that did not are told
   apart. */
#define AS_PARKED_TILE 2

/* What the pending experience holds on the way in. */
#define AS_STALE_CREDIT 999

/* One cache slot of map unit sprites: twelve stream offsets at the base of the
   block, which is the one sheet whose table starts there rather than at 0x0f
   (src/mapdraw.c).  Every entry points at the same 24-row flat fill, so the
   four units on the map draw as solid blocks. */
#define AS_SPRITE_CACHE_ENTRIES 12
#define AS_SPRITE_TABLE_BYTES (AS_SPRITE_CACHE_ENTRIES * 4)
#define AS_SPRITE_CACHE_BYTES (AS_SPRITE_TABLE_BYTES + AA_TILE_PX * 2)
#define AS_SPRITE_COLOR 0x24

/* The shadow the first of fdps_draw_map_units' two sweeps lays down for every
   unit whose portrait id is not one of the exempt ones.  Its table is at the
   ordinary 0x0f and the frames it reaches are the walk frame and the acted
   frame, so four entries would do and sixteen are staged.  The attack fixture
   above leaves this sheet NULL because it draws no units at all; the cast
   cases put four on the map, and the sweep does not test the pointer. */
#define AS_SHADOW_ENTRIES 16
#define AS_SHADOW_STREAM_AT (AA_CEL_TABLE_AT + AS_SHADOW_ENTRIES * 4)
#define AS_SHADOW_BYTES (AS_SHADOW_STREAM_AT + AA_TILE_PX * 2)
#define AS_SHADOW_COLOR 0x25

/* The blast outline the overlay mode this function sets puts on the map.  Mode
   3 draws its five cells from cursor sprites 2, 3, 4, 5 and 0x0e, and the
   attack fixture's kit carries only sprite 0 -- an entry the other indices
   reach is zero there, which aims the decoder at the sheet's own header.  The
   whole 0x12 the three diamond modes can ask for are staged. */
#define AS_CURSOR_ENTRIES 0x12
#define AS_CURSOR_STREAM_AT (AA_CEL_TABLE_AT + AS_CURSOR_ENTRIES * 4)
#define AS_CURSOR_BYTES (AS_CURSOR_STREAM_AT + AA_TILE_PX * 2)
#define AS_CURSOR_COLOR 0x26

static unsigned char as_spells[AS_SPELL_TABLE_ENTRIES * AS_SPELL_RECORD_STRIDE];
static unsigned char as_sprite_cache[AS_SPRITE_CACHE_BYTES];
static unsigned char as_shadow_sheet[AS_SHADOW_BYTES];
static unsigned char as_cursor_kit[AS_CURSOR_BYTES];

static struct fdps_spell_effect *as_spell(int spell_id)
{
    return (struct fdps_spell_effect *)
           (as_spells + spell_id * AS_SPELL_RECORD_STRIDE);
}

/* aa_stage's whole battle environment, plus the MAGICDAT.DAT record and the
   decision the spell search is claimed to have published.  The four units are
   moved onto the geometry described above, the caster on the player's side so
   the full-screen presentation composes the M-prefixed clip names MISC.VFS
   holds for it. */
static void as_stage(int spell_id, int target_side_byte)
{
    int i;

    aa_stage();

    memset(as_spells, 0, (size_t) sizeof(as_spells));
    as_spell(spell_id)->power = AS_SPELL_POWER;
    as_spell(spell_id)->hit_rate = AS_ALWAYS_HITS;
    as_spell(spell_id)->area = AS_BLAST_REACH;
    as_spell(spell_id)->mp_cost = AS_SPELL_MP_COST;
    as_spell(spell_id)->target_side = (unsigned char) target_side_byte;
    data_fdps_battle_spell_effect_table_ptr = as_spells;

    for (i = 0; i < AA_CLASS_ROWS; i++) {
        aa_class_row(i)->magic_resist_complement = AS_NO_RESISTANCE;
    }

    memset(as_sprite_cache, 0, (size_t) AS_SPRITE_CACHE_BYTES);
    for (i = 0; i < AS_SPRITE_CACHE_ENTRIES; i++) {
        aa_u32(as_sprite_cache, i * 4, (unsigned long) AS_SPRITE_TABLE_BYTES);
    }
    aa_fill_rows(as_sprite_cache + AS_SPRITE_TABLE_BYTES, AA_TILE_PX,
                 AA_TILE_PX, AS_SPRITE_COLOR);
    data_fdps_cel_sprite_cache_ptr = as_sprite_cache;

    memset(as_shadow_sheet, 0, (size_t) AS_SHADOW_BYTES);
    for (i = 0; i < AS_SHADOW_ENTRIES; i++) {
        aa_u32(as_shadow_sheet, AA_CEL_TABLE_AT + i * 4,
               (unsigned long) AS_SHADOW_STREAM_AT);
    }
    aa_fill_rows(as_shadow_sheet + AS_SHADOW_STREAM_AT, AA_TILE_PX, AA_TILE_PX,
                 AS_SHADOW_COLOR);
    data_fdps_shadow_sprite_sheet_ptr = as_shadow_sheet;

    memset(as_cursor_kit, 0, (size_t) AS_CURSOR_BYTES);
    for (i = 0; i < AS_CURSOR_ENTRIES; i++) {
        aa_u32(as_cursor_kit, AA_CEL_TABLE_AT + i * 4,
               (unsigned long) AS_CURSOR_STREAM_AT);
    }
    aa_fill_rows(as_cursor_kit + AS_CURSOR_STREAM_AT, AA_TILE_PX, AA_TILE_PX,
                 AS_CURSOR_COLOR);
    data_fdps_cursor_highlight_sprite_sheet_ptr = as_cursor_kit;

    /* The damage figure this cast floats over its target is the first thing in
       this file to blit a digit, and fdps_cel_blit_sprite takes the piece's
       size out of the SHEET's header at +0x07 and +0x09 (src/sprite.c).  The
       attack fixture leaves that header at zero because nothing there draws
       one: a width of 0 sends the decoder's row counter under zero and it
       decodes 65535 pixels a row for 65535 rows. */
    ((struct fdps_cel_header *) aa_glyphs)->sprite_width = AA_GLYPH_W;
    ((struct fdps_cel_header *) aa_glyphs)->sprite_height = AA_GLYPH_ROWS;

    /* The fight screen builds these two and frees them without clearing the
       pointers, so they are the witness that tells the two routes apart. */
    data_fdps_combat_gauge_sprite_sheet_ptr = NULL;
    data_fdps_gauge_fill_sheet_ptr = NULL;

    aa_place(AA_ACTOR, AS_CASTER_X, AS_CASTER_Y, AA_PLAYER_SIDE,
             AA_EXCHANGE_ACTOR_PORTRAIT, AA_ACTOR_LEVEL);
    aa_place(AA_TARGET, AS_CAST_TILE_X, AS_CAST_TILE_Y, AA_PLAYER_SIDE,
             AA_EXCHANGE_TARGET_PORTRAIT, AA_LEVEL_CAP);
    aa_place(AS_ALLY, AS_ALLY_X, AS_ALLY_Y, AA_ENEMY_SIDE,
             AA_EXCHANGE_TARGET_PORTRAIT, AA_LEVEL_CAP);
    aa_place(AA_BYSTANDER, AS_WITNESS_X, AS_WITNESS_Y, AA_PLAYER_SIDE,
             AA_EXCHANGE_TARGET_PORTRAIT, AA_LEVEL_CAP);
    aa_units[AA_ACTOR].mp_current = AS_CASTER_MP;
    data_fdps_map_unit_count = AA_UNITS;

    data_fdps_map_cursor_world_x = AS_PARKED_TILE * AA_TILE_PX;
    data_fdps_map_cursor_world_y = AS_PARKED_TILE * AA_TILE_PX;

    data_fdps_map_ai_best_spell_id = spell_id;
    data_fdps_battle_ai_best_spell_score = AS_SCORE_PASSES;
    data_fdps_battle_ai_best_spell_target_x = (unsigned int) AS_CAST_TILE_X;
    data_fdps_battle_ai_best_spell_target_y = (unsigned int) AS_CAST_TILE_Y;

    data_fdps_battle_pending_xp_credit = AS_STALE_CREDIT;
}

static void as_unstage(void)
{
    aa_unstage();
    data_fdps_battle_spell_effect_table_ptr = NULL;
    data_fdps_shadow_sprite_sheet_ptr = NULL;
    data_fdps_combat_gauge_sprite_sheet_ptr = NULL;
    data_fdps_gauge_fill_sheet_ptr = NULL;
}

/* One whole cast, played in mode 13h with the tick moving under it. */
static int as_run(int side_select)
{
    int acted;

    aa_set_mode(AA_MODE_320X200X256);
    aa_saved_timer = _dos_getvect(AA_TIMER_VECTOR);
    _dos_setvect(AA_TIMER_VECTOR, aa_timer_isr);

    acted = fdps_map_actor_cast_chosen_spell(AA_ACTOR, side_select);

    _dos_setvect(AA_TIMER_VECTOR, aa_saved_timer);
    aa_set_mode(AA_MODE_TEXT);
    return acted;
}

/* How many of the movement grid's cells still carry the 0xff sentinel. */
static int as_untouched_grid_cells(void)
{
    int cell_index;
    int untouched;

    untouched = 0;
    for (cell_index = 0; cell_index < AA_CELLS; cell_index++) {
        if (aa_grid[4 + cell_index * 2 + 1] == 0xff) {
            untouched++;
        }
    }
    return untouched;
}

/* Below 6 the whole body is skipped: no cursor is moved, no target is
   collected, no spell is played, the overlay mode the caller was in survives
   and the pending experience is not discarded.  The answer is 0, which is the
   only path that produces it. */
static void as_the_score_gate_refuses_below_six(void)
{
    int acted;

    as_stage(AS_SPELL_MAP, AS_SIDE_BYTE_PLAYER);
    data_fdps_battle_ai_best_spell_score = AS_SCORE_REFUSED;
    acted = as_run(AS_ENEMY_PHASE);

    CHECK_EQ(acted, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AA_DRAW_MODE_SENTINEL);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, AS_STALE_CREDIT);
    CHECK_EQ(data_fdps_map_cursor_world_x, AS_PARKED_TILE * AA_TILE_PX);
    CHECK_EQ(data_fdps_map_cursor_world_y, AS_PARKED_TILE * AA_TILE_PX);
    CHECK_EQ((int) aa_units[AA_TARGET].hp_current, AA_START_HP);
    CHECK_EQ((int) aa_units[AS_ALLY].hp_current, AA_START_HP);
    CHECK_EQ((int) aa_units[AA_ACTOR].mp_current, AS_CASTER_MP);
    CHECK_EQ(as_untouched_grid_cells(), AA_CELLS);
    as_unstage();
}

/* A score of exactly 6 passes the gate: the answer is 1, the cursor is left on
   the aim tile with no overlay at all -- 0, and not the 1 the on-map cast puts
   there before returning -- the movement grid is back at its sentinel because
   the reset is made after the collect, the pending experience is thrown away,
   and the caster paid record 5's MP cost, which is how the spell id that
   reached the presentation is known to be the one the search published. */
static void as_a_cast_returns_one_and_leaves_no_overlay(void)
{
    int acted;

    as_stage(AS_SPELL_MAP, AS_SIDE_BYTE_PLAYER);
    acted = as_run(AS_ENEMY_PHASE);

    CHECK_EQ(acted, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 0);
    CHECK_EQ(data_fdps_map_cursor_world_x, AS_CAST_TILE_X * AA_TILE_PX);
    CHECK_EQ(data_fdps_map_cursor_world_y, AS_CAST_TILE_Y * AA_TILE_PX);
    CHECK_EQ(as_untouched_grid_cells(), AA_CELLS);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    CHECK_EQ((int) aa_units[AA_ACTOR].mp_current, AS_MP_AFTER_CAST);
    as_unstage();
}

/* The enemy phase inverts the record's target side.  An authored 0 becomes the
   filter that keeps every non-zero side, so the player-side target inside the
   blast is struck and the caster's own ally beside it is not; an authored 2 --
   any value but 0 -- collapses to the filter that keeps side 0, so the ally is
   struck and the target is not.  A body that handed the byte through on this
   arm would have aimed the first case at its own ranks. */
static void as_the_enemy_phase_inverts_the_authored_side(void)
{
    as_stage(AS_SPELL_MAP, AS_SIDE_BYTE_PLAYER);
    as_run(AS_ENEMY_PHASE);

    CHECK_EQ((int) aa_units[AA_TARGET].hp_current < AA_START_HP, 1);
    CHECK_EQ((int) aa_units[AS_ALLY].hp_current, AA_START_HP);
    CHECK_EQ((int) aa_units[AA_BYSTANDER].hp_current, AA_START_HP);

    as_stage(AS_SPELL_MAP, AS_SIDE_BYTE_TWO);
    as_run(AS_ENEMY_PHASE);

    CHECK_EQ((int) aa_units[AS_ALLY].hp_current < AA_START_HP, 1);
    CHECK_EQ((int) aa_units[AA_TARGET].hp_current, AA_START_HP);
    as_unstage();
}

/* The NPC phase hands the byte through unchanged.  The same authored 0 that
   the enemy phase turned into "every non-zero side" is now the filter that
   keeps side 0, so this time the ally is struck and the target is not; an
   authored 3 keeps side 2 and strikes the target.  Both are the opposite of
   what the inverting arm produces for the same record. */
static void as_the_npc_phase_uses_the_authored_side(void)
{
    as_stage(AS_SPELL_MAP, AS_SIDE_BYTE_PLAYER);
    as_run(AS_NPC_PHASE);

    CHECK_EQ((int) aa_units[AS_ALLY].hp_current < AA_START_HP, 1);
    CHECK_EQ((int) aa_units[AA_TARGET].hp_current, AA_START_HP);

    as_stage(AS_SPELL_MAP, AS_SIDE_BYTE_THREE);
    as_run(AS_NPC_PHASE);

    CHECK_EQ((int) aa_units[AA_TARGET].hp_current < AA_START_HP, 1);
    CHECK_EQ((int) aa_units[AS_ALLY].hp_current, AA_START_HP);
    as_unstage();
}

/* The blast is centred on the tile the spell search named, x out of 00063f9c
   and y out of 00063fa0, and the cursor is walked to that tile scaled by 24
   pixels.  The second half asks for the transposed tile and gets the
   transposed answer -- the witness on (1, 4) struck instead of the target on
   (4, 1) -- which is what a body that read the two globals the other way round
   would produce for the first. */
static void as_the_blast_is_centred_on_the_tile_the_search_named(void)
{
    as_stage(AS_SPELL_MAP, AS_SIDE_BYTE_PLAYER);
    as_run(AS_ENEMY_PHASE);

    CHECK_EQ((int) aa_units[AA_TARGET].hp_current < AA_START_HP, 1);
    CHECK_EQ((int) aa_units[AA_BYSTANDER].hp_current, AA_START_HP);
    CHECK_EQ(data_fdps_map_cursor_world_x, AS_CAST_TILE_X * AA_TILE_PX);
    CHECK_EQ(data_fdps_map_cursor_world_y, AS_CAST_TILE_Y * AA_TILE_PX);

    as_stage(AS_SPELL_MAP, AS_SIDE_BYTE_PLAYER);
    data_fdps_battle_ai_best_spell_target_x = (unsigned int) AS_WITNESS_X;
    data_fdps_battle_ai_best_spell_target_y = (unsigned int) AS_WITNESS_Y;
    as_run(AS_ENEMY_PHASE);

    CHECK_EQ((int) aa_units[AA_BYSTANDER].hp_current < AA_START_HP, 1);
    CHECK_EQ((int) aa_units[AA_TARGET].hp_current, AA_START_HP);
    CHECK_EQ(data_fdps_map_cursor_world_x, AS_WITNESS_X * AA_TILE_PX);
    CHECK_EQ(data_fdps_map_cursor_world_y, AS_WITNESS_Y * AA_TILE_PX);
    as_unstage();
}

/* The animation setting is an equality against 1 and not a truth test.  A 2
   takes the map route, which never builds a fight-screen gauge, so both gauge
   pointers are still the NULL the fixture left them at.  A 1 takes the
   full-screen route, which loads one sheet out of MISC.VFS and mallocs the
   other and frees both without clearing either pointer, so both come back
   non-NULL.  The target loses hit points on both routes, so the damage is not
   what tells them apart -- under `if (flag)` the 2 would have gone to the
   fight screen and this case would read the same figures with the pointers
   set. */
static void as_the_animation_flag_is_an_equality_with_one(void)
{
    as_stage(AS_SPELL_FIGHT, AS_SIDE_BYTE_PLAYER);
    data_fdps_ui_battle_animation_enabled = 2;
    as_run(AS_ENEMY_PHASE);

    CHECK_EQ(data_fdps_combat_gauge_sprite_sheet_ptr == NULL, 1);
    CHECK_EQ(data_fdps_gauge_fill_sheet_ptr == NULL, 1);
    CHECK_EQ((int) aa_units[AA_TARGET].hp_current < AA_START_HP, 1);

    as_stage(AS_SPELL_FIGHT, AS_SIDE_BYTE_PLAYER);
    data_fdps_ui_battle_animation_enabled = 1;
    as_run(AS_ENEMY_PHASE);

    CHECK_EQ(data_fdps_combat_gauge_sprite_sheet_ptr == NULL, 0);
    CHECK_EQ(data_fdps_gauge_fill_sheet_ptr == NULL, 0);
    CHECK_EQ((int) aa_units[AA_TARGET].hp_current < AA_START_HP, 1);
    as_unstage();
}

/* The third fixture, for fdps_map_actor_use_item, and it builds on the cast's
 * the way the cast's builds on the attack's: as_stage already puts up the map
 * sprite cache, the shadow sheet, the whole 0x12-entry cursor kit the diamond
 * modes reach into and the glyph header, all of which the item action needs
 * for the same reasons.  What is added here is the two ITEM.DAT records, the
 * bag entries they sit in, the line the beam is fired along and the decision
 * the item search is claimed to have published.
 *
 * BOTH RECORDS CARRY A DEAD USE-EFFECT.  Code 5 matches no branch of
 * fdps_apply_item_effect_to_targets (item.h): it plays nothing, changes
 * nothing, consumes nothing and is one of the few codes that comes back
 * without touching the frame clock.  That is deliberate -- these cases are
 * about the target collection and the presentation this function drives, and
 * the effect's own cover is tests/item.c.
 *
 * LAYER 0'S BLOB HAS TO OPEN WITH A REAL .MPL MAGIC.  The clamp on the
 * swept-to tile reads its bounds from the signed words at +0 and +2 of it,
 * which on a real .MPL are the first four bytes of the magic and never the
 * map's dimensions (src/aiact.c).  aa_stage leaves 0xaa there, which is a
 * bound no map ever presents, so the fixture writes the magic back.
 *
 * Expected values come from the assembly at 00027180 -- the byte at record
 * +0xb + 2 * slot at 000271cf, CMP byte ptr [EAX + 0x11],0x0 at 000271f3, the
 * CMP EAX,0xf / JLE at 00027233 and CMP EAX,0x10 / JGE at 000272d0, the push
 * orders of the two collectors at 00027269 and 00027298, ADD EAX,0x2 at
 * 000272c0, MOV dword ptr [0x00069cd0],0x6 at 00027356, the two MOVSX word
 * reads at 00027365 and 00027370, the IDIV 0x18 pairs at 0002738a-000273f4,
 * the four clamp arms at 000273fd-0002744d, MOV dword ptr [0x0006015c],0x14 at
 * 0002744d and MOV dword ptr [0x00069cd0],0x0 at 0002746f -- and from the
 * documented behaviour of the two collectors and of fdps_unit_face_target.
 */

/* Two ITEM.DAT records in the table aa_stage staged, one of each shape. */
#define AI_AREA_ITEM 2
#define AI_LINE_ITEM 3
#define AI_DEAD_USE_EFFECT 5

/* Which of the eight two-byte bag entries each of them sits in.  The id of
   entry n is the record byte at 0x0b + 2 * n, so entry 2 and entry 3 are two
   bytes apart and a body that dropped the doubling would read entry 3's id out
   of the middle of entry 1. */
#define AI_AREA_SLOT 2
#define AI_LINE_SLOT 3

/* The area record: a use_distance below 0x10 and a blast radius of 1, so the
   overlay left up when the routine returns is mode 3. */
#define AI_AREA_USE_DISTANCE 1
#define AI_AREA_RADIUS 1
#define AI_AREA_MODE (AI_AREA_RADIUS + 2)

/* The line record: a use_distance of 0x10 or more makes it a line, and the
   byte less 0x10 is the beam's length in tiles (compared and subtracted, not
   masked).  Its radius is 0, so the diamond the sweep opens under is
   mode 2 and the mode the sweep itself runs in is 6. */
#define AI_BEAM_TILES 5
#define AI_LINE_USE_DISTANCE (0x10 + AI_BEAM_TILES)
#define AI_LINE_RADIUS 0

/* The authored target side both records carry: the player-facing 0 that the
   enemy phase inverts. */
#define AI_SIDE_BYTE_PLAYER 0

/* The row everything stands on, and who stands where.  The actor is on the
   enemy side, so on the enemy phase the beam keeps side 0 -- the unit at
   AI_ENEMY_TARGET_X -- and on the NPC phase it keeps every non-zero side --
   the unit at AI_PLAYER_TARGET_X, one tile further out.  Which of the two the
   beam picked is read back off the cursor, because the last thing the line arm
   does is walk the cursor onto targets[0]. */
#define AI_ROW_Y 1
#define AI_ACTOR_X 1
#define AI_ENEMY_TARGET_X 3
#define AI_PLAYER_TARGET_X 4
#define AI_WEST_TARGET_X 0

/* The tile the search is claimed to have picked: one tile east of the actor,
   which is a direction and not a distance -- the collector walks AI_BEAM_TILES
   tiles that way whatever the aim tile's own distance is (aitarget.h). */
#define AI_AIM_EAST_X 2

/* Where the north beam's target stands, for the case that clamps the swept-to
   y at 0. */
#define AI_NORTH_TARGET_X 1
#define AI_NORTH_TARGET_Y 0

/* The aim tile of the area case, far enough from the actor that the walk to it
   is more than one tile on the dominant axis. */
#define AI_AREA_AIM_X 4

/* Where the beam's far end lands: the actor's own tile plus the beam's length
   times the step toward the aim tile, so 1 + 5 * (2 - 1).  Six is PAST the
   staged map's own width of five and is left alone, because the clamp's bounds
   come from the .MPL magic and not from the map -- a clamp against the real
   width would have cut it to four. */
#define AI_SWEEP_X (AI_ACTOR_X + AI_BEAM_TILES * (AI_AIM_EAST_X - AI_ACTOR_X))

/* What the two bogus bounds actually are: the signed words the magic bytes
   'M' 'P' and 'L' '\0' spell. */
#define AI_MPL_BOUND_X 0x504d
#define AI_MPL_BOUND_Y 0x004c

/* MOV dword ptr [0x0006015c],0x14 on the line arm, and a phase the area arm
   must leave exactly as it found it.  Nothing advances the ramp in these runs
   because the fixture leaves data_fdps_scene_layer_count at 0 and the advance
   lives in the per-layer draw (src/mapdraw.c), so both values are readable
   after the run.  The sentinel is a phase the ramp modulo accepts, so a run
   that did draw a layer would still not corrupt anything. */
#define AI_TRAIL_PHASE 0x14
#define AI_BLEND_SENTINEL 55

/* fdps_unit_face_target's code for +x (unit.h), which is where every beam in
   these cases but the two clamp ones points. */
#define AI_FACING_RIGHT 3

/* aa_stage's whole battle environment and as_stage's presentation kit, plus
   the two item records, the bag entries holding them and the decision the item
   search is claimed to have published.  bag_slot is which of the two entries
   that decision names. */
static void ai_stage(int bag_slot)
{
    as_stage(AS_SPELL_MAP, AS_SIDE_BYTE_PLAYER);

    aa_tilemap[0] = 'M';
    aa_tilemap[1] = 'P';
    aa_tilemap[2] = 'L';
    aa_tilemap[3] = 0;

    aa_item(AI_AREA_ITEM)->use_effect = AI_DEAD_USE_EFFECT;
    aa_item(AI_AREA_ITEM)->use_distance = AI_AREA_USE_DISTANCE;
    aa_item(AI_AREA_ITEM)->use_target = AI_SIDE_BYTE_PLAYER;
    aa_item(AI_AREA_ITEM)->use_radius = AI_AREA_RADIUS;

    aa_item(AI_LINE_ITEM)->use_effect = AI_DEAD_USE_EFFECT;
    aa_item(AI_LINE_ITEM)->use_distance = AI_LINE_USE_DISTANCE;
    aa_item(AI_LINE_ITEM)->use_target = AI_SIDE_BYTE_PLAYER;
    aa_item(AI_LINE_ITEM)->use_radius = AI_LINE_RADIUS;

    aa_place(AA_ACTOR, AI_ACTOR_X, AI_ROW_Y, AA_ENEMY_SIDE,
             AA_FIRST_ENEMY_PORTRAIT, AA_ACTOR_LEVEL);
    aa_place(AA_BYSTANDER, AI_ENEMY_TARGET_X, AI_ROW_Y, AA_ENEMY_SIDE,
             AA_TARGET_PORTRAIT, AA_LEVEL_CAP);
    aa_place(AA_TARGET, AI_PLAYER_TARGET_X, AI_ROW_Y, AA_PLAYER_SIDE,
             AA_TARGET_PORTRAIT, AA_LEVEL_CAP);
    aa_place(AS_ALLY, AI_WEST_TARGET_X, AI_ROW_Y, AA_ENEMY_SIDE,
             AA_TARGET_PORTRAIT, AA_LEVEL_CAP);

    /* aa_place leaves entry 0 holding the equipped weapon, so neither of these
       overwrites it. */
    aa_units[AA_ACTOR].inventory_slots[AI_AREA_SLOT * 2 + 1] =
        (unsigned char) AI_AREA_ITEM;
    aa_units[AA_ACTOR].inventory_slots[AI_LINE_SLOT * 2 + 1] =
        (unsigned char) AI_LINE_ITEM;

    data_fdps_map_ai_best_item_bag_slot = bag_slot;
    data_fdps_map_ai_best_item_target_x = AI_AIM_EAST_X;
    data_fdps_battle_ai_best_item_target_y = AI_ROW_Y;

    data_fdps_map_cursor_draw_mode = AA_DRAW_MODE_SENTINEL;
    data_fdps_marked_tile_blend_phase = AI_BLEND_SENTINEL;
    data_fdps_battle_pending_xp_credit = AS_STALE_CREDIT;
}

/* One whole item use, played in mode 13h with the tick moving under it. */
static int ai_run(int side_select)
{
    int used;

    aa_set_mode(AA_MODE_320X200X256);
    aa_saved_timer = _dos_getvect(AA_TIMER_VECTOR);
    _dos_setvect(AA_TIMER_VECTOR, aa_timer_isr);

    used = fdps_map_actor_use_item(AA_ACTOR, side_select);

    _dos_setvect(AA_TIMER_VECTOR, aa_saved_timer);
    aa_set_mode(AA_MODE_TEXT);
    return used;
}

/* Every offset this action reads a record at.  The bag entry's id byte is
   0x0b + 2 * slot, which is inventory_slots[2 * slot + 1] only while that
   array starts at 0x0a. */
static void ai_the_record_layouts_this_action_reads(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots), 0x0a);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_effect), 0x0d);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_distance), 0x10);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_target), 0x11);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, use_radius), 0x12);
}

/* An area item -- use_distance below 0x10 -- collects with the range collector
   around the tile the search named, walks the cursor there carrying the
   radius-plus-two diamond and stops.  The two search coordinates are left
   exactly as the search wrote them, the highlight ramp is not touched at all,
   the diamond is still up when the routine returns, and the movement grid is
   back at its sentinel because the reset is made after the collect.  The
   answer is 0, which is the answer on every path. */
static void ai_an_area_item_leaves_the_diamond_on_the_aim_tile(void)
{
    int used;

    ai_stage(AI_AREA_SLOT);
    data_fdps_map_ai_best_item_target_x = AI_AREA_AIM_X;
    used = ai_run(AS_ENEMY_PHASE);

    CHECK_EQ(used, 0);
    CHECK_EQ(data_fdps_map_cursor_world_x, AI_AREA_AIM_X * AA_TILE_PX);
    CHECK_EQ(data_fdps_map_cursor_world_y, AI_ROW_Y * AA_TILE_PX);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_AREA_MODE);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, AI_AREA_AIM_X);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, AI_ROW_Y);
    CHECK_EQ(data_fdps_marked_tile_blend_phase, AI_BLEND_SENTINEL);
    CHECK_EQ(as_untouched_grid_cells(), AA_CELLS);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    as_unstage();
}

/* A line item -- use_distance from 0x10 up -- collects along the beam instead,
   turns the actor to face the first unit it found, sweeps the cursor out to
   the beam's far end and then brings it back onto that unit.  The far end is
   the actor's tile plus the beam's length times the step toward the aim tile,
   which is one tile PAST the staged map's own width and is left there; the
   ramp is put back to its fixed phase; the overlay is cleared to 0 rather than
   left on the diamond; and the grid is back at its sentinel, wiped a second
   time after the mode-6 sweep marked every cell the cursor crossed. */
static void ai_a_line_item_sweeps_the_beam_and_returns_to_the_first_target(void)
{
    int used;

    ai_stage(AI_LINE_SLOT);
    used = ai_run(AS_ENEMY_PHASE);

    CHECK_EQ(used, 0);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, AI_SWEEP_X);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, AI_ROW_Y);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 0);
    CHECK_EQ(data_fdps_marked_tile_blend_phase, AI_TRAIL_PHASE);
    CHECK_EQ(data_fdps_map_cursor_world_x, AI_ENEMY_TARGET_X * AA_TILE_PX);
    CHECK_EQ(data_fdps_map_cursor_world_y, AI_ROW_Y * AA_TILE_PX);
    CHECK_EQ((int) aa_units[AA_ACTOR].facing, AI_FACING_RIGHT);
    CHECK_EQ(as_untouched_grid_cells(), AA_CELLS);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    as_unstage();
}

/* The bag slot the search published names the entry at record +0xb + 2 * slot.
   Entry 2 holds the area record and entry 3 the line record, and the two arms
   are told apart by what they leave behind: the area arm leaves the diamond up
   and never touches the ramp, the line arm clears the overlay and parks the
   ramp on its fixed phase.  A body that read the entry without the doubling,
   or read the first byte of the pair instead of the second, would find neither
   record. */
static void ai_the_bag_slot_names_the_entry_two_bytes_apart(void)
{
    ai_stage(AI_AREA_SLOT);
    ai_run(AS_ENEMY_PHASE);

    CHECK_EQ(data_fdps_map_cursor_draw_mode, AI_AREA_MODE);
    CHECK_EQ(data_fdps_marked_tile_blend_phase, AI_BLEND_SENTINEL);

    ai_stage(AI_LINE_SLOT);
    ai_run(AS_ENEMY_PHASE);

    CHECK_EQ(data_fdps_map_cursor_draw_mode, 0);
    CHECK_EQ(data_fdps_marked_tile_blend_phase, AI_TRAIL_PHASE);
    as_unstage();
}

/* The enemy phase inverts the record's authored target side and the NPC phase
   hands it through.  With an authored 0 the enemy-side actor's beam keeps side
   0 and stops on the enemy standing three tiles east; the NPC phase turns the
   same record into the filter that keeps every non-zero side, so the beam runs
   past that unit and stops on the player one behind it.  Where the beam
   stopped is where the cursor is left, because targets[0] is what the line arm
   walks back to.  A body that handed the byte through on both arms would have
   aimed the enemy phase at the player's ranks. */
static void ai_the_enemy_phase_inverts_the_authored_target_side(void)
{
    ai_stage(AI_LINE_SLOT);
    ai_run(AS_ENEMY_PHASE);

    CHECK_EQ(data_fdps_map_cursor_world_x, AI_ENEMY_TARGET_X * AA_TILE_PX);

    ai_stage(AI_LINE_SLOT);
    ai_run(AS_NPC_PHASE);

    CHECK_EQ(data_fdps_map_cursor_world_x, AI_PLAYER_TARGET_X * AA_TILE_PX);
    as_unstage();
}

/* The two lower clamps are the only ones that ever fire, and they fire at 0.
   A beam aimed west from the actor's tile extrapolates to -4 and comes back 0;
   one aimed north extrapolates the y to -4 and comes back 0 while the x, whose
   difference is zero, stays on the actor's own column.  The upper arm is what
   the case above shows never firing: it would have to reach 0x504d in x or
   0x004c in y, which is what the .MPL magic reads as. */
static void ai_the_sweep_is_clamped_at_zero(void)
{
    ai_stage(AI_LINE_SLOT);
    data_fdps_map_ai_best_item_target_x = AI_WEST_TARGET_X;
    ai_run(AS_ENEMY_PHASE);

    CHECK_EQ(data_fdps_map_ai_best_item_target_x, 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, AI_ROW_Y);
    CHECK_EQ(data_fdps_map_cursor_world_x, AI_WEST_TARGET_X * AA_TILE_PX);
    CHECK_EQ(data_fdps_map_cursor_world_y, AI_ROW_Y * AA_TILE_PX);

    ai_stage(AI_LINE_SLOT);
    aa_place(AS_ALLY, AI_NORTH_TARGET_X, AI_NORTH_TARGET_Y, AA_ENEMY_SIDE,
             AA_TARGET_PORTRAIT, AA_LEVEL_CAP);
    data_fdps_map_ai_best_item_target_x = AI_ACTOR_X;
    data_fdps_battle_ai_best_item_target_y = AI_NORTH_TARGET_Y;
    ai_run(AS_ENEMY_PHASE);

    CHECK_EQ(data_fdps_battle_ai_best_item_target_y, 0);
    CHECK_EQ(data_fdps_map_ai_best_item_target_x, AI_ACTOR_X);
    CHECK_EQ(data_fdps_map_cursor_world_x, AI_NORTH_TARGET_X * AA_TILE_PX);
    CHECK_EQ(data_fdps_map_cursor_world_y, AI_NORTH_TARGET_Y * AA_TILE_PX);
    as_unstage();
}

/* What the clamp's two bounds are, read the way the action reads them: the
   signed words at +0 and +2 of layer 0's blob.  They are the .MPL magic and
   not the map, which is 5 by 5 in this fixture and 0x504d by 0x004c as far as
   the clamp is concerned. */
static void ai_the_clamp_bounds_come_from_the_magic(void)
{
    ai_stage(AI_LINE_SLOT);

    CHECK_EQ((int) *(short *) data_fdps_scene_layer_tile_map_ptrs[0],
             AI_MPL_BOUND_X);
    CHECK_EQ((int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0] + 2),
             AI_MPL_BOUND_Y);
    CHECK_EQ((int) *(short *) (data_fdps_battle_move_grid_ptr), AA_MAP_W);
    CHECK_EQ((int) *(short *) (data_fdps_battle_move_grid_ptr + 2), AA_MAP_H);
    as_unstage();
}

void run_aiact_tests(void)
{
    RUN_TEST(aa_the_record_layouts_this_action_reads);
    RUN_TEST(aa_returns_one_and_leaves_the_box_cursor);
    RUN_TEST(aa_walks_the_actor_to_the_tile_the_search_named);
    RUN_TEST(aa_fights_the_unit_the_search_named);
    RUN_TEST(aa_an_armed_defender_strikes_back);
    RUN_TEST(aa_an_unarmed_defender_never_strikes_back);
    RUN_TEST(aa_a_dead_defender_never_strikes_back);
    RUN_TEST(aa_the_credit_is_scaled_by_fifteen_tenths);
    RUN_TEST(aa_the_experience_is_paid_to_the_target);
    RUN_TEST(aa_the_pending_credit_is_cleared_on_entry);
    RUN_TEST(aa_the_animation_flag_picks_the_full_screen_fight);

    RUN_TEST(as_the_score_gate_refuses_below_six);
    RUN_TEST(as_a_cast_returns_one_and_leaves_no_overlay);
    RUN_TEST(as_the_enemy_phase_inverts_the_authored_side);
    RUN_TEST(as_the_npc_phase_uses_the_authored_side);
    RUN_TEST(as_the_blast_is_centred_on_the_tile_the_search_named);
    RUN_TEST(as_the_animation_flag_is_an_equality_with_one);

    RUN_TEST(ai_the_record_layouts_this_action_reads);
    RUN_TEST(ai_an_area_item_leaves_the_diamond_on_the_aim_tile);
    RUN_TEST(ai_a_line_item_sweeps_the_beam_and_returns_to_the_first_target);
    RUN_TEST(ai_the_bag_slot_names_the_entry_two_bytes_apart);
    RUN_TEST(ai_the_enemy_phase_inverts_the_authored_target_side);
    RUN_TEST(ai_the_sweep_is_clamped_at_zero);
    RUN_TEST(ai_the_clamp_bounds_come_from_the_magic);
}
