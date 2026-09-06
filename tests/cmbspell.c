/* tests/cmbspell.c -- cover for src/cmbspell.c.
 *
 * fdps_combat_play_spell_on_targets is a sequencer: almost every step it takes
 * is a call, and the only things a caller can see are the globals it leaves
 * behind, the screen, the DAC and the unit records.  So, like tests/combat.c's
 * cover for the attack exchange, every case here runs the whole function
 * against the real game files -- FMER1.TMP, FMER2.TMP, MER1.TMP, MER2.TMP,
 * MISC.VFS, FIGHT.VFS and BACKGRND.VFS, all staged beside the executable by
 * tests/gamefile.lst -- with the adapter in mode 13h and IRQ0 hooked so the
 * per-frame tick waits end.
 *
 * A FABRICATED STAND-IN WOULD PROVE NOTHING.  Every member name is composed
 * inside the function out of a record byte and the spell id, and a name that
 * misses ends the process inside fdps_vfs_load_entry rather than failing an
 * assertion.  That is what makes the runs below evidence: MISC.VFS ships
 * MB00/ML00/ME00 and no MS00, EB13/EL13/EE13/ES13 and no MB13, and
 * EB23/EL23/EE23 -- so a run that returns at all is a run whose prefix came
 * from the caster's side byte and whose format strings are the ones the
 * assembly holds.
 *
 * THE EXPECTED VALUES.  ML00.SAF's twelve frames carry the hit marker on
 * frames 0, 4 and 8, so a full-power spell 0 drains its target in three steps;
 * MAGIC000.SAF's frame 0 carries a lead-in of 13, which is the frame the
 * opening phase ends on.  The damage figures are the ones tests/spell.c pins
 * on fdps_spell_damage_unit: a power of 10 against a magic-resist complement
 * of 100 rolls 9 and leaves a 100 HP target on 91.  Everything else comes off
 * the assembly cited in src/cmbspell.c.
 *
 * WHAT THE TIMER HANDLER IS FOR BESIDES THE TICK.  Four of the function's
 * effects exist only while it is running -- the fight blend tables, the gauge
 * fill sheet it frees before returning, and the backdrop id the terrain image
 * was named from -- so the handler snapshots them the first tick after a frame
 * has reached the screen.  It also samples the first target's HP word on every
 * tick, which is how the stepped drain is told apart from a value that snapped
 * straight to its final figure.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "vfs.h"
#include "sprite.h"
#include "cmbspell.h"

/* The strides the three accessors multiply by. */
#define UNIT_RECORD_STRIDE 0x50
#define CLASS_RECORD_STRIDE 0x0a
#define SPELL_RECORD_STRIDE 0x07

/* Six unit records are staged so that a walk which strayed past the three the
   runs name would be visible.  The class table is staged out to 0x100 records
   and the spell table to the 0x28 MAGICDAT.DAT actually holds. */
#define STAGE_UNITS 6
#define STAGE_CLASSES 0x100
#define STAGE_SPELLS 0x28

#define CASTER 0
#define TARGET_A 1
#define TARGET_B 2
#define BYSTANDER 3

/* Side 0 is the enemy side, which takes the "E" clip family; 2 is the
   player's, which takes "M". */
#define PLAYER_SIDE 2
#define ENEMY_SIDE 0

/* The portrait ids the Stand and Magic clip names are composed from.  Both
   have a STAND%03d.SAF in FIGHT.VFS and the caster's has a MAGIC%03d.SAF; the
   two differ so that a clip loaded for the wrong unit would be a different
   file. */
#define CASTER_PORTRAIT 0
#define TARGET_PORTRAIT 1

/* MISC.VFS ships MB00/ML00/ME00 and no MS00, so spell 0 is an ordinary
   combat-screen spell with no overlay clip. */
#define SPELL_PLAIN 0x00
/* 鬼動死靈陣.  One below the map-only span, so it must NOT be handed off, and
   the only id with both an overlay clip and the random-ailment arm.  MISC.VFS
   ships EB13/EL13/EE13/ES13 and no M-side pair, so this one has to be cast by
   an enemy-side unit. */
#define SPELL_NECROMANCY 0x0d
/* One above the map-only span, so it must not be handed off either.  MISC.VFS
   ships EB23/EL23/EE23 and no M-side pair. */
#define SPELL_ABOVE_MAP_ONLY 0x17
/* 神行術, the top of the map-only span: handed straight to
   fdps_cast_spell_on_targets, whose haste arm takes bit 7 off every listed
   target's flags byte and does nothing else a record can show. */
#define SPELL_HASTE 0x16

/* rand() % 100 is 0..99, so these two rates are the deterministic ends. */
#define ALWAYS_HITS 100
#define NEVER_HITS 0

/* A magic-resist complement of 100 leaves the spell's power unscaled, and
   fdps_unit_apply_damage takes nine tenths of it: a power of 10 against a 100
   HP target is a roll of 9 and a record left on 91. */
#define NO_RESISTANCE 100
#define SPELL_POWER 10
#define EXPECTED_DAMAGE 9
#define START_HP 100
#define DAMAGED_HP (START_HP - EXPECTED_DAMAGE)

/* ML00.SAF carries the hit marker on frames 0, 4 and 8, so the drain is three
   steps of 100 - 9 * k / 3: 97, then 94, then 91.  Either of the two
   intermediate figures can only come from the write-back-and-re-drain; a body
   that let fdps_spell_damage_unit's own store stand would show 91 from the
   first sample on. */
#define DRAIN_STEP_1 97
#define DRAIN_STEP_2 94

/* The caster's MP and each spell's cost, all different so that a charge made
   for the wrong spell would be visible. */
#define MP_START 40
#define MP_COST_PLAIN 5
#define MP_COST_NECROMANCY 3
#define MP_COST_ABOVE 2
#define MP_COST_HASTE 4

/* The scene layers, in the shape src/maptile.c reads them: a terrain layer
   whose signed 16-bit tile width is at +7 and whose 16-bit tile ids start at
   +0x0b, an attribute table whose 4-byte rows start at +0x11, a movement grid
   of 2-byte cells after a 4-byte header, and an event layer whose own width is
   at +7 and whose cell bytes start at +0x10. */
#define TERRAIN_CELLS_AT 0x0b
#define ATTR_ROWS_AT 0x11
#define EVENT_CELLS_AT 0x10
#define MAP_WIDTH 4
#define MAP_CELLS 16
#define ATTR_ROWS 16
/* Byte +3 of a four-byte attribute row is combat_backdrop_id. */
#define ATTR_BACKDROP_AT 3

/* The caster stands on cell (0,0) and so on attribute row 0; the targets stand
   on rows 1 and 2 with different ids, so the id the global is left holding
   says whose tile was resolved.  BACKGRND.VFS ships BACK00..BACK62, so every
   one of the three decrements names a member that exists. */
#define CASTER_BACKDROP_ID 7
#define TARGET_A_BACKDROP_ID 5
#define TARGET_B_BACKDROP_ID 3
#define CASTER_BACKDROP_AFTER (CASTER_BACKDROP_ID - 1)

#define TIMER_VECTOR 8
#define MODE_TEXT 0x03
#define MODE_320X200X256 0x13
#define VGA_BASE 0x000a0000
#define SCREEN_BYTES (0x140 * 0xc8)

/* What the aperture is filled with before a run.  A probe every 4001 bytes is
   sixteen readings spread over the whole window, so the first frame's blit
   cannot leave all of them looking untouched. */
#define SCREEN_SENTINEL 0x5a
#define PROBE_STRIDE 4001

#define DAC_READ_INDEX 0x3c7
#define DAC_WRITE_INDEX 0x3c8
#define DAC_DATA 0x3c9
#define DAC_ENTRIES 256
/* Outside the 0..60 span both staged palettes are built in, so a DAC entry
   still holding it is one neither upload wrote. */
#define DAC_SENTINEL 63
#define PALETTE_SPAN 61
#define READING_SLOTS 6

/* PUSH 0x4800 at 0001a58d and PUSH 0x1000 at 0001a5c9.  The window compared is
   128 bytes at an offset where the fight file and the map file differ:
   FMER1.TMP and MER1.TMP first disagree at 1028, FMER2.TMP and MER2.TMP at 2. */
#define RAMP_BYTES 0x4800
#define CUBE_BYTES 0x1000
#define RAMP_WINDOW_AT 1024
#define CUBE_WINDOW_AT 0
#define WINDOW_BYTES 128

/* PUSH 0x9c4 at 0001a603, then sprites 4..7 at pitch 0x7d and y = index * 5. */
#define FILL_BYTES 0x9c4
#define FILL_STRIPS 4
#define FILL_FIRST_SPRITE 4
#define FILL_PITCH 0x7d
#define FILL_STRIP_HEIGHT 5
/* The sprite index a body that started the run at 0 would have used. */
#define FILL_WRONG_SPRITE 0
/* How far the decode's cursor reaches, which is NOT the size of the buffer the
   function allocates for it.  FigBar.cel's own header says eight sprites of
   145 by 9, and the fourth strip starts on row 15, so the blit walks to
   15 * 0x7d + 8 * 0x7d + 145 = 3019 -- past the 0x9c4 the function mallocs.
   Whether anything is actually painted out there is the sprite's business:
   the strips the function uses leave the last columns transparent, which is
   why its own heap block survives, but the sprites 0..3 the control below
   decodes do not.  A copy of the decode built here is given the room either
   way, because a static array has no slack to be wrong about. */
#define FILL_SCRATCH_BYTES 0x1000

/* How many per-tick HP readings a run keeps.  The longest run below is a few
   hundred ticks; the samples that matter are all in the hit phase. */
#define HP_SAMPLES 512
#define HP_SAMPLE_UNSET (-1)

/* The three ailment timers 鬼動死靈陣 rolls for, at record offsets 0x25..0x27
   (status_timers[3..5]). */
#define AILMENT_FIRST_SLOT 3
#define AILMENT_SLOTS 3

/* A chapter with no arm in fdps_cycle_scene_palette's dispatch, so the frames
   the handed-off path renders cannot write the DAC behind an upload. */
#define QUIET_CHAPTER 1

/* Every bit of a flags byte set going in, and what the haste arm's 0x7f mask
   has to leave behind. */
#define FLAGS_ALL_SET 0xff
#define FLAGS_AFTER_HASTE 0x7f

/* A byte no shade-ramp entry the two .tmp files hold can be confused with,
   written over the table before a handed-off cast: it has to still be there
   afterwards, because only the combat-screen path reads FMer1.tmp. */
#define RAMP_SENTINEL 0x5a5a5a5aUL
#define RAMP_SENTINEL_AT 7

static unsigned char unit_block[STAGE_UNITS * UNIT_RECORD_STRIDE];
static unsigned char class_block[STAGE_CLASSES * CLASS_RECORD_STRIDE];
static unsigned char spell_block[STAGE_SPELLS * SPELL_RECORD_STRIDE];

static unsigned char stage_tile_map[TERRAIN_CELLS_AT + MAP_CELLS * 2];
static unsigned char stage_attr[ATTR_ROWS_AT + ATTR_ROWS * 4];
static unsigned char stage_grid[4 + MAP_CELLS * 2];
static unsigned char stage_event[EVENT_CELLS_AT + MAP_CELLS];

static struct fdps_palette_entry fight_palette[DAC_ENTRIES];
static struct fdps_palette_entry map_palette[DAC_ENTRIES];

static unsigned char fill_snapshot[FILL_BYTES];
static unsigned char fill_over_zero[FILL_SCRATCH_BYTES];
static unsigned char fill_over_ones[FILL_SCRATCH_BYTES];
static unsigned char ramp_snapshot[WINDOW_BYTES];
static unsigned char cube_snapshot[WINDOW_BYTES];
static unsigned char ramp_at_return[WINDOW_BYTES];
static unsigned char cube_at_return[WINDOW_BYTES];
static unsigned char file_window[WINDOW_BYTES];

/* The member name is handed to fdps_vfs_load_entry, which upper-cases the
   caller's own storage in place, so it cannot be a literal the compiler may
   put somewhere read-only. */
static char gauge_member[] = "FigBar.cel";
static char misc_archive[] = "MISC.VFS";

static unsigned char target_list[4];

static volatile int hp_sample[HP_SAMPLES];
static volatile int hp_sample_count;
static volatile int snapshot_taken;
static int snapshot_backdrop_id;
static int snapshot_dac[READING_SLOTS];
static int return_dac[READING_SLOTS];
static int screen_nonzero;
static int run_returned;

static void (__interrupt __far *saved_timer)();

static struct fdps_unit_record *unit(int unit_index)
{
    return (struct fdps_unit_record *)
        (unit_block + unit_index * UNIT_RECORD_STRIDE);
}

static struct fdps_class_record *class_row(int row_index)
{
    return (struct fdps_class_record *)
        (class_block + row_index * CLASS_RECORD_STRIDE);
}

static struct fdps_spell_effect *spell_rec(int spell_id)
{
    return (struct fdps_spell_effect *)
        (spell_block + spell_id * SPELL_RECORD_STRIDE);
}

static void read_dac(int *into)
{
    outp(DAC_READ_INDEX, 0);
    into[0] = (int) inp(DAC_DATA);
    into[1] = (int) inp(DAC_DATA);
    into[2] = (int) inp(DAC_DATA);
    outp(DAC_READ_INDEX, DAC_ENTRIES - 1);
    into[3] = (int) inp(DAC_DATA);
    into[4] = (int) inp(DAC_DATA);
    into[5] = (int) inp(DAC_DATA);
}

/* The three things that exist only while the presentation is running. */
static void take_snapshot(void)
{
    memmove(fill_snapshot, data_fdps_gauge_fill_sheet_ptr,
            (size_t) FILL_BYTES);
    memmove(ramp_snapshot,
            (unsigned char *) data_fdps_palette_shade_ramp_table
            + RAMP_WINDOW_AT, (size_t) WINDOW_BYTES);
    memmove(cube_snapshot, data_fdps_inverse_palette_cube + CUBE_WINDOW_AT,
            (size_t) WINDOW_BYTES);
    snapshot_backdrop_id = (int) data_fdps_map_tile_combat_backdrop_id;
    read_dac(snapshot_dac);
}

static void __interrupt __far spell_timer_isr(void)
{
    unsigned char *aperture;
    int probe;

    ++data_fdps_timer_tick_counter;

    if (hp_sample_count < HP_SAMPLES) {
        hp_sample[hp_sample_count] = (int) unit(TARGET_A)->hp_current;
        hp_sample_count++;
    }

    if (snapshot_taken == 0) {
        aperture = (unsigned char *) VGA_BASE;
        for (probe = 0; probe < SCREEN_BYTES; probe += PROBE_STRIDE) {
            if (aperture[probe] != SCREEN_SENTINEL) {
                take_snapshot();
                snapshot_taken = 1;
                break;
            }
        }
    }

    _chain_intr(saved_timer);
}

static void set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Publishes every global the presentation and its callees read, and builds one
   caster on side 2 with two full-HP targets beside him.  The two spell records
   the combat-screen runs use are given powers that hit on every draw; the
   handed-off one is given only a cost. */
static void stage(void)
{
    int i;

    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_block[i] = 0;
    }
    for (i = 0; i < (int) sizeof(class_block); i++) {
        class_block[i] = 0;
    }
    for (i = 0; i < (int) sizeof(spell_block); i++) {
        spell_block[i] = 0;
    }

    /* The terrain layer: cell n holds tile id n, so the attribute row a unit
       selects is the number of the cell it stands on. */
    for (i = 0; i < TERRAIN_CELLS_AT; i++) {
        stage_tile_map[i] = 0xaa;
    }
    *(short *) (stage_tile_map + 7) = (short) MAP_WIDTH;
    for (i = 0; i < MAP_CELLS; i++) {
        *(short *) (stage_tile_map + TERRAIN_CELLS_AT + i * 2) = (short) i;
    }
    for (i = 0; i < (int) sizeof(stage_attr); i++) {
        stage_attr[i] = 0;
    }
    for (i = 0; i < (int) sizeof(stage_grid); i++) {
        stage_grid[i] = 0;
    }
    *(short *) stage_grid = (short) MAP_WIDTH;
    for (i = 0; i < (int) sizeof(stage_event); i++) {
        stage_event[i] = 0;
    }
    *(short *) (stage_event + 7) = (short) MAP_WIDTH;

    stage_attr[ATTR_ROWS_AT + 0 * 4 + ATTR_BACKDROP_AT] =
        (unsigned char) CASTER_BACKDROP_ID;
    stage_attr[ATTR_ROWS_AT + 1 * 4 + ATTR_BACKDROP_AT] =
        (unsigned char) TARGET_A_BACKDROP_ID;
    stage_attr[ATTR_ROWS_AT + 2 * 4 + ATTR_BACKDROP_AT] =
        (unsigned char) TARGET_B_BACKDROP_ID;

    data_fdps_map_unit_array_ptr = unit_block;
    data_fdps_class_table_ptr = class_block;
    data_fdps_battle_spell_effect_table_ptr = spell_block;
    data_fdps_scene_layer_tile_map_ptrs[0] = stage_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = stage_attr;
    data_fdps_battle_move_grid_ptr = stage_grid;
    data_fdps_map_cell_event_code_layer_ptr = stage_event;

    unit(CASTER)->pos_x = 0;
    unit(CASTER)->pos_y = 0;
    unit(CASTER)->side = PLAYER_SIDE;
    unit(CASTER)->portrait_id = CASTER_PORTRAIT;
    unit(CASTER)->mp_current = MP_START;
    unit(CASTER)->mp_max = MP_START;
    unit(CASTER)->hp_current = START_HP;
    unit(CASTER)->hp_max = START_HP;

    for (i = TARGET_A; i < STAGE_UNITS; i++) {
        unit(i)->pos_x = (unsigned char) i;
        unit(i)->pos_y = 0;
        unit(i)->side = PLAYER_SIDE;
        unit(i)->portrait_id = TARGET_PORTRAIT;
        unit(i)->hp_current = START_HP;
        unit(i)->hp_max = START_HP;
    }

    /* fdps_spell_damage_unit indexes the class table one row past the class
       code, so class 0 reads row 1. */
    class_row(1)->magic_resist_complement = NO_RESISTANCE;

    spell_rec(SPELL_PLAIN)->power = SPELL_POWER;
    spell_rec(SPELL_PLAIN)->hit_rate = ALWAYS_HITS;
    spell_rec(SPELL_PLAIN)->mp_cost = MP_COST_PLAIN;
    spell_rec(SPELL_NECROMANCY)->power = SPELL_POWER;
    spell_rec(SPELL_NECROMANCY)->hit_rate = ALWAYS_HITS;
    spell_rec(SPELL_NECROMANCY)->mp_cost = MP_COST_NECROMANCY;
    spell_rec(SPELL_ABOVE_MAP_ONLY)->power = SPELL_POWER;
    spell_rec(SPELL_ABOVE_MAP_ONLY)->hit_rate = ALWAYS_HITS;
    spell_rec(SPELL_ABOVE_MAP_ONLY)->mp_cost = MP_COST_ABOVE;
    spell_rec(SPELL_HASTE)->mp_cost = MP_COST_HASTE;

    for (i = 0; i < DAC_ENTRIES; i++) {
        fight_palette[i].red = (unsigned char) (i % PALETTE_SPAN);
        fight_palette[i].green = (unsigned char) ((i + 7) % PALETTE_SPAN);
        fight_palette[i].blue = (unsigned char) ((i + 14) % PALETTE_SPAN);
        map_palette[i].red = (unsigned char) ((i + 21) % PALETTE_SPAN);
        map_palette[i].green = (unsigned char) ((i + 28) % PALETTE_SPAN);
        map_palette[i].blue = (unsigned char) ((i + 35) % PALETTE_SPAN);
    }
    data_fdps_vga_fight_palette_ptr = (unsigned char *) fight_palette;
    data_fdps_vga_main_palette_ptr = (unsigned char *) map_palette;

    /* Both audio gates closed, so the sound ids the shipped clips carry play
       nothing, and eight empty voices for the stop-everything the teardown
       makes. */
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    for (i = 0; i < SFX_SAMPLE_SLOT_COUNT; i++) {
        data_fdps_audio_sample_handle_table[i] = NULL;
    }

    /* An empty scene and an empty popup queue, so the handed-off path renders
       frames that paint nothing and needs no sheet of its own. */
    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_chapter_current_chapter_id = QUIET_CHAPTER;
    data_fdps_indicator_queue_count = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;

    data_fdps_timer_tick_counter = 0;

    target_list[0] = TARGET_A;
    target_list[1] = TARGET_B;
    target_list[2] = TARGET_A;
    target_list[3] = TARGET_B;
}

/* One whole presentation, with everything a case reads afterwards copied out
   before anything else can move it. */
static void run(int spell_id, int target_count)
{
    int i;

    snapshot_taken = 0;
    snapshot_backdrop_id = -1;
    run_returned = 0;
    hp_sample_count = 0;
    for (i = 0; i < HP_SAMPLES; i++) {
        hp_sample[i] = HP_SAMPLE_UNSET;
    }
    memset(fill_snapshot, 0, (size_t) FILL_BYTES);
    memset(ramp_snapshot, 0, (size_t) WINDOW_BYTES);
    memset(cube_snapshot, 0, (size_t) WINDOW_BYTES);
    for (i = 0; i < READING_SLOTS; i++) {
        snapshot_dac[i] = -1;
        return_dac[i] = -1;
    }

    set_mode(MODE_320X200X256);
    memset((void *) VGA_BASE, SCREEN_SENTINEL, (size_t) SCREEN_BYTES);
    for (i = 0; i < DAC_ENTRIES; i++) {
        outp(DAC_WRITE_INDEX, i);
        outp(DAC_DATA, DAC_SENTINEL);
        outp(DAC_DATA, DAC_SENTINEL);
        outp(DAC_DATA, DAC_SENTINEL);
    }

    saved_timer = _dos_getvect(TIMER_VECTOR);
    _dos_setvect(TIMER_VECTOR, spell_timer_isr);
    fdps_combat_play_spell_on_targets(CASTER, spell_id, target_count,
                                      target_list);
    _dos_setvect(TIMER_VECTOR, saved_timer);
    run_returned = 1;

    read_dac(return_dac);
    screen_nonzero = 0;
    for (i = 0; i < SCREEN_BYTES; i++) {
        if (((unsigned char *) VGA_BASE)[i] != 0) {
            screen_nonzero++;
        }
    }
    set_mode(MODE_TEXT);

    memmove(ramp_at_return,
            (unsigned char *) data_fdps_palette_shade_ramp_table
            + RAMP_WINDOW_AT, (size_t) WINDOW_BYTES);
    memmove(cube_at_return, data_fdps_inverse_palette_cube + CUBE_WINDOW_AT,
            (size_t) WINDOW_BYTES);
}

static void read_file_window(char *name, long at, unsigned char *into)
{
    FILE *fp;

    memset(into, 0, (size_t) WINDOW_BYTES);
    fp = fopen(name, "rb");
    if (fp != NULL) {
        fseek(fp, at, SEEK_SET);
        fread(into, 1, (size_t) WINDOW_BYTES, fp);
        fclose(fp);
    }
}

/* The four strips as this file's own reading of the assembly composes them,
   over a surface pre-filled with `fill` so that two runs with different fills
   say which bytes the decode actually wrote. */
static void build_fill_sheet(int first_sprite, unsigned char fill,
                             unsigned char *into)
{
    unsigned char *sheet;
    int strip;

    memset(into, fill, (size_t) FILL_SCRATCH_BYTES);
    sheet = (unsigned char *) fdps_vfs_load_entry(misc_archive, gauge_member);
    for (strip = 0; strip < FILL_STRIPS; strip++) {
        fdps_cel_blit_sprite(sheet, strip + first_sprite, into, FILL_PITCH, 0,
                             strip * FILL_STRIP_HEIGHT, 0, 0);
    }
    free(sheet);
}

/* How many of the bytes the decode really wrote -- the ones both fills agree
   on -- the snapshot disagrees with. */
static int fill_mismatches(void)
{
    int offset;
    int wrong;

    wrong = 0;
    for (offset = 0; offset < FILL_BYTES; offset++) {
        if (fill_over_zero[offset] == fill_over_ones[offset]
            && fill_snapshot[offset] != fill_over_zero[offset]) {
            wrong++;
        }
    }
    return wrong;
}

static int fill_written_bytes(void)
{
    int offset;
    int written;

    written = 0;
    for (offset = 0; offset < FILL_BYTES; offset++) {
        if (fill_over_zero[offset] == fill_over_ones[offset]) {
            written++;
        }
    }
    return written;
}

static int window_differs(unsigned char *a, unsigned char *b)
{
    int offset;

    for (offset = 0; offset < WINDOW_BYTES; offset++) {
        if (a[offset] != b[offset]) {
            return 1;
        }
    }
    return 0;
}

/* How many of the samples the handler took hold `value`. */
static int hp_samples_holding(int value)
{
    int index;
    int seen;

    seen = 0;
    for (index = 0; index < hp_sample_count; index++) {
        if (hp_sample[index] == value) {
            seen++;
        }
    }
    return seen;
}

/* The layouts every offset below is read at.  A record whose HP word moved
   would send the write-back over a different field. */
static void the_record_layouts_the_presenter_reads(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), UNIT_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 0x01);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 0x05);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 0x06);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 0x07);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_current), 0x44);
    CHECK_EQ((int) sizeof(struct fdps_spell_effect), SPELL_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_spell_effect, mp_cost), 0x05);
}

/* ---- the standard two-target cast ---------------------------------------
 *
 * Spell 0 from a side-2 caster over targets 1 and 2, both of whom the roll
 * lands on.  Five cases read this one run, so it is played once and its
 * results are held.
 */
static int baseline_done;
static int baseline_returned;
static int baseline_hp_a;
static int baseline_hp_b;
static int baseline_hp_bystander;
static int baseline_mp;
static int baseline_ailments;
static int baseline_step_1_seen;
static int baseline_step_2_seen;
static int baseline_final_seen;

static void baseline(void)
{
    int slot;

    if (baseline_done != 0) {
        return;
    }

    stage();
    run(SPELL_PLAIN, 2);

    baseline_returned = run_returned;
    baseline_hp_a = (int) unit(TARGET_A)->hp_current;
    baseline_hp_b = (int) unit(TARGET_B)->hp_current;
    baseline_hp_bystander = (int) unit(BYSTANDER)->hp_current;
    baseline_mp = (int) unit(CASTER)->mp_current;
    baseline_step_1_seen = hp_samples_holding(DRAIN_STEP_1);
    baseline_step_2_seen = hp_samples_holding(DRAIN_STEP_2);
    baseline_final_seen = hp_samples_holding(DAMAGED_HP);

    baseline_ailments = 0;
    for (slot = 0; slot < AILMENT_SLOTS; slot++) {
        if (unit(TARGET_A)->status_timers[AILMENT_FIRST_SLOT + slot] != 0) {
            baseline_ailments++;
        }
    }

    build_fill_sheet(FILL_FIRST_SPRITE, 0, fill_over_zero);
    build_fill_sheet(FILL_FIRST_SPRITE, 0xff, fill_over_ones);

    baseline_done = 1;
}

/* Every listed target ends on what the roll took off it, and a unit that was
   not listed is untouched. */
static void every_listed_target_is_left_on_the_rolled_figure(void)
{
    baseline();

    CHECK_EQ(baseline_returned, 1);
    CHECK_EQ(baseline_hp_a, DAMAGED_HP);
    CHECK_EQ(baseline_hp_b, DAMAGED_HP);
    CHECK_EQ(baseline_hp_bystander, START_HP);
}

/* The one the rebuild note is about.  fdps_spell_damage_unit has already put
   91 in the record when it returns; the pre-damage 100 goes back over it and
   is stepped down again across the three hit frames of ML00.SAF, so 97 and 94
   are both on the record at some point during the run.  A body that let the
   callee's store stand would show 91 and nothing between. */
static void the_hp_is_drained_across_the_hit_frames_not_snapped(void)
{
    baseline();

    CHECK_EQ(baseline_step_1_seen > 0, 1);
    CHECK_EQ(baseline_step_2_seen > 0, 1);
    CHECK_EQ(baseline_final_seen > 0, 1);
}

/* The MP is charged once, by fdps_spell_deduct_mp_cost between the build-up
   phase and the hit phase, whatever the target count is. */
static void the_cast_charges_the_casters_mp_once(void)
{
    baseline();

    CHECK_EQ(baseline_mp, MP_START - MP_COST_PLAIN);
}

/* Only 鬼動死靈陣 lands ailments, so an ordinary spell leaves all three
   timers clear however many hits it puts in. */
static void an_ordinary_spell_lands_no_ailment(void)
{
    baseline();

    CHECK_EQ(baseline_ailments, 0);
}

/* The terrain image is named from the CASTER's tile, and the id is decremented
   in place before the name is composed: attribute row 0 carries 7, so the
   global is left holding 6 and Back06.saf is what was loaded.  A body that
   resolved a target's tile would leave 4 or 2 behind. */
static void the_backdrop_id_is_the_casters_tile_less_one(void)
{
    baseline();

    CHECK_EQ(snapshot_backdrop_id, CASTER_BACKDROP_AFTER);
    CHECK_EQ((int) data_fdps_map_tile_combat_backdrop_id,
             CASTER_BACKDROP_AFTER);
}

/* FMer1.tmp and FMer2.tmp are over the two globals while the animation runs,
   and Mer1.tmp and Mer2.tmp are back over them when it returns. */
static void the_fight_blend_tables_replace_the_map_ones_and_go_back(void)
{
    baseline();

    read_file_window("FMER1.TMP", (long) RAMP_WINDOW_AT, file_window);
    CHECK_EQ(window_differs(ramp_snapshot, file_window), 0);
    read_file_window("FMER2.TMP", (long) CUBE_WINDOW_AT, file_window);
    CHECK_EQ(window_differs(cube_snapshot, file_window), 0);

    read_file_window("MER1.TMP", (long) RAMP_WINDOW_AT, file_window);
    CHECK_EQ(window_differs(ramp_at_return, file_window), 0);
    read_file_window("MER2.TMP", (long) CUBE_WINDOW_AT, file_window);
    CHECK_EQ(window_differs(cube_at_return, file_window), 0);

    /* And the two really are different files, so the checks above are not
       both satisfied by one table. */
    read_file_window("FMER1.TMP", (long) RAMP_WINDOW_AT, file_window);
    CHECK_EQ(window_differs(ramp_at_return, file_window), 1);
}

/* The gauge fill strip is FigBar.cel's sprites 4, 5, 6 and 7 decoded into a
   0x9c4-byte buffer at pitch 0x7d, five rows apart.  The comparison is made
   only over the bytes the decode actually writes, which two builds over
   different fills agree on. */
static void the_gauge_fill_sheet_is_figbars_last_four_sprites(void)
{
    baseline();

    CHECK_EQ(fill_written_bytes() > 0, 1);
    CHECK_EQ(fill_mismatches(), 0);

    /* Sprites 0..3 are a different picture, so a body that started at 0 would
       not match. */
    build_fill_sheet(FILL_WRONG_SPRITE, 0, fill_over_zero);
    build_fill_sheet(FILL_WRONG_SPRITE, 0xff, fill_over_ones);
    CHECK_EQ(fill_mismatches() > 0, 1);
    build_fill_sheet(FILL_FIRST_SPRITE, 0, fill_over_zero);
    build_fill_sheet(FILL_FIRST_SPRITE, 0xff, fill_over_ones);
}

/* The teardown clears all 64000 bytes of the aperture and uploads the map
   palette, while the fight palette is what was on the DAC during the run. */
static void the_screen_is_cleared_and_the_map_palette_uploaded(void)
{
    baseline();

    CHECK_EQ(screen_nonzero, 0);

    CHECK_EQ(snapshot_dac[0], (int) fight_palette[0].red);
    CHECK_EQ(snapshot_dac[3], (int) fight_palette[DAC_ENTRIES - 1].red);
    CHECK_EQ(return_dac[0], (int) map_palette[0].red);
    CHECK_EQ(return_dac[1], (int) map_palette[0].green);
    CHECK_EQ(return_dac[2], (int) map_palette[0].blue);
    CHECK_EQ(return_dac[3], (int) map_palette[DAC_ENTRIES - 1].red);
}

/* A roll that never lands leaves the record exactly where it was: no
   write-back to undo, no drain step and no knockback.  The samples say the
   record was never anything but 100 for the whole run. */
static void a_spell_that_misses_leaves_the_target_alone(void)
{
    stage();
    spell_rec(SPELL_PLAIN)->hit_rate = NEVER_HITS;
    run(SPELL_PLAIN, 1);

    CHECK_EQ(run_returned, 1);
    CHECK_EQ(unit(TARGET_A)->hp_current, START_HP);
    CHECK_EQ(hp_samples_holding(START_HP), hp_sample_count);
    CHECK_EQ(hp_sample_count > 0, 1);
    /* The MP is spent whether the roll lands or not. */
    CHECK_EQ(unit(CASTER)->mp_current, MP_START - MP_COST_PLAIN);
}

/* 鬼動死靈陣 is one below the map-only span, so it is played here and not
   handed off, and it is cast by a side-0 unit, so every clip name it composes
   leads with "E".  MISC.VFS ships EB13, EL13, EE13 and ES13 and no M-side
   pair at all: a prefix taken from the wrong side, or a span whose lower
   bound had swallowed 0x0d, would end the process inside fdps_vfs_load_entry
   instead of reaching the assertions. */
static void the_id_below_the_map_only_span_is_played_here(void)
{
    stage();
    unit(CASTER)->side = ENEMY_SIDE;
    run(SPELL_NECROMANCY, 1);

    CHECK_EQ(run_returned, 1);
    CHECK_EQ(unit(TARGET_A)->hp_current, DAMAGED_HP);
    CHECK_EQ(unit(CASTER)->mp_current, MP_START - MP_COST_NECROMANCY);
    CHECK_EQ((int) data_fdps_map_tile_combat_backdrop_id,
             CASTER_BACKDROP_AFTER);
}

/* And the id one above the span, for the other end of the same test.  EL23
   carries eight hit frames rather than three, so the drain here is eight
   steps to the same figure. */
static void the_id_above_the_map_only_span_is_played_here(void)
{
    stage();
    unit(CASTER)->side = ENEMY_SIDE;
    run(SPELL_ABOVE_MAP_ONLY, 1);

    CHECK_EQ(run_returned, 1);
    CHECK_EQ(unit(TARGET_A)->hp_current, DAMAGED_HP);
    CHECK_EQ(unit(CASTER)->mp_current, MP_START - MP_COST_ABOVE);
    CHECK_EQ(hp_samples_holding(START_HP) > 0, 1);
    CHECK_EQ(hp_samples_holding(DAMAGED_HP) > 0, 1);
}

/* 神行術 sits at the top of the map-only span and is handed straight on with
   all four arguments: the arm fdps_cast_spell_on_targets picks for it takes
   bit 7 off exactly the listed targets' flags bytes, which nothing on the
   combat-screen path does.  And the fight blend tables are never read, so the
   sentinel written over the shade ramp is still there afterwards. */
static void a_map_only_id_is_handed_to_the_map_presenter(void)
{
    stage();
    unit(TARGET_A)->flags = FLAGS_ALL_SET;
    unit(TARGET_B)->flags = FLAGS_ALL_SET;
    unit(BYSTANDER)->flags = FLAGS_ALL_SET;
    data_fdps_palette_shade_ramp_table[RAMP_SENTINEL_AT] = RAMP_SENTINEL;
    data_fdps_gauge_fill_sheet_ptr = NULL;
    data_fdps_combat_gauge_sprite_sheet_ptr = NULL;

    run(SPELL_HASTE, 2);

    CHECK_EQ(run_returned, 1);
    CHECK_EQ(unit(TARGET_A)->flags, FLAGS_AFTER_HASTE);
    CHECK_EQ(unit(TARGET_B)->flags, FLAGS_AFTER_HASTE);
    CHECK_EQ(unit(BYSTANDER)->flags, FLAGS_ALL_SET);
    CHECK_EQ(unit(CASTER)->mp_current, MP_START - MP_COST_HASTE);

    /* Nothing of the combat-screen setup happened. */
    CHECK_EQ(data_fdps_palette_shade_ramp_table[RAMP_SENTINEL_AT],
             RAMP_SENTINEL);
    CHECK_EQ(data_fdps_gauge_fill_sheet_ptr == NULL, 1);
    CHECK_EQ(data_fdps_combat_gauge_sprite_sheet_ptr == NULL, 1);
    /* And no HP was touched: the haste arm has no damage roll in it, so every
       reading the handler took is the figure the record started on. */
    CHECK_EQ(hp_sample_count > 0, 1);
    CHECK_EQ(hp_samples_holding(START_HP), hp_sample_count);
}

void run_cmbspell_tests(void)
{
    RUN_TEST(the_record_layouts_the_presenter_reads);
    RUN_TEST(every_listed_target_is_left_on_the_rolled_figure);
    RUN_TEST(the_hp_is_drained_across_the_hit_frames_not_snapped);
    RUN_TEST(the_cast_charges_the_casters_mp_once);
    RUN_TEST(an_ordinary_spell_lands_no_ailment);
    RUN_TEST(the_backdrop_id_is_the_casters_tile_less_one);
    RUN_TEST(the_fight_blend_tables_replace_the_map_ones_and_go_back);
    RUN_TEST(the_gauge_fill_sheet_is_figbars_last_four_sprites);
    RUN_TEST(the_screen_is_cleared_and_the_map_palette_uploaded);
    RUN_TEST(a_spell_that_misses_leaves_the_target_alone);
    RUN_TEST(the_id_below_the_map_only_span_is_played_here);
    RUN_TEST(the_id_above_the_map_only_span_is_played_here);
    RUN_TEST(a_map_only_id_is_handed_to_the_map_presenter);
}
