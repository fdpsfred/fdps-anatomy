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
#include "mapdraw.h"
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
