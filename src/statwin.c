/* statwin.c -- the frame of the battle unit status window.
 *
 * The window's own artwork and the movement of it: the sheet the empty window
 * is decoded from, one step of the four-panel slide that puts it on the
 * screen, and the same slide run backwards to take it away again.  What is
 * painted into the window -- one unit's figures, its inventory list and its
 * walk cycle -- is statunit.c.  See statwin.h for the window's geometry.
 *
 * Nothing here owns state except the byte the close hands back to the caller
 * it was opened from, data_fdps_ui_play_active_flag_saved, which statwin.h
 * declares.
 *
 * inp comes from <conio.h> and delay from <i86.h>, which is where Watcom
 * 10.0a declares them, and both are ordinary calls in the original rather
 * than an inline IN instruction: 00016e80 issues CALL 0003d4e4 and CALL
 * 0003d370.  Watcom only turns inp into an instruction when -oi is given, and
 * it is not in this build's flag set (rebuild_info/build_flags.md).
 */
#include <conio.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "audio.h"
#include "blit.h"
#include "gamedata.h"
#include "mapdraw.h"
#include "statwin.h"
#include "vfs.h"

/* The VGA graphics aperture as a flat linear address, the mode 13h scanline
   pitch, the row count and the size of one whole frame.  All four are
   hard-coded in the original (PUSH 0xa0000, PUSH 0x140, 0xc8, PUSH 0xfa00)
   and stay literals here: 0xa0000 is where the display adapter answers, not
   the address of anything the linker places, so there is no symbol to
   reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8
#define VGA_SCREEN_BYTES 0xfa00

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit this file looks at. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* How many steps the slide-in has, and so how long each of the four offset
   tables is.  Both callers that open the window walk 0..8 (CMP ...,0x9 / JL
   at 00016bc3 and 00025a8e); the one that closes it walks 5 down to 0. */
#define ANIM_STEP_COUNT 9

/* PUSH 0xf / CALL delay at 00017067: every step holds its frame for fifteen
   milliseconds on top of the retrace wait. */
#define ANIM_FRAME_DELAY_MS 0xf

/* Where the four panels sit once the window has finished opening, in pixels.
   The two left panels share a column span and the two right ones share
   another, and the two spans meet exactly: LEFT_COLUMN_X + LEFT_COLUMN_WIDTH
   is RIGHT_COLUMN_X.  The horizontal split between the panels is at a
   different row on each side -- row 108 on the left, row 44 on the right --
   which is why the four heights are not two pairs. */
#define LEFT_COLUMN_X 0x0f
#define LEFT_COLUMN_WIDTH 0x85
#define UPPER_LEFT_HEIGHT 0x6c
#define LOWER_LEFT_Y 0x6c
#define RIGHT_COLUMN_X 0x94
#define RIGHT_COLUMN_WIDTH 0x9e
#define UPPER_RIGHT_HEIGHT 0x2c
#define LOWER_RIGHT_Y 0x2c
#define LOWER_RIGHT_HEIGHT 0x9c

/* 00016e80.  The four tables are the four panels' positions, one entry per
   step, and each panel travels along one axis only: the two left panels are
   fixed in the other axis by LEFT_COLUMN_X, the two right ones by
   RIGHT_COLUMN_X and LOWER_RIGHT_Y.  A position outside the screen means the
   panel is still partly off that edge, and each blit below is cut down to
   whatever part of it is on screen -- there is no clipping anywhere else, so
   these four tests are the whole of it.

   The tables live in the original as four nine-int local arrays initialised
   from templates at 0x14788, 0x147ac, 0x147d0 and 0x147f4, copied in with
   four REP MOVSD of ECX=9 at the top of the frame.  They are locals and not
   globals: nothing else in the image reads those addresses, and emitting them
   as data symbols would put four arrays in the rebuild that the original does
   not have as separate objects.

   THE ENTRIES ARE SIGNED AND THREE TABLES REALLY DO GO NEGATIVE.  CMP dword
   ptr [EAX+EBP-0x30],0x0 / JGE at 00016eff is the signed test, and the first
   five entries of upper_left_x and of upper_right_y are below zero.  Read as
   unsigned, upper_left_x[0] is about four billion, the JGE branch is taken,
   and the first frame blits 133 columns from four billion bytes past the
   window image.

   Steps 6 and 7 step back one pixel from step 5 before step 8 returns to it:
   the window overshoots its resting place by a pixel and settles.  That is
   not an off-by-one in the tables, it is the animation.

   The last two steps of lower_right_x are inside the screen by more than the
   panel is wide, which is what the clamp below is for. */
void fdps_draw_status_window_anim_frame(void *background, void *window_image,
                                        int step)
{
    /* Declared in this order because it is the order the original's frame is
       laid out in -- [EBP-4] is the off-screen buffer, [EBP-8] the upper-left
       panel's destination column and the four arrays run down from [EBP-0x30]
       -- and wcc386 hands out the slots in declaration order.  Nothing
       depends on it; the arrangement is codegen, not behaviour.

       The original holds all three panel extents below in the single slot at
       [EBP-0xc], one after another, so its frame is eight bytes smaller than
       this one's.  They are three different measurements of three different
       panels and are named as such here (ADR-0001: the standard is observable
       behaviour, and a stack slot is not). */
    unsigned char *frame;
    int upper_left_dest_x;
    int upper_left_width;
    int lower_right_width;
    int upper_right_height;
    int upper_left_x[ANIM_STEP_COUNT] =
        { -120, -100, -70, -40, -15, 15, 14, 14, 15 };
    int lower_left_y[ANIM_STEP_COUNT] =
        { 190, 175, 160, 140, 125, 108, 109, 109, 108 };
    int lower_right_x[ANIM_STEP_COUNT] =
        { 300, 270, 240, 215, 175, 148, 149, 149, 148 };
    int upper_right_y[ANIM_STEP_COUNT] =
        { -36, -32, -24, -16, -8, 0, -1, -1, 0 };

    /* One whole frame's worth of off-screen buffer, allocated and freed per
       call.  The original does not check the result and neither does this:
       PUSH 0xfa00 / CALL malloc / MOV [EBP-4],EAX at 00016ecb goes straight
       into the memmove that follows. */
    frame = (unsigned char *) malloc(VGA_SCREEN_BYTES);
    memmove(frame, background, (size_t) VGA_SCREEN_BYTES);

    /* Upper-left panel, sliding in from the left edge.  While its x is
       negative only its rightmost LEFT_COLUMN_WIDTH + x columns are on
       screen, and they go at column 0; once x reaches zero the whole panel is
       drawn at column x.  Either way the source column is
       RIGHT_COLUMN_X - width, which is LEFT_COLUMN_X shifted right by however
       much of the panel is still off the edge. */
    if (upper_left_x[step] < 0) {
        upper_left_width = upper_left_x[step] + LEFT_COLUMN_WIDTH;
        upper_left_dest_x = 0;
    } else {
        upper_left_width = LEFT_COLUMN_WIDTH;
        upper_left_dest_x = upper_left_x[step];
    }
    fdps_blit_rect((unsigned int) window_image
                       + (RIGHT_COLUMN_X - upper_left_width),
                   VGA_SCREEN_PITCH,
                   frame + upper_left_dest_x,
                   VGA_SCREEN_PITCH,
                   upper_left_width, UPPER_LEFT_HEIGHT);

    /* Lower-left panel, sliding up from below the screen.  Its top row is the
       table entry and its height is simply what is left above the bottom
       scanline, so the part still below the screen is never blitted rather
       than being blitted and clipped. */
    fdps_blit_rect((unsigned int) window_image
                       + (LOWER_LEFT_Y * VGA_SCREEN_PITCH + LEFT_COLUMN_X),
                   VGA_SCREEN_PITCH,
                   frame + lower_left_y[step] * VGA_SCREEN_PITCH
                       + LEFT_COLUMN_X,
                   VGA_SCREEN_PITCH,
                   LEFT_COLUMN_WIDTH, VGA_SCREEN_ROWS - lower_left_y[step]);

    /* Lower-right panel, sliding in from the right edge.  Its width is the
       room left between its x and the right edge of the screen, and it stops
       growing once the whole panel fits: CMP dword ptr [EBP-0xc],0x9e / JLE
       at 00016fc8, the signed test.  The clamp is load bearing and not
       defensive -- the last four steps sit at x 148 or 149, which leave 172
       and 171 columns of room, and without it those frames would blit past
       the panel's own right edge into whatever the window image holds
       there. */
    lower_right_width = VGA_SCREEN_PITCH - lower_right_x[step];
    if (lower_right_width > RIGHT_COLUMN_WIDTH) {
        lower_right_width = RIGHT_COLUMN_WIDTH;
    }
    fdps_blit_rect((unsigned int) window_image
                       + (LOWER_RIGHT_Y * VGA_SCREEN_PITCH + RIGHT_COLUMN_X),
                   VGA_SCREEN_PITCH,
                   frame + LOWER_RIGHT_Y * VGA_SCREEN_PITCH
                       + lower_right_x[step],
                   VGA_SCREEN_PITCH,
                   lower_right_width, LOWER_RIGHT_HEIGHT);

    /* Upper-right panel, sliding down from above the screen.  Its y is zero
       or negative, so the height on screen is UPPER_RIGHT_HEIGHT + y and the
       source starts that far down the panel; the destination is always row 0.
       No clamp is needed on this one because y never goes positive. */
    upper_right_height = upper_right_y[step] + UPPER_RIGHT_HEIGHT;
    fdps_blit_rect((unsigned int) window_image
                       + (UPPER_RIGHT_HEIGHT - upper_right_height)
                           * VGA_SCREEN_PITCH
                       + RIGHT_COLUMN_X,
                   VGA_SCREEN_PITCH,
                   frame + RIGHT_COLUMN_X,
                   VGA_SCREEN_PITCH,
                   RIGHT_COLUMN_WIDTH, upper_right_height);

    /* The frame's pacing, in the order the original has it: hold for fifteen
       milliseconds, then wait for a vertical retrace to start and then for it
       to finish, and only then put the buffer up.  Presenting on the far side
       of the retrace rather than at its start is what keeps the copy off the
       displayed scanlines; the delay on top of it is what makes the window
       take about a third of a second to open rather than nine frames. */
    delay((unsigned int) ANIM_FRAME_DELAY_MS);
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
    }
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
    }

    memmove((void *) VGA_SCREEN_BASE, frame, (size_t) VGA_SCREEN_BYTES);
    free(frame);
}

/* Where a .CEL sheet's offset table begins, spelled as the size of the header
   it follows because that is what the 15 is (resource_info/cel.md).  MOV EAX,
   dword ptr [EDX + 0xf] at 00016894 is the read, and 0xf is an immediate in
   the instruction: the header's own u16 at +0x05 that declares the table
   position is never looked at, here or anywhere else in the image. */
#define CEL_OFFSET_TABLE_START ((int) sizeof(struct fdps_cel_header))

/* The source rectangle the sheet is decoded as, PUSH 0x140 at 000168a8 and
   PUSH 0xc8 at 000168a3.  Both are immediates and neither is a read of the
   sheet's header: Status.cel's own i16 pair at +0x07 and +0x09 declares
   exactly this 320x200, but the code does not consult it. */
#define STATUS_CEL_WIDTH 0x140
#define STATUS_CEL_HEIGHT 0xc8

/* Mode 0, the plain opaque RLE kernel, PUSH 0x0 at 0001689a; the mode operand
   pushed with it at 0001689c is the dword mode 0 does not read. */
#define BLIT_MODE_OPAQUE 0

/* 00016840.  One basic block: no branch, no test and no loop, and nothing is
   checked -- not the container handle, not the loaded member and not the
   allocation.  A missing MISC.VFS therefore does not fail gracefully, it hands
   fdps_vfs_load_file a null handle to search.  That is the original's
   behaviour and is reproduced rather than guarded (rebuild_info/pitfalls.md).

   THE STRING LITERALS ARE WRITTEN THROUGH.  fdps_vfs_load_file upper-cases its
   query in place (vfs.h), so the "Status.cel" below is permanently
   "STATUS.CEL" after the first call, exactly as the original's copy at 0x61590
   is.  Both spellings find the member -- every entry name in every shipped
   container is upper case (resource_info/vfs.md) -- so the rewrite is
   invisible, but the literal has to be in writable storage for the call to
   work at all.  Under -mf it is: wcc386 puts string literals in CONST
   (rebuild_info/build_flags.md) and CONST is a member of DGROUP alongside
   _DATA and _BSS, which is where the original's own copy lives too.

   THE ORDER OF THE TWO FREES IS NOT INTERCHANGEABLE WITH THE BLIT.  The
   container handle goes back before the sheet is even addressed (CALL free at
   00016886, ahead of the offset-table read at 00016894) and the sheet goes
   back after the decode (CALL free at 000168c1).  So the handle is dead for
   the whole of the drawing and only the 64000-byte result outlives the call.

   Of the seven arguments the blit takes, three are the geometry and the
   distinction between two of them matters: STATUS_CEL_WIDTH is the source
   rectangle's width, the third argument, pushed last at 000168a8 and landing
   at [EBP + 0x10] in fdps_blit_dispatch, and VGA_SCREEN_PITCH is the
   destination surface's pitch, the fifth, pushed at 0001689e and landing at
   [EBP + 0x18].  They are the same 0x140 here because the destination is a
   whole mode 13h frame and the sheet fills it, which is why nothing in the
   emitted call would look wrong if the two were swapped -- and why they are
   named apart rather than sharing one constant.

   The caller owns what comes back and must free it; all four call sites store
   EAX into a local of their own and pass it on as the window image. */
void *fdps_load_status_cel_image(void)
{
    void *misc_vfs;
    unsigned char *cel_sheet;
    unsigned char *cel_stream;
    unsigned char *status_image;

    misc_vfs = fdps_vfs_open("MISC.VFS");
    cel_sheet = (unsigned char *) fdps_vfs_load_file("Status.cel", misc_vfs);
    status_image = (unsigned char *) malloc(VGA_SCREEN_BYTES);
    free(misc_vfs);

    /* Sprite 0's stream, addressed from the start of the FILE: the table
       entry is loaded off the sheet base and then added to the sheet base
       again, never to the address it was read from. */
    cel_stream = cel_sheet
        + *(int *) (cel_sheet + CEL_OFFSET_TABLE_START);
    fdps_blit_dispatch(cel_stream, status_image, STATUS_CEL_WIDTH,
                       STATUS_CEL_HEIGHT, VGA_SCREEN_PITCH, 0,
                       BLIT_MODE_OPAQUE);

    free(cel_sheet);

    /* The original copies the buffer into a fifth slot and returns that --
       MOV [EBP-4],EAX / MOV EAX,[EBP-4] at 000168cc -- which is what a plain
       return of a local compiles to at -od.  This one does the same: the
       object file wcc386 produces from the line below carries both moves and
       the same SUB ESP,0x14.  Only which slot holds which local differs, and
       a stack slot is not observable behaviour (ADR-0001). */
    return status_image;
}

/* The offscreen page the map behind the window is composed on: 360 x 240 8bpp
   at pitch 0x168, PUSH 0x15180 at 000168fa, with the 24-pixel apron on all
   four sides that fdps_draw_scene_layers needs.  Its visible window starts at
   page byte 0x21d8 (ADD EAX,0x21d8 at 00016952), page coordinate (24,24), and
   lands at byte 0x504 of a whole 320x200 frame (ADD EAX,0x504 at 00016944),
   frame coordinate (4,4).  The window itself is 0x138 by 0xc0, the two pushes
   at 00016937 and 00016932. */
#define SCENE_PAGE_PITCH 0x168
#define SCENE_PAGE_BYTES 0x15180
#define SCENE_PAGE_WINDOW_AT 0x21d8
#define SCREEN_WINDOW_AT 0x504
#define SCREEN_WINDOW_W 0x138
#define SCREEN_WINDOW_H 0xc0

/* The window cue, PUSH 0x6159c / CALL fdps_play_sfx at 000168ec.  It is the
   same member the opening plays (PUSH 0x6159c at 00016b09 in
   fdps_battle_show_unit_status_window), and those two are the only references
   to that string in the image -- the ring menu's own cue is a different member,
   "OpWin.wav" at 0x61584 (menu.c).

   It has to be a plain writable literal, for the reason menu.c's copy does:
   the lookup inside fdps_play_sfx upper-cases the caller's own storage in place
   (vfs.h), so this spelling is permanently "OPWIN1.WAV" after the first status
   window of the run, exactly as the original's copy at 0x6159c is
   (rebuild_info/pitfalls.md). */
#define STATUS_WINDOW_SOUND "OpWin1.wav"

/* Where the closing walk starts.  MOV dword ptr [EBP-0x4],0x5 at 000169a9 and
   CMP against 0 / JGE at 000169b0, so the steps drawn are 5, 4, 3, 2, 1, 0 --
   six frames, and step 0 is the closed position.  The opening walks 0 up to 8,
   so the closing does NOT retrace it: it starts from the resting frame's
   predecessor and never draws steps 6, 7 or 8, which are the one-pixel
   overshoot the opening settles through (statwin.h). */
#define CLOSE_ANIM_FIRST_STEP 5

/* The view's frame: the four pixels of screen around the 312x192 window that
   fdps_render_view_frame never writes.  VIEW_BORDER_BAND_BYTES is one band of
   four whole rows, the 0x500 pushed at 000169e8 and 000169fc, and
   VIEW_BOTTOM_BORDER_AT is where the lower band starts -- the 0xaf500 pushed at
   00016a03, which is row 196. */
#define VIEW_BORDER_WIDTH 4
#define VIEW_BORDER_BAND_BYTES (VIEW_BORDER_WIDTH * VGA_SCREEN_PITCH)
#define VIEW_BOTTOM_BORDER_AT \
    ((VGA_SCREEN_ROWS - VIEW_BORDER_WIDTH) * VGA_SCREEN_PITCH)
#define VIEW_RIGHT_BORDER_AT (VGA_SCREEN_PITCH - VIEW_BORDER_WIDTH)

/* How many times the loop that blanks the left and right margins runs: CMP
   dword ptr [EBP-0x4],0x140 / JL at 00016a17.

   IT IS THE SCREEN'S WIDTH AND NOT ITS HEIGHT, AND THAT IS THE ORIGINAL'S BUG.
   Only the first 200 iterations land on a scanline; the last 120 write their
   four-byte zero runs from the end of the frame at 0xafa00 up to 0xb8fff, which
   is past the mode 13h aperture entirely.  It is harmless only because the
   destination is the adapter and not a buffer -- writing the obvious thing, a
   border clear over a 64000-byte offscreen page, would overrun that page by 38
   KB (rebuild_info/pitfalls.md).  So the count stays 0x140 and the writes stay
   aimed at VGA_SCREEN_BASE; it is spelled as the pitch because that is the
   constant the original used, not because a row index belongs in it. */
#define BORDER_CLEAR_ROW_COUNT VGA_SCREEN_PITCH

/* 000168e0.  Takes the status window away again.  See statwin.h for what the
   two arguments are and what the caller still owns afterwards.

   THE SCRATCH PAGE IS TAKEN IN BOTH BRANCHES AND USED IN ONLY ONE.  The
   0x15180-byte scene page is allocated before the branch and freed after
   everything, but the village arm never reads it: the blit that arm performs
   writes the saved page's view window INTO the scratch, and nothing looks at it
   again.  That blit is dead work in the original too, and it stays -- dropping
   it would drop the read of the saved page and its 192 row-by-row memmoves, and
   keeping the allocation out of the village arm would change which block the
   next malloc in the program is handed.

   THE ANIMATION IS DRAWN OVER background AND ITS RESULT IS THROWN AWAY.  Each
   of the six frames presents itself to the screen, and then the restore below
   paints over the whole of what the last one put there.  What the player sees
   is the six frames; what is left on the adapter afterwards is the restore
   alone.

   NOTHING IS CHECKED.  malloc's result goes straight into the branch, the
   saved page pointer is dereferenced without a null test, and neither the cue
   nor the compositor reports anything back. */
void fdps_close_status_window(void *window_image, void *background)
{
    /* The 360x240 page the scene behind the window is composed on, taken and
       given back inside this one call. */
    unsigned char *scene_page;
    /* Which step of the four-panel slide is being drawn, counting down. */
    int step;
    /* The screen row whose four-pixel left and right margins are being
       blanked -- for the first 200 of the loop's 320 passes; see
       BORDER_CLEAR_ROW_COUNT above for what the rest write. */
    int border_row;

    fdps_play_sfx(STATUS_WINDOW_SOUND);
    scene_page = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);

    /* The picture that lies behind the window, into background.  On the battle
       map it is composed again from the scene layers and only the view window
       is written, the rest of the frame being cleared; in a village it is the
       saved page copied whole. */
    if (data_fdps_village_mode_flag == 0) {
        fdps_draw_scene_layers(scene_page);
        memset(background, 0, (size_t) VGA_SCREEN_BYTES);
        fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                       SCENE_PAGE_PITCH,
                       (void *) ((unsigned char *) background
                                 + SCREEN_WINDOW_AT),
                       VGA_SCREEN_PITCH, SCREEN_WINDOW_W, SCREEN_WINDOW_H);
    } else {
        fdps_blit_rect((unsigned int) (data_fdps_village_backdrop_page_ptr
                                       + SCREEN_WINDOW_AT),
                       VGA_SCREEN_PITCH,
                       scene_page + SCENE_PAGE_WINDOW_AT,
                       SCENE_PAGE_PITCH, SCREEN_WINDOW_W, SCREEN_WINDOW_H);
        memmove(background, data_fdps_village_backdrop_page_ptr,
                (size_t) VGA_SCREEN_BYTES);
    }

    for (step = CLOSE_ANIM_FIRST_STEP; step >= 0; step--) {
        fdps_draw_status_window_anim_frame(background, window_image, step);
    }

    /* And the visible screen.  The battle branch goes through the compositor,
       which writes only the 312x192 window, and then blanks the frame around it
       directly in the adapter; the village branch puts the saved page back and
       blanks nothing, because the border there belongs to the village screen
       and the saved page already holds it. */
    if (data_fdps_village_mode_flag == 0) {
        fdps_render_view_frame();
        memset((void *) VGA_SCREEN_BASE, 0, (size_t) VIEW_BORDER_BAND_BYTES);
        memset((void *) (VGA_SCREEN_BASE + VIEW_BOTTOM_BORDER_AT), 0,
               (size_t) VIEW_BORDER_BAND_BYTES);
        for (border_row = 0; border_row < BORDER_CLEAR_ROW_COUNT;
             border_row++) {
            memset((void *) (VGA_SCREEN_BASE
                             + border_row * VGA_SCREEN_PITCH),
                   0, (size_t) VIEW_BORDER_WIDTH);
            memset((void *) (VGA_SCREEN_BASE
                             + border_row * VGA_SCREEN_PITCH
                             + VIEW_RIGHT_BORDER_AT),
                   0, (size_t) VIEW_BORDER_WIDTH);
        }
    } else {
        memmove((void *) VGA_SCREEN_BASE, data_fdps_village_backdrop_page_ptr,
                (size_t) VGA_SCREEN_BYTES);
    }

    free(scene_page);
    data_fdps_ui_play_active_flag = data_fdps_ui_play_active_flag_saved;
}
