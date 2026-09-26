/* cmbspell.c -- the full-screen combat presentation of a spell: the fight
 * screen the game brings up when a spell is cast with the battle animation on,
 * and the drain of every target's HP gauge across the spell's own hit frames.
 *
 * See cmbspell.h for what a caller has to know.  The file owns no state: it
 * borrows the two blend tables and the two combat gauge sheets src/gamedata.h
 * declares for the length of one spell and puts them back.
 *
 * fopen, fread, fclose, sprintf and printf come from <stdio.h> and are the
 * calls at 0004265e, 0004270d, 000428be, 00042d41 and 00042deb; malloc, free
 * and exit come from <stdlib.h> and are the calls at 0003d375, 0003d478 and
 * 00042e0f; memset comes from <string.h> and is the call at 00042cd0; inp
 * comes from <conio.h> and is the library routine at 0003d4e4 rather than the
 * IN instruction an intrinsic would have produced.  None of them is expanded
 * inline, because the flag set carries no -oi (rebuild_info/build_flags.md).
 */
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "unitstat.h"
#include "maptile.h"
#include "blit.h"
#include "gauge.h"
#include "saf.h"
#include "sprite.h"
#include "vfs.h"
#include "palette.h"
#include "audio.h"
#include "spell.h"
#include "combat.h"
#include "cmbspell.h"

/* The offscreen page every frame is composed on: 368 by 248 with 24 pixels of
   margin on all four sides, of which only the 320x200 window at +0x2298 is
   put on the adapter.  The margin is what lets a knocked-back target hang off
   the edge of the picture instead of wrapping onto the next row. */
#define COMBAT_SURFACE_PITCH 0x170
#define COMBAT_SURFACE_ROWS 0xf8
#define COMBAT_SURFACE_BYTES 0x16480
#define COMBAT_SURFACE_MARGIN 0x18
#define COMBAT_VISIBLE_ORIGIN_OFFSET 0x2298

#define SCREEN_WIDTH 0x140
#define SCREEN_HEIGHT 0xc8
#define VGA_SCREEN_BASE 0x000a0000

/* Input status register 1 and its vertical retrace bit. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The two blend tables, read straight off disk over the globals gamedata.h
   owns: PUSH 0x4800 at 0001a58d for the shade ramp and PUSH 0x1000 at
   0001a5c9 for the inverse-palette cube, each with an element size of 1.  The
   fight pair goes in at the top and the map pair comes back at the bottom. */
#define SHADE_RAMP_BYTES 0x4800
#define PALETTE_CUBE_BYTES 0x1000
#define FIGHT_SHADE_RAMP_FILE "FMer1.tmp"
#define FIGHT_PALETTE_CUBE_FILE "FMer2.tmp"
#define MAP_SHADE_RAMP_FILE "Mer1.tmp"
#define MAP_PALETTE_CUBE_FILE "Mer2.tmp"
#define BLEND_TABLE_MODE "rb"

/* The containers every piece of the presentation is loaded out of, and the
   member names composed into them.  The three effect clips and the fourth
   overlay one take a one-character prefix that says which side cast the
   spell, so the format strings lead with %s. */
#define MISC_ARCHIVE "MISC.VFS"
#define FIGHT_ARCHIVE "Fight.vfs"
#define BACKDROP_ARCHIVE "BackGrnd.vfs"
#define GAUGE_SHEET_MEMBER "FigBar.cel"
#define BUILDUP_CLIP_FORMAT "%sB%02d.saf"
#define MAIN_CLIP_FORMAT "%sL%02d.saf"
#define FINISH_CLIP_FORMAT "%sE%02d.saf"
#define OVERLAY_CLIP_FORMAT "%sS%02d.saf"
#define STAND_CLIP_FORMAT "Stand%03d.saf"
#define MAGIC_CLIP_FORMAT "Magic%03d.saf"
#define BACKDROP_CLIP_FORMAT "Back%02d.saf"

/* The buffer every member name is composed in, and the one the prefix itself
   is held in.  The prefix is passed to sprintf as a %s and so has to be a
   NUL-terminated string rather than a bare character. */
#define CLIP_NAME_BYTES 16
#define CLIP_PREFIX_BYTES 4

/* Side 0 is the enemy side.  Its clips are the E family and its knockback
   throws the target to the right; the NPC and player sides take the M family
   and throw to the left, so the target always travels away from the caster. */
#define ENEMY_SIDE 0
#define ENEMY_CLIP_PREFIX 'E'
#define ALLY_CLIP_PREFIX 'M'
#define KNOCKBACK_SIGN_AWAY_FROM_ENEMY (-1)
#define KNOCKBACK_SIGN_AWAY_FROM_ALLY 1

/* The 125x20 gauge strip, decoded out of FigBar.cel's sprites 4..7 five rows
   apart, exactly as fdps_combat_play_attack_exchange builds it. */
#define GAUGE_FILL_SHEET_BYTES 0x9c4
#define GAUGE_FILL_STRIPS 4
#define GAUGE_FIRST_FILL_SPRITE 4
#define GAUGE_FILL_STRIP_PITCH 0x7d
#define GAUGE_FILL_STRIP_HEIGHT 5

#define LAST_DAC_ENTRY 0xff

/* fdps_saf_advance_tick's mode byte (src/saf.h): 1 rewinds the cursor, 0 runs
   the clip and restarts it at frame 0 at the end, and any other value -- 2 is
   what the standing clips use here -- leaves it resting on the last frame. */
#define SAF_CURSOR_RESET 1
#define SAF_ADVANCE_LOOPING 0
#define SAF_ADVANCE_HOLD_LAST 2

/* Byte +4 of a .SAF frame record is its lead-in count and byte +5 its hit
   marker (src/saf.h).  Frame 0's lead-in is read out of two clips here: the
   caster's Magic clip, where it is the frame the opening phase ends on, and
   the spell's main clip, where it is the frame the hit phase loops back to. */
#define SAF_FRAME_LEAD_IN_OFFSET 4
#define SAF_FRAME_HIT_MARKER_OFFSET 5

/* The knockback runs from step 5 down to step 0 over six frames, and -1 is
   the resting state that leaves the target on the page's border corner. */
#define KNOCKBACK_STEPS 6
#define KNOCKBACK_FIRST_STEP 5
#define KNOCKBACK_AT_REST (-1)

/* The recolour selector a landed hit paints the target through: blit mode 3
   with a 0x03-prefixed operand carrying the spell's own colour band and a
   phase that cycles 0..7, one step per frame of the hit loop. */
#define RECOLOR_BLIT_MODE 3
#define RECOLOR_SELECTOR_BASE 0x30000
#define RECOLOR_BAND_STRIDE 0x100
#define RECOLOR_PHASES 8

/* The changeover between two targets: sixteen frames over which the finished
   target fades out through the mode-9 descriptor while the next one walks in
   fourteen pixels a frame.  The descriptor's level slot runs 16 - frame, so
   the outgoing sprite is solid on the first frame and gone on the last
   (src/rleblend.h). */
#define TARGET_SWAP_FRAMES 16
#define TARGET_SWAP_PIXELS_PER_FRAME 0xe
#define FADE_BLIT_MODE 9
#define FADE_LEVEL_TOP 0x10

/* The three dwords of a mode-9 blend descriptor (src/rleblend.h): the shade
   ramp base, the blend level and the inverse colour cube base.  They are one
   block here because the address of the first is what is handed to the
   kernel, so they have to be adjacent and in this order. */
#define BLEND_DESCRIPTOR_RAMP 0
#define BLEND_DESCRIPTOR_LEVEL 1
#define BLEND_DESCRIPTOR_CUBE 2
#define BLEND_DESCRIPTOR_DWORDS 3

/* MAGICDAT.DAT's 40 records, which is the range of spell ids and so the length
   of the recolour band table below. */
#define SPELL_TABLE_ENTRIES 0x28

/* 鬼動死靈陣, the one spell that also lands random ailments on a target it
   hit -- CMP dword ptr [EBP+0x18],0xd at 0001ae89. */
#define SPELL_NECROMANCY 0x0d

/* How many targets' standing clips the stack block has room for.  The count
   the caller passes is never checked against it. */
#define TARGET_CLIP_SLOTS 40

/* fdps_audio_stop_sample's "every voice" argument. */
#define SFX_STOP_ALL_SLOTS (-1)

/* The ids that have no combat-screen presentation of their own and are played
   over the battle map instead: the two ground shocks, the three heals, the
   seal, the two ailments, the blessing, the teleport, the haste, the revival
   and the requiem.  The span is tested with signed compares -- CMP dword ptr
   [EBP+0x18],0xe / JL and CMP ...,0x16 / JLE at 0001a517. */
#define SPELL_MAP_ONLY_FIRST 0x0e
#define SPELL_MAP_ONLY_LAST 0x16
#define SPELL_QUAKE 0x0a
#define SPELL_GREAT_QUAKE 0x0b
#define SPELL_REVIVE 0x18
#define SPELL_REQUIEM 0x21

/* The ids MISC.VFS ships a fourth %sS%02d.saf overlay clip for, as three spans
   and one singleton -- the compare chain at 0001a747.  0x21 sits in the second
   span but can never reach it, having been handed off at the top. */
#define SPELL_OVERLAY_SPAN1_FIRST 0x1a
#define SPELL_OVERLAY_SPAN1_LAST 0x1f
#define SPELL_OVERLAY_SPAN2_FIRST 0x21
#define SPELL_OVERLAY_SPAN2_LAST 0x24
#define SPELL_OVERLAY_SPAN3_FIRST 0x26
#define SPELL_OVERLAY_SPAN3_LAST 0x27

/* 0001a4c0.  The whole presentation, written out the way the assembly plays
   it: five phases of frames, each one composed the same way -- clear the page,
   draw the backdrop, the overlay where the phase uses it, the target, the
   caster and the effect, paint both gauge panels, advance the cursors, wait
   for the retrace to begin and end, put the window on the adapter, and then
   wait for the timer tick to change.

   THE PACE LATCH IS DELIBERATELY NOT INITIALISED.  Every frame ends by
   spinning until data_fdps_timer_tick_counter differs from `last_tick` and
   then re-latching it, and `last_tick` is never seeded, so the very first
   frame's wait ends against whatever the stack held
   (rebuild_info/pitfalls.md).  Seeding it adds a tick to every spell in the
   game.

   THE PER-TARGET HP IS WRITTEN BACK AND RE-DRAINED.  fdps_spell_damage_unit
   has already taken the damage off the record when it returns, so the
   pre-damage figure is put back over it and stepped down again one hit frame
   at a time -- the obvious "damage the target, then play the animation" makes
   the gauge snap to its final value instead of emptying.

   THE REQUEST BLOCK'S LEFTOVERS ARE LOAD-BEARING.  One nine-slot block serves
   every draw in the function and the fields a draw does not write keep what
   the draw before it left there: the page, its pitch and its height are filled
   in once and never touched again, and the fourth and fifth phases inherit the
   x and y of the last hit frame, which is why neither of them sets an origin
   of its own. */
void fdps_combat_play_spell_on_targets(int caster_unit_index, int spell_id,
                                       int target_count,
                                       unsigned char *target_unit_indices)
{
    /* Where the knocked-back target's draw origin walks, in page columns and
       rows away from the border corner, over the six frames a landed hit
       lasts.  The walk runs from the last entry down to the first, so the
       target is thrown out 18 columns and 10 rows and settles back onto its
       feet.  Both are copied out of the initialiser images at 00018c90 and
       00018ca8 by the two REP MOVSD at 0001a4f1 and 0001a503. */
    int knockback_offset_x[KNOCKBACK_STEPS] = { 0, 4, 9, 14, 18, 14 };
    int knockback_offset_y[KNOCKBACK_STEPS] = { 0, 2, 4, 6, 8, 10 };
    /* One palette band per spell id -- the colour a landed hit tints its
       target with -- copied out of the initialiser image at 00018cc0 by the
       REP MOVSD at 0001a515.  Its zero entries are exactly the ids handed off
       at the top, so every id that reaches the hit loop has a band of its
       own. */
    int spell_recolor_band[SPELL_TABLE_ENTRIES] = {
        214, 214, 214, 224, 224,  40,  40,  40, 160, 160,
          0,   0,  72,  32,   0,   0,   0,   0,   0,   0,
          0,   0,   0,  32,   0, 214, 224,  40, 214, 180,
         40, 214,  40,   0,  72, 214, 214, 160, 160,  40
    };
    /* Every target's Stand clip, loaded up front and freed at the end.  The
       caller's target_count is not checked against these 40 slots. */
    void *target_stand_clips[TARGET_CLIP_SLOTS];
    /* The nine-slot draw request block sprite.h describes -- see the note
       above on why one block serves the whole function. */
    int request[DRAW_REQUEST_DWORDS];
    /* The mode-9 descriptor the outgoing target is faded out through.  Its two
       table pointers are filled in once at the top; only the level moves. */
    int fade_descriptor[BLEND_DESCRIPTOR_DWORDS];
    /* The five three-dword .SAF playback cursors (src/saf.h) the frames are
       drawn from, plus a copy of the target's taken when a changeover starts
       so the finished target can go on being drawn while the next one runs. */
    int overlay_cursor[SAF_CURSOR_DWORDS];
    int caster_stand_cursor[SAF_CURSOR_DWORDS];
    int target_cursor[SAF_CURSOR_DWORDS];
    int caster_magic_cursor[SAF_CURSOR_DWORDS];
    int effect_cursor[SAF_CURSOR_DWORDS];
    int outgoing_cursor[SAF_CURSOR_DWORDS];
    /* Where every member name is composed, and the one-character side prefix
       the three effect names lead with. */
    char clip_name[CLIP_NAME_BYTES];
    char clip_prefix[CLIP_PREFIX_BYTES];
    /* The one handle all four blend-table reads pass through, reused. */
    FILE *blend_table_file;
    /* The casting unit, read for its side, its portrait id and its tile. */
    struct fdps_unit_record *caster;
    /* The target the hit loop is working on, held across the roll so the
       pre-damage HP can be written back over it. */
    struct fdps_unit_record *target;
    /* A target's record while its Stand clip is being loaded, read for its
       portrait id and not kept. */
    struct fdps_unit_record *target_for_clip;
    /* A .SAF frame record, read only for one of its two marker bytes. */
    void *saf_frame;
    /* The terrain the spell is played over, entry 0 of a Back%02d.saf. */
    void *backdrop;
    /* The spell's three effect clips, played in this order, and the fourth
       overlay clip the ids that have one draw underneath them.  The overlay
       stays NULL for every other id and that is what the three tests on it
       ask. */
    void *buildup_clip;
    void *main_clip;
    void *finish_clip;
    void *overlay_clip;
    /* The caster's own two clips.  The standing one is played only during the
       entrance slide; the magic one runs from the first frame to the last. */
    void *caster_stand_clip;
    void *caster_magic_clip;
    /* The portrait id both of those are named from. */
    int caster_clip_id;
    /* +1 or -1: which way a struck target is thrown, away from its caster. */
    int knockback_sign;
    /* How much of the knockback is left, 5 down to 0, and -1 while the target
       is standing where it belongs. */
    int knockback_step;
    /* Frame 0's lead-in count in each of the two clips that carry one: the
       frame the hit loop rewinds the main clip to, and the frame of the
       caster's Magic clip that ends the opening phase. */
    int main_clip_lead_in;
    int magic_clip_lead_in;
    /* How many of the main clip's frames carry a hit marker, and how many
       frames it holds in all.  A clip with no hit frame is fatal. */
    int hit_frame_count;
    int main_clip_frames;
    /* How many hit frames of the current target's beating have gone by. */
    int hits_played;
    /* Which of the sixteen changeover frames is being drawn, counting down. */
    int slide_frame;
    /* The target's HP either side of the roll: what it had before the spell
       was applied, and what fdps_spell_damage_unit left it holding. */
    int hp_before;
    int hp_after;
    /* The 0..7 counter the recolour selector's low byte cycles through, one
       step per frame of the hit loop, and the selector built out of it. */
    int recolor_phase;
    int recolor_selector;
    /* fdps_saf_advance_tick's answer for the effect clip: non-zero on the tick
       that steps past its last frame. */
    int clip_finished;
    /* Whether the roll missed this target: 1 when fdps_spell_damage_unit
       answered 0.  A miss draws no knockback and no recolour and leaves the
       HP where it was. */
    char spell_missed;
    /* Which of the four gauge fill strips is being decoded, 0 through 3. */
    int strip;
    /* Which of the main clip's frames the hit scan is looking at. */
    int frame;
    /* Which entry of target_unit_indices is being played out. */
    int i;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    overlay_clip = NULL;
    hit_frame_count = 0;
    recolor_phase = 0;

    if ((spell_id >= SPELL_MAP_ONLY_FIRST && spell_id <= SPELL_MAP_ONLY_LAST)
        || spell_id == SPELL_QUAKE || spell_id == SPELL_GREAT_QUAKE
        || spell_id == SPELL_REVIVE || spell_id == SPELL_REQUIEM) {
        fdps_cast_spell_on_targets(caster_unit_index, spell_id, target_count,
                                   target_unit_indices);
        return;
    }

    fade_descriptor[BLEND_DESCRIPTOR_RAMP] =
        (int) data_fdps_palette_shade_ramp_table;
    fade_descriptor[BLEND_DESCRIPTOR_CUBE] =
        (int) data_fdps_inverse_palette_cube;

    blend_table_file = fopen(FIGHT_SHADE_RAMP_FILE, BLEND_TABLE_MODE);
    fread(data_fdps_palette_shade_ramp_table, 1, (size_t) SHADE_RAMP_BYTES,
          blend_table_file);
    fclose(blend_table_file);
    blend_table_file = fopen(FIGHT_PALETTE_CUBE_FILE, BLEND_TABLE_MODE);
    fread(data_fdps_inverse_palette_cube, 1, (size_t) PALETTE_CUBE_BYTES,
          blend_table_file);
    fclose(blend_table_file);

    data_fdps_combat_gauge_sprite_sheet_ptr = (unsigned char *)
        fdps_vfs_load_entry(MISC_ARCHIVE, GAUGE_SHEET_MEMBER);
    data_fdps_gauge_fill_sheet_ptr =
        (unsigned char *) malloc((size_t) GAUGE_FILL_SHEET_BYTES);
    for (strip = 0; strip < GAUGE_FILL_STRIPS; strip++) {
        fdps_cel_blit_sprite(data_fdps_combat_gauge_sprite_sheet_ptr,
                             strip + GAUGE_FIRST_FILL_SPRITE,
                             data_fdps_gauge_fill_sheet_ptr,
                             GAUGE_FILL_STRIP_PITCH, 0,
                             strip * GAUGE_FILL_STRIP_HEIGHT, 0, 0);
    }

    /* The side byte is read twice in the original, once for each of the two
       things it decides; one test answers both. */
    caster = fdps_get_unit_record(caster_unit_index);
    caster_clip_id = (int) caster->portrait_id;
    if (caster->side == ENEMY_SIDE) {
        knockback_sign = KNOCKBACK_SIGN_AWAY_FROM_ENEMY;
        clip_prefix[0] = ENEMY_CLIP_PREFIX;
    } else {
        knockback_sign = KNOCKBACK_SIGN_AWAY_FROM_ALLY;
        clip_prefix[0] = ALLY_CLIP_PREFIX;
    }
    clip_prefix[1] = '\0';

    sprintf(clip_name, BUILDUP_CLIP_FORMAT, clip_prefix, spell_id);
    buildup_clip = fdps_vfs_load_entry(MISC_ARCHIVE, clip_name);
    sprintf(clip_name, MAIN_CLIP_FORMAT, clip_prefix, spell_id);
    main_clip = fdps_vfs_load_entry(MISC_ARCHIVE, clip_name);
    sprintf(clip_name, FINISH_CLIP_FORMAT, clip_prefix, spell_id);
    finish_clip = fdps_vfs_load_entry(MISC_ARCHIVE, clip_name);

    if ((spell_id >= SPELL_OVERLAY_SPAN1_FIRST
         && spell_id <= SPELL_OVERLAY_SPAN1_LAST)
        || (spell_id >= SPELL_OVERLAY_SPAN2_FIRST
            && spell_id <= SPELL_OVERLAY_SPAN2_LAST)
        || (spell_id >= SPELL_OVERLAY_SPAN3_FIRST
            && spell_id <= SPELL_OVERLAY_SPAN3_LAST)
        || spell_id == SPELL_NECROMANCY) {
        sprintf(clip_name, OVERLAY_CLIP_FORMAT, clip_prefix, spell_id);
        overlay_clip = fdps_vfs_load_entry(MISC_ARCHIVE, clip_name);
        overlay_cursor[SAF_CURSOR_IMAGE] = (int) overlay_clip;
        fdps_saf_advance_tick(overlay_cursor, SAF_CURSOR_RESET);
    }

    sprintf(clip_name, STAND_CLIP_FORMAT, caster_clip_id);
    caster_stand_clip = fdps_vfs_load_entry(FIGHT_ARCHIVE, clip_name);
    caster_stand_cursor[SAF_CURSOR_IMAGE] = (int) caster_stand_clip;
    fdps_saf_advance_tick(caster_stand_cursor, SAF_CURSOR_RESET);

    sprintf(clip_name, MAGIC_CLIP_FORMAT, caster_clip_id);
    caster_magic_clip = fdps_vfs_load_entry(FIGHT_ARCHIVE, clip_name);
    caster_magic_cursor[SAF_CURSOR_IMAGE] = (int) caster_magic_clip;
    fdps_saf_advance_tick(caster_magic_cursor, SAF_CURSOR_RESET);

    /* The terrain is the CASTER's, not any target's, and its id is decremented
       in place: the backdrop the spell is played over is one below the number
       the tile's attribute row carries. */
    fdps_map_load_tile_info((int) caster->pos_x, (int) caster->pos_y);
    if (data_fdps_map_tile_combat_backdrop_id > 0) {
        data_fdps_map_tile_combat_backdrop_id--;
    }
    sprintf(clip_name, BACKDROP_CLIP_FORMAT,
            (int) data_fdps_map_tile_combat_backdrop_id);
    backdrop = fdps_vfs_load_entry(BACKDROP_ARCHIVE, clip_name);

    for (i = 0; i < target_count; i++) {
        target_for_clip =
            fdps_get_unit_record((int) target_unit_indices[i]);
        sprintf(clip_name, STAND_CLIP_FORMAT,
                (int) target_for_clip->portrait_id);
        target_stand_clips[i] = fdps_vfs_load_entry(FIGHT_ARCHIVE, clip_name);
    }

    target_cursor[SAF_CURSOR_IMAGE] = (int) target_stand_clips[0];
    fdps_saf_advance_tick(target_cursor, SAF_CURSOR_RESET);

    fdps_set_palette_range((struct fdps_palette_entry *)
                           data_fdps_vga_fight_palette_ptr,
                           0, LAST_DAC_ENTRY, 0, 0, 0);
    fdps_combat_slide_in_attacker(caster_unit_index,
                                  (int) target_unit_indices[0], 0,
                                  caster_stand_cursor, target_cursor,
                                  backdrop);

    saf_frame = fdps_saf_get_frame(main_clip, 0);
    main_clip_lead_in = (int) *((unsigned char *) saf_frame
                                + SAF_FRAME_LEAD_IN_OFFSET);
    saf_frame = fdps_saf_get_frame(caster_magic_clip, 0);
    magic_clip_lead_in = (int) *((unsigned char *) saf_frame
                                 + SAF_FRAME_LEAD_IN_OFFSET);

    request[DRAW_REQUEST_DEST_BASE] = (int) malloc((size_t)
                                                   COMBAT_SURFACE_BYTES);
    request[DRAW_REQUEST_DEST_PITCH] = COMBAT_SURFACE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = COMBAT_SURFACE_ROWS;
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;
    request[DRAW_REQUEST_X] = COMBAT_SURFACE_MARGIN;
    request[DRAW_REQUEST_Y] = COMBAT_SURFACE_MARGIN;

    /* Phase 1: the caster's Magic clip alone, until its cursor reaches the
       frame its own frame 0 names.  No effect clip is on the page yet. */
    while (caster_magic_cursor[SAF_CURSOR_FRAME_INDEX] != magic_clip_lead_in) {
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) COMBAT_SURFACE_BYTES);

        request[DRAW_REQUEST_ITEM_INDEX] = 0;
        request[DRAW_REQUEST_IMAGE] = (int) backdrop;
        fdps_draw_composite_sprite(request, 0);

        request[DRAW_REQUEST_IMAGE] = target_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            target_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 0);

        request[DRAW_REQUEST_IMAGE] = caster_magic_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            caster_magic_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);

        fdps_saf_advance_tick(target_cursor, SAF_ADVANCE_LOOPING);
        fdps_saf_advance_tick(caster_magic_cursor, SAF_ADVANCE_HOLD_LAST);

        fdps_draw_unit_hp_mp_gauges(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
            COMBAT_SURFACE_PITCH, caster_unit_index);
        fdps_draw_unit_hp_mp_gauges(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
            COMBAT_SURFACE_PITCH, (int) target_unit_indices[0]);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame just composed is
               the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the 64000-byte transfer starts clear. */
        }
        fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                       + COMBAT_VISIBLE_ORIGIN_OFFSET),
                       COMBAT_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                       SCREEN_WIDTH, SCREEN_WIDTH, SCREEN_HEIGHT);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    /* Phase 2: the build-up clip, played through once over the same three
       layers, and then the spell's MP is spent. */
    effect_cursor[SAF_CURSOR_IMAGE] = (int) buildup_clip;
    fdps_saf_advance_tick(effect_cursor, SAF_CURSOR_RESET);

    clip_finished = 0;
    while (clip_finished == 0) {
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) COMBAT_SURFACE_BYTES);

        request[DRAW_REQUEST_ITEM_INDEX] = 0;
        request[DRAW_REQUEST_IMAGE] = (int) backdrop;
        fdps_draw_composite_sprite(request, 0);

        request[DRAW_REQUEST_IMAGE] = target_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            target_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 0);

        request[DRAW_REQUEST_IMAGE] = caster_magic_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            caster_magic_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);

        request[DRAW_REQUEST_IMAGE] = effect_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            effect_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);

        fdps_saf_advance_tick(target_cursor, SAF_ADVANCE_LOOPING);
        fdps_saf_advance_tick(caster_magic_cursor, SAF_ADVANCE_HOLD_LAST);
        clip_finished = fdps_saf_advance_tick(effect_cursor,
                                              SAF_ADVANCE_LOOPING);

        fdps_draw_unit_hp_mp_gauges(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
            COMBAT_SURFACE_PITCH, caster_unit_index);
        fdps_draw_unit_hp_mp_gauges(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
            COMBAT_SURFACE_PITCH, (int) target_unit_indices[0]);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }
        fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                       + COMBAT_VISIBLE_ORIGIN_OFFSET),
                       COMBAT_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                       SCREEN_WIDTH, SCREEN_WIDTH, SCREEN_HEIGHT);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    fdps_spell_deduct_mp_cost(caster_unit_index, spell_id);

    /* How many hit frames one pass of the main clip holds, which is how many
       steps the drain takes and how many passes the loop below needs. */
    main_clip_frames = fdps_saf_frame_count(main_clip);
    for (frame = 0; frame < main_clip_frames; frame++) {
        saf_frame = fdps_saf_get_frame(main_clip, frame);
        if (*((unsigned char *) saf_frame + SAF_FRAME_HIT_MARKER_OFFSET)
                != 0) {
            hit_frame_count++;
        }
    }
    if (hit_frame_count == 0) {
        printf("ERROR: No Hit Point !!!\n");
        exit(1);
    }

    /* Phase 3: the main clip, one target at a time.  The clip is rewound to
       its lead-in frame at every end and keeps going until as many hit frames
       have gone by as the scan above counted. */
    effect_cursor[SAF_CURSOR_IMAGE] = (int) main_clip;
    fdps_saf_advance_tick(effect_cursor, SAF_CURSOR_RESET);

    for (i = 0; i < target_count; i++) {
        hits_played = 0;
        knockback_step = KNOCKBACK_AT_REST;

        target = fdps_get_unit_record((int) target_unit_indices[i]);
        hp_before = (int) target->hp_current;
        spell_missed = (char) (fdps_spell_damage_unit(
                                   caster_unit_index,
                                   (int) target_unit_indices[i],
                                   spell_id) == 0);
        hp_after = (int) target->hp_current;
        target->hp_current = (short) hp_before;

        if (spell_id == SPELL_NECROMANCY && spell_missed == 0) {
            fdps_unit_inflict_random_ailments((int) target_unit_indices[i]);
        }

        while (hits_played < hit_frame_count) {
            recolor_phase = (recolor_phase + 1) % RECOLOR_PHASES;

            /* Only the first tick a frame is held on counts, so one hit frame
               lands one step of the drain however long it stays up. */
            if (effect_cursor[SAF_CURSOR_TICKS_HELD] == 0) {
                saf_frame = fdps_saf_get_frame(
                    main_clip, effect_cursor[SAF_CURSOR_FRAME_INDEX]);
                if (*((unsigned char *) saf_frame
                      + SAF_FRAME_HIT_MARKER_OFFSET) != 0) {
                    hits_played++;
                    if (spell_missed == 0) {
                        knockback_step = KNOCKBACK_FIRST_STEP;
                        target->hp_current = (short)
                            (hp_before - (hp_before - hp_after) * hits_played
                                         / hit_frame_count);
                    }
                }
            }

            memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
                   (size_t) COMBAT_SURFACE_BYTES);

            request[DRAW_REQUEST_ITEM_INDEX] = 0;
            request[DRAW_REQUEST_IMAGE] = (int) backdrop;
            fdps_draw_composite_sprite(request, 0);

            if (overlay_clip != NULL) {
                request[DRAW_REQUEST_IMAGE] = overlay_cursor[SAF_CURSOR_IMAGE];
                request[DRAW_REQUEST_ITEM_INDEX] =
                    overlay_cursor[SAF_CURSOR_FRAME_INDEX];
                fdps_draw_composite_sprite(request, 0);
                fdps_saf_advance_tick(overlay_cursor, SAF_ADVANCE_LOOPING);
            }

            if (knockback_step == KNOCKBACK_AT_REST) {
                request[DRAW_REQUEST_X] = COMBAT_SURFACE_MARGIN;
                request[DRAW_REQUEST_Y] = COMBAT_SURFACE_MARGIN;
            } else {
                request[DRAW_REQUEST_X] = COMBAT_SURFACE_MARGIN
                    - knockback_offset_x[knockback_step] * knockback_sign;
                request[DRAW_REQUEST_Y] = COMBAT_SURFACE_MARGIN
                    - knockback_offset_y[knockback_step] * knockback_sign;
            }

            recolor_selector = RECOLOR_SELECTOR_BASE
                               + spell_recolor_band[spell_id]
                                 * RECOLOR_BAND_STRIDE
                               + recolor_phase;
            request[DRAW_REQUEST_BLIT_OPERAND] = recolor_selector;
            request[DRAW_REQUEST_IMAGE] = target_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                target_cursor[SAF_CURSOR_FRAME_INDEX];
            if (spell_missed == 0) {
                request[DRAW_REQUEST_BLIT_MODE] = RECOLOR_BLIT_MODE;
            } else {
                request[DRAW_REQUEST_BLIT_MODE] = 0;
            }
            fdps_draw_composite_sprite(request, 0);

            request[DRAW_REQUEST_BLIT_OPERAND] = 0;
            request[DRAW_REQUEST_BLIT_MODE] = 0;
            request[DRAW_REQUEST_X] = COMBAT_SURFACE_MARGIN;
            request[DRAW_REQUEST_Y] = COMBAT_SURFACE_MARGIN;

            request[DRAW_REQUEST_IMAGE] =
                caster_magic_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                caster_magic_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_draw_composite_sprite(request, 1);

            request[DRAW_REQUEST_IMAGE] = effect_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                effect_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_draw_composite_sprite(request, 1);

            fdps_draw_unit_hp_mp_gauges(
                (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                COMBAT_SURFACE_PITCH, caster_unit_index);
            fdps_draw_unit_hp_mp_gauges(
                (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                COMBAT_SURFACE_PITCH, (int) target_unit_indices[i]);

            fdps_saf_advance_tick(target_cursor, SAF_ADVANCE_LOOPING);
            fdps_saf_advance_tick(caster_magic_cursor, SAF_ADVANCE_HOLD_LAST);
            if (fdps_saf_advance_tick(effect_cursor, SAF_ADVANCE_LOOPING)
                    != 0) {
                effect_cursor[SAF_CURSOR_FRAME_INDEX] = main_clip_lead_in;
            }

            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   == 0) {
            }
            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   != 0) {
            }
            fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                           + COMBAT_VISIBLE_ORIGIN_OFFSET),
                           COMBAT_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                           SCREEN_WIDTH, SCREEN_WIDTH, SCREEN_HEIGHT);
            while (last_tick == data_fdps_timer_tick_counter) {
            }
            last_tick = data_fdps_timer_tick_counter;

            if (knockback_step != KNOCKBACK_AT_REST) {
                knockback_step--;
            }
        }

        /* The changeover.  The finished target keeps being drawn out of the
           copy taken here while the live cursor is rewound onto the next
           target's clip, and the two pass each other over sixteen frames.
           Both gauge panels are painted twice a frame, once before the draws
           for target 0 and once after them for the target that just
           finished. */
        if (i != target_count - 1) {
            outgoing_cursor[SAF_CURSOR_IMAGE] = target_cursor[SAF_CURSOR_IMAGE];
            outgoing_cursor[SAF_CURSOR_FRAME_INDEX] =
                target_cursor[SAF_CURSOR_FRAME_INDEX];
            outgoing_cursor[SAF_CURSOR_TICKS_HELD] =
                target_cursor[SAF_CURSOR_TICKS_HELD];
            target_cursor[SAF_CURSOR_IMAGE] = (int) target_stand_clips[i + 1];
            fdps_saf_advance_tick(target_cursor, SAF_CURSOR_RESET);

            for (slide_frame = TARGET_SWAP_FRAMES - 1; slide_frame >= 0;
                 slide_frame--) {
                memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
                       (size_t) COMBAT_SURFACE_BYTES);

                request[DRAW_REQUEST_X] = COMBAT_SURFACE_MARGIN;
                request[DRAW_REQUEST_ITEM_INDEX] = 0;
                request[DRAW_REQUEST_IMAGE] = (int) backdrop;

                fdps_draw_unit_hp_mp_gauges(
                    (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                    COMBAT_SURFACE_PITCH, caster_unit_index);
                fdps_draw_unit_hp_mp_gauges(
                    (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                    COMBAT_SURFACE_PITCH, (int) target_unit_indices[0]);
                fdps_draw_composite_sprite(request, 0);

                if (overlay_clip != NULL) {
                    request[DRAW_REQUEST_IMAGE] =
                        overlay_cursor[SAF_CURSOR_IMAGE];
                    request[DRAW_REQUEST_ITEM_INDEX] =
                        overlay_cursor[SAF_CURSOR_FRAME_INDEX];
                    fdps_draw_composite_sprite(request, 0);
                    fdps_saf_advance_tick(overlay_cursor, SAF_ADVANCE_LOOPING);
                }

                fade_descriptor[BLEND_DESCRIPTOR_LEVEL] =
                    FADE_LEVEL_TOP - slide_frame;
                request[DRAW_REQUEST_BLIT_OPERAND] = (int) fade_descriptor;
                request[DRAW_REQUEST_BLIT_MODE] = FADE_BLIT_MODE;
                request[DRAW_REQUEST_X] = COMBAT_SURFACE_MARGIN;
                request[DRAW_REQUEST_IMAGE] =
                    outgoing_cursor[SAF_CURSOR_IMAGE];
                request[DRAW_REQUEST_ITEM_INDEX] =
                    outgoing_cursor[SAF_CURSOR_FRAME_INDEX];
                fdps_draw_composite_sprite(request, 0);

                request[DRAW_REQUEST_BLIT_OPERAND] = 0;
                request[DRAW_REQUEST_BLIT_MODE] = 0;
                request[DRAW_REQUEST_X] = COMBAT_SURFACE_MARGIN
                    - slide_frame * TARGET_SWAP_PIXELS_PER_FRAME
                      * knockback_sign;
                request[DRAW_REQUEST_IMAGE] = target_cursor[SAF_CURSOR_IMAGE];
                request[DRAW_REQUEST_ITEM_INDEX] =
                    target_cursor[SAF_CURSOR_FRAME_INDEX];
                fdps_draw_composite_sprite(request, 0);

                request[DRAW_REQUEST_X] = COMBAT_SURFACE_MARGIN;
                request[DRAW_REQUEST_IMAGE] = effect_cursor[SAF_CURSOR_IMAGE];
                request[DRAW_REQUEST_ITEM_INDEX] =
                    effect_cursor[SAF_CURSOR_FRAME_INDEX];
                fdps_draw_composite_sprite(request, 1);

                request[DRAW_REQUEST_IMAGE] =
                    caster_magic_cursor[SAF_CURSOR_IMAGE];
                request[DRAW_REQUEST_ITEM_INDEX] =
                    caster_magic_cursor[SAF_CURSOR_FRAME_INDEX];
                fdps_draw_composite_sprite(request, 1);

                fdps_saf_advance_tick(target_cursor, SAF_ADVANCE_LOOPING);
                fdps_saf_advance_tick(caster_magic_cursor,
                                      SAF_ADVANCE_HOLD_LAST);
                if (fdps_saf_advance_tick(effect_cursor, SAF_ADVANCE_LOOPING)
                        != 0) {
                    effect_cursor[SAF_CURSOR_FRAME_INDEX] = main_clip_lead_in;
                }

                fdps_draw_unit_hp_mp_gauges(
                    (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                    COMBAT_SURFACE_PITCH, caster_unit_index);
                fdps_draw_unit_hp_mp_gauges(
                    (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                    COMBAT_SURFACE_PITCH, (int) target_unit_indices[i]);

                while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                       == 0) {
                }
                while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                       != 0) {
                }
                fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                               + COMBAT_VISIBLE_ORIGIN_OFFSET),
                               COMBAT_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                               SCREEN_WIDTH, SCREEN_WIDTH, SCREEN_HEIGHT);
                while (last_tick == data_fdps_timer_tick_counter) {
                }
                last_tick = data_fdps_timer_tick_counter;
            }
        }
    }

    /* Phase 4: the main clip played out to its end, this time without being
       rewound, over the last target. */
    clip_finished = 0;
    while (clip_finished == 0) {
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) COMBAT_SURFACE_BYTES);

        request[DRAW_REQUEST_ITEM_INDEX] = 0;
        request[DRAW_REQUEST_IMAGE] = (int) backdrop;
        fdps_draw_composite_sprite(request, 0);

        if (overlay_clip != NULL) {
            request[DRAW_REQUEST_IMAGE] = overlay_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                overlay_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_draw_composite_sprite(request, 0);
            fdps_saf_advance_tick(overlay_cursor, SAF_ADVANCE_LOOPING);
        }

        request[DRAW_REQUEST_IMAGE] = target_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            target_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 0);

        request[DRAW_REQUEST_IMAGE] = caster_magic_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            caster_magic_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);

        request[DRAW_REQUEST_IMAGE] = effect_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            effect_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);

        fdps_saf_advance_tick(target_cursor, SAF_ADVANCE_LOOPING);
        fdps_saf_advance_tick(caster_magic_cursor, SAF_ADVANCE_HOLD_LAST);
        clip_finished = fdps_saf_advance_tick(effect_cursor,
                                              SAF_ADVANCE_LOOPING);

        fdps_draw_unit_hp_mp_gauges(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
            COMBAT_SURFACE_PITCH, caster_unit_index);
        fdps_draw_unit_hp_mp_gauges(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
            COMBAT_SURFACE_PITCH,
            (int) target_unit_indices[target_count - 1]);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }
        fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                       + COMBAT_VISIBLE_ORIGIN_OFFSET),
                       COMBAT_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                       SCREEN_WIDTH, SCREEN_WIDTH, SCREEN_HEIGHT);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    /* Phase 5: the finish clip.  The overlay is NOT drawn here -- the arm the
       other three phases carry has no counterpart in this loop. */
    clip_finished = 0;
    effect_cursor[SAF_CURSOR_IMAGE] = (int) finish_clip;
    fdps_saf_advance_tick(effect_cursor, SAF_CURSOR_RESET);

    while (clip_finished == 0) {
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) COMBAT_SURFACE_BYTES);

        request[DRAW_REQUEST_ITEM_INDEX] = 0;
        request[DRAW_REQUEST_IMAGE] = (int) backdrop;
        fdps_draw_composite_sprite(request, 0);

        request[DRAW_REQUEST_IMAGE] = target_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            target_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 0);

        request[DRAW_REQUEST_IMAGE] = caster_magic_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            caster_magic_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);

        request[DRAW_REQUEST_IMAGE] = effect_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            effect_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);

        fdps_saf_advance_tick(target_cursor, SAF_ADVANCE_LOOPING);
        fdps_saf_advance_tick(caster_magic_cursor, SAF_ADVANCE_HOLD_LAST);
        clip_finished = fdps_saf_advance_tick(effect_cursor,
                                              SAF_ADVANCE_LOOPING);

        fdps_draw_unit_hp_mp_gauges(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
            COMBAT_SURFACE_PITCH, caster_unit_index);
        fdps_draw_unit_hp_mp_gauges(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
            COMBAT_SURFACE_PITCH,
            (int) target_unit_indices[target_count - 1]);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }
        fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                       + COMBAT_VISIBLE_ORIGIN_OFFSET),
                       COMBAT_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                       SCREEN_WIDTH, SCREEN_WIDTH, SCREEN_HEIGHT);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    fdps_audio_stop_sample(SFX_STOP_ALL_SLOTS);
    free(backdrop);
    free(caster_stand_clip);
    free(data_fdps_combat_gauge_sprite_sheet_ptr);
    free(data_fdps_gauge_fill_sheet_ptr);
    free(caster_magic_clip);
    free((void *) request[DRAW_REQUEST_DEST_BASE]);
    free(buildup_clip);
    free(main_clip);
    free(finish_clip);
    if (overlay_clip != NULL) {
        free(overlay_clip);
    }
    for (i = 0; i < target_count; i++) {
        free(target_stand_clips[i]);
    }

    memset((void *) VGA_SCREEN_BASE, 0,
           (size_t) (SCREEN_WIDTH * SCREEN_HEIGHT));
    fdps_set_palette_range((struct fdps_palette_entry *)
                           data_fdps_vga_main_palette_ptr,
                           0, LAST_DAC_ENTRY, 0, 0, 0);

    blend_table_file = fopen(MAP_SHADE_RAMP_FILE, BLEND_TABLE_MODE);
    fread(data_fdps_palette_shade_ramp_table, 1, (size_t) SHADE_RAMP_BYTES,
          blend_table_file);
    fclose(blend_table_file);
    blend_table_file = fopen(MAP_PALETTE_CUBE_FILE, BLEND_TABLE_MODE);
    fread(data_fdps_inverse_palette_cube, 1, (size_t) PALETTE_CUBE_BYTES,
          blend_table_file);
    fclose(blend_table_file);
}
