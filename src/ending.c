/* ending.c -- the ending: one animated epilogue card for each member of the
 * party roster, and the "End" movie the last card hands the screen to.
 *
 * See ending.h for what the entry point promises its caller.  This file holds
 * no state of its own: everything it touches belongs to another module or to
 * the machine.  The screens on either side of the ending -- the title menu,
 * its attract-mode demo and the game-over screen -- are title.c's, and so is
 * the movie player this file calls out to.
 *
 * sprintf, fopen, fread and fclose come from <stdio.h>, memset from
 * <string.h>, inp from <conio.h> and malloc and free from <stdlib.h>, and
 * every one of them is a real library call in the image rather than an inline
 * expansion -- CALL 00042d41, CALL 0004265e, CALL 0004270d, CALL 000428be,
 * CALL 00042cd0, CALL 0003d4e4, CALL 0003d375 and CALL 0003d478 -- because the
 * flag that would inline the string and character routines, -oi, is not in
 * this build's set (rebuild_info/build_flags.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "cdaudio.h"
#include "palette.h"
#include "saf.h"
#include "sprite.h"
#include "table.h"
#include "text.h"
#include "vfs.h"
#include "title.h"
#include "ending.h"

/* The VGA graphics aperture as a flat linear address, the size of one whole
   mode 13h frame, and that frame's shape as the blitters address it -- 320
   bytes to the row and 200 rows, PUSH 0x140 / PUSH 0xc8 at 0001bd76 and
   0001bd6c and again at the four sibling presentations.  All of them are
   hard-coded in the original (PUSH 0xfa00 / PUSH 0xa0000 at 0001ba76 and
   0001ba7d for the opening clear) and stay literals here: 0xa0000 is where the
   display adapter answers, not the address of anything the linker places, so
   there is no symbol to reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_BYTES 0xfa00
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit this file looks at: TEST AL,0x8 against
   inp's answer at 0001bd57 and its nine siblings. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The whole DAC, which is the range both palette uploads cover: PUSH 0x0 /
   PUSH 0xff for the first and last entry at 0001bb1d and 0001bb1b, and again
   at 0001c1d5 and 0001c1d0.  The bound is inclusive -- fdps_set_palette_range
   uploads first_entry..last_entry -- so the last entry is 255 and not 256. */
#define VGA_DAC_FIRST_ENTRY 0
#define VGA_DAC_LAST_ENTRY 0xff

/* The blit mode a translucent draw asks for and the three-dword descriptor it
   is handed, whose slots rleblend.h is the canon for: [0] the shade ramp base,
   [1] the blend level and [2] the inverse colour cube base.  MOV [EBP-0x70],0x9
   at 0001bfc9 and 0001c0f7 asks for the mode and MOV [EBP-0x70],0x0 at 0001bfef
   puts it back; the descriptor is the [EBP-0x58], [EBP-0x54], [EBP-0x50] run
   filled at 0001baf6 and 0001bafd.  Same names and same layout as sprite.c,
   which builds one of these per layer. */
#define BLIT_MODE_OPAQUE 0
#define BLIT_MODE_TRANSLUCENT 9
#define BLEND_DESC_SHADE_RAMP 0
#define BLEND_DESC_LEVEL 1
#define BLEND_DESC_CUBE 2
#define BLEND_DESC_DWORDS 3

/* PUSH 0x1 / CALL fdps_cd_set_music_track at 0001ba53.  The game's own music
   index and not a track number: the drive is asked for the track above it
   (cdaudio.h). */
#define CREDIT_MUSIC_INDEX 1

/* CMP dword ptr [0x00069cf4],0x1d / JNZ at 0001ba5d and CMP [0x00069cf4],0x1a
   at 0001bb4a.  The two chapter indices that reach here, 0-based: 0x1d is
   chapter 30 and 0x1a is chapter 27. */
#define CREDIT_LAST_CHAPTER_ID 0x1d
#define CREDIT_CHAPTER_27_ID 0x1a

/* MOV dword ptr [EBP-0x30],0x20 at 0001ba66 against MOV [EBP-0x30],0x19 at
   0001ba6f: which block of the chapter text the epilogue lines are in.  Member
   i takes entry base + i, ADD EAX,[EBP-0x2c] at 0001c040. */
#define CREDIT_LAST_CHAPTER_TEXT_BASE 0x20
#define CREDIT_TEXT_BASE 0x19

/* CMP dword ptr [EBP-0x2c],0x3 / JZ / MOV [EBP-0x2c],0x4 at 0001bb53: on the
   chapter 27 ending the loop counter is moved from 3 to 4 rather than
   incremented, so roster slot 3 gets no card and caption index base + 3 is
   never drawn. */
#define CREDIT_SKIPPED_ROSTER_SLOT 3
#define CREDIT_SKIP_TO_ROSTER_SLOT 4

/* MOV dword ptr [EBP-0x8],0xc at 0001bb89, the sprite id used on the one pass
   past the end of the roster.  Class 12, and it is not read out of any
   record. */
#define CREDIT_EXTRA_PASS_SPRITE_ID 12

/* The containers and the three name formats, from the pushes at 0001bbaa,
   0001bbed and 0001bc4f and the format strings at 0x616b4, 0x616c4 and
   0x616d0.  The backdrop id is the roster index times three -- LEA
   EAX,[EAX+EAX*2] at 0001bc35 -- so consecutive members stand on different
   terrain. */
#define CREDIT_FIGHT_ARCHIVE "Fight.vfs"
#define CREDIT_FIGACT_ARCHIVE "FigAct.vfs"
#define CREDIT_BACKDROP_ARCHIVE "BackGrnd.vfs"
#define CREDIT_STAND_CLIP_FORMAT "Stand%03d.saf"
#define CREDIT_ACT_CLIP_FORMAT "Act%03d.saf"
#define CREDIT_BACKDROP_CLIP_FORMAT "Back%02d.saf"
#define CREDIT_BACKDROP_ID_STEP 3

/* The name buffer is twenty bytes: LEA EAX,[EBP-0x6c] against a frame whose
   next slot is the blend descriptor at [EBP-0x58].  The longest name any of
   the three formats can produce from a byte is "Stand255.saf". */
#define CREDIT_CLIP_NAME_BYTES 20

/* The off-screen page and the window taken out of it: PUSH 0x16480 at
   0001bc60, MOV [EBP-0x8c],0x170 and MOV [EBP-0x88],0xf8 at 0001bc73, the two
   MOV ...,0x18 at 0001bc95 for the resting origin, ADD EAX,0x2298 at 0001bd8b
   for the presented window and ADD EAX,0x2578 at 0001c037 for the caption's
   origin.  It is the same 368x248 page with a 24-pixel margin that saf.h's
   player and fdps_show_game_over use, so the window is page row 24, column 24
   and the caption lands at page row 26, column 24 -- screen row 2, column
   0. */
#define CREDIT_PAGE_PITCH 0x170
#define CREDIT_PAGE_ROWS 0xf8
#define CREDIT_PAGE_BYTES 0x16480
#define CREDIT_PAGE_MARGIN 0x18
#define CREDIT_PAGE_WINDOW_AT 0x2298
#define CREDIT_PAGE_CAPTION_AT 0x2578

/* The five stages' counters.  The slide runs 8 down to 0 (MOV [EBP-0x34],0x8
   at 0001bca6 with CMP ...,0x0 / JGE and DEC), the hold 0 up to 0x31 (CMP
   ...,0x32 / JL at 0001bdc9), the caption 0 up to 0xfa inclusive (CMP ...,0xfa
   / JLE at 0001bf89) and the fade 0 up to 8 inclusive (CMP ...,0x8 / JLE at
   0001c0c0).  Only the caption's first eight frames still draw the backdrop,
   CMP [EBP-0x34],0x8 / JGE at 0001bfb5.

   The two travelling stages step by 0x14 rows and 0xf columns per frame (IMUL
   at 0001bced and 0001bd24), so at step 8 the backdrop starts 160 rows below
   its resting place and the standing sprite 120 columns to the right of it.
   Both dissolves step the blend level by two per frame (ADD EAX,EAX at
   0001bfbe and 0001c0ec), so eight frames reach level 14 and the ninth level
   16, which is invisible (rleblend.h). */
#define CREDIT_SLIDE_FIRST_STEP 8
#define CREDIT_SLIDE_BACKDROP_STEP_Y 0x14
#define CREDIT_SLIDE_SPRITE_STEP_X 0xf
#define CREDIT_HOLD_FRAMES 0x32
#define CREDIT_CAPTION_LAST_FRAME 0xfa
#define CREDIT_CAPTION_BACKDROP_FRAMES 8
#define CREDIT_FADE_LAST_STEP 8
#define CREDIT_BLEND_LEVEL_STEP 2

/* PUSH 0x6d / PUSH 0x0 / PUSH 0xd0 at 0001c023: the standard message colours,
   foreground, background and outline in the order fdps_draw_text takes them
   (text.h).  A background of zero leaves the glyph cell unfilled, so the
   caption is drawn over the picture rather than into a box. */
#define CREDIT_CAPTION_FG_COLOR 0xd0
#define CREDIT_CAPTION_BG_COLOR 0
#define CREDIT_CAPTION_OUTLINE_COLOR 0x6d

/* mode 1 of fdps_saf_advance_tick (saf.h) is the reset that zeroes a cursor's
   frame and tick counters; the image slot is filled in first. */
#define CREDIT_SAF_CURSOR_RESET 1

/* Byte +4 of a .SAF frame record, the travelling-attack lead-in count
   (resource_info/saf.md).  It is read off frame 0 of the Act clip here and
   nothing in this function ever looks at it again -- see the note below. */
#define CREDIT_SAF_FRAME_LEAD_IN_OFFSET 4

/* The two blend tables the ending composites through, read straight off disk
   over the globals gamedata.h owns: PUSH 0x4800 at 0001baa5 for the shade ramp
   and PUSH 0x1000 at 0001bae1 for the inverse-palette cube, each with an
   element size of 1.  The ending pair goes in on the way in and the map pair
   comes back on the way out; all four names are bare, so they are looked for
   in the working directory. */
#define CREDIT_SHADE_RAMP_BYTES 0x4800
#define CREDIT_PALETTE_CUBE_BYTES 0x1000
#define CREDIT_ENDING_SHADE_RAMP_FILE "FMer1.tmp"
#define CREDIT_ENDING_PALETTE_CUBE_FILE "FMer2.tmp"
#define CREDIT_MAP_SHADE_RAMP_FILE "Mer1.tmp"
#define CREDIT_MAP_PALETTE_CUBE_FILE "Mer2.tmp"
#define CREDIT_BLEND_TABLE_MODE "rb"

/* PUSH 0x61768 at 0001c25d.  The stem of the pair of stream files the ending
   movie is in; fdps_play_movie composes the paths around it. */
#define CREDIT_MOVIE_NAME "End"

/* 0001ba40.  Plays the whole ending and returns.  The frame is the plain -4s
   one -- PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x90 -- with no argument
   read anywhere in the body and nothing returned: both call sites, CALL
   0x0001ba40 at 0003b97c and at 0003bab2, push nothing before it, clean
   nothing after it and overwrite EAX with the next instruction.

   VALUES USED AFTER A CALL, and what the assembly says each one is.  fopen's
   EAX is stored at [EBP-0x4] four times and is the handle the matching fread
   and fclose are given; none of the four is tested, so a missing blend table
   faults inside fread rather than being reported.  fdps_vfs_load_entry's EAX
   is the loaded clip three times -- into [EBP-0x10] and [EBP-0x38] at 0001bbb8
   for the standing clip, into [EBP-0x1c] and [EBP-0x44] at 0001bbfb for the
   action clip, and into [EBP-0x18] at 0001bc5d for the backdrop -- and the
   pair of stores is what puts a clip both in a variable this function frees
   and in the playback cursor's image slot.  malloc's EAX at 0001bc6d goes
   straight into the request's element 0 and is re-read out of that slot every
   time the page is needed.  fdps_get_roster_record's EAX at 0001bb79 is the
   record the portrait byte comes out of.  fdps_saf_get_frame's EAX at 0001bc24
   is dereferenced once, at +4, into a local nothing reads.  Only the action
   stage looks at fdps_saf_advance_tick's answer -- MOV [EBP-0x14],EAX at
   0001bf06 -- and that is the loop's only exit; the other seven calls discard
   it.  inp's is tested for bit 3 at each of the ten retrace spins.  Every
   other callee returns nothing the original reads, fdps_draw_text's cursor
   included.

   CONTROL FLOW.  Straight line to 0001bb2b, then one counted loop over the
   roster with five loops inside it.  The outer bound is signed and INCLUSIVE,
   CMP EAX,[0x00064114] / JLE at 0001bb35, which is what makes the pass past
   the end of the roster; the counter is written rather than incremented on the
   chapter 27 skip, so the pass that follows the skip is slot 4 and the
   increment at the bottom then takes it to 5.  The four counted inner loops
   have their compares at the top and their steps at the bottom; the action
   loop is a top-tested while over a flag seeded to zero, so it always draws at
   least one frame.

   THE TICK LATCH IS DELIBERATELY NOT INITIALISED, the same contract every
   animation loop in the game carries: [EBP-0x24] is read at 0001bd99 before
   anything has written it, so the very first frame of the very first card goes
   up without waiting.  Seeding it adds a tick to the sequence
   (rebuild_info/pitfalls.md).

   THE PAGE IS ALLOCATED INSIDE THE LOOP AND FREED OUTSIDE IT.  PUSH 0x16480 /
   CALL malloc at 0001bc60 is inside the per-member body and the only CALL free
   for it is at 0001c1c2, past the loop's end, so every card but the last leaks
   the page.  Moving the free into the loop is what a rewrite naturally does
   and it changes the program's memory behaviour, so it stays where the
   original has it (rebuild_info/pitfalls.md).  The three clips are freed at
   the end of every card, at 0001c196, 0001c1a2 and 0001c1ae.

   THE LEAD-IN BYTE IS READ AND NEVER USED.  fdps_saf_get_frame(act_clip, 0) at
   0001bc1c and the XOR EAX,EAX / MOV AL,[EDX+0x4] that follows it fill
   [EBP-0x20], and nothing in the body reads that slot again.  It is kept
   because it is a dereference of a pointer this function never checks: a clip
   whose frame count is zero makes fdps_saf_get_frame answer NULL and the read
   fault, which a rebuild that dropped the read would survive.

   THE STANDING CLIP IS NOT TICKED AT THE SAME POINT IN EVERY STAGE.  In the
   slide the cursor is advanced BEFORE its frame number is copied into the
   request (0001bd09 then 0001bd18), and in the hold, the caption and the fade
   it is advanced AFTER (0001be17 then 0001be1d, and the same shape at 0001bffc
   and 0001c104), so those three stages draw the frame the cursor held on entry
   and the slide draws the one after it.  Making the four agree shifts the
   standing animation by one frame in one direction or the other.

   THE BLIT MODE IS PUT BACK AND THE MODE OPERAND IS NOT.  The caption stage's
   dissolve arm leaves the blend descriptor in the request's element 7 and only
   element 8 is returned to 0 (MOV [EBP-0x70],0x0 at 0001bfef).  That is
   harmless, because element 7 is only consulted for a mode that uses it, and
   it is what the assembly does.

   The two palette tables are addressed as symbols and not as the 0x653f0 and
   0x643f0 the original's linker gave them; the rebuild does not place anything
   where the original placed it (rebuild_info/pitfalls.md).  0xa0000 stays a
   literal for the reason given at the top of this file. */
void fdps_play_ending_credit_roll(void)
{
    /* The nine-dword draw request sprite.h describes, rewritten in place for
       every layer of every frame. */
    int request[DRAW_REQUEST_DWORDS];
    /* The three-dword mode-9 descriptor rleblend.h describes.  Its two table
       pointers are filled once, before the roster loop; only the level moves,
       and only in the two dissolves. */
    int blend_descriptor[BLEND_DESC_DWORDS];
    /* The playback cursors saf.h describes, one for the member's standing loop
       and one for the action clip that plays once. */
    int stand_cursor[SAF_CURSOR_DWORDS];
    int act_cursor[SAF_CURSOR_DWORDS];
    /* The member name each of the three loads is formatted into.  It is handed
       to fdps_vfs_load_entry, which upper-cases the caller's own storage in
       place, so it has to be writable storage and not a literal (vfs.h). */
    char clip_name[CREDIT_CLIP_NAME_BYTES];
    /* The handle the four blend-table reads are made through, one at a
       time. */
    FILE *blend_table_file;
    /* The roster member this card is about, resolved once per card and read
       only for its portrait id. */
    struct fdps_unit_record *member_record;
    /* The three .SAF images this card owns and releases: the standing loop,
       the action clip and the terrain behind them. */
    void *stand_clip;
    void *act_clip;
    void *backdrop_clip;
    /* The 368x248 page every frame is composed on.  Allocated per card and
       released once -- see the note above. */
    unsigned char *work_page;
    /* Frame 0 of the action clip, read for one byte and nothing else. */
    unsigned char *act_first_frame;
    /* That byte, the clip's lead-in count.  Nothing here uses it; it is read
       because the original reads it. */
    unsigned int act_lead_in_count;
    /* Which Stand/Act pair this card plays: the member's portrait id, or the
       fixed class on the pass past the end of the roster. */
    int sprite_id;
    /* The chapter text entry the first member's epilogue is at. */
    int caption_text_base;
    /* Which card is being played, and equally which roster slot it is. */
    int roster_index;
    /* How far the stage in hand has gone.  All four counted stages share the
       one slot, as the original does. */
    int step;
    /* fdps_saf_advance_tick's answer for the action clip: 0 while it is still
       running and 1 on the tick that steps past its last frame. */
    int clip_ended;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    caption_text_base = 0;
    fdps_cd_set_music_track(CREDIT_MUSIC_INDEX);

    if (data_fdps_chapter_current_chapter_id == CREDIT_LAST_CHAPTER_ID) {
        caption_text_base = CREDIT_LAST_CHAPTER_TEXT_BASE;
    } else {
        caption_text_base = CREDIT_TEXT_BASE;
    }

    memset((void *) VGA_SCREEN_BASE, 0, (size_t) VGA_SCREEN_BYTES);

    blend_table_file = fopen(CREDIT_ENDING_SHADE_RAMP_FILE,
                             CREDIT_BLEND_TABLE_MODE);
    fread(data_fdps_palette_shade_ramp_table, 1,
          (size_t) CREDIT_SHADE_RAMP_BYTES, blend_table_file);
    fclose(blend_table_file);

    blend_table_file = fopen(CREDIT_ENDING_PALETTE_CUBE_FILE,
                             CREDIT_BLEND_TABLE_MODE);
    fread(data_fdps_inverse_palette_cube, 1,
          (size_t) CREDIT_PALETTE_CUBE_BYTES, blend_table_file);
    blend_descriptor[BLEND_DESC_SHADE_RAMP] =
        (int) data_fdps_palette_shade_ramp_table;
    blend_descriptor[BLEND_DESC_CUBE] = (int) data_fdps_inverse_palette_cube;
    fclose(blend_table_file);

    fdps_set_palette_range(
        (struct fdps_palette_entry *) data_fdps_vga_fight_palette_ptr,
        VGA_DAC_FIRST_ENTRY, VGA_DAC_LAST_ENTRY, 0, 0, 0);

    for (roster_index = 0;
         roster_index <= data_fdps_roster_member_count;
         roster_index++) {
        if (data_fdps_chapter_current_chapter_id == CREDIT_CHAPTER_27_ID
            && roster_index == CREDIT_SKIPPED_ROSTER_SLOT) {
            roster_index = CREDIT_SKIP_TO_ROSTER_SLOT;
        }

        if (roster_index == data_fdps_roster_member_count) {
            sprite_id = CREDIT_EXTRA_PASS_SPRITE_ID;
        } else {
            member_record = fdps_get_roster_record(roster_index);
            /* Zero-extended -- XOR EAX,EAX / MOV AL,[EDX+0x7] at 0001bb7c --
               so the id is 0..255 and never negative (contract C). */
            sprite_id = (int) member_record->portrait_id;
        }

        sprintf(clip_name, CREDIT_STAND_CLIP_FORMAT, sprite_id);
        stand_clip = fdps_vfs_load_entry(CREDIT_FIGHT_ARCHIVE, clip_name);
        stand_cursor[SAF_CURSOR_IMAGE] = (int) stand_clip;
        fdps_saf_advance_tick(stand_cursor, CREDIT_SAF_CURSOR_RESET);

        sprintf(clip_name, CREDIT_ACT_CLIP_FORMAT, sprite_id);
        act_clip = fdps_vfs_load_entry(CREDIT_FIGACT_ARCHIVE, clip_name);
        act_cursor[SAF_CURSOR_IMAGE] = (int) act_clip;
        fdps_saf_advance_tick(act_cursor, CREDIT_SAF_CURSOR_RESET);

        act_first_frame = (unsigned char *) fdps_saf_get_frame(act_clip, 0);
        act_lead_in_count =
            (unsigned int) act_first_frame[CREDIT_SAF_FRAME_LEAD_IN_OFFSET];

        sprintf(clip_name, CREDIT_BACKDROP_CLIP_FORMAT,
                roster_index * CREDIT_BACKDROP_ID_STEP);
        backdrop_clip = fdps_vfs_load_entry(CREDIT_BACKDROP_ARCHIVE,
                                            clip_name);

        work_page = (unsigned char *) malloc((size_t) CREDIT_PAGE_BYTES);
        request[DRAW_REQUEST_DEST_BASE] = (int) work_page;
        request[DRAW_REQUEST_DEST_PITCH] = CREDIT_PAGE_PITCH;
        request[DRAW_REQUEST_DEST_ROWS] = CREDIT_PAGE_ROWS;
        request[DRAW_REQUEST_BLIT_OPERAND] = 0;
        request[DRAW_REQUEST_BLIT_MODE] = BLIT_MODE_OPAQUE;
        request[DRAW_REQUEST_X] = CREDIT_PAGE_MARGIN;
        request[DRAW_REQUEST_Y] = CREDIT_PAGE_MARGIN;

        for (step = CREDIT_SLIDE_FIRST_STEP; step >= 0; step--) {
            memset(work_page, 0, (size_t) CREDIT_PAGE_BYTES);

            request[DRAW_REQUEST_IMAGE] = (int) backdrop_clip;
            request[DRAW_REQUEST_ITEM_INDEX] = 0;
            request[DRAW_REQUEST_X] = CREDIT_PAGE_MARGIN;
            request[DRAW_REQUEST_Y] =
                step * CREDIT_SLIDE_BACKDROP_STEP_Y + CREDIT_PAGE_MARGIN;
            fdps_draw_composite_sprite(request, 0);

            fdps_saf_advance_tick(stand_cursor, 0);
            request[DRAW_REQUEST_IMAGE] = stand_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                stand_cursor[SAF_CURSOR_FRAME_INDEX];
            request[DRAW_REQUEST_X] =
                step * CREDIT_SLIDE_SPRITE_STEP_X + CREDIT_PAGE_MARGIN;
            request[DRAW_REQUEST_Y] = CREDIT_PAGE_MARGIN;
            fdps_draw_composite_sprite(request, 0);

            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   == 0) {
                /* Spin until the retrace begins, so the frame that has just
                   been composed is the one the monitor shows whole. */
            }
            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   != 0) {
                /* And until it ends, so the present starts clear of it. */
            }
            fdps_blit_rect((unsigned int) (work_page + CREDIT_PAGE_WINDOW_AT),
                           CREDIT_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, VGA_SCREEN_PITCH,
                           VGA_SCREEN_ROWS);
            while (last_tick == data_fdps_timer_tick_counter) {
                /* spin: only the timer interrupt can end this */
            }
            last_tick = data_fdps_timer_tick_counter;
        }

        request[DRAW_REQUEST_X] = CREDIT_PAGE_MARGIN;
        request[DRAW_REQUEST_Y] = CREDIT_PAGE_MARGIN;

        for (step = 0; step < CREDIT_HOLD_FRAMES; step++) {
            memset(work_page, 0, (size_t) CREDIT_PAGE_BYTES);

            request[DRAW_REQUEST_ITEM_INDEX] = 0;
            request[DRAW_REQUEST_IMAGE] = (int) backdrop_clip;
            fdps_draw_composite_sprite(request, 0);

            request[DRAW_REQUEST_IMAGE] = stand_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                stand_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_saf_advance_tick(stand_cursor, 0);
            fdps_draw_composite_sprite(request, 0);

            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   == 0) {
                /* The same pair of spins, for the same reason. */
            }
            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   != 0) {
            }
            fdps_blit_rect((unsigned int) (work_page + CREDIT_PAGE_WINDOW_AT),
                           CREDIT_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, VGA_SCREEN_PITCH,
                           VGA_SCREEN_ROWS);
            while (last_tick == data_fdps_timer_tick_counter) {
                /* spin: only the timer interrupt can end this */
            }
            last_tick = data_fdps_timer_tick_counter;
        }

        clip_ended = 0;
        while (clip_ended == 0) {
            memset(work_page, 0, (size_t) CREDIT_PAGE_BYTES);

            request[DRAW_REQUEST_ITEM_INDEX] = 0;
            request[DRAW_REQUEST_IMAGE] = (int) backdrop_clip;
            fdps_draw_composite_sprite(request, 0);

            request[DRAW_REQUEST_IMAGE] = act_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                act_cursor[SAF_CURSOR_FRAME_INDEX];
            clip_ended = fdps_saf_advance_tick(act_cursor, 0);
            fdps_draw_composite_sprite(request, 0);

            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   == 0) {
                /* The same pair of spins, for the same reason. */
            }
            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   != 0) {
            }
            fdps_blit_rect((unsigned int) (work_page + CREDIT_PAGE_WINDOW_AT),
                           CREDIT_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, VGA_SCREEN_PITCH,
                           VGA_SCREEN_ROWS);
            while (last_tick == data_fdps_timer_tick_counter) {
                /* spin: only the timer interrupt can end this */
            }
            last_tick = data_fdps_timer_tick_counter;
        }

        for (step = 0; step <= CREDIT_CAPTION_LAST_FRAME; step++) {
            memset(work_page, 0, (size_t) CREDIT_PAGE_BYTES);

            if (step < CREDIT_CAPTION_BACKDROP_FRAMES) {
                blend_descriptor[BLEND_DESC_LEVEL] =
                    step * CREDIT_BLEND_LEVEL_STEP;
                request[DRAW_REQUEST_BLIT_OPERAND] = (int) blend_descriptor;
                request[DRAW_REQUEST_BLIT_MODE] = BLIT_MODE_TRANSLUCENT;
                request[DRAW_REQUEST_ITEM_INDEX] = 0;
                request[DRAW_REQUEST_IMAGE] = (int) backdrop_clip;
                fdps_draw_composite_sprite(request, 0);
            }

            request[DRAW_REQUEST_BLIT_MODE] = BLIT_MODE_OPAQUE;
            request[DRAW_REQUEST_IMAGE] = stand_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                stand_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_saf_advance_tick(stand_cursor, 0);
            fdps_draw_composite_sprite(request, 0);

            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           caption_text_base + roster_index,
                           work_page + CREDIT_PAGE_CAPTION_AT,
                           CREDIT_PAGE_PITCH, CREDIT_CAPTION_FG_COLOR,
                           CREDIT_CAPTION_BG_COLOR,
                           CREDIT_CAPTION_OUTLINE_COLOR);

            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   == 0) {
                /* The same pair of spins, for the same reason. */
            }
            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   != 0) {
            }
            fdps_blit_rect((unsigned int) (work_page + CREDIT_PAGE_WINDOW_AT),
                           CREDIT_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, VGA_SCREEN_PITCH,
                           VGA_SCREEN_ROWS);
            while (last_tick == data_fdps_timer_tick_counter) {
                /* spin: only the timer interrupt can end this */
            }
            last_tick = data_fdps_timer_tick_counter;
        }

        for (step = 0; step <= CREDIT_FADE_LAST_STEP; step++) {
            memset(work_page, 0, (size_t) CREDIT_PAGE_BYTES);

            blend_descriptor[BLEND_DESC_LEVEL] =
                step * CREDIT_BLEND_LEVEL_STEP;
            request[DRAW_REQUEST_BLIT_OPERAND] = (int) blend_descriptor;
            request[DRAW_REQUEST_BLIT_MODE] = BLIT_MODE_TRANSLUCENT;
            request[DRAW_REQUEST_IMAGE] = stand_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                stand_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_saf_advance_tick(stand_cursor, 0);
            fdps_draw_composite_sprite(request, 0);

            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   == 0) {
                /* The same pair of spins, for the same reason. */
            }
            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   != 0) {
            }
            fdps_blit_rect((unsigned int) (work_page + CREDIT_PAGE_WINDOW_AT),
                           CREDIT_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, VGA_SCREEN_PITCH,
                           VGA_SCREEN_ROWS);
            while (last_tick == data_fdps_timer_tick_counter) {
                /* spin: only the timer interrupt can end this */
            }
            last_tick = data_fdps_timer_tick_counter;
        }

        free(backdrop_clip);
        free(act_clip);
        free(stand_clip);
    }

    free(work_page);

    fdps_set_palette_range(
        (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
        VGA_DAC_FIRST_ENTRY, VGA_DAC_LAST_ENTRY, 0, 0, 0);

    blend_table_file = fopen(CREDIT_MAP_SHADE_RAMP_FILE,
                             CREDIT_BLEND_TABLE_MODE);
    fread(data_fdps_palette_shade_ramp_table, 1,
          (size_t) CREDIT_SHADE_RAMP_BYTES, blend_table_file);
    fclose(blend_table_file);

    blend_table_file = fopen(CREDIT_MAP_PALETTE_CUBE_FILE,
                             CREDIT_BLEND_TABLE_MODE);
    fread(data_fdps_inverse_palette_cube, 1,
          (size_t) CREDIT_PALETTE_CUBE_BYTES, blend_table_file);
    fclose(blend_table_file);

    fdps_play_movie(CREDIT_MOVIE_NAME);
}
