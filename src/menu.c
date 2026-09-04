/* menu.c -- the four-way command menus the battle screens put a cursor on.
 *
 * See menu.h for what a menu descriptor is.  Everything in this file works on
 * a caller-supplied descriptor; there is no menu state of its own here.
 *
 * malloc and free come from <stdlib.h>, cos and sin from <math.h> and delay
 * from <i86.h>.  All five are real calls in the original and the vendor
 * headers are what declare them, so nothing here declares one of its own.
 */
#include <stdlib.h>
#include <math.h>
#include <i86.h>
#include "gamedata.h"
#include "audio.h"
#include "blit.h"
#include "keybd.h"
#include "mapdraw.h"
#include "palcycle.h"
#include "unit.h"
#include "menu.h"

/* 000160e0.  The four-entry bound is hard-coded in the original -- CMP
   dword ptr [EBP-8],4 / JL -- and is not derived from anything the caller
   passes, so it stays a literal here.  JL is the signed compare, which is why
   the index is a plain int.

   The entry test is CMP dword ptr [EAX],0 / JNZ: any non-zero value means
   greyed out, negative included.  It is not a "> 0" test, and writing one
   would let a negative entry be picked. */
int fdps_menu_find_first_enabled_entry(int *cmd_disabled)
{
    int index;

    for (index = 0; index < 4; index++) {
        if (cmd_disabled[index] == 0) {
            return index;
        }
    }
    return -1;
}

/* The offscreen page one frame is composed on: 360 x 240 8bpp at pitch 0x168,
   PUSH 0x15180 / CALL malloc at 00015873.  The 24-pixel apron on all four
   sides is the whole reason the page is wider than the window it presents: a
   button plate whose origin has scrolled off the view still lands inside the
   allocation instead of over the adapter.

   THE PAGE IS NOT CLEARED.  malloc's block goes straight to the compositor and
   the compositor paints only the layers, cursor and units it is given, so
   whatever the heap left behind shows through anywhere those do not reach.
   malloc's answer is not tested against NULL either; there is no CMP EAX,0x0
   between the CALL at 00015878 and the store at 00015880. */
#define SCENE_PAGE_PITCH 0x168
#define SCENE_PAGE_BYTES 0x15180
#define SCENE_PAGE_BORDER 0x18

/* The window handed to the adapter, and where the adapter answers.  312 x 192
   taken from page byte 0x21d8 -- page coordinate (24,24), the top-left of the
   picture inside the apron -- and put down at screen byte 0x504, screen
   coordinate (4,4).  Both are hard-coded in the original (PUSH 0xa0504 at
   00015b58) and stay literals here: 0xa0000 is where the display adapter
   answers, not the address of anything the linker places. */
#define SCENE_PAGE_WINDOW_AT 0x21d8
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define RING_WINDOW_AT 0x504
#define RING_WINDOW_W 0x138
#define RING_WINDOW_H 0xc0

/* The four button slots, in the order the menu cursor moves through them.  The
   count is the hard-coded 4 of CMP dword ptr [EBP-0x8],0x4 / JL at 00015982
   and comes from nothing the caller passes. */
#define RING_SLOTS 4

/* 2 * 3.14159 / 96 rounded to float, which is the dword 0x3d860a8a the
   original stores into its frame at 0001586c and multiplies the radius by.

   IT IS NOT 2 * pi / 96.  The author's pi is the six-digit 3.14159, and
   0x3d860a8a decodes to 0.06544978916645050048828125 (exponent field 0x7b,
   mantissa field 0x060a8a = 395914); the nearest float to the true
   2 * pi / 96 needs mantissa 395922 and is 0x3d860a92, eight ulps higher.
   So the constant is written out exactly rather than derived from a library
   pi.

   THE EIGHT ULPS ARE NOT WHAT MAKES THE RESTING RING 23 PIXELS AND NOT 24.
   The truncation is.  sin never exceeds 1, so radius * sin(angle) is below
   radius for every angle, and CALL __CHP / FISTP at 000158d7 chops toward
   zero.  With 0x3d860a92 the angle at radius 0x18 would land 4.4e-8 ABOVE
   pi / 2, 24 * sin would be 23.99999999999998, and that truncates to 23 as
   well; the cosine leg truncates to 0 with either constant.  Rounding
   instead of truncating is what would move the ring -- see the note on the
   offsets below -- not the choice of pi. */
#define RING_RADIANS_PER_PIXEL 0.06544979f

/* Command.cel sub-images.  The highlighted plate is 0x1a and the plain one
   0x1b, and cmd_disabled[slot] is ADDED to whichever was picked, so 0x1c is
   the greyed-out plain plate.  The icon's own greyed-out form is a whole
   second bank 0x24 sub-images further into the sheet (IMUL EDX,dword ptr
   [EAX],0x24 at 00015aac), not the next sub-image along. */
#define RING_PLATE_HIGHLIGHTED 0x1a
#define RING_PLATE_PLAIN 0x1b
#define RING_DISABLED_ICON_BANK 0x24

/* Every plate and every icon is blitted at this one size, 25 x 22. */
#define RING_PLATE_W 0x19
#define RING_PLATE_H 0x16

/* The slot's origin has to sit inside this box or the slot is not drawn at
   all.

   IT IS AN ORIGIN TEST AND NOT A CLIP.  Only the top-left corner is examined;
   the plate's own 25 x 22 extent is never taken into account, so a slot that
   passed at x = 0x14e would write 25 bytes from column 342 of a 360-byte row
   and spill into the head of the next row.  Widening the box, or narrowing it
   by the plate's size to make the test look symmetric, changes which slots
   appear.

   The box is looser than the camera can actually drive it, but the slack is
   not entirely unused.  fdps_map_cursor_move_to keeps the cursor between 0x18
   and 0xd8 pixels from the view's left edge and between 0x18 and 0x90 from
   its top, and pulls the origin back to map width - 0x138 or map height -
   0xc0 at the far edges, so the widest the centre can get is 288 across and
   168 down.  The largest radius any caller passes is 0x19, not 0x18 -- PUSH
   0x19 at 00016193, the frame fdps_menu_animate_open ends its sweep on --
   and at that radius the legs are -1 and 24, so the extreme slot origins are
   x = 312 (slot 2) and y = 192 (slot 3).  Both pass the test.  The y extreme
   puts its plate on page rows 216..237 of 240.  The x extreme puts its plate
   on page columns 336..360, and 360 is one past the last column of a
   360-byte row, so the rightmost plate's final column lands on the head of
   the following scanline -- still well inside the 0x15180 allocation, never
   over the adapter.  So nothing in play is dropped by this test and nothing
   is written past the page, but do not read that as licence to add a guard:
   the wrap is what the original does.

   All four compares are the signed ones -- JL, JL, JGE, JL at 000159a4,
   000159b8, 000159cb and 000159e1 -- which is what keeps a slot that has
   scrolled off the top or the left of the page out of the drawn set instead of
   reading as a huge positive coordinate. */
#define RING_SLOT_X_MIN (-0x18)
#define RING_SLOT_X_MAX 0x14f
#define RING_SLOT_Y_MIN (-0x18)
#define RING_SLOT_Y_MAX 0xd7

/* A .CEL's sub-image offset table starts at byte 0x0f of the file and holds
   one dword per sub-image, each the distance from the file's own base to that
   sub-image's RLE stream. */
#define CEL_SUB_IMAGE_TABLE_OFFSET 0x0f
#define CEL_SUB_IMAGE_ENTRY_BYTES 4

/* Mode 0, the RLE pass-through kernel (blit.h). */
#define BLIT_MODE_OPAQUE 0

/* What fdps_battle_find_unit_at_cursor answers when the cursor tile is empty
   (unit.h), and the value the redraw below is gated on. */
#define NO_UNIT_AT_CURSOR (-1)

/* The frame's pacing, PUSH 0x2 / CALL delay at 00015b3f.  It is the only thing
   holding the open and close animations to a watchable speed; the drawing
   itself scales with the machine. */
#define RING_FRAME_DELAY_MS 2

/* 00015860.  One whole frame of the ring menu, composed on a private page and
   put on the screen.  See menu.h for the arguments.

   THE RESTING RING IS 23 PIXELS OUT, NOT 24.  At radius 0x18 the angle is
   24 * 0.06544979 = 1.5707949, a shade under pi/2, so the sine comes back
   0.999999999999 rather than 1 and the truncation toward zero that closes the
   conversion turns 23.99999999998 into 23.  The cosine leg does land on 0.
   Writing the offsets as a table of quarter turns, or rounding instead of
   truncating, moves all four buttons a pixel further out than the game puts
   them.  The truncation is the CALL __CHP / FISTP pair at 000158ad and
   000158b2, and it governs the whole sweep: at radius 0x19 the cosine leg is
   -1.635 and lands on -1, toward zero rather than down.

   The four offsets come out of ONE radius.  cos and sin of radius times the
   constant, each scaled by radius again, put slot 0 up and left of the cursor
   at (-dx, -dy), slot 1 at (-dy, +dx), slot 2 at (+dy, -dx) and slot 3 at
   (+dx, +dy) -- a quarter turn apart, and at the resting radius one tile up,
   left, right and down.  A smaller radius puts them on a spiral between the
   cursor and those resting places, which is the whole of the open and close
   animation.

   The cursor's position on the page is its world pixel minus the camera
   scroll, and both globals are re-read for every one of the eight coordinates
   rather than differenced once (000158df through 0001596c).

   cos and sin are ordinary library calls and not intrinsics: 00015891 through
   00015897 is SUB ESP,0x8 / FSTP qword ptr [ESP] / CALL with the double coming
   back in EDX:EAX, the stack-convention library form.  math.h in 10.0a carries
   an unguarded #pragma intrinsic(cos,sin,...), but that pragma only bites once
   the optimiser is on and this file is built at -od.

   THE UNIT UNDER THE CURSOR IS DRAWN TWICE, AND THE SECOND TIME IS WHY IT SITS
   ON TOP OF THE BUTTONS.  The compositor has already drawn it along with
   everything else on the map, underneath the plates; this second pass puts it
   back over them.  It paints the sprite rather than the shadow only because
   fdps_draw_map_units leaves data_fdps_map_unit_shadow_pass_flag at 0 on its
   way out (mapdraw.h), so moving the call above fdps_draw_scene_layers turns
   the unit whose menu this is into a shadow.  The literal 0 is
   fdps_draw_map_unit's own third argument, which that routine overwrites at
   entry and never reads.

   Nothing here is bounds checked: not cursor_dir, not the two descriptor
   arrays against the four slots, not the sub-image ids against the sheet, and
   not malloc against NULL. */
void fdps_render_ring_menu_frame(int *cmd_icons, int *cmd_disabled, int radius,
                                 int cursor_dir)
{
    /* The page the frame is composed on, allocated and freed here. */
    unsigned char *scene_page;
    /* Where each slot's plate goes, in page pixels before the 24-pixel apron
       is added on.  Two separate four-int arrays indexed by slot, which is
       what [EAX*0x4 + EBP - 0x30] and [EAX*0x4 + EBP - 0x40] are. */
    int slot_x[RING_SLOTS];
    int slot_y[RING_SLOTS];
    /* The page byte the current slot's two sub-images are blitted at. */
    unsigned char *slot_pixel;
    /* The sub-image's RLE stream inside the Command.cel image. */
    unsigned char *sprite_stream;
    /* Which unit is standing on the cursor tile, or -1 for none. */
    int unit_under_cursor;
    /* The ring's horizontal and vertical legs at this radius, both truncated
       toward zero. */
    int ring_offset_x;
    int ring_offset_y;
    /* The slot being placed, 0 up to 3 down. */
    int slot;
    /* The button plate's sub-image, and the icon's that goes over it. */
    int plate_sprite_index;
    int icon_sprite_index;
    /* Radians per pixel of radius, held in the frame as a float because that
       is what the FMUL at 0001588e reads. */
    float radians_per_pixel;

    radians_per_pixel = RING_RADIANS_PER_PIXEL;
    scene_page = (unsigned char *) malloc((size_t) SCENE_PAGE_BYTES);
    unit_under_cursor = fdps_battle_find_unit_at_cursor();

    ring_offset_x = (int) (cos(radius * radians_per_pixel) * radius);
    ring_offset_y = (int) (sin(radius * radians_per_pixel) * radius);

    /* Placed in the original's own order, slot 3 first and slot 2 last. */
    slot_x[3] = (data_fdps_map_cursor_world_x
                 - data_fdps_battle_view_window_origin_x) + ring_offset_x;
    slot_y[3] = (data_fdps_map_cursor_world_y
                 - data_fdps_battle_view_window_origin_y) + ring_offset_y;
    slot_x[0] = (data_fdps_map_cursor_world_x
                 - data_fdps_battle_view_window_origin_x) - ring_offset_x;
    slot_y[0] = (data_fdps_map_cursor_world_y
                 - data_fdps_battle_view_window_origin_y) - ring_offset_y;
    slot_x[1] = (data_fdps_map_cursor_world_x
                 - data_fdps_battle_view_window_origin_x) - ring_offset_y;
    slot_y[1] = (data_fdps_map_cursor_world_y
                 - data_fdps_battle_view_window_origin_y) + ring_offset_x;
    slot_x[2] = (data_fdps_map_cursor_world_x
                 - data_fdps_battle_view_window_origin_x) + ring_offset_y;
    slot_y[2] = (data_fdps_map_cursor_world_y
                 - data_fdps_battle_view_window_origin_y) - ring_offset_x;

    fdps_draw_scene_layers(scene_page);

    for (slot = 0; slot < RING_SLOTS; slot++) {
        if (slot_x[slot] >= RING_SLOT_X_MIN && slot_x[slot] < RING_SLOT_X_MAX
            && slot_y[slot] >= RING_SLOT_Y_MIN
            && slot_y[slot] < RING_SLOT_Y_MAX) {
            if (cursor_dir == slot) {
                plate_sprite_index = RING_PLATE_HIGHLIGHTED;
            } else {
                plate_sprite_index = RING_PLATE_PLAIN;
            }

            slot_pixel = scene_page
                + (slot_y[slot] + SCENE_PAGE_BORDER) * SCENE_PAGE_PITCH
                + SCENE_PAGE_BORDER + slot_x[slot];

            plate_sprite_index += cmd_disabled[slot];
            sprite_stream = data_fdps_command_sprite_sheet_ptr
                + *(int *) (data_fdps_command_sprite_sheet_ptr
                            + plate_sprite_index * CEL_SUB_IMAGE_ENTRY_BYTES
                            + CEL_SUB_IMAGE_TABLE_OFFSET);
            fdps_blit_dispatch(sprite_stream, slot_pixel, RING_PLATE_W,
                               RING_PLATE_H, SCENE_PAGE_PITCH, 0,
                               BLIT_MODE_OPAQUE);

            icon_sprite_index = cmd_icons[slot]
                + cmd_disabled[slot] * RING_DISABLED_ICON_BANK;
            sprite_stream = data_fdps_command_sprite_sheet_ptr
                + *(int *) (data_fdps_command_sprite_sheet_ptr
                            + icon_sprite_index * CEL_SUB_IMAGE_ENTRY_BYTES
                            + CEL_SUB_IMAGE_TABLE_OFFSET);
            fdps_blit_dispatch(sprite_stream, slot_pixel, RING_PLATE_W,
                               RING_PLATE_H, SCENE_PAGE_PITCH, 0,
                               BLIT_MODE_OPAQUE);
        }
    }

    if (unit_under_cursor != NO_UNIT_AT_CURSOR) {
        fdps_draw_map_unit(unit_under_cursor, scene_page, 0);
    }

    delay((unsigned int) RING_FRAME_DELAY_MS);
    fdps_blit_rect((unsigned int) (scene_page + SCENE_PAGE_WINDOW_AT),
                   SCENE_PAGE_PITCH,
                   (void *) (VGA_SCREEN_BASE + RING_WINDOW_AT),
                   VGA_SCREEN_PITCH, RING_WINDOW_W, RING_WINDOW_H);
    free(scene_page);
}

/* The window cue the opening sweep plays, PUSH 0x61584 / CALL fdps_play_sfx at
   0001613c.  The pack member it names is the one fdps_menu_animate_close plays
   as well; the program's second window sound, "OpWin1.wav" at 0x6159c, belongs
   to the status and shop windows and is never played from here.

   It has to be a plain writable literal.  The lookup inside fdps_play_sfx
   upper-cases the caller's own storage in place (vfs.h), so this spelling is
   permanently "OPWIN.WAV" after the first menu of the run, exactly as the
   original's copy at 0x61584 is (rebuild_info/pitfalls.md). */
#define RING_OPEN_SOUND "OpWin.wav"

/* The sweep's radius ladder, MOV dword ptr [EBP-0x4],0x1 at 0001614a, CMP
   against 0x18 / JL at 00016151 and ADD 0x4 at 00016159, then the two literal
   frames PUSH 0x18 at 0001617d and PUSH 0x19 at 00016193.

   THE LAST FRAME IS 0x19 AND NOT THE RESTING 0x18.  The loop stops one step
   short of the resting ring, the frame after it is the resting ring, and the
   frame after that is a pixel past it -- so the sweep overshoots and stays
   overshot until something else repaints the view.  Writing the ladder as a
   loop that simply runs up to and including the resting radius drops both the
   overshoot and the resting frame's own place in the order. */
#define RING_OPEN_FIRST_RADIUS 1
#define RING_OPEN_RADIUS_STEP 4
#define RING_OPEN_LOOP_LIMIT 0x18
#define RING_RESTING_RADIUS 0x18
#define RING_OPEN_OVERSHOOT_RADIUS 0x19

/* 00016130.  The opening animation of the four-command ring menu.  See menu.h
   for the arguments and for what the sweep leaves on the screen.

   The three descriptors are forwarded untouched to every frame -- each of the
   eight call sites reloads them straight out of the parameter slots
   ([EBP+0x14], [EBP+0x18] and [EBP+0x1c]) rather than caching them -- and
   nothing here indexes them, so this function never looks at what is in them.

   The cue is played once, before the first frame, and its result is dropped:
   fdps_play_sfx returns nothing and a member the pack does not hold is silence
   with no way to tell (audio.h), so a machine with sound effects off runs the
   same eight frames as one with them on.

   The pacing is fdps_render_ring_menu_frame's own delay(2), not anything here;
   this function's only contribution to how the animation looks is the ladder,
   and the ladder is eight frames whatever the machine. */
void fdps_menu_animate_open(int *cmd_icons, int *cmd_disabled, int cursor_dir)
{
    /* The ring's radius this frame, and with it the angle of the sweep. */
    int radius;

    fdps_play_sfx(RING_OPEN_SOUND);
    for (radius = RING_OPEN_FIRST_RADIUS; radius < RING_OPEN_LOOP_LIMIT;
         radius += RING_OPEN_RADIUS_STEP) {
        fdps_render_ring_menu_frame(cmd_icons, cmd_disabled, radius,
                                    cursor_dir);
    }
    fdps_render_ring_menu_frame(cmd_icons, cmd_disabled, RING_RESTING_RADIUS,
                                cursor_dir);
    fdps_render_ring_menu_frame(cmd_icons, cmd_disabled,
                                RING_OPEN_OVERSHOOT_RADIUS, cursor_dir);
}

/* The retraction's radius ladder, MOV dword ptr [EBP-0x4],0x17 at 000161ca,
   CMP against 0 / JG at 000161d1 and ADD -0x4 at 000161d9: six frames at 0x17,
   0x13, 0xf, 0xb, 7 and 3.

   The original's step instruction is ADD -0x4 and this file's is SUB 0x4.  That
   is not a spelling that can be chosen back: wcc386 10.0a here normalises both
   "radius -= 4" and "radius += -4" into the same SUB, measured on the object
   this file compiles to.  It is the same class of difference as the argument
   loads, where the original does MOV EAX / PUSH EAX and this build pushes the
   slot directly, in fdps_menu_animate_open as well as here.

   IT STARTS AT 0x17 AND NOT AT THE RESTING 0x18.  The first frame of the
   retraction is already a pixel in from where the menu was sitting, so the ring
   never redraws the resting radius on its way out.

   IT STOPS AT 3 AND DRAWS NOTHING AT RADIUS 0.  The test is the signed JG
   against zero, so 3 - 4 = -1 ends the loop and the four buttons are left
   standing three pixels off the cursor.  Writing the guard as "not equal to
   zero" never terminates, and stepping down to and including 0 draws a seventh
   frame with all four buttons stacked on the cursor itself. */
#define RING_CLOSE_FIRST_RADIUS 0x17
#define RING_CLOSE_RADIUS_STEP 4

/* 000161b0.  The closing animation of the four-command ring menu.  See menu.h
   for the arguments and for what is left on the screen afterwards.

   The cue is the same "OpWin.wav" the opening sweep plays, and it is the same
   copy: both functions push the one literal the original keeps at 0x61584, so
   the in-place upper-casing fdps_play_sfx does to the caller's storage (vfs.h)
   happens once and both spellings are "OPWIN.WAV" from the first menu of the
   run onward.  Its result is dropped, exactly as in the opening sweep.

   The three descriptors are forwarded untouched: all four call sites reload
   them out of the parameter slots ([EBP+0x14], [EBP+0x18] and [EBP+0x1c]) for
   every frame and nothing here indexes them, so this function never looks
   inside either array.

   NOTHING ERASES THE BUTTONS HERE.  The last frame is drawn at radius 3 and the
   function returns with it on the screen; what clears it is the caller's next
   repaint, and fdps_options_menu has no repaint of its own and leaves the
   stale buttons up until the next fdps_menu_animate_open recomposites the view.

   fdps_battle_system_menu does not call this function -- it carries the same
   six-frame ladder inline at 00014b17, without the cue -- so the top-level
   battle menu closes silently while these four close with the window sound. */
void fdps_menu_animate_close(int *cmd_icons, int *cmd_disabled, int cursor_dir)
{
    /* The ring's radius this frame, and with it the angle of the retraction. */
    int radius;

    fdps_play_sfx(RING_OPEN_SOUND);
    for (radius = RING_CLOSE_FIRST_RADIUS; radius > 0;
         radius -= RING_CLOSE_RADIUS_STEP) {
        fdps_render_ring_menu_frame(cmd_icons, cmd_disabled, radius,
                                    cursor_dir);
    }
}

/* The make codes the input loop acts on, straight off the CMP immediates at
   0001622a, 00016230, 00016242, 00016248, 0001625a, 00016275, 00016291 and
   000162ad.  They are set 1 scancodes as fdps_keyboard_isr queues them
   (keybd.h), not ASCII, so Enter is 0x1c and not '\r'.

   THE TWO CANCEL KEYS ARE ESCAPE AND KEYPAD DEL, AND THE TWO CONFIRM KEYS ARE
   ENTER AND SPACE.  0x53 is the keypad's Del/full-stop key and it cancels
   exactly as Escape does; the same pairing runs through the other modal
   readers in the game (keybd.h).  There is no arrow-key alternative for
   confirm and no separate "back" code. */
#define KEY_ESC 0x01
#define KEY_ENTER 0x1c
#define KEY_SPACE 0x39
#define KEY_KEYPAD_DEL 0x53
#define KEY_UP 0x48
#define KEY_LEFT 0x4b
#define KEY_RIGHT 0x4d
#define KEY_DOWN 0x50

/* The slot each arrow key moves the cursor to.  The descriptor's order is the
   one menu.h states -- 0 up, 1 left, 2 right, 3 down -- and the store is the
   plain literal in every arm (MOV dword ptr [EAX],0x0 / 0x3 / 0x1 / 0x2). */
#define RING_SLOT_UP 0
#define RING_SLOT_LEFT 1
#define RING_SLOT_RIGHT 2
#define RING_SLOT_DOWN 3

/* What the loop hands back: 0 means it has not finished, so it is also the
   value the loop tests to decide whether to run another pass. */
#define MENU_CURSOR_OPEN 0
#define MENU_CURSOR_CANCELLED (-1)
#define MENU_CURSOR_CONFIRMED 1

/* 00016200.  The modal loop that drives the ring menu's cursor.  See menu.h
   for what the arguments are and what the answer means.

   ONE SCANCODE PER FRAME, NOT ONE FRAME PER SCANCODE.  The pass reads the ring
   exactly once and acts on whatever came back, including the 0xff that means
   the queue was empty, and then repaints regardless.  So the loop is a frame
   loop paced by the vertical retrace fdps_cycle_ui_palette waits for and not
   an input loop that blocks: a player who touches nothing still has the scene
   palette cycled and the ring redrawn every retrace.  Ten keystrokes queued
   between two passes therefore take ten more frames to drain, one each, which
   is what makes a held arrow key walk the cursor at the ring's frame rate.

   The three calls at the bottom run on EVERY pass, the one that chose the
   answer included: the arms that set the result jump to 000162c7, which is the
   palette pair and the repaint, and only then is the loop test reached.  So a
   confirmed or cancelled menu is repainted once more before this returns, with
   the cursor on whatever slot the player last moved to.  Cutting that last
   repaint out -- returning from inside the branch -- leaves the previous
   frame's highlight on the screen for whatever the caller draws next.

   THE ORDER OF THE THREE IS PART OF THE PACE.  The scene cycle is handed to
   fdps_set_palette_range_on_retrace and does its own waiting inside; the UI
   cycle waits for the retrace unconditionally before it looks at anything
   (palcycle.h).  Swapping them, or dropping one on a pass that changed no
   colour, changes how long a frame takes on real hardware.

   A BLOCKED DIRECTION IS IGNORED, NOT CLAMPED.  cmd_disabled[slot] non-zero
   makes the arm fall through to the next comparison -- CMP dword ptr
   [EAX + n],0x0 / JZ to the store, otherwise the JMP past it -- and no other
   arm can match, so *cursor_dir keeps the value it had and the frame is drawn
   with the cursor where it already was.  It does not skip to the next
   selectable entry and it does not move part way.

   NOTHING HERE IS BOUNDS CHECKED AND NOTHING VALIDATES *cursor_dir ON ENTRY.
   The caller's starting value is passed to the first repaint untouched, so a
   caller that opens the menu on a greyed-out entry keeps that entry
   highlighted until the player moves off it; fdps_menu_find_first_enabled_entry
   is what the callers use to avoid that, and it is their call to make.

   cmd_icons is never read here.  It is loaded from its parameter slot and
   pushed straight to the repaint, which is the only thing this function does
   with it. */
int fdps_menu_cursor_input_loop(int *cmd_icons, int *cmd_disabled,
                                int *cursor_dir)
{
    /* Cancelled, confirmed, or still open.  It is both the loop's condition
       and the answer, which is why the loop cannot end on the pass that reads
       nothing. */
    int result;
    /* The make code this pass took out of the ring, widened from the byte
       fdps_read_keyboard_queue sets in AL -- the AND EAX,0xff at 00016222.
       0xff means the queue was empty and matches none of the eight codes. */
    int scancode;

    result = MENU_CURSOR_OPEN;
    while (result == MENU_CURSOR_OPEN) {
        scancode = fdps_read_keyboard_queue();

        if (scancode == KEY_ESC || scancode == KEY_KEYPAD_DEL) {
            result = MENU_CURSOR_CANCELLED;
        } else if (scancode == KEY_SPACE || scancode == KEY_ENTER) {
            result = MENU_CURSOR_CONFIRMED;
        } else if (scancode == KEY_UP && cmd_disabled[RING_SLOT_UP] == 0) {
            *cursor_dir = RING_SLOT_UP;
        } else if (scancode == KEY_DOWN && cmd_disabled[RING_SLOT_DOWN] == 0) {
            *cursor_dir = RING_SLOT_DOWN;
        } else if (scancode == KEY_LEFT && cmd_disabled[RING_SLOT_LEFT] == 0) {
            *cursor_dir = RING_SLOT_LEFT;
        } else if (scancode == KEY_RIGHT
                   && cmd_disabled[RING_SLOT_RIGHT] == 0) {
            *cursor_dir = RING_SLOT_RIGHT;
        }

        fdps_cycle_scene_palette();
        fdps_cycle_ui_palette();
        fdps_render_ring_menu_frame(cmd_icons, cmd_disabled,
                                    RING_RESTING_RADIUS, *cursor_dir);
    }

    return result;
}
