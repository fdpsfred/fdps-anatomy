/* church.c -- the village church: the candidate list the player picks a
 * promotion from, the class change itself and the transformation animation
 * that shows it happening.
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
#include "audio.h"
#include "unit.h"
#include "table.h"
#include "blit.h"
#include "keybd.h"
#include "msgwin.h"
#include "palcycle.h"
#include "saf.h"
#include "sprite.h"
#include "text.h"
#include "unititem.h"
#include "vfs.h"
#include "village.h"
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

/* ------------------------------------------------------------------
 * fdps_church_select_promote_candidate @ 000352c0
 * ------------------------------------------------------------------ */

/* The list's offscreen page: 312 bytes to a row, 76 rows, and the 0x5ca0 a
   pass mallocs is exactly that product.  It is rebuilt from nothing every pass
   and freed at the end of it, so no state carries in the pixels. */
#define LIST_PAGE_PITCH 0x138
#define LIST_PAGE_BYTES 0x5ca0

/* The panel as it reaches the visible screen: 304 by 67 read from page offset
   0x3ad, which is page (5, 3), and written at 0xa9c48, which is screen
   (8, 125) on the mode 13h framebuffer.  The page offsets stay literals for
   the same reason VGA_SCREEN_BASE above does -- they are positions inside a
   block this function allocated, not addresses of anything the linker
   places. */
#define LIST_PANEL_PAGE_AT 0x3ad
#define LIST_PANEL_SCREEN_AT 0x9c48
#define LIST_PANEL_W 0x130
#define LIST_PANEL_H 0x43

/* The three slots: one row of three, 0x65 apart starting at x 0x0b, at y 0x09.
   Three is also the distance the window base keeps from the cursor and the
   step both the window and the vertical cursor moves take.

   The column and the row come out of ONE signed division of the slot number by
   three, remainder for the column and quotient for the row (MOV EBX,0x3 /
   SAR EDX,0x1f / IDIV EBX at 000354ad and again at 000354c6).  With three
   slots the row is always 0 and the column always the slot number; the divide
   is in the original all the same, and the y stride it scales is the same
   0x1b the village grid lays its two rows out on. */
#define LIST_VISIBLE_SLOTS 3
#define LIST_COLUMNS 3
#define LIST_ROW_STEP 3
#define LIST_SLOT_X_BASE 0x0b
#define LIST_SLOT_X_STRIDE 0x65
#define LIST_SLOT_Y_BASE 0x09
#define LIST_SLOT_Y_STRIDE 0x1b

/* Sprite 0 of the village window sheet is the panel frame and sprite 2 of
   SelBar.Cel is the highlight behind the slot the cursor stands on, hung six
   pixels left of the slot and five below its top.  Both go through
   fdps_cel_blit_sprite in its plain opaque mode with no operand. */
#define LIST_WINDOW_SPRITE 0
#define LIST_HIGHLIGHT_SPRITE 2
#define HIGHLIGHT_X_LIFT 6
#define HIGHLIGHT_Y_DROP 5
#define LIST_BLIT_OPERAND 0
#define LIST_BLIT_MODE 0

/* Command.cel sprites 0x2a and 0x2b are the left and right halves of the stand
   the walking icon is posed on, laid 0x10 scanlines below the slot top with
   their halves 0x17 apart and the pair starting two pixels left of the slot.
   fdps_blit_command_sprite puts a fixed 25 by 22 block down with no clipping
   (sprite.h). */
#define STAND_LEFT_SPRITE 0x2a
#define STAND_RIGHT_SPRITE 0x2b
#define STAND_X_LIFT 2
#define STAND_RIGHT_X_OFFSET 0x17
#define STAND_Y_DROP 0x10

/* The walking icon.  The sprite cache holds twelve stream offsets per member,
   four facings of three walk frames, and ITS offset table starts at the base
   rather than at a .CEL file's +0x0f (mapdraw.c).  The slot is the ROSTER
   INDEX the caller named, IMUL EAX,dword ptr [EBP-0x2c],0xc at 00035584, and
   not the record's own sprite_cache_slot.

   The frame is (tick / 6) & 3 with the value 3 folded back onto 1, which rocks
   the icon 0, 1, 2, 1 instead of jumping from 2 to 0.  The divide is DIV and
   not IDIV (MOV EBX,0x6 / XOR EDX,EDX / DIV EBX at 00035563), which is why
   data_fdps_timer_tick_counter is unsigned at its declaration. */
#define MEMBER_SPRITES_PER_CACHE_SLOT 0x0c
#define CEL_SUB_IMAGE_ENTRY_BYTES 4
#define WALK_FRAME_TICKS 6
#define WALK_FRAME_MASK 3
#define WALK_FRAME_FOLD_FROM 3
#define WALK_FRAME_FOLD_TO 1
#define MEMBER_ICON_W 0x18
#define MEMBER_ICON_H 0x18

/* The two lines of text, both out of the resident Fdetxt00.txt table and both
   in the standard glyph colour 0xd0 over no background with outline 0x6d.  The
   name is message entry char_id + 1, drawn 0x1c right and six down from the
   slot; the target class is message entry class_code + 0xa1, drawn 0x24 right
   and 0x1e down. */
#define NAME_X_OFFSET 0x1c
#define NAME_Y_OFFSET 6
#define NAME_TEXT_ID_BIAS 1
#define CLASS_X_OFFSET 0x24
#define CLASS_Y_OFFSET 0x1e
#define CLASS_TEXT_ID_BIAS 0xa1
#define LIST_TEXT_FG_COLOR 0xd0
#define LIST_TEXT_BG_COLOR 0
#define LIST_TEXT_OUTLINE_COLOR 0x6d

/* The level plate: Command.cel sprite 0x3a two pixels left of the slot and
   0x22 scanlines down, with the member's level printed 0x14 right of the slot
   on the same line as a zero-padded two-digit figure and no leading sign. */
#define LEVEL_PLATE_SPRITE 0x3a
#define LEVEL_PLATE_X_LIFT 2
#define LEVEL_PLATE_Y_DROP 0x22
#define LEVEL_X_OFFSET 0x14
#define LEVEL_DIGITS 2
#define LEVEL_SHOW_PLUS 0

/* The two scroll arrows: Command.cel sprites 0x44/0x45 for up and 0x46/0x47
   for down, the second of each pair being the lit frame.  Which one is drawn
   is (tick / 5) & 1, so each frame of the blink lasts five timer ticks.

   THE DOWN ARROW IS DRAWN OFF THE END OF THE PAGE, exactly as the village
   grid's is: fdps_blit_command_sprite puts a fixed 25 by 22 block down with no
   clipping (sprite.h) and page offset 0x4e96 is row 64 of the 76 the page has,
   so its last ten rows run past the 0x5ca0 block.  It stays where it is: the
   screen window only ever shows its top six rows, so moving it to fit the page
   changes the picture (rebuild_info/pitfalls.md). */
#define ARROW_BLINK_TICKS 5
#define ARROW_BLINK_MASK 1
#define ARROW_UP_SPRITE 0x44
#define ARROW_DOWN_SPRITE 0x46
#define ARROW_UP_PAGE_AT 0x6ae
#define ARROW_DOWN_PAGE_AT 0x4e96

/* The make codes the list acts on.  Everything else, the filter's 0xff "no key
   this poll" included, falls through the chain and only costs a frame. */
#define KEY_ESCAPE 0x01
#define KEY_ENTER 0x1c
#define KEY_SPACE 0x39
#define KEY_UP 0x48
#define KEY_LEFT 0x4b
#define KEY_RIGHT 0x4d
#define KEY_DOWN 0x50

/* What the loop's own result slot holds.  The epilogue hands -1 back for
   anything that is not a confirmation, which is the same -1 a cancel put
   there. */
#define LIST_RUNNING 0
#define LIST_CANCELLED (-1)
#define LIST_CONFIRMED 1

/* The sound every accepted cursor move plays.  This function pushes the copy
   of the string at 0x61f4c -- MOV EAX,0x61f4c ahead of all four calls -- which
   is the same literal village.c and vilmenu.c name. */
#define LIST_MOVE_SFX "Beep.wav"

/* 000352c0.  Three arguments and caller-cleaned: its one call site at 00034745
   pushes the promotion-choice array, the roster-index array and the count, then
   ADD ESP,0xc, and takes the answer out of EAX.  The prologue is the ordinary
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x40 and the three arguments are
   read at [EBP+0x14], [EBP+0x18] and [EBP+0x1c].

   THE CURSOR AND THE WINDOW ARE LOCALS AND BOTH START AT 0, unlike the village
   grid's, which are file globals that remember where the last visit left them.
   MOV dword ptr [EBP-0x24],0x0 and MOV dword ptr [EBP-0x20],0x0 at 000352cc
   and 000352d3 are the whole of their seeding, so this list always opens on
   the first candidate.

   THE KEY CHAIN IS SHORT-CIRCUIT AND ITS ORDER IS THE ASSEMBLY'S: cancel,
   confirm, right, left, up, down.  Each arrow arm is one `if (code == k &&
   bound)` whose failure falls into the NEXT code's test -- the JNZ and the
   failed bound both land on the same address.  No two codes can be equal so
   the ordering is not observable, but the guards are: a blocked move plays no
   sound and costs nothing but a frame.

   THE VERTICAL MOVES ARE NOT CLAMPED AGAINST THE ROW, ONLY AGAINST THE LIST.
   Up needs a cursor of 3 or more and Down needs three more candidates to
   exist, so in a list whose length is not a multiple of three the last entries
   are reachable by Right from the one before them but not by Down.

   THE RECORD IS RESOLVED INSIDE THE COUNT TEST HERE, unlike the village grid,
   which resolves one for every visible cell whether or not the party reaches
   it: the CMP at 00035442 and its JGE jump past the CALL 00023950 at
   0003545a.

   THE FRAME IS DRAWN AFTER THE KEY IS HANDLED AND BEFORE THE LOOP TEST, so the
   pass that reads a cancel or a confirm still builds, presents and paces a
   whole frame before the function returns.

   THE PACE LATCH IS DELIBERATELY NOT INITIALISED.  Every pass ends by spinning
   until data_fdps_timer_tick_counter differs from `last_tick` and then
   re-latching it, and `last_tick` is never seeded, so the first pass compares
   stack garbage and normally falls straight through the wait.  Seeding it adds
   a tick to the opening of every list (rebuild_info/pitfalls.md).
   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of that wait: nothing inside the loop writes the counter, so a build
   allowed to hoist the load would spin here forever.

   The values used after a CALL are four.  fdps_read_scancode_auto_repeat's EAX
   is the scancode and goes to [EBP-0x14] at 000352f0.  malloc's EAX is the
   page, MOV dword ptr [EBP-0x40],EAX at 000353f8, used unchecked -- there is
   no test for NULL anywhere -- and freed at the end of the pass.
   fdps_get_roster_record's EAX is the member record, MOV dword ptr
   [EBP-0x38],EAX at 00035462, and only its char_id byte at +8 and its level
   byte at +0x21 are read, both zero-extended.  fdps_get_promotion_record's EAX
   is that character's RankUp.dat record, MOV dword ptr [EBP-0x34],EAX at
   00035479, and only byte 1 of the route the caller chose is read from it.
   fdps_draw_text answers a pen position that this caller drops (ADD ESP,0x1c
   at 0003560d and 0003564a with no use of EAX), and inp's answer is tested and
   not kept.  The drawing, sound and palette calls return nothing this function
   looks at. */
int fdps_church_select_promote_candidate(int candidate_count,
                                         int *roster_indices,
                                         int *promotion_choices)
{
    /* The answer, filled in once the loop has stopped. */
    int picked_candidate;
    /* Whether the loop is still running, and if not, how it stopped. */
    int loop_result;
    /* One poll of the auto-repeat filter: a make code, or 0xff for nothing. */
    unsigned int scancode;
    /* Which candidate the cursor stands on, and which candidate the leftmost
       of the three visible slots is showing. */
    int cursor;
    int window_base;
    /* The pass's offscreen page. */
    unsigned char *page;
    /* Which of the three visible slots is being drawn, 0 to 2. */
    int slot_index;
    /* Which candidate that slot is showing, and the roster index the caller's
       array gives for it. */
    int candidate_index;
    int roster_index;
    /* That roster entry's record, the source of the name and the level. */
    struct fdps_unit_record *member;
    /* The three bytes of the promotion route this candidate's choice picks out
       of the character's RankUp.dat record, and the class code byte 1 of them
       names -- which is what the second line of the slot spells out. */
    unsigned char *promotion_route;
    int target_class_id;
    /* The slot's top left corner in the page. */
    int slot_x;
    int slot_y;
    /* 0, 1, 2 or 1: which frame of the member's walk cycle this pass shows,
       that frame's entry in the sprite cache and the RLE stream it names. */
    int walk_frame;
    int sprite_index;
    unsigned char *icon_stream;
    /* 0 or 1: which frame of the two-frame scroll arrows this pass shows. */
    int arrow_blink_phase;
    /* The tick the previous pass finished on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    window_base = 0;
    cursor = 0;
    loop_result = LIST_RUNNING;

    while (loop_result == LIST_RUNNING) {
        scancode = fdps_read_scancode_auto_repeat();

        if (scancode == KEY_ESCAPE) {
            loop_result = LIST_CANCELLED;
        } else if (scancode == KEY_ENTER || scancode == KEY_SPACE) {
            loop_result = LIST_CONFIRMED;
        } else if (scancode == KEY_RIGHT && cursor < candidate_count - 1) {
            cursor++;
            if (window_base + LIST_VISIBLE_SLOTS <= cursor) {
                window_base += LIST_ROW_STEP;
            }
            fdps_play_sfx(LIST_MOVE_SFX);
        } else if (scancode == KEY_LEFT && cursor > 0) {
            cursor--;
            if (cursor < window_base) {
                window_base -= LIST_ROW_STEP;
            }
            fdps_play_sfx(LIST_MOVE_SFX);
        } else if (scancode == KEY_UP && cursor > LIST_ROW_STEP - 1) {
            cursor -= LIST_ROW_STEP;
            if (cursor < window_base) {
                window_base -= LIST_ROW_STEP;
            }
            fdps_play_sfx(LIST_MOVE_SFX);
        } else if (scancode == KEY_DOWN
                   && cursor < candidate_count - LIST_ROW_STEP) {
            cursor += LIST_ROW_STEP;
            if (window_base + LIST_VISIBLE_SLOTS <= cursor) {
                window_base += LIST_ROW_STEP;
            }
            fdps_play_sfx(LIST_MOVE_SFX);
        }

        page = (unsigned char *) malloc(LIST_PAGE_BYTES);
        fdps_cel_blit_sprite(data_fdps_village_window_sheet_ptr,
                             LIST_WINDOW_SPRITE, page, LIST_PAGE_PITCH,
                             0, 0, LIST_BLIT_OPERAND, LIST_BLIT_MODE);

        for (slot_index = 0; slot_index < LIST_VISIBLE_SLOTS; slot_index++) {
            candidate_index = window_base + slot_index;

            if (candidate_index < candidate_count) {
                member = fdps_get_roster_record(
                             roster_indices[candidate_index]);
                promotion_route = (unsigned char *)
                                      fdps_get_promotion_record(
                                          (int) member->char_id)
                                  + promotion_choices[candidate_index]
                                    * PROMOTION_ROUTE_BYTES;
                target_class_id = (int) promotion_route[PROMOTION_ROUTE_CLASS];
                roster_index = roster_indices[candidate_index];

                slot_x = (slot_index % LIST_COLUMNS) * LIST_SLOT_X_STRIDE
                         + LIST_SLOT_X_BASE;
                slot_y = (slot_index / LIST_COLUMNS) * LIST_SLOT_Y_STRIDE
                         + LIST_SLOT_Y_BASE;

                if (candidate_index == cursor) {
                    fdps_cel_blit_sprite(data_fdps_selection_bar_sheet_ptr,
                                         LIST_HIGHLIGHT_SPRITE, page,
                                         LIST_PAGE_PITCH,
                                         slot_x - HIGHLIGHT_X_LIFT,
                                         slot_y + HIGHLIGHT_Y_DROP,
                                         LIST_BLIT_OPERAND, LIST_BLIT_MODE);
                }

                fdps_blit_command_sprite(page
                                             + (slot_y + STAND_Y_DROP)
                                               * LIST_PAGE_PITCH
                                             + slot_x - STAND_X_LIFT,
                                         LIST_PAGE_PITCH, STAND_LEFT_SPRITE);
                fdps_blit_command_sprite(page
                                             + (slot_y + STAND_Y_DROP)
                                               * LIST_PAGE_PITCH
                                             + slot_x + STAND_RIGHT_X_OFFSET,
                                         LIST_PAGE_PITCH, STAND_RIGHT_SPRITE);

                walk_frame = (int) ((data_fdps_timer_tick_counter
                                     / WALK_FRAME_TICKS) & WALK_FRAME_MASK);
                if (walk_frame == WALK_FRAME_FOLD_FROM) {
                    walk_frame = WALK_FRAME_FOLD_TO;
                }
                sprite_index = walk_frame
                               + roster_index * MEMBER_SPRITES_PER_CACHE_SLOT;
                icon_stream = data_fdps_cel_sprite_cache_ptr
                    + *(int *) (data_fdps_cel_sprite_cache_ptr
                                + sprite_index * CEL_SUB_IMAGE_ENTRY_BYTES);
                fdps_blit_dispatch(icon_stream,
                                   page + slot_y * LIST_PAGE_PITCH + slot_x,
                                   MEMBER_ICON_W, MEMBER_ICON_H,
                                   LIST_PAGE_PITCH, LIST_BLIT_OPERAND,
                                   LIST_BLIT_MODE);

                fdps_draw_text(data_fdps_all_game_text_ptr,
                               member->char_id + NAME_TEXT_ID_BIAS,
                               page + (slot_y + NAME_Y_OFFSET)
                                   * LIST_PAGE_PITCH
                                   + slot_x + NAME_X_OFFSET,
                               LIST_PAGE_PITCH, LIST_TEXT_FG_COLOR,
                               LIST_TEXT_BG_COLOR, LIST_TEXT_OUTLINE_COLOR);
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               target_class_id + CLASS_TEXT_ID_BIAS,
                               page + (slot_y + CLASS_Y_OFFSET)
                                   * LIST_PAGE_PITCH
                                   + slot_x + CLASS_X_OFFSET,
                               LIST_PAGE_PITCH, LIST_TEXT_FG_COLOR,
                               LIST_TEXT_BG_COLOR, LIST_TEXT_OUTLINE_COLOR);

                fdps_blit_command_sprite(page
                                             + (slot_y + LEVEL_PLATE_Y_DROP)
                                               * LIST_PAGE_PITCH
                                             + slot_x - LEVEL_PLATE_X_LIFT,
                                         LIST_PAGE_PITCH, LEVEL_PLATE_SPRITE);
                fdps_draw_number(page + (slot_y + LEVEL_PLATE_Y_DROP)
                                     * LIST_PAGE_PITCH
                                     + slot_x + LEVEL_X_OFFSET,
                                 LIST_PAGE_PITCH, (int) member->level,
                                 LEVEL_DIGITS, LEVEL_SHOW_PLUS);
            }
        }

        arrow_blink_phase = (int) ((data_fdps_timer_tick_counter
                                    / ARROW_BLINK_TICKS) & ARROW_BLINK_MASK);

        if (window_base != 0) {
            fdps_blit_command_sprite(page + ARROW_UP_PAGE_AT, LIST_PAGE_PITCH,
                                     arrow_blink_phase + ARROW_UP_SPRITE);
        }

        if (window_base + LIST_VISIBLE_SLOTS < candidate_count) {
            fdps_blit_command_sprite(page + ARROW_DOWN_PAGE_AT,
                                     LIST_PAGE_PITCH,
                                     arrow_blink_phase + ARROW_DOWN_SPRITE);
        }

        fdps_cycle_ui_palette();
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the present starts clear of it. */
        }
        fdps_blit_rect((unsigned int) (page + LIST_PANEL_PAGE_AT),
                       LIST_PAGE_PITCH,
                       (void *) (VGA_SCREEN_BASE + LIST_PANEL_SCREEN_AT),
                       VGA_SCREEN_PITCH, LIST_PANEL_W, LIST_PANEL_H);
        free(page);

        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    if (loop_result > LIST_RUNNING) {
        picked_candidate = cursor;
    } else {
        picked_candidate = LIST_CANCELLED;
    }

    return picked_candidate;
}

/* ------------------------------------------------------------------
 * fdps_church_promote_loop @ 000345a0
 * ------------------------------------------------------------------ */

/* The two parallel candidate arrays, ten ints each: the frame's SUB ESP,0x70
   leaves the roster indices at EBP-0x70 and the routes at EBP-0x48, both
   addressed with LEA ...,[EAX*0x4 + 0x0].  The sweep carries no bound of its
   own, but the tenth slot is unreachable and nine is the most that can ever be
   filled.  Only portrait ids 0 to 8 clear CMP EAX,0x9 / JL at 0003462e, and no
   two roster entries can carry the same portrait id: every one of the 24 calls
   to fdps_roster_add_character in the image enrols a distinct character -- the
   twelve chapter inits pass 0,6,4,1,3,2,8,9,7,5,0xb,0xa and the twelve in
   fdps_title_demo pass 0,1,8,9,2,0xa,3,0xb,4,0xc,5,6 -- and that function
   writes its one argument into both portrait_id (+0x07) and char_id (+0x08). */
#define PROMOTE_CANDIDATES_MAX 10

/* Who the church will promote.  CMP EAX,0x14 / JL at 0003461e is the level
   floor and CMP EAX,0x9 / JL at 0003462e is the form ceiling, and the second
   reads the PORTRAIT id at +0x07 rather than the class code at +0x20
   (rebuild_info/pitfalls.md). */
#define PROMOTE_MIN_LEVEL 0x14
#define PROMOTABLE_PORTRAIT_COUNT 9

/* The three badges and the route each buys, and the route every eligible
   member gets for nothing.  The 勇者徽章 arm is guarded by CMP byte ptr
   [EAX + 0x7],0x0 at 0003464c, so only 蘭迪斯 can take it however many of
   them another bag holds. */
#define ITEM_HERO_BADGE 0xdb
#define ITEM_LIGHT_BADGE 0xe0
#define ITEM_DARK_BADGE 0xe1
#define HERO_BADGE_PORTRAIT_ID 0
#define PROMOTE_ROUTE_NO_BADGE 0
#define PROMOTE_ROUTE_LIGHT_BADGE 1
#define PROMOTE_ROUTE_DARK_BADGE 2
#define PROMOTE_ROUTE_HERO_BADGE 3

/* Byte 2 of a route, the movement bonus, which this body reads and the
   transformation above does not. */
#define PROMOTION_ROUTE_MOVE 2

/* What fdps_unit_find_item_slot answers for a bag that is not carrying the item
   (src/unititem.h).  All six probes below test against it. */
#define NO_ITEM_SLOT (-1)

/* fdps_prompt_two_choice's affirmative.  1 is the right cell and -1 is a
   cancel, and this call site tests for 0 alone, so both of the others decline
   (src/msgwin.h). */
#define PROMOTE_ACCEPTED 0

/* The three text rows of the village text box, as offsets on top of
   VGA_SCREEN_BASE: 0xaa3d4, 0xabb94 and 0xad354, which are column 20 of rows
   131, 150 and 169.  They are positions on the adapter and not the addresses of
   anything the linker places, so they stay literals for the same reason the
   base does (contract E). */
#define MESSAGE_ROW_1_AT 0xa3d4
#define MESSAGE_ROW_2_AT 0xbb94
#define MESSAGE_ROW_3_AT 0xd354

/* The colour trio every one of the nine messages is drawn in: the standard
   glyph colour over no background with the standard outline, PUSH 0xd0 / PUSH 0
   / PUSH 0x6d ahead of each call.  They are this function's own literals and
   not the candidate list's. */
#define PROMOTE_TEXT_FG_COLOR 0xd0
#define PROMOTE_TEXT_BG_COLOR 0
#define PROMOTE_TEXT_OUTLINE_COLOR 0x6d

/* The nine entries of the resident Fdetxt00.txt table this screen draws: the
   refusal when nobody is eligible, the question, the announcement, and then one
   line per gain -- movement, AP, DP, DX, HP and MP, in the order they are paid
   out. */
#define NO_CANDIDATE_TEXT_ID 0x214
#define PROMOTE_ASK_TEXT_ID 0x215
#define PROMOTE_DONE_TEXT_ID 0x216
#define MOVE_GAIN_TEXT_ID 0x217
#define AP_GAIN_TEXT_ID 0x218
#define DP_GAIN_TEXT_ID 0x219
#define DX_GAIN_TEXT_ID 0x21a
#define HP_GAIN_TEXT_ID 0x21b
#define MP_GAIN_TEXT_ID 0x21c

/* The two substitution slots the question at 0x215 prints, and the biases that
   turn a form id and a class code into the message entry naming each.  They are
   the same two biases the candidate list draws its slots with -- one table and
   one pair of biases, not two that happen to agree -- but these are this
   function's own stores, MOV [0x00064030],EAX at 000347b7 and MOV
   [0x00064034],EAX at 000347dd. */
#define PROMOTE_NAME_TEXT_ID_BIAS 1
#define PROMOTE_CLASS_TEXT_ID_BIAS 0xa1

/* What the level is reset to once the new form's stats have been paid out: MOV
   byte ptr [EAX + 0x21],0x1 at 00034b94.  The promoted unit starts again at
   level 1 and climbs the new form's growth table from there. */
#define PROMOTED_LEVEL 1

/* fdps_village_animate_window_zoom's second argument (src/village.h): non-zero
   closes the window frame and zero opens it.  Every close here is a literal 1,
   MOV EAX,0x1 / PUSH EAX, and every open a literal 0, XOR EAX,EAX / PUSH
   EAX. */
#define WINDOW_CLOSE 1
#define WINDOW_OPEN 0

/* 000345a0.  One stack argument, caller-cleaned: the page is read at [EBP+0x14]
   behind the ordinary PUSH EBX/ESI/EDI/EBP and the return address, RET carries
   no immediate, and the sole call site at 00035c0b pushes the page, calls and
   then ADD ESP,0x4 without looking at EAX.

   THE SESSION IS ONE LOOP AND EVERY PASS STARTS OVER.  The candidate arrays are
   rebuilt from the roster on each pass rather than being filtered down, which is
   what drops a member who has just been promoted -- his level is 1 again -- and
   what re-reads the bag of a member whose badge was consumed.  The loop is
   top-tested on a flag only two arms set: an empty candidate list, and a cancel
   out of the picker.

   THE WINDOW IS CLOSED AND REOPENED AROUND EVERY MESSAGE PAGE, which is how the
   text box is cleared between pages -- nothing here paints the box background
   itself.  The pairs are (close, open) except at the two exits, where the close
   is the last thing that happens.

   THE ELIGIBILITY TEST IS ON THE PORTRAIT ID AND NOT THE CLASS CODE.  Writing
   the obvious `clazz < 9` lets 蓋亞, 珊 and 蘭斯洛特 through, and every
   already-promoted unit with them, and then indexes a RankUp.dat that holds
   nine records out of bounds (rebuild_info/pitfalls.md).

   THE STAT BONUSES ARE THE RAW *_max BYTES of the FRILEVUP.DAT row, which are
   one greater than the growth maximum the tables print, and they are added
   whole.  A rebuild whose parse normalised those bytes down by one would hand
   out one point less on every stat.

   THE GROWTH ROW IS THE ONE OF THE FORM BEING ENTERED, byte 0 of the chosen
   route -- not the form the member is leaving, and not the character id at +0x08
   the RankUp.dat record itself was fetched with.

   THE FIGURE EACH GAIN LINE PRINTS IS STAGED THROUGH A GLOBAL.  All six go into
   data_fdps_dialog_last_action_value_param first, and the record is then updated
   by reading that global back rather than the byte it came from -- its low byte
   for movement, MOV DL,byte ptr at 000349ac, and its low word for the five
   stats, MOV DX,word ptr at 00034a10 and after.  The global is a full dword
   (gamedata.h) and the narrowing is the reader's.

   The values used after a CALL are six.  fdps_get_unit_record's EAX is the
   record and is stored at [EBP-0xc] twice, once per sweep entry and once for the
   member being promoted.  fdps_unit_find_item_slot's EAX is a slot index or -1,
   tested against -1 in the sweep and kept at [EBP-0x10] for the removal.
   fdps_church_select_promote_candidate's EAX is the chosen array slot or -1, at
   [EBP-0x1c].  fdps_get_promotion_record's EAX is the character's twelve bytes,
   at [EBP-0x8].  fdps_prompt_two_choice's EAX is the answer, TEST EAX,EAX at
   0003480d.  fdps_get_growth_record's EAX is the new form's row, at [EBP-0x4].
   fdps_draw_text answers a pen position all nine call sites drop (ADD ESP,0x1c
   with no use of EAX) and memmove answers its destination, which nothing
   reads. */
void fdps_church_promote_loop(unsigned char *screen_page)
{
    /* The pass's candidate list: one roster index and one promotion route per
       entry, filled in step and handed to the picker as two arrays. */
    int candidate_unit_index[PROMOTE_CANDIDATES_MAX];
    int candidate_route[PROMOTE_CANDIDATES_MAX];
    /* 0 while the screen is still offering promotions, 1 once the player has
       cancelled or a pass has found nobody eligible. */
    int finished;
    /* How many entries the pass put in the two arrays. */
    int candidate_count;
    /* Which of those entries the picker answered with, or -1 for a cancel. */
    int chosen_slot;
    /* The roster index and the promotion route that entry names. */
    int chosen_unit_index;
    int chosen_route;
    /* The sweep's cursor over the roster. */
    int roster_index;
    /* Which inventory entry the badge being consumed sits in. */
    int badge_slot;
    /* The record the sweep is looking at, and then the record being promoted --
       one slot in the original, reused for both. */
    struct fdps_unit_record *member;
    /* The character's whole RankUp.dat record, and the three bytes of it the
       chosen route picks out.  Both are needed: the badge is consumed only when
       the route's form id differs from route 0's, which is byte 0 of the record
       itself. */
    struct fdps_promotion_record *promotion_record;
    unsigned char *promotion_route;
    /* The FRILEVUP.DAT row of the form being entered. */
    struct fdps_character_growth *growth;

    finished = 0;
    fdps_village_animate_window_zoom(screen_page, WINDOW_CLOSE);
    fdps_village_animate_window_zoom(screen_page, WINDOW_OPEN);

    while (finished == 0) {
        candidate_count = 0;

        for (roster_index = 0;
             roster_index < data_fdps_roster_member_count;
             roster_index++) {
            member = fdps_get_unit_record(roster_index);
            if (member->level >= PROMOTE_MIN_LEVEL
                && member->portrait_id < PROMOTABLE_PORTRAIT_COUNT) {
                candidate_unit_index[candidate_count] = roster_index;
                if (member->portrait_id == HERO_BADGE_PORTRAIT_ID
                    && fdps_unit_find_item_slot(roster_index, ITEM_HERO_BADGE)
                       != NO_ITEM_SLOT) {
                    candidate_route[candidate_count] =
                        PROMOTE_ROUTE_HERO_BADGE;
                } else if (fdps_unit_find_item_slot(roster_index,
                                                    ITEM_LIGHT_BADGE)
                           != NO_ITEM_SLOT) {
                    candidate_route[candidate_count] =
                        PROMOTE_ROUTE_LIGHT_BADGE;
                } else if (fdps_unit_find_item_slot(roster_index,
                                                    ITEM_DARK_BADGE)
                           != NO_ITEM_SLOT) {
                    candidate_route[candidate_count] =
                        PROMOTE_ROUTE_DARK_BADGE;
                } else {
                    candidate_route[candidate_count] = PROMOTE_ROUTE_NO_BADGE;
                }
                candidate_count++;
            }
        }

        if (candidate_count == 0) {
            fdps_draw_text(data_fdps_all_game_text_ptr, NO_CANDIDATE_TEXT_ID,
                           (unsigned char *) (VGA_SCREEN_BASE
                                              + MESSAGE_ROW_1_AT),
                           VGA_SCREEN_PITCH, PROMOTE_TEXT_FG_COLOR,
                           PROMOTE_TEXT_BG_COLOR, PROMOTE_TEXT_OUTLINE_COLOR);
            fdps_village_animate_window_zoom(screen_page, WINDOW_CLOSE);
            finished = 1;
        } else {
            chosen_slot = fdps_church_select_promote_candidate(
                              candidate_count, candidate_unit_index,
                              candidate_route);
            fdps_village_animate_window_zoom(screen_page, WINDOW_CLOSE);

            if (chosen_slot == LIST_CANCELLED) {
                finished = 1;
            } else {
                fdps_village_animate_window_zoom(screen_page, WINDOW_OPEN);
                chosen_unit_index = candidate_unit_index[chosen_slot];
                chosen_route = candidate_route[chosen_slot];
                member = fdps_get_unit_record(chosen_unit_index);
                promotion_record =
                    fdps_get_promotion_record((int) member->char_id);
                promotion_route = (unsigned char *) promotion_record
                                  + chosen_route * PROMOTION_ROUTE_BYTES;
                data_fdps_dialog_last_action_text_id_param =
                    (int) member->portrait_id + PROMOTE_NAME_TEXT_ID_BIAS;
                data_fdps_dialog_subst_text_id_2 =
                    (int) promotion_route[PROMOTION_ROUTE_CLASS]
                    + PROMOTE_CLASS_TEXT_ID_BIAS;
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               PROMOTE_ASK_TEXT_ID,
                               (unsigned char *) (VGA_SCREEN_BASE
                                                  + MESSAGE_ROW_1_AT),
                               VGA_SCREEN_PITCH, PROMOTE_TEXT_FG_COLOR,
                               PROMOTE_TEXT_BG_COLOR,
                               PROMOTE_TEXT_OUTLINE_COLOR);

                if (fdps_prompt_two_choice() == PROMOTE_ACCEPTED) {
                    fdps_village_animate_window_zoom(screen_page,
                                                     WINDOW_CLOSE);
                    fdps_church_promote_unit(chosen_unit_index, chosen_route);
                    memmove((void *) VGA_SCREEN_BASE, screen_page,
                            (size_t) VGA_SCREEN_BYTES);

                    if (chosen_route != PROMOTE_ROUTE_NO_BADGE
                        && promotion_record->default_portrait_id
                           != promotion_route[PROMOTION_ROUTE_PORTRAIT]) {
                        if (chosen_route == PROMOTE_ROUTE_HERO_BADGE) {
                            badge_slot =
                                fdps_unit_find_item_slot(chosen_unit_index,
                                                         ITEM_HERO_BADGE);
                        } else if (chosen_route
                                   == PROMOTE_ROUTE_LIGHT_BADGE) {
                            badge_slot =
                                fdps_unit_find_item_slot(chosen_unit_index,
                                                         ITEM_LIGHT_BADGE);
                        } else {
                            badge_slot =
                                fdps_unit_find_item_slot(chosen_unit_index,
                                                         ITEM_DARK_BADGE);
                        }
                        fdps_unit_remove_item(chosen_unit_index, badge_slot);
                    }

                    fdps_village_animate_window_zoom(screen_page, WINDOW_OPEN);
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   PROMOTE_DONE_TEXT_ID,
                                   (unsigned char *) (VGA_SCREEN_BASE
                                                      + MESSAGE_ROW_1_AT),
                                   VGA_SCREEN_PITCH, PROMOTE_TEXT_FG_COLOR,
                                   PROMOTE_TEXT_BG_COLOR,
                                   PROMOTE_TEXT_OUTLINE_COLOR);
                    data_fdps_dialog_last_action_value_param =
                        (int) promotion_route[PROMOTION_ROUTE_MOVE];
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   MOVE_GAIN_TEXT_ID,
                                   (unsigned char *) (VGA_SCREEN_BASE
                                                      + MESSAGE_ROW_2_AT),
                                   VGA_SCREEN_PITCH, PROMOTE_TEXT_FG_COLOR,
                                   PROMOTE_TEXT_BG_COLOR,
                                   PROMOTE_TEXT_OUTLINE_COLOR);
                    member->move += (unsigned char)
                        data_fdps_dialog_last_action_value_param;
                    fdps_village_animate_window_zoom(screen_page,
                                                     WINDOW_CLOSE);
                    fdps_village_animate_window_zoom(screen_page, WINDOW_OPEN);

                    growth = fdps_get_growth_record(
                        (int) promotion_route[PROMOTION_ROUTE_PORTRAIT]);

                    data_fdps_dialog_last_action_value_param =
                        (int) growth->ap_max;
                    member->ap_base += (short)
                        data_fdps_dialog_last_action_value_param;
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   AP_GAIN_TEXT_ID,
                                   (unsigned char *) (VGA_SCREEN_BASE
                                                      + MESSAGE_ROW_1_AT),
                                   VGA_SCREEN_PITCH, PROMOTE_TEXT_FG_COLOR,
                                   PROMOTE_TEXT_BG_COLOR,
                                   PROMOTE_TEXT_OUTLINE_COLOR);

                    data_fdps_dialog_last_action_value_param =
                        (int) growth->dp_max;
                    member->dp_base += (short)
                        data_fdps_dialog_last_action_value_param;
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   DP_GAIN_TEXT_ID,
                                   (unsigned char *) (VGA_SCREEN_BASE
                                                      + MESSAGE_ROW_2_AT),
                                   VGA_SCREEN_PITCH, PROMOTE_TEXT_FG_COLOR,
                                   PROMOTE_TEXT_BG_COLOR,
                                   PROMOTE_TEXT_OUTLINE_COLOR);

                    data_fdps_dialog_last_action_value_param =
                        (int) growth->dx_max;
                    member->dx_base += (short)
                        data_fdps_dialog_last_action_value_param;
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   DX_GAIN_TEXT_ID,
                                   (unsigned char *) (VGA_SCREEN_BASE
                                                      + MESSAGE_ROW_3_AT),
                                   VGA_SCREEN_PITCH, PROMOTE_TEXT_FG_COLOR,
                                   PROMOTE_TEXT_BG_COLOR,
                                   PROMOTE_TEXT_OUTLINE_COLOR);

                    fdps_village_animate_window_zoom(screen_page,
                                                     WINDOW_CLOSE);
                    fdps_village_animate_window_zoom(screen_page, WINDOW_OPEN);

                    data_fdps_dialog_last_action_value_param =
                        (int) growth->hp_max;
                    member->hp_current += (short)
                        data_fdps_dialog_last_action_value_param;
                    member->hp_max += (short)
                        data_fdps_dialog_last_action_value_param;
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   HP_GAIN_TEXT_ID,
                                   (unsigned char *) (VGA_SCREEN_BASE
                                                      + MESSAGE_ROW_1_AT),
                                   VGA_SCREEN_PITCH, PROMOTE_TEXT_FG_COLOR,
                                   PROMOTE_TEXT_BG_COLOR,
                                   PROMOTE_TEXT_OUTLINE_COLOR);

                    data_fdps_dialog_last_action_value_param =
                        (int) growth->mp_max;
                    member->mp_current += (short)
                        data_fdps_dialog_last_action_value_param;
                    member->mp_max += (short)
                        data_fdps_dialog_last_action_value_param;
                    fdps_draw_text(data_fdps_all_game_text_ptr,
                                   MP_GAIN_TEXT_ID,
                                   (unsigned char *) (VGA_SCREEN_BASE
                                                      + MESSAGE_ROW_2_AT),
                                   VGA_SCREEN_PITCH, PROMOTE_TEXT_FG_COLOR,
                                   PROMOTE_TEXT_BG_COLOR,
                                   PROMOTE_TEXT_OUTLINE_COLOR);

                    member->level = PROMOTED_LEVEL;
                    fdps_unit_recompute_combat_stats(chosen_unit_index);
                    fdps_village_animate_window_zoom(screen_page,
                                                     WINDOW_CLOSE);
                    fdps_village_animate_window_zoom(screen_page, WINDOW_OPEN);
                } else {
                    fdps_village_animate_window_zoom(screen_page,
                                                     WINDOW_CLOSE);
                    fdps_village_animate_window_zoom(screen_page, WINDOW_OPEN);
                }
            }
        }
    }
}
