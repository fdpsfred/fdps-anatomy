/* btlend.c -- the end of a battle: the win and fail tests, the sweep that
 * clears the map of enemies once the outcome is settled, and the window that
 * reports it.
 *
 * See btlend.h for what each entry point is asked and what its answer means.
 * Nothing here owns state: the battle units are reached through unit.h and
 * their count through gamedata.h.
 *
 * printf and exit come from <stdio.h> and <stdlib.h>, malloc and free from
 * <stdlib.h>, memset from <string.h> and inp from <conio.h>, which is where
 * Watcom 10.0a declares each of them.  All six are real calls in the original
 * -- CALL 0x00042deb and CALL 0x00042e0f at 00017d1d and 00017d27, CALL
 * 0x0003d375 at 00017d62, CALL 0x0003d478 at 00017d55, CALL 0x00042cd0 at
 * 00017d78 and CALL 0x0003d4e4 at 00017fff -- because the flag set carries no
 * -oi (rebuild_info/build_flags.md), so the plain declarations are what
 * reproduce them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "death.h"
#include "keybd.h"
#include "main.h"
#include "mapdraw.h"
#include "sprite.h"
#include "text.h"
#include "unit.h"
#include "vfs.h"
#include "btlend.h"

/* 00018350.  One walk over the battle unit array with two tests per unit and a
   counter.

   The bound is data_fdps_map_unit_count compared with CMP EAX,[0x00060150] /
   JL at 0001836d, so both the index and the count are signed and the test runs
   before the body: a count of 0 or below returns 0 without resolving a single
   record.

   The side test is the record byte at +6 widened by MOV AL / AND EAX,0xff at
   00018391 and then compared with the argument by CMP / JNZ at 00018399.  Zero
   extension is behaviour and not spelling: reading the side byte through a
   signed char would make a code of 0x80 or above compare as a negative number
   and never match the positive code a caller passes.  The comparison is an
   equality, so a side code is matched exactly and nothing is treated as a
   range or as a truth value.

   The retirement test is only reached when the side matched -- the JNZ at
   0001839c jumps past the CALL -- so a unit of another side is never handed to
   fdps_unit_is_retired.  The && below keeps that order and that short circuit.
   The call's answer is taken from EAX and tested against zero alone (TEST
   EAX,EAX / JZ at 000183aa), so the counter is bumped for a unit the flag
   reports as still in the battle.

   The record pointer is re-resolved on every iteration rather than stepped by
   0x50, which is what the original does and what keeps the walk correct across
   an array that has moved. */
int fdps_battle_count_remaining_units_on_side(int side)
{
    struct fdps_unit_record *unit;
    int unit_index;
    int remaining;

    remaining = 0;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if (unit->side == side && fdps_unit_is_retired(unit_index) == 0) {
            remaining = remaining + 1;
        }
    }
    return remaining;
}

/* 0003a2e0.  The outer gate, one walk and one unguarded defeat store, in that
   order.

   The gate is CMP dword ptr [0x00069da0],0x0 / JNZ 0003a39a at 0003a2ec: a
   non-zero code jumps straight to the epilogue, so nothing below runs and a
   verdict some chapter event already recorded survives untouched.  The compare
   is against zero for equality, so the code's declared unsignedness does not
   enter it.

   MOV dword ptr [0x00069da0],0x2 at 0003a2f9 writes the cleared verdict up
   front, before anything has been examined; the walk's job is to take it away
   again.

   The walk is bounded by CMP EAX,[0x00060150] / JL at 0003a30d, so index and
   count are both signed and the test runs before the body -- a count of 0 or
   below examines no record.  Its two tests are spelled out here rather than
   asked of unit.h: CMP byte ptr [EAX+0x6],0x0 / JNZ at 0003a331 reads the side
   byte directly, and MOV AL,byte ptr [EAX+0x5] / AND AL,0x1 / AND EAX,0xff at
   0003a33a reads bit 0 of the flags byte in place instead of calling
   fdps_unit_is_retired with the index.  The && below keeps that order and the
   short circuit the JNZ gives it.  There is no early exit: the loop always
   runs to the count, and because nothing ever puts the 2 back, a single live
   enemy anywhere in the array settles the code at 0.

   The defeat test at 0003a356 is reached by falling out of the loop and not by
   any branch that knows what the loop found, so it is sequential and not an
   else.  The chapter id decides which slot it asks about -- 0x10 and 0x15
   take PUSH 0x3 at 0003a368, everything else PUSH 0x0 at 0003a382 -- and both
   arms store 1 with no test of the current code, which is why a defeat
   outranks a victory recorded moments earlier.  Rewriting this as
   if (victory) ... else if (defeat) ... inverts exactly that case: when the
   last enemy falls in the same action that retires the watched unit, the
   original records 1 and the rewrite records 2.

   Both stores go to the same global, so the last one to run is the answer. */
void fdps_battle_check_default_end_conditions(void)
{
    struct fdps_unit_record *unit;
    int unit_index;

    if (data_fdps_chapter_event_or_battle_end_code != 0) {
        return;
    }

    data_fdps_chapter_event_or_battle_end_code = 2;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if (unit->side == 0 && (unit->flags & 1) == 0) {
            data_fdps_chapter_event_or_battle_end_code = 0;
        }
    }

    if (data_fdps_chapter_current_chapter_id == 0x10 ||
        data_fdps_chapter_current_chapter_id == 0x15) {
        if (fdps_unit_is_retired(3) != 0) {
            data_fdps_chapter_event_or_battle_end_code = 1;
        }
    } else {
        if (fdps_unit_is_retired(0) != 0) {
            data_fdps_chapter_event_or_battle_end_code = 1;
        }
    }
}

/* 00039e10.  One walk over the battle unit array with a single test per unit,
   and then one call that does the destroying.

   The bound is data_fdps_map_unit_count compared with CMP EAX,[0x00060150] /
   JL at 00039e26, so index and count are both signed and the test runs before
   the body: a count of 0 or below resolves no record at all.

   The side test is CMP byte ptr [EAX+0x6],0x0 / JNZ at 00039e4a, the record
   byte compared in place against zero -- the enemy side, against 1 for the
   third side and 2 for the player's.  It is the only test: the flags byte is
   never read here, so a retired enemy's record is written exactly like a
   standing one's.

   THE STORE IS 16 BITS WIDE.  MOV word ptr [EAX+0x40],0x0 at 00039e53 clears
   the current hit-point word and stops there, and maximum HP is the word
   immediately behind it at +0x42.  The chapter-end handlers that call this
   then run restore-current-from-maximum passes -- fdps_chapter_24_end does one
   explicitly -- so a store widened to an int would bring every enemy back with
   a maximum of zero (rebuild_info/pitfalls.md).

   The CALL at 00039e5b is unconditional and sits outside the loop, so it runs
   once whatever the walk found, including when it found nothing.  It is what
   actually removes the units this loop has just brought to zero: it collects
   every unit that is not retired and whose hit-point word is 0, spins them,
   sets each one's flags byte to 1 and plays Explo.Saf over their tiles
   (death.h).  This loop only sets up the condition that call reads, which is
   why the order of the two is the whole behaviour and not a detail.

   The record pointer is re-resolved on every iteration rather than stepped by
   0x50, which is what the original does and what keeps the walk correct across
   an array that has moved. */
void fdps_battle_destroy_remaining_enemies(void)
{
    /* The record the walk is on, [EBP-0x4]. */
    struct fdps_unit_record *unit;
    /* Which unit the walk is on, [EBP-0x8]. */
    int unit_index;

    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if (unit->side == 0) {
            unit->hp_current = 0;
        }
    }
    fdps_play_death_animation_and_mark_dead();
}

/* The container the panel's artwork and the nine data tables come out of, and
   the member the artwork is.  Both are literals in the original -- MOV
   EAX,0x60128 at 00017d00 and MOV EAX,0x615f4 at 00017d3f -- and both have to
   live in writable storage, because the lookup upper-cases the CALLER'S buffer
   in place before it compares (vfs.h, rebuild_info/pitfalls.md). */
#define WIN_FAIL_ARCHIVE "MISC.VFS"
#define WIN_FAIL_SHEET "WinFail.Cel"

/* The panel page: 209 x 133 at a pitch of 209, which is the 0xd1 pushed as
   every destination stride below and the 0x6c95 pushed to malloc and memset at
   00017d5d and 00017d6d. */
#define PANEL_PITCH 0xd1
#define PANEL_BYTES 0x6c95

/* The scene the panel is composited over: a whole 360 x 240 page, allocated
   and freed inside every frame, which is the 0x15180 pushed to malloc at
   00017ee9 and the 0x168 pushed as its stride. */
#define SCENE_PITCH 0x168
#define SCENE_BYTES 0x15180

/* Where the panel sits inside that page: ADD EDX,0x4c for the column and ADD
   EAX,0x14 before the IMUL by the stride for the row, at 00017fb5 and
   00017fac.  The scene's own 24-row, 24-column apron is not subtracted here --
   the panel is placed against the page, not against the presented window. */
#define PANEL_IN_SCENE_X 0x4c
#define PANEL_IN_SCENE_Y 0x14

/* The mode 13h aperture and the window each frame is presented through: 312 x
   192 taken from 0x18 rows and 0x18 columns into the page -- the 0x21d8 added
   at 00018038 -- and put 4 pixels into the screen, the 0x504 inside the
   0xa0504 pushed at 0001802b.  0xa0000 is written as a literal because it is
   the adapter's real linear address under DOS/4GW and not a symbol the rebuild
   places anywhere (rebuild_info/pitfalls.md, contract E). */
#define SCENE_WINDOW_AT 0x21d8
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_WINDOW_AT 0x504
#define VIEW_WIDTH 0x138
#define VIEW_ROWS 0xc0

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, which is what each frame's present straddles. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The panel is drawn as four independent strips so that each can slide in at
   its own speed; their x, source row, width and height are the four four-entry
   tables at 0x1498c, 0x1499c, 0x149ac and 0x149bc. */
#define PANEL_STRIPS 4

/* Frames in each slide, and the rows the two keyframe tables hold per strip:
   CMP dword ptr [EBP-0xc],0x13 / JL at 00017ed6 and CMP dword ptr
   [EBP-0xc],0xd / JL at 000181b9, both SIGNED, and the IMUL by 0x4c and by
   0x34 at 00017f1f and 00018202 that pick a strip's row out of each table. */
#define SLIDE_IN_FRAMES 19
#define SLIDE_OUT_FRAMES 13

/* The row the slide-out stops the panel against: CMP EAX,0xc8 / JLE at
   00018233.  It is a row of the panel window and not of the screen, so it is
   not the same 200 the adapter has. */
#define SLIDE_OUT_BOTTOM 0xc8

/* A scancode of 0x80 or above keeps the panel up.  CMP EAX,0x7f / JLE at
   00018079, over the byte the queue handed back widened by AND EAX,0xff -- so
   the test is against 0x7f and not against the queue-empty marker 0xff, which
   is why an empty queue holds the panel open and any make code dismisses it
   (btlend.h). */
#define SCANCODE_HELD_THRESHOLD 0x7f

/* Where each number is stamped into the panel, as a page offset, and how many
   digits wide its field is.  The offsets are the literals the assembly adds to
   the page pointer; the (column, row) each comes to at a pitch of 209 is in
   the comment beside it. */
#define CHAPTER_NUMBER_AT (4 * PANEL_PITCH + 85)   /* (85, 4)    */
#define TURN_NUMBER_AT (4 * PANEL_PITCH + 131)     /* (131, 4)   */
#define GOLD_NUMBER_AT (121 * PANEL_PITCH + 87)    /* (87, 121)  */
#define SIDE_0_COUNT_AT (104 * PANEL_PITCH + 47)   /* (47, 104)  */
#define SIDE_2_COUNT_AT (104 * PANEL_PITCH + 119)  /* (119, 104) */
#define SIDE_1_COUNT_AT (104 * PANEL_PITCH + 174)  /* (174, 104) */
#define CHAPTER_NUMBER_DIGITS 2
#define TURN_NUMBER_DIGITS 3
#define GOLD_NUMBER_DIGITS 8
#define UNIT_COUNT_DIGITS 2

/* The two conditions, as entries of the chapter's own text block, and where
   each is drawn.  fdps_draw_text's three colours are 0xd0 / 0 / 0x6d, the
   standard message colours (text.h). */
#define WIN_CONDITION_TEXT_ID 2
#define FAIL_CONDITION_TEXT_ID 3
#define WIN_CONDITION_AT (22 * PANEL_PITCH + 35)   /* (35, 22) */
#define FAIL_CONDITION_AT (59 * PANEL_PITCH + 35)  /* (35, 59) */
#define CONDITION_FG_COLOR 0xd0
#define CONDITION_BG_COLOR 0
#define CONDITION_OUTLINE_COLOR 0x6d

/* The three sides the panel tallies, in the order it asks about them: the
   player's own line first, then the two others (btlend.h). */
#define TALLY_SIDE_FIRST 0
#define TALLY_SIDE_SECOND 2
#define TALLY_SIDE_THIRD 1

/* 00017ca0.  Takes nothing, returns nothing -- the one call site at 0001506a
   pushes nothing and cleans nothing, and never looks at EAX.

   The six tables are automatic arrays with initialisers and not statics: the
   assembly copies each one out of rodata into the frame on entry (REP MOVSD
   with ECX 0x4c at 00017cbc, four unrolled MOVSDs each for the four small
   ones, REP MOVSD with ECX 0x34 at 00017cfe), which is the inline expansion of
   an initialised auto array.  Nothing writes to them, so the copy costs
   nothing observable, but spelling them as file-scope statics would take the
   copies away.

   THE DATA TABLES ARE RELOADED ON EVERY OPEN AND THE OLD SET IS LEAKED.
   fdps_load_data_tables overwrites the nine global table pointers with fresh
   allocations and nothing frees what was there, so each visit to this panel
   leaks the previous set.  Hoisting the call out of the function, or adding
   frees, changes behaviour (rebuild_info/pitfalls.md).

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED, the same contract
   the animation players in anim.c carry: last_tick is read at 00018046 before
   anything has written it, so the first frame of the slide-in ends its wait at
   once unless the stack garbage happens to equal the counter, and the hold and
   the slide-out then inherit whatever the phase before them left in it.
   Latching the counter before each phase adds a tick to each.

   THE TWO SLIDES CLIP DIFFERENTLY AND CANNOT SHARE A HELPER.  On the way in a
   negative destination row is clipped against the top of the window by moving
   the SOURCE down by -y and shortening the strip by the same amount (the NEG
   at 00017f5d and the ADD at 00017f6e); on the way out the strip is only
   shortened, against row 200, and the source is not moved (00018233).  Folding
   them together draws the wrong rows.

   THE HOLD LOOP TESTS ITS SCANCODE AT THE TOP and against 0x80, not against
   the queue-empty marker: fdps_flush_keyboard_queue empties the ring first, so
   the first read reports 0xff, which is above the threshold and keeps the
   panel up.  A key RELEASE code -- also above 0x80 -- keeps it up as well, and
   only a make code dismisses it.

   Nothing that comes back from a CALL is tested.  Neither malloc is checked,
   fdps_vfs_load_file_or_exit and fdps_load_data_tables return nothing,
   fdps_draw_text's cursor is discarded, and the three tallies go straight into
   fdps_draw_number's value argument.  fdps_vfs_open's handle is the one answer
   that is tested, and its failure arm ends the process. */
void fdps_battle_show_win_fail_window(void)
{
    /* Where each strip's top row sits on each of the 19 slide-in frames, one
       row of 19 per strip; a negative row is above the window and is clipped.
       0x0001485c. */
    int slide_in_y[PANEL_STRIPS][SLIDE_IN_FRAMES] = {
        { -8, 8, 20, 45, 75, 110, 156, 157, 156, 156,
          156, 156, 156, 156, 156, 156, 156, 156, 156 },
        { -16, -16, -16, -16, -8, 10, 30, 55, 85, 110,
          139, 140, 139, 139, 139, 139, 139, 139, 139 },
        { -90, -90, -90, -90, -90, -90, -90, -90, -80, -70,
          -50, -30, -10, 15, 56, 57, 56, 56, 56 },
        { -16, -16, -16, -16, -16, -16, -16, -16, -16, -16,
          -16, -16, -16, -8, 0, 15, 39, 40, 39 }
    };
    /* Each strip's column in the panel page.  0x0001498c. */
    int strip_x[PANEL_STRIPS] = { 0x45, 0, 0, 0x37 };
    /* Each strip's own top row in the panel page, which is where the strip is
       read from and is unrelated to where it is drawn.  0x0001499c. */
    int strip_src_y[PANEL_STRIPS] = { 0x75, 0x64, 0x11, 0 };
    /* Each strip's width and full height in the page.  0x000149ac and
       0x000149bc. */
    int strip_width[PANEL_STRIPS] = { 0x47, 0xd1, 0xd1, 0x63 };
    int strip_height[PANEL_STRIPS] = { 0x10, 0x10, 0x52, 0x10 };
    /* The same per-strip rows for the 13 slide-out frames, all of them
       positive and clipped against row 200 instead.  0x000149cc. */
    int slide_out_y[PANEL_STRIPS][SLIDE_OUT_FRAMES] = {
        { 157, 160, 170, 195, 200, 200, 200, 200, 200, 200, 200, 200, 200 },
        { 139, 139, 140, 141, 145, 150, 170, 190, 200, 200, 200, 200, 200 },
        { 56, 56, 56, 56, 57, 60, 70, 90, 120, 160, 200, 200, 200 },
        { 39, 39, 39, 39, 39, 39, 43, 48, 60, 80, 110, 150, 190 }
    };
    /* The container handle, held only long enough to take two things out of
       it. */
    void *archive;
    /* WinFail.Cel as loaded, released as soon as its sprite is on the page. */
    unsigned char *panel_sheet;
    /* The finished 209 x 133 panel, drawn once and then slid about. */
    unsigned char *panel;
    /* The frame being composed, allocated and freed inside every frame. */
    unsigned char *scene;
    /* Which frame of the slide being played. */
    int frame;
    /* Which of the four strips is being placed. */
    int strip;
    /* That strip's top row in the panel window on this frame, after clipping. */
    int strip_y;
    /* How many of its rows survive the clip. */
    int strip_rows;
    /* How far down its source is moved when the top of it is cut off. */
    int top_clip;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    archive = fdps_vfs_open(WIN_FAIL_ARCHIVE);
    if (archive == NULL) {
        printf("file not found: 'Misc.vfs'\n");
        exit(1);
    }

    fdps_load_data_tables(archive);
    fdps_vfs_load_file_or_exit(archive, WIN_FAIL_SHEET, (void **) &panel_sheet);
    free(archive);

    panel = (unsigned char *) malloc((size_t) PANEL_BYTES);
    memset(panel, 0, (size_t) PANEL_BYTES);
    fdps_cel_blit_sprite(panel_sheet, 0, panel, PANEL_PITCH, 0, 0, 0, 0);
    free(panel_sheet);

    fdps_draw_number(panel + CHAPTER_NUMBER_AT, PANEL_PITCH,
                     data_fdps_chapter_current_chapter_id + 1,
                     CHAPTER_NUMBER_DIGITS, 0);
    fdps_draw_number(panel + TURN_NUMBER_AT, PANEL_PITCH,
                     data_fdps_battle_turn_counter, TURN_NUMBER_DIGITS, 0);
    fdps_draw_number(panel + GOLD_NUMBER_AT, PANEL_PITCH,
                     data_fdps_shared_party_total_gold, GOLD_NUMBER_DIGITS, 0);
    fdps_draw_number(panel + SIDE_0_COUNT_AT, PANEL_PITCH,
                     fdps_battle_count_remaining_units_on_side(
                         TALLY_SIDE_FIRST),
                     UNIT_COUNT_DIGITS, 0);
    fdps_draw_number(panel + SIDE_2_COUNT_AT, PANEL_PITCH,
                     fdps_battle_count_remaining_units_on_side(
                         TALLY_SIDE_SECOND),
                     UNIT_COUNT_DIGITS, 0);
    fdps_draw_number(panel + SIDE_1_COUNT_AT, PANEL_PITCH,
                     fdps_battle_count_remaining_units_on_side(
                         TALLY_SIDE_THIRD),
                     UNIT_COUNT_DIGITS, 0);

    fdps_draw_text(data_fdps_current_chapter_text_ptr, WIN_CONDITION_TEXT_ID,
                   panel + WIN_CONDITION_AT, PANEL_PITCH, CONDITION_FG_COLOR,
                   CONDITION_BG_COLOR, CONDITION_OUTLINE_COLOR);
    fdps_draw_text(data_fdps_current_chapter_text_ptr, FAIL_CONDITION_TEXT_ID,
                   panel + FAIL_CONDITION_AT, PANEL_PITCH, CONDITION_FG_COLOR,
                   CONDITION_BG_COLOR, CONDITION_OUTLINE_COLOR);

    for (frame = 0; frame < SLIDE_IN_FRAMES; frame++) {
        scene = (unsigned char *) malloc((size_t) SCENE_BYTES);
        fdps_draw_scene_layers(scene);

        for (strip = 0; strip < PANEL_STRIPS; strip++) {
            strip_y = slide_in_y[strip][frame];
            strip_rows = strip_height[strip];
            top_clip = 0;
            if (strip_y < 0) {
                top_clip = -strip_y;
                strip_rows = strip_height[strip] + strip_y;
                strip_y = 0;
            }
            if (strip_rows > 0) {
                fdps_blit_rect(
                    (unsigned int) (panel
                                    + (strip_src_y[strip] + top_clip)
                                      * PANEL_PITCH
                                    + strip_x[strip]),
                    PANEL_PITCH,
                    scene + (strip_y + PANEL_IN_SCENE_Y) * SCENE_PITCH
                          + strip_x[strip] + PANEL_IN_SCENE_X,
                    SCENE_PITCH, strip_width[strip], strip_rows);
            }
        }

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the present starts clear of it. */
        }
        fdps_blit_rect((unsigned int) (scene + SCENE_WINDOW_AT), SCENE_PITCH,
                       (void *) (VGA_SCREEN_BASE + VGA_WINDOW_AT),
                       VGA_SCREEN_PITCH, VIEW_WIDTH, VIEW_ROWS);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
        free(scene);
    }

    fdps_flush_keyboard_queue();

    while (fdps_read_keyboard_queue() > SCANCODE_HELD_THRESHOLD) {
        scene = (unsigned char *) malloc((size_t) SCENE_BYTES);
        fdps_draw_scene_layers(scene);
        /* The four strips at rest, written out as the original writes them:
           the resting rows are the last column of the slide-in table, so
           nothing here reads a table at all. */
        fdps_blit_rect((unsigned int) (panel + 0 * PANEL_PITCH + 55),
                       PANEL_PITCH,
                       scene + (39 + PANEL_IN_SCENE_Y) * SCENE_PITCH
                             + 55 + PANEL_IN_SCENE_X,
                       SCENE_PITCH, 99, 16);
        fdps_blit_rect((unsigned int) (panel + 17 * PANEL_PITCH + 0),
                       PANEL_PITCH,
                       scene + (56 + PANEL_IN_SCENE_Y) * SCENE_PITCH
                             + 0 + PANEL_IN_SCENE_X,
                       SCENE_PITCH, 209, 82);
        fdps_blit_rect((unsigned int) (panel + 100 * PANEL_PITCH + 0),
                       PANEL_PITCH,
                       scene + (139 + PANEL_IN_SCENE_Y) * SCENE_PITCH
                             + 0 + PANEL_IN_SCENE_X,
                       SCENE_PITCH, 209, 16);
        fdps_blit_rect((unsigned int) (panel + 117 * PANEL_PITCH + 69),
                       PANEL_PITCH,
                       scene + (156 + PANEL_IN_SCENE_Y) * SCENE_PITCH
                             + 69 + PANEL_IN_SCENE_X,
                       SCENE_PITCH, 71, 16);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }
        fdps_blit_rect((unsigned int) (scene + SCENE_WINDOW_AT), SCENE_PITCH,
                       (void *) (VGA_SCREEN_BASE + VGA_WINDOW_AT),
                       VGA_SCREEN_PITCH, VIEW_WIDTH, VIEW_ROWS);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
        free(scene);
    }

    for (frame = 0; frame < SLIDE_OUT_FRAMES; frame++) {
        scene = (unsigned char *) malloc((size_t) SCENE_BYTES);
        fdps_draw_scene_layers(scene);

        for (strip = 0; strip < PANEL_STRIPS; strip++) {
            strip_y = slide_out_y[strip][frame];
            strip_rows = strip_height[strip];
            if (strip_y + strip_rows > SLIDE_OUT_BOTTOM) {
                strip_rows = SLIDE_OUT_BOTTOM - strip_y;
            }
            if (strip_rows > 0) {
                fdps_blit_rect(
                    (unsigned int) (panel
                                    + strip_src_y[strip] * PANEL_PITCH
                                    + strip_x[strip]),
                    PANEL_PITCH,
                    scene + (strip_y + PANEL_IN_SCENE_Y) * SCENE_PITCH
                          + strip_x[strip] + PANEL_IN_SCENE_X,
                    SCENE_PITCH, strip_width[strip], strip_rows);
            }
        }

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }
        fdps_blit_rect((unsigned int) (scene + SCENE_WINDOW_AT), SCENE_PITCH,
                       (void *) (VGA_SCREEN_BASE + VGA_WINDOW_AT),
                       VGA_SCREEN_PITCH, VIEW_WIDTH, VIEW_ROWS);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
        free(scene);
    }

    fdps_flush_keyboard_queue();
    free(panel);
}
