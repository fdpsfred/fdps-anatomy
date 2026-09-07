/* church.c -- the village church: the class change and the transformation
 * animation that shows it happening.
 *
 * See church.h for what a caller has to know.  This file owns no state of its
 * own: everything it touches is either the unit record it was pointed at or a
 * global src/gamedata.h declares.
 *
 * malloc and free come from <stdlib.h> and are real calls in the original,
 * CALL 0003d375 and CALL 0003d478.  memset comes from <string.h> and is the
 * call at 00042cd0, and inp comes from <conio.h> and is the library routine at
 * 0003d4e4 rather than the IN instruction an intrinsic would have produced.
 * fopen, fread, fclose and sprintf come from <stdio.h> and are the calls at
 * 0004265e, 0004270d, 000428be and 00042d41.
 */
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "table.h"
#include "blit.h"
#include "saf.h"
#include "sprite.h"
#include "vfs.h"
#include "palette.h"
#include "church.h"

/* The two blend tables the composited frames go through, read straight off
   disk over the globals src/gamedata.h owns: PUSH 0x4800 at 00034c63 for the
   shade ramp and PUSH 0x1000 at 00034c9f for the inverse-palette cube, each
   with an element size of 1.  The fight pair goes in on the way in and the
   field pair comes back on the way out; all four names are bare, so they are
   looked for in the working directory. */
#define SHADE_RAMP_BYTES 0x4800
#define PALETTE_CUBE_BYTES 0x1000
#define FIGHT_SHADE_RAMP_FILE "FMer1.tmp"
#define FIGHT_PALETTE_CUBE_FILE "FMer2.tmp"
#define MAP_SHADE_RAMP_FILE "Mer1.tmp"
#define MAP_PALETTE_CUBE_FILE "Mer2.tmp"
#define BLEND_TABLE_MODE "rb"

/* The containers the three clips come out of and the format the two standing
   clips are named by.  A standing clip is picked by an id byte of the unit
   record, exactly as the combat screen picks one. */
#define FIGHT_ARCHIVE "Fight.vfs"
#define EFFECT_ARCHIVE "MISC.VFS"
#define STAND_CLIP_FORMAT "Stand%03d.saf"

/* The name buffer is twenty bytes, LEA EAX,[EBP-0x68] against a frame whose
   next slot is at [EBP-0x54].  "Stand255.saf" is the longest name the format
   can produce from a byte, so it fits with room to spare. */
#define CLIP_NAME_BYTES 20

/* One promotion route is three bytes and the caller names the route, so the
   record is stepped through as bytes: LEA EDX,[EDX+EDX*2] / ADD EAX,EDX at
   00034cf1.  Byte 0 is the new form id and byte 1 the new class code; byte 2,
   the move bonus, is not read here. */
#define PROMOTION_ROUTE_BYTES 3
#define PROMOTION_ROUTE_PORTRAIT 0
#define PROMOTION_ROUTE_CLASS 1

/* Which of the two transformation effects a class gets.  Twenty-five entries,
   one per class code, holding 0 for the green effect and 1 for the blue one --
   a 25-byte template at 0003107f copied onto the frame with REP MOVSD plus a
   trailing MOVSB at 00034c33, which is what an initialised automatic array
   compiles to.  Nothing else reads it and it is not a global.

   A class code above 24 indexes past the end of the frame copy; nothing here
   bounds it, and the caller does not either.  Every class code RankUp.dat's
   nine records hand out is inside it, the highest being 24
   (assets/characters.md). */
#define EFFECT_CLASS_COUNT 25

/* The two effect member names, a 40-byte template at 00031098 copied onto the
   frame the same way.  They are automatic and not literals because
   fdps_vfs_load_entry upper-cases the CALLER'S storage in place (src/vfs.h),
   so the name it is handed has to be writable and has to survive being
   rewritten; the stride is twenty bytes, IMUL EAX,EAX,0x14 at 00034db5. */
#define EFFECT_NAME_COUNT 2
#define EFFECT_NAME_BYTES 20

/* The offscreen page every frame is composed on: 368 bytes to the row, 248
   rows and the 91,264 bytes that comes to, with the figure's origin 24 pixels
   in on both axes.  What is presented is the 320x200 window at page byte
   0x2298 -- page pixel (24,24), which is that same origin -- so the margin
   holds whatever a layer hangs off the edge into.  THE PAGE IS NOT CLEARED BY
   malloc AND malloc'S ANSWER IS NOT TESTED; the first thing each loop does is
   memset it, so nothing is ever composed over heap leftovers. */
#define CANVAS_BYTES 0x16480
#define CANVAS_PITCH 0x170
#define CANVAS_ROWS 0xf8
#define CANVAS_ORIGIN 0x18
#define CANVAS_WINDOW_AT 0x2298

/* Mode 13h, whole: 0xa0000 is where the display adapter answers and not the
   address of anything the linker places, so it stays a literal
   (rebuild_info/pitfalls.md, contract E). */
#define VGA_SCREEN_BASE 0xa0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8
#define VGA_SCREEN_BYTES 0xfa00

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, which is what every presented frame straddles. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* Both palette uploads cover the whole DAC with no bias: PUSH 0/0/0 then PUSH
   0xff and PUSH 0 at 00034dfe and again at 0003521b. */
#define LAST_DAC_ENTRY 0xff

/* mode 1 of fdps_saf_advance_tick (src/saf.h) is the reset that zeroes a
   cursor's frame and tick counters; mode 0 is the advance that restarts the
   clip when it runs off the end.  The image slot is filled in before the
   reset. */
#define SAF_CURSOR_RESET 1
#define SAF_CURSOR_ADVANCE 0

/* How long the figure is held before the effect starts and again after it
   ends: CMP dword ptr [EBP-0x24],0x19 / JL at 00034e6f and at 00035125.  These
   are the two stretches where nothing but the standing clip is on screen, and
   both are drawn with the sound flag clear so the standing clip's own frame
   sounds stay silent. */
#define STAND_HOLD_FRAMES 0x19

/* Where the figure changes: the effect's frame 30 is the last frame of the
   before phase and the first of the after phase.  CMP dword ptr
   [EBP+0xffffff70],0x1e / JGE at 00034f32 is SIGNED, and it reads the
   REQUEST's frame slot rather than the cursor -- on every pass but the first
   that slot holds the effect cursor's frame, and on the first it still holds
   the standing cursor's, which is always below 30 because every Stand%03d.saf
   has four frames (resource_info/saf.md). */
#define EFFECT_CHANGE_FRAME 0x1e

/* 00034c10.  Four presentation loops with the same seven-statement tail
   written out four times -- the original has it inline in all four and not
   behind a call, so it stays written out here.  The first and last are counted
   loops with their test at the top and their increment in a block of their
   own; the middle two are while loops, one on the request's frame slot and one
   on what fdps_saf_advance_tick reported.

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED, the same contract
   fdps_saf_play_over_scene (src/saf.c) and fdps_animate_turn_banner
   (src/anim.c) carry.  last_tick is read at 00034f1a before anything has
   written it, so the very first presented frame ends its wait at once unless
   the stack happened to hold the counter's current value, and the animation is
   one tick shorter than a clean reading of it would be.  Latching the counter
   before the first loop -- which is what writing this tidily leads to -- adds
   that tick back.  data_fdps_timer_tick_counter is volatile at its declaration
   (src/gamedata.h) precisely so these four loops keep reloading it.

   THE PAGE POINTER LIVES IN THE REQUEST AND NOWHERE ELSE.  malloc's answer is
   stored straight into the request's destination field at 00034e26 and every
   later use -- the clear, the blit's source, free's argument -- reads it back
   out of there.  Keeping a separate copy would be a variable the original does
   not have.

   THE STANDING CURSOR IS NOT ADVANCED IN THE AFTER PHASE'S FIRST LOOP AND THE
   EFFECT CURSOR IS NOT ADVANCED IN THE BEFORE PHASE'S LAST.  Each loop
   advances exactly the cursors it drew, so the effect's frame counter carries
   straight from 30 into the third loop and the new figure's starts at 0 there.

   Every CALL whose value is read is read here: malloc's page, the three
   fdps_vfs_load_entry handles, fdps_get_unit_record's and
   fdps_get_promotion_record's records, and the third loop's
   fdps_saf_advance_tick.  The other twelve advances discard theirs, and
   fdps_draw_composite_sprite, fdps_blit_rect, fdps_set_palette_range, memset,
   sprintf, fread and fclose return values nothing looks at. */
void fdps_church_promote_unit(int unit_index, int promotion_entry)
{
    /* Set by the third loop's advance and nothing else: 1 once the effect clip
       has stepped past its last frame, which is what ends that loop. */
    int effect_finished = 0;
    /* Which of the two effects each class code gets -- see the note above. */
    unsigned char effect_by_class[EFFECT_CLASS_COUNT] = {
        0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1,
        0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0};
    /* The two effect members, in writable storage -- see the note above. */
    char effect_names[EFFECT_NAME_COUNT][EFFECT_NAME_BYTES] = {
        "magic0gg.saf", "magic0bb.saf"};
    /* The one handle all four blend-table reads pass through, reused. */
    FILE *blend_table_file;
    /* The record being promoted, resolved once and then written twice. */
    struct fdps_unit_record *unit;
    /* The three bytes of the promotion route the caller picked, addressed off
       the character's RankUp.dat record. */
    unsigned char *promotion_route;
    /* The unit's base identity at record +0x08, which names the clip the
       figure starts the animation as. */
    int character_id;
    /* The route's two bytes: the form the figure ends as, which also names its
       clip, and the class code, which picks the effect. */
    int new_portrait_id;
    int new_class_id;
    /* The figure before the change, the figure after it, and the
       transformation effect drawn over both. */
    void *before_clip;
    void *after_clip;
    void *effect_clip;
    /* One three-dword playback cursor per clip (src/saf.h).  Each also holds
       its clip in its image slot, and each clip is freed through its own
       local. */
    int before_cursor[SAF_CURSOR_DWORDS];
    int after_cursor[SAF_CURSOR_DWORDS];
    int effect_cursor[SAF_CURSOR_DWORDS];
    /* Where each member name is formatted before it is looked up. */
    char clip_name[CLIP_NAME_BYTES];
    /* The nine-dword draw request src/sprite.h describes, holding the
       composing page in its destination field for the whole animation. */
    int request[DRAW_REQUEST_DWORDS];
    /* Which of the 25 held frames the first and last loops are on. */
    int hold_frame;
    /* The tick the previous presented frame ended on.  Deliberately not
       initialised -- see the note above. */
    unsigned int last_tick;

    blend_table_file = fopen(FIGHT_SHADE_RAMP_FILE, BLEND_TABLE_MODE);
    fread(data_fdps_palette_shade_ramp_table, 1, (size_t) SHADE_RAMP_BYTES,
          blend_table_file);
    fclose(blend_table_file);
    blend_table_file = fopen(FIGHT_PALETTE_CUBE_FILE, BLEND_TABLE_MODE);
    fread(data_fdps_inverse_palette_cube, 1, (size_t) PALETTE_CUBE_BYTES,
          blend_table_file);
    fclose(blend_table_file);

    unit = fdps_get_unit_record(unit_index);
    character_id = (int) unit->char_id;
    promotion_route = (unsigned char *)
                          fdps_get_promotion_record((int) unit->char_id)
                      + promotion_entry * PROMOTION_ROUTE_BYTES;
    new_portrait_id = (int) promotion_route[PROMOTION_ROUTE_PORTRAIT];
    new_class_id = (int) promotion_route[PROMOTION_ROUTE_CLASS];
    unit->portrait_id = (unsigned char) new_portrait_id;
    unit->clazz = (unsigned char) new_class_id;

    sprintf(clip_name, STAND_CLIP_FORMAT, character_id);
    before_clip = fdps_vfs_load_entry(FIGHT_ARCHIVE, clip_name);
    before_cursor[SAF_CURSOR_IMAGE] = (int) before_clip;
    fdps_saf_advance_tick(before_cursor, SAF_CURSOR_RESET);

    sprintf(clip_name, STAND_CLIP_FORMAT, new_portrait_id);
    after_clip = fdps_vfs_load_entry(FIGHT_ARCHIVE, clip_name);
    after_cursor[SAF_CURSOR_IMAGE] = (int) after_clip;
    fdps_saf_advance_tick(after_cursor, SAF_CURSOR_RESET);

    effect_clip = fdps_vfs_load_entry(EFFECT_ARCHIVE,
                                      effect_names[effect_by_class
                                                       [new_class_id]]);
    effect_cursor[SAF_CURSOR_IMAGE] = (int) effect_clip;
    fdps_saf_advance_tick(effect_cursor, SAF_CURSOR_RESET);

    memset((void *) VGA_SCREEN_BASE, 0, (size_t) VGA_SCREEN_BYTES);
    fdps_set_palette_range((struct fdps_palette_entry *)
                           data_fdps_vga_fight_palette_ptr,
                           0, LAST_DAC_ENTRY, 0, 0, 0);

    request[DRAW_REQUEST_DEST_BASE] = (int) malloc((size_t) CANVAS_BYTES);
    request[DRAW_REQUEST_DEST_PITCH] = CANVAS_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = CANVAS_ROWS;
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;
    request[DRAW_REQUEST_X] = CANVAS_ORIGIN;
    request[DRAW_REQUEST_Y] = CANVAS_ORIGIN;

    for (hold_frame = 0; hold_frame < STAND_HOLD_FRAMES; hold_frame++) {
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) CANVAS_BYTES);
        request[DRAW_REQUEST_IMAGE] = (int) before_clip;
        request[DRAW_REQUEST_ITEM_INDEX] =
            before_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 0);
        fdps_saf_advance_tick(before_cursor, SAF_CURSOR_ADVANCE);
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the blit starts clear of it. */
        }
        fdps_blit_rect((unsigned int)
                           ((unsigned char *) request[DRAW_REQUEST_DEST_BASE]
                            + CANVAS_WINDOW_AT),
                       CANVAS_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    while (request[DRAW_REQUEST_ITEM_INDEX] < EFFECT_CHANGE_FRAME) {
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) CANVAS_BYTES);
        request[DRAW_REQUEST_IMAGE] = (int) before_clip;
        request[DRAW_REQUEST_ITEM_INDEX] =
            before_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);
        fdps_saf_advance_tick(before_cursor, SAF_CURSOR_ADVANCE);
        request[DRAW_REQUEST_IMAGE] = (int) effect_clip;
        request[DRAW_REQUEST_ITEM_INDEX] =
            effect_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);
        fdps_saf_advance_tick(effect_cursor, SAF_CURSOR_ADVANCE);
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }
        fdps_blit_rect((unsigned int)
                           ((unsigned char *) request[DRAW_REQUEST_DEST_BASE]
                            + CANVAS_WINDOW_AT),
                       CANVAS_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    while (effect_finished == 0) {
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) CANVAS_BYTES);
        request[DRAW_REQUEST_IMAGE] = (int) after_clip;
        request[DRAW_REQUEST_ITEM_INDEX] =
            after_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);
        fdps_saf_advance_tick(after_cursor, SAF_CURSOR_ADVANCE);
        request[DRAW_REQUEST_IMAGE] = (int) effect_clip;
        request[DRAW_REQUEST_ITEM_INDEX] =
            effect_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);
        effect_finished = fdps_saf_advance_tick(effect_cursor,
                                                SAF_CURSOR_ADVANCE);
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }
        fdps_blit_rect((unsigned int)
                           ((unsigned char *) request[DRAW_REQUEST_DEST_BASE]
                            + CANVAS_WINDOW_AT),
                       CANVAS_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    for (hold_frame = 0; hold_frame < STAND_HOLD_FRAMES; hold_frame++) {
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) CANVAS_BYTES);
        request[DRAW_REQUEST_IMAGE] = (int) after_clip;
        request[DRAW_REQUEST_ITEM_INDEX] =
            after_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 0);
        fdps_saf_advance_tick(after_cursor, SAF_CURSOR_ADVANCE);
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }
        fdps_blit_rect((unsigned int)
                           ((unsigned char *) request[DRAW_REQUEST_DEST_BASE]
                            + CANVAS_WINDOW_AT),
                       CANVAS_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    free((void *) request[DRAW_REQUEST_DEST_BASE]);
    free(before_clip);
    free(after_clip);
    free(effect_clip);

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
