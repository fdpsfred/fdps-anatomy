/* title.c -- the front-of-house screens: the title menu and its demo, the
 * game-over screen, the ending credit roll, and the full-screen FMV playback
 * they reach for.
 *
 * See title.h for what each entry point promises its caller.  This file holds
 * no state of its own: everything it touches belongs to another module or to
 * the machine.
 *
 * sprintf comes from <stdio.h>, memset and memmove from <string.h>, kbhit,
 * getch and inp from <conio.h>, malloc and free from <stdlib.h> and spawnlp
 * from <process.h>, and every one of them is a real library call in the image
 * rather than an inline expansion -- CALL 00042d41, CALL 00042cd0, CALL
 * 00043570, CALL 00043587, CALL 000435a2, CALL 0003d375, CALL 0003d478, CALL
 * 0003d4e4 and CALL 0003d514 -- because the flag that would inline the string
 * and character routines, -oi, is not in this build's set
 * (rebuild_info/build_flags.md).  <stddef.h> is here for NULL, the
 * argument-list terminator spawnlp wants.
 *
 * The Miles library is reached only through its public entry AIL_shutdown; the
 * bring-up on the way back is audio.c's business and goes through
 * fdps_audio_init.  rebuild_info/ail_link.md has the linking contract.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <process.h>
#include "ailv3.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "blit.h"
#include "cdaudio.h"
#include "keybd.h"
#include "palette.h"
#include "saf.h"
#include "sprite.h"
#include "vfs.h"
#include "title.h"

/* The VGA graphics aperture as a flat linear address, the size of one whole
   mode 13h frame, and that frame's shape as the blitters address it -- 320
   bytes to the row and 200 rows, PUSH 0x140 / PUSH 0xc8 at 0002aa73 and
   0002aa6e.  All of them are hard-coded in the original (PUSH 0xa0000, PUSH
   0xfa00 at 00030ff2 and 00030ff9) and stay literals here: 0xa0000 is where
   the display adapter answers, not the address of anything the linker places,
   so there is no symbol to reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_BYTES 0xfa00
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit the game-over presenter looks at:
   TEST AL,0x8 against inp's answer at 0002aa59 and its three siblings. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The whole DAC, which is the range both palette uploads cover: PUSH 0x0 /
   PUSH 0xff for the first and last entry at 00030f76 and 00030f71, and again
   at 00031011 and 0003100c.  The bound is inclusive -- fdps_set_palette_range
   uploads first_entry..last_entry -- so the last entry is 255 and not 256. */
#define VGA_DAC_FIRST_ENTRY 0
#define VGA_DAC_LAST_ENTRY 0xff

/* The bias the fade-out upload applies to all three channels, PUSH -0x40 three
   times at 00030f6b..00030f71.  fdps_set_palette_range clamps each component
   into 0..63, and 64 is the whole of that range, so a single upload at this
   bias takes every entry to black however bright it was.  It is not a ramp and
   there is no loop: the picture is about to be replaced by the movie player's
   own screen, so there is nothing to fade gracefully into. */
#define MOVIE_FADE_TO_BLACK_BIAS (-64)

/* PUSH 0x19 at 00031026.  The tick rate the audio stack is brought back up at,
   the same 25 main@00029220 opens the session with. */
#define MOVIE_AUDIO_TICK_RATE_HZ 25

/* Three stack buffers of 20 bytes each -- the locals at [EBP-0x3c], [EBP-0x28]
   and [EBP-0x14] under a SUB ESP,0x3c, so they are adjacent and equal-sized.
   Nothing bounds what is formatted into them; see title.h for the margin the
   shipped paths leave. */
#define MOVIE_PATH_MAX 20

/* 00030f40.  Straight-line apart from one loop: CALL kbhit / TEST EAX,EAX / JZ
   out / CALL getch / JMP back at 00030f56..00030f64, the drain of whatever the
   player typed while the previous screen was up.  kbhit's EAX is the only value
   in the body that is used after a CALL -- it is tested for zero and nothing
   else -- and getch's is discarded, which is the point: the characters are
   being thrown away, not read.  spawnlp's result is discarded too (ADD ESP,0x18
   at 00030fef and then straight into the next PUSH, with no test of EAX
   anywhere), so a spawn that failed and a movie that played are the same thing
   to everything below it.

   The order matters and is the order the assembly has it in.  The audio stack
   goes down before the keyboard hook comes off, because AIL's timer is what
   would otherwise still be running over a machine the game no longer owns; the
   drain happens after the hook is off, so that it is the BIOS's queue being
   emptied and not the game's own ring; and the fade is the last thing before
   the spawn, so the player's screen arrives on a black DAC rather than on the
   palette of whatever was on screen.

   Coming back the order is mirrored but not symmetrical: the frame is cleared
   before the DAC is restored, so the unbiased palette lands on a screen that is
   already colour 0 and no frame of the movie's last image is ever shown under
   the game's own palette. */
void fdps_play_movie(char *movie_name)
{
    char movie_player_path[MOVIE_PATH_MAX];
    char video_stream_path[MOVIE_PATH_MAX];
    char audio_stream_path[MOVIE_PATH_MAX];

    AIL_shutdown();
    fdps_uninstall_keyboard_isr();

    while (kbhit() != 0) {
        getch();
    }

    fdps_cd_stop_audio();

    fdps_set_palette_range(
        (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
        VGA_DAC_FIRST_ENTRY, VGA_DAC_LAST_ENTRY,
        MOVIE_FADE_TO_BLACK_BIAS, MOVIE_FADE_TO_BLACK_BIAS,
        MOVIE_FADE_TO_BLACK_BIAS);

    sprintf(movie_player_path, "%s\\fd.exe", data_fdps_cdrom_path);
    sprintf(video_stream_path, "%s\\%s.Vid", data_fdps_cdrom_path, movie_name);
    sprintf(audio_stream_path, "%s\\%s.Aud", data_fdps_cdrom_path, movie_name);

    /* The player's own path is passed twice on purpose: once as the file to
       execute and once as argv[0], which is what the two identical LEA
       EAX,[EBP-0x3c] / PUSH EAX pairs at 00030fe0 and 00030fe4 are.  P_WAIT is
       the PUSH 0x0 at 00030fe8, so the game is suspended for as long as the
       movie runs. */
    spawnlp(P_WAIT, movie_player_path, movie_player_path, video_stream_path,
            audio_stream_path, NULL);

    memset((void *) VGA_SCREEN_BASE, 0, (size_t) VGA_SCREEN_BYTES);

    fdps_set_palette_range(
        (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
        VGA_DAC_FIRST_ENTRY, VGA_DAC_LAST_ENTRY, 0, 0, 0);

    fdps_install_keyboard_isr();
    fdps_audio_init(MOVIE_AUDIO_TICK_RATE_HZ);
}

/* --- fdps_show_game_over @ 0002a960 ------------------------------------- */

/* The container and the member, from PUSH 0x60128 / PUSH 0x61e68 at 0002a9ab
   and 0002a9a5.  The member name reaches fdps_vfs_load_entry, which
   upper-cases its argument IN PLACE before the compare, so this literal is
   permanently folded to GAMEOVER.SAF by the first call and cannot live in
   read-only storage (vfs.h, rebuild_info/pitfalls.md).  The container name is
   copied raw and is left as it stands. */
#define GAME_OVER_ARCHIVE "MISC.VFS"
#define GAME_OVER_MEMBER "GameOver.saf"

/* The off-screen page every frame is composed on and the window taken out of
   it: PUSH 0x16480 at 0002a9b7 for the block, MOV [EBP-0x4c],0x170 and MOV
   [EBP-0x48],0xf8 for its shape, MOV [EBP-0x44],0x18 and MOV [EBP-0x40],0x18
   for the drawing origin, and ADD EAX,0x2298 at 0002aa0f for the window.  The
   page is 368 by 248 with a 24-pixel margin on all four sides and the window
   is row 24, column 24 of it, which is byte 0x18 * 0x170 + 0x18; the two 24s
   cancel, so a frame layer carrying screen coordinates lands on those screen
   coordinates and a layer that hangs off an edge is drawn into the margin
   rather than clipped or wrapped.  It is the same page saf.h's player uses. */
#define OVER_PAGE_PITCH 0x170
#define OVER_PAGE_ROWS 0xf8
#define OVER_PAGE_BYTES 0x16480
#define OVER_PAGE_MARGIN 0x18
#define OVER_PAGE_WINDOW_AT 0x2298

/* THE CLIP OPENS ON FRAME 1, NOT FRAME 0.  MOV [EBP-0x2c],0x1 at 0002a9e9
   seeds the cursor's frame number directly and MOV [EBP-0x28],0x0 its tick
   count, where saf.h's own player instead calls fdps_saf_advance_tick with
   mode 1 and so opens on frame 0.  Frame 0 is not part of the animation here:
   it is the still picture the fade below brings up underneath it. */
#define OVER_FIRST_FRAME 1

/* The fade: MOV [EBP-0x4],0x0 with CMP against 0x10 / JL and INC at the
   bottom, so the step counter runs 0 up to 15 inclusive -- sixteen steps, the
   last of them at alpha 15 and never at 16.  The two things the step drives
   move in opposite directions.  The snapshot behind the picture is tinted
   toward palette colour 0x6f (PUSH 0x6f at 0002aae5) with the step itself as
   the alpha, so it starts untouched and ends all but replaced by the tint;
   the still picture over it is drawn translucent at level 16 - step, and that
   level runs 0 = opaque through 16 = invisible (rleblend.h), so the picture
   starts invisible and ends all but solid.  Handing either of them the other's
   sense plays the whole sequence backwards. */
#define OVER_FADE_STEPS 0x10
#define OVER_FADE_TINT_COLOR 0x6f
#define OVER_BLEND_LEVEL_INVISIBLE 0x10

/* The two entries the fade composes, written straight into the request's item
   index at 0002ab34 and 0002ab5b.  Entry 0 is the still picture that fades in;
   entry 0x1f is the last of GameOver.saf's 32 entries and is drawn opaque over
   the top of it at every step, so it never fades. */
#define OVER_PICTURE_FRAME 0
#define OVER_SIGN_FRAME 0x1f

/* The blit mode a translucent draw asks for and the three-dword descriptor it
   is handed, whose slots rleblend.h is the canon for: [0] the shade ramp base,
   [1] the blend level and [2] the inverse colour cube base.  Same names and
   same layout as sprite.c, which builds one of these per layer. */
#define BLIT_MODE_OPAQUE 0
#define BLIT_MODE_TRANSLUCENT 9
#define BLEND_DESC_SHADE_RAMP 0
#define BLEND_DESC_LEVEL 1
#define BLEND_DESC_CUBE 2
#define BLEND_DESC_DWORDS 3

/* 0002a960.  Runs the whole game-over screen: snapshots whatever is on the
   adapter, plays GameOver.saf over that snapshot once, fades the snapshot out
   under the still picture in sixteen steps, and then waits for a key before
   handing control back to main.  It returns -- CALL 0x0002a960 at 00029389 is
   followed by CALL 0x0002a2b0 and a JMP back into main's loop -- so the two
   keyboard calls at the end are a pause and not an exit.

   VALUES USED AFTER A CALL, and what the assembly says each one is.  Four
   CALLs hand something back.  malloc's EAX at 0002a984 becomes the snapshot
   buffer and is re-read at 0002a994, 0002aa1a, 0002ab10 and 0002abf0;
   fdps_vfs_load_entry's EAX at 0002a9b4 becomes the .SAF image and is copied
   into both the request and the cursor at 0002a9e6 and 0002a9fa; the second
   malloc's EAX at 0002a9c4 goes straight into the request's element 0 and is
   re-read out of that slot every time the page is needed; and
   fdps_saf_advance_tick's EAX at 0002aaba is stored and then tested against 0
   at 0002aabd, which is the playback loop's only exit.  inp's answer is used
   four times, each as the TEST AL,0x8 of the retrace spin it was fetched for.
   memmove's and free's results are discarded and every other callee returns
   void.

   CONTROL FLOW.  Straight line to 0002a9fd, then two loops.  The playback loop
   is BOTTOM-tested -- there is no compare above 0002a9fd and the only branch
   back is CMP [EBP-0x10],0x0 / JZ at 0002aabd -- so at least one frame is
   always drawn and presented, even for an image fdps_saf_advance_tick refuses
   outright.  saf.h's player tests at the top instead and can draw nothing at
   all.  The fade is a plain counted loop with its increment at the bottom, and
   its bound is signed: CMP dword ptr [EBP-0x4],0x10 / JL.

   THE TICK LATCH IS DELIBERATELY NOT INITIALISED, exactly as in
   fdps_saf_play_over_background: [EBP-0xc] is read at 0002aa98 before anything
   has written it, so the first frame goes up and moves on without waiting for
   a tick.  Seeding it adds one tick to the sequence
   (rebuild_info/pitfalls.md).

   BOTH LOOPS PRESENT ON THE RETRACE AND PACE ON THE TICK, and the two are not
   the same wait.  The retrace pair waits for the retrace to begin and then for
   it to end, and only then is the page copied to the adapter, so the copy runs
   inside the displayed part of the frame.  The tick wait comes after the copy
   and is what sets the sequence's speed.

   THE SNAPSHOT IS TAKEN ONCE AND IS THE SOURCE EVERY TIME.  Both loops repaint
   the page from it before they draw anything, so the animation composites over
   a still picture rather than over its own previous frame, and each fade step
   tints the original screen rather than the tinted result of the step before
   it.  Reading 0xa0000 back instead at either point would compound.

   The two palette tables are addressed as symbols and not as the 0x653f0 and
   0x643f0 the original's linker gave them; the rebuild does not place anything
   where the original placed it (rebuild_info/pitfalls.md).  All three
   allocations are this function's own and all three are released before it
   waits for the key. */
void fdps_show_game_over(void)
{
    /* The nine-dword draw request sprite.h describes.  Built once, then
       rewritten in place: the playback loop moves only its item index, and the
       fade moves its item index, its blit mode and its mode operand. */
    int request[DRAW_REQUEST_DWORDS];
    /* The three-dword playback cursor saf.h describes, seeded here rather than
       reset through fdps_saf_advance_tick -- see OVER_FIRST_FRAME above. */
    int playback_cursor[SAF_CURSOR_DWORDS];
    /* The three-dword mode-9 descriptor rleblend.h describes.  Its two table
       pointers are filled before anything else in the body and never move
       again; only the level changes, once per fade step. */
    int blend_descriptor[BLEND_DESC_DWORDS];
    /* The 320x200 picture that was on the adapter when the call was made. */
    unsigned char *screen_snapshot;
    /* GameOver.saf.  This function owns it and frees it. */
    void *game_over_bank;
    /* The 368x248 page every frame is composed on, so that nothing is ever
       seen half-drawn.  The assembly keeps it only in the request's element 0
       and re-reads it from there; the name is here for the reader. */
    unsigned char *work_page;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;
    /* fdps_saf_advance_tick's answer: 0 while the clip is still running, and 1
       on the tick that steps past its last frame. */
    int clip_ended;
    /* How far the fade has gone, 0 through 15. */
    int fade_step;

    blend_descriptor[BLEND_DESC_SHADE_RAMP] =
        (int) data_fdps_palette_shade_ramp_table;
    blend_descriptor[BLEND_DESC_CUBE] = (int) data_fdps_inverse_palette_cube;

    screen_snapshot = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    memmove(screen_snapshot, (void *) VGA_SCREEN_BASE,
            (size_t) VGA_SCREEN_BYTES);

    game_over_bank = fdps_vfs_load_entry(GAME_OVER_ARCHIVE, GAME_OVER_MEMBER);

    work_page = (unsigned char *) malloc((size_t) OVER_PAGE_BYTES);
    request[DRAW_REQUEST_DEST_BASE] = (int) work_page;
    request[DRAW_REQUEST_DEST_PITCH] = OVER_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = OVER_PAGE_ROWS;
    request[DRAW_REQUEST_X] = OVER_PAGE_MARGIN;
    request[DRAW_REQUEST_Y] = OVER_PAGE_MARGIN;
    request[DRAW_REQUEST_IMAGE] = (int) game_over_bank;

    playback_cursor[SAF_CURSOR_FRAME_INDEX] = OVER_FIRST_FRAME;
    playback_cursor[SAF_CURSOR_TICKS_HELD] = 0;
    playback_cursor[SAF_CURSOR_IMAGE] = (int) game_over_bank;

    do {
        fdps_blit_rect((unsigned int) screen_snapshot, VGA_SCREEN_PITCH,
                       work_page + OVER_PAGE_WINDOW_AT, OVER_PAGE_PITCH,
                       VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);

        request[DRAW_REQUEST_BLIT_OPERAND] = 0;
        request[DRAW_REQUEST_BLIT_MODE] = BLIT_MODE_OPAQUE;
        request[DRAW_REQUEST_ITEM_INDEX] =
            playback_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 1);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the present runs inside the displayed part
               of the frame. */
        }

        fdps_blit_rect((unsigned int) (work_page + OVER_PAGE_WINDOW_AT),
                       OVER_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);

        while (last_tick == data_fdps_timer_tick_counter) {
            /* spin: only the timer interrupt can end this */
        }
        last_tick = data_fdps_timer_tick_counter;

        clip_ended = fdps_saf_advance_tick(playback_cursor, 0);
    } while (clip_ended == 0);

    for (fade_step = 0; fade_step < OVER_FADE_STEPS; fade_step++) {
        fdps_blit_tint_rect(screen_snapshot, VGA_SCREEN_PITCH,
                            work_page + OVER_PAGE_WINDOW_AT, OVER_PAGE_PITCH,
                            VGA_SCREEN_PITCH, VGA_SCREEN_ROWS,
                            data_fdps_palette_shade_ramp_table,
                            data_fdps_inverse_palette_cube,
                            OVER_FADE_TINT_COLOR, fade_step);

        blend_descriptor[BLEND_DESC_LEVEL] =
            OVER_BLEND_LEVEL_INVISIBLE - fade_step;
        request[DRAW_REQUEST_BLIT_OPERAND] = (int) blend_descriptor;
        request[DRAW_REQUEST_BLIT_MODE] = BLIT_MODE_TRANSLUCENT;
        request[DRAW_REQUEST_ITEM_INDEX] = OVER_PICTURE_FRAME;
        fdps_draw_composite_sprite(request, 1);

        request[DRAW_REQUEST_BLIT_OPERAND] = 0;
        request[DRAW_REQUEST_BLIT_MODE] = BLIT_MODE_OPAQUE;
        request[DRAW_REQUEST_ITEM_INDEX] = OVER_SIGN_FRAME;
        fdps_draw_composite_sprite(request, 1);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* The same pair of spins, for the same reason. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }

        fdps_blit_rect((unsigned int) (work_page + OVER_PAGE_WINDOW_AT),
                       OVER_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);

        while (last_tick == data_fdps_timer_tick_counter) {
            /* spin: only the timer interrupt can end this */
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    free(work_page);
    free(game_over_bank);
    free(screen_snapshot);

    /* The picture is left standing on the adapter: nothing here clears it, and
       the caller draws over it next.  The queue is emptied first so that a key
       pressed during the fade is not taken as the key that dismisses the
       screen, and only then does the wait begin (keybd.h). */
    fdps_flush_keyboard_queue();
    fdps_wait_any_key();
}
